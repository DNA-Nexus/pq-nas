const CS_API = "/api/v4/circlestack";
const CS_REACTIONS = ["👍", "❤️", "😂", "😮", "👏", "🔥"];
let csSelectedMentions = [];

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


async function csOpenPersonCard(fp, fallback = {}) {
  const safeFp = String(fp || "").trim();
  if (!safeFp && !fallback.display_name) return;

  let user = null;

  try {
    if (safeFp) {
      const res = await fetch("/api/v4/circlestack/users", {
        credentials: "same-origin"
      });
      if (res.ok) {
        const data = await res.json();
        user = (data.users || []).find(u => u.fingerprint === safeFp) || null;
      }
    }
  } catch (_) {
    // Person card can still render from post fallback data.
  }

  const name =
    (user && user.name) ||
    fallback.display_name ||
    fallback.fp_short ||
    csElideFp(safeFp) ||
    "Unknown";

  const role = (user && user.role) || "";
  const avatarUrl = (user && user.avatar_url) || fallback.avatar_url || "";
  const fpShort = (user && user.fp_short) || fallback.fp_short || csElideFp(safeFp);

  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal cs-profile-modal";

  const close = () => backdrop.remove();

  const head = document.createElement("div");
  head.className = "cs-profile-head";

  const avatar = document.createElement("div");
  avatar.className = "cs-profile-avatar";

  if (avatarUrl) {
    const img = document.createElement("img");
    img.src = avatarUrl;
    img.alt = "";
    avatar.appendChild(img);
  } else {
    avatar.textContent = name.slice(0, 1).toUpperCase();
  }

  const titleWrap = document.createElement("div");
  titleWrap.className = "cs-profile-title-wrap";

  const title = document.createElement("div");
  title.className = "cs-profile-name";
  title.textContent = name;

  const sub = document.createElement("div");
  sub.className = "cs-profile-sub";
  sub.textContent = role ? `${role} · ${fpShort}` : fpShort;

  titleWrap.appendChild(title);
  titleWrap.appendChild(sub);

  head.appendChild(avatar);
  head.appendChild(titleWrap);

  const body = document.createElement("div");
  body.className = "cs-profile-body";

  const fpLabel = document.createElement("div");
  fpLabel.className = "cs-profile-label";
  fpLabel.textContent = "Fingerprint";

  const fpValue = document.createElement("div");
  fpValue.className = "cs-profile-fingerprint";
  fpValue.textContent = safeFp || fpShort || "unknown";

  body.appendChild(fpLabel);
  body.appendChild(fpValue);

  const actions = document.createElement("div");
  actions.className = "cs-modal-actions";

  if (safeFp && navigator.clipboard) {
    const copy = document.createElement("button");
    copy.className = "cs-modal-cancel";
    copy.type = "button";
    copy.textContent = "Copy fingerprint";
    copy.addEventListener("click", async () => {
      await navigator.clipboard.writeText(safeFp);
      copy.textContent = "Copied";
      setTimeout(() => { copy.textContent = "Copy fingerprint"; }, 1200);
    });
    actions.appendChild(copy);
  }

  const closeBtn = document.createElement("button");
  closeBtn.className = "cs-modal-cancel";
  closeBtn.type = "button";
  closeBtn.textContent = "Close";
  closeBtn.addEventListener("click", close);
  actions.appendChild(closeBtn);

  modal.appendChild(head);
  modal.appendChild(body);
  modal.appendChild(actions);

  backdrop.appendChild(modal);
  backdrop.addEventListener("click", (ev) => {
    if (ev.target === backdrop) close();
  });

  document.body.appendChild(backdrop);
}



function csReactionTitle(summary) {
  const people = Array.isArray(summary.people) ? summary.people : [];
  if (!people.length) return "";

  return people
    .map(p => p.display_name || p.fp_short || csElideFp(p.fp))
    .join(", ");
}

function csReactionPeopleLine(post) {
  const summaries = Array.isArray(post.reactions) ? post.reactions : [];
  const bits = [];

  for (const summary of summaries) {
    const people = Array.isArray(summary.people) ? summary.people : [];
    for (const p of people) {
      bits.push({
        reaction: summary.reaction,
        name: p.display_name || p.fp_short || csElideFp(p.fp)
      });
    }
  }

  if (!bits.length) return null;

  const line = document.createElement("div");
  line.className = "cs-reaction-people";

  const visible = bits.slice(0, 8);
  visible.forEach((item, idx) => {
    const chip = document.createElement("span");
    chip.className = "cs-reaction-person";
    chip.textContent = `${item.name} ${item.reaction}`;
    line.appendChild(chip);

    if (idx < visible.length - 1) {
      const sep = document.createElement("span");
      sep.className = "cs-reaction-sep";
      sep.textContent = "·";
      line.appendChild(sep);
    }
  });

  if (bits.length > visible.length) {
    const more = document.createElement("span");
    more.className = "cs-reaction-more";
    more.textContent = `+${bits.length - visible.length}`;
    line.appendChild(more);
  }

  return line;
}

function csRenderReactionBar(post) {
  const wrap = document.createElement("div");
  wrap.className = "cs-reactions";

  const summaries = new Map(
    (Array.isArray(post.reactions) ? post.reactions : [])
      .map(r => [r.reaction, r])
  );

  const top = document.createElement("div");
  top.className = "cs-reaction-top";

  const summaryRow = document.createElement("div");
  summaryRow.className = "cs-reaction-summary";

  for (const reaction of CS_REACTIONS) {
    const summary = summaries.get(reaction);
    if (!summary || Number(summary.count || 0) <= 0) continue;

    const count = Number(summary.count || 0);
    const isMine = post.my_reaction === reaction || summary.reacted_by_me === true;

    const chip = document.createElement("button");
    chip.className = "cs-reaction-chip";
    if (isMine) chip.classList.add("is-active");
    chip.type = "button";
    chip.textContent = `${reaction} ${count}`;

    const names = csReactionTitle(summary);
    chip.title = names ? `${reaction} ${names}` : reaction;

    chip.addEventListener("click", async () => {
      await csReactToPost(post.id, isMine ? "" : reaction);
    });

    summaryRow.appendChild(chip);
  }

  const picker = document.createElement("div");
  picker.className = "cs-reaction-picker";

  const trigger = document.createElement("button");
  trigger.className = "cs-reaction-trigger";
  trigger.type = "button";
  trigger.textContent = post.my_reaction ? `${post.my_reaction} React` : "🙂 React";
  trigger.title = "React to this post";

  const menu = document.createElement("div");
  menu.className = "cs-reaction-menu";

  for (const reaction of CS_REACTIONS) {
    const isMine = post.my_reaction === reaction;

    const btn = document.createElement("button");
    btn.className = "cs-reaction-menu-button";
    if (isMine) btn.classList.add("is-active");
    btn.type = "button";
    btn.textContent = reaction;
    btn.title = isMine ? "Remove reaction" : `React ${reaction}`;

    btn.addEventListener("click", async () => {
      await csReactToPost(post.id, isMine ? "" : reaction);
    });

    menu.appendChild(btn);
  }

  picker.appendChild(trigger);
  picker.appendChild(menu);

  if (summaryRow.children.length > 0) {
    top.appendChild(summaryRow);
  }

  top.appendChild(picker);
  wrap.appendChild(top);

  const peopleLine = csReactionPeopleLine(post);
  if (peopleLine) {
    wrap.appendChild(peopleLine);
  }

  return wrap;
}

async function csReactToPost(postId, reaction) {
  if (!postId) return;

  await fetch(`${CS_API}/posts/react`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ post_id: postId, reaction })
  });

  await csLoadFeed();
}



function csRenderReplies(post) {
  const wrap = document.createElement("section");
  wrap.className = "cs-replies";

  const replies = Array.isArray(post.replies) ? post.replies : [];

  const list = document.createElement("div");
  list.className = "cs-reply-list";

  for (const reply of replies) {
    list.appendChild(csRenderReply(reply));
  }

  const toggle = document.createElement("button");
  toggle.className = "cs-reply-toggle";
  toggle.type = "button";
  toggle.textContent = replies.length ? `Reply (${replies.length})` : "Reply";

  const updateReplyCount = () => {
    toggle.textContent = replies.length ? `Reply (${replies.length})` : "Reply";
  };

  const composer = csRenderReplyComposer(post.id, (reply) => {
    replies.push(reply);
    list.appendChild(csRenderReply(reply));
    updateReplyCount();
    composer.hidden = true;
  });
  composer.hidden = true;

  toggle.addEventListener("click", () => {
    if (composer.hidden) {
      composer.hidden = false;
      composer.querySelector("textarea")?.focus();
      return;
    }

    csRequestCloseReplyComposer(composer);
  });

  wrap.appendChild(list);
  wrap.appendChild(toggle);
  wrap.appendChild(composer);

  return wrap;
}


function csRenderReplyReactionBar(reply) {
  const wrap = document.createElement("div");
  wrap.className = "cs-reactions cs-reply-reactions";

  const summaries = new Map(
    (Array.isArray(reply.reactions) ? reply.reactions : [])
      .map(r => [r.reaction, r])
  );

  const top = document.createElement("div");
  top.className = "cs-reaction-top";

  const summaryRow = document.createElement("div");
  summaryRow.className = "cs-reaction-summary";

  for (const reaction of CS_REACTIONS) {
    const summary = summaries.get(reaction);
    if (!summary || Number(summary.count || 0) <= 0) continue;

    const count = Number(summary.count || 0);
    const isMine = reply.my_reaction === reaction || summary.reacted_by_me === true;

    const chip = document.createElement("button");
    chip.className = "cs-reaction-chip";
    if (isMine) chip.classList.add("is-active");
    chip.type = "button";
    chip.textContent = `${reaction} ${count}`;

    const names = csReactionTitle(summary);
    chip.title = names ? `${reaction} ${names}` : reaction;

    chip.addEventListener("click", async () => {
      await csReactToReply(reply.id, isMine ? "" : reaction);
    });

    summaryRow.appendChild(chip);
  }

  const picker = document.createElement("div");
  picker.className = "cs-reaction-picker cs-reply-reaction-picker";

  const trigger = document.createElement("button");
  trigger.className = "cs-reaction-trigger";
  trigger.type = "button";
  trigger.textContent = reply.my_reaction ? `${reply.my_reaction} React` : "🙂 React";
  trigger.title = "React to this reply";

  const menu = document.createElement("div");
  menu.className = "cs-reaction-menu";

  for (const reaction of CS_REACTIONS) {
    const isMine = reply.my_reaction === reaction;

    const btn = document.createElement("button");
    btn.className = "cs-reaction-menu-button";
    if (isMine) btn.classList.add("is-active");
    btn.type = "button";
    btn.textContent = reaction;
    btn.title = isMine ? "Remove reaction" : `React ${reaction}`;

    btn.addEventListener("click", async () => {
      await csReactToReply(reply.id, isMine ? "" : reaction);
    });

    menu.appendChild(btn);
  }

  picker.appendChild(trigger);
  picker.appendChild(menu);

  if (summaryRow.children.length > 0) {
    top.appendChild(summaryRow);
  }

  top.appendChild(picker);
  wrap.appendChild(top);

  const peopleLine = csReactionPeopleLine(reply);
  if (peopleLine) {
    wrap.appendChild(peopleLine);
  }

  return wrap;
}

async function csReactToReply(replyId, reaction) {
  if (!replyId) return;

  await fetch(`${CS_API}/replies/react`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ reply_id: replyId, reaction })
  });

  await csLoadFeed();
}


function csRenderReply(reply) {
  const row = document.createElement("div");
  row.className = "cs-reply";
  row.dataset.replyId = String(reply.id || "");

  const avatar = document.createElement("button");
  avatar.className = "cs-reply-avatar";
  avatar.type = "button";
  avatar.title = "Open person card";

  if (reply.actor_avatar_url) {
    const img = document.createElement("img");
    img.src = reply.actor_avatar_url;
    img.alt = "";
    avatar.appendChild(img);
  } else {
    avatar.textContent = (reply.actor_display_name || "?").slice(0, 1).toUpperCase();
  }

  avatar.addEventListener("click", () => {
    csOpenPersonCard(reply.actor_fp || "", {
      display_name: reply.actor_display_name || "",
      fp_short: reply.actor_fp_short || "",
      avatar_url: reply.actor_avatar_url || ""
    });
  });

  const body = document.createElement("div");
  body.className = "cs-reply-body";

  const head = document.createElement("div");
  head.className = "cs-reply-head";

  const name = document.createElement("button");
  name.className = "cs-reply-author";
  name.type = "button";
  name.textContent = reply.actor_display_name || reply.actor_fp_short || "unknown";
  name.addEventListener("click", () => {
    csOpenPersonCard(reply.actor_fp || "", {
      display_name: reply.actor_display_name || "",
      fp_short: reply.actor_fp_short || "",
      avatar_url: reply.actor_avatar_url || ""
    });
  });

  const time = document.createElement("span");
  time.className = "cs-reply-time";
  time.textContent = reply.created_epoch
    ? new Date(reply.created_epoch * 1000).toLocaleString()
    : "";

  head.appendChild(name);
  head.appendChild(time);

  if (reply.is_mine) {
    const tools = document.createElement("div");
    tools.className = "cs-reply-tools";

    const edit = document.createElement("button");
    edit.type = "button";
    edit.textContent = "Edit";
    edit.addEventListener("click", () => {
      csOpenReplyEdit(row, reply);
    });

    const del = document.createElement("button");
    del.type = "button";
    del.textContent = "Delete";
    del.addEventListener("click", async () => {
      if (!confirm("Delete this reply?")) return;

      const ok = await csDeleteReply(reply.id);
      if (ok) {
        row.remove();
        csUpdateReplyCountNear(row);
      }
    });

    tools.appendChild(edit);
    tools.appendChild(del);
    head.appendChild(tools);
  }

  body.appendChild(head);

  const content = document.createElement("div");
  content.className = "cs-reply-content";
  csFillReplyContent(content, reply);
  body.appendChild(content);
  body.appendChild(csRenderReplyReactionBar(reply));

  row.appendChild(avatar);
  row.appendChild(body);
  return row;
}


function csRenderReplyMentions(reply) {
  const mentions = Array.isArray(reply.mentions) ? reply.mentions : [];
  if (!mentions.length) return null;

  const wrap = document.createElement("div");
  wrap.className = "cs-reply-mentions";

  const label = document.createElement("span");
  label.className = "cs-post-mentions-label";
  label.textContent = "Tagged:";
  wrap.appendChild(label);

  for (const m of mentions) {
    const chip = document.createElement("button");
    chip.className = "cs-mention-chip";
    chip.type = "button";
    chip.textContent = `@${m.display_name || m.fp_short || csElideFp(m.fp)}`;
    chip.title = m.fp || "";

    chip.addEventListener("click", () => {
      csOpenPersonCard(m.fp || "", {
        display_name: m.display_name || "",
        fp_short: m.fp_short || "",
        avatar_url: m.avatar_url || ""
      });
    });

    wrap.appendChild(chip);
  }

  return wrap;
}

async function csOpenReplyMentionPicker(selectedMentions, onChange) {
  const candidates = await csLoadMentionCandidates();

  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal cs-intro-modal";

  modal.innerHTML = `
    <div class="cs-modal-title">Tag friend</div>
    <div class="cs-modal-text">Pick people to tag in this reply.</div>
    <input id="csReplyMentionSearch" placeholder="Search people...">
    <div id="csReplyMentionResults" class="cs-mention-results"></div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel" type="button">Close</button>
    </div>
  `;

  const input = modal.querySelector("#csReplyMentionSearch");
  const results = modal.querySelector("#csReplyMentionResults");
  const selected = new Set(selectedMentions.map(p => p.fingerprint));

  function render(q = "") {
    const needle = q.trim().toLowerCase();
    results.textContent = "";

    const filtered = candidates.filter(p => {
      const hay = `${p.name || ""} ${p.fingerprint || ""}`.toLowerCase();
      return !needle || hay.includes(needle);
    });

    if (!filtered.length) {
      const empty = document.createElement("div");
      empty.className = "cs-search-hint";
      empty.textContent = "No people found";
      results.appendChild(empty);
      return;
    }

    for (const person of filtered) {
      const row = document.createElement("button");
      row.className = "cs-mention-result";
      row.type = "button";

      const avatar = document.createElement("span");
      avatar.className = "cs-mention-avatar";

      if (person.avatar_url) {
        const img = document.createElement("img");
        img.src = person.avatar_url;
        img.alt = "";
        avatar.appendChild(img);
      } else {
        avatar.textContent = (person.name || "?").slice(0, 1).toUpperCase();
      }

      const name = document.createElement("span");
      name.className = "cs-mention-name";
      name.textContent = person.name || person.fp_short || csElideFp(person.fingerprint);

      const mark = document.createElement("span");
      mark.className = "cs-mention-mark";
      mark.textContent = selected.has(person.fingerprint) ? "✓" : "";

      row.appendChild(avatar);
      row.appendChild(name);
      row.appendChild(mark);

      row.addEventListener("click", () => {
        if (selected.has(person.fingerprint)) {
          selected.delete(person.fingerprint);
          selectedMentions = selectedMentions.filter(
            p => p.fingerprint !== person.fingerprint
          );
        } else {
          selected.add(person.fingerprint);
          selectedMentions.push(person);
        }

        if (typeof onChange === "function") {
          onChange(selectedMentions);
        }

        render(input.value);
      });

      results.appendChild(row);
    }
  }

  input.addEventListener("input", () => render(input.value));
  modal.querySelector(".cs-modal-cancel").addEventListener("click", () => {
    backdrop.remove();
  });

  backdrop.appendChild(modal);
  document.body.appendChild(backdrop);

  render("");
  input.focus();
}


function csFillReplyContent(content, reply) {
  content.textContent = "";

  if (reply.text) {
    content.appendChild(csRenderTextWithLinks(reply.text, "cs-reply-text"));

    const preview = csRenderLinkPreviewFromText(reply.text);
    if (preview) {
      content.appendChild(preview);
    }
  }

  const mentions = csRenderReplyMentions(reply);
  if (mentions) {
    content.appendChild(mentions);
  }

  if (reply.media_url) {
    const img = document.createElement("img");
    img.className = "cs-reply-media";
    img.src = reply.media_url;
    img.loading = "lazy";
    img.decoding = "async";
    img.alt = "";
    content.appendChild(img);
  }
}

function csUpdateReplyCountNear(row) {
  const replies = row.closest(".cs-replies");
  if (!replies) return;

  const count = replies.querySelectorAll(".cs-reply").length;
  const toggle = replies.querySelector(".cs-reply-toggle");

  if (toggle) {
    toggle.textContent = count ? `Reply (${count})` : "Reply";
  }
}

function csOpenReplyEdit(row, reply) {
  const body = row.querySelector(".cs-reply-body");
  const content = row.querySelector(".cs-reply-content");
  if (!body || !content) return;

  const oldEditor = row.querySelector(".cs-reply-edit-box");
  if (oldEditor) {
    oldEditor.remove();
    content.hidden = false;
    return;
  }

  content.hidden = true;

  const box = document.createElement("div");
  box.className = "cs-reply-edit-box";

  const textarea = document.createElement("textarea");
  textarea.className = "cs-reply-textarea";
  textarea.value = reply.text || "";

  const mediaRow = document.createElement("div");
  mediaRow.className = "cs-reply-media-row";

  const mediaInput = document.createElement("input");
  mediaInput.className = "cs-reply-media-input";
  mediaInput.placeholder = "Optional image path";

  const browse = document.createElement("button");
  browse.className = "cs-reply-browse";
  browse.type = "button";
  browse.textContent = "Browse";

  const actions = document.createElement("div");
  actions.className = "cs-reply-edit-actions";

  const cancel = document.createElement("button");
  cancel.className = "cs-reply-browse";
  cancel.type = "button";
  cancel.textContent = "Cancel";

  const save = document.createElement("button");
  save.className = "cs-reply-submit";
  save.type = "button";
  save.textContent = "Save";

  browse.addEventListener("click", async () => {
    const picked = await csOpenMediaPicker();
    if (picked) mediaInput.value = picked;
  });

  cancel.addEventListener("click", () => {
    box.remove();
    content.hidden = false;
  });

  save.addEventListener("click", async () => {
    const text = textarea.value.trim();
    const media_path = mediaInput.value.trim();

    if (!text && !media_path) return;

    save.disabled = true;

    try {
      const updated = await csUpdateReply(reply.id, text, media_path);
      if (!updated) return;

      Object.assign(reply, updated);
      csFillReplyContent(content, reply);
      box.remove();
      content.hidden = false;
    } finally {
      save.disabled = false;
    }
  });

  mediaRow.appendChild(mediaInput);
  mediaRow.appendChild(browse);

  actions.appendChild(cancel);
  actions.appendChild(save);

  box.appendChild(textarea);
  box.appendChild(mediaRow);
  box.appendChild(actions);

  content.after(box);
  textarea.focus();
}

async function csUpdateReply(id, text, media_path) {
  const res = await fetch(`${CS_API}/replies/update`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ id, text, media_path })
  });

  if (!res.ok) return null;

  const data = await res.json();
  return data.reply || null;
}

async function csDeleteReply(id) {
  const res = await fetch(`${CS_API}/replies/delete`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ id })
  });

  return res.ok;
}



let csReplyComposerClosingInitialized = false;

function csReplyComposerHasDraft(composer) {
  if (!composer) return false;

  const textarea = composer.querySelector(".cs-reply-textarea");
  const mediaInput = composer.querySelector(".cs-reply-media-input");

  return Boolean(
    (textarea && textarea.value.trim()) ||
    (mediaInput && mediaInput.value.trim())
  );
}

function csClearReplyComposerDraft(composer) {
  if (!composer) return;

  const textarea = composer.querySelector(".cs-reply-textarea");
  const mediaInput = composer.querySelector(".cs-reply-media-input");

  if (textarea) textarea.value = "";
  if (mediaInput) mediaInput.value = "";
}

function csCloseReplyComposer(composer, options = {}) {
  if (!composer) return false;

  const discard = options.discard === true;

  if (csReplyComposerHasDraft(composer) && !discard) {
    return false;
  }

  if (discard) {
    csClearReplyComposerDraft(composer);
  }

  composer.hidden = true;
  return true;
}

function csRequestCloseReplyComposer(composer) {
  if (!composer) return false;

  if (csReplyComposerHasDraft(composer)) {
    const ok = confirm("Discard this reply draft?");
    if (!ok) return false;

    return csCloseReplyComposer(composer, { discard: true });
  }

  return csCloseReplyComposer(composer);
}

function csInitReplyComposerClosing() {
  if (csReplyComposerClosingInitialized) return;
  csReplyComposerClosingInitialized = true;

  document.addEventListener("click", (ev) => {
    const openComposers = document.querySelectorAll(".cs-reply-composer:not([hidden])");
    if (!openComposers.length) return;

    if (ev.target.closest(".cs-reply-composer")) return;
    if (ev.target.closest(".cs-reply-toggle")) return;
    if (ev.target.closest(".cs-modal-backdrop")) return;
    if (ev.target.closest(".cs-lightbox")) return;

    for (const composer of openComposers) {
      csCloseReplyComposer(composer);
    }
  });

  document.addEventListener("keydown", (ev) => {
    if (ev.key !== "Escape") return;

    const openComposers = document.querySelectorAll(".cs-reply-composer:not([hidden])");
    for (const composer of openComposers) {
      csCloseReplyComposer(composer);
    }
  });
}


function csRenderReplyComposer(postId, onReplyCreated) {
  csInitReplyComposerClosing();

  let selectedMentions = [];

  const box = document.createElement("div");
  box.className = "cs-reply-composer";

  const close = document.createElement("button");
  close.className = "cs-reply-composer-close";
  close.type = "button";
  close.textContent = "×";
  close.title = "Close reply composer";
  close.setAttribute("aria-label", "Close reply composer");
  close.addEventListener("click", () => {
    csRequestCloseReplyComposer(box);
  });

  const textarea = document.createElement("textarea");
  textarea.className = "cs-reply-textarea";
  textarea.placeholder = "Write a reply...";

  const mediaRow = document.createElement("div");
  mediaRow.className = "cs-reply-media-row";

  const mediaInput = document.createElement("input");
  mediaInput.className = "cs-reply-media-input";
  mediaInput.placeholder = "Optional image path";

  const browse = document.createElement("button");
  browse.className = "cs-reply-browse";
  browse.type = "button";
  browse.textContent = "Browse";

  const mentionChips = document.createElement("div");
  mentionChips.className = "cs-reply-mention-chips";

  function renderSelectedReplyMentions() {
    mentionChips.textContent = "";

    for (const person of selectedMentions) {
      const chip = document.createElement("span");
      chip.className = "cs-compose-mention-chip";

      const label = document.createElement("span");
      label.textContent = `@${person.name || person.fp_short || csElideFp(person.fingerprint)}`;

      const remove = document.createElement("button");
      remove.type = "button";
      remove.textContent = "×";
      remove.title = "Remove tag";
      remove.addEventListener("click", () => {
        selectedMentions = selectedMentions.filter(
          p => p.fingerprint !== person.fingerprint
        );
        renderSelectedReplyMentions();
      });

      chip.appendChild(label);
      chip.appendChild(remove);
      mentionChips.appendChild(chip);
    }
  }

  const tagBtn = document.createElement("button");
  tagBtn.className = "cs-reply-tag";
  tagBtn.type = "button";
  tagBtn.textContent = "Tag friend";
  tagBtn.addEventListener("click", async () => {
    await csOpenReplyMentionPicker(selectedMentions, (next) => {
      selectedMentions = next;
      renderSelectedReplyMentions();
    });
  });

  const submit = document.createElement("button");
  submit.className = "cs-reply-submit";
  submit.type = "button";
  submit.textContent = "Send";

  browse.addEventListener("click", async () => {
    const picked = await csOpenMediaPicker();
    if (picked) mediaInput.value = picked;
  });

  submit.addEventListener("click", async () => {
    const text = textarea.value.trim();
    const media_path = mediaInput.value.trim();
    if (!text && !media_path) return;

    submit.disabled = true;
    try {
      const mentions = selectedMentions.map(p => p.fingerprint).filter(Boolean);
      const reply = await csCreateReply(postId, text, media_path, mentions);

      if (reply) {
        csClearReplyComposerDraft(box);
        selectedMentions = [];
        renderSelectedReplyMentions();

        if (typeof onReplyCreated === "function") {
          onReplyCreated(reply);
        }
      }
    } finally {
      submit.disabled = false;
    }
  });

  mediaRow.appendChild(mediaInput);
  mediaRow.appendChild(browse);

  const bottom = document.createElement("div");
  bottom.className = "cs-reply-composer-bottom";
  bottom.appendChild(tagBtn);
  bottom.appendChild(submit);

  box.appendChild(close);
  box.appendChild(textarea);
  box.appendChild(mediaRow);
  box.appendChild(mentionChips);
  box.appendChild(bottom);

  return box;
}

async function csCreateReply(postId, text, media_path, mentions = []) {
  const res = await fetch(`${CS_API}/posts/reply`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ post_id: postId, text, media_path, mentions })
  });

  if (!res.ok) {
    return null;
  }

  const data = await res.json();
  return data.reply || null;
}



function csExtractUrls(text) {
  const raw = String(text || "");
  const re = /\bhttps?:\/\/[^\s<>"']+/gi;
  const out = [];
  let m;

  while ((m = re.exec(raw)) !== null) {
    let url = m[0];

    while (/[),.;!?]+$/.test(url)) {
      url = url.slice(0, -1);
    }

    if (!url) continue;

    out.push({
      url,
      index: m.index,
      end: m.index + url.length
    });
  }

  return out;
}

function csAbsoluteUrl(url) {
  try {
    return new URL(String(url || ""), window.location.origin);
  } catch (_) {
    return null;
  }
}

function csIsSameOriginUrl(urlObj) {
  return !!(urlObj && urlObj.origin === window.location.origin);
}

function csMetaContent(doc, selector) {
  const el = doc.querySelector(selector);
  return el ? String(el.getAttribute("content") || "").trim() : "";
}

function csPreviewImageAbs(raw, baseUrl) {
  const v = String(raw || "").trim();
  if (!v) return "";

  try {
    return new URL(v, baseUrl).href;
  } catch (_) {
    return "";
  }
}

function csPreviewImageLooksDecorative(url) {
  const u = String(url || "").toLowerCase();

  return (
    u.includes("favicon") ||
    u.includes("/icon") ||
    u.includes("icon.") ||
    u.includes("logo") ||
    u.includes("avatar") ||
    u.includes("profile") ||
    u.includes("mascot") ||
    u.includes("squirrel") ||
    u.includes("chipmunk") ||
    u.includes("onboarding") ||
    u.includes("guide") ||
    u.includes("nav_icon") ||
    u.includes("nexuslogo")
  );
}

function csPreviewImageScore(item) {
  const url = String(item.url || "");
  const u = url.toLowerCase();
  const el = item.el || null;

  if (!url) return -10000;
  if (u.startsWith("data:image/svg")) return -10000;

  let score = 0;

  if (item.source === "meta") score += 20;
  if (item.source === "img") score += 40;
  if (item.source === "style") score += 35;

  if (csPreviewImageLooksDecorative(url)) score -= 500;

  if (/\.(jpg|jpeg|png|webp|gif)(\?|#|$)/i.test(url)) score += 25;

  if (
    u.includes("photo") ||
    u.includes("gallery") ||
    u.includes("album") ||
    u.includes("share") ||
    u.includes("thumb") ||
    u.includes("thumbnail") ||
    u.includes("preview") ||
    u.includes("/api/v4/")
  ) {
    score += 45;
  }

  if (el) {
    const hay = [
      el.className || "",
      el.id || "",
      el.getAttribute("alt") || "",
      el.getAttribute("title") || "",
      el.closest("[class]")?.className || "",
      el.closest("article")?.className || "",
      el.closest("main")?.className || ""
    ].join(" ").toLowerCase();

    if (
      hay.includes("album") ||
      hay.includes("cover") ||
      hay.includes("photo") ||
      hay.includes("gallery") ||
      hay.includes("tile") ||
      hay.includes("thumb") ||
      hay.includes("preview")
    ) {
      score += 80;
    }

    if (
      hay.includes("logo") ||
      hay.includes("avatar") ||
      hay.includes("profile") ||
      hay.includes("mascot") ||
      hay.includes("onboarding") ||
      hay.includes("guide")
    ) {
      score -= 250;
    }

    const w = Number(el.getAttribute("width") || 0);
    const h = Number(el.getAttribute("height") || 0);

    if (w >= 120 && h >= 80) score += 25;
    if (w > 0 && w <= 80) score -= 80;
    if (h > 0 && h <= 80) score -= 80;
  }

  return score;
}

function csExtractCssUrl(styleValue) {
  const s = String(styleValue || "");
  const m = s.match(/url\((['"]?)(.*?)\1\)/i);
  return m ? String(m[2] || "").trim() : "";
}

function csResolvePreviewImage(doc, baseUrl) {
  const candidates = [];
  const seen = new Set();

  function add(raw, source, el = null) {
    const url = csPreviewImageAbs(raw, baseUrl);
    if (!url || seen.has(url)) return;

    seen.add(url);
    candidates.push({ url, source, el });
  }

  add(csMetaContent(doc, 'meta[property="og:image"]'), "meta");
  add(csMetaContent(doc, 'meta[name="twitter:image"]'), "meta");

  // OpenGraph/Twitter image is the page author's intended preview image.
  // Trust it before scanning visible <img> tags, otherwise album pages may pick
  // the first grid image instead of the selected album cover.
  const metaBest = candidates
    .filter((item) => item.source === "meta")
    .sort((a, b) => csPreviewImageScore(b) - csPreviewImageScore(a))[0];

  if (
    metaBest &&
    csPreviewImageScore(metaBest) >= 0 &&
    !csPreviewImageLooksDecorative(metaBest.url)
  ) {
    return metaBest.url;
  }

  const preferredSelectors = [
    ".album img",
    ".albumCard img",
    ".album-card img",
    ".albumTile img",
    ".album-tile img",
    ".gallery img",
    ".photoGrid img",
    ".photo-grid img",
    ".tile img",
    ".shareGrid img",
    ".share-grid img",
    ".cover img",
    "main img",
    "article img",
    "img"
  ];

  for (const selector of preferredSelectors) {
    for (const img of doc.querySelectorAll(selector)) {
      add(
        img.getAttribute("src") ||
        img.getAttribute("data-src") ||
        img.getAttribute("data-lazy-src") ||
        img.getAttribute("data-thumb") ||
        img.getAttribute("data-thumbnail") ||
        "",
        "img",
        img
      );

      const srcset = img.getAttribute("srcset") || "";
      if (srcset) {
        const first = srcset.split(",")[0]?.trim()?.split(/\s+/)[0] || "";
        add(first, "img", img);
      }
    }
  }

  for (const el of doc.querySelectorAll("[style]")) {
    const bg = csExtractCssUrl(el.getAttribute("style") || "");
    if (bg) add(bg, "style", el);
  }

  candidates.sort((a, b) => csPreviewImageScore(b) - csPreviewImageScore(a));

  const best = candidates[0];
  if (!best) return "";

  if (csPreviewImageScore(best) < 0) {
    return "";
  }

  return best.url;
}

function csYouTubeVideoIdFromUrl(urlObj) {
  if (!urlObj) return "";

  const host = String(urlObj.hostname || "").toLowerCase().replace(/^www\./, "");
  const path = String(urlObj.pathname || "");

  if (host === "youtu.be") {
    const id = path.split("/").filter(Boolean)[0] || "";
    return /^[A-Za-z0-9_-]{6,}$/.test(id) ? id : "";
  }

  if (host === "youtube.com" || host === "m.youtube.com" || host === "music.youtube.com") {
    const v = urlObj.searchParams.get("v") || "";
    if (/^[A-Za-z0-9_-]{6,}$/.test(v)) return v;

    const parts = path.split("/").filter(Boolean);
    const embedIndex = parts.indexOf("embed");
    if (embedIndex >= 0 && parts[embedIndex + 1] && /^[A-Za-z0-9_-]{6,}$/.test(parts[embedIndex + 1])) {
      return parts[embedIndex + 1];
    }

    const shortsIndex = parts.indexOf("shorts");
    if (shortsIndex >= 0 && parts[shortsIndex + 1] && /^[A-Za-z0-9_-]{6,}$/.test(parts[shortsIndex + 1])) {
      return parts[shortsIndex + 1];
    }
  }

  return "";
}

function csYouTubeThumbUrl(videoId) {
  return videoId
    ? `https://i.ytimg.com/vi/${encodeURIComponent(videoId)}/hqdefault.jpg`
    : "";
}

function csDefaultLinkPreviewBadge(urlObj) {
  if (urlObj && urlObj.pathname.startsWith("/s/")) {
    return "DNA-NEXUS SHARE";
  }

  return urlObj ? urlObj.hostname : "LINK";
}

function csPreviewBadgeFromDoc(doc, urlObj) {
  if (!doc || !urlObj) return csDefaultLinkPreviewBadge(urlObj);

  const appName = csMetaContent(doc, 'meta[name="application-name"]');
  const siteName = csMetaContent(doc, 'meta[property="og:site_name"]');
  const ogType = csMetaContent(doc, 'meta[property="og:type"]');
  const ogVideo =
    csMetaContent(doc, 'meta[property="og:video"]') ||
    csMetaContent(doc, 'meta[property="og:video:url"]') ||
    csMetaContent(doc, 'meta[property="og:video:secure_url"]');
  const twitterPlayer =
    csMetaContent(doc, 'meta[name="twitter:player"]') ||
    csMetaContent(doc, 'meta[name="twitter:player:stream"]');

  const hay = [
    appName,
    siteName,
    ogType,
    ogVideo,
    twitterPlayer
  ].join(" ").toLowerCase();

  if (
    hay.includes("reel stack") ||
    hay.includes("reelstack") ||
    hay.includes("video.") ||
    hay.includes("video/") ||
    ogVideo ||
    twitterPlayer
  ) {
    return "REEL STACK VIDEO";
  }

  if (
    hay.includes("photo gallery") ||
    hay.includes("gallery") ||
    hay.includes("album")
  ) {
    return "PHOTO GALLERY SHARE";
  }

  return csDefaultLinkPreviewBadge(urlObj);
}

function csDefaultLinkPreviewTitle(urlObj) {
  if (urlObj && urlObj.pathname.startsWith("/s/")) {
    return "DNA-Nexus shared item";
  }

  return urlObj ? urlObj.hostname : "Link";
}

function csDefaultLinkPreviewDesc(urlObj) {
  if (urlObj && urlObj.pathname.startsWith("/s/")) {
    return "Open shared DNA-Nexus item";
  }

  return urlObj ? urlObj.href : "";
}

function csRenderTextWithLinks(rawText, className) {
  const wrap = document.createElement("div");
  wrap.className = className || "";

  const text = String(rawText || "");
  const urls = csExtractUrls(text);

  if (!urls.length) {
    wrap.textContent = text;
    return wrap;
  }

  let pos = 0;

  for (const item of urls) {
    if (item.index > pos) {
      wrap.appendChild(document.createTextNode(text.slice(pos, item.index)));
    }

    const urlObj = csAbsoluteUrl(item.url);

    const a = document.createElement("a");
    a.className = "cs-text-link";
    a.href = urlObj ? urlObj.href : item.url;
    a.textContent = item.url;
    a.target = "_blank";
    a.rel = "noopener noreferrer";

    wrap.appendChild(a);
    pos = item.end;
  }

  if (pos < text.length) {
    wrap.appendChild(document.createTextNode(text.slice(pos)));
  }

  return wrap;
}

function csRenderLinkPreviewFromText(rawText) {
  const first = csExtractUrls(rawText)[0];
  if (!first) return null;

  const urlObj = csAbsoluteUrl(first.url);
  if (!urlObj) return null;

  const card = document.createElement("a");
  card.className = "cs-link-preview";
  card.href = urlObj.href;
  card.target = "_blank";
  card.rel = "noopener noreferrer";

  const thumb = document.createElement("div");
  thumb.className = "cs-link-preview-thumb";
  thumb.textContent = "🔗";

  const body = document.createElement("div");
  body.className = "cs-link-preview-body";

  const badge = document.createElement("div");
  badge.className = "cs-link-preview-badge";
  badge.textContent = csDefaultLinkPreviewBadge(urlObj);

  const title = document.createElement("div");
  title.className = "cs-link-preview-title";
  title.textContent = csDefaultLinkPreviewTitle(urlObj);

  const desc = document.createElement("div");
  desc.className = "cs-link-preview-desc";
  desc.textContent = csDefaultLinkPreviewDesc(urlObj);

  const urlLine = document.createElement("div");
  urlLine.className = "cs-link-preview-url";
  urlLine.textContent = urlObj.hostname + urlObj.pathname;

  const youtubeVideoId = csYouTubeVideoIdFromUrl(urlObj);
  if (youtubeVideoId) {
    const youtubeThumbUrl = csYouTubeThumbUrl(youtubeVideoId);

    badge.textContent = "YOUTUBE VIDEO";

    if (title.textContent === csDefaultLinkPreviewTitle(urlObj)) {
      title.textContent = "YouTube video";
    }

    if (desc.textContent === csDefaultLinkPreviewDesc(urlObj)) {
      desc.textContent = urlObj.href;
    }

    if (youtubeThumbUrl) {
      thumb.textContent = "";
      thumb.classList.add("has-image");
      thumb.style.backgroundImage = `url("${youtubeThumbUrl.replaceAll('"', "%22")}")`;
    }
  }

  body.appendChild(badge);
  body.appendChild(title);
  body.appendChild(desc);
  body.appendChild(urlLine);

  card.appendChild(thumb);
  card.appendChild(body);

  if (csIsSameOriginUrl(urlObj)) {
    fetch(urlObj.href, {
      credentials: "same-origin",
      cache: "no-store"
    })
      .then(async (res) => {
        const ct = String(res.headers.get("content-type") || "");
        if (!res.ok || !ct.includes("text/html")) return null;
        return await res.text();
      })
      .then((html) => {
        if (!html) return;

        const doc = new DOMParser().parseFromString(html, "text/html");

        const pageBadge = csPreviewBadgeFromDoc(doc, urlObj);
        if (pageBadge) badge.textContent = pageBadge;

        const pageTitle =
          csMetaContent(doc, 'meta[property="og:title"]') ||
          csMetaContent(doc, 'meta[name="twitter:title"]') ||
          String(doc.querySelector("title")?.textContent || "").trim();

        const pageDesc =
          csMetaContent(doc, 'meta[property="og:description"]') ||
          csMetaContent(doc, 'meta[name="description"]') ||
          csMetaContent(doc, 'meta[name="twitter:description"]');

        const imgUrl = csResolvePreviewImage(doc, urlObj.href);

        if (pageTitle) title.textContent = pageTitle;
        if (pageDesc) desc.textContent = pageDesc;

        if (imgUrl) {
          thumb.textContent = "";
          thumb.classList.add("has-image");
          thumb.style.backgroundImage = `url("${imgUrl.replaceAll('"', "%22")}")`;
        }
      })
      .catch(() => {
        // Preview is best-effort. The link itself still works.
      });
  }

  return card;
}


function csRenderPostMentions(post) {
  const mentions = Array.isArray(post.mentions) ? post.mentions : [];
  if (!mentions.length) return null;

  const wrap = document.createElement("div");
  wrap.className = "cs-post-mentions";

  const label = document.createElement("span");
  label.className = "cs-post-mentions-label";
  label.textContent = "Tagged:";
  wrap.appendChild(label);

  for (const m of mentions) {
    const chip = document.createElement("button");
    chip.className = "cs-mention-chip";
    chip.type = "button";
    chip.textContent = `@${m.display_name || m.fp_short || csElideFp(m.fp)}`;
    chip.title = m.fp || "";

    chip.addEventListener("click", () => {
      csOpenPersonCard(m.fp || "", {
        display_name: m.display_name || "",
        fp_short: m.fp_short || "",
        avatar_url: m.avatar_url || ""
      });
    });

    wrap.appendChild(chip);
  }

  return wrap;
}

function csRenderMentionComposer() {
  const chips = document.getElementById("csMentionChips");
  if (!chips) return;

  chips.textContent = "";

  for (const person of csSelectedMentions) {
    const chip = document.createElement("span");
    chip.className = "cs-compose-mention-chip";

    const label = document.createElement("span");
    label.textContent = `@${person.name || person.fp_short || csElideFp(person.fingerprint)}`;

    const remove = document.createElement("button");
    remove.type = "button";
    remove.textContent = "×";
    remove.title = "Remove tag";
    remove.addEventListener("click", () => {
      csSelectedMentions = csSelectedMentions.filter(
        p => p.fingerprint !== person.fingerprint
      );
      csRenderMentionComposer();
    });

    chip.appendChild(label);
    chip.appendChild(remove);
    chips.appendChild(chip);
  }
}

async function csLoadMentionCandidates() {
  const [peopleRes, usersRes] = await Promise.all([
    fetch(`${CS_API}/people`, { credentials: "same-origin" }),
    fetch(`${CS_API}/users`, { credentials: "same-origin" })
  ]);

  const peopleData = await peopleRes.json();
  const usersData = await usersRes.json();

  const usersByFp = new Map(
    (usersData.users || []).map(u => [u.fingerprint, u])
  );

  const out = [];

  for (const p of (peopleData.items || [])) {
    const u = usersByFp.get(p.fp);
    if (!u || u.is_me || u.role === "external") continue;

    out.push({
      fingerprint: u.fingerprint,
      fp_short: u.fp_short,
      name: u.name || p.display_name || u.fp_short,
      avatar_url: u.avatar_url || ""
    });
  }

  out.sort((a, b) => String(a.name || "").localeCompare(String(b.name || "")));
  return out;
}

async function csOpenMentionPicker() {
  const candidates = await csLoadMentionCandidates();

  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal cs-intro-modal";

  modal.innerHTML = `
    <div class="cs-modal-title">Tag friend</div>
    <div class="cs-modal-text">Pick people from your Circle / contacts.</div>
    <input id="csMentionSearch" placeholder="Search people...">
    <div id="csMentionResults" class="cs-mention-results"></div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel" type="button">Close</button>
    </div>
  `;

  const input = modal.querySelector("#csMentionSearch");
  const results = modal.querySelector("#csMentionResults");

  const selected = new Set(csSelectedMentions.map(p => p.fingerprint));

  function render(q = "") {
    const needle = q.trim().toLowerCase();
    results.textContent = "";

    const filtered = candidates.filter(p => {
      const hay = `${p.name || ""} ${p.fingerprint || ""}`.toLowerCase();
      return !needle || hay.includes(needle);
    });

    if (!filtered.length) {
      const empty = document.createElement("div");
      empty.className = "cs-search-hint";
      empty.textContent = "No people found";
      results.appendChild(empty);
      return;
    }

    for (const person of filtered) {
      const row = document.createElement("button");
      row.className = "cs-mention-result";
      row.type = "button";

      const avatar = document.createElement("span");
      avatar.className = "cs-mention-avatar";

      if (person.avatar_url) {
        const img = document.createElement("img");
        img.src = person.avatar_url;
        img.alt = "";
        avatar.appendChild(img);
      } else {
        avatar.textContent = (person.name || "?").slice(0, 1).toUpperCase();
      }

      const name = document.createElement("span");
      name.className = "cs-mention-name";
      name.textContent = person.name || person.fp_short || csElideFp(person.fingerprint);

      const mark = document.createElement("span");
      mark.className = "cs-mention-mark";
      mark.textContent = selected.has(person.fingerprint) ? "✓" : "";

      row.appendChild(avatar);
      row.appendChild(name);
      row.appendChild(mark);

      row.addEventListener("click", () => {
        if (selected.has(person.fingerprint)) {
          selected.delete(person.fingerprint);
          csSelectedMentions = csSelectedMentions.filter(
            p => p.fingerprint !== person.fingerprint
          );
        } else {
          selected.add(person.fingerprint);
          csSelectedMentions.push(person);
        }

        csRenderMentionComposer();
        render(input.value);
      });

      results.appendChild(row);
    }
  }

  input.addEventListener("input", () => render(input.value));
  modal.querySelector(".cs-modal-cancel").addEventListener("click", () => {
    backdrop.remove();
  });

  backdrop.appendChild(modal);
  document.body.appendChild(backdrop);

  render("");
  input.focus();
}


function csRenderPost(post) {
  const el = document.createElement("article");
  el.className = "cs-post";

  const header = document.createElement("div");
  header.className = "cs-post-header";

  const author = document.createElement("button");
  author.className = "cs-post-author cs-post-author-button";
  author.type = "button";
  author.textContent = post.owner_display_name || post.owner_fp_short || "anon";
  author.title = "Open person card";
  author.addEventListener("click", () => {
    csOpenPersonCard(post.owner_fp || "", {
      display_name: post.owner_display_name || "",
      fp_short: post.owner_fp_short || "",
      avatar_url: post.owner_avatar_url || ""
    });
  });
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

  if (post.text && post.post_kind !== "memory_node") {
    el.appendChild(csRenderTextWithLinks(post.text || "", "cs-post-text"));

    const preview = csRenderLinkPreviewFromText(post.text || "");
    if (preview) {
      el.appendChild(preview);
    }
  }

  const mentions = csRenderPostMentions(post);
  if (mentions) {
    el.appendChild(mentions);
  }

  if (post.media_url) {
    const img = document.createElement("img");
    img.className = "cs-post-media";
    img.src = post.media_url;
    img.loading = "lazy";
    img.decoding = "async";
    img.alt = "";
    el.appendChild(img);
  }

  if (window.CircleStackMemoryNodes &&
      typeof window.CircleStackMemoryNodes.decoratePost === "function") {
    window.CircleStackMemoryNodes.decoratePost(el, post);
  }

  el.appendChild(csRenderReactionBar(post));
  el.appendChild(csRenderReplies(post));

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


function csComposeHasDraft() {
  const textEl = document.getElementById("csText");
  const mediaEl = document.getElementById("csMediaPath");

  return Boolean(
    (textEl && textEl.value.trim()) ||
    (mediaEl && mediaEl.value.trim()) ||
    (Array.isArray(csSelectedMentions) && csSelectedMentions.length > 0)
  );
}

function csClearComposeDraft() {
  const textEl = document.getElementById("csText");
  const mediaEl = document.getElementById("csMediaPath");

  if (textEl) textEl.value = "";
  if (mediaEl) mediaEl.value = "";

  csSetMediaPreview("");
  csSelectedMentions = [];
  csRenderMentionComposer();
}

function csSetComposeExpanded(expanded) {
  const compose = document.querySelector(".cs-compose");
  if (!compose) return;

  compose.classList.toggle("is-compact", !expanded);
}

function csCloseCompose(options = {}) {
  const discard = options.discard === true;

  if (csComposeHasDraft() && !discard) {
    return false;
  }

  if (discard) {
    csClearComposeDraft();
  }

  csSetComposeExpanded(false);
  return true;
}

function csEnsureComposeCloseButton() {
  const compose = document.querySelector(".cs-compose");
  if (!compose || document.getElementById("csComposeClose")) return;

  const close = document.createElement("button");
  close.id = "csComposeClose";
  close.type = "button";
  close.textContent = "×";
  close.title = "Close composer";
  close.setAttribute("aria-label", "Close composer");

  close.addEventListener("click", () => {
    if (csComposeHasDraft()) {
      const ok = confirm("Discard this post draft?");
      if (!ok) return;
      csCloseCompose({ discard: true });
      return;
    }

    csCloseCompose();
  });

  compose.appendChild(close);
}

function csInitCompactCompose() {
  const compose = document.querySelector(".cs-compose");
  const textEl = document.getElementById("csText");
  const mediaEl = document.getElementById("csMediaPath");

  if (!compose || !textEl) return;

  csEnsureComposeCloseButton();
  csSetComposeExpanded(csComposeHasDraft());

  textEl.addEventListener("focus", () => {
    csSetComposeExpanded(true);
  });

  textEl.addEventListener("click", () => {
    csSetComposeExpanded(true);
  });

  textEl.addEventListener("input", () => {
    if (csComposeHasDraft()) csSetComposeExpanded(true);
  });

  if (mediaEl) {
    mediaEl.addEventListener("input", () => {
      if (csComposeHasDraft()) csSetComposeExpanded(true);
    });
  }

  document.addEventListener("click", (ev) => {
    const activeCompose = document.querySelector(".cs-compose:not(.is-compact)");
    if (!activeCompose) return;

    if (ev.target.closest(".cs-compose")) return;
    if (ev.target.closest(".cs-modal-backdrop")) return;
    if (ev.target.closest(".cs-lightbox")) return;

    csCloseCompose();
  });

  document.addEventListener("keydown", (ev) => {
    if (ev.key !== "Escape") return;

    const activeCompose = document.querySelector(".cs-compose:not(.is-compact)");
    if (!activeCompose) return;

    csCloseCompose();
  });
}


async function csCreatePost() {
  const textEl = document.getElementById("csText");
  const mediaEl = document.getElementById("csMediaPath");

  const text = textEl ? textEl.value.trim() : "";
  const media_path = mediaEl ? mediaEl.value.trim() : "";

  if (!text && !media_path) return;

  const mentions = csSelectedMentions.map(p => p.fingerprint).filter(Boolean);

  await fetch(`${CS_API}/posts/create`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ text, media_path, mentions })
  });

  csClearComposeDraft();
  csSetComposeExpanded(false);

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

  const mentionBtn = document.getElementById("csAddMentionBtn");
  if (mentionBtn) {
    mentionBtn.addEventListener("click", () => {
      csSetComposeExpanded(true);
      csOpenMentionPicker();
    });
  }

  csRenderMentionComposer();
  csInitCompactCompose();

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
  csSetComposeExpanded(true);

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
  const img = ev.target.closest(".cs-compose-preview-img, .cs-post-media, .cs-reply-media");
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
  const peopleRes = await fetch("/api/v4/circlestack/people", { credentials: "same-origin" });
  const peopleData = await peopleRes.json();

  const usersRes = await fetch("/api/v4/circlestack/users", { credentials: "same-origin" });
  const usersData = await usersRes.json();
  const usersByFp = new Map((usersData.users || []).map(u => [u.fingerprint, u]));

  const users = (peopleData.items || [])
    .map(p => usersByFp.get(p.fp))
    .filter(u => u && u.fingerprint && u.role !== "external");

  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal cs-intro-modal";

  modal.innerHTML = `
    <div class="cs-modal-title">Introduce people</div>
    <div class="cs-modal-text">Pick two people you know.</div>
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
    const label = csUserLabel(u.fingerprint, usersByFp);

    const optA = document.createElement("option");
    optA.value = u.fingerprint;
    optA.textContent = label;
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
      body: JSON.stringify({ person_a_fp: a, person_b_fp: b, message: msg })
    });

    close();
    csLoadIntroductions();
  };

  backdrop.appendChild(modal);
  document.body.appendChild(backdrop);
}

document.getElementById("csIntroduceBtn")
  ?.addEventListener("click", csOpenIntroduceModal);


function csRemoveLegacyIntroductionsPanel() {
  document.getElementById("csIntroductions")?.remove();
}

async function csLoadIntroductions() {
  // Introductions are notifications/actions, not feed posts.
  // Keep this function as a compatibility refresh hook for existing callers.
  csRemoveLegacyIntroductionsPanel();
}

document.addEventListener("DOMContentLoaded", csRemoveLegacyIntroductionsPanel);


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

const peopleRes = await fetch("/api/v4/circlestack/people", { credentials: "same-origin" });
const peopleData = await peopleRes.json();

const merged = new Map();

// circle ensin (vahvempi)
for (const it of items) {
  merged.set(it.fp, { fp: it.fp, source: "circle" });
}

// sitten people
for (const it of (peopleData.items || [])) {
  if (!merged.has(it.fp)) {
    merged.set(it.fp, it);
  }
}

const list = Array.from(merged.values());


  if (!items.length) {
    body.innerHTML = `<div class="cs-empty">Your circle is empty.</div>`;
  } else {
    for (const it of items) {
      const row = document.createElement("div");
      row.className = "cs-circle-row";
      
const name = csUserLabel(it.fp, usersByFp);

const badge =
    it.source === "circle" ? "Circle" :
        it.source === "manual" ? "Manual" :
            "Workspace";

row.innerHTML = `
  <span>${name}</span>
  <span class="cs-badge">${badge}</span>
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

async function csOpenFindPeople() {
  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal cs-intro-modal";

  modal.innerHTML = `
    <div class="cs-modal-title">Find people</div>
    <div class="cs-modal-text">Search users and send contact requests.</div>
    <div class="cs-modal-title" style="font-size:16px;margin-top:12px">Requests</div>
    <div id="csContactRequests"></div>
    <input id="csFindInput" placeholder="Search users..." />
    <div id="csFindResults"></div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel">Close</button>
    </div>
  `;

  const input = modal.querySelector("#csFindInput");
  const results = modal.querySelector("#csFindResults");
  const requestsBox = modal.querySelector("#csContactRequests");

  async function loadRequests() {
    const [notificationsRes, requestsRes] = await Promise.all([
      fetch("/api/v4/circlestack/notifications", { credentials: "same-origin" }),
      fetch("/api/v4/circlestack/contact/requests", { credentials: "same-origin" })
    ]);

    const notificationsData = await notificationsRes.json();
    const requestsData = await requestsRes.json();

    requestsBox.innerHTML = "";

    const notifications = Array.isArray(notificationsData.items)
      ? notificationsData.items
      : [];

    const outgoing = (requestsData.outgoing || []).filter(r => r.status === "pending");

    if (!notifications.length && !outgoing.length) {
      requestsBox.innerHTML = `<div class="cs-search-hint">No pending requests</div>`;
      return;
    }

    for (const n of notifications) {
      const row = document.createElement("div");
      row.className = "cs-search-row cs-notification-row";

      const label = document.createElement("span");
      label.className = "cs-notification-label";

      if (n.type === "contact_request") {
        label.textContent = `Contact request: ${n.from_display_name || csElideFp(n.from_fp)}`;

        const accept = document.createElement("button");
        accept.type = "button";
        accept.className = "cs-mini-action cs-mini-action-primary";
        accept.textContent = "Accept";

        const reject = document.createElement("button");
        reject.type = "button";
        reject.className = "cs-mini-action";
        reject.textContent = "Reject";

        accept.onclick = async () => {
          await fetch("/api/v4/circlestack/contact/respond", {
            method: "POST",
            credentials: "same-origin",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ id: n.id, action: "accept" })
          });

          await loadRequests();
          await csUpdateFindPeopleBadge();
          await csLoadIntroductions();
        };

        reject.onclick = async () => {
          await fetch("/api/v4/circlestack/contact/respond", {
            method: "POST",
            credentials: "same-origin",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ id: n.id, action: "reject" })
          });

          await loadRequests();
          await csUpdateFindPeopleBadge();
        };

        row.appendChild(label);
        row.appendChild(accept);
        row.appendChild(reject);
      } else if (n.type === "introduction") {
        label.textContent = "";

        const title = document.createElement("div");
        title.className = "cs-notification-title";
        title.textContent =
          `Introduction: ${n.introducer_display_name || csElideFp(n.introducer_fp)} introduced you to ${n.other_display_name || csElideFp(n.other_fp)}`;
        label.appendChild(title);

        const msg = String(n.message || "").trim();
        if (msg) {
          const msgEl = document.createElement("div");
          msgEl.className = "cs-notification-message";
          msgEl.textContent = `"${msg}"`;
          label.appendChild(msgEl);
        }

        const accept = document.createElement("button");
        accept.type = "button";
        accept.className = "cs-mini-action cs-mini-action-primary";
        accept.textContent = "Accept";

        const dismiss = document.createElement("button");
        dismiss.type = "button";
        dismiss.className = "cs-mini-action";
        dismiss.textContent = "Dismiss";

        accept.onclick = async () => {
          await fetch("/api/v4/circlestack/introductions/respond", {
            method: "POST",
            credentials: "same-origin",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ id: n.id, action: "accept" })
          });

          await loadRequests();
          await csUpdateFindPeopleBadge();
          await csLoadIntroductions();
        };

        dismiss.onclick = async () => {
          await fetch("/api/v4/circlestack/introductions/respond", {
            method: "POST",
            credentials: "same-origin",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ id: n.id, action: "dismiss" })
          });

          await loadRequests();
          await csUpdateFindPeopleBadge();
          await csLoadIntroductions();
        };

        row.appendChild(label);
        row.appendChild(accept);
        row.appendChild(dismiss);
      } else {
        label.textContent = "Notification";
        row.appendChild(label);
      }

      requestsBox.appendChild(row);
    }

    for (const r of outgoing) {
      const row = document.createElement("div");
      row.className = "cs-search-row cs-notification-row";

      const label = document.createElement("span");
      label.className = "cs-notification-label";
      label.textContent = `Outgoing: ${csElideFp(r.to_fp)}`;

      const status = document.createElement("span");
      status.textContent = "Pending";

      row.appendChild(label);
      row.appendChild(status);
      requestsBox.appendChild(row);
    }
  }

  loadRequests();
  let timer = null;

  input.oninput = () => {
    const q = input.value.trim();
    clearTimeout(timer);

    if (q.length < 2) {
      results.innerHTML = `<div class="cs-search-hint">Type at least 2 characters</div>`;
      return;
    }

    timer = setTimeout(async () => {
      const res = await fetch(`/api/v4/circlestack/search_users?q=${encodeURIComponent(q)}`, {
        credentials: "same-origin"
      });
      const data = await res.json();

      results.innerHTML = "";

      for (const u of data.users || []) {
        const row = document.createElement("div");
        row.className = "cs-search-row cs-find-row";

        const name = u.name || u.fp_short || u.fingerprint.slice(0, 8);

        row.innerHTML = `
          <span>${name}</span>
          <button type="button">Send request</button>
        `;

        row.querySelector("button").onclick = async () => {
          await fetch("/api/v4/circlestack/people/add", {
            method: "POST",
            credentials: "same-origin",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ fp: u.fingerprint })
          });

          row.innerHTML = `<span>${name}</span><span>✓ Request sent</span>`;
          await loadRequests();
        };

        results.appendChild(row);
      }
    }, 250);
  };

  modal.querySelector(".cs-modal-cancel").onclick = () => backdrop.remove();

  backdrop.appendChild(modal);
  document.body.appendChild(backdrop);
  input.focus();
}


async function csUpdateFindPeopleBadge() {
  const btn = document.getElementById("csFindPeopleBtn");
  if (!btn) return;

  btn.querySelector(".cs-badge-dot")?.remove();
  btn.removeAttribute("title");

  try {
    const res = await fetch("/api/v4/circlestack/notifications", {
      credentials: "same-origin"
    });

    if (!res.ok) return;

    const data = await res.json();
    const items = Array.isArray(data.items) ? data.items : [];
    const count = Number.isFinite(data.count) ? data.count : items.length;

    if (count > 0) {
      const dot = document.createElement("span");
      dot.className = "cs-badge-dot";
      dot.textContent = count > 9 ? "9+" : String(count);
      btn.title = count === 1 ? "1 pending notification" : `${count} pending notifications`;
      btn.appendChild(dot);
    }
  } catch (_) {
    // Badge is best-effort. Do not break Circle Stack if notifications fail.
  }
}

csUpdateFindPeopleBadge();

document.getElementById("csFindPeopleBtn")
  ?.addEventListener("click", csOpenFindPeople);
