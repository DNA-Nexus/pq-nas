#pragma once
#include <string>

bool session_cookie_mint(const unsigned char key32[32],
                         const std::string& fingerprint_b64,
                         long iat, long exp,
                         std::string& out_cookie_value);

bool session_cookie_verify(const unsigned char key32[32],
                           const std::string& cookie_value,
                           std::string& out_fingerprint_b64,
                           long& out_exp);

// RT-018: workspace sessions are a distinct cryptographic cookie family.
// These wrappers derive a workspace-only signing key from the base COOKIE_KEY
// before calling the generic session-cookie primitive.
bool workspace_session_cookie_mint(const unsigned char base_key32[32],
                                   const std::string& workspace_claim_b64,
                                   long iat,
                                   long exp,
                                   std::string& out_cookie_value);

bool workspace_session_cookie_verify(const unsigned char base_key32[32],
                                     const std::string& cookie_value,
                                     std::string& out_workspace_claim_b64,
                                     long& out_exp);
