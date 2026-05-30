#pragma once

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <filesystem>
#include <string>

namespace pqnas::achievements {

nlohmann::json circle_stack_public_badges(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role);

nlohmann::json circle_stack_public_badges(
    sqlite3* circle_db,
    const std::filesystem::path& user_root,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role);

nlohmann::json circle_stack_profile_json(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role,
    bool include_private_stats);

nlohmann::json circle_stack_profile_json(
    sqlite3* circle_db,
    const std::filesystem::path& user_root,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role,
    bool include_private_stats);

bool mark_achievement_dismissed(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& achievement_id,
    std::string* err);

} // namespace pqnas::achievements
