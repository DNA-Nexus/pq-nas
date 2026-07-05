#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace pqnas {

constexpr std::size_t kNotepadMaxBodyBytes = 64 * 1024;
constexpr std::size_t kNotepadMaxMarks = 200;
constexpr std::size_t kNotepadMaxMarksJsonBytes = 16 * 1024;
constexpr std::size_t kNotepadMaxNoteJsonBytes = 128 * 1024;

struct NotepadNoteRec {
    std::string owner_fingerprint;
    std::string body;
    std::string marks_json = "[]";
    std::int64_t revision = 0;
    std::int64_t updated_at_epoch = 0;
};

class NotepadStore {
public:
    // legacy_db_path is read-only migration input for notes saved by the first
    // Notepad backend version. New writes go to the user's own storage root.
    explicit NotepadStore(std::filesystem::path legacy_db_path = {});

    bool init(std::string* err) const;

    std::filesystem::path note_path_for_user(const std::filesystem::path& user_dir) const;

    std::optional<NotepadNoteRec> get_note(const std::string& owner_fingerprint,
                                           const std::filesystem::path& user_dir,
                                           std::string* err) const;

    bool save_note(const std::string& owner_fingerprint,
                   const std::filesystem::path& user_dir,
                   const std::string& body,
                   const std::string& marks_json,
                   std::int64_t expected_revision,
                   std::int64_t now_epoch,
                   NotepadNoteRec* out,
                   bool* revision_mismatch,
                   std::string* err) const;

private:
    std::filesystem::path legacy_db_path_;
};

} // namespace pqnas
