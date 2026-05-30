#pragma once

#include <string>

namespace pqnas::federation {

struct CircleFederationSigningIdentity {
    std::string public_key_b64;
    std::string public_key_fingerprint;
};

bool ensure_circle_federation_signing_identity(
    const std::string& identity_dir,
    CircleFederationSigningIdentity* out_identity,
    std::string* err);

bool load_circle_federation_signing_identity(
    const std::string& identity_dir,
    CircleFederationSigningIdentity* out_identity,
    std::string* err);

std::string circle_federation_signing_public_key_fingerprint(
    const std::string& public_key_b64);

bool sign_circle_federation_canonical_json(
    const std::string& identity_dir,
    const std::string& canonical_json,
    std::string* out_signature_b64,
    std::string* err);

bool verify_circle_federation_canonical_json(
    const std::string& public_key_b64,
    const std::string& canonical_json,
    const std::string& signature_b64,
    std::string* err);

} // namespace pqnas::federation
