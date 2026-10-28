#include "cluster/node_enrollment_token.h"

#include <sodium.h>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

inline constexpr std::size_t
    kEncodedBufferChars =
        sodium_base64_ENCODED_LEN(
            kNodeEnrollmentTokenBytes,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        );

static_assert(
    kEncodedBufferChars ==
        kNodeEnrollmentTokenChars + 1,
    "unexpected libsodium base64url size"
);

using RawToken = std::array<
    unsigned char,
    kNodeEnrollmentTokenBytes
>;

using TokenHash = std::array<
    unsigned char,
    kNodeEnrollmentTokenHashBytes
>;

using EncodedBuffer = std::array<
    char,
    kEncodedBufferChars
>;

template <typename Array>
class SensitiveArrayWiper {
public:
    explicit SensitiveArrayWiper(
        Array& value
    ) noexcept
        : value_(value) {}

    ~SensitiveArrayWiper() {
        sodium_memzero(
            value_.data(),
            value_.size()
        );
    }

    SensitiveArrayWiper(
        const SensitiveArrayWiper&
    ) = delete;

    SensitiveArrayWiper& operator=(
        const SensitiveArrayWiper&
    ) = delete;

private:
    Array& value_;
};

bool sodium_ready() noexcept {
    return sodium_init() >= 0;
}

bool valid_base64url_text(
    const std::string& value,
    std::size_t expected_size
) noexcept {
    if (value.size() != expected_size) {
        return false;
    }

    for (unsigned char ch : value) {
        const bool alphanumeric =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9');

        if (!alphanumeric &&
            ch != '-' &&
            ch != '_') {
            return false;
        }
    }

    return true;
}

bool encode_raw_32(
    const unsigned char* raw,
    std::size_t raw_size,
    EncodedBuffer* output
) noexcept {
    if (!raw ||
        !output ||
        raw_size != kNodeEnrollmentTokenBytes) {
        return false;
    }

    output->fill('\0');

    const char* result =
        sodium_bin2base64(
            output->data(),
            output->size(),
            raw,
            raw_size,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        );

    return
        result != nullptr &&
        (*output)[
            kNodeEnrollmentTokenChars
        ] == '\0';
}

bool decode_canonical_raw_32(
    const std::string& value,
    RawToken* output
) noexcept {
    if (!output ||
        !valid_base64url_text(
            value,
            kNodeEnrollmentTokenChars) ||
        !sodium_ready()) {
        return false;
    }

    RawToken decoded{};
    SensitiveArrayWiper<RawToken>
        decoded_wiper(decoded);

    std::size_t decoded_size = 0;
    const char* decode_end = nullptr;

    const int decode_result =
        sodium_base642bin(
            decoded.data(),
            decoded.size(),
            value.data(),
            value.size(),
            nullptr,
            &decoded_size,
            &decode_end,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        );

    EncodedBuffer canonical{};
    SensitiveArrayWiper<EncodedBuffer>
        canonical_wiper(canonical);

    const bool canonical_encoding =
        decode_result == 0 &&
        decoded_size == decoded.size() &&
        decode_end ==
            value.data() + value.size() &&
        encode_raw_32(
            decoded.data(),
            decoded.size(),
            &canonical) &&
        sodium_memcmp(
            canonical.data(),
            value.data(),
            kNodeEnrollmentTokenChars
        ) == 0;

    if (!canonical_encoding) {
        return false;
    }

    *output = decoded;
    return true;
}

bool hash_raw_token(
    const RawToken& raw,
    std::string* output_hash
) {
    if (!output_hash) {
        return false;
    }

    TokenHash digest{};
    SensitiveArrayWiper<TokenHash>
        digest_wiper(digest);

    if (crypto_hash_sha256(
            digest.data(),
            raw.data(),
            raw.size()) != 0) {
        return false;
    }

    EncodedBuffer encoded{};
    SensitiveArrayWiper<EncodedBuffer>
        encoded_wiper(encoded);

    if (!encode_raw_32(
            digest.data(),
            digest.size(),
            &encoded)) {
        return false;
    }

    try {
        output_hash->assign(
            encoded.data(),
            kNodeEnrollmentTokenHashChars
        );
    } catch (...) {
        output_hash->clear();
        return false;
    }

    return true;
}

} // namespace

const char* node_enrollment_token_result_code(
    NodeEnrollmentTokenResult result
) {
    switch (result) {
        case NodeEnrollmentTokenResult::Ok:
            return "ok";

        case NodeEnrollmentTokenResult::
                InvalidInput:
            return "enrollment_token_invalid";

        case NodeEnrollmentTokenResult::
                CryptoUnavailable:
            return "enrollment_token_crypto_unavailable";

        case NodeEnrollmentTokenResult::
                CryptoFailed:
            return "enrollment_token_crypto_failed";

        case NodeEnrollmentTokenResult::
                EncodingFailed:
            return "enrollment_token_encoding_failed";
    }

    return "enrollment_token_invalid";
}

void clear_node_enrollment_token_text(
    std::string* value
) noexcept {
    if (!value) {
        return;
    }

    if (!value->empty()) {
        sodium_memzero(
            value->data(),
            value->size()
        );
    }

    value->clear();
}

NodeEnrollmentTokenMaterial::
~NodeEnrollmentTokenMaterial() {
    clear();
}

NodeEnrollmentTokenMaterial::
NodeEnrollmentTokenMaterial(
    NodeEnrollmentTokenMaterial&& other
) noexcept
    : token_(std::move(other.token_)),
      token_hash_(
          std::move(other.token_hash_)),
      configured_(other.configured_) {
    other.clear();
}

NodeEnrollmentTokenMaterial&
NodeEnrollmentTokenMaterial::operator=(
    NodeEnrollmentTokenMaterial&& other
) noexcept {
    if (this == &other) {
        return *this;
    }

    clear();

    token_ = std::move(other.token_);
    token_hash_ =
        std::move(other.token_hash_);

    configured_ = other.configured_;
    other.clear();

    return *this;
}

bool NodeEnrollmentTokenMaterial::
configured() const noexcept {
    return configured_;
}

const std::string&
NodeEnrollmentTokenMaterial::
token_hash() const noexcept {
    return token_hash_;
}

bool NodeEnrollmentTokenMaterial::take_token(
    std::string* output
) noexcept {
    if (!output ||
        !configured_ ||
        token_.size() !=
            kNodeEnrollmentTokenChars) {
        return false;
    }

    clear_node_enrollment_token_text(
        output
    );

    output->swap(token_);

    clear_node_enrollment_token_text(
        &token_
    );

    token_hash_.clear();
    configured_ = false;
    return true;
}

void NodeEnrollmentTokenMaterial::clear() noexcept {
    clear_node_enrollment_token_text(
        &token_
    );

    if (!token_hash_.empty()) {
        sodium_memzero(
            token_hash_.data(),
            token_hash_.size()
        );

        token_hash_.clear();
    }

    configured_ = false;
}

NodeEnrollmentTokenResult
generate_node_enrollment_token(
    NodeEnrollmentTokenMaterial* output
) {
    if (!output) {
        return NodeEnrollmentTokenResult::
            InvalidInput;
    }

    output->clear();

    if (!sodium_ready()) {
        return NodeEnrollmentTokenResult::
            CryptoUnavailable;
    }

    RawToken raw{};
    SensitiveArrayWiper<RawToken>
        raw_wiper(raw);

    TokenHash digest{};
    SensitiveArrayWiper<TokenHash>
        digest_wiper(digest);

    EncodedBuffer encoded_token{};
    SensitiveArrayWiper<EncodedBuffer>
        token_wiper(encoded_token);

    EncodedBuffer encoded_hash{};
    SensitiveArrayWiper<EncodedBuffer>
        hash_wiper(encoded_hash);

    randombytes_buf(
        raw.data(),
        raw.size()
    );

    if (crypto_hash_sha256(
            digest.data(),
            raw.data(),
            raw.size()) != 0) {
        return NodeEnrollmentTokenResult::
            CryptoFailed;
    }

    if (!encode_raw_32(
            raw.data(),
            raw.size(),
            &encoded_token) ||
        !encode_raw_32(
            digest.data(),
            digest.size(),
            &encoded_hash)) {
        return NodeEnrollmentTokenResult::
            EncodingFailed;
    }

    NodeEnrollmentTokenMaterial candidate;

    try {
        candidate.token_.assign(
            encoded_token.data(),
            kNodeEnrollmentTokenChars
        );

        candidate.token_hash_.assign(
            encoded_hash.data(),
            kNodeEnrollmentTokenHashChars
        );
    } catch (...) {
        candidate.clear();

        return NodeEnrollmentTokenResult::
            EncodingFailed;
    }

    candidate.configured_ = true;
    *output = std::move(candidate);

    return NodeEnrollmentTokenResult::Ok;
}

NodeEnrollmentTokenResult
hash_node_enrollment_token(
    const std::string& token,
    std::string* output_hash
) {
    if (!output_hash) {
        return NodeEnrollmentTokenResult::
            InvalidInput;
    }

    if (!output_hash->empty()) {
        sodium_memzero(
            output_hash->data(),
            output_hash->size()
        );

        output_hash->clear();
    }

    if (!sodium_ready()) {
        return NodeEnrollmentTokenResult::
            CryptoUnavailable;
    }

    RawToken raw{};
    SensitiveArrayWiper<RawToken>
        raw_wiper(raw);

    if (!decode_canonical_raw_32(
            token,
            &raw)) {
        return NodeEnrollmentTokenResult::
            InvalidInput;
    }

    if (!hash_raw_token(
            raw,
            output_hash)) {
        return NodeEnrollmentTokenResult::
            CryptoFailed;
    }

    return NodeEnrollmentTokenResult::Ok;
}

bool valid_node_enrollment_token_hash(
    const std::string& value
) noexcept {
    RawToken decoded{};
    SensitiveArrayWiper<RawToken>
        decoded_wiper(decoded);

    return decode_canonical_raw_32(
        value,
        &decoded
    );
}

} // namespace pqnas::cluster
