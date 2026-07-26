srv.Post("/api/v4/shares/pq/enroll", [&](const httplib::Request& req, httplib::Response& res) {
    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

    json body;
    try { body = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
        return;
    }

    const std::string invite_id = body.value("invite_id", std::string{});
    const std::string device_label = body.value("device_label", std::string{});
    std::string kem_alg = body.value("kem_alg", std::string{});
    if (kem_alg.empty() || kem_alg == "ml-kem-768" || kem_alg == "mlkem768" || kem_alg == "MLKEM768") {
        kem_alg = "ML-KEM-768";
    } else if (kem_alg != "ML-KEM-768") {
        reply(400, {
            {"ok", false},
            {"error", "unsupported_kem_alg"},
            {"message", "Only ML-KEM-768 is supported"}
        });
        return;
    }
    const std::string public_key_b64 = body.value("public_key_b64", std::string{});

    if (invite_id.empty() || device_label.empty() || public_key_b64.empty()) {
        reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing fields"}});
        return;
    }

    pqnas::PqShareInviteV1 inv;
    std::string inv_err;
    if (!share_pq.load_invite(invite_id, &inv, &inv_err) || inv.state != "pending") {
        reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "invite not found"}});
        return;
    }

    if (pqnas::SharePqStoreV1::iso_expired_local(inv.expires_at)) {
        reply(410, json{{"ok", false}, {"error", "expired"}, {"message", "invite expired"}});
        return;
    }

    pqnas::PqShareManifestV1 mf;
    std::string mf_err;
    if (!share_pq.load_manifest(inv.share_token, &mf, &mf_err)) {
        reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "share manifest not found"}});
        return;
    }
    pqnas::PqShareRecipientDeviceV1 dev;
    std::string find_dev_err;
    const bool reused_device =
        share_pq.find_active_recipient_device_by_public_key(
            inv.owner_fp,
            kem_alg,
            public_key_b64,
            &dev,
            &find_dev_err);

    if (!reused_device) {
        dev.owner_fp = inv.owner_fp;
        dev.recipient_device_id = pqnas::SharePqStoreV1::random_id_b64url_local(24);
        dev.label = device_label;
        dev.state = "active";
        dev.created_at = pqnas::SharePqStoreV1::now_iso_utc_local();
        dev.updated_at = dev.created_at;
        dev.last_used_at = dev.created_at;
        dev.registered_via = "invite";
        dev.invite_id = invite_id;
        dev.kem_alg = kem_alg;
        dev.key_id = pqnas::SharePqStoreV1::random_id_b64url_local(12);
        dev.public_key_b64 = public_key_b64;

        std::string dev_err;
        if (!share_pq.save_recipient_device(dev, &dev_err)) {
            reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "recipient save failed"}});
            return;
        }
    } else {
        // Preserve stable human label unless explicitly empty
        if (dev.label.empty() && !device_label.empty()) {
            dev.label = device_label;
        }
        dev.updated_at = pqnas::SharePqStoreV1::now_iso_utc_local();
        dev.last_used_at = dev.updated_at;

        std::string dev_err;
        if (!share_pq.save_recipient_device(dev, &dev_err)) {
            reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "recipient update failed"}});
            return;
        }
    }

    if (std::find(mf.recipient_device_ids.begin(),
              mf.recipient_device_ids.end(),
              dev.recipient_device_id) == mf.recipient_device_ids.end()) {
    mf.recipient_device_ids.push_back(dev.recipient_device_id);
}
    mf.state = "active";

    std::string save_mf_err;
    if (!share_pq.save_manifest(mf, &save_mf_err)) {
        reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "manifest update failed"}});
        return;
    }

    inv.state = "claimed";
    inv.claim_count = 1;
    inv.claimed_recipient_device_id = dev.recipient_device_id;

    std::string save_inv_err;
    if (!share_pq.save_invite(inv, &save_inv_err)) {
        reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "invite update failed"}});
        return;
    }

    pqnas::PqShareRecipientSessionV1 sess;
    sess.session_id = pqnas::SharePqStoreV1::random_id_b64url_local(24);
    sess.owner_fp = dev.owner_fp;
    sess.recipient_device_id = dev.recipient_device_id;
    sess.created_at = pqnas::SharePqStoreV1::now_iso_utc_local();
    sess.last_used_at = sess.created_at;
    sess.expires_at = pqnas::SharePqStoreV1::add_seconds_iso_utc_local(365LL * 24 * 3600);
    sess.state = "active";

    std::string sess_err;
    if (!share_pq.save_session(sess, &sess_err)) {
        reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "session create failed"}});
        return;
    }

    res.set_header("Set-Cookie",
        ("pqnas_share_recipient=" + sess.session_id + "; Path=/; HttpOnly; Secure; SameSite=Strict").c_str());

    reply(200, json{
        {"ok", true},
        {"share_token", inv.share_token},
        {"share_url", std::string("/s/") + inv.share_token},
        {"recipient_device_id", dev.recipient_device_id},
        {"pq_state", "active"}
    });
});

    struct PqShareOpenContextV1 {
        std::string share_token;
        std::string owner_fp;
        std::string rel_path;
        std::string file_name;
        std::filesystem::path abs_path;
        pqnas::ShareLink share;
        pqnas::PqShareManifestV1 manifest;
        pqnas::PqShareRecipientSessionV1 session;
        pqnas::PqShareRecipientDeviceV1 device;
    };
auto sha256_file_hex_local = [&](const std::filesystem::path& p,
                                 std::string* out_hex,
                                 std::string* err) -> bool {
    if (out_hex) out_hex->clear();
    if (err) err->clear();

    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "open_failed";
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        if (err) *err = "digest_ctx_new_failed";
        return false;
    }

    bool ok = false;
    do {
        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            if (err) *err = "digest_init_failed";
            break;
        }

        std::array<char, 64 * 1024> buf{};
        while (f.good()) {
            f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = f.gcount();
            if (n > 0) {
                if (EVP_DigestUpdate(ctx, buf.data(), static_cast<size_t>(n)) != 1) {
                    if (err) *err = "digest_update_failed";
                    break;
                }
            }
        }

        if (!f.eof() && f.fail()) {
            if (err) *err = "read_failed";
            break;
        }

        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int md_len = 0;
        if (EVP_DigestFinal_ex(ctx, md, &md_len) != 1) {
            if (err) *err = "digest_final_failed";
            break;
        }

        static const char* kHex = "0123456789abcdef";
        std::string hex;
        hex.reserve(md_len * 2);
        for (unsigned int i = 0; i < md_len; ++i) {
            unsigned char b = md[i];
            hex.push_back(kHex[(b >> 4) & 0x0f]);
            hex.push_back(kHex[b & 0x0f]);
        }

        if (out_hex) *out_hex = std::move(hex);
        ok = true;
    } while (false);

    EVP_MD_CTX_free(ctx);
    return ok;
};

auto resolve_pq_share_open_context_v1 =
    [&](const httplib::Request& req,
        const std::string& share_token,
        PqShareOpenContextV1* out,
        std::string* err) -> bool {

    if (out) *out = PqShareOpenContextV1{};
    if (err) err->clear();

    auto fail = [&](const std::string& why) -> bool {
        if (err) *err = why;
        return false;
    };

    auto lower_ascii = [](std::string s) -> std::string {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return s;
    };

    auto get_cookie_value_local = [&](const std::string& name) -> std::string {
        auto it = req.headers.find("Cookie");
        if (it == req.headers.end()) return {};

        const std::string all = it->second;
        const std::string needle = name + "=";

        std::size_t pos = 0;
        while (pos < all.size()) {
            while (pos < all.size() && (all[pos] == ' ' || all[pos] == ';')) ++pos;
            if (pos >= all.size()) break;

            if (all.compare(pos, needle.size(), needle) == 0) {
                std::size_t start = pos + needle.size();
                std::size_t end = all.find(';', start);
                if (end == std::string::npos) end = all.size();
                return all.substr(start, end - start);
            }

            std::size_t next = all.find(';', pos);
            if (next == std::string::npos) break;
            pos = next + 1;
        }
        return {};
    };

    pqnas::ShareLink s;
    std::string share_err;
    auto valid = shares.is_valid_now(share_token, &s, &share_err);

    if (!valid.has_value()) return fail("share_not_found");
    if (!valid.value()) return fail("share_expired");

    pqnas::PqShareManifestV1 mf;
    std::string mf_err;
    if (!share_pq.load_manifest(share_token, &mf, &mf_err)) {
        return fail("share_not_found");
    }
    if (mf.kind != "pq_recipient_enrolled_v1") {
        return fail("pq_open_denied");
    }

    if (s.type != "file") {
        return fail("pq_share_requires_file");
    }

    const std::string session_id = get_cookie_value_local("pqnas_share_recipient");
    if (session_id.empty()) {
        return fail("enrollment_required");
    }

    pqnas::PqShareRecipientSessionV1 sess;
    std::string sess_err;
    if (!share_pq.load_session(session_id, &sess, &sess_err)) {
        return fail("enrollment_required");
    }
    if (sess.state != "active") {
        return fail("enrollment_required");
    }
    if (pqnas::SharePqStoreV1::iso_expired_local(sess.expires_at)) {
        return fail("enrollment_required");
    }
    if (sess.owner_fp != s.owner_fp) {
        return fail("enrollment_required");
    }

    pqnas::PqShareRecipientDeviceV1 dev;
    std::string dev_err;
    if (!share_pq.load_recipient_device(sess.owner_fp, sess.recipient_device_id, &dev, &dev_err)) {
        return fail("enrollment_required");
    }
    if (dev.state != "active") {
        return fail("enrollment_required");
    }
    if (dev.owner_fp != s.owner_fp) {
        return fail("enrollment_required");
    }

    const bool allowed =
        std::find(mf.recipient_device_ids.begin(),
                  mf.recipient_device_ids.end(),
                  sess.recipient_device_id) != mf.recipient_device_ids.end();
    if (!allowed) {
        return fail("enrollment_required");
    }

    pqnas::ResolvedExistingPath rp;
    std::string rerr;
    if (!pqnas::resolve_existing_user_file_path(users, s.owner_fp, s.path, &rp, &rerr)) {
        return fail("share_not_found");
    }

    const std::filesystem::path abs = rp.abs_path;

    std::error_code ec;
    auto st = std::filesystem::symlink_status(abs, ec);
    if (ec || !std::filesystem::exists(st) || std::filesystem::is_symlink(st)) {
        return fail("share_not_found");
    }
    if (!std::filesystem::is_regular_file(st)) {
        return fail("share_not_found");
    }

    {
        std::error_code ec_size;
        const auto live_size = std::filesystem::file_size(abs, ec_size);
        if (ec_size) return fail("share_not_found");

        if (mf.snapshot.size_bytes > 0 &&
            static_cast<std::uint64_t>(live_size) != mf.snapshot.size_bytes) {
            return fail("snapshot_mismatch");
        }
    }

    if (!mf.snapshot.sha256_hex.empty()) {
        std::string got_sha;
        std::string sha_err;
        if (!sha256_file_hex_local(abs, &got_sha, &sha_err)) {
            return fail("snapshot_mismatch");
        }
        if (lower_ascii(got_sha) != lower_ascii(mf.snapshot.sha256_hex)) {
            return fail("snapshot_mismatch");
        }
    }

    if (out) {
        out->share_token = share_token;
        out->owner_fp = s.owner_fp;
        out->rel_path = s.path;
        out->file_name = std::filesystem::path(s.path).filename().string();
        out->abs_path = abs;
        out->share = s;
        out->manifest = mf;
        out->session = sess;
        out->device = dev;
    }

    return true;
};
auto b64_encode_bytes_local = [&](const std::vector<std::uint8_t>& in) -> std::string {
    if (in.empty()) return {};
    const int out_len = 4 * static_cast<int>((in.size() + 2) / 3);
    std::string out(out_len, '\0');
    const int n = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(&out[0]),
        reinterpret_cast<const unsigned char*>(in.data()),
        static_cast<int>(in.size()));
    if (n < 0) return {};
    out.resize(static_cast<std::size_t>(n));
    return out;
};

auto b64_decode_bytes_local = [&](const std::string& in, std::vector<std::uint8_t>* out) -> bool {
    if (!out) return false;
    out->clear();
    if (in.empty()) return true;

    int pad = 0;
    if (!in.empty() && in.back() == '=') ++pad;
    if (in.size() >= 2 && in[in.size() - 2] == '=') ++pad;

    std::vector<std::uint8_t> tmp(3 * ((in.size() + 3) / 4), 0);
    const int n = EVP_DecodeBlock(
        reinterpret_cast<unsigned char*>(tmp.data()),
        reinterpret_cast<const unsigned char*>(in.data()),
        static_cast<int>(in.size()));
    if (n < 0) return false;

    tmp.resize(static_cast<std::size_t>(n - pad));
    *out = std::move(tmp);
    return true;
};

auto u32be_local = [&](std::uint32_t v) -> std::vector<std::uint8_t> {
    return {
        static_cast<std::uint8_t>((v >> 24) & 0xff),
        static_cast<std::uint8_t>((v >> 16) & 0xff),
        static_cast<std::uint8_t>((v >>  8) & 0xff),
        static_cast<std::uint8_t>(v & 0xff)
    };
};

auto make_chunk_iv_v2_local = [&](const std::vector<std::uint8_t>& prefix8, std::uint32_t chunk_index, std::vector<std::uint8_t>* out) -> bool {
    if (!out || prefix8.size() != 8) return false;
    *out = prefix8;
    const auto tail = u32be_local(chunk_index);
    out->insert(out->end(), tail.begin(), tail.end());
    return true;
};

auto make_chunk_aad_v2_local = [&](const std::vector<std::uint8_t>& base_aad,
                                   std::uint32_t chunk_index,
                                   std::uint32_t plain_chunk_size,
                                   std::vector<std::uint8_t>* out) -> bool {
    if (!out) return false;
    *out = base_aad;
    const auto idx = u32be_local(chunk_index);
    const auto psz = u32be_local(plain_chunk_size);
    out->insert(out->end(), idx.begin(), idx.end());
    out->insert(out->end(), psz.begin(), psz.end());
    return true;
};

auto guess_mime_type_local = [&](const std::string& file_name) -> std::string {
    auto lower_ascii = [](std::string s) -> std::string {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return s;
    };

    const std::string fn = lower_ascii(file_name);

    auto ends_with = [&](const char* suffix) -> bool {
        const std::string suf = suffix ? suffix : "";
        return fn.size() >= suf.size() &&
               fn.compare(fn.size() - suf.size(), suf.size(), suf) == 0;
    };

    if (ends_with(".txt"))  return "text/plain";
    if (ends_with(".md"))   return "text/markdown";
    if (ends_with(".html")) return "text/html";
    if (ends_with(".htm"))  return "text/html";
    if (ends_with(".css"))  return "text/css";
    if (ends_with(".js"))   return "application/javascript";
    if (ends_with(".json")) return "application/json";
    if (ends_with(".csv"))  return "text/csv";
    if (ends_with(".xml"))  return "application/xml";

    if (ends_with(".pdf"))  return "application/pdf";
    if (ends_with(".zip"))  return "application/zip";
    if (ends_with(".gz"))   return "application/gzip";
    if (ends_with(".tar"))  return "application/x-tar";
    if (ends_with(".7z"))   return "application/x-7z-compressed";
    if (ends_with(".rar"))  return "application/vnd.rar";

    if (ends_with(".png"))  return "image/png";
    if (ends_with(".jpg"))  return "image/jpeg";
    if (ends_with(".jpeg")) return "image/jpeg";
    if (ends_with(".gif"))  return "image/gif";
    if (ends_with(".webp")) return "image/webp";
    if (ends_with(".svg"))  return "image/svg+xml";
    if (ends_with(".bmp"))  return "image/bmp";
    if (ends_with(".ico"))  return "image/x-icon";

    if (ends_with(".mp3"))  return "audio/mpeg";
    if (ends_with(".wav"))  return "audio/wav";
    if (ends_with(".ogg"))  return "audio/ogg";
    if (ends_with(".flac")) return "audio/flac";

    if (ends_with(".mp4"))  return "video/mp4";
    if (ends_with(".webm")) return "video/webm";
    if (ends_with(".mkv"))  return "video/x-matroska";
    if (ends_with(".mov"))  return "video/quicktime";
    if (ends_with(".avi"))  return "video/x-msvideo";

    if (ends_with(".deb"))  return "application/vnd.debian.binary-package";
    if (ends_with(".rpm"))  return "application/x-rpm";
    if (ends_with(".apk"))  return "application/vnd.android.package-archive";
    if (ends_with(".exe"))  return "application/vnd.microsoft.portable-executable";

    return "application/octet-stream";
};

srv.Post("/api/v4/shares/pq/open/init", [&](const httplib::Request& req, httplib::Response& res) {
    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

    json body = json::object();
    try {
        if (!req.body.empty()) body = json::parse(req.body);
    } catch (...) {
        reply(400, {{"ok", false}, {"error", "bad_json"}});
        return;
    }

    const std::string share_token =
        (body.contains("share_token") && body["share_token"].is_string())
            ? body["share_token"].get<std::string>()
            : std::string{};

    if (share_token.empty()) {
        reply(400, {{"ok", false}, {"error", "missing_share_token"}});
        return;
    }

    PqShareOpenContextV1 ctx;
    std::string ctx_err;
    if (!resolve_pq_share_open_context_v1(req, share_token, &ctx, &ctx_err)) {
        reply(ctx_err == "share_not_found" ? 404 : 403, {
            {"ok", false},
            {"error", ctx_err.empty() ? "pq_open_denied" : ctx_err}
        });
        return;
    }

    std::string device_kem_alg = ctx.device.kem_alg;
    if (device_kem_alg == "ml-kem-768" || device_kem_alg == "mlkem768" || device_kem_alg == "MLKEM768") {
        device_kem_alg = "ML-KEM-768";
    }
    if (device_kem_alg != "ML-KEM-768") {
        reply(400, {
            {"ok", false},
            {"error", "unsupported_recipient_kem_alg"},
            {"message", "Only ML-KEM-768 recipients are supported"}
        });
        return;
    }

    const std::uint64_t file_size = ctx.manifest.snapshot.size_bytes;
    const std::uint64_t chunk_size = 1024ull * 1024ull;
    const std::uint64_t chunk_count = (file_size == 0) ? 0 : ((file_size + chunk_size - 1) / chunk_size);

    json aad = {
        {"v", 2},
        {"purpose", "pq_share_open_stream"},
        {"mode", "mlkem768_aes256gcm_chunks_v2"},
        {"share_token", ctx.share_token},
        {"recipient_device_id", ctx.device.recipient_device_id},
        {"file_name", ctx.file_name},
        {"snapshot_sha256_hex", ctx.manifest.snapshot.sha256_hex},
        {"snapshot_size_bytes", ctx.manifest.snapshot.size_bytes},
        {"snapshot_mtime_epoch", ctx.manifest.snapshot.mtime_epoch}
    };
    const std::string aad_json = aad.dump();

    std::vector<std::uint8_t> cek(32, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(cek.data()), static_cast<int>(cek.size())) != 1) {
        reply(500, {{"ok", false}, {"error", "random_cek_failed"}});
        return;
    }

    std::vector<std::uint8_t> nonce_prefix(8, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(nonce_prefix.data()), static_cast<int>(nonce_prefix.size())) != 1) {
        reply(500, {{"ok", false}, {"error", "random_nonce_prefix_failed"}});
        return;
    }

    pqnas::PqOpenSnapshotV1 snap;
    snap.size_bytes = ctx.manifest.snapshot.size_bytes;
    snap.mtime_epoch = ctx.manifest.snapshot.mtime_epoch;
    snap.sha256_hex = ctx.manifest.snapshot.sha256_hex;

    pqnas::PqWrappedKeyV1 wrapped_key_rec;
    std::string crypto_err;
    if (!pqnas::wrap_pq_stream_cek_mlkem768_v1(
            ctx.device.recipient_device_id,
            ctx.device.public_key_b64,
            cek,
            aad_json,
            &wrapped_key_rec,
            &crypto_err)) {
        reply(500, {
            {"ok", false},
            {"error", "pq_wrap_stream_cek_failed"},
            {"detail", crypto_err}
        });
        return;
    }

    const std::string recipient_session_id = [&]() -> std::string {
        auto it = req.headers.find("Cookie");
        if (it == req.headers.end()) return {};
        const std::string all = it->second;
        const std::string needle = "pqnas_share_recipient=";
        std::size_t pos = all.find(needle);
        if (pos == std::string::npos) return {};
        pos += needle.size();
        std::size_t end = all.find(';', pos);
        if (end == std::string::npos) end = all.size();
        return all.substr(pos, end - pos);
    }();

    pqnas::PqShareOpenStreamSessionV1 os;
    os.open_id = pqnas::SharePqStoreV1::random_id_b64url_local(24);
    os.owner_fp = ctx.owner_fp;
    os.share_token = ctx.share_token;
    os.recipient_session_id = recipient_session_id;
    os.recipient_device_id = ctx.device.recipient_device_id;
    os.rel_path = ctx.rel_path;
    os.file_name = ctx.file_name;
    os.mime_type = guess_mime_type_local(ctx.file_name);
    os.file_size_bytes = file_size;
    os.chunk_size_bytes = chunk_size;
    os.chunk_count = chunk_count;
    os.snapshot_mtime_epoch = ctx.manifest.snapshot.mtime_epoch;
    os.snapshot_sha256_hex = ctx.manifest.snapshot.sha256_hex;
    os.aad_b64 = b64_encode_bytes_local(std::vector<std::uint8_t>(aad_json.begin(), aad_json.end()));
    os.cek_b64 = b64_encode_bytes_local(cek);
    os.chunk_nonce_prefix_b64 = b64_encode_bytes_local(nonce_prefix);
    os.created_at = pqnas::SharePqStoreV1::now_iso_utc_local();
    os.expires_at = pqnas::SharePqStoreV1::add_seconds_iso_utc_local(15 * 60);
    os.state = "active";

    std::string os_err;
    if (!share_pq.save_open_stream_session(os, &os_err)) {
        reply(500, {{"ok", false}, {"error", "open_stream_session_save_failed"}, {"detail", os_err}});
        return;
    }

    json wrapped_key = {
        {"mode", wrapped_key_rec.mode},
        {"recipient_device_id", wrapped_key_rec.recipient_device_id},
        {"kem_alg", wrapped_key_rec.kem_alg},
        {"kem_ciphertext_b64", wrapped_key_rec.kem_ciphertext_b64},
        {"hkdf_salt_b64", wrapped_key_rec.hkdf_salt_b64},
        {"hkdf_info_b64", wrapped_key_rec.hkdf_info_b64},
        {"wrap_iv_b64", wrapped_key_rec.wrap_iv_b64},
        {"wrapped_cek_b64", wrapped_key_rec.wrapped_cek_b64}
    };

    reply(200, {
        {"ok", true},
        {"open", {
            {"version", 2},
            {"mode", "mlkem768_aes256gcm_chunks_v2"},
            {"share_token", ctx.share_token},
            {"file_name", ctx.file_name},
            {"mime_type", os.mime_type},
            {"recipient_device_id", ctx.device.recipient_device_id},
            {"aad_b64", os.aad_b64},
            {"snapshot", {
                {"size_bytes", snap.size_bytes},
                {"mtime_epoch", snap.mtime_epoch},
                {"sha256_hex", snap.sha256_hex}
            }},
            {"wrapped_key", wrapped_key},
            {"stream", {
                {"open_id", os.open_id},
                {"chunk_size_bytes", os.chunk_size_bytes},
                {"chunk_count", os.chunk_count},
                {"chunk_nonce_prefix_b64", os.chunk_nonce_prefix_b64},
                {"chunk_api", "/api/v4/shares/pq/open/chunk"}
            }}
        }}
    });
});

srv.Get("/api/v4/shares/pq/open/chunk", [&](const httplib::Request& req, httplib::Response& res) {
    auto fail = [&](int status, const std::string& err, const std::string& msg = std::string{}) {
        json j{{"ok", false}, {"error", err}};
        if (!msg.empty()) j["message"] = msg;
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

    const std::string open_id = req.get_param_value("open_id");
    const std::string idx_s = req.get_param_value("i");
    if (open_id.empty() || idx_s.empty()) {
        fail(400, "missing_open_id_or_index");
        return;
    }

    std::uint64_t chunk_index = 0;
    try {
        chunk_index = static_cast<std::uint64_t>(std::stoull(idx_s));
    } catch (...) {
        fail(400, "bad_chunk_index");
        return;
    }

    pqnas::PqShareOpenStreamSessionV1 os;
    std::string os_err;
    if (!share_pq.load_open_stream_session(open_id, &os, &os_err)) {
        fail(404, "open_stream_session_not_found");
        return;
    }
    if (os.state != "active") {
        fail(403, "open_stream_session_inactive");
        return;
    }
    if (pqnas::SharePqStoreV1::iso_expired_local(os.expires_at)) {
        fail(410, "open_stream_session_expired");
        return;
    }
    if (chunk_index >= os.chunk_count) {
        fail(416, "chunk_index_out_of_range");
        return;
    }

    // Bind chunk fetch to the same recipient browser session
    auto get_cookie_value_local = [&](const std::string& name) -> std::string {
        auto it = req.headers.find("Cookie");
        if (it == req.headers.end()) return {};
        const std::string all = it->second;
        const std::string needle = name + "=";
        std::size_t pos = all.find(needle);
        if (pos == std::string::npos) return {};
        pos += needle.size();
        std::size_t end = all.find(';', pos);
        if (end == std::string::npos) end = all.size();
        return all.substr(pos, end - pos);
    };
    if (get_cookie_value_local("pqnas_share_recipient") != os.recipient_session_id) {
        fail(403, "recipient_session_mismatch");
        return;
    }

    pqnas::ResolvedExistingPath rp;
    std::string rerr;
    if (!pqnas::resolve_existing_user_file_path(users, os.owner_fp, os.rel_path, &rp, &rerr)) {
        fail(404, "file_not_found");
        return;
    }

    const std::uint64_t offset = chunk_index * os.chunk_size_bytes;
    const std::uint64_t remain = (offset < os.file_size_bytes) ? (os.file_size_bytes - offset) : 0;
    const std::uint64_t this_plain_size = std::min<std::uint64_t>(os.chunk_size_bytes, remain);

    std::ifstream f(rp.abs_path, std::ios::binary);
    if (!f) {
        fail(404, "file_open_failed");
        return;
    }
    f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!f) {
        fail(500, "file_seek_failed");
        return;
    }

    std::vector<std::uint8_t> plain(static_cast<std::size_t>(this_plain_size), 0);
    if (this_plain_size > 0) {
        f.read(reinterpret_cast<char*>(plain.data()), static_cast<std::streamsize>(this_plain_size));
        if (static_cast<std::uint64_t>(f.gcount()) != this_plain_size) {
            fail(500, "file_read_failed");
            return;
        }
    }

    std::vector<std::uint8_t> cek;
    std::vector<std::uint8_t> nonce_prefix;
    std::vector<std::uint8_t> base_aad;
    if (!b64_decode_bytes_local(os.cek_b64, &cek) ||
        !b64_decode_bytes_local(os.chunk_nonce_prefix_b64, &nonce_prefix) ||
        !b64_decode_bytes_local(os.aad_b64, &base_aad)) {
        fail(500, "open_stream_session_decode_failed");
        return;
    }

    std::vector<std::uint8_t> iv;
    std::vector<std::uint8_t> chunk_aad;
    if (!make_chunk_iv_v2_local(nonce_prefix, static_cast<std::uint32_t>(chunk_index), &iv) ||
        !make_chunk_aad_v2_local(base_aad,
                                 static_cast<std::uint32_t>(chunk_index),
                                 static_cast<std::uint32_t>(this_plain_size),
                                 &chunk_aad)) {
        fail(500, "chunk_crypto_params_failed");
        return;
    }

    std::vector<std::uint8_t> ciphertext;
    std::string enc_err;
    if (!pqnas::encrypt_aes256gcm_bytes_v1(cek, iv, chunk_aad, plain, &ciphertext, &enc_err)) {
        fail(500, "chunk_encrypt_failed", enc_err);
        return;
    }

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Content-Type", "application/octet-stream");
    res.set_content(reinterpret_cast<const char*>(ciphertext.data()),
                    static_cast<size_t>(ciphertext.size()),
                    "application/octet-stream");
});

srv.Post("/api/v4/shares/pq/open", [&](const httplib::Request& req, httplib::Response& res) {
    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

    auto json_string = [](const json& j, const char* key) -> std::string {
        auto it = j.find(key);
        return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
    };

    json body = json::object();
    try {
        if (!req.body.empty()) body = json::parse(req.body);
    } catch (...) {
        reply(400, {
            {"ok", false},
            {"error", "bad_json"},
            {"message", "Request body must be valid JSON"}
        });
        return;
    }

    const std::string share_token = json_string(body, "share_token");
    if (share_token.empty()) {
        reply(400, {
            {"ok", false},
            {"error", "missing_share_token"},
            {"message", "share_token is required"}
        });
        return;
    }

    PqShareOpenContextV1 ctx;
    std::string ctx_err;

    if (!resolve_pq_share_open_context_v1(req, share_token, &ctx, &ctx_err)) {
        if (ctx_err == "share_not_found") {
            reply(404, {
                {"ok", false},
                {"error", "share_not_found"}
            });
            return;
        }

        if (ctx_err == "enrollment_required") {
            reply(403, {
                {"ok", false},
                {"error", "enrollment_required"},
                {"message", "This protected share requires browser enrollment first"}
            });
            return;
        }

        if (ctx_err == "share_expired") {
            reply(410, {
                {"ok", false},
                {"error", "share_expired"}
            });
            return;
        }

        if (ctx_err == "snapshot_mismatch") {
            reply(409, {
                {"ok", false},
                {"error", "snapshot_mismatch"},
                {"message", "Shared file changed since PQ share snapshot was created"}
            });
            return;
        }

        reply(403, {
            {"ok", false},
            {"error", ctx_err.empty() ? "pq_open_denied" : ctx_err}
        });
        return;
    }

    if (ctx.device.public_key_b64.empty()) {
        reply(403, {
            {"ok", false},
            {"error", "recipient_public_key_missing"}
        });
        return;
    }

    std::vector<std::uint8_t> plaintext;
    {
        std::ifstream f(ctx.abs_path, std::ios::binary);
        if (!f) {
            reply(404, {
                {"ok", false},
                {"error", "file_not_found"}
            });
            return;
        }
        plaintext.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }

    std::string device_kem_alg = ctx.device.kem_alg;
    if (device_kem_alg == "ml-kem-768" || device_kem_alg == "mlkem768" || device_kem_alg == "MLKEM768") {
        device_kem_alg = "ML-KEM-768";
    }

    if (device_kem_alg != "ML-KEM-768") {
        reply(400, {
            {"ok", false},
            {"error", "unsupported_recipient_kem_alg"},
            {"message", "Only ML-KEM-768 recipients are supported"}
        });
        return;
    }

    const std::string envelope_mode = "mlkem768_aes256gcm_v1";

    const std::string mime_type = guess_mime_type_local(ctx.file_name);

    json aad = {
        {"v", 1},
        {"purpose", "pq_share_open"},
        {"mode", envelope_mode},
        {"share_token", ctx.share_token},
        {"recipient_device_id", ctx.device.recipient_device_id},
        {"file_name", ctx.file_name},
        {"snapshot_sha256_hex", ctx.manifest.snapshot.sha256_hex},
        {"snapshot_size_bytes", ctx.manifest.snapshot.size_bytes},
        {"snapshot_mtime_epoch", ctx.manifest.snapshot.mtime_epoch}
    };

    pqnas::PqOpenSnapshotV1 snap;
    snap.size_bytes = ctx.manifest.snapshot.size_bytes;
    snap.mtime_epoch = ctx.manifest.snapshot.mtime_epoch;
    snap.sha256_hex = ctx.manifest.snapshot.sha256_hex;

    pqnas::PqOpenEnvelopeV1 env;
    std::string crypto_err;

    const bool build_ok = pqnas::build_pq_open_envelope_mlkem768_v1(
        ctx.share_token,
        ctx.file_name,
        ctx.device.recipient_device_id,
        ctx.device.public_key_b64,
        plaintext,
        aad.dump(),
        snap,
        &env,
        &crypto_err
    );

    if (!build_ok) {
        reply(500, {
            {"ok", false},
            {"error", "pq_envelope_build_failed"},
            {"detail", crypto_err}
        });
        return;
    }

    json wrapped_key = {
        {"mode", env.wrapped_key.mode},
        {"recipient_device_id", env.wrapped_key.recipient_device_id},
        {"kem_alg", env.wrapped_key.kem_alg},
        {"hkdf_salt_b64", env.wrapped_key.hkdf_salt_b64},
        {"hkdf_info_b64", env.wrapped_key.hkdf_info_b64},
        {"wrap_iv_b64", env.wrapped_key.wrap_iv_b64},
        {"wrapped_cek_b64", env.wrapped_key.wrapped_cek_b64}
    };

    if (!env.wrapped_key.sender_public_key_b64.empty()) {
        wrapped_key["sender_public_key_b64"] = env.wrapped_key.sender_public_key_b64;
    }
    if (!env.wrapped_key.kem_ciphertext_b64.empty()) {
        wrapped_key["kem_ciphertext_b64"] = env.wrapped_key.kem_ciphertext_b64;
    }

    reply(200, {
        {"ok", true},
        {"envelope", {
            {"version", env.version},
            {"mode", env.mode},
            {"share_token", env.share_token},
            {"file_name", env.file_name},
            {"mime_type", env.mime_type},
            {"recipient_device_id", env.recipient_device_id},
            {"aad_b64", env.aad_b64},
            {"snapshot", {
                {"size_bytes", env.snapshot.size_bytes},
                {"mtime_epoch", env.snapshot.mtime_epoch},
                {"sha256_hex", env.snapshot.sha256_hex}
            }},
            {"wrapped_key", wrapped_key},
            {"payload", {
                {"enc_alg", env.payload.enc_alg},
                {"iv_b64", env.payload.iv_b64},
                {"ciphertext_b64", env.payload.ciphertext_b64}
            }}
        }}
    });
});

srv.Post("/api/v4/shares/pq/recipient/update", [&](const httplib::Request& req, httplib::Response& res) {
    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };
    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);
        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);
        ev.f["ua"] = audit_ua();
    };

    json body;
    try {
        body = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (const std::exception& e) {
        reply(400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"},
            {"detail", e.what()}
        });
        return;
    }

    const std::string share_token = body.value("share_token", std::string{});
    const std::string recipient_device_id = body.value("recipient_device_id", std::string{});

    if (share_token.empty() || recipient_device_id.empty()) {
        reply(400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing share_token or recipient_device_id"}
        });
        return;
    }

    const bool has_label = body.contains("label") && body["label"].is_string();
    const bool has_note  = body.contains("note")  && body["note"].is_string();

    if (!has_label && !has_note) {
        reply(400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "nothing to update"}
        });
        return;
    }

    pqnas::ShareLink share_row;
    bool share_found = false;
    for (const auto& s : shares.list()) {
        if (s.token == share_token && s.owner_fp == fp_hex) {
            share_row = s;
            share_found = true;
            break;
        }
    }

    if (!share_found) {
        reply(404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "share not found"}
        });
        return;
    }

    pqnas::PqShareManifestV1 mf;
    std::string mf_err;
    if (!share_pq.load_manifest(share_token, &mf, &mf_err) || mf.kind != "pq_recipient_enrolled_v1") {
        reply(404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "PQ share manifest not found"}
        });
        return;
    }

    const bool recipient_allowed =
        std::find(mf.recipient_device_ids.begin(),
                  mf.recipient_device_ids.end(),
                  recipient_device_id) != mf.recipient_device_ids.end();

    if (!recipient_allowed) {
        reply(404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "recipient device not found for this share"}
        });
        return;
    }

    pqnas::PqShareRecipientDeviceV1 dev;
    std::string dev_err;
    if (!share_pq.load_recipient_device(fp_hex, recipient_device_id, &dev, &dev_err)) {
        reply(404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "recipient device not found"}
        });
        return;
    }

    if (has_label) dev.label = body["label"].get<std::string>();
    if (has_note)  dev.note  = body["note"].get<std::string>();
    dev.updated_at = pqnas::SharePqStoreV1::now_iso_utc_local();

    std::string save_err;
    if (!share_pq.save_recipient_device(dev, &save_err)) {
        reply(500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "recipient update failed"},
            {"detail", save_err}
        });
        return;
    }

    {
        pqnas::AuditEvent ev;
        ev.event = "share_pq_recipient_update";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["token"] = pqnas::shorten(share_token, 32);
        ev.f["recipient_device_id"] = pqnas::shorten(recipient_device_id, 32);
        if (has_label) ev.f["label"] = pqnas::shorten(dev.label, 120);
        if (has_note) ev.f["note"] = pqnas::shorten(dev.note, 120);
        add_ip_headers(ev);
        audit_append(ev);
    }

    reply(200, json{
        {"ok", true},
        {"recipient", {
            {"recipient_device_id", dev.recipient_device_id},
            {"label", dev.label},
            {"note", dev.note},
            {"state", dev.state},
            {"created_at", dev.created_at},
            {"updated_at", dev.updated_at},
            {"last_used_at", dev.last_used_at},
            {"registered_via", dev.registered_via},
            {"invite_id", dev.invite_id},
            {"kem_alg", dev.kem_alg},
            {"key_id", dev.key_id}
        }}
    });
});

    auto share_scope_kind_normalized_local = [](const pqnas::ShareLink& s) -> std::string {
    return (s.scope_kind == "workspace") ? "workspace" : "user";
};

auto resolve_share_abs_path_local =
    [&](const pqnas::ShareLink& s,
        std::filesystem::path* out_abs,
        std::string* out_err) -> bool {

    if (out_abs) *out_abs = std::filesystem::path{};
    if (out_err) out_err->clear();

    const std::string scope_kind = share_scope_kind_normalized_local(s);

    if (scope_kind == "user") {
        pqnas::ResolvedExistingPath rp;
        std::string rerr;
        if (!pqnas::resolve_existing_user_file_path(users, s.owner_fp, s.path, &rp, &rerr)) {
            if (out_err) *out_err = rerr.empty() ? "share_not_found" : rerr;
            return false;
        }
        if (out_abs) *out_abs = rp.abs_path;
        return true;
    }

    if (!workspaces.load(workspaces_path)) {
        if (out_err) *out_err = "failed to reload workspaces";
        return false;
    }

    auto wopt = workspaces.get(s.workspace_id);
    if (!wopt.has_value()) {
        if (out_err) *out_err = "workspace not found";
        return false;
    }

    pqnas::WorkspaceResolvedTarget tgt;
    std::string terr;
    if (!pqnas::resolve_workspace_target_default_pool_only(
            users_path,
            *wopt,
            s.path,
            &tgt,
            &terr)) {
        if (out_err) *out_err = terr.empty() ? "share_not_found" : terr;
        return false;
    }

    if (out_abs) *out_abs = tgt.abs_path;
    return true;
};

auto require_workspace_share_member_local =
    [&](const std::string& actor_fp,
        const std::string& workspace_id,
        bool require_write,
        std::string* out_err) -> bool {

    if (out_err) out_err->clear();

    if (!workspaces.load(workspaces_path)) {
        if (out_err) *out_err = "failed to reload workspaces";
        return false;
    }

    auto wopt = workspaces.get(workspace_id);
    if (!wopt.has_value()) {
        if (out_err) *out_err = "workspace not found";
        return false;
    }

    auto mopt = pqnas::workspace_enabled_member_for_actor(*wopt, actor_fp);
    if (!mopt.has_value()) {
        if (out_err) *out_err = "workspace access denied";
        return false;
    }

    if (require_write && !pqnas::workspace_member_can_write(*mopt)) {
        if (out_err) *out_err = "workspace write access denied";
        return false;
    }

    return true;
};

srv.Post("/api/v4/shares/create", [&](const httplib::Request& req, httplib::Response& res) {
    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

	std::string fp_hex, role;
	if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };
    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);
        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);
        ev.f["ua"] = audit_ua();
    };
    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "share_create";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        add_ip_headers(ev);
        audit_append(ev);
    };
    auto audit_ok = [&](const pqnas::ShareLink& s) {
        pqnas::AuditEvent ev;
        ev.event = "share_create";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["token"] = pqnas::shorten(s.token, 32);
        ev.f["owner_fp"] = s.owner_fp;
        ev.f["path"] = pqnas::shorten(s.path, 200);
        ev.f["type"] = s.type;
        if (!s.expires_at.empty()) ev.f["expires_at"] = s.expires_at;
        add_ip_headers(ev);
        audit_append(ev);
    };

	// user-scoped: any authenticated user can create shares for THEIR storage
	(void)role;
	/*
	if (role != "admin") {
    	audit_fail("not_admin", 403);
    	reply(403, json{{"ok", false}, {"error", "not_authorized"}, {"message", "Admin required"}});
 	   return;
	}
*/

    // Parse body: { "path": "<rel>", "expires_sec": 86400 }
    json body;
    try { body = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (const std::exception& e) {
        audit_fail("json_parse", 400, e.what());
        reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
        return;
    }
    if (!body.is_object() || !body.contains("path") || !body["path"].is_string()) {
        audit_fail("missing_path", 400);
        reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing path"}});
        return;
    }

    std::string path_rel = body["path"].get<std::string>();
    for (char& c : path_rel) if (c == '\\') c = '/';
    while (!path_rel.empty() && path_rel[0] == '/') path_rel.erase(path_rel.begin());
    while (path_rel.size() > 1 && path_rel.back() == '/') path_rel.pop_back();

    // basic safety: reject '-' and CRLF
    if (path_rel.empty() || path_rel[0] == '-' ||
        path_rel.find('\n') != std::string::npos || path_rel.find('\r') != std::string::npos) {
        audit_fail("invalid_path", 400, "bad chars", path_rel);
        reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid path"}});
        return;
    }
    std::string requested_type;
    if (body.contains("type") && body["type"].is_string()) {
        requested_type = body["type"].get<std::string>();
    }

    if (!requested_type.empty() &&
        requested_type != "file" &&
        requested_type != "dir" &&
        requested_type != "album") {
        audit_fail("invalid_type", 400, requested_type, path_rel);
        reply(400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid share type"}
        });
        return;
    }

    const bool requested_album_share = (requested_type == "album");
    // Optional expires_sec
    long long expires_sec = 0;
    if (body.contains("expires_sec")) {
        try {
            if (body["expires_sec"].is_number_integer()) expires_sec = body["expires_sec"].get<long long>();
            else if (body["expires_sec"].is_string()) expires_sec = std::stoll(body["expires_sec"].get<std::string>());
        } catch (...) {}
    }
    if (expires_sec < 0) expires_sec = 0;

    std::string share_mode = "standard";
    if (body.contains("mode") && body["mode"].is_string()) {
        share_mode = body["mode"].get<std::string>();
        if (share_mode.empty()) share_mode = "standard";
    }
    if (share_mode != "standard" && share_mode != "pq_recipient_enrolled_v1") {
        audit_fail("bad_mode", 400, share_mode, path_rel);
        reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid share mode"}});
        return;
    }

    long long invite_expires_sec = 24 * 3600;
    if (body.contains("invite_expires_sec")) {
        try {
            if (body["invite_expires_sec"].is_number_integer()) invite_expires_sec = body["invite_expires_sec"].get<long long>();
            else if (body["invite_expires_sec"].is_string()) invite_expires_sec = std::stoll(body["invite_expires_sec"].get<std::string>());
        } catch (...) {}
    }
    if (invite_expires_sec <= 0) invite_expires_sec = 24 * 3600;

    std::string recipient_label_hint;
    if (body.contains("recipient_label_hint") && body["recipient_label_hint"].is_string()) {
        recipient_label_hint = body["recipient_label_hint"].get<std::string>();
    }

    std::string workspace_id;
    if (body.contains("workspace_id") && body["workspace_id"].is_string()) {
        workspace_id = body["workspace_id"].get<std::string>();
    }

    const bool workspace_scope = !workspace_id.empty();
    const std::string scope_kind = workspace_scope ? "workspace" : "user";
    const bool pq_recipient_enrolled = (share_mode == "pq_recipient_enrolled_v1");

    if (requested_album_share && workspace_scope) {
        audit_fail("album_workspace_not_supported", 400, "", path_rel);
        reply(400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "Album shares are currently user-scoped only"}
        });
        return;
    }

    if (requested_album_share && pq_recipient_enrolled) {
        audit_fail("album_pq_not_supported", 400, "", path_rel);
        reply(400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "Album shares currently support standard links only"}
        });
        return;
    }

    if (workspace_scope && pq_recipient_enrolled) {
        audit_fail("workspace_pq_not_supported", 400, "", path_rel);
        reply(400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "PQ recipient-enrolled shares are not supported for workspaces"}
        });
        return;
    }

    std::string resolved_path_rel = path_rel;
    std::filesystem::path resolved_abs_path;
    std::string type = requested_album_share ? "album" : "";

    if (requested_album_share) {
        bool album_id_ok = !path_rel.empty() && path_rel.size() <= 160;
        for (char c : path_rel) {
            const bool ok =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_' ||
                c == '-';
            if (!ok) {
                album_id_ok = false;
                break;
            }
        }

        if (!album_id_ok) {
            audit_fail("invalid_album_id", 400, "", path_rel);
            reply(400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid album_id"}
            });
            return;
        }

        std::string album_err;
        auto album = gallery_albums_index.get_album("user", fp_hex, path_rel, &album_err);
        if (!album.has_value()) {
            audit_fail("album_not_found", 404, album_err, path_rel);
            reply(404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "album not found"}
            });
            return;
        }

        resolved_path_rel = path_rel;
        type = "album";
    } else if (!workspace_scope) {
        // ---- user-scoped share ----
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value() || uopt->storage_state != "allocated") {
            audit_fail("storage_unallocated", 403, "", path_rel);
            reply(403, json{
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"}
            });
            return;
        }

        pqnas::ResolvedLogicalItem item;
        std::string rerr;
        if (!pqnas::resolve_existing_user_item(users, fp_hex, path_rel, &item, &rerr) || !item.exists) {
            if (rerr == "empty path" || rerr == "invalid path" ||
                rerr == "absolute path not allowed" || rerr == "reserved path") {
                audit_fail("invalid_path", 400, rerr, path_rel);
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid path"}
                });
                return;
            }

            audit_fail("not_found", 404, rerr, path_rel);
            reply(404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            });
            return;
        }

        if (!item.has_physical_anchor) {
            audit_fail("missing_physical_anchor", 404, "", path_rel);
            reply(404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            });
            return;
        }

        std::error_code ec;
        auto st = std::filesystem::symlink_status(item.abs_path, ec);
        if (ec || !std::filesystem::exists(st)) {
            audit_fail("not_found", 404, "", path_rel);
            reply(404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            });
            return;
        }

        if (std::filesystem::is_symlink(st)) {
            audit_fail("symlink_not_supported", 400, "", path_rel);
            reply(400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            });
            return;
        }

        type = item.is_dir ? "dir" : (item.is_file ? "file" : "");
        if (type.empty()) {
            audit_fail("unsupported_type", 400, "", path_rel);
            reply(400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "unsupported path type"}
            });
            return;
        }

        resolved_path_rel = path_rel;
        resolved_abs_path = item.abs_path;
    } else {
        // ---- workspace-scoped share ----
        if (!workspaces.load(workspaces_path)) {
            audit_fail("workspaces_reload_failed", 500, "", path_rel);
            reply(500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to reload workspaces"}
            });
            return;
        }

        auto wopt = workspaces.get(workspace_id);
        if (!wopt.has_value()) {
            audit_fail("workspace_not_found", 404, workspace_id, path_rel);
            reply(404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "workspace not found"}
            });
            return;
        }

        pqnas::WorkspaceResolvedTarget ws_target;
        std::string ws_err;
        if (!pqnas::resolve_workspace_member_target_default_pool_only(
                users_path,
                *wopt,
                fp_hex,
                true,
                path_rel,
                &ws_target,
                &ws_err)) {

            if (ws_err == "workspace access denied" ||
                ws_err == "workspace write access denied" ||
                ws_err == "workspace disabled" ||
                ws_err == "workspace storage not allocated") {
                audit_fail("workspace_forbidden", 403, ws_err, path_rel);
                reply(403, json{
                    {"ok", false},
                    {"error", "forbidden"},
                    {"message", ws_err}
                });
                return;
            }

            if (ws_err == "workspace not found" || ws_err == "path not found") {
                audit_fail("not_found", 404, ws_err, path_rel);
                reply(404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "path not found"}
                });
                return;
            }

            if (ws_err == "pool not supported yet") {
                audit_fail("pool_not_supported_yet", 400, ws_err, path_rel);
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", ws_err}
                });
                return;
            }

            audit_fail("invalid_workspace_path", 400, ws_err, path_rel);
            reply(400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", ws_err.empty() ? "invalid path" : ws_err}
            });
            return;
        }

        resolved_path_rel = ws_target.rel_norm;
        resolved_abs_path = ws_target.abs_path;
        type = ws_target.is_dir ? "dir" : (ws_target.is_file ? "file" : "");

        if (type.empty()) {
            audit_fail("unsupported_type", 400, "", path_rel);
            reply(400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "unsupported path type"}
            });
            return;
        }
    }

    if (pq_recipient_enrolled) {
        if (type != "file") {
            audit_fail("pq_share_requires_file", 400, "", resolved_path_rel);
            reply(400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "PQ recipient-enrolled shares currently support files only"}
            });
            return;
        }
    }

    pqnas::ShareLink out;
    std::string err;

    {
        pqnas::AuditEvent ev;
        ev.event = "share_create_attempt";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(resolved_path_rel, 200);
        ev.f["type"] = pqnas::shorten(type, 32);
        ev.f["expires_sec"] = std::to_string((long long)expires_sec);
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);
    }
    if (!shares.create_scoped(fp_hex, scope_kind, workspace_id, resolved_path_rel, type, expires_sec, &out, &err)) {
        reply(500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "share create failed"},
            {"detail", err}
        });
        return;
    }
        pqnas::PqShareCreateResultV1 pq_out;
        if (pq_recipient_enrolled) {
            std::string pq_err;
            if (!share_pq.create_recipient_enrolled_share(
                    out.token,
                    fp_hex,
                    resolved_path_rel,
                    out.created_at,
                    out.expires_at,
                    resolved_abs_path,
                    invite_expires_sec,
                    recipient_label_hint,
                    &pq_out,
                    &pq_err)) {

                std::string rollback_err;
                (void)shares.revoke_owner(fp_hex, out.token, &rollback_err);

                pqnas::AuditEvent ev;
                ev.event = "share_pq_create";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp_hex;
                ev.f["token"] = pqnas::shorten(out.token, 32);
                ev.f["path"] = pqnas::shorten(resolved_path_rel, 200);
                ev.f["reason"] = "pq_sidecar_create_failed";
                ev.f["detail"] = pqnas::shorten(pq_err, 180);
                add_ip_headers(ev);
                audit_append(ev);

                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "PQ share create failed"}
                });
                return;
            }

            pqnas::AuditEvent ev;
            ev.event = "share_pq_create";
            ev.outcome = "ok";
            ev.f["fingerprint"] = fp_hex;
            ev.f["token"] = pqnas::shorten(out.token, 32);
            ev.f["path"] = pqnas::shorten(resolved_path_rel, 200);
            ev.f["mode"] = "pq_recipient_enrolled_v1";
            ev.f["invite_id"] = pqnas::shorten(pq_out.invite.invite_id, 32);
            add_ip_headers(ev);
            audit_append(ev);
        }
    audit_ok(out);
    record_share_activity_best_effort_local(users, fp_hex, "share.created", out);

    json resp = {
        {"ok", true},
        {"token", out.token},
        {"url", std::string("/s/") + out.token},
        {"expires_at", out.expires_at.empty() ? json() : json(out.expires_at)},
        {"type", out.type},
        {"path", out.path},
        {"scope_kind", out.scope_kind.empty() ? "user" : out.scope_kind},
        {"workspace_id", out.workspace_id}
    };

    if (pq_recipient_enrolled) {
        resp["pq_mode"] = "pq_recipient_enrolled_v1";
        resp["pq_state"] = "pending_enrollment";
        resp["invite_url"] = share_pq.invite_url_path(pq_out.invite.invite_id);
    }

    reply(200, resp);
});


    srv.Post("/api/v4/shares/revoke", [&](const httplib::Request& req, httplib::Response& res) {
    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

    std::string fp_hex, role;
	if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };
    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);
        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);
        ev.f["ua"] = audit_ua();
    };
    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& token_short = "") {
        pqnas::AuditEvent ev;
        ev.event = "share_revoke";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!token_short.empty()) ev.f["token"] = token_short;
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        add_ip_headers(ev);
        audit_append(ev);
    };
    auto audit_ok = [&](const std::string& token_short) {
        pqnas::AuditEvent ev;
        ev.event = "share_revoke";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["token"] = token_short;
        add_ip_headers(ev);
        audit_append(ev);
    };

	(void)role;
	/* if we restric to only admin level
    if (role != "admin") {
        audit_fail("not_admin", 403);
        reply(403, json{{"ok", false}, {"error", "not_authorized"}, {"message", "Admin required"}});
        return;
    }
	*/
    json body;
    try { body = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (const std::exception& e) {
        audit_fail("json_parse", 400, e.what());
        reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
        return;
    }

    if (!body.is_object() || !body.contains("token") || !body["token"].is_string()) {
        audit_fail("missing_token", 400);
        reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing token"}});
        return;
    }

    std::string token = body["token"].get<std::string>();
    std::string token_short = pqnas::shorten(token, 32);

    auto sopt = shares.find(token);
    if (!sopt.has_value() || sopt->owner_fp != fp_hex) {
        audit_fail("not_found", 404, "", token_short);
        reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "token not found"}});
        return;
    }

    if ((sopt->scope_kind == "workspace") && !sopt->workspace_id.empty()) {
        std::string werr;
        if (!require_workspace_share_member_local(fp_hex, sopt->workspace_id, true, &werr)) {
            audit_fail("workspace_access_denied", 403, werr, token_short);
            reply(403, json{{"ok", false}, {"error", "forbidden"}, {"message", werr}});
            return;
        }
    }

    std::string err;
    bool removed = shares.revoke_owner(fp_hex, token, &err);
    if (!removed) {
        // If err set => save failed; else token not found
        if (!err.empty()) {
            audit_fail("revoke_failed", 500, err, token_short);
            reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "share revoke failed"}});
        } else {
            audit_fail("not_found", 404, "", token_short);
            reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "token not found"}});
        }
        return;
    }

        (void)share_pq.delete_manifest(token, nullptr);
        (void)share_pq.revoke_invites_for_share(token, nullptr);

        audit_ok(token_short);
        record_share_activity_best_effort_local(users, fp_hex, "share.disabled", *sopt);
        reply(200, json{{"ok", true}});

});


srv.Get("/api/v4/shares/list", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    const std::string workspace_id =
    req.has_param("workspace_id") ? req.get_param_value("workspace_id") : std::string{};

    if (!workspace_id.empty()) {
        std::string werr;
        if (!require_workspace_share_member_local(fp_hex, workspace_id, false, &werr)) {
            res.status = (werr == "workspace not found") ? 404 : ((werr == "failed to reload workspaces") ? 500 : 403);
            res.set_header("Cache-Control", "no-store");
            res.set_content(json{
                {"ok", false},
                {"error", (res.status == 404) ? "not_found" : ((res.status == 500) ? "server_error" : "forbidden")},
                {"message", werr}
            }.dump(2), "application/json; charset=utf-8");
            return;
        }
    }

    const auto v = shares.list();

    bool workspaces_loaded_for_names = false;
    std::string workspaces_names_err;

    auto ensure_workspaces_loaded_for_names = [&]() -> bool {
        if (workspaces_loaded_for_names) return true;
        if (!workspaces.load(workspaces_path)) {
            workspaces_names_err = "failed to reload workspaces";
            return false;
        }
        workspaces_loaded_for_names = true;
        return true;
    };

    json out = {
        {"ok", true},
        {"shares", json::array()}
    };

    for (const auto& s : v) {
        if (s.owner_fp != fp_hex) continue;

        const std::string scope_kind = (s.scope_kind == "workspace") ? "workspace" : "user";

        // No workspace_id => return all shares owned by this user.
        // workspace_id present => return only shares from that workspace.
        if (!workspace_id.empty()) {
            if (scope_kind != "workspace") continue;
            if (s.workspace_id != workspace_id) continue;
        }

        json it;
        it["token"] = s.token;
        it["url"] = std::string("/s/") + s.token;
        it["path"] = s.path;
        it["type"] = s.type;
        it["created_at"] = s.created_at;
        it["scope_kind"] = s.scope_kind.empty() ? "user" : s.scope_kind;
        if (!s.workspace_id.empty()) it["workspace_id"] = s.workspace_id;
        if (!s.expires_at.empty()) it["expires_at"] = s.expires_at;
        it["downloads"] = s.downloads;

        if ((s.scope_kind == "workspace") && !s.workspace_id.empty()) {
            if (ensure_workspaces_loaded_for_names()) {
                auto wopt = workspaces.get(s.workspace_id);
                if (wopt.has_value() && !wopt->name.empty()) {
                    it["workspace_name"] = wopt->name;
                }
            }
        }

        pqnas::PqShareManifestV1 mf;
        std::string mf_err;
        if (share_pq.load_manifest(s.token, &mf, &mf_err)) {
            it["pq_mode"] = mf.kind;
            it["pq_state"] = mf.state;

            it["recipient_count"] = static_cast<int>(mf.recipient_device_ids.size());
            it["recipient_device_ids"] = mf.recipient_device_ids;

            json recipients = json::array();
            for (const auto& rid : mf.recipient_device_ids) {
                pqnas::PqShareRecipientDeviceV1 dev;
                std::string dev_err;
                if (share_pq.load_recipient_device(mf.owner_fp, rid, &dev, &dev_err)) {
                    recipients.push_back(json{
                        {"recipient_device_id", dev.recipient_device_id},
                        {"label", dev.label},
                        {"note", dev.note},
                        {"state", dev.state},
                        {"created_at", dev.created_at},
                        {"updated_at", dev.updated_at},
                        {"last_used_at", dev.last_used_at},
                        {"registered_via", dev.registered_via},
                        {"invite_id", dev.invite_id},
                        {"kem_alg", dev.kem_alg},
                        {"key_id", dev.key_id}
                    });
                } else {
                    recipients.push_back(json{
                        {"recipient_device_id", rid},
                        {"label", ""},
                        {"note", ""},
                        {"state", "missing"}
                    });
                }
            }
            it["recipients"] = std::move(recipients);

            pqnas::PqShareInviteV1 inv;
            std::string inv_err;
            if (share_pq.load_latest_invite_for_share(s.token, &inv, &inv_err)) {
                it["invite_id"] = inv.invite_id;
                it["invite_state"] = inv.state;
                if (!inv.created_at.empty()) it["invite_created_at"] = inv.created_at;
                if (!inv.expires_at.empty()) it["invite_expires_at"] = inv.expires_at;
                if (!inv.label_hint.empty()) it["invite_label_hint"] = inv.label_hint;
                it["invite_claim_count"] = inv.claim_count;
                it["invite_max_claims"] = inv.max_claims;

                const bool invite_live =
                    (inv.state == "pending") &&
                    !pqnas::SharePqStoreV1::iso_expired_local(inv.expires_at);

                if (invite_live) {
                    it["invite_url"] = share_pq.invite_url_path(inv.invite_id);
                }
            }
        }

        out["shares"].push_back(std::move(it));
    }

    const std::string body = out.dump(2);
    std::cerr << "[shares/list] fp=" << (fp_hex.size() > 16 ? fp_hex.substr(0, 12) + "..." : fp_hex)
              << " visible=" << out["shares"].size()
              << " total=" << v.size()
              << " body_bytes=" << body.size()
              << std::endl;

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_content(body, "application/json; charset=utf-8");
});

    // ---- Admin user storage migration/cleanup job routes ----
    {
        AdminUserStorageJobsRoutesContext admin_user_storage_jobs_ctx;
        admin_user_storage_jobs_ctx.users = &users;
        admin_user_storage_jobs_ctx.users_path = users_path;
        admin_user_storage_jobs_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        admin_user_storage_jobs_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };
        admin_user_storage_jobs_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        admin_user_storage_jobs_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };
        admin_user_storage_jobs_ctx.enqueue_migration_job =
            [&](const std::string& actor_fp,
                const std::string& fp,
                const std::string& pool_id,
                const std::string& remote_addr) {
                return user_mig_enqueue_job_fail_closed(actor_fp, fp, pool_id, remote_addr);
            };
        admin_user_storage_jobs_ctx.read_migration_record =
            [&](const std::string& job_id, json* out, std::string* err) {
                return user_mig_record_read(job_id, out, err);
            };
        admin_user_storage_jobs_ctx.enqueue_cleanup_job =
            [&](const std::string& actor_fp,
                const std::string& fp,
                const std::string& expected_active_pool_id,
                const std::string& old_pool_id,
                const std::string& remote_addr) {
                return user_cleanup_enqueue_job_fail_closed(
                    actor_fp,
                    fp,
                    expected_active_pool_id,
                    old_pool_id,
                    remote_addr
                );
            };
        admin_user_storage_jobs_ctx.read_cleanup_record =
            [&](const std::string& job_id, json* out, std::string* err) {
                return user_cleanup_record_read(job_id, out, err);
            };

        register_admin_user_storage_jobs_routes(srv, admin_user_storage_jobs_ctx);
    }


    // ---- Admin storage tiering routes ----
    {
        AdminStorageTieringRoutesContext admin_storage_tiering_ctx;
        admin_storage_tiering_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        admin_storage_tiering_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };
        admin_storage_tiering_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        admin_storage_tiering_ctx.migrate_one_landing_file =
            [&](const std::string& fp, const std::string& rel_norm, std::string* err) {
                return migrate_one_landing_file(users, fp, rel_norm, err);
            };
        admin_storage_tiering_ctx.tiering_status_json =
            [&](json* out_json, std::string* err) -> bool {
                if (out_json) *out_json = json::object();
                if (err) err->clear();

                auto* idx = pqnas::get_file_location_index();
                if (!idx) {
                    if (err) *err = "file location index not initialized";
                    return false;
                }

                pqnas::FileLocationTierSummary summary;
                std::string serr;
                if (!idx->get_tier_summary(&summary, &serr)) {
                    if (err) *err = std::string("failed to read tiering status: ") + pqnas::shorten(serr, 180);
                    return false;
                }

                const UploadTieringConfig upload_cfg = upload_tiering_config();
                const TieringWorkerConfig worker_cfg = tiering_worker_config();

                if (out_json) {
                    *out_json = json{
                        {"ok", true},
                        {"tiering_enabled", upload_cfg.enabled},
                        {"landing_pool_id", upload_cfg.landing_pool_id},

                        {"worker", {
                            {"enabled", worker_cfg.enabled},
                            {"interval_sec", worker_cfg.interval_sec},
                            {"min_age_sec", worker_cfg.min_age_sec},
                            {"max_candidates_per_pass", worker_cfg.max_candidates_per_pass}
                        }},

                        {"counts", {
                            {"landing_files", summary.landing_files},
                            {"migrating_files", summary.migrating_files},
                            {"capacity_files", summary.capacity_files},
                            {"total_files", summary.total_files}
                        }},

                        {"bytes", {
                            {"landing_bytes", summary.landing_bytes},
                            {"migrating_bytes", summary.migrating_bytes},
                            {"capacity_bytes", summary.capacity_bytes},
                            {"total_bytes", summary.total_bytes}
                        }}
                    };
                }

                return true;
            };

        admin_storage_tiering_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };

        register_admin_storage_tiering_routes(srv, admin_storage_tiering_ctx);
    }

    srv.Post("/api/v4/gallery/export_sel_zip", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          std::uint64_t max_bytes = 0, std::uint64_t paths_n = 0) {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_export_sel_zip_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (max_bytes) ev.f["max_bytes"] = std::to_string((unsigned long long)max_bytes);
        if (paths_n)  ev.f["paths_n"]  = std::to_string((unsigned long long)paths_n);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };
        auto lower_ascii_local = [](std::string s) -> std::string {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };

    auto file_ext_lower = [&](const std::string& name) -> std::string {
        std::string n = name;
        const auto slash = n.find_last_of("/\\");
        if (slash != std::string::npos) n = n.substr(slash + 1);

        auto lower = lower_ascii_local(n);
        const auto dot = lower.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= lower.size()) return "";
        return lower.substr(dot + 1);
    };

    auto is_gallery_image_name = [&](const std::string& name) -> bool {
        const std::string ext = file_ext_lower(name);
        return ext == "png"  ||
               ext == "jpg"  ||
               ext == "jpeg" ||
               ext == "gif"  ||
               ext == "webp" ||
               ext == "svg"  ||
               ext == "bmp"  ||
               ext == "ico"  ||
               ext == "tif"  ||
               ext == "tiff" ||
               ext == "heic" ||
               ext == "heif" ||
               ext == "cr2"  ||
               ext == "cr3"  ||
               ext == "nef"  ||
               ext == "arw"  ||
               ext == "raf"  ||
               ext == "dng"  ||
               ext == "rw2"  ||
               ext == "orf";
    };

    auto pg_xml_escape = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() + 32);
        for (char c : s) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&apos;"; break;
                default: out.push_back(c); break;
            }
        }
        return out;
    };

    auto pg_split_keywords_csv = [](const std::string& s) -> std::vector<std::string> {
        auto trim_ws = [](std::string v) -> std::string {
            auto is_ws = [](unsigned char c) {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            };

            std::size_t a = 0;
            while (a < v.size() && is_ws((unsigned char)v[a])) ++a;

            std::size_t b = v.size();
            while (b > a && is_ws((unsigned char)v[b - 1])) --b;

            return v.substr(a, b - a);
        };

        std::vector<std::string> out;
        std::size_t start = 0;

        while (start <= s.size()) {
            std::size_t comma = s.find(',', start);
            std::string part = (comma == std::string::npos)
                ? s.substr(start)
                : s.substr(start, comma - start);

            part = trim_ws(part);
            if (!part.empty()) out.push_back(part);

            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        return out;
    };

    auto pg_build_xmp_sidecar = [&](int rating,
                                    const std::string& tags_text,
                                    const std::string& notes_text) -> std::string {
        const auto keywords = pg_split_keywords_csv(tags_text);

        std::string x;
        x += "<?xpacket begin=\"﻿\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n";
        x += "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"DNA-Nexus Server\">\n";
        x += " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n";
        x += "  <rdf:Description rdf:about=\"\"\n";
        x += "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n";
        x += "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n";

        if (rating > 0) {
            x += "   <xmp:Rating>" + std::to_string(rating) + "</xmp:Rating>\n";
        }

        if (!notes_text.empty()) {
            x += "   <dc:description>\n";
            x += "    <rdf:Alt>\n";
            x += "     <rdf:li xml:lang=\"x-default\">" + pg_xml_escape(notes_text) + "</rdf:li>\n";
            x += "    </rdf:Alt>\n";
            x += "   </dc:description>\n";
        }

        if (!keywords.empty()) {
            x += "   <dc:subject>\n";
            x += "    <rdf:Bag>\n";
            for (const auto& kw : keywords) {
                x += "     <rdf:li>" + pg_xml_escape(kw) + "</rdf:li>\n";
            }
            x += "    </rdf:Bag>\n";
            x += "   </dc:subject>\n";
        }

        x += "  </rdf:Description>\n";
        x += " </rdf:RDF>\n";
        x += "</x:xmpmeta>\n";
        x += "<?xpacket end=\"w\"?>\n";
        return x;
    };
    auto audit_ok = [&](std::uint64_t max_bytes,
                        std::uint64_t input_bytes,
                        std::uint64_t zip_bytes,
                        std::uint64_t files,
                        std::uint64_t dirs,
                        std::uint64_t paths_n,
                        const std::string& base_rel) {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_export_sel_zip_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["max_bytes"] = std::to_string((unsigned long long)max_bytes);
        ev.f["input_bytes"] = std::to_string((unsigned long long)input_bytes);
        ev.f["zip_bytes"] = std::to_string((unsigned long long)zip_bytes);
        ev.f["files"] = std::to_string((unsigned long long)files);
        ev.f["dirs"]  = std::to_string((unsigned long long)dirs);
        ev.f["paths_n"] = std::to_string((unsigned long long)paths_n);
        if (!base_rel.empty()) ev.f["base"] = pqnas::shorten(base_rel, 200);

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    // Parse JSON body
    json body;
    try {
        body = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (const std::exception& e) {
        audit_fail("json_parse", 400, e.what());
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    if (!body.is_object()) {
        audit_fail("json_schema", 400, "body must be object");
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid json schema"}}.dump());
        return;
    }

    if (!body.contains("paths") || !body["paths"].is_array()) {
        audit_fail("missing_paths", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing paths[]"}
        }.dump());
        return;
    }

    // Optional base folder: when provided, zip entries are stored relative to base.
    // Example: base="test" and paths=["test/note.txt"] -> zip entry "note.txt".
    std::string base_rel;
    if (body.contains("base") && body["base"].is_string()) {
        base_rel = body["base"].get<std::string>();

        // normalize slashes + trim leading slashes
        for (char& c : base_rel) if (c == '\\') c = '/';
        while (!base_rel.empty() && base_rel[0] == '/') base_rel.erase(base_rel.begin());

        // strip duplicate slashes
        {
            std::string tmp;
            tmp.reserve(base_rel.size());
            bool prev_slash = false;
            for (char c : base_rel) {
                if (c == '/') {
                    if (prev_slash) continue;
                    prev_slash = true;
                    tmp.push_back(c);
                } else {
                    prev_slash = false;
                    tmp.push_back(c);
                }
            }
            base_rel.swap(tmp);
        }

        // remove trailing slash
        while (!base_rel.empty() && base_rel.back() == '/') base_rel.pop_back();

        // reject unsafe base
        bool bad = false;
        if (!base_rel.empty() && base_rel[0] == '-') bad = true;
        if (base_rel.find('\n') != std::string::npos || base_rel.find('\r') != std::string::npos) bad = true;

        // reject traversal segments
        if (!bad && !base_rel.empty()) {
            size_t start = 0;
            while (start < base_rel.size()) {
                size_t end = base_rel.find('/', start);
                if (end == std::string::npos) end = base_rel.size();
                std::string seg = base_rel.substr(start, end - start);
                if (seg == "." || seg == ".." || seg.empty()) { bad = true; break; }
                start = end + 1;
            }
        }

        if (bad) base_rel.clear();
    }

    /*
     * ZIP output is written to a service-owned temporary file and streamed
     * to the client. The cap protects temporary-disk use and CPU work rather
     * than requiring an equally large in-memory response buffer.
     */
    std::uint64_t max_bytes =
        1ull * 1024 * 1024 * 1024; // 1 GiB default

    if (body.contains("max_bytes")) {
        try {
            long long v = 0;

            if (body["max_bytes"].is_number_integer()) {
                v = body["max_bytes"].get<long long>();
            } else if (body["max_bytes"].is_string()) {
                v = std::stoll(
                    body["max_bytes"].get<std::string>()
                );
            }

            if (v > 0) {
                max_bytes = static_cast<std::uint64_t>(v);
            }
        } catch (...) {}
    }

    const std::uint64_t MINB =
        1ull * 1024 * 1024; // 1 MiB

    const std::uint64_t MAXB =
        2ull * 1024 * 1024 * 1024; // 2 GiB hard clamp

    if (max_bytes < MINB) max_bytes = MINB;
    if (max_bytes > MAXB) max_bytes = MAXB;

    // Collect + basic sanitize
    std::vector<std::string> paths_in;
    paths_in.reserve(body["paths"].size());

    for (const auto& it : body["paths"]) {
        if (!it.is_string()) continue;
        std::string p = it.get<std::string>();

        // normalize slashes + trim leading slashes
        for (char& c : p) if (c == '\\') c = '/';
        while (!p.empty() && p[0] == '/') p.erase(p.begin());

        // strip duplicate slashes
        {
            std::string tmp;
            tmp.reserve(p.size());
            bool prev_slash = false;
            for (char c : p) {
                if (c == '/') {
                    if (prev_slash) continue;
                    prev_slash = true;
                    tmp.push_back(c);
                } else {
                    prev_slash = false;
                    tmp.push_back(c);
                }
            }
            p.swap(tmp);
        }

        // remove trailing slash (we still treat dir fine)
        while (p.size() > 1 && p.back() == '/') p.pop_back();

        if (p.empty()) continue;

        // Safety: reject leading '-' so it can't be treated as an option by zip
        if (!p.empty() && p[0] == '-') continue;

        // Safety: reject traversal segments
        bool bad = false;
        {
            size_t start = 0;
            while (start < p.size()) {
                size_t end = p.find('/', start);
                if (end == std::string::npos) end = p.size();
                std::string seg = p.substr(start, end - start);
                if (seg == "." || seg == ".." || seg.empty()) { bad = true; break; }
                start = end + 1;
            }
        }
        if (bad) continue;

        // prevent CR/LF injection into -@ stdin list
        if (p.find('\n') != std::string::npos || p.find('\r') != std::string::npos) continue;

        paths_in.push_back(std::move(p));
    }

    // Hard cap selection count to avoid abuse
    const std::size_t MAX_PATHS = 500;
    if (paths_in.empty()) {
        audit_fail("no_valid_paths", 400, "", max_bytes, 0);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "no valid paths"}
        }.dump());
        return;
    }
    if (paths_in.size() > MAX_PATHS) {
        audit_fail("too_many_paths", 413, "paths[] too large", max_bytes, (std::uint64_t)paths_in.size());
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "too_large"},
            {"message", "too many selected paths"}
        }.dump());
        return;
    }

    // Dedupe + drop children if parent dir is selected
    std::sort(paths_in.begin(), paths_in.end());
    paths_in.erase(std::unique(paths_in.begin(), paths_in.end()), paths_in.end());

    std::vector<std::string> paths_rel;
    paths_rel.reserve(paths_in.size());

    auto is_child_of = [&](const std::string& child, const std::string& parent) -> bool {
        if (child.size() <= parent.size()) return false;
        if (child.compare(0, parent.size(), parent) != 0) return false;
        return child[parent.size()] == '/';
    };

    for (const auto& p : paths_in) {
        if (paths_rel.empty()) {
            paths_rel.push_back(p);
            continue;
        }
        bool covered = false;
        for (const auto& sel : paths_rel) {
            if (is_child_of(p, sel)) { covered = true; break; }
        }
        if (!covered) paths_rel.push_back(p);
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    // If base is provided, ensure it's a real directory under user_dir (fail-closed).
    std::filesystem::path base_abs;
    if (!base_rel.empty()) {
        std::string berr;
        if (!pqnas::resolve_user_path_strict(user_dir, base_rel, &base_abs, &berr)) {
            audit_fail("invalid_base", 400, berr, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid base"}}.dump());
            return;
        }

        {
            std::string serr;
            if (!ensure_no_symlink_in_existing_path_prefix(base_abs, &serr)) {
                audit_fail("invalid_base", 400, serr, max_bytes, (std::uint64_t)paths_rel.size());
                reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid base"}}.dump());
                return;
            }
        }

        std::error_code bec;
        auto bst = std::filesystem::symlink_status(base_abs, bec);
        if (bec || !std::filesystem::exists(bst) || !std::filesystem::is_directory(bst) || std::filesystem::is_symlink(bst)) {
            audit_fail("invalid_base", 400, "base must be an existing directory", max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid base"}}.dump());
            return;
        }
    }

    // If base is set, require all selected paths to be within base.
    // This prevents weird results when we chdir into base but zip tries to access paths outside it.
    if (!base_rel.empty()) {
        for (const auto& p : paths_rel) {
            if (p == base_rel) continue;
            if (!is_child_of(p, base_rel)) {
                audit_fail("path_outside_base", 400, pqnas::shorten(p, 180), max_bytes, (std::uint64_t)paths_rel.size());
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "selected path outside base"}
                }.dump());
                return;
            }
        }
    }

    // Resolve + pre-walk all selections (size + symlink checks)
    std::uint64_t files = 0, dirs = 0, input_bytes = 0;

    for (const auto& path_rel : paths_rel) {
        std::filesystem::path path_abs;
        std::string err;
        if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &path_abs, &err)) {
            audit_fail("invalid_path", 400, err, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid path"}}.dump());
            return;
        }

        std::error_code ec;
        {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs, &serr)) {
            audit_fail("symlink_not_supported", 400, serr, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported for zip download"}
            }.dump());
            return;
        }
    }
        auto st = std::filesystem::symlink_status(path_abs, ec);
        if (ec || !std::filesystem::exists(st)) {
            audit_fail("not_found", 404, path_rel, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 404, json{{"ok",false},{"error","not_found"},{"message","path not found"}}.dump());
            return;
        }

        if (std::filesystem::is_symlink(st)) {
            audit_fail("symlink_not_supported", 400, "symlink selected", max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","symlinks not supported for zip download"}}.dump());
            return;
        }

        const bool is_file = std::filesystem::is_regular_file(st);
        const bool is_dir  = std::filesystem::is_directory(st);

        if (is_file) {
            files += 1;
            input_bytes += pqnas::file_size_u64_safe(path_abs);
            if (input_bytes > max_bytes) break;
            continue;
        }

        if (is_dir) {
            dirs += 1; // include selected root dir

            std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;
            ec.clear();
            for (auto it = std::filesystem::recursive_directory_iterator(path_abs, opts, ec);
                 it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec)) {

                if (ec) {
                    audit_fail("walk_failed", 500, ec.message(), max_bytes, (std::uint64_t)paths_rel.size());
                    reply_json(res, 500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "directory walk failed"},
                        {"detail", pqnas::shorten(ec.message(), 180)}
                    }.dump());
                    return;
                }

                std::error_code ec2;
                auto st2 = std::filesystem::symlink_status(it->path(), ec2);
                if (ec2) continue;

                if (std::filesystem::is_symlink(st2)) {
                    audit_fail("symlink_not_supported", 400, "symlink inside tree", max_bytes, (std::uint64_t)paths_rel.size());
                    reply_json(res, 400, json{
                        {"ok", false},
                        {"error", "bad_request"},
                        {"message", "symlinks inside directory are not supported for zip download"}
                    }.dump());
                    return;
                }

                if (std::filesystem::is_regular_file(st2)) {
                    files += 1;
                    input_bytes += pqnas::file_size_u64_safe(it->path());
                    if (input_bytes > max_bytes) break;
                    continue;
                }

                files += 1; // other types, 0 bytes
            }
            if (input_bytes > max_bytes) break;
            continue;
        }

        audit_fail("unsupported_type", 400, path_rel, max_bytes, (std::uint64_t)paths_rel.size());
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","unsupported path type for zip download"}}.dump());
        return;
    }

    if (input_bytes > max_bytes) {
        audit_fail("too_large", 413, "input exceeds max_bytes", max_bytes, (std::uint64_t)paths_rel.size());
        reply_json(res, 413, json{{"ok",false},{"error","too_large"},{"message","selected content exceeds max_bytes"}}.dump());
        return;
    }

        auto* gidx = pqnas::get_gallery_meta_index();
    if (!gidx) {
        audit_fail("gallery_meta_index_missing", 500, "", max_bytes, (std::uint64_t)paths_rel.size());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "gallery metadata index missing"}
        }.dump());
        return;
    }

    auto logical_to_export_rel = [&](const std::string& logical_rel) -> std::string {
        if (base_rel.empty()) return logical_rel;
        if (logical_rel == base_rel) return "";
        if (logical_rel.size() > base_rel.size() &&
            logical_rel.compare(0, base_rel.size(), base_rel) == 0 &&
            logical_rel[base_rel.size()] == '/') {
            return logical_rel.substr(base_rel.size() + 1);
        }
        return logical_rel;
    };

    auto has_exportable_meta = [](int rating,
                                  const std::string& tags_text,
                                  const std::string& notes_text) -> bool {
        return rating > 0 || !tags_text.empty() || !notes_text.empty();
    };

    std::filesystem::path stage_root;
    std::filesystem::path archive_path;
    int archive_fd = -1;

    auto cleanup_stage = [&]() {
        if (archive_fd >= 0) {
            ::close(archive_fd);
            archive_fd = -1;
        }

        if (!archive_path.empty()) {
            std::error_code ec;
            std::filesystem::remove(archive_path, ec);
            archive_path.clear();
        }

        if (!stage_root.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(stage_root, ec);
            stage_root.clear();
        }
    };

    {
        // Security: public share/gallery export staging contains user-selected
        // content. Do not allow standalone tmp-dir overrides; derive it from the
        // trusted runtime data-root layout so staging stays under the PQ-NAS
        // service-owned runtime tree.
        const std::filesystem::path tmp_root_path =
            pqnas::data_root_path().parent_path() / "tmp";
        const std::string tmp_root = tmp_root_path.string();

        std::error_code tmp_ec;
        std::filesystem::create_directories(tmp_root, tmp_ec);
        if (tmp_ec) {
            audit_fail("tmp_dir_create_failed", 500, tmp_ec.message(), max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "export staging failed"}
            }.dump());
            return;
        }

        std::string tmpl = (std::filesystem::path(tmp_root) / "dna_gallery_export_XXXXXX").string();
        std::vector<char> tmpl_buf(tmpl.begin(), tmpl.end());
        tmpl_buf.push_back('\0');

        char* made = ::mkdtemp(tmpl_buf.data());
        if (!made) {
            audit_fail("mkdtemp_failed", 500, "mkdtemp()", max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "export staging failed"}
            }.dump());
            return;
        }
        stage_root = made;

        /*
         * Security: create the archive atomically with mode 0600. Writing ZIP
         * output through this open descriptor prevents symlink/path races.
         */
        std::string archive_tmpl =
            (
                tmp_root_path /
                "dna_gallery_export_zip_XXXXXX"
            ).string();

        std::vector<char> archive_buf(
            archive_tmpl.begin(),
            archive_tmpl.end()
        );
        archive_buf.push_back('\0');

        archive_fd = ::mkstemp(archive_buf.data());

        if (archive_fd < 0) {
            cleanup_stage();

            audit_fail(
                "mkstemp_failed",
                500,
                "mkstemp()",
                max_bytes,
                static_cast<std::uint64_t>(paths_rel.size())
            );

            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "export staging failed"}
            }.dump());

            return;
        }

        archive_path = archive_buf.data();
    }

    auto stage_one_file = [&](const std::filesystem::path& src_abs,
                              const std::filesystem::path& dst_abs,
                              const std::string& logical_rel_file,
                              std::string* err) -> bool {
        std::error_code ec;

        std::filesystem::create_directories(dst_abs.parent_path(), ec);
        if (ec) {
            if (err) *err = "create_directories failed: " + ec.message();
            return false;
        }

        ec.clear();
        std::filesystem::create_hard_link(src_abs, dst_abs, ec);
        if (ec) {
            ec.clear();
            if (!std::filesystem::copy_file(src_abs, dst_abs,
                                            std::filesystem::copy_options::overwrite_existing, ec)) {
                if (err) *err = "copy_file failed: " + ec.message();
                return false;
            }
        }

        const std::string fname = dst_abs.filename().string();
        if (!is_gallery_image_name(fname)) return true;

        std::string gerr;
        auto grec = gidx->get("user", fp_hex, logical_rel_file, &gerr);
        if (!gerr.empty()) {
            if (err) *err = "gallery metadata lookup failed: " + gerr;
            return false;
        }

        if (!grec.has_value()) return true;
        if (!has_exportable_meta(grec->rating, grec->tags_text, grec->notes_text)) return true;

        const std::filesystem::path sidecar_abs =
            dst_abs.parent_path() / (dst_abs.stem().string() + ".xmp");

        ec.clear();
        if (std::filesystem::exists(sidecar_abs, ec)) {
            if (err) *err = "sidecar path conflict: " + sidecar_abs.string();
            return false;
        }

        const std::string xmp = pg_build_xmp_sidecar(
            grec->rating,
            grec->tags_text,
            grec->notes_text
        );

        std::ofstream ofs(sidecar_abs, std::ios::binary);
        if (!ofs) {
            if (err) *err = "failed to create sidecar: " + sidecar_abs.string();
            return false;
        }

        ofs.write(xmp.data(), (std::streamsize)xmp.size());
        if (!ofs.good()) {
            if (err) *err = "failed to write sidecar: " + sidecar_abs.string();
            return false;
        }

        return true;
    };

    for (const auto& path_rel : paths_rel) {
        std::filesystem::path src_abs;
        std::string err;
        if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &src_abs, &err)) {
            cleanup_stage();
            audit_fail("invalid_path", 400, err, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }

        const std::string export_rel = logical_to_export_rel(path_rel);
        const std::filesystem::path dst_root =
            export_rel.empty() ? stage_root : (stage_root / export_rel);

        std::error_code ec;
        auto st = std::filesystem::symlink_status(src_abs, ec);
        if (ec || !std::filesystem::exists(st)) {
            cleanup_stage();
            audit_fail("not_found", 404, path_rel, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            }.dump());
            return;
        }

        if (std::filesystem::is_regular_file(st)) {
            std::string serr;
            if (!stage_one_file(src_abs, dst_root, path_rel, &serr)) {
                cleanup_stage();
                audit_fail("stage_file_failed", 500, serr, max_bytes, (std::uint64_t)paths_rel.size());
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "export staging failed"},
                    {"detail", pqnas::shorten(serr, 180)}
                }.dump());
                return;
            }
            continue;
        }

        if (std::filesystem::is_directory(st)) {
            ec.clear();
            std::filesystem::create_directories(dst_root, ec);
            if (ec) {
                cleanup_stage();
                audit_fail("stage_dir_failed", 500, ec.message(), max_bytes, (std::uint64_t)paths_rel.size());
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "export staging failed"},
                    {"detail", pqnas::shorten(ec.message(), 180)}
                }.dump());
                return;
            }

            std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;
            for (auto it = std::filesystem::recursive_directory_iterator(src_abs, opts, ec);
                 it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec)) {

                if (ec) {
                    cleanup_stage();
                    audit_fail("stage_walk_failed", 500, ec.message(), max_bytes, (std::uint64_t)paths_rel.size());
                    reply_json(res, 500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "export staging failed"},
                        {"detail", pqnas::shorten(ec.message(), 180)}
                    }.dump());
                    return;
                }

                std::error_code ec2;
                auto st2 = std::filesystem::symlink_status(it->path(), ec2);
                if (ec2) continue;
                if (std::filesystem::is_symlink(st2)) continue;

                auto rel_inside = std::filesystem::relative(it->path(), src_abs, ec2);
                if (ec2) {
                    cleanup_stage();
                    audit_fail("relative_failed", 500, ec2.message(), max_bytes, (std::uint64_t)paths_rel.size());
                    reply_json(res, 500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "export staging failed"},
                        {"detail", pqnas::shorten(ec2.message(), 180)}
                    }.dump());
                    return;
                }

                const std::filesystem::path dst_abs = dst_root / rel_inside;

                if (std::filesystem::is_directory(st2)) {
                    ec2.clear();
                    std::filesystem::create_directories(dst_abs, ec2);
                    if (ec2) {
                        cleanup_stage();
                        audit_fail("stage_dir_failed", 500, ec2.message(), max_bytes, (std::uint64_t)paths_rel.size());
                        reply_json(res, 500, json{
                            {"ok", false},
                            {"error", "server_error"},
                            {"message", "export staging failed"},
                            {"detail", pqnas::shorten(ec2.message(), 180)}
                        }.dump());
                        return;
                    }
                    continue;
                }

                if (std::filesystem::is_regular_file(st2)) {
                    const std::string logical_rel_file =
                        path_rel + "/" + rel_inside.generic_string();

                    std::string serr;
                    if (!stage_one_file(it->path(), dst_abs, logical_rel_file, &serr)) {
                        cleanup_stage();
                        audit_fail("stage_file_failed", 500, serr, max_bytes, (std::uint64_t)paths_rel.size());
                        reply_json(res, 500, json{
                            {"ok", false},
                            {"error", "server_error"},
                            {"message", "export staging failed"},
                            {"detail", pqnas::shorten(serr, 180)}
                        }.dump());
                        return;
                    }
                }
            }

            continue;
        }

        cleanup_stage();
        audit_fail("unsupported_type", 400, path_rel, max_bytes, (std::uint64_t)paths_rel.size());
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "unsupported path type for export"}
        }.dump());
        return;
    }

    pid_t pid = ::fork();

    if (pid < 0) {
        cleanup_stage();

        audit_fail(
            "fork_failed",
            500,
            "fork()",
            max_bytes,
            static_cast<std::uint64_t>(paths_rel.size())
        );

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "zip failed"}
        }.dump());

        return;
    }

    if (pid == 0) {
        /*
         * Resource protection: write ZIP output directly to the temporary
         * file instead of accumulating it in the server process memory.
         */
        if (::dup2(archive_fd, STDOUT_FILENO) < 0) {
            _exit(127);
        }

        ::close(archive_fd);

        if (::chdir(stage_root.c_str()) != 0) {
            _exit(127);
        }

        const char* argv[] = {
            "zip",
            "-r",
            "-q",
            "-",
            ".",
            nullptr
        };

        ::execvp("zip", (char* const*)argv);
        _exit(127);
    }

    ::close(archive_fd);
    archive_fd = -1;

    int zip_status = 0;

    if (::waitpid(pid, &zip_status, 0) < 0) {
        cleanup_stage();

        audit_fail(
            "waitpid_failed",
            500,
            "waitpid()",
            max_bytes,
            static_cast<std::uint64_t>(paths_rel.size())
        );

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "zip failed"}
        }.dump());

        return;
    }

    if (!(
        WIFEXITED(zip_status) &&
        WEXITSTATUS(zip_status) == 0
    )) {
        cleanup_stage();

        audit_fail(
            "zip_failed",
            500,
            "zip exit nonzero",
            max_bytes,
            static_cast<std::uint64_t>(paths_rel.size())
        );

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "zip failed"}
        }.dump());

        return;
    }

    const std::uint64_t zip_bytes =
        pqnas::file_size_u64_safe(archive_path);

    /*
     * Allow bounded ZIP structure overhead while preventing uncontrolled
     * temporary-file growth.
     */
    const std::uint64_t zip_limit =
        max_bytes + 64ull * 1024 * 1024;

    if (zip_bytes == 0 || zip_bytes > zip_limit) {
        cleanup_stage();

        audit_fail(
            "zip_too_large",
            413,
            zip_bytes == 0
                ? "empty zip output"
                : "zip output exceeds limit",
            max_bytes,
            static_cast<std::uint64_t>(paths_rel.size())
        );

        reply_json(res, 413, json{
            {"ok", false},
            {"error", "too_large"},
            {"message", "zip output too large"}
        }.dump());

        return;
    }

    /*
     * The ZIP process has finished reading staged content. Remove staging
     * now and retain only the completed archive for response streaming.
     */
    {
        std::error_code ec;
        std::filesystem::remove_all(stage_root, ec);
        stage_root.clear();
    }

    auto archive_stream = std::make_shared<std::ifstream>(
        archive_path,
        std::ios::binary
    );

    if (!archive_stream->good()) {
        cleanup_stage();

        audit_fail(
            "open_zip_failed",
            500,
            "failed to open generated archive",
            max_bytes,
            static_cast<std::uint64_t>(paths_rel.size())
        );

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "zip failed"}
        }.dump());

        return;
    }

    const std::filesystem::path streamed_archive_path =
        archive_path;

    const std::string fname = "gallery-export.zip";

    audit_ok(
        max_bytes,
        input_bytes,
        zip_bytes,
        files,
        dirs,
        static_cast<std::uint64_t>(paths_rel.size()),
        base_rel
    );

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header(
        "Content-Disposition",
        build_content_disposition("attachment", fname)
    );

    res.set_content_provider(
        static_cast<size_t>(zip_bytes),
        "application/zip",
        [archive_stream](
            size_t offset,
            size_t length,
            httplib::DataSink& sink
        ) mutable {
            if (
                !archive_stream ||
                !archive_stream->is_open()
            ) {
                return false;
            }

            archive_stream->clear();
            archive_stream->seekg(
                static_cast<std::streamoff>(offset),
                std::ios::beg
            );

            if (!archive_stream->good()) {
                return false;
            }

            constexpr size_t kChunk = 64 * 1024;
            std::array<char, kChunk> buffer{};
            size_t remaining = length;

            while (remaining > 0) {
                const size_t wanted =
                    std::min(buffer.size(), remaining);

                archive_stream->read(
                    buffer.data(),
                    static_cast<std::streamsize>(wanted)
                );

                const std::streamsize received =
                    archive_stream->gcount();

                if (received <= 0) {
                    break;
                }

                if (!sink.write(
                    buffer.data(),
                    static_cast<size_t>(received)
                )) {
                    return false;
                }

                remaining -=
                    static_cast<size_t>(received);

                if (
                    static_cast<size_t>(received) <
                    wanted
                ) {
                    break;
                }
            }

            return remaining == 0;
        },
        [archive_stream, streamed_archive_path](
            bool /*success*/
        ) mutable {
            if (
                archive_stream &&
                archive_stream->is_open()
            ) {
                archive_stream->close();
            }

            /*
             * Security/privacy: remove the temporary user export after both
             * successful downloads and interrupted client connections.
             */
            std::error_code ec;
            std::filesystem::remove(
                streamed_archive_path,
                ec
            );
        }
    );
});



    auto public_album_html_escape = [](const std::string& in) -> std::string {
    std::string out;
    out.reserve(in.size() + 16);
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#039;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
};

auto public_album_url_encode = [](const std::string& in) -> std::string {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);

    for (unsigned char c : in) {
        const bool safe =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~';

        if (safe) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }

    return out;
};

auto public_album_normalize_rel = [](std::string p) -> std::string {
    for (char& c : p) {
        if (c == '\\') c = '/';
    }
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
};

auto public_album_mime_for_path = [](std::string p) -> std::string {
    for (char& c : p) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }

    const auto dot = p.find_last_of('.');
    const std::string ext = (dot == std::string::npos) ? "" : p.substr(dot);

    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".tif" || ext == ".tiff") return "image/tiff";
    if (ext == ".heic") return "image/heic";
    if (ext == ".heif") return "image/heif";

    return "";
};

auto public_album_resolve_photo = [&](const pqnas::ShareLink& share,
                                      const std::string& rel_in,
                                      std::filesystem::path* out_abs,
                                      std::string* out_mime,
                                      std::string* err) -> bool {
    if (share.type != "album") {
        if (err) *err = "not album share";
        return false;
    }

    if (share.owner_fp.empty() || share.path.empty()) {
        if (err) *err = "invalid album share";
        return false;
    }

    const std::string rel = public_album_normalize_rel(rel_in);
    if (rel.empty() || rel[0] == '-' ||
        rel.find('\n') != std::string::npos ||
        rel.find('\r') != std::string::npos) {
        if (err) *err = "invalid path";
        return false;
    }

    const std::string mime = public_album_mime_for_path(rel);
    if (mime.empty()) {
        if (err) *err = "not an image";
        return false;
    }

    std::string album_err;
    auto album = gallery_albums_index.get_album("user", share.owner_fp, share.path, &album_err);
    if (!album.has_value()) {
        if (err) *err = "album not found";
        return false;
    }

    std::string items_err;
    const auto items = gallery_albums_index.list_items("user", share.owner_fp, share.path, 5000, &items_err);

    bool in_album = false;
    for (const auto& item : items) {
        if (public_album_normalize_rel(item.logical_rel_path) == rel) {
            in_album = true;
            break;
        }
    }

    if (!in_album) {
        if (err) *err = "photo not in album";
        return false;
    }

    pqnas::ResolvedLogicalItem resolved;
    std::string rerr;
    if (!pqnas::resolve_existing_user_item(users, share.owner_fp, rel, &resolved, &rerr) ||
        !resolved.exists ||
        !resolved.has_physical_anchor ||
        !resolved.is_file) {
        if (err) *err = rerr.empty() ? "photo not found" : rerr;
        return false;
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(resolved.abs_path, ec);
    if (ec || !std::filesystem::exists(st) ||
        std::filesystem::is_symlink(st) ||
        !std::filesystem::is_regular_file(st)) {
        if (err) *err = "photo not found";
        return false;
    }

    if (out_abs) *out_abs = resolved.abs_path;
    if (out_mime) *out_mime = mime;
    return true;
};

srv.Get("/api/public/gallery/album/image", [&](const httplib::Request& req, httplib::Response& res) {
    auto fail = [&](int status, const std::string& msg) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(msg + "\n", "text/plain; charset=utf-8");
    };

    const std::string token = req.has_param("token") ? req.get_param_value("token") : "";
    const std::string rel = req.has_param("path") ? req.get_param_value("path") : "";

    if (token.empty() || rel.empty()) {
        fail(400, "Bad request");
        return;
    }

    pqnas::ShareLink share;
    std::string err;
    auto valid = shares.is_valid_now(token, &share, &err);

    if (!valid.has_value()) {
        fail(404, "Not found");
        return;
    }

    if (valid.value() == false) {
        fail(410, "Expired");
        return;
    }

    if (share.type != "album") {
        fail(404, "Not found");
        return;
    }

    std::filesystem::path abs;
    std::string mime;
    std::string rerr;
    if (!public_album_resolve_photo(share, rel, &abs, &mime, &rerr)) {
        fail(404, "Not found");
        return;
    }

    std::ifstream f(abs, std::ios::binary);
    if (!f.good()) {
        fail(500, "Server error");
        return;
    }

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::string fname = std::filesystem::path(abs).filename().string();
    if (fname.empty()) fname = "photo";

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Content-Disposition", build_content_disposition("inline", fname));
    res.set_content(std::move(data), mime);
});

// Public share download: GET /s/<token>
srv.Get(R"(/s/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
    const std::string token = req.matches[1].str();

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };
    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);
        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);
        ev.f["ua"] = audit_ua();
    };
    auto get_cookie_value = [&](const std::string& name) -> std::string {
        auto it = req.headers.find("Cookie");
        if (it == req.headers.end()) return {};
        const std::string all = it->second;
        const std::string needle = name + "=";

        std::size_t pos = 0;
        while (pos < all.size()) {
            while (pos < all.size() && (all[pos] == ' ' || all[pos] == ';')) ++pos;
            if (pos >= all.size()) break;

            if (all.compare(pos, needle.size(), needle) == 0) {
                std::size_t start = pos + needle.size();
                std::size_t end = all.find(';', start);
                if (end == std::string::npos) end = all.size();
                return all.substr(start, end - start);
            }

            std::size_t next = all.find(';', pos);
            if (next == std::string::npos) break;
            pos = next + 1;
        }
        return {};
    };
    auto audit_event = [&](const std::string& name,
                           const std::string& outcome,
                           const pqnas::ShareLink* s,
                           const std::string& reason = "",
                           int http = 0,
                           const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = name;
        ev.outcome = outcome;
        ev.f["token"] = pqnas::shorten(token, 32);
        if (http) ev.f["http"] = std::to_string(http);
        if (!reason.empty()) ev.f["reason"] = reason;
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        if (s) {
            ev.f["owner_fp"] = s->owner_fp;
            ev.f["path"] = pqnas::shorten(s->path, 200);
            ev.f["type"] = s->type;
            if (!s->expires_at.empty()) ev.f["expires_at"] = s->expires_at;
        }
        add_ip_headers(ev);
        audit_append(ev);
    };

    pqnas::ShareLink s;
    std::string err;
    auto valid = shares.is_valid_now(token, &s, &err);

    if (!valid.has_value()) {

        audit_event("share_download", "fail", nullptr, "not_found", 404);
        res.status = 404;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Not found\n", "text/plain; charset=utf-8");
        return;
    }

    if (valid.value() == false) {
        // expired
        audit_event("share_expired", "ok", &s, "expired", 410);
        audit_event("share_download", "fail", &s, "expired", 410);

        res.status = 410;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Expired\n", "text/plain; charset=utf-8");
        return;
    }

    pqnas::PqShareManifestV1 pq_mf;
    std::string pq_mf_err;
    const bool has_pq_manifest = share_pq.load_manifest(token, &pq_mf, &pq_mf_err);
    const bool is_pq_share = has_pq_manifest && pq_mf.kind == "pq_recipient_enrolled_v1";

    pqnas::PqShareRecipientSessionV1 pq_sess;
    pqnas::PqShareRecipientDeviceV1 pq_rdev;
    bool pq_session_ok = false;

    if (is_pq_share) {
        const std::string session_id = get_cookie_value("pqnas_share_recipient");

        if (!session_id.empty()) {
            std::string sess_err;
            if (share_pq.load_session(session_id, &pq_sess, &sess_err) &&
                pq_sess.state == "active" &&
                !pqnas::SharePqStoreV1::iso_expired_local(pq_sess.expires_at)) {

                std::string rdev_err;
                if (share_pq.load_recipient_device(
                        pq_sess.owner_fp,
                        pq_sess.recipient_device_id,
                        &pq_rdev,
                        &rdev_err) &&
                    pq_rdev.state == "active") {

                    const bool allowed =
                        std::find(
                            pq_mf.recipient_device_ids.begin(),
                            pq_mf.recipient_device_ids.end(),
                            pq_sess.recipient_device_id
                        ) != pq_mf.recipient_device_ids.end();

                    if (allowed) {
                        pq_session_ok = true;
                    }
                }
            }
        }
    }

    if (s.type == "album") {
    if (is_pq_share) {
        audit_event("share_album_open", "fail", &s, "pq_album_not_supported", 400);
        res.status = 400;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Bad request\n", "text/plain; charset=utf-8");
        return;
    }

    std::string album_err;
    auto album = gallery_albums_index.get_album("user", s.owner_fp, s.path, &album_err);
    if (!album.has_value()) {

        audit_event("share_album_open", "fail", &s, "album_not_found", 404, album_err);
        res.status = 404;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Not found\n", "text/plain; charset=utf-8");
        return;
    }

    std::string items_err;
    const auto items = gallery_albums_index.list_items("user", s.owner_fp, s.path, 5000, &items_err);

    const std::string album_name = album->name.empty() ? "Shared album" : album->name;
    const std::string album_desc = album->description;

    std::string album_cover_rel =
        public_album_normalize_rel(album->cover_logical_rel_path);

    if (album_cover_rel.empty() ||
        public_album_mime_for_path(album_cover_rel).empty()) {
        for (const auto& item : items) {
            const std::string rel = public_album_normalize_rel(item.logical_rel_path);
            if (rel.empty()) continue;
            if (public_album_mime_for_path(rel).empty()) continue;

            album_cover_rel = rel;
            break;
        }
    }

    std::string album_cover_url;
    if (!album_cover_rel.empty()) {
        album_cover_url =
            "/api/public/gallery/album/image?token=" +
            public_album_url_encode(token) +
            "&path=" +
            public_album_url_encode(album_cover_rel);
    }

    auto public_share_origin = [&]() -> std::string {
        auto host_it = req.headers.find("Host");
        if (host_it == req.headers.end() || host_it->second.empty()) {
            return "";
        }

        std::string proto = "http";
        auto proto_it = req.headers.find("X-Forwarded-Proto");
        if (proto_it != req.headers.end() && !proto_it->second.empty()) {
            proto = proto_it->second;
            const auto comma = proto.find(',');
            if (comma != std::string::npos) proto = proto.substr(0, comma);
            while (!proto.empty() && proto.back() == ' ') proto.pop_back();
        }

        return proto + "://" + host_it->second;
    };

    const std::string album_cover_abs_url =
        album_cover_url.empty()
            ? ""
            : public_share_origin() + album_cover_url;

    std::ostringstream html;
    html
        << "<!doctype html>"
        << "<html lang='en'>"
        << "<head>"
        << "<meta charset='utf-8'>"
        << "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        << "<title>" << public_album_html_escape(album_name) << " • DNA-Nexus</title>"
        << "<meta property='og:type' content='website'>"
        << "<meta property='og:title' content='" << public_album_html_escape(album_name) << " • DNA-Nexus'>"
        << "<meta property='og:description' content='" << public_album_html_escape(album_desc.empty() ? "Shared Photo Gallery album" : album_desc) << "'>"
        << "<meta name='twitter:card' content='summary_large_image'>"
        << "<meta name='twitter:title' content='" << public_album_html_escape(album_name) << " • DNA-Nexus'>"
        << "<meta name='twitter:description' content='" << public_album_html_escape(album_desc.empty() ? "Shared Photo Gallery album" : album_desc) << "'>";

    if (!album_cover_abs_url.empty()) {
        html
            << "<meta property='og:image' content='" << public_album_html_escape(album_cover_abs_url) << "'>"
            << "<meta name='twitter:image' content='" << public_album_html_escape(album_cover_abs_url) << "'>";
    }

    html
        << "<style>"
        << ":root{color-scheme:dark;}"
        << "*{box-sizing:border-box;}"
        << "body{margin:0;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#050712;color:#e9fbff;}"
        << ".wrap{max-width:1240px;margin:0 auto;padding:24px;}"
        << ".head{display:flex;justify-content:space-between;gap:16px;align-items:flex-end;margin-bottom:20px;}"
        << ".kicker{opacity:.68;font-size:12px;font-weight:800;letter-spacing:.12em;text-transform:uppercase;}"
        << "h1{margin:.2rem 0 0;font-size:clamp(28px,5vw,54px);line-height:1.02;}"
        << ".desc{opacity:.78;max-width:780px;margin-top:10px;line-height:1.5;}"
        << ".count{opacity:.72;font-size:13px;white-space:nowrap;}"
        << ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:14px;}"
        << ".tile{display:block;position:relative;aspect-ratio:1/1;border-radius:18px;overflow:hidden;background:#111827;border:1px solid rgba(255,255,255,.12);box-shadow:0 14px 40px rgba(0,0,0,.32);}"
        << ".tile img{width:100%;height:100%;object-fit:cover;display:block;transition:transform .18s ease,filter .18s ease;}"
        << ".tile:hover img{transform:scale(1.035);filter:brightness(1.08);}"
        << ".name{position:absolute;left:0;right:0;bottom:0;padding:28px 10px 10px;font-size:12px;font-weight:750;color:white;background:linear-gradient(180deg,transparent,rgba(0,0,0,.72));overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}"
        << ".empty{padding:42px;border:1px dashed rgba(255,255,255,.2);border-radius:20px;opacity:.75;text-align:center;}"
        << ".brand{margin-top:28px;opacity:.42;font-size:12px;}"
        << "</style>"
        << "</head>"
        << "<body>"
        << "<div class='wrap'>"
        << "<div class='head'>"
        << "<div>"
        << "<div class='kicker'>DNA-Nexus shared photo album</div>"
        << "<h1>" << public_album_html_escape(album_name) << "</h1>";

    if (!album_desc.empty()) {
        html << "<div class='desc'>" << public_album_html_escape(album_desc) << "</div>";
    }

    html
        << "</div>"
        << "<div class='count'>" << items.size() << " photo" << (items.size() == 1 ? "" : "s") << "</div>"
        << "</div>";

    if (items.empty()) {
        html << "<div class='empty'>This shared album is empty.</div>";
    } else {
        html << "<div class='grid'>";

        for (const auto& item : items) {
            const std::string rel = public_album_normalize_rel(item.logical_rel_path);
            if (rel.empty()) continue;

            if (public_album_mime_for_path(rel).empty()) {
                continue;
            }

            const std::string url =
                "/api/public/gallery/album/image?token=" +
                public_album_url_encode(token) +
                "&path=" +
                public_album_url_encode(rel);

            std::string name = std::filesystem::path(rel).filename().string();
            if (name.empty()) name = rel;

            html
                << "<a class='tile' href='" << url << "' target='_blank' rel='noopener'>"
                << "<img loading='lazy' src='" << url << "' alt='" << public_album_html_escape(name) << "'>"
                << "<div class='name'>" << public_album_html_escape(name) << "</div>"
                << "</a>";
        }

        html << "</div>";
    }

    html
        << "<div class='brand'>Shared with DNA-Nexus Server</div>"
        << "</div>"
        << "</body>"
        << "</html>";

    audit_event("share_album_open", "ok", &s, "", 200);
    (void)shares.increment_downloads(token, &err);

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_content(html.str(), "text/html; charset=utf-8");
    return;
}

    std::filesystem::path abs;
    std::string rerr;
    if (!resolve_share_abs_path_local(s, &abs, &rerr)) {
        audit_event("share_download", "fail", &s, "invalid_path", 400, rerr);
        res.status = 404; // safer: do not leak
        res.set_header("Cache-Control", "no-store");
        res.set_content("Not found\n", "text/plain; charset=utf-8");
        return;
    }

    std::error_code ec_abs;
    auto st_abs = std::filesystem::symlink_status(abs, ec_abs);
    if (ec_abs || !std::filesystem::exists(st_abs) || std::filesystem::is_symlink(st_abs)) {
        audit_event("share_download", "fail", &s, "not_found", 404);
        res.status = 404;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Not found\n", "text/plain; charset=utf-8");
        return;
    }

    if (s.type == "file" && !std::filesystem::is_regular_file(st_abs)) {
        audit_event("share_download", "fail", &s, "type_mismatch", 404);
        res.status = 404;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Not found\n", "text/plain; charset=utf-8");
        return;
    }

    if (s.type == "dir" && !std::filesystem::is_directory(st_abs)) {
        audit_event("share_download", "fail", &s, "type_mismatch", 404);
        res.status = 404;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Not found\n", "text/plain; charset=utf-8");
        return;
    }

    if (is_pq_share) {
        std::error_code ec;
        auto st = std::filesystem::symlink_status(abs, ec);
        if (ec || !std::filesystem::exists(st) || std::filesystem::is_symlink(st)) {
            audit_event("share_download", "fail", &s, "not_found", 404);
            res.status = 404;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Not found\n", "text/plain; charset=utf-8");
            return;
        }

        if (s.type == "file" && !std::filesystem::is_regular_file(st)) {
            audit_event("share_download", "fail", &s, "type_mismatch", 404);
            res.status = 404;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Not found\n", "text/plain; charset=utf-8");
            return;
        }

        if (s.type == "dir" && !std::filesystem::is_directory(st)) {
            audit_event("share_download", "fail", &s, "type_mismatch", 404);
            res.status = 404;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Not found\n", "text/plain; charset=utf-8");
            return;
        }

        if (s.type != "file") {
            audit_event("share_pq_access", "fail", &s, "pq_share_requires_file", 400);
            res.status = 400;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Bad request\n", "text/plain; charset=utf-8");
            return;
        }

        if (!pq_session_ok) {
    pqnas::PqShareInviteV1 inv;
    std::string inv_err;

    if (share_pq.load_latest_invite_for_share(token, &inv, &inv_err) &&
        inv.state == "pending" &&
        !pqnas::SharePqStoreV1::iso_expired_local(inv.expires_at)) {
        audit_event("share_pq_access", "ok", &s, "redirect_pending_invite", 302, inv.invite_id);
        res.status = 302;
        res.set_header("Cache-Control", "no-store");
        res.set_header("Location", share_pq.invite_url_path(inv.invite_id).c_str());
        return;
    }

    audit_event("share_pq_access", "fail", &s, "recipient_enrollment_required", 403);

    std::ostringstream html;
    html
        << "<!doctype html>"
        << "<html lang='en'>"
        << "<head>"
        << "<meta charset='utf-8'>"
        << "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        << "<title>DNA-Nexus secure share</title>"
        << "</head>"
        << "<body style='font-family:system-ui,sans-serif;padding:24px;max-width:760px;margin:auto'>"
        << "<h2>Recipient enrollment required</h2>"
        << "<p>This secure share must be opened from an enrolled browser or device.</p>"
        << "<p>If you received an invite link, open that invite first.</p>"
        << "</body>"
        << "</html>";

    res.status = 403;
    res.set_header("Cache-Control", "no-store");
    res.set_content(html.str(), "text/html; charset=utf-8");
    return;
}

        // best effort: touch last_used_at
        {
            std::string touch_err;
            (void)share_pq.touch_session(
                pq_sess.session_id,
                pqnas::SharePqStoreV1::now_iso_utc_local(),
                &touch_err
            );
        }

        const json boot = {
            {"token", token}, // keep for temporary compatibility
            {"share_token", token},
            {"open_api", "/api/v4/shares/pq/open"},
            {"open_init_api", "/api/v4/shares/pq/open/init"},
            {"open_chunk_api", "/api/v4/shares/pq/open/chunk"},
            {"file_name", std::filesystem::path(s.path).filename().string()},
            {"recipient_device_id", pq_sess.recipient_device_id}
        };

        std::ostringstream html;
        html
            << "<!doctype html>"
            << "<html lang='en'>"
            << "<head>"
            << "<meta charset='utf-8'>"
            << "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            << "<title>DNA-Nexus secure share</title>"
            << "<script>window.PQ_SHARE_OPEN_BOOT=" << boot.dump() << ";</script>"
            << "<script defer src='/static/share_pq_mlkem.js?v=7'></script>"
            << "<script defer src='/static/share_pq_keys.js?v=7'></script>"
            << "<script src='/static/i18n.js?v=20260519-pq-share-i18n-5'></script>"
            << "<script defer src='/static/share_pq_unlock_modal.js?v=20260519-pq-share-i18n-5'></script>"
            << "<script defer src='/static/share_pq_open.js?v=20260519-pq-share-i18n-5'></script>"
            << "</head>"
            << "<body>"
            << "<div id='pqShareOpenApp'>Opening secure share…</div>"
            << "</body>"
            << "</html>";

        audit_event("share_pq_access", "ok", &s, "serve_pq_bootstrap", 200);

        res.status = 200;
        res.set_header("Cache-Control", "no-store");
        res.set_content(html.str(), "text/html; charset=utf-8");
        return;
    }

    // Serve
    if (s.type == "file") {
        std::string fname = std::filesystem::path(s.path).filename().string();
        if (fname.empty()) fname = "download";

        const std::string mime = public_share_guess_mime_local(abs);

        const bool force_download = req.has_param("download");
        const bool raw_stream =
            req.has_param("raw") ||
            !req.get_header_value("Range").empty();

        if (!force_download && req.has_param("poster") && public_share_is_video_mime_local(mime)) {
            audit_event("share_download", "ok", &s, "video_poster", 200);
            public_share_send_video_poster_local(res, abs, token);
            return;
        }

        // Keep old behavior for normal non-video share links:
        // /s/<token> still downloads normal files.
        //
        // But video links open a browser player page:
        // /s/<token>        -> HTML video page
        // /s/<token>?raw=1  -> actual video stream with Range support
        // /s/<token>?poster=1 -> generated poster image for link previews
        if (!force_download && !raw_stream && public_share_is_video_mime_local(mime)) {
            std::error_code ec_file;
            const auto sz = std::filesystem::file_size(abs, ec_file);
            if (ec_file || sz == 0) {
                audit_event(
                    "share_download",
                    "fail",
                    &s,
                    ec_file ? "file_size_failed" : "empty_file",
                    404,
                    ec_file ? ec_file.message() : "empty file"
                );

                res.status = 404;
                res.set_header("Cache-Control", "no-store");
                res.set_content("Not found\n", "text/plain; charset=utf-8");
                return;
            }

            audit_event("share_download", "ok", &s, "video_preview_page", 200);
            // Do not count the HTML preview page as a download.
            // Circle Stack and link-preview crawlers fetch /s/<token> to read OG metadata.
            // Count explicit downloads and raw streams below instead.

            res.status = 200;
            res.set_header("Cache-Control", "no-store");
            res.set_content(
                public_share_video_page_local(token, fname, mime, req),
                "text/html; charset=utf-8"
            );
            return;
        }

        const bool attachment_mode =
            force_download ||
            !raw_stream;

        audit_event(
            "share_download",
            "ok",
            &s,
            attachment_mode ? "" : "inline_stream",
            raw_stream ? 206 : 200
        );

        // Count old-style downloads and explicit downloads.
        // Do not count every video Range request, otherwise one video view can become many downloads.
        if (attachment_mode || force_download) {
            (void)shares.increment_downloads(token, &err);
        }

        res.set_header("Cache-Control", "no-store");
        public_share_send_file_stream_local(req, res, abs, fname, attachment_mode);
        return;
    }

    // s.type == "dir" -> zip directory using the already-resolved physical path.
    // This avoids legacy assumptions about user_dir + s.path and works with pools,
    // migration, landing tier, and other metadata-aware storage locations.

    {
        std::error_code ec_dir;
        auto st_dir = std::filesystem::symlink_status(abs, ec_dir);
        if (ec_dir || !std::filesystem::exists(st_dir)) {
            audit_event("share_download", "fail", &s, "not_found", 404);
            res.status = 404;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Not found\n", "text/plain; charset=utf-8");
            return;
        }
        if (std::filesystem::is_symlink(st_dir)) {
            audit_event("share_download", "fail", &s, "symlink_not_supported", 400, "shared root is symlink");
            res.status = 400;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Bad request\n", "text/plain; charset=utf-8");
            return;
        }
        if (!std::filesystem::is_directory(st_dir)) {
            audit_event("share_download", "fail", &s, "type_mismatch", 404);
            res.status = 404;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Not found\n", "text/plain; charset=utf-8");
            return;
        }
    }

    [[maybe_unused]] std::uint64_t input_bytes = 0;
    [[maybe_unused]] std::uint64_t files = 0, dirs = 0;

    dirs = 1;
    {
        std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;
        std::error_code ec;
        ec.clear();

        for (auto it = std::filesystem::recursive_directory_iterator(abs, opts, ec);
             it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {

            if (ec) {
                audit_event("share_download", "fail", &s, "walk_failed", 500, ec.message());
                res.status = 500;
                res.set_header("Cache-Control", "no-store");
                res.set_content("Server error\n", "text/plain; charset=utf-8");
                return;
            }

            std::error_code ec2;
            auto st2 = it->symlink_status(ec2);
            if (ec2) continue;

            if (std::filesystem::is_symlink(st2)) {
                audit_event("share_download", "fail", &s, "symlink_not_supported", 400, "symlink inside tree");
                res.status = 400;
                res.set_header("Cache-Control", "no-store");
                res.set_content("Bad request\n", "text/plain; charset=utf-8");
                return;
            }

            if (std::filesystem::is_directory(st2)) {
                dirs += 1;
                continue;
            }

            if (std::filesystem::is_regular_file(st2)) {
                files += 1;
                input_bytes += pqnas::file_size_u64_safe(it->path());
                continue;
            }

            files += 1;
        }
    }

    const std::filesystem::path zip_parent = abs.parent_path();
    const std::string zip_arg = abs.filename().string();

    if (zip_parent.empty() || zip_arg.empty()) {
        audit_event("share_download", "fail", &s, "zip_failed", 500, "empty zip parent/name");
        res.status = 500;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Server error\n", "text/plain; charset=utf-8");
        return;
    }

    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        audit_event("share_download", "fail", &s, "pipe_failed", 500, "pipe()");
        res.status = 500;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Server error\n", "text/plain; charset=utf-8");
        return;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        audit_event("share_download", "fail", &s, "fork_failed", 500, "fork()");
        res.status = 500;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Server error\n", "text/plain; charset=utf-8");
        return;
    }

    if (pid == 0) {
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[0]);
        ::close(pipefd[1]);

        if (::chdir(zip_parent.c_str()) != 0) _exit(127);

        const char* argv[] = {
            "zip",
            "-r",
            "-q",
            "-",
            zip_arg.c_str(),
            nullptr
        };
        ::execvp("zip", (char* const*)argv);
        _exit(127);
    }

    ::close(pipefd[1]);

    std::string zip_data;
    zip_data.reserve(4ull * 1024 * 1024);

    std::array<char, 64 * 1024> buf{};
    while (true) {
        ssize_t n = ::read(pipefd[0], buf.data(), (ssize_t)buf.size());
        if (n == 0) break;
        if (n < 0) {
            ::close(pipefd[0]);
            ::kill(pid, SIGKILL);
            audit_event("share_download", "fail", &s, "read_failed", 500, "read(zip)");
            res.status = 500;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Server error\n", "text/plain; charset=utf-8");
            return;
        }
        zip_data.append(buf.data(), (size_t)n);
    }
    ::close(pipefd[0]);

    int child_status = 0;
    ::waitpid(pid, &child_status, 0);
    if (!(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0)) {
        audit_event("share_download", "fail", &s, "zip_failed", 500, "zip exit nonzero");
        res.status = 500;
        res.set_header("Cache-Control", "no-store");
        res.set_content("Server error\n", "text/plain; charset=utf-8");
        return;
    }

    audit_event("share_download", "ok", &s, "", 200);
    (void)shares.increment_downloads(token, &err); // best effort

    std::string base = std::filesystem::path(s.path).filename().string();
    if (base.empty()) base = zip_arg;
    if (base.empty()) base = "download";
    std::string fname = base + ".zip";

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Content-Type", "application/zip");
    res.set_header("Content-Disposition", build_content_disposition("attachment", fname));
    res.body = std::move(zip_data);
});

// POST /api/v4/gallery/meta/embedded_get
// Body:
// {
//   "path": "photos/cat.jpg"
// }
srv.Post("/api/v4/gallery/meta/embedded_get", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http,
                          const std::string& detail = "",
                          const std::string& rel_path = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_meta_embedded_get_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!rel_path.empty()) ev.f["path"] = pqnas::shorten(rel_path, 200);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& rel_path,
                        bool has_exif,
                        bool has_iptc,
                        bool has_xmp) {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_meta_embedded_get_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_path, 200);
        ev.f["has_exif"] = has_exif ? "1" : "0";
        ev.f["has_iptc"] = has_iptc ? "1" : "0";
        ev.f["has_xmp"] = has_xmp ? "1" : "0";
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto lower_ascii_local = [](std::string s) -> std::string {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };

    auto file_ext_lower = [&](const std::string& name) -> std::string {
        std::string n = name;
        const auto slash = n.find_last_of("/\\");
        if (slash != std::string::npos) n = n.substr(slash + 1);
        auto lower = lower_ascii_local(n);
        const auto dot = lower.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= lower.size()) return "";
        return lower.substr(dot + 1);
    };

    auto is_gallery_image_name = [&](const std::string& name) -> bool {
        const std::string ext = file_ext_lower(name);
        return ext == "png"  ||
               ext == "jpg"  ||
               ext == "jpeg" ||
               ext == "gif"  ||
               ext == "webp" ||
               ext == "svg"  ||
               ext == "bmp"  ||
               ext == "ico"  ||
               ext == "tif"  ||
               ext == "tiff" ||
               ext == "heic" ||
               ext == "heif" ||
               ext == "cr2"  ||
               ext == "cr3"  ||
               ext == "nef"  ||
               ext == "arw"  ||
               ext == "raf"  ||
               ext == "dng"  ||
               ext == "rw2"  ||
               ext == "orf";
    };

    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value()) {
            audit_fail("user_missing", 403);
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "forbidden"},
                {"message", "policy denied"}
            }.dump());
            return;
        }

        const auto& u = *uopt;
        if (u.storage_state != "allocated") {
            audit_fail("storage_unallocated", 403);
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"},
                {"fingerprint_hex", fp_hex},
                {"quota_bytes", u.quota_bytes}
            }.dump());
            return;
        }
    }

    json in = json::parse(req.body, nullptr, false);
    if (in.is_discarded() || !in.is_object()) {
        audit_fail("bad_json", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json body"}
        }.dump());
        return;
    }

    const std::string path_in = in.value("path", "");
    if (path_in.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    std::string rel_norm;
    std::string nerr;
    if (!pqnas::normalize_user_rel_path_strict(path_in, &rel_norm, &nerr)) {
        audit_fail("invalid_path", 400, nerr, path_in);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    pqnas::ResolvedLogicalItem item;
    std::string rerr;
    if (!pqnas::resolve_existing_user_item(users, fp_hex, rel_norm, &item, &rerr) || !item.exists) {
        audit_fail("not_found", 404, rerr, rel_norm);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    if (!item.is_file) {
        audit_fail("not_file", 400, "", rel_norm);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "embedded metadata is only available for files"}
        }.dump());
        return;
    }

    if (!is_gallery_image_name(std::filesystem::path(rel_norm).filename().string())) {
        audit_fail("not_supported_image", 400, "", rel_norm);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "file is not a supported gallery image"}
        }.dump());
        return;
    }

    std::filesystem::path abs_path;
    bool have_abs = false;

    // Prefer file_locations physical path if present.
    if (auto* idx = pqnas::get_file_location_index()) {
        std::string lerr;
        auto rec = idx->get(fp_hex, rel_norm, &lerr);
        if (!lerr.empty()) {
            audit_fail("file_location_lookup_failed", 500, lerr, rel_norm);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "file metadata lookup failed"},
                {"detail", pqnas::shorten(lerr, 180)}
            }.dump());
            return;
        }

        if (rec.has_value() && !rec->physical_path.empty()) {
            abs_path = std::filesystem::path(rec->physical_path);
            have_abs = true;
        }
    }

    if (!have_abs && item.has_physical_anchor) {
        abs_path = item.abs_path;
        have_abs = true;
    }

    if (!have_abs) {
        audit_fail("file_missing_physical_anchor", 500, "", rel_norm);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "file missing physical anchor"}
        }.dump());
        return;
    }

    {
        std::error_code ec;
        auto st = std::filesystem::symlink_status(abs_path, ec);
        if (ec || !std::filesystem::exists(st)) {
            audit_fail("not_found", 404, "", rel_norm);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "file not found"}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(st)) {
            audit_fail("symlink_not_supported", 400, "", rel_norm);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_regular_file(st)) {
            audit_fail("not_regular_file", 400, "", rel_norm);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "not a regular file"}
            }.dump());
            return;
        }
    }

    pqnas::json summary = pqnas::json::object();
    pqnas::json embedded = pqnas::json{
        {"exif", pqnas::json::object()},
        {"iptc", pqnas::json::object()},
        {"xmp", pqnas::json::object()}
    };

    std::string merr;
    if (!pqnas::read_embedded_image_metadata_exiftool(abs_path, &summary, &embedded, &merr)) {
        const int http = (merr == "tool_unavailable") ? 503 : 500;
        const std::string error_code = (merr == "tool_unavailable") ? "service_unavailable" : "server_error";
        const std::string message =
            (merr == "tool_unavailable")
                ? "embedded metadata reader not available"
                : "failed to read embedded image metadata";

        audit_fail("embedded_meta_read_failed", http, merr, rel_norm);
        reply_json(res, http, json{
            {"ok", false},
            {"error", error_code},
            {"message", message},
            {"detail", merr}
        }.dump());
        return;
    }

    const bool has_exif = embedded.contains("exif") && embedded["exif"].is_object() && !embedded["exif"].empty();
    const bool has_iptc = embedded.contains("iptc") && embedded["iptc"].is_object() && !embedded["iptc"].empty();
    const bool has_xmp  = embedded.contains("xmp")  && embedded["xmp"].is_object()  && !embedded["xmp"].empty();

    audit_ok(rel_norm, has_exif, has_iptc, has_xmp);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", rel_norm},
        {"summary", summary},
        {"embedded", embedded}
    }.dump());
});

srv.Get(R"(/pq/invite/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string invite_id = req.matches[1].str();

        pqnas::PqShareInviteV1 inv;
        std::string err;
        if (!share_pq.load_invite(invite_id, &inv, &err) || inv.state != "pending") {
            res.status = 404;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Invite not found\n", "text/plain; charset=utf-8");
            return;
        }

        if (pqnas::SharePqStoreV1::iso_expired_local(inv.expires_at)) {
            res.status = 410;
            res.set_header("Cache-Control", "no-store");
            res.set_content("Invite expired\n", "text/plain; charset=utf-8");
            return;
        }

        const json boot = {
            {"invite_id", invite_id},
            {"expires_at", inv.expires_at},
            {"label_hint", inv.label_hint},
            {"preferred_kem_alg", "ML-KEM-768"}
        };

        std::ostringstream html;
        html
            << "<!doctype html>"
            << "<html lang='en'>"
            << "<head>"
            << "<meta charset='utf-8'>"
            << "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            << "<title>DNA-Nexus secure share</title>"
            << "<script>window.PQ_SHARE_INVITE_BOOT=" << boot.dump() << ";</script>"
            << "<script defer src='/static/share_pq_mlkem.js?v=7'></script>"
            << "<script defer src='/static/share_pq_keys.js?v=7'></script>"
            << "<script src='/static/i18n.js?v=20260519-pq-share-i18n-5'></script>"
            << "<script defer src='/static/share_pq_unlock_modal.js?v=20260519-pq-share-i18n-5'></script>"
            << "<script defer src='/static/share_pq_invite.js?v=20260519-pq-share-i18n-5'></script>"
            << "</head>"
            << "<body>"
            << "<div id='pqShareInviteApp'>Loading…</div>"
            << "</body>"
            << "</html>";

        res.status = 200;
        res.set_header("Cache-Control", "no-store");
        res.set_content(html.str(), "text/html; charset=utf-8");
});
