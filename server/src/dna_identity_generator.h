#pragma once

#include <string>

namespace pqnas {

struct GeneratedDnaIdentity {
    // 24-word BIP39 recovery phrase.
    // Show once to the admin/user. Do not store on the server.
    std::string recovery_words;

    // SHA3-512(public ML-DSA-87 key), 128 hex chars.
    std::string fingerprint_hex;

    // Public ML-DSA-87 key, base64 encoded for optional future storage/display.
    std::string public_key_b64;
};

// Generates a CPUNK/DNA-style identity compatible with DNA Connect / pq-ssh:
//
// 24-word BIP39 mnemonic
// -> qgp_derive_seeds_from_mnemonic(...)
// -> qgp_dsa87_keypair_derand(...)
// -> qgp_sha3_512_fingerprint(public key)
//
// Private key and seeds are wiped and are not returned.
bool generate_dna_identity(GeneratedDnaIdentity& out, std::string& error);

// Derives the same CPUNK/DNA fingerprint from 24 recovery words.
// Does not return or store the recovery words.
bool derive_dna_identity_from_recovery_words(const std::string& recovery_words,
                                             GeneratedDnaIdentity& out,
                                             std::string& error);

} // namespace pqnas
