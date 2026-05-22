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
    modal.className = "cs-modal";

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
    name.textContent = u.name || u.fp_short;

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
