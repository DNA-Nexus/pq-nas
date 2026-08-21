#include "opaque_credentials.h"
#include "password_credentials.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pqnas::OpaqueCredentialRec make_opaque(
    const std::string& login,
    const std::string& fingerprint
) {
    pqnas::OpaqueCredentialRec rec;
    rec.login = login;
    rec.fingerprint = fingerprint;
    rec.opaque_password_file_b64 = "test-password-file";
    rec.opaque_suite = "test-suite";
    rec.enabled = true;
    return rec;
}

pqnas::PasswordCredentialRec make_password(
    const std::string& login,
    const std::string& fingerprint
) {
    pqnas::PasswordCredentialRec rec;
    rec.login = login;
    rec.fingerprint = fingerprint;
    rec.password_hash = "test-nonempty-hash";
    rec.enabled = true;
    return rec;
}

void test_opaque_lost_update(const fs::path& path) {
    {
        pqnas::OpaqueCredentials seed;
        expect(
            seed.upsert(make_opaque("base@example.test", "fp-base")),
            "opaque seed upsert"
        );
        expect(seed.save(path.string()), "opaque seed save");
    }

    pqnas::OpaqueCredentials writer_a;
    pqnas::OpaqueCredentials writer_b;

    expect(writer_a.load(path.string()), "opaque writer A load");
    expect(writer_b.load(path.string()), "opaque writer B load");

    expect(
        writer_a.upsert(make_opaque("writer-a@example.test", "fp-a")),
        "opaque writer A upsert"
    );
    expect(writer_a.save(path.string()), "opaque writer A save");

    expect(
        writer_b.upsert(make_opaque("writer-b@example.test", "fp-b")),
        "opaque writer B upsert"
    );

    // Security: writer B loaded an older snapshot. Its save must be rejected
    // instead of silently erasing writer A's committed credential.
    expect(
        !writer_b.save(path.string()),
        "stale opaque writer must be rejected"
    );

    pqnas::OpaqueCredentials verify;
    expect(verify.load(path.string()), "opaque verify load");

    expect(
        verify.get("writer-a@example.test").has_value(),
        "opaque writer A credential must survive stale writer"
    );

    expect(
        !verify.get("writer-b@example.test").has_value(),
        "rejected opaque stale mutation must not be persisted"
    );

    // A retry after reloading the current generation must succeed.
    pqnas::OpaqueCredentials retry;
    expect(retry.load(path.string()), "opaque retry load");
    expect(
        retry.upsert(make_opaque("writer-b@example.test", "fp-b")),
        "opaque retry upsert"
    );
    expect(retry.save(path.string()), "opaque retry save");

    pqnas::OpaqueCredentials final_verify;
    expect(final_verify.load(path.string()), "opaque final verify load");

    expect(
        final_verify.get("writer-a@example.test").has_value() &&
        final_verify.get("writer-b@example.test").has_value(),
        "opaque retry must preserve both committed credentials"
    );
}

void test_opaque_delete_resurrection(const fs::path& path) {
    {
        pqnas::OpaqueCredentials seed;
        expect(
            seed.upsert(make_opaque("delete-me@example.test", "fp-delete")),
            "opaque delete seed target"
        );
        expect(
            seed.upsert(make_opaque("other@example.test", "fp-other")),
            "opaque delete seed unrelated"
        );
        expect(seed.save(path.string()), "opaque delete seed save");
    }

    pqnas::OpaqueCredentials deleter;
    pqnas::OpaqueCredentials stale_writer;

    expect(deleter.load(path.string()), "opaque deleter load");
    expect(stale_writer.load(path.string()), "opaque stale updater load");

    expect(
        deleter.erase("delete-me@example.test"),
        "opaque credential erase"
    );
    expect(deleter.save(path.string()), "opaque delete save");

    auto other = stale_writer.get("other@example.test");
    expect(other.has_value(), "opaque stale unrelated credential exists");

    if (other.has_value()) {
        other->enabled = false;
        expect(stale_writer.upsert(*other), "opaque stale unrelated update");
    }

    // Security: stale state must never resurrect a credential already deleted
    // by another committed writer.
    expect(
        !stale_writer.save(path.string()),
        "opaque stale writer must not resurrect deleted credential"
    );

    pqnas::OpaqueCredentials verify;
    expect(verify.load(path.string()), "opaque deletion verify load");

    expect(
        !verify.get("delete-me@example.test").has_value(),
        "opaque deleted credential must stay deleted"
    );
}

void test_password_lost_update(const fs::path& path) {
    {
        pqnas::PasswordCredentials seed;
        expect(
            seed.upsert(make_password("base@example.test", "fp-base")),
            "password seed upsert"
        );
        expect(seed.save(path.string()), "password seed save");
    }

    pqnas::PasswordCredentials writer_a;
    pqnas::PasswordCredentials writer_b;

    expect(writer_a.load(path.string()), "password writer A load");
    expect(writer_b.load(path.string()), "password writer B load");

    expect(
        writer_a.upsert(make_password("writer-a@example.test", "fp-a")),
        "password writer A upsert"
    );
    expect(writer_a.save(path.string()), "password writer A save");

    expect(
        writer_b.upsert(make_password("writer-b@example.test", "fp-b")),
        "password writer B upsert"
    );

    expect(
        !writer_b.save(path.string()),
        "stale password writer must be rejected"
    );

    pqnas::PasswordCredentials verify;
    expect(verify.load(path.string()), "password verify load");

    expect(
        verify.get("writer-a@example.test").has_value(),
        "password writer A credential must survive stale writer"
    );

    expect(
        !verify.get("writer-b@example.test").has_value(),
        "rejected password stale mutation must not be persisted"
    );

    pqnas::PasswordCredentials retry;
    expect(retry.load(path.string()), "password retry load");
    expect(
        retry.upsert(make_password("writer-b@example.test", "fp-b")),
        "password retry upsert"
    );
    expect(retry.save(path.string()), "password retry save");

    pqnas::PasswordCredentials final_verify;
    expect(final_verify.load(path.string()), "password final verify load");

    expect(
        final_verify.get("writer-a@example.test").has_value() &&
        final_verify.get("writer-b@example.test").has_value(),
        "password retry must preserve both committed credentials"
    );
}

void test_password_delete_resurrection(const fs::path& path) {
    {
        pqnas::PasswordCredentials seed;
        expect(
            seed.upsert(make_password("delete-me@example.test", "fp-delete")),
            "password delete seed target"
        );
        expect(
            seed.upsert(make_password("other@example.test", "fp-other")),
            "password delete seed unrelated"
        );
        expect(seed.save(path.string()), "password delete seed save");
    }

    pqnas::PasswordCredentials deleter;
    pqnas::PasswordCredentials stale_writer;

    expect(deleter.load(path.string()), "password deleter load");
    expect(stale_writer.load(path.string()), "password stale updater load");

    expect(
        deleter.erase("delete-me@example.test"),
        "password credential erase"
    );
    expect(deleter.save(path.string()), "password delete save");

    auto other = stale_writer.get("other@example.test");
    expect(other.has_value(), "password stale unrelated credential exists");

    if (other.has_value()) {
        other->enabled = false;
        expect(stale_writer.upsert(*other), "password stale unrelated update");
    }

    expect(
        !stale_writer.save(path.string()),
        "password stale writer must not resurrect deleted credential"
    );

    pqnas::PasswordCredentials verify;
    expect(verify.load(path.string()), "password deletion verify load");

    expect(
        !verify.get("delete-me@example.test").has_value(),
        "password deleted credential must stay deleted"
    );
}

} // namespace

int main() {
    const fs::path dir =
        fs::temp_directory_path() /
        ("pqnas-auth-stale-write-" + std::to_string(::getpid()));

    std::error_code ec;
    fs::remove_all(dir, ec);
    ec.clear();

    if (!fs::create_directories(dir, ec) || ec) {
        std::cerr << "FAIL: temp directory creation failed\n";
        return 1;
    }

    test_opaque_lost_update(
        dir / "opaque_lost_update.json"
    );

    test_opaque_delete_resurrection(
        dir / "opaque_delete_resurrection.json"
    );

    test_password_lost_update(
        dir / "password_lost_update.json"
    );

    test_password_delete_resurrection(
        dir / "password_delete_resurrection.json"
    );

    fs::remove_all(dir, ec);

    if (failures != 0) {
        std::cerr
            << "FAIL: credential stale-write regression failures="
            << failures
            << '\n';
        return 1;
    }

    std::cout
        << "OK: credential stale-write regression tests passed\n";

    return 0;
}
