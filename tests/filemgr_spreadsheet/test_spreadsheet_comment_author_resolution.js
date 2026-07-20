"use strict";

const assert = require("assert");

async function run() {
  const originalFetch = global.fetch;
  const requests = [];

  global.fetch = async (url, options = {}) => {
    requests.push({
      url: String(url),
      options
    });

    if (url === "/api/v4/me") {
      return {
        ok: true,
        async json() {
          return {
            user: {
              display_name: "",
              fingerprint:
                "A8885F401234567890ABCDEF1234BEDE39"
            }
          };
        }
      };
    }

    if (
      String(url).startsWith(
        "/api/v4/people/resolve?"
      )
    ) {
      return {
        ok: true,
        async json() {
          return {
            ok: true,
            resolved: true,
            person: {
              display_name: "Timo Erkvaara"
            }
          };
        }
      };
    }

    throw new Error(
      `unexpected fetch URL: ${url}`
    );
  };

  try {
    const modulePath =
      "../../apps/bundled/filemgr/src/www/" +
      "spreadsheet_comments.js";

    delete require.cache[
      require.resolve(modulePath)
    ];

    const comments = require(modulePath);

    assert.strictEqual(
      comments.peopleResolvedDisplayName({
        person: {
          display_name: "  Timo   Erkvaara  "
        }
      }),
      "Timo Erkvaara"
    );

    assert.strictEqual(
      await comments.currentAuthor(),
      "Timo Erkvaara"
    );

    assert.strictEqual(
      requests.length,
      2
    );

    assert.strictEqual(
      requests[0].url,
      "/api/v4/me"
    );

    assert.match(
      requests[1].url,
      /^\/api\/v4\/people\/resolve\?fingerprint=/
    );

    assert.ok(
      requests[1].url.includes(
        "a8885f401234567890abcdef1234bede39"
      )
    );

    assert.strictEqual(
      requests[1].options.credentials,
      "include"
    );

    console.log(
      "spreadsheet comment author resolution tests: OK"
    );
  } finally {
    global.fetch = originalFetch;
  }
}

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
