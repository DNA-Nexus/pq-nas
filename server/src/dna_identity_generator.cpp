#include "dna_identity_generator.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

#include <sodium.h>

namespace pqnas {
namespace {

constexpr int kBip39Words24 = 24;
constexpr std::size_t kMnemonicBufSize = 512;
constexpr std::size_t kSigningSeedBytes = 32;
constexpr std::size_t kEncryptionSeedBytes = 32;
constexpr std::size_t kDsa87PublicKeyBytes = 2592;
// ML-DSA-87 secret key size is 4896 bytes in the NIST ML-DSA-87 layout.
// Older Dilithium5 references sometimes list 4864 bytes. The DNA lib used by
// DNA Connect / CPUNK may write 4896 bytes, so using 4864 caused stack smashing.
constexpr std::size_t kDsa87SecretKeyBytes = 4896;
constexpr std::size_t kFingerprintHexBytes = 128;

using bip39_generate_mnemonic_fn =
    int (*)(int word_count, char* out, std::size_t out_size);

using qgp_derive_seeds_from_mnemonic_fn =
    int (*)(const char* mnemonic,
            const char* passphrase,
            unsigned char* signing_seed,
            unsigned char* encryption_seed);

using qgp_dsa87_keypair_derand_fn =
    int (*)(unsigned char* pk, unsigned char* sk, const unsigned char* seed);

using qgp_sha3_512_fingerprint_fn =
    int (*)(const unsigned char* pubkey, std::size_t pubkey_len, char* fingerprint_out);

static std::string dna_lib_path() {
    // Security: keep dlopen() pinned to a fixed, root-managed library path.
    // Loading a .so from an environment-controlled path would allow a bad
    // service configuration or environment injection to redirect execution to
    // attacker-controlled code.
    return "/opt/pqnas/lib/dna/libdna_lib.so";
}

template <typename T>
static bool load_sym(void* handle, const char* name, T& out, std::string& error) {
    dlerror();
    void* sym = dlsym(handle, name);
    const char* e = dlerror();

    if (e || !sym) {
        error = std::string("missing symbol ") + name + ": " + (e ? e : "unknown dlsym error");
        return false;
    }

    out = reinterpret_cast<T>(sym);
    return true;
}

static bool b64_original(const unsigned char* data, std::size_t len, std::string& out) {
    out.clear();

    if (sodium_init() < 0) {
        return false;
    }

    const std::size_t out_len =
        sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_ORIGINAL);

    std::string tmp(out_len, '\0');

    char* encoded = sodium_bin2base64(tmp.data(),
                                      tmp.size(),
                                      data,
                                      len,
                                      sodium_base64_VARIANT_ORIGINAL);
    if (!encoded) {
        return false;
    }

    while (!tmp.empty() && tmp.back() == '\0') {
        tmp.pop_back();
    }

    out = std::move(tmp);
    return true;
}

static bool valid_fingerprint_128_hex(const std::string& s) {
    if (s.size() != kFingerprintHexBytes) return false;

    for (char ch : s) {
        const bool ok =
            (ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'f') ||
            (ch >= 'A' && ch <= 'F');

        if (!ok) return false;
    }

    return true;
}

} // namespace

bool generate_dna_identity(GeneratedDnaIdentity& out, std::string& error) {
    out = {};
    error.clear();

    if (sodium_init() < 0) {
        error = "sodium_init failed";
        return false;
    }

    const std::string lib_path = dna_lib_path();

    void* handle = dlopen(lib_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* e = dlerror();
        error = std::string("dlopen failed for ") + lib_path + ": " + (e ? e : "unknown error");
        return false;
    }

    bip39_generate_mnemonic_fn bip39_generate_mnemonic = nullptr;
    qgp_derive_seeds_from_mnemonic_fn qgp_derive_seeds_from_mnemonic = nullptr;
    qgp_dsa87_keypair_derand_fn qgp_dsa87_keypair_derand = nullptr;
    qgp_sha3_512_fingerprint_fn qgp_sha3_512_fingerprint = nullptr;

    bool ok =
        load_sym(handle, "bip39_generate_mnemonic", bip39_generate_mnemonic, error) &&
        load_sym(handle, "qgp_derive_seeds_from_mnemonic", qgp_derive_seeds_from_mnemonic, error) &&
        load_sym(handle, "qgp_dsa87_keypair_derand", qgp_dsa87_keypair_derand, error) &&
        load_sym(handle, "qgp_sha3_512_fingerprint", qgp_sha3_512_fingerprint, error);

    if (!ok) {
        dlclose(handle);
        return false;
    }

    std::array<char, kMnemonicBufSize> mnemonic_buf{};
    std::vector<unsigned char> signing_seed(kSigningSeedBytes, 0);
    std::vector<unsigned char> encryption_seed(kEncryptionSeedBytes, 0);
    std::vector<unsigned char> pk(kDsa87PublicKeyBytes, 0);
    std::vector<unsigned char> sk(kDsa87SecretKeyBytes, 0);
    std::array<char, kFingerprintHexBytes + 1> fp{};

    if (bip39_generate_mnemonic(kBip39Words24, mnemonic_buf.data(), mnemonic_buf.size()) != 0) {
        error = "bip39_generate_mnemonic failed";
        dlclose(handle);
        return false;
    }

    const std::string mnemonic = mnemonic_buf.data();

    if (mnemonic.empty()) {
        error = "empty mnemonic generated";
        dlclose(handle);
        return false;
    }

    if (qgp_derive_seeds_from_mnemonic(mnemonic.c_str(),
                                       "",
                                       signing_seed.data(),
                                       encryption_seed.data()) != 0) {
        sodium_memzero(mnemonic_buf.data(), mnemonic_buf.size());
        error = "qgp_derive_seeds_from_mnemonic failed";
        dlclose(handle);
        return false;
    }

    if (qgp_dsa87_keypair_derand(pk.data(), sk.data(), signing_seed.data()) != 0) {
        sodium_memzero(mnemonic_buf.data(), mnemonic_buf.size());
        sodium_memzero(signing_seed.data(), signing_seed.size());
        sodium_memzero(encryption_seed.data(), encryption_seed.size());
        sodium_memzero(sk.data(), sk.size());
        error = "qgp_dsa87_keypair_derand failed";
        dlclose(handle);
        return false;
    }

    if (qgp_sha3_512_fingerprint(pk.data(), pk.size(), fp.data()) != 0) {
        sodium_memzero(mnemonic_buf.data(), mnemonic_buf.size());
        sodium_memzero(signing_seed.data(), signing_seed.size());
        sodium_memzero(encryption_seed.data(), encryption_seed.size());
        sodium_memzero(sk.data(), sk.size());
        error = "qgp_sha3_512_fingerprint failed";
        dlclose(handle);
        return false;
    }

    fp[kFingerprintHexBytes] = '\0';

    const std::string fp_hex = fp.data();
    if (!valid_fingerprint_128_hex(fp_hex)) {
        sodium_memzero(mnemonic_buf.data(), mnemonic_buf.size());
        sodium_memzero(signing_seed.data(), signing_seed.size());
        sodium_memzero(encryption_seed.data(), encryption_seed.size());
        sodium_memzero(sk.data(), sk.size());
        error = "invalid fingerprint generated";
        dlclose(handle);
        return false;
    }

    std::string public_key_b64;
    if (!b64_original(pk.data(), pk.size(), public_key_b64)) {
        sodium_memzero(mnemonic_buf.data(), mnemonic_buf.size());
        sodium_memzero(signing_seed.data(), signing_seed.size());
        sodium_memzero(encryption_seed.data(), encryption_seed.size());
        sodium_memzero(sk.data(), sk.size());
        error = "public key base64 failed";
        dlclose(handle);
        return false;
    }

    // Return the mnemonic string once, but wipe the mutable buffer copy.
    out.recovery_words = mnemonic;
    out.fingerprint_hex = fp_hex;
    out.public_key_b64 = public_key_b64;

    sodium_memzero(mnemonic_buf.data(), mnemonic_buf.size());
    sodium_memzero(signing_seed.data(), signing_seed.size());
    sodium_memzero(encryption_seed.data(), encryption_seed.size());
    sodium_memzero(sk.data(), sk.size());

    dlclose(handle);
    return true;
}

bool derive_dna_identity_from_recovery_words(const std::string& recovery_words,
                                             GeneratedDnaIdentity& out,
                                             std::string& error) {
    out = {};
    error.clear();

    if (sodium_init() < 0) {
        error = "sodium_init failed";
        return false;
    }

    if (recovery_words.empty() || recovery_words.size() > kMnemonicBufSize) {
        error = "invalid recovery words length";
        return false;
    }

    const std::string lib_path = dna_lib_path();

    void* handle = dlopen(lib_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* e = dlerror();
        error = std::string("dlopen failed for ") + lib_path + ": " + (e ? e : "unknown error");
        return false;
    }

    qgp_derive_seeds_from_mnemonic_fn qgp_derive_seeds_from_mnemonic = nullptr;
    qgp_dsa87_keypair_derand_fn qgp_dsa87_keypair_derand = nullptr;
    qgp_sha3_512_fingerprint_fn qgp_sha3_512_fingerprint = nullptr;

    bool ok =
        load_sym(handle, "qgp_derive_seeds_from_mnemonic", qgp_derive_seeds_from_mnemonic, error) &&
        load_sym(handle, "qgp_dsa87_keypair_derand", qgp_dsa87_keypair_derand, error) &&
        load_sym(handle, "qgp_sha3_512_fingerprint", qgp_sha3_512_fingerprint, error);

    if (!ok) {
        dlclose(handle);
        return false;
    }

    std::vector<unsigned char> signing_seed(kSigningSeedBytes, 0);
    std::vector<unsigned char> encryption_seed(kEncryptionSeedBytes, 0);
    std::vector<unsigned char> pk(kDsa87PublicKeyBytes, 0);
    std::vector<unsigned char> sk(kDsa87SecretKeyBytes, 0);
    std::array<char, kFingerprintHexBytes + 1> fp{};

    if (qgp_derive_seeds_from_mnemonic(recovery_words.c_str(),
                                       "",
                                       signing_seed.data(),
                                       encryption_seed.data()) != 0) {
        error = "qgp_derive_seeds_from_mnemonic failed";
        dlclose(handle);
        return false;
    }

    if (qgp_dsa87_keypair_derand(pk.data(), sk.data(), signing_seed.data()) != 0) {
        sodium_memzero(signing_seed.data(), signing_seed.size());
        sodium_memzero(encryption_seed.data(), encryption_seed.size());
        sodium_memzero(sk.data(), sk.size());
        error = "qgp_dsa87_keypair_derand failed";
        dlclose(handle);
        return false;
    }

    if (qgp_sha3_512_fingerprint(pk.data(), pk.size(), fp.data()) != 0) {
        sodium_memzero(signing_seed.data(), signing_seed.size());
        sodium_memzero(encryption_seed.data(), encryption_seed.size());
        sodium_memzero(sk.data(), sk.size());
        error = "qgp_sha3_512_fingerprint failed";
        dlclose(handle);
        return false;
    }

    fp[kFingerprintHexBytes] = '\0';

    const std::string fp_hex = fp.data();
    if (!valid_fingerprint_128_hex(fp_hex)) {
        sodium_memzero(signing_seed.data(), signing_seed.size());
        sodium_memzero(encryption_seed.data(), encryption_seed.size());
        sodium_memzero(sk.data(), sk.size());
        error = "invalid fingerprint generated";
        dlclose(handle);
        return false;
    }

    std::string public_key_b64;
    if (!b64_original(pk.data(), pk.size(), public_key_b64)) {
        sodium_memzero(signing_seed.data(), signing_seed.size());
        sodium_memzero(encryption_seed.data(), encryption_seed.size());
        sodium_memzero(sk.data(), sk.size());
        error = "public key base64 failed";
        dlclose(handle);
        return false;
    }

    out.recovery_words = "";
    out.fingerprint_hex = fp_hex;
    out.public_key_b64 = public_key_b64;

    sodium_memzero(signing_seed.data(), signing_seed.size());
    sodium_memzero(encryption_seed.data(), encryption_seed.size());
    sodium_memzero(sk.data(), sk.size());

    dlclose(handle);
    return true;
}


} // namespace pqnas
