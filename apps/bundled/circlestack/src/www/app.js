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
  meta.textContent = post.created_epoch
    ? new Date(post.created_epoch * 1000).toLocaleString()
    : "";
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

document.addEventListener("DOMContentLoaded", async () => {
  await csApplyI18n();

  const btn = document.getElementById("csPostButton");
  if (btn) btn.addEventListener("click", csCreatePost);

  await csLoadFeed();
});
