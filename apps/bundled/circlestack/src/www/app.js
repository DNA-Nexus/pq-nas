const CS_API = "/api/v4/circlestack";

async function csLoadFeed() {
  const feed = document.getElementById("csFeed");
  if (!feed) return;

  feed.textContent = "";

  const res = await fetch(`${CS_API}/feed`, { credentials: "same-origin" });
  const data = await res.json();
  const posts = Array.isArray(data.posts) ? data.posts : [];

  if (!posts.length) {
    const empty = document.createElement("div");
    empty.className = "cs-empty";
    empty.textContent = csT("feed.empty");
    feed.appendChild(empty);
    return;
  }

  for (const post of posts) {
    feed.appendChild(csRenderPost(post));
  }
}

function csRenderPost(post) {
  const el = document.createElement("article");
  el.className = "cs-post";

  const header = document.createElement("div");
  header.className = "cs-post-header";

  const author = document.createElement("div");
  author.className = "cs-post-author";
  author.textContent = post.owner_display_name || post.owner_fp_short || "anon";
  header.appendChild(author);

  const del = document.createElement("button");
  del.className = "cs-post-delete";
  del.type = "button";
  del.textContent = "✕";
  del.title = "Delete post";
  del.setAttribute("aria-label", "Delete post");
  del.addEventListener("click", () => csDeletePost(post.id));
  header.appendChild(del);

  el.appendChild(header);

  const text = document.createElement("div");
  text.className = "cs-post-text";
  text.textContent = post.text || "";
  el.appendChild(text);

  if (post.media_url) {
    const img = document.createElement("img");
    img.className = "cs-post-media";
    img.src = post.media_url;
    img.loading = "lazy";
    img.decoding = "async";
    img.alt = "";
    el.appendChild(img);
  }

  const meta = document.createElement("div");
  meta.className = "cs-post-meta";

  const vis = post.visibility || "public";
  let visLabel = "🌍";
  if (vis === "private") visLabel = "🔒";
  if (vis === "circle") visLabel = "👥";

  meta.textContent = `${visLabel} ${
    post.created_epoch
      ? new Date(post.created_epoch * 1000).toLocaleString()
      : ""
  }`;
  el.appendChild(meta);

  return el;
}

async function csCreatePost() {
  const textEl = document.getElementById("csText");
  const mediaEl = document.getElementById("csMediaPath");

  const text = textEl ? textEl.value.trim() : "";
  const media_path = mediaEl ? mediaEl.value.trim() : "";

  if (!text && !media_path) return;

  await fetch(`${CS_API}/posts/create`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ text, media_path })
  });

  if (textEl) textEl.value = "";
  if (mediaEl) mediaEl.value = "";

  await csLoadFeed();
}

async function csDeletePost(id) {
  if (!id) return;

  const ok = await csConfirmDelete();
  if (!ok) return;

  await fetch(`${CS_API}/posts?id=${encodeURIComponent(id)}`, {
    method: "DELETE",
    credentials: "same-origin"
  });

  await csLoadFeed();
}

function csConfirmDelete() {
  return new Promise((resolve) => {
    const backdrop = document.createElement("div");
    backdrop.className = "cs-modal-backdrop";

    const modal = document.createElement("div");
    modal.className = "cs-modal cs-intro-modal";

    const title = document.createElement("div");
    title.className = "cs-modal-title";
    title.textContent = "Delete post?";

    const text = document.createElement("div");
    text.className = "cs-modal-text";
    text.textContent = "This cannot be undone.";

    const actions = document.createElement("div");
    actions.className = "cs-modal-actions";

    const cancel = document.createElement("button");
    cancel.className = "cs-modal-cancel";
    cancel.type = "button";
    cancel.textContent = "Cancel";

    const del = document.createElement("button");
    del.className = "cs-modal-delete";
    del.type = "button";
    del.textContent = "Delete";

    const close = (value) => {
      backdrop.remove();
      resolve(value);
    };

    cancel.addEventListener("click", () => close(false));
    del.addEventListener("click", () => close(true));
    backdrop.addEventListener("click", (ev) => {
      if (ev.target === backdrop) close(false);
    });

    actions.appendChild(cancel);
    actions.appendChild(del);
    modal.appendChild(title);
    modal.appendChild(text);
    modal.appendChild(actions);
    backdrop.appendChild(modal);
    document.body.appendChild(backdrop);

    cancel.focus();
  });
}

document.addEventListener("DOMContentLoaded", async () => {
  await csApplyI18n();

  const btn = document.getElementById("csPostButton");
  if (btn) btn.addEventListener("click", csCreatePost);

  await csLoadFeed();
});


async function csLoadUsers() {
  const el = document.getElementById("csCircleUsers");
  if (!el) return;

  el.textContent = "";

  const res = await fetch("/api/v4/circlestack/users", {
    credentials: "same-origin"
  });
  const data = await res.json();
  const users = Array.isArray(data.users) ? data.users : [];

  for (const u of users) {
    if (u.is_me) continue;

    const row = document.createElement("label");
    row.className = "cs-user-row";

    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.className = "cs-user-checkbox";
    cb.value = u.fingerprint;

    const name = document.createElement("span");
    name.textContent = u.name || (u.fingerprint ? u.fingerprint.slice(0, 16) : u.fp_short);

    row.appendChild(cb);
    row.appendChild(name);
    el.appendChild(row);
  }
}

document.addEventListener("click", (ev) => {
  const btn = ev.target.closest(".cs-vis-option");
  if (!btn) return;

  const wrap = document.getElementById("csVisibility");
  const circleEl = document.getElementById("csCircleUsers");
  if (!wrap) return;

  wrap.dataset.value = btn.dataset.value;

  wrap.querySelectorAll(".cs-vis-option").forEach(b => {
    b.classList.toggle("is-active", b === btn);
  });

  if (circleEl) {
    circleEl.hidden = btn.dataset.value !== "circle";
  }
});

async function csOpenMediaPicker() {
  let cur = "";

  return new Promise(async (resolve) => {
    const backdrop = document.createElement("div");
    backdrop.className = "cs-modal-backdrop";

    const card = document.createElement("div");
    card.className = "cs-media-modal";

    card.innerHTML = `
      <div class="cs-media-head">
        <div>
          <div class="cs-modal-title">Choose media</div>
          <div class="cs-media-path">/</div>
        </div>
        <button class="cs-media-close" type="button">×</button>
      </div>
      <div class="cs-media-body"></div>
      <div class="cs-modal-actions">
        <button class="cs-modal-cancel" type="button">Cancel</button>
        <button class="cs-media-choose" type="button">Choose</button>
      </div>
    `;

    const body = card.querySelector(".cs-media-body");
    const pathEl = card.querySelector(".cs-media-path");
    const chooseBtn = card.querySelector(".cs-media-choose");
    let selectedPath = null;

    const close = (val) => {
      backdrop.remove();
      resolve(val);
    };

    async function load(path) {
      cur = path || "";
      pathEl.textContent = "/" + cur;

      const url = cur
        ? `/api/v4/files/list?path=${encodeURIComponent(cur)}`
        : "/api/v4/files/list";

      body.textContent = "Loading…";

      const res = await fetch(url, { credentials: "same-origin" });
      const data = await res.json();
      const items = Array.isArray(data.items) ? data.items : [];

      body.textContent = "";

      if (cur) {
        const up = document.createElement("button");
        up.className = "cs-media-item";
        up.type = "button";
        up.textContent = "← ..";
        up.addEventListener("click", () => {
          const parts = cur.split("/").filter(Boolean);
          parts.pop();
          load(parts.join("/"));
        });
        body.appendChild(up);
      }

      for (const it of items) {
        if ((it.name || "").startsWith(".pqnas")) continue;
        const isMedia = it.type === "dir" || /\.(jpg|jpeg|png|webp|gif|mp4|webm|mov)$/i.test(it.name || "");
        if (!isMedia) continue;
        const full = cur ? `${cur}/${it.name}` : it.name;
        const isDir = it.type === "dir";

        const row = document.createElement("button");
        row.className = "cs-media-item";
        row.type = "button";
        row.textContent = "";

        if (!isDir && csIsImagePath(full)) {
          const thumb = document.createElement("img");
          thumb.className = "cs-media-thumb is-loading";
          thumb.src = csFileUrl(full);
          thumb.alt = "";
          thumb.addEventListener("load", () => thumb.classList.remove("is-loading"));
          row.appendChild(thumb);
        } else {
          const icon = document.createElement("span");
          icon.className = "cs-media-icon";
          icon.textContent = isDir ? "📁" : "📄";
          row.appendChild(icon);
        }

        const label = document.createElement("span");
        label.textContent = it.name;
        row.appendChild(label);

        row.addEventListener("click", () => {
          if (isDir) {
            selectedPath = null;
            load(full);
            return;
          }

          selectedPath = full;
          body.querySelectorAll(".cs-media-item").forEach(el => {
            el.classList.remove("is-selected");
          });
          row.classList.add("is-selected");
        });

        row.addEventListener("dblclick", () => {
          if (isDir) return;
          close(full);
        });

        body.appendChild(row);
      }
    }

    card.querySelector(".cs-media-close").addEventListener("click", () => close(null));
    card.querySelector(".cs-modal-cancel").addEventListener("click", () => close(null));
    chooseBtn.addEventListener("click", () => {
      if (selectedPath) close(selectedPath);
    });
    backdrop.addEventListener("click", (ev) => {
      if (ev.target === backdrop) close(null);
    });

    backdrop.appendChild(card);
    document.body.appendChild(backdrop);

    await load("");
  });
}

document.addEventListener("click", async (ev) => {
  if (!ev.target.closest("#csPickMedia")) return;

  const picked = await csOpenMediaPicker();
  if (!picked) return;

  const mediaEl = document.getElementById("csMediaPath");
  if (mediaEl) mediaEl.value = picked;
  csSetMediaPreview(picked);
});

function csIsImagePath(path) {
  return /\.(jpg|jpeg|png|webp|gif)$/i.test(path || "");
}

function csFileUrl(path) {
  return `/api/v4/files/get?path=${encodeURIComponent(path || "")}`;
}

function csSetMediaPreview(path) {
  const box = document.getElementById("csMediaPreview");
  if (!box) return;

  box.textContent = "";
  if (!path || !csIsImagePath(path)) {
    box.hidden = true;
    return;
  }

  const img = document.createElement("img");
  img.className = "cs-compose-preview-img is-loading";
  img.src = csFileUrl(path);
  img.alt = "";

  img.addEventListener("load", () => {
    img.classList.remove("is-loading");
  });

  const clear = document.createElement("button");
  clear.className = "cs-media-clear";
  clear.type = "button";
  clear.textContent = "Remove image";

  box.appendChild(img);
  box.appendChild(clear);
  box.hidden = false;
}

document.addEventListener("input", (ev) => {
  if (ev.target && ev.target.id === "csMediaPath") {
    csSetMediaPreview(ev.target.value.trim());
  }
});

function csOpenImageLightbox(src) {
  const backdrop = document.createElement("div");
  backdrop.className = "cs-lightbox";
  backdrop.innerHTML = `
    <button class="cs-lightbox-close" type="button">×</button>
    <img src="${src}" alt="">
  `;

  backdrop.addEventListener("click", () => {
    backdrop.remove();
  });

  document.body.appendChild(backdrop);
}

document.addEventListener("click", (ev) => {
  const img = ev.target.closest(".cs-compose-preview-img, .cs-post-media");
  if (!img) return;
  csOpenImageLightbox(img.src);
});

document.addEventListener("keydown", (ev) => {
  if (ev.key !== "Escape") return;
  document.querySelector(".cs-lightbox")?.remove();
});

document.addEventListener("click", (ev) => {
  if (!ev.target.closest(".cs-media-clear")) return;

  const mediaEl = document.getElementById("csMediaPath");
  if (mediaEl) mediaEl.value = "";

  csSetMediaPreview("");
});

async function csOpenIntroduceModal() {
  const usersRes = await fetch("/api/v4/circlestack/users", {
    credentials: "same-origin"
  });
  const usersData = await usersRes.json();
  const users = (usersData.users || []).filter(u => !u.is_me);

  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal cs-intro-modal";

  modal.innerHTML = `
    <div class="cs-modal-title">Introduce people</div>
    <div class="cs-modal-text">Pick two people to introduce.</div>

    <div class="cs-intro-grid">
      <select id="csIntroA"></select>
      <select id="csIntroB"></select>
    </div>

    <textarea id="csIntroMsg" placeholder="Optional message"></textarea>

    <div class="cs-modal-actions">
      <button class="cs-modal-cancel">Cancel</button>
      <button class="cs-modal-delete">Send</button>
    </div>
  `;

  const selA = modal.querySelector("#csIntroA");
  const selB = modal.querySelector("#csIntroB");

  for (const u of users) {
    const optA = document.createElement("option");
    optA.value = u.fingerprint;
    optA.textContent = (u.name && u.name !== u.fp_short) ? u.name : csElideFp(u.fingerprint);
    selA.appendChild(optA);

    const optB = optA.cloneNode(true);
    selB.appendChild(optB);
  }

  const close = () => backdrop.remove();

  modal.querySelector(".cs-modal-cancel").onclick = close;

  modal.querySelector(".cs-modal-delete").onclick = async () => {
    const a = selA.value;
    const b = selB.value;
    const msg = modal.querySelector("#csIntroMsg").value;

    if (!a || !b || a === b) return;

    await fetch("/api/v4/circlestack/introductions/create", {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        person_a_fp: a,
        person_b_fp: b,
        message: msg
      })
    });

    close();
    csLoadIntroductions();
  };

  backdrop.appendChild(modal);
  document.body.appendChild(backdrop);
}

document.getElementById("csIntroduceBtn")
  ?.addEventListener("click", csOpenIntroduceModal);


async function csLoadIntroductions() {
  const res = await fetch("/api/v4/circlestack/introductions", {
    credentials: "same-origin"
  });
  const data = await res.json();
  const items = data.items || [];

  const usersRes = await fetch("/api/v4/circlestack/users", {
    credentials: "same-origin"
  });
  const usersData = await usersRes.json();
  const users = usersData.users || [];
  const me = users.find(u => u.is_me);
  const meFp = me ? me.fingerprint : "";

  const usersByFp = new Map(
    users.map(u => [u.fingerprint, u])
  );

  let box = document.getElementById("csIntroductions");
  if (!box) {
    box = document.createElement("section");
    box.id = "csIntroductions";
    box.className = "cs-feed";
    document.querySelector(".cs-shell").appendChild(box);
  }

  box.innerHTML = "";

  for (const it of items) {
    const el = document.createElement("div");
    el.className = "cs-post";

    const introducer = csUserLabel(it.introducer_fp, usersByFp);
    const a = csUserLabel(it.person_a_fp, usersByFp);
    const b = csUserLabel(it.person_b_fp, usersByFp);

    el.innerHTML = `
      <div class="cs-intro-line">
        <span class="cs-intro-from">${introducer}</span>
        <span class="cs-intro-verb">introduced</span>
        <span class="cs-intro-to">${a} ↔ ${b}</span>
      </div>
      ${it.message ? `<div class="cs-intro-msg">"${it.message}"</div>` : ""}
    `;

    const canRespond =
      it.status === "pending" &&
      meFp &&
      (meFp === it.person_a_fp || meFp === it.person_b_fp) &&
      meFp !== it.introducer_fp;

    if (canRespond) {
      const actions = document.createElement("div");
      actions.className = "cs-intro-actions";

      const ok = document.createElement("button");
      ok.textContent = "Accept";

      const no = document.createElement("button");
      no.textContent = "Dismiss";

      ok.onclick = () => csRespondIntro(it.id, "accept");
      no.onclick = () => csRespondIntro(it.id, "dismiss");

      actions.appendChild(ok);
      actions.appendChild(no);
      el.appendChild(actions);
    }

    box.appendChild(el);
  }
}

async function csRespondIntro(id, action) {
  await fetch("/api/v4/circlestack/introductions/respond", {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ id, action })
  });

  csLoadIntroductions();
}

document.addEventListener("DOMContentLoaded", csLoadIntroductions);


function csUserLabel(fp, usersByFp) {
  const u = usersByFp.get(fp);
  if (u && u.name) return u.name;
  if (u && u.fp_short) return u.fp_short;
  return fp ? fp.slice(0, 16) : "unknown";
}

function csElideFp(fp) {
  if (!fp) return "unknown";
  if (fp.length <= 16) return fp;
  return fp.slice(0, 8) + "…" + fp.slice(-6);
}

async function csOpenMyCircle() {
  const usersRes = await fetch("/api/v4/circlestack/users", { credentials: "same-origin" });
  const usersData = await usersRes.json();
  const usersByFp = new Map((usersData.users || []).map(u => [u.fingerprint, u]));

  const res = await fetch("/api/v4/circlestack/circle", { credentials: "same-origin" });
  const data = await res.json();

  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal cs-intro-modal";

  modal.innerHTML = `
    <div class="cs-modal-title">My Circle</div>
    <div class="cs-modal-body" id="csMyCircleBody"></div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel">Close</button>
    </div>
  `;

  const body = modal.querySelector("#csMyCircleBody");
  const items = data.items || [];

  if (!items.length) {
    body.innerHTML = `<div class="cs-empty">Your circle is empty.</div>`;
  } else {
    for (const it of items) {
      const row = document.createElement("div");
      row.className = "cs-circle-row";
      
const name = csUserLabel(it.fp, usersByFp);

row.innerHTML = `
  <span>${name}</span>
  <button class="cs-circle-remove">Forget</button>
`;

row.querySelector("button").onclick = () => {
  csConfirmRemove(it.fp, name);
};

      body.appendChild(row);
    }
  }

  modal.querySelector(".cs-modal-cancel").onclick = () => backdrop.remove();

  backdrop.appendChild(modal);
  document.body.appendChild(backdrop);
}

document.getElementById("csMyCircleBtn")
  ?.addEventListener("click", csOpenMyCircle);


function csConfirmRemove(fp, name) {
  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal";

  modal.innerHTML = `
    <div class="cs-modal-title">Remove from Circle?</div>
    <div class="cs-modal-text">
      This will remove <b>${name}</b> from your circle.
    </div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel">Cancel</button>
      <button class="cs-modal-delete">Remove</button>
    </div>
  `;

  modal.querySelector(".cs-modal-cancel").onclick = () => backdrop.remove();

  modal.querySelector(".cs-modal-delete").onclick = async () => {
    await fetch("/api/v4/circlestack/circle/remove", {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ fp })
    });

    backdrop.remove();
    csOpenMyCircle(); // refresh
  };

  backdrop.appendChild(modal);
  document.body.appendChild(backdrop);
}
