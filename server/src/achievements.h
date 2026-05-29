#pragma once

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <string>

namespace pqnas::achievements {

nlohmann::json circle_stack_public_badges(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role);

nlohmann::json circle_stack_profile_json(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role,
    bool include_private_stats);

} // namespace pqnas::achievements
