#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pqnas {

using json = nlohmann::json;

struct WorkspaceLinkRec {
    std::string id;
    std::string parent_path;
    std::string name;
    std::string url;
    std::string detected_type;
    std::string created_by_fp;
    std::int64_t created_at_epoch = 0;
    std::int64_t updated_at_epoch = 0;
};

std::filesystem::path workspace_links_dir(const std::filesystem::path& workspace_root);

bool workspace_link_is_valid_id(const std::string& id);
std::string workspace_link_new_id();

std::string workspace_link_detect_type(const std::string& url);
bool workspace_link_validate_url(const std::string& url, std::string* err);

json workspace_link_to_json(const WorkspaceLinkRec& rec);
bool workspace_link_from_json(const json& j, WorkspaceLinkRec* out, std::string* err);

bool workspace_links_load_all(const std::filesystem::path& workspace_root,
                              std::vector<WorkspaceLinkRec>* out,
                              std::string* err);

bool workspace_links_load_one(const std::filesystem::path& workspace_root,
                              const std::string& id,
                              WorkspaceLinkRec* out,
                              std::string* err);

bool workspace_links_save_one(const std::filesystem::path& workspace_root,
                              const WorkspaceLinkRec& rec,
                              bool fail_if_exists,
                              std::string* err);

bool workspace_links_delete_one(const std::filesystem::path& workspace_root,
                                const std::string& id,
                                std::string* err);

} // namespace pqnas
