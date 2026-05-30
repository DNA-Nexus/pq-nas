#include "federation/circle_federation_signing.h"

#include <sodium.h>
#include <openssl/evp.h>

#include <array>
#include <vector>
#include <utility>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>

namespace pqnas::federation {
namespace {

std::string trim_copy(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;

    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;

    return s.substr(a, b - a);
}

std::filesystem::path signing_public_key_path(const std::string& identity_dir) {
    return std::filesystem::path(identity_dir) / "federation_signing_ed25519.pk";
}

std::filesystem::path signing_secret_key_path(const std::string& identity_dir) {
    return std::filesystem::path(identity_dir) / "federation_signing_ed25519.sk";
}

bool read_text_file_trimmed(
    const std::filesystem::path& path,
    std::string* out,
    std::string* err) {
    if (out) out->clear();

    std::ifstream f(path);
    if (!f) {
        if (err) *err = "failed to open " + path.string();
        return false;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    if (out) *out = trim_copy(ss.str());
    return true;
}

bool write_private_text_file(
    const std::filesystem::path& path,
    const std::string& content,
    std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (err) *err = "failed to create signing key directory: " + ec.message();
        return false;
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (err) *err = "failed to write " + path.string();
        return false;
    }

    f << content << "\n";
    f.close();

    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        ec);
    ec.clear();

    return true;
}

std::string b64_encode(const unsigned char* data, std::size_t len) {
    const std::size_t out_len =
        sodium_base64_encoded_len(len, sodium_base64_VARIANT_ORIGINAL);

    std::string out(out_len, '\0');
    sodium_bin2base64(
        out.data(),
        out.size(),
        data,
        len,
        sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

bool b64_decode_exact(
    const std::string& b64,
    std::size_t expected_len,
    std::vector<unsigned char>* out,
    std::string* err) {
    if (out) out->clear();

    std::vector<unsigned char> tmp(expected_len);
    std::size_t actual_len = 0;

    const int rc = sodium_base642bin(
        tmp.data(),
        tmp.size(),
        b64.c_str(),
        b64.size(),
        nullptr,
        &actual_len,
        nullptr,
        sodium_base64_VARIANT_ORIGINAL);

    if (rc != 0 || actual_len != expected_len) {
        if (err) *err = "invalid base64 key/signature size";
        return false;
    }

    if (out) *out = std::move(tmp);
    return true;
}

std::string hex_lower(const unsigned char* data, std::size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";

    std::string out;
    out.resize(len * 2);

    for (std::size_t i = 0; i < len; ++i) {
        out[i * 2] = kHex[(data[i] >> 4) & 0x0f];
        out[i * 2 + 1] = kHex[data[i] & 0x0f];
    }

    return out;
}

bool sha3_512_hex(
    const unsigned char* data,
    std::size_t len,
    std::string* out_hex,
    std::string* err) {
    if (out_hex) out_hex->clear();

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        if (err) *err = "EVP_MD_CTX_new failed";
        return false;
    }

    unsigned char digest[64];
    unsigned int digest_len = 0;

    const bool ok =
        EVP_DigestInit_ex(ctx, EVP_sha3_512(), nullptr) == 1 &&
        EVP_DigestUpdate(ctx, data, len) == 1 &&
        EVP_DigestFinal_ex(ctx, digest, &digest_len) == 1 &&
        digest_len == sizeof(digest);

    EVP_MD_CTX_free(ctx);

    if (!ok) {
        if (err) *err = "sha3-512 failed";
        return false;
    }

    if (out_hex) *out_hex = hex_lower(digest, sizeof(digest));
    return true;
}

bool ensure_sodium(std::string* err) {
    if (sodium_init() < 0) {
        if (err) *err = "sodium_init failed";
        return false;
    }
    return true;
}

} // namespace

std::string circle_federation_signing_public_key_fingerprint(
    const std::string& public_key_b64) {
    std::string err;
    std::vector<unsigned char> pk;

    if (!ensure_sodium(&err) ||
        !b64_decode_exact(
            public_key_b64,
            crypto_sign_PUBLICKEYBYTES,
            &pk,
            &err)) {
        return "";
    }

    std::string fp;
    if (!sha3_512_hex(pk.data(), pk.size(), &fp, &err)) {
        return "";
    }

    return fp;
}

bool ensure_circle_federation_signing_identity(
    const std::string& identity_dir,
    CircleFederationSigningIdentity* out_identity,
    std::string* err) {
    if (out_identity) *out_identity = {};

    if (identity_dir.empty()) {
        if (err) *err = "identity_dir is empty";
        return false;
    }

    if (!ensure_sodium(err)) return false;

    const auto pk_path = signing_public_key_path(identity_dir);
    const auto sk_path = signing_secret_key_path(identity_dir);

    std::error_code ec;
    const bool has_pk = std::filesystem::exists(pk_path, ec) && !ec;
    ec.clear();
    const bool has_sk = std::filesystem::exists(sk_path, ec) && !ec;

    if (!has_pk || !has_sk) {
        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> pk{};
        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> sk{};

        crypto_sign_keypair(pk.data(), sk.data());

        if (!write_private_text_file(pk_path, b64_encode(pk.data(), pk.size()), err)) {
            return false;
        }
        if (!write_private_text_file(sk_path, b64_encode(sk.data(), sk.size()), err)) {
            return false;
        }
    }

    return load_circle_federation_signing_identity(identity_dir, out_identity, err);
}

bool load_circle_federation_signing_identity(
    const std::string& identity_dir,
    CircleFederationSigningIdentity* out_identity,
    std::string* err) {
    if (out_identity) *out_identity = {};

    std::string pk_b64;
    if (!read_text_file_trimmed(signing_public_key_path(identity_dir), &pk_b64, err)) {
        return false;
    }

    std::vector<unsigned char> pk;
    if (!ensure_sodium(err) ||
        !b64_decode_exact(
            pk_b64,
            crypto_sign_PUBLICKEYBYTES,
            &pk,
            err)) {
        return false;
    }

    std::string fp;
    if (!sha3_512_hex(pk.data(), pk.size(), &fp, err)) {
        return false;
    }

    if (out_identity) {
        out_identity->public_key_b64 = pk_b64;
        out_identity->public_key_fingerprint = fp;
    }

    return true;
}

bool sign_circle_federation_canonical_json(
    const std::string& identity_dir,
    const std::string& canonical_json,
    std::string* out_signature_b64,
    std::string* err) {
    if (out_signature_b64) out_signature_b64->clear();

    if (!ensure_sodium(err)) return false;

    CircleFederationSigningIdentity identity;
    if (!ensure_circle_federation_signing_identity(identity_dir, &identity, err)) {
        return false;
    }

    std::string sk_b64;
    if (!read_text_file_trimmed(signing_secret_key_path(identity_dir), &sk_b64, err)) {
        return false;
    }

    std::vector<unsigned char> sk;
    if (!b64_decode_exact(
            sk_b64,
            crypto_sign_SECRETKEYBYTES,
            &sk,
            err)) {
        return false;
    }

    std::array<unsigned char, crypto_sign_BYTES> sig{};

    crypto_sign_detached(
        sig.data(),
        nullptr,
        reinterpret_cast<const unsigned char*>(canonical_json.data()),
        static_cast<unsigned long long>(canonical_json.size()),
        sk.data());

    if (out_signature_b64) {
        *out_signature_b64 = b64_encode(sig.data(), sig.size());
    }

    return true;
}

bool verify_circle_federation_canonical_json(
    const std::string& public_key_b64,
    const std::string& canonical_json,
    const std::string& signature_b64,
    std::string* err) {
    if (!ensure_sodium(err)) return false;

    std::vector<unsigned char> pk;
    if (!b64_decode_exact(
            public_key_b64,
            crypto_sign_PUBLICKEYBYTES,
            &pk,
            err)) {
        return false;
    }

    std::vector<unsigned char> sig;
    if (!b64_decode_exact(
            signature_b64,
            crypto_sign_BYTES,
            &sig,
            err)) {
        return false;
    }

    const int rc = crypto_sign_verify_detached(
        sig.data(),
        reinterpret_cast<const unsigned char*>(canonical_json.data()),
        static_cast<unsigned long long>(canonical_json.size()),
        pk.data());

    if (rc != 0) {
        if (err) *err = "invalid federation event signature";
        return false;
    }

    return true;
}

} // namespace pqnas::federation
