#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace pqnas {

constexpr std::size_t kNotepadMaxBodyBytes = 64 * 1024;

struct NotepadNoteRec {
    std::string owner_fingerprint;
    std::string body;
    std::int64_t revision = 0;
    std::int64_t updated_at_epoch = 0;
};

class NotepadStore {
public:
    explicit NotepadStore(std::filesystem::path db_path);

    bool init(std::string* err) const;

    std::optional<NotepadNoteRec> get_note(const std::string& owner_fingerprint,
                                           std::string* err) const;

    bool save_note(const std::string& owner_fingerprint,
                   const std::string& body,
                   std::int64_t expected_revision,
                   std::int64_t now_epoch,
                   NotepadNoteRec* out,
                   bool* revision_mismatch,
                   std::string* err) const;

private:
    std::filesystem::path db_path_;
};

} // namespace pqnas
