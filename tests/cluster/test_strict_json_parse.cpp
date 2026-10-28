#include "cluster/strict_json_parse.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;

        std::cerr
            << "FAIL: "
            << message
            << '\n';
    }
}

} // namespace

int main() {
    using nlohmann::json;

    {
        json output;

        expect(
            pqnas::cluster::
                parse_json_reject_duplicate_keys(
                    R"({"fingerprint":"a","binding":"b"})",
                    &output
                ),
            "canonical flat object accepted"
        );

        expect(
            output.is_object() &&
            output["fingerprint"] == "a" &&
            output["binding"] == "b",
            "successful parse publishes exact object"
        );
    }

    {
        json output =
            json{
                {"sentinel", true}
            };

        const json before =
            output;

        expect(
            !pqnas::cluster::
                parse_json_reject_duplicate_keys(
                    R"({"fingerprint":"a","fingerprint":"b"})",
                    &output
                ),
            "duplicate top-level key rejected"
        );

        expect(
            output == before,
            "duplicate-key failure does not publish ambiguous parse result"
        );
    }

    {
        json output =
            json{
                {"sentinel", true}
            };

        const json before =
            output;

        expect(
            !pqnas::cluster::
                parse_json_reject_duplicate_keys(
                    R"({"outer":{"x":1,"x":2}})",
                    &output
                ),
            "duplicate nested key rejected"
        );

        expect(
            output == before,
            "nested duplicate failure leaves caller output unchanged"
        );
    }

    {
        json output;

        expect(
            pqnas::cluster::
                parse_json_reject_duplicate_keys(
                    R"({"left":{"id":1},"right":{"id":2}})",
                    &output
                ),
            "same key name in distinct objects is allowed"
        );

        expect(
            output["left"]["id"] == 1 &&
            output["right"]["id"] == 2,
            "object-local duplicate tracking preserves valid nested JSON"
        );
    }

    {
        json output =
            json{
                {"sentinel", true}
            };

        const json before =
            output;

        expect(
            !pqnas::cluster::
                parse_json_reject_duplicate_keys(
                    R"({"broken":)",
                    &output
                ),
            "malformed JSON rejected"
        );

        expect(
            output == before,
            "syntax failure leaves caller output unchanged"
        );
    }

    {
        expect(
            !pqnas::cluster::
                parse_json_reject_duplicate_keys(
                    "{}",
                    nullptr
                ),
            "null output rejected"
        );
    }

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " strict JSON parse test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: strict JSON parse tests passed\n";

    return 0;
}
