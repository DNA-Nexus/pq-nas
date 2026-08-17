#pragma once

#include "cluster/storage_user_projection_store.h"

#include <string>

namespace pqnas {

struct UserRec;

} // namespace pqnas

namespace pqnas::cluster {

// Build the minimal Director-owned authorization/storage projection that may
// be synchronized to the user's assigned Storage Node.
//
// Security: profile, role, credentials, placement policy and other Director-
// only metadata are deliberately excluded.
bool build_storage_user_projection(
    const pqnas::UserRec& user,
    StorageUserProjection* projection,
    std::string* error
);

} // namespace pqnas::cluster
