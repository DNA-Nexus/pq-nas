#include "cluster/internal_auth.h"

#include <sodium.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace pqnas::cluster {
namespace {

constexpr std::size_t kMaxCredentialFileBytes = 128;
constexpr char kBearerPrefix[] = "Bearer ";
constexpr std::size_t kBearerPrefixLength = sizeof(kBearerPrefix) - 1;

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}

    ~UniqueFd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    int get() const noexcept {
        return fd_;
    }

private:
    int fd_;
};

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(std::string& value) noexcept : value_(value) {}

    ~SensitiveStringWiper() {
        if (!value_.empty()) {
            sodium_memzero(value_.data(), value_.size());
        }
    }

    SensitiveStringWiper(const SensitiveStringWiper&) = delete;
    SensitiveStringWiper& operator=(const SensitiveStringWiper&) = delete;

private:
    std::string& value_;
};

template <std::size_t N>
class SensitiveArrayWiper {
public:
    explicit SensitiveArrayWiper(std::array<unsigned char, N>& value) noexcept
        : value_(value) {}

    ~SensitiveArrayWiper() {
        sodium_memzero(value_.data(), value_.size());
    }

    SensitiveArrayWiper(const SensitiveArrayWiper&) = delete;
    SensitiveArrayWiper& operator=(const SensitiveArrayWiper&) = delete;

private:
    std::array<unsigned char, N>& value_;
};

void set_error(std::string* err, const std::string& message) {
    if (err) {
        *err = message;
    }
}

bool valid_base64url_token_text(const char* data, std::size_t size) {
    if (!data || size != kInternalAuthTokenChars) {
        return false;
    }

    for (std::size_t i = 0; i < size; ++i) {
        const unsigned char ch = static_cast<unsigned char>(data[i]);

        const bool allowed =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' ||
            ch == '_';

        if (!allowed) {
            return false;
        }
    }

    return true;
}

bool decode_token(
    const char* token,
    std::size_t token_size,
    std::array<unsigned char, kInternalAuthSecretBytes>* out
) {
    if (!out || !valid_base64url_token_text(token, token_size)) {
        return false;
    }

    std::size_t decoded_size = 0;
    const char* decode_end = nullptr;

    const int rc = sodium_base642bin(
        out->data(),
        out->size(),
        token,
        token_size,
        nullptr,
        &decoded_size,
        &decode_end,
        sodium_base64_VARIANT_URLSAFE_NO_PADDING
    );

    return rc == 0 &&
           decoded_size == out->size() &&
           decode_end == token + token_size;
}

bool read_bounded_file(
    int fd,
    std::string* out,
    std::string* err
) {
    if (!out) {
        set_error(err, "missing credential output buffer");
        return false;
    }

    std::array<char, kMaxCredentialFileBytes + 1> buffer{};
    std::size_t total = 0;

    while (total < buffer.size()) {
        const ssize_t count = ::read(
            fd,
            buffer.data() + total,
            buffer.size() - total
        );

        if (count > 0) {
            total += static_cast<std::size_t>(count);
            continue;
        }

        if (count == 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        set_error(err, "failed to read internal credential file");
        return false;
    }

    if (total > kMaxCredentialFileBytes) {
        set_error(err, "internal credential file is too large");
        return false;
    }

    out->assign(buffer.data(), total);
    return true;
}

} // namespace

InternalAuthCredential::~InternalAuthCredential() {
    clear();
}

InternalAuthCredential::InternalAuthCredential(
    InternalAuthCredential&& other
) noexcept {
    secret_ = other.secret_;
    configured_ = other.configured_;
    other.clear();
}

InternalAuthCredential& InternalAuthCredential::operator=(
    InternalAuthCredential&& other
) noexcept {
    if (this == &other) {
        return *this;
    }

    clear();
    secret_ = other.secret_;
    configured_ = other.configured_;
    other.clear();
    return *this;
}

bool InternalAuthCredential::configured() const noexcept {
    return configured_;
}

void InternalAuthCredential::clear() noexcept {
    sodium_memzero(secret_.data(), secret_.size());
    configured_ = false;
}

bool load_internal_auth_credential_file(
    const std::string& path,
    InternalAuthCredential* out,
    std::string* err
) {
    if (err) {
        err->clear();
    }

    if (!out) {
        set_error(err, "missing credential output");
        return false;
    }

    if (path.empty()) {
        set_error(err, "internal credential file path is empty");
        return false;
    }

    // Security: O_NOFOLLOW prevents replacing the configured credential file
    // with a symlink to another readable file.
    const int raw_fd = ::open(
        path.c_str(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW
    );

    if (raw_fd < 0) {
        if (errno == ELOOP) {
            set_error(err, "internal credential file must not be a symlink");
        } else {
            set_error(err, "failed to open internal credential file");
        }
        return false;
    }

    UniqueFd fd(raw_fd);

    struct stat st {};
    if (::fstat(fd.get(), &st) != 0) {
        set_error(err, "failed to inspect internal credential file");
        return false;
    }

    if (!S_ISREG(st.st_mode)) {
        set_error(err, "internal credential file must be a regular file");
        return false;
    }

    // Security: cluster credentials must never be readable or writable by
    // group or other users.
    if ((st.st_mode & 0077) != 0) {
        set_error(
            err,
            "internal credential file permissions must not allow group or other access"
        );
        return false;
    }

    const uid_t effective_uid = ::geteuid();
    if (st.st_uid != effective_uid && st.st_uid != 0) {
        set_error(
            err,
            "internal credential file must be owned by root or the service user"
        );
        return false;
    }

    if (st.st_size < 0 ||
        static_cast<unsigned long long>(st.st_size) >
            static_cast<unsigned long long>(kMaxCredentialFileBytes)) {
        set_error(err, "internal credential file has an invalid size");
        return false;
    }

    std::string token_text;
    SensitiveStringWiper token_wiper(token_text);

    if (!read_bounded_file(fd.get(), &token_text, err)) {
        return false;
    }

    if (!token_text.empty() && token_text.back() == '\n') {
        token_text.pop_back();

        if (!token_text.empty() && token_text.back() == '\r') {
            token_text.pop_back();
        }
    }

    if (token_text.find('\n') != std::string::npos ||
        token_text.find('\r') != std::string::npos) {
        set_error(err, "internal credential file must contain exactly one token");
        return false;
    }

    std::array<unsigned char, kInternalAuthSecretBytes> decoded{};
    SensitiveArrayWiper<kInternalAuthSecretBytes> decoded_wiper(decoded);

    if (!decode_token(token_text.data(), token_text.size(), &decoded)) {
        set_error(
            err,
            "internal credential must be a 32-byte base64url token without padding"
        );
        return false;
    }

    // Replace only after all validation has succeeded, preserving the previous
    // credential if loading a replacement fails.
    out->clear();
    out->secret_ = decoded;
    out->configured_ = true;
    return true;
}

bool verify_internal_bearer_authorization(
    const std::string& authorization_header,
    const InternalAuthCredential& credential
) {
    if (!credential.configured_) {
        return false;
    }

    if (authorization_header.size() !=
        kBearerPrefixLength + kInternalAuthTokenChars) {
        return false;
    }

    if (authorization_header.compare(
            0,
            kBearerPrefixLength,
            kBearerPrefix) != 0) {
        return false;
    }

    const char* token =
        authorization_header.data() + kBearerPrefixLength;

    std::array<unsigned char, kInternalAuthSecretBytes> decoded{};
    SensitiveArrayWiper<kInternalAuthSecretBytes> decoded_wiper(decoded);

    if (!decode_token(token, kInternalAuthTokenChars, &decoded)) {
        return false;
    }

    return sodium_memcmp(
        decoded.data(),
        credential.secret_.data(),
        credential.secret_.size()
    ) == 0;
}

} // namespace pqnas::cluster
