
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pqnas {

struct PeopleContactRecord {
    std::int64_t id = 0;

    std::string owner_fingerprint;

    // Optional future link to a local DNA-Nexus user account.
    std::string subject_user_id;

    // Stable identity anchor. Local users, external DNA members and manual contacts all resolve here.
    std::string subject_fingerprint;

    // manual_contact | fingerprint | local_user | external_dna
    std::string subject_kind = "manual_contact";

    // Address book / lightweight CRM fields.
    std::string contact_type = "person"; // person | company | customer | supplier | family | other
    std::string display_name;
    std::string nickname;

    std::string company;
    std::string title;

    std::string email;
    std::string phone;
    std::string mobile;
    std::string website;

    std::string street;
    std::string postal_code;
    std::string city;
    std::string country;

    std::string delivery_name;
    std::string delivery_street;
    std::string delivery_postal_code;
    std::string delivery_city;
    std::string delivery_country;

    std::string tags;
    std::string status = "active"; // active | inactive | archived

    std::string notes;

    std::int64_t created_at_epoch = 0;
    std::int64_t updated_at_epoch = 0;
};

class PeopleContactsStore {
public:
    explicit PeopleContactsStore(std::filesystem::path db_path);

    bool init(std::string* err) const;

    bool list_for_owner(const std::string& owner_fp,
                        std::vector<PeopleContactRecord>* out,
                        std::string* err) const;

    bool find_for_owner(const std::string& owner_fp,
                        const std::string& subject_fp,
                        std::optional<PeopleContactRecord>* out,
                        std::string* err) const;

    bool upsert_for_owner(const std::string& owner_fp,
                          const PeopleContactRecord& input,
                          PeopleContactRecord* out,
                          std::string* err) const;

    bool delete_for_owner(const std::string& owner_fp,
                          const std::string& subject_fp,
                          bool* deleted,
                          std::string* err) const;

    const std::filesystem::path& db_path() const { return db_path_; }

private:
    std::filesystem::path db_path_;
};

std::string people_canonical_fingerprint(const std::string& input);
bool people_valid_fingerprint(const std::string& fp);
std::string people_normalize_subject_kind(const std::string& input);
std::string people_normalize_contact_type(const std::string& input);
std::string people_normalize_status(const std::string& input);
std::string people_fingerprint_short(const std::string& fp);

} // namespace pqnas
