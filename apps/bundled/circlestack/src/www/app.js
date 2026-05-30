const CS_API = "/api/v4/circlestack";
const CS_REACTIONS = ["👍", "❤️", "😂", "😮", "👏", "🔥"];

// FEDERATED_LOCAL_REACTION_RENDER_V2
// Server-side federated local reaction state lives in FEDERATED_LOCAL_REACTIONS_SERVER_UI_V1.
// Keep only rendering helpers here; persistence/cache helpers are defined below.

function csRenderFederatedLocalReaction(ev) {
  if (!ev || ev.event_type !== "circle.post.created") return null;

  const item = csFederatedLocalReactionFor(ev);
  if (!item || !item.reaction) return null;

  const row = document.createElement("div");
  row.className = "cs-federated-local-reaction";
  row.title = item.federation_event_id
    ? `Queued as ${item.federation_event_id}`
    : "Your federated reaction was queued";

  const chip = document.createElement("span");
  chip.className = "cs-federated-local-reaction-chip";
  chip.textContent = `${item.reaction} 1`;

  const label = document.createElement("span");
  label.className = "cs-federated-local-reaction-label";
  label.textContent = `You ${item.reaction}`;

  row.appendChild(chip);
  row.appendChild(label);
  return row;
}

function csRefreshFederatedLocalReactionInCard(card, ev) {
  if (!card) return;

  const old = card.querySelector(".cs-federated-local-reaction");
  if (old) old.remove();

  const next = csRenderFederatedLocalReaction(ev);
  if (!next) return;

  const meta = card.querySelector(".cs-post-meta");
  if (meta) {
    card.insertBefore(next, meta);
  } else {
    card.appendChild(next);
  }
}
let csSelectedMentions = [];

function csAchievementListFrom(value) {
  return Array.isArray(value)
    ? value.filter(b => b && typeof b === "object" && b.id && b.title)
    : [];
}

const CS_BADGE_ICON_ASSETS = {
  "account.node_steward": "badges/node-steward.svg",
  "account.established_signal": "badges/established-signal.svg",
  "account.old_guard": "badges/old-guard.svg",
  "account.legacy_node": "badges/legacy-node.svg",

  "circlestack.first_signal": "badges/first-signal.svg",
  "circlestack.signal_sender": "badges/signal-sender.svg",
  "circlestack.broadcast_node": "badges/broadcast-node.svg",
  "circlestack.anchor_voice": "badges/anchor-voice.svg",
  "circlestack.public_voice": "badges/public-voice.svg",
  "circlestack.media_runner": "badges/media-runner.svg",
  "circlestack.conversation_spark": "badges/conversation-spark.svg",
  "circlestack.signal_amplifier": "badges/signal-amplifier.svg",
  "circlestack.crowd_spark": "badges/crowd-spark.svg",
  "circlestack.thread_starter": "badges/thread-starter.svg",
  "circlestack.circle_builder": "badges/circle-builder.svg",

  "shares.first_share": "badges/first-share.svg",
  "shares.packet_runner": "badges/packet-runner.svg",
  "shares.distribution_node": "badges/distribution-node.svg",

  "storage.data_seed": "badges/data-seed.svg",
  "storage.vault_keeper": "badges/vault-keeper.svg",
  "storage.keeper_500gb": "badges/keeper-500gb.svg",
  "storage.terabyte_guardian": "badges/terabyte-guardian.svg",

  "dropzone.operator": "badges/dropzone-operator.svg",
  "dropzone.gatekeeper": "badges/gatekeeper.svg",

  "security.trusted_device": "badges/trusted-device.svg",

  "echostack.first_archive": "badges/echo-first-archive.svg",
  "echostack.web_preserver": "badges/echo-web-preserver.svg",
  "echostack.memory_vault": "badges/echo-memory-vault.svg",
  "echostack.deep_archive": "badges/echo-deep-archive.svg",

  "media.first_snapshot": "badges/media-first-snapshot.svg",
  "media.memory_keeper": "badges/media-memory-keeper.svg",
  "media.gallery_curator": "badges/media-gallery-curator.svg",

  "media.first_reel": "badges/media-first-reel.svg",
  "media.video_vault": "badges/media-video-vault.svg",
  "media.cinema_keeper": "badges/media-cinema-keeper.svg",

  "media.first_track": "badges/media-first-track.svg",
  "media.signal_dj": "badges/media-signal-dj.svg",
  "media.sound_vault": "badges/media-sound-vault.svg",

  "federation.pioneer": "badges/federation-pioneer.svg",
  "federation.signal_courier": "badges/federation-signal-courier.svg",
  "federation.first_remote_signal": "badges/federation-first-remote-signal.svg",
  "federation.cross_node_conversation": "badges/federation-cross-node-conversation.svg",
  "federation.known_origin": "badges/federation-known-origin.svg",
  "federation.bridge_builder": "badges/federation-bridge-builder.svg"
};

function csSafeLocalBadgeAsset(raw) {
  const value = String(raw || "").trim();
  if (!value) return "";
  if (!value.startsWith("badges/")) return "";
  if (value.includes("..") || value.includes(":") || value.includes("\\")) return "";
  if (!/^[A-Za-z0-9_./-]+\.svg$/.test(value)) return "";
  return value;
}

function csBadgeIconAsset(badge) {
  const id = String((badge && badge.id) || "");
  return CS_BADGE_ICON_ASSETS[id] || csSafeLocalBadgeAsset(badge && badge.icon_asset);
}

function csCreateBadgeIconElement(badge, className, options = {}) {
  const asset = csBadgeIconAsset(badge);

  if (asset) {
    const img = document.createElement("img");
    img.className = className || "cs-badge-svg-icon";
    img.src = asset;
    img.alt = "";
    img.loading = "eager";
    img.decoding = "sync";
    img.fetchPriority = "high";

    return img;
  }

  const span = document.createElement("span");
  span.className = className || "cs-badge-emoji-icon";
  span.textContent = badge && badge.icon ? badge.icon : "◆";
  return span;
}

function csPreloadBadgeIconAssets(rawBadges) {
  const badges = csAchievementListFrom(rawBadges);
  const assets = Array.from(new Set(
    badges.map(b => csBadgeIconAsset(b)).filter(Boolean)
  ));

  if (!assets.length) return Promise.resolve();

  return Promise.allSettled(assets.map(asset => new Promise(resolve => {
    const img = new Image();
    img.onload = resolve;
    img.onerror = resolve;
    img.src = asset;

    if (img.decode) {
      img.decode().then(resolve).catch(resolve);
    }
  })));
}

function csRenderAchievementStrip(rawBadges, options = {}) {
  const badges = csAchievementListFrom(rawBadges);
  if (!badges.length) return null;

  const max = Number(options.max || 4);
  const wrap = document.createElement("div");
  wrap.className = options.profile
    ? "cs-achievement-strip cs-achievement-strip-profile"
    : "cs-achievement-strip";

  for (const badge of badges.slice(0, max)) {
    const chip = document.createElement("span");
    chip.className = "cs-achievement-chip";
    if (badge.tier) chip.classList.add(`tier-${String(badge.tier).toLowerCase()}`);
    chip.title = badge.description || badge.title || "";

    const chipIcon = csCreateBadgeIconElement(badge, "cs-achievement-chip-icon");
    const chipTitle = document.createElement("span");
    chipTitle.className = "cs-achievement-chip-title";
    chipTitle.textContent = badge.title || "Badge";

    chip.appendChild(chipIcon);
    chip.appendChild(chipTitle);
    wrap.appendChild(chip);
  }

  if (badges.length > max) {
    const more = document.createElement("span");
    more.className = "cs-achievement-more";
    more.textContent = `+${badges.length - max}`;
    wrap.appendChild(more);
  }

  return wrap;
}


function csAchievementCategoryLabel(category) {
  const raw = String(category || "").trim();
  if (!raw) return "";
  const key = raw.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "");
  return csT(`profile.achievementCategory.${key}`, raw);
}

function csAchievementTierLabel(tier) {
  const raw = String(tier || "").trim();
  if (!raw) return "";
  const key = raw.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "");
  return csT(`profile.achievementTier.${key}`, raw);
}

function csAchievementMetaLabel(badge) {
  if (!badge) return "";
  const category = csAchievementCategoryLabel(badge.category || "");
  const tier = csAchievementTierLabel(badge.tier || "");
  if (category && tier) return `${category} · ${tier}`;
  return category || tier || "";
}


function csAchievementSlugFromBadge(badge) {
  const normalize = (value) => String(value || "")
    .trim()
    .toLowerCase()
    .replace(/&/g, " and ")
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "");

  const direct = {
    node_steward: "nodeSteward",
    steward: "nodeSteward",
    first_signal: "firstSignal",
    signal: "firstSignal",
    first_share: "firstShare",
    share: "firstShare",
    drop_zone_operator: "dropZoneOperator",
    dropzone_operator: "dropZoneOperator",
    trusted_device: "trustedDevice",
    first_snapshot: "firstSnapshot",
    snapshot: "firstSnapshot",
    first_reel: "firstReel",
    reel: "firstReel",
    federation_pioneer: "federationPioneer",
    signal_courier: "signalCourier",
    deep_archive: "deepArchive",
    first_remote_signal: "firstRemoteSignal",
    federation_known_origin: "knownOrigin",
    known_origin: "knownOrigin",
    bridge_builder: "bridgeBuilder"
  };

  const candidates = [
    badge && badge.id,
    badge && badge.badge_id,
    badge && badge.achievement_id,
    badge && badge.key,
    badge && badge.slug,
    badge && badge.icon_key,
    badge && badge.title,
    badge && badge.name
  ].map(normalize).filter(Boolean);

  for (const c of candidates) {
    if (direct[c]) return direct[c];

    for (const [needle, slug] of Object.entries(direct)) {
      if (c === needle || c.endsWith(`_${needle}`) || c.includes(needle)) {
        return slug;
      }
    }
  }

  return "";
}



function csAchievementLockedTextKeyFromFallback(text) {
  const raw = String(text || "").trim();

  const direct = {
    "Account has existed for at least 100 days.": "account100Days",
    "Account has existed for at least 500 days.": "account500Days",
    "Account has existed for at least 1000 days.": "account1000Days",
    "Create your first Circle Stack post.": "firstSignal",
    "Keep contributing meaningful Circle Stack posts over time.": "steadySignal",
    "Become a steady voice in your Circle Stack community.": "circleVoice",
    "Build a long-term posting history.": "longTermPoster",
    "Share useful public posts over time.": "publicPoster",
    "Share media-rich posts over time.": "mediaPoster",
    "Take part in discussions.": "discussionParticipant",
    "React to posts and help surface useful content.": "reactionGiver",
    "Create posts that others respond to.": "responseMagnet",
    "Start conversations that receive replies.": "conversationStarter",
    "Build trusted Circle connections.": "circleConnector",
    "Create your first share link.": "firstShare",
    "Share files with others over time.": "fileSharer",
    "Become a long-term sharing hub.": "sharingHub",
    "Start building your personal data vault.": "dataVaultStarter",
    "Keep growing your stored data over time.": "dataVaultBuilder",
    "Maintain a large personal data vault.": "largeDataVault",
    "Build a serious long-term storage archive.": "storageArchivist",
    "Create your first Drop Zone.": "dropZoneOperator",
    "Receive uploads through Drop Zone over time.": "dropZoneReceiver",
    "Pair your first trusted device.": "trustedDevice",
    "Archive your first web page in Echo Stack.": "echoStackFirstArchive",
    "Preserve useful pages over time.": "echoStackKeeper",
    "Build a serious personal web memory vault.": "echoStackVault",
    "Store larger archived web snapshots over time.": "deepArchive",
    "Upload your first photo.": "firstSnapshot",
    "Build your photo memory collection over time.": "photoCollector",
    "Create a serious personal photo library.": "photoLibrary",
    "Upload your first video.": "firstReel",
    "Build your video collection over time.": "videoCollector",
    "Create a serious self-hosted video library.": "videoLibrary",
    "Upload your first audio track.": "firstTrack",
    "Build your music/audio collection over time.": "audioCollector",
    "Create a serious self-hosted sound vault.": "soundVault",
    "Publish your first public Circle Stack post through federation.": "federationPioneer",
    "Keep sending meaningful signals through federation.": "signalCourier",
    "Discover or add your first remote NAS origin.": "knownOrigin",
    "Build bridges with several remote NAS origins.": "bridgeBuilder"
  };

  return direct[raw] || "";
}

function csAchievementLockedText(badge) {
  const fallback = String((badge && badge.locked_text) || "");
  const defaultText = csT(
    "profile.lockedAchievementDefaultDesc",
    "Keep using Circle Stack to discover this achievement."
  );

  const slug = csAchievementSlugFromBadge(badge);
  if (slug) {
    const missing = "__PQNAS_LOCKED_TEXT_MISSING__";
    const translated = csT(`profile.achDesc.${slug}.locked`, missing);
    if (translated !== missing) return translated;
  }

  const textKey = csAchievementLockedTextKeyFromFallback(fallback);
  if (textKey) {
    return csT(`profile.achLocked.${textKey}`, fallback || defaultText);
  }

  return fallback || defaultText;
}


function csAchievementReviewAllEnabled() {
  try {
    const params = new URLSearchParams(window.location.search || "");
    if (params.get("all_achievements") === "1") return true;
    return localStorage.getItem("circlestack.reviewAllAchievements") === "1";
  } catch (_) {
    return false;
  }
}

function csAchievementIdentity(badge) {
  return String(
    (badge && (
      badge.id ||
      badge.badge_id ||
      badge.achievement_id ||
      badge.key ||
      badge.slug ||
      badge.title ||
      badge.name
    )) || ""
  ).trim().toLowerCase();
}

function csAchievementReviewPromoteBadge(badge) {
  const copy = Object.assign({}, badge || {});
  copy.unlocked = true;
  copy.earned = true;
  copy.is_unlocked = true;
  copy.is_earned = true;
  copy.locked = false;
  copy.unlocked_epoch = copy.unlocked_epoch || Math.floor(Date.now() / 1000);
  copy.earned_epoch = copy.earned_epoch || copy.unlocked_epoch;
  copy.created_epoch = copy.created_epoch || copy.unlocked_epoch;
  copy.description = csAchievementDescriptionText(copy, "short") || copy.description || copy.desc || "";
  copy.long_description = csAchievementDescriptionText(copy, "long") || copy.long_description || copy.description || "";
  return copy;
}

function csAchievementReviewMergeUnlocked(earned, locked) {
  const out = Array.isArray(earned) ? earned.map(csAchievementReviewPromoteBadge) : [];
  const seen = new Set(out.map(csAchievementIdentity).filter(Boolean));

  for (const badge of (Array.isArray(locked) ? locked : [])) {
    const id = csAchievementIdentity(badge);
    if (id && seen.has(id)) continue;
    out.push(csAchievementReviewPromoteBadge(badge));
    if (id) seen.add(id);
  }

  return out;
}

function csAchievementReviewMaybeHideLockedSection(el) {
  if (!csAchievementReviewAllEnabled()) return;
  if (el && el.style) el.style.display = "none";
}



function csAchievementShortText(badge) {
  const id = String((badge && badge.id) || "");

  const texts = {
    "account.node_steward":
      csT("profile.achShort.account.nodeSteward", "Admin or steward of this DNA-Nexus node."),

    "account.established_signal":
      csT("profile.achShort.account.establishedSignal", "Account has existed for at least 100 days."),

    "account.old_guard":
      csT("profile.achShort.account.oldGuard", "Account has existed for at least 500 days."),

    "account.legacy_node":
      csT("profile.achShort.account.legacyNode", "Account has existed for at least 1000 days."),

    "circlestack.first_signal":
      csT("profile.achShort.circlestack.firstSignal", "Created the first Circle Stack post."),

    "circlestack.signal_sender":
      csT("profile.achShort.circlestack.signalSender", "Keep contributing meaningful Circle Stack posts over time."),

    "circlestack.broadcast_node":
      csT("profile.achShort.circlestack.broadcastNode", "Become a steady voice in your Circle Stack community."),

    "circlestack.anchor_voice":
      csT("profile.achShort.circlestack.anchorVoice", "Build a long-term posting history."),

    "circlestack.public_voice":
      csT("profile.achShort.circlestack.publicVoice", "Share useful public posts over time."),

    "circlestack.media_runner":
      csT("profile.achShort.circlestack.mediaRunner", "Share media-rich posts over time."),

    "circlestack.conversation_spark":
      csT("profile.achShort.circlestack.conversationSpark", "Take part in discussions."),

    "circlestack.signal_amplifier":
      csT("profile.achShort.circlestack.signalAmplifier", "React to posts and help surface useful content."),

    "circlestack.crowd_spark":
      csT("profile.achShort.circlestack.crowdSpark", "Create posts that others respond to."),

    "circlestack.thread_starter":
      csT("profile.achShort.circlestack.threadStarter", "Start conversations that receive replies."),

    "circlestack.circle_builder":
      csT("profile.achShort.circlestack.circleBuilder", "Build trusted Circle connections."),

    "shares.first_share":
      csT("profile.achShort.shares.firstShare", "Created the first share link."),

    "shares.packet_runner":
      csT("profile.achShort.shares.packetRunner", "Share files with others over time."),

    "shares.distribution_node":
      csT("profile.achShort.shares.distributionNode", "Become a long-term sharing hub."),

    "storage.data_seed":
      csT("profile.achShort.storage.dataSeed", "Start building your personal data vault."),

    "storage.vault_keeper":
      csT("profile.achShort.storage.vaultKeeper", "Keep growing your stored data over time."),

    "storage.keeper_500gb":
      csT("profile.achShort.storage.keeper500gb", "Maintain a large personal data vault."),

    "storage.terabyte_guardian":
      csT("profile.achShort.storage.terabyteGuardian", "Build a serious long-term storage archive."),

    "dropzone.operator":
      csT("profile.achShort.dropzone.operator", "Created the first Drop Zone."),

    "dropzone.gatekeeper":
      csT("profile.achShort.dropzone.gatekeeper", "Receive uploads through Drop Zone over time."),

    "security.trusted_device":
      csT("profile.achShort.security.trustedDevice", "Paired the first trusted device."),

    "echostack.first_archive":
      csT("profile.achShort.echostack.firstArchive", "Archive your first web page in Echo Stack."),

    "echostack.web_preserver":
      csT("profile.achShort.echostack.webPreserver", "Preserve useful pages over time."),

    "echostack.memory_vault":
      csT("profile.achShort.echostack.memoryVault", "Build a serious personal web memory vault."),

    "echostack.deep_archive":
      csT("profile.achShort.echostack.deepArchive", "Store larger archived web snapshots over time."),

    "media.first_snapshot":
      csT("profile.achShort.media.firstSnapshot", "Uploaded the first photo."),

    "media.memory_keeper":
      csT("profile.achShort.media.memoryKeeper", "Build your photo memory collection over time."),

    "media.gallery_curator":
      csT("profile.achShort.media.galleryCurator", "Create a serious personal photo library."),

    "media.first_reel":
      csT("profile.achShort.media.firstReel", "Uploaded the first video."),

    "media.video_vault":
      csT("profile.achShort.media.videoVault", "Build your video collection over time."),

    "media.cinema_keeper":
      csT("profile.achShort.media.cinemaKeeper", "Create a serious self-hosted video library."),

    "media.first_track":
      csT("profile.achShort.media.firstTrack", "Upload your first audio track."),

    "media.signal_dj":
      csT("profile.achShort.media.signalDj", "Build your music/audio collection over time."),

    "media.sound_vault":
      csT("profile.achShort.media.soundVault", "Create a serious self-hosted sound vault."),

    "federation.pioneer":
      csT("profile.achShort.federation.pioneer", "Published the first local public Circle Stack post through federation."),

    "federation.signal_courier":
      csT("profile.achShort.federation.signalCourier", "Delivered several local Circle Stack signals through federation."),

    "federation.first_remote_signal":
      csT("profile.achShort.federation.firstRemoteSignal", "Received the first remote federation signal."),

    "federation.cross_node_conversation":
      csT("profile.achShort.federation.crossNodeConversation", "Created a cross-node conversation."),

    "federation.known_origin":
      csT("profile.achShort.federation.knownOrigin", "Added or discovered a remote NAS origin."),

    "federation.bridge_builder":
      csT("profile.achShort.federation.bridgeBuilder", "Connected this NAS with several remote origins.")
  };

  return texts[id] || "";
}

function csAchievementDescriptionText(badge, mode = "short") {
  if (mode !== "long") {
    const shortText = csAchievementShortText(badge);
    if (shortText) return shortText;
  }

  const fallback = String(
    (badge && (
      (mode === "long" && (badge.long_description || badge.detail || badge.details || badge.full_description)) ||
      badge.description ||
      badge.desc ||
      badge.long_description ||
      badge.locked_text ||
      badge.text
    )) ||
    ""
  );

  const suffix = mode === "long" ? "long" : "short";

  const slug = csAchievementSlugFromBadge(badge);
  if (slug) {
    const missing = "__PQNAS_ACH_DESC_MISSING__";
    const translated = csT(`profile.achDesc.${slug}.${suffix}`, missing);
    if (translated !== missing) return translated;
  }

  const exact = {
    "Admin or steward of this DNA-Nexus node.": "nodeSteward",
    "Created the first Circle Stack post.": "firstSignal",
    "Created the first share link.": "firstShare",
    "Created the first Drop Zone.": "dropZoneOperator",
    "Paired the first trusted device.": "trustedDevice",
    "Uploaded the first photo.": "firstSnapshot",
    "Uploaded the first video.": "firstReel",
    "Published the first local public Circle Stack post through federation.": "federationPioneer",
    "Delivered several local Circle Stack signals through federation.": "signalCourier",

    "You are an admin or steward of this DNA-Nexus node.": "nodeSteward",
    "You created your first Circle Stack post.": "firstSignal",
    "You created your first share link. Sharing is one of the core NAS powers: letting someone access exactly what you choose, when you choose.": "firstShare",
    "You created your first Drop Zone.": "dropZoneOperator",
    "You paired your first trusted device.": "trustedDevice",
    "You uploaded your first photo.": "firstSnapshot",
    "You uploaded your first video.": "firstReel",
    "Every network starts with one signal. This badge marks the moment your Circle Stack identity stopped being empty and started becoming part of the feed.": "firstSignal",
    "First shares are how a NAS starts reaching outside itself. This badge marks the moment you created your first controlled access path.": "firstShare",
    "Drop Zone starts with one open door, carefully controlled. This badge marks the moment you created your first outside upload path.": "dropZoneOperator",
    "Trusted devices turn secure access into something smoother. This badge marks the first device you paired with your account.": "trustedDevice",
    "One photo is the beginning of a memory library. This badge marks the first image you uploaded into your self-hosted collection.": "firstSnapshot",
    "You uploaded your first video. Reel Stack starts with one clip, but grows into a self-hosted video vault over time.": "firstReel",
    "You uploaded your first photo. Photo Gallery starts with one image, but grows into a personal memory library over time.": "firstSnapshot",
    "You created your first Drop Zone. Drop Zone lets outsiders send files inward without giving them full NAS access.": "dropZoneOperator",
    "You paired your first trusted device. Trusted devices make secure access easier while keeping control tied to your account.": "trustedDevice",
    "You created your first Circle Stack post. Circle Stack starts with one signal, then grows into a private social layer around your NAS.": "firstSignal",
    "You are an admin or steward of this DNA-Nexus node. Stewardship means keeping the node useful, safe, and alive.": "nodeSteward",
    "You published the first local public Circle Stack post through federation.": "federationPioneer",
    "You delivered several local Circle Stack signals through federation.": "signalCourier",

    "You are storing heavier web snapshots locally. Deep Archive is for users who want pages to remain available even when the original web changes.": "deepArchive",
    "Your node heard another NAS. First Remote Signal means the federation is no longer theoretical — another origin reached your Circle Stack.": "firstRemoteSignal",
    "You added or discovered another NAS origin. Known origins are the first step toward a real web of personal servers.": "knownOrigin",
    "You have helped connect this NAS with multiple origins. Bridge Builder is about making the wider DNA-Nexus network more discoverable.": "bridgeBuilder"
  };

  const exactSlug = exact[fallback] || "";
  if (exactSlug) {
    return csT(`profile.achDesc.${exactSlug}.${suffix}`, fallback);
  }

  return fallback;
}


function csRenderAchievementProfileBlock(rawBadges) {
  let badges = csAchievementListFrom(rawBadges);
  if (csAchievementReviewAllEnabled()) {
    badges = csAchievementReviewMergeUnlocked(badges, csAllAchievementPlaceholders());
  }
  if (!badges.length) return null;

  const section = document.createElement("div");
  section.className = "cs-profile-achievements";

  const label = document.createElement("div");
  label.className = "cs-profile-label";
  label.textContent = csT("profile.achievements", "Achievements");
  section.appendChild(label);

  const grid = document.createElement("div");
  grid.className = "cs-profile-achievement-grid";

  for (const badge of badges) {
    const item = document.createElement("div");
    item.className = "cs-profile-achievement";
    if (badge.tier) item.classList.add(`tier-${String(badge.tier).toLowerCase()}`);

    const icon = csCreateBadgeIconElement(badge, "cs-profile-achievement-icon");

    const body = document.createElement("span");
    body.className = "cs-profile-achievement-body";

    const title = document.createElement("span");
    title.className = "cs-profile-achievement-title";
    title.textContent = badge.title || "Badge";

    const desc = document.createElement("span");
    desc.className = "cs-profile-achievement-desc";
    desc.textContent = csAchievementDescriptionText(badge, "short");

    body.appendChild(title);
    if (desc.textContent) body.appendChild(desc);

    item.appendChild(icon);
    item.appendChild(body);

    item.classList.add("cs-profile-achievement-earned");
    item.setAttribute("role", "button");
    item.setAttribute("tabindex", "0");
    item.title = csT("profile.openAchievement", "Open achievement");

    const openAchievement = () => {
      if (typeof csShowAchievementUnlockedModal === "function") {
        csShowAchievementUnlockedModal(badge, { replay: true });
      }
    };

    item.addEventListener("click", openAchievement);
    item.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter" || ev.key === " ") {
        ev.preventDefault();
        openAchievement();
      }
    });

    grid.appendChild(item);
  }

  section.appendChild(grid);
  return section;
}

function csFederatedActorBadges(ev) {
  const payload = ev && ev.payload && typeof ev.payload === "object" ? ev.payload : {};
  return csAchievementListFrom(
    ev.owner_badges ||
    ev.actor_badges ||
    payload.owner_badges ||
    payload.actor_badges ||
    []
  );
}

function csAllAchievementPlaceholders() {
  return [
    {
      id: "account.established_signal",
      title: "Established Signal",
      category: "account",
      tier: "bronze",
      icon_key: "established-signal",
      icon_asset: "badges/established-signal.svg",
      locked_text: "Account has existed for at least 100 days.",
      disclosure: "exact"
    },
    {
      id: "account.old_guard",
      title: "Old Guard",
      category: "account",
      tier: "gold",
      icon_key: "old-guard",
      icon_asset: "badges/old-guard.svg",
      locked_text: "Account has existed for at least 500 days.",
      disclosure: "exact"
    },
    {
      id: "account.legacy_node",
      title: "Legacy Node",
      category: "account",
      tier: "legendary",
      icon_key: "legacy-node",
      icon_asset: "badges/legacy-node.svg",
      locked_text: "Account has existed for at least 1000 days.",
      disclosure: "exact"
    },
    {
      id: "circlestack.first_signal",
      title: "First Signal",
      category: "circlestack",
      tier: "bronze",
      icon_key: "first-signal",
      icon_asset: "badges/first-signal.svg",
      locked_text: "Create your first Circle Stack post.",
      disclosure: "exact"
    },

    {
      id: "circlestack.signal_sender",
      title: "Signal Sender",
      category: "circlestack",
      tier: "silver",
      icon_key: "signal-sender",
      icon_asset: "badges/signal-sender.svg",
      locked_text: "Keep contributing meaningful Circle Stack posts over time.",
      disclosure: "hint"
    },
    {
      id: "circlestack.broadcast_node",
      title: "Broadcast Node",
      category: "circlestack",
      tier: "gold",
      icon_key: "broadcast-node",
      icon_asset: "badges/broadcast-node.svg",
      locked_text: "Become a steady voice in your Circle Stack community.",
      disclosure: "hint"
    },
    {
      id: "circlestack.anchor_voice",
      title: "Anchor Voice",
      category: "circlestack",
      tier: "legendary",
      icon_key: "anchor-voice",
      icon_asset: "badges/anchor-voice.svg",
      locked_text: "Build a long-term posting history.",
      disclosure: "hint"
    },
    {
      id: "circlestack.public_voice",
      title: "Public Voice",
      category: "circlestack",
      tier: "silver",
      icon_key: "public-voice",
      icon_asset: "badges/public-voice.svg",
      locked_text: "Share useful public posts over time.",
      disclosure: "hint"
    },
    {
      id: "circlestack.media_runner",
      title: "Media Runner",
      category: "circlestack",
      tier: "silver",
      icon_key: "media-runner",
      icon_asset: "badges/media-runner.svg",
      locked_text: "Share media-rich posts over time.",
      disclosure: "hint"
    },
    {
      id: "circlestack.conversation_spark",
      title: "Conversation Spark",
      category: "social",
      tier: "silver",
      icon_key: "conversation-spark",
      icon_asset: "badges/conversation-spark.svg",
      locked_text: "Take part in discussions.",
      disclosure: "hint"
    },
    {
      id: "circlestack.signal_amplifier",
      title: "Signal Amplifier",
      category: "social",
      tier: "silver",
      icon_key: "signal-amplifier",
      icon_asset: "badges/signal-amplifier.svg",
      locked_text: "React to posts and help surface useful content.",
      disclosure: "hint"
    },
    {
      id: "circlestack.crowd_spark",
      title: "Crowd Spark",
      category: "social",
      tier: "gold",
      icon_key: "crowd-spark",
      icon_asset: "badges/crowd-spark.svg",
      locked_text: "Create posts that others respond to.",
      disclosure: "hint"
    },
    {
      id: "circlestack.thread_starter",
      title: "Thread Starter",
      category: "social",
      tier: "gold",
      icon_key: "thread-starter",
      icon_asset: "badges/thread-starter.svg",
      locked_text: "Start conversations that receive replies.",
      disclosure: "hint"
    },
    {
      id: "circlestack.circle_builder",
      title: "Circle Builder",
      category: "social",
      tier: "silver",
      icon_key: "circle-builder",
      icon_asset: "badges/circle-builder.svg",
      locked_text: "Build trusted Circle connections.",
      disclosure: "hint"
    },

    {
      id: "shares.first_share",
      title: "First Share",
      category: "sharing",
      tier: "bronze",
      icon_key: "first-share",
      icon_asset: "badges/first-share.svg",
      locked_text: "Create your first share link.",
      disclosure: "exact"
    },
    {
      id: "shares.packet_runner",
      title: "Packet Runner",
      category: "sharing",
      tier: "silver",
      icon_key: "packet-runner",
      icon_asset: "badges/packet-runner.svg",
      locked_text: "Share files with others over time.",
      disclosure: "hint"
    },
    {
      id: "shares.distribution_node",
      title: "Distribution Node",
      category: "sharing",
      tier: "legendary",
      icon_key: "distribution-node",
      icon_asset: "badges/distribution-node.svg",
      locked_text: "Become a long-term sharing hub.",
      disclosure: "hint"
    },

    {
      id: "storage.data_seed",
      title: "Data Seed",
      category: "storage",
      tier: "bronze",
      icon_key: "data-seed",
      icon_asset: "badges/data-seed.svg",
      locked_text: "Start building your personal data vault.",
      disclosure: "hint"
    },
    {
      id: "storage.vault_keeper",
      title: "Vault Keeper",
      category: "storage",
      tier: "silver",
      icon_key: "vault-keeper",
      icon_asset: "badges/vault-keeper.svg",
      locked_text: "Keep growing your stored data over time.",
      disclosure: "hint"
    },
    {
      id: "storage.keeper_500gb",
      title: "500GB Keeper",
      category: "storage",
      tier: "gold",
      icon_key: "keeper-500gb",
      icon_asset: "badges/keeper-500gb.svg",
      locked_text: "Maintain a large personal data vault.",
      disclosure: "hint"
    },
    {
      id: "storage.terabyte_guardian",
      title: "Terabyte Guardian",
      category: "storage",
      tier: "legendary",
      icon_key: "terabyte-guardian",
      icon_asset: "badges/terabyte-guardian.svg",
      locked_text: "Build a serious long-term storage archive.",
      disclosure: "hint"
    },

    {
      id: "dropzone.operator",
      title: "Drop Zone Operator",
      category: "dropzone",
      tier: "bronze",
      icon_key: "dropzone-operator",
      icon_asset: "badges/dropzone-operator.svg",
      locked_text: "Create your first Drop Zone.",
      disclosure: "exact"
    },
    {
      id: "dropzone.gatekeeper",
      title: "Gatekeeper",
      category: "dropzone",
      tier: "gold",
      icon_key: "gatekeeper",
      icon_asset: "badges/gatekeeper.svg",
      locked_text: "Receive uploads through Drop Zone over time.",
      disclosure: "hint"
    },

    {
      id: "security.trusted_device",
      title: "Trusted Device",
      category: "security",
      tier: "bronze",
      icon_key: "trusted-device",
      icon_asset: "badges/trusted-device.svg",
      locked_text: "Pair your first trusted device.",
      disclosure: "exact"
    },

    {
      id: "echostack.first_archive",
      title: "First Archive",
      category: "echostack",
      tier: "bronze",
      icon_key: "echo-first-archive",
      icon_asset: "badges/echo-first-archive.svg",
      locked_text: "Archive your first web page in Echo Stack.",
      disclosure: "exact"
    },
    {
      id: "echostack.web_preserver",
      title: "Web Preserver",
      category: "echostack",
      tier: "silver",
      icon_key: "echo-web-preserver",
      icon_asset: "badges/echo-web-preserver.svg",
      locked_text: "Preserve useful pages over time.",
      disclosure: "hint"
    },
    {
      id: "echostack.memory_vault",
      title: "Memory Vault",
      category: "echostack",
      tier: "gold",
      icon_key: "echo-memory-vault",
      icon_asset: "badges/echo-memory-vault.svg",
      locked_text: "Build a serious personal web memory vault.",
      disclosure: "hint"
    },
    {
      id: "echostack.deep_archive",
      title: "Deep Archive",
      category: "echostack",
      tier: "gold",
      icon_key: "echo-deep-archive",
      icon_asset: "badges/echo-deep-archive.svg",
      locked_text: "Store larger archived web snapshots over time.",
      disclosure: "hint"
    },

    {
      id: "media.first_snapshot",
      title: "First Snapshot",
      category: "media",
      tier: "bronze",
      icon_key: "media-first-snapshot",
      icon_asset: "badges/media-first-snapshot.svg",
      locked_text: "Upload your first photo.",
      disclosure: "exact"
    },
    {
      id: "media.memory_keeper",
      title: "Memory Keeper",
      category: "media",
      tier: "silver",
      icon_key: "media-memory-keeper",
      icon_asset: "badges/media-memory-keeper.svg",
      locked_text: "Build your photo memory collection over time.",
      disclosure: "hint"
    },
    {
      id: "media.gallery_curator",
      title: "Gallery Curator",
      category: "media",
      tier: "gold",
      icon_key: "media-gallery-curator",
      icon_asset: "badges/media-gallery-curator.svg",
      locked_text: "Create a serious personal photo library.",
      disclosure: "hint"
    },

    {
      id: "media.first_reel",
      title: "First Reel",
      category: "media",
      tier: "bronze",
      icon_key: "media-first-reel",
      icon_asset: "badges/media-first-reel.svg",
      locked_text: "Upload your first video.",
      disclosure: "exact"
    },
    {
      id: "media.video_vault",
      title: "Video Vault",
      category: "media",
      tier: "silver",
      icon_key: "media-video-vault",
      icon_asset: "badges/media-video-vault.svg",
      locked_text: "Build your video collection over time.",
      disclosure: "hint"
    },
    {
      id: "media.cinema_keeper",
      title: "Cinema Keeper",
      category: "media",
      tier: "gold",
      icon_key: "media-cinema-keeper",
      icon_asset: "badges/media-cinema-keeper.svg",
      locked_text: "Create a serious self-hosted video library.",
      disclosure: "hint"
    },

    {
      id: "media.first_track",
      title: "First Track",
      category: "media",
      tier: "bronze",
      icon_key: "media-first-track",
      icon_asset: "badges/media-first-track.svg",
      locked_text: "Upload your first audio track.",
      disclosure: "exact"
    },
    {
      id: "media.signal_dj",
      title: "Signal DJ",
      category: "media",
      tier: "silver",
      icon_key: "media-signal-dj",
      icon_asset: "badges/media-signal-dj.svg",
      locked_text: "Build your music/audio collection over time.",
      disclosure: "hint"
    },
    {
      id: "media.sound_vault",
      title: "Sound Vault",
      category: "media",
      tier: "gold",
      icon_key: "media-sound-vault",
      icon_asset: "badges/media-sound-vault.svg",
      locked_text: "Create a serious self-hosted sound vault.",
      disclosure: "hint"
    },

    {
      id: "federation.pioneer",
      title: "Federation Pioneer",
      category: "federation",
      tier: "bronze",
      icon_key: "federation-pioneer",
      icon_asset: "badges/federation-pioneer.svg",
      locked_text: "Publish your first public Circle Stack post through federation.",
      disclosure: "exact"
    },
    {
      id: "federation.signal_courier",
      title: "Signal Courier",
      category: "federation",
      tier: "silver",
      icon_key: "federation-signal-courier",
      icon_asset: "badges/federation-signal-courier.svg",
      locked_text: "Keep sending meaningful signals through federation.",
      disclosure: "hint"
    },
    /* Node-level federation badges such as First Remote Signal and
       Cross-Node Conversation are intentionally not shown in My Profile.
       They will belong in a future Node Badges view. */
    {
      id: "federation.known_origin",
      title: "Known Origin",
      category: "federation",
      tier: "bronze",
      icon_key: "federation-known-origin",
      icon_asset: "badges/federation-known-origin.svg",
      locked_text: csT("profile.badgeDesc.lockedKnownOrigin", "Discover or add your first remote NAS origin."),
      disclosure: "exact"
    },
    {
      id: "federation.bridge_builder",
      title: "Bridge Builder",
      category: "federation",
      tier: "gold",
      icon_key: "federation-bridge-builder",
      icon_asset: "badges/federation-bridge-builder.svg",
      locked_text: csT("profile.badgeDesc.lockedBridgeBuilder", "Build bridges with several remote NAS origins."),
      disclosure: "hint"
    }
  ];
}

function csRenderLockedAchievementPlaceholders(earnedBadges) {
  if (csAchievementReviewAllEnabled()) return null;

  const earnedIds = new Set(
    csAchievementListFrom(earnedBadges).map(b => String(b.id || ""))
  );

  const locked = csAllAchievementPlaceholders()
    .filter(b => b && b.id && !earnedIds.has(b.id));

  if (!locked.length) return null;

  const section = document.createElement("div");
  section.className = "cs-profile-achievements cs-my-profile-locked-achievements";

  const label = document.createElement("div");
  label.className = "cs-profile-label";
  label.textContent = csT("profile.lockedAchievements", "Locked achievements");

  csAchievementReviewMaybeHideLockedSection(label.closest(".cs-profile-section") || label.parentElement);
section.appendChild(label);

  const grid = document.createElement("div");
  grid.className = "cs-profile-achievement-grid";

  for (const badge of locked) {
    const item = document.createElement("div");
    item.className = "cs-profile-achievement cs-profile-achievement-locked";
    if (badge.tier) item.classList.add(`tier-${String(badge.tier).toLowerCase()}`);
    if (badge.disclosure) item.classList.add(`disclosure-${String(badge.disclosure).toLowerCase()}`);

    const iconWrap = document.createElement("div");
    iconWrap.className = "cs-locked-achievement-icon-wrap";
    iconWrap.appendChild(csCreateBadgeIconElement(badge, "cs-profile-achievement-icon"));

    const lock = document.createElement("span");
    lock.className = "cs-locked-achievement-lock";
    lock.textContent = "Locked";
    iconWrap.appendChild(lock);

    const body = document.createElement("span");
    body.className = "cs-profile-achievement-body";

    const title = document.createElement("span");
    title.className = "cs-profile-achievement-title";
    title.textContent = badge.title || csT("profile.lockedAchievement", "Locked achievement");

    const desc = document.createElement("span");
    desc.className = "cs-profile-achievement-desc";
    desc.textContent = csAchievementLockedText(badge);

    body.appendChild(title);
    body.appendChild(desc);

    item.appendChild(iconWrap);
    item.appendChild(body);
    grid.appendChild(item);
  }

  section.appendChild(grid);
  return section;
}

function csAchievementStorageKey() {
  return "pqnas.circlestack.achievements.seen.v1";
}

function csLoadSeenAchievementIds() {
  try {
    const raw = localStorage.getItem(csAchievementStorageKey());
    const arr = raw ? JSON.parse(raw) : [];
    return new Set(Array.isArray(arr) ? arr.filter(Boolean) : []);
  } catch (_) {
    return new Set();
  }
}

function csSaveSeenAchievementIds(ids) {
  try {
    localStorage.setItem(csAchievementStorageKey(), JSON.stringify(Array.from(ids)));
  } catch (_) {}
}

async function csDismissAchievementUnlock(achievementId) {
  const id = String(achievementId || "").trim();
  if (!id) return;

  try {
    await fetch(`${CS_API}/achievements/dismiss`, {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ achievement_id: id })
    });
  } catch (_) {}
}

let csAchievementUnlockQueueActive = false;


function csAchievementReplayText(badge) {
  const id = String((badge && badge.id) || "");

  const texts = {
    "account.node_steward":
      csT("profile.achReplay.account.nodeSteward", "You help keep this DNA-Nexus node alive. Steward badges are about responsibility: maintaining the server, supporting users, and keeping the space trustworthy."),

    "account.established_signal":
      csT("profile.achReplay.account.establishedSignal", "Your identity has started to build history. Time matters in DNA-Nexus because long-lived accounts are harder to fake than one-day identities."),

    "account.old_guard":
      csT("profile.achReplay.account.oldGuard", "This account has been around for a long time. Old Guard badges show continuity, patience, and a persistent identity inside your own network."),

    "account.legacy_node":
      csT("profile.achReplay.account.legacyNode", "A long-lived node becomes part of the story. Legacy Node marks an identity that has survived across time, updates, and changing communities."),

    "circlestack.first_signal":
      csT("profile.achReplay.circlestack.firstSignal", "Every network starts with one signal. This badge marks the moment your Circle Stack identity stopped being empty and started becoming part of the feed."),

    "circlestack.signal_sender":
      csT("profile.achReplay.circlestack.signalSender", "You keep contributing to the conversation. This badge is about showing up over time, not just making noise once."),

    "circlestack.broadcast_node":
      csT("profile.achReplay.circlestack.broadcastNode", "Your Circle Stack presence has become steady. A broadcast node helps keep the community alive by adding regular signals to the network."),

    "circlestack.anchor_voice":
      csT("profile.achReplay.circlestack.anchorVoice", "Anchor voices shape the memory of a circle. This badge is for long-term posting history and continued presence."),

    "circlestack.public_voice":
      csT("profile.achReplay.circlestack.publicVoice", "You have shared beyond your private circle. Public Voice means some of your posts help the wider DNA-Nexus network feel alive."),

    "circlestack.media_runner":
      csT("profile.achReplay.circlestack.mediaRunner", "You enrich posts with media. Images, videos, and other files turn a feed from plain text into shared memory."),

    "circlestack.conversation_spark":
      csT("profile.achReplay.circlestack.conversationSpark", "You take part in discussions instead of only posting alone. Replies are one of the strongest signs that a circle is becoming social."),

    "circlestack.signal_amplifier":
      csT("profile.achReplay.circlestack.signalAmplifier", "You help surface other people’s posts. Reactions are small signals, but together they tell the community what matters."),

    "circlestack.crowd_spark":
      csT("profile.achReplay.circlestack.crowdSpark", "Your posts invite response. This badge means your activity is not only visible — it is getting reactions from others."),

    "circlestack.thread_starter":
      csT("profile.achReplay.circlestack.threadStarter", "You start conversations that others join. Thread Starter is about creating openings for discussion, not only broadcasting."),

    "circlestack.circle_builder":
      csT("profile.achReplay.circlestack.circleBuilder", "You are building trusted connections. Circle Builder is about turning isolated identities into a real social graph."),

    "shares.first_share":
      csT("profile.achReplay.shares.firstShare", "You created your first share link. Sharing is one of the core NAS powers: letting someone access exactly what you choose, when you choose."),

    "shares.packet_runner":
      csT("profile.achReplay.shares.packetRunner", "You use your NAS as a distribution point. Packet Runner reflects repeated sharing without needing a centralized cloud service."),

    "shares.distribution_node":
      csT("profile.achReplay.shares.distributionNode", "Your server is becoming a serious distribution node. This badge marks long-term sharing activity from your own infrastructure."),

    "storage.data_seed":
      csT("profile.achReplay.storage.dataSeed", "You started growing your personal data vault. Small uploads become the seed of a long-term archive."),

    "storage.vault_keeper":
      csT("profile.achReplay.storage.vaultKeeper", "You are keeping meaningful data under your own control. Vault Keeper is about building a private archive instead of scattering files everywhere."),

    "storage.keeper_500gb":
      csT("profile.achReplay.storage.keeper500gb", "Your storage is no longer just experimental. A large vault means your NAS is becoming part of your real digital life."),

    "storage.terabyte_guardian":
      csT("profile.achReplay.storage.terabyteGuardian", "You are guarding a serious archive. Terabyte Guardian marks commitment to long-term self-hosted storage."),

    "dropzone.operator":
      csT("profile.achReplay.dropzone.operator", "You opened a controlled upload path for others. Drop Zones let people send files to you without giving them full access to your NAS."),

    "dropzone.gatekeeper":
      csT("profile.achReplay.dropzone.gatekeeper", "You show strong engagement by letting others send data into your storage through controlled gates. Keep managing those gates well."),

    "security.trusted_device":
      csT("profile.achReplay.security.trustedDevice", "You paired a trusted device. Security badges are about strengthening identity and making access safer without losing convenience."),

    "echostack.first_archive":
      csT("profile.achReplay.echostack.firstArchive", "You saved your first page into Echo Stack. This is the start of your own web memory, kept on your NAS instead of disappearing into browser tabs."),

    "echostack.web_preserver":
      csT("profile.achReplay.echostack.webPreserver", "You are preserving useful pieces of the web. Web Preserver is about turning passing links into a library you control."),

    "echostack.memory_vault":
      csT("profile.achReplay.echostack.memoryVault", "Your Echo Stack is becoming a real knowledge vault. This badge means you are building memory, not just collecting bookmarks."),

    "echostack.deep_archive":
      csT("profile.achReplay.echostack.deepArchive", "You are storing heavier web snapshots locally. Deep Archive is for users who want pages to remain available even when the original web changes."),

    "media.first_snapshot":
      csT("profile.achReplay.media.firstSnapshot", "You uploaded your first photo. This is where your NAS starts becoming a personal memory machine, not just a file bucket."),

    "media.memory_keeper":
      csT("profile.achReplay.media.memoryKeeper", "You are building a real photo history. Memory Keeper is about keeping your own visual memories under your own control."),

    "media.gallery_curator":
      csT("profile.achReplay.media.galleryCurator", "Your photo library is becoming serious. Gallery Curator marks a NAS that is starting to feel like a private photo cloud."),

    "media.first_reel":
      csT("profile.achReplay.media.firstReel", "You uploaded your first video. Reel Stack starts with one clip, but grows into a self-hosted video vault over time."),

    "media.video_vault":
      csT("profile.achReplay.media.videoVault", "You are keeping videos on your own storage. Video Vault is about moving memories and media away from disposable cloud silos."),

    "media.cinema_keeper":
      csT("profile.achReplay.media.cinemaKeeper", "Your NAS is becoming a real video library. Cinema Keeper is for users who preserve larger moving memories and media collections."),

    "media.first_track":
      csT("profile.achReplay.media.firstTrack", "You uploaded your first audio track. NeonWave starts when your NAS begins to carry sound, not only files."),

    "media.signal_dj":
      csT("profile.achReplay.media.signalDj", "You are building a music/audio collection. Signal DJ is about turning your NAS into a private listening base."),

    "media.sound_vault":
      csT("profile.achReplay.media.soundVault", "Your audio archive is becoming serious. Sound Vault marks a self-hosted library of tracks, recordings, or sound memories."),

    "federation.pioneer":
      csT("profile.achReplay.federation.pioneer", "Your NAS has sent its first public Circle Stack signal into the wider DNA-Nexus network. This is the moment your node stops being only local."),

    "federation.signal_courier":
      csT("profile.achReplay.federation.signalCourier", "Your NAS is carrying signals across the network. Signal Courier is about reliable participation, not just one successful test."),

    "federation.first_remote_signal":
      csT("profile.achReplay.federation.firstRemoteSignal", "Your node heard another NAS. First Remote Signal means the federation is no longer theoretical — another origin reached your Circle Stack."),

    "federation.cross_node_conversation":
      csT("profile.achReplay.federation.crossNodeConversation", "Conversation has crossed node boundaries. This badge marks real social activity between separate DNA-Nexus servers."),

    "federation.known_origin":
      csT("profile.achReplay.federation.knownOrigin", "You added or discovered another NAS origin. Known origins are the first step toward a real web of personal servers."),

    "federation.bridge_builder":
      csT("profile.achReplay.federation.bridgeBuilder", "You have helped connect this NAS with multiple origins. Bridge Builder is about making the wider DNA-Nexus network more discoverable.")
  };

  return texts[id] || "";
}

function csShowAchievementUnlockedModal(badge, options = {}) {
  const modalOptions = options && typeof options === "object" ? options : {};

  return new Promise((resolve) => {
    if (!badge || !badge.id) {
      resolve(false);
      return;
    }

    const old = document.querySelector(".cs-achievement-unlock-backdrop");
    if (old) old.remove();

    const previousFocus = document.activeElement;

    const backdrop = document.createElement("div");
    backdrop.className = "cs-modal-backdrop cs-achievement-unlock-backdrop";

    const modal = document.createElement("div");
    modal.className = "cs-modal cs-achievement-unlock-modal";
    modal.setAttribute("role", "dialog");
    modal.setAttribute("aria-modal", "true");

    const kicker = document.createElement("div");
    kicker.className = "cs-achievement-unlock-kicker";
    kicker.textContent = modalOptions.replay ? "Achievement" : "Achievement unlocked";

    const icon = document.createElement("div");
    icon.className = "cs-achievement-unlock-icon";
    icon.appendChild(csCreateBadgeIconElement(badge, "cs-achievement-unlock-icon-img", {
    eager: true
  }));

    const title = document.createElement("div");
    title.className = "cs-achievement-unlock-title";
    title.textContent = badge.title || csT("profile.newAchievement", "New achievement");

    const desc = document.createElement("div");
    desc.className = "cs-achievement-unlock-desc";
    desc.textContent = csAchievementDescriptionText(badge, "short");

    const tier = document.createElement("div");
    tier.className = "cs-achievement-unlock-tier";
    tier.textContent = csAchievementMetaLabel(badge);

    const replayText = document.createElement("div");
    replayText.className = "cs-achievement-replay-text";
    replayText.textContent = csAchievementReplayText(badge) || csAchievementDescriptionText(badge, "long");

    const actions = document.createElement("div");
    actions.className = "cs-modal-actions";

    const close = document.createElement("button");
    close.className = "cs-modal-cancel";
    close.type = "button";
    close.textContent = modalOptions.replay ? csT("common.close", "Close") : csT("common.nice", "Nice");

    let closed = false;

    const dismissAndClose = () => {
      if (closed) return;
      closed = true;

      document.removeEventListener("keydown", onKey, true);

      const closePromise = modalOptions.replay
        ? Promise.resolve()
        : Promise.resolve(csDismissAchievementUnlock(badge.id));

      closePromise
        .catch(() => {})
        .finally(() => {
          backdrop.remove();

          try {
            if (previousFocus && typeof previousFocus.focus === "function") {
              previousFocus.focus();
            }
          } catch (_) {}

          resolve(true);
        });
    };

    function onKey(ev) {
      if (ev.key === "Escape" || ev.key === "Enter") {
        ev.preventDefault();
        dismissAndClose();
      }
    }

    close.addEventListener("click", dismissAndClose);

    actions.appendChild(close);

    modal.appendChild(kicker);
    modal.appendChild(icon);
    modal.appendChild(title);
    if (desc.textContent) modal.appendChild(desc);
    if (tier.textContent) modal.appendChild(tier);
    if (replayText.textContent) modal.appendChild(replayText);
    modal.appendChild(actions);

    backdrop.appendChild(modal);
    backdrop.addEventListener("click", (ev) => {
      if (ev.target === backdrop) dismissAndClose();
    });

    document.body.appendChild(backdrop);
    document.addEventListener("keydown", onKey, true);
    close.focus();
  });
}

async function csRunAchievementUnlockQueue(rawBadges) {
  const queue = csAchievementListFrom(rawBadges)
    .filter(b => b && b.id);

  if (!queue.length) return;
  if (csAchievementUnlockQueueActive) return;

  csAchievementUnlockQueueActive = true;

  try {
    // Preload SVG icons before showing the queue so the modal does not open
    // with an empty/glowing placeholder while the image decodes.
    await csPreloadBadgeIconAssets(queue);

    // Small delay lets the feed render first, so the modal feels intentional.
    await new Promise(resolve => setTimeout(resolve, 250));

    for (const badge of queue) {
      await csShowAchievementUnlockedModal(badge);
      await new Promise(resolve => setTimeout(resolve, 140));
    }
  } finally {
    csAchievementUnlockQueueActive = false;
  }
}

async function csCheckAchievementUnlocks() {
  let data = null;

  try {
    const res = await fetch(`${CS_API}/achievements/me`, {
      credentials: "same-origin"
    });
    if (!res.ok) return;
    data = await res.json();
  } catch (_) {
    return;
  }

  const badges = csAchievementListFrom(data && data.achievements);
  if (!badges.length) return;

  // V2 backend returns newly_unlocked. When present, trust server-side unlock history.
  if (data && Array.isArray(data.newly_unlocked)) {
    const serverNewlyUnlocked = csAchievementListFrom(data.newly_unlocked);
    if (serverNewlyUnlocked.length) {
      csRunAchievementUnlockQueue(serverNewlyUnlocked);
    }
    return;
  }

  // Fallback for older backends.
  const seen = csLoadSeenAchievementIds();
  const newlyUnlocked = badges.filter(b => b && b.id && !seen.has(b.id));

  for (const badge of badges) {
    if (badge && badge.id) seen.add(badge.id);
  }
  csSaveSeenAchievementIds(seen);

  if (newlyUnlocked.length) {
    csRunAchievementUnlockQueue(newlyUnlocked);
  }
}

let csFeedMode = "local";
let csFederatedFeedLoadSeq = 0;
let csMutedFederatedOrigins = new Set();

function csI18nKey(key) {
  const raw = String(key || "");
  return raw.startsWith("circlestack.") ? raw : `circlestack.${raw}`;
}

function csT(key, vars = null, fallback = undefined) {
  if (typeof vars === "string" && fallback === undefined) {
    fallback = vars;
    vars = null;
  }

  const fullKey = csI18nKey(key);
  const i18n = window.PQNAS_I18N;

  if (i18n && typeof i18n.t === "function") {
    return i18n.t(fullKey, vars || null, fallback ?? fullKey);
  }

  return String(fallback ?? fullKey);
}

async function csApplyI18n(root = document) {
  const i18n = window.PQNAS_I18N;
  if (!i18n) return;

  if (typeof i18n.ready === "function") {
    await i18n.ready();
  }

  if (typeof i18n.apply === "function") {
    i18n.apply(root || document);
  }
}

window.csT = csT;
window.csApplyI18n = csApplyI18n;


async function csLoadFeed() {
  csCheckAchievementUnlocks();

  const feed = document.getElementById("csFeed");
  if (!feed) return;

  feed.textContent = "";

  const res = await fetch(`${CS_API}/feed`, { credentials: "same-origin" });
  const data = await res.json();
  const posts = Array.isArray(data.posts) ? data.posts : [];

  if (!posts.length) {
    const empty = document.createElement("div");
    empty.className = "cs-empty";
    empty.textContent = csT("feed.empty", "No moments yet.");
    feed.appendChild(empty);
    return;
  }

  for (const post of posts) {
    feed.appendChild(csRenderPost(post));
  }
}


function csFederatedTypeLabel(type, targetType = "") {
  if (type === "circle.post.created") return "Remote post";
  if (type === "circle.reply.created") return "Remote reply";
  if (type === "circle.reaction.created") {
    return targetType === "reply" ? "Remote reply reaction" : csT("federation.remotePostReaction", "Remote post reaction");
  }
  if (type === "circle.reaction.removed") {
    return targetType === "reply" ? "Remote reply reaction removed" : "Remote post reaction removed";
  }
  return type || "Remote event";
}

function csFederatedActorLabel(ev) {
  const payload = ev && ev.payload && typeof ev.payload === "object" ? ev.payload : {};

  return (
    ev.origin_label ||
    ev.actor_display_name ||
    payload.origin_label ||
    payload.owner_display_name ||
    payload.actor_display_name ||
    ev.actor_fp_short ||
    ev.actor_fp ||
    ev.origin_nas ||
    "remote"
  );
}




// FEDERATED_REMOTE_ACTIONS_PATCH_V1

function csFederatedActorFingerprint(ev) {
  const payload = ev && ev.payload && typeof ev.payload === "object" ? ev.payload : {};

  return String(
    payload.owner_fp ||
    payload.actor_fp ||
    ev.actor_fp ||
    ""
  ).trim();
}


// KNOWN_REMOTE_ORIGINS_FROM_PEOPLE_UI_PATCH_V1

function csFederatedOriginInfo(ev) {
  const payload = ev && ev.payload && typeof ev.payload === "object" ? ev.payload : {};
  const eventJson = ev && ev.event && typeof ev.event === "object" ? ev.event : {};

  const origin =
    payload.origin && typeof payload.origin === "object"
      ? payload.origin
      : (
          eventJson.origin && typeof eventJson.origin === "object"
            ? eventJson.origin
            : {}
        );

  return {
    origin_nas: String(
      ev.origin_nas ||
      eventJson.origin_nas ||
      origin.nas_id ||
      ""
    ).trim(),
    public_base_url: String(origin.preview_base_url || "").replace(/\/+$/, "")
  };
}

// CIRCLESTACK_CUSTOM_MESSAGE_MODAL_PATCH_V1

function csShowMessageDialog(opts = {}) {
  return new Promise((resolve) => {
    const old = document.querySelector(".cs-message-backdrop");
    if (old) old.remove();

    const previousFocus = document.activeElement;

    const backdrop = document.createElement("div");
    backdrop.className = "cs-modal-backdrop cs-message-backdrop";

    const card = document.createElement("div");
    card.className = `cs-modal cs-message-modal cs-message-${opts.kind || "info"}`;
    card.setAttribute("role", "dialog");
    card.setAttribute("aria-modal", "true");

    const title = document.createElement("div");
    title.className = "cs-modal-title";
    title.textContent = String(opts.title || "Circle Stack");

    const text = document.createElement("div");
    text.className = "cs-modal-text";
    text.textContent = String(opts.message || "");

    card.appendChild(title);
    if (text.textContent) card.appendChild(text);

    if (opts.detail) {
      const detail = document.createElement("pre");
      detail.className = "cs-modal-detail";
      detail.textContent = String(opts.detail);
      card.appendChild(detail);
    }

    const actions = document.createElement("div");
    actions.className = "cs-modal-actions";

    const ok = document.createElement("button");
    ok.className = "cs-modal-cancel cs-modal-ok";
    ok.type = "button";
    ok.textContent = String(opts.okText || "OK");

    function close() {
      document.removeEventListener("keydown", onKey, true);
      backdrop.remove();
      try {
        if (previousFocus && typeof previousFocus.focus === "function") {
          previousFocus.focus();
        }
      } catch (_) {}
      resolve(true);
    }

    function onKey(ev) {
      if (ev.key === "Escape" || ev.key === "Enter") {
        ev.preventDefault();
        close();
      }
    }

    ok.addEventListener("click", close);
    actions.appendChild(ok);
    card.appendChild(actions);
    backdrop.appendChild(card);
    document.body.appendChild(backdrop);

    document.addEventListener("keydown", onKey, true);
    setTimeout(() => ok.focus(), 0);
  });
}

function csRemoteAddSuccessMessage(data) {
  if (data && data.self) {
    return {
      title: "Remote NAS followed",
      message:
        "This post belongs to your own fingerprint on another NAS. " +
        "I did not add yourself as a People contact, but I added that remote NAS as a known Circle Stack origin.",
      detail: data.remote_origin_nas || "",
      kind: "success"
    };
  }

  if (data && data.polling === "known_remote_origin_added") {
    return {
      title: "Added to People",
      message:
        "The remote person was added and their NAS origin was saved for Circle Stack polling.",
      detail: data.remote_origin_nas || "",
      kind: "success"
    };
  }

  return {
    title: "Added to People",
    message: "The remote person was added.",
    kind: "success"
  };
}

async function csAddFederatedPerson(ev) {
  const fp = csFederatedActorFingerprint(ev);
  if (!fp && !(ev && ev.event_id)) {
    await csShowMessageDialog({
      title: "Cannot add remote person",
      message: "No remote fingerprint or source event was found for this federated item.",
      kind: "error"
    });
    return;
  }

  const res = await fetch(`${CS_API}/federated/people/add`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      fp,
      display_name: csFederatedActorLabel(ev),
      source_event_id: ev.event_id || "",
      remote_origin_nas: csFederatedOriginInfo(ev).origin_nas,
      remote_public_base_url: csFederatedOriginInfo(ev).public_base_url
    })
  });

  const data = await res.json().catch(() => ({ ok: false }));
  if (!res.ok || !data.ok) {
    await csShowMessageDialog({
      title: "Could not add remote person",
      message: data.error || `HTTP ${res.status}`,
      detail: data.detail || data.message || "",
      kind: "error"
    });
    return;
  }

  await csShowMessageDialog(csRemoteAddSuccessMessage(data));
}

// FEDERATED_REACTION_CLICK_FEEDBACK_PATCH_V1
async function csReactToFederatedPost(ev, reaction, button = null) {
  if (!ev || !ev.event_id) return;

  const oldText = button ? button.textContent : "";
  if (button) {
    button.disabled = true;
    button.textContent = "…";
  }

  try {
    const res = await fetch(`${CS_API}/federated/posts/react`, {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        event_id: ev.event_id,
        reaction
      })
    });

    const data = await res.json().catch(() => ({ ok: false }));

    if (!res.ok || !data.ok) {
      const msg = data.error || data.detail || `HTTP ${res.status}`;
      if (button) {
        button.textContent = "!";
        button.title = `Could not queue federated reaction: ${msg}`;
        setTimeout(() => {
          button.disabled = false;
          button.textContent = oldText || reaction;
        }, 1600);
      }
      await csShowMessageDialog({
        title: "Could not queue reaction",
        message: msg,
        kind: "error"
      });
      return;
    }

    if (button) {
      button.textContent = `Queued ${reaction}`;
      button.title = data.federation_event_id
        ? `Queued as ${data.federation_event_id}`
        : "Federated reaction queued";
      setTimeout(() => {
        button.disabled = false;
        button.textContent = oldText || reaction;
      }, 1800);
    }

    csRememberFederatedLocalReaction(ev, reaction, data);
    csRefreshFederatedLocalReactionInCard(button ? button.closest(".cs-federated-post") : null, ev);

    console.log("Federated reaction queued", data);
  } catch (err) {
    if (button) {
      button.textContent = "!";
      button.title = String(err && err.message ? err.message : err);
      setTimeout(() => {
        button.disabled = false;
        button.textContent = oldText || reaction;
      }, 1600);
    }
    await csShowMessageDialog({
      title: "Could not queue reaction",
      message: err && err.message ? err.message : String(err),
      kind: "error"
    });
  }
}

function csRenderFederatedActions(ev) {
  const actorFp = csFederatedActorFingerprint(ev);
  const wrap = document.createElement("div");
  wrap.className = "cs-federated-actions";

  const personActions = document.createElement("div");
  personActions.className = "cs-federated-person-actions";

  const postActions = document.createElement("div");
  postActions.className = "cs-federated-post-actions";

  if (actorFp) {
    const person = document.createElement("button");
    person.className = "cs-modal-cancel";
    person.type = "button";
    person.textContent = "Person";
    person.addEventListener("click", () => {
      csOpenPersonCard(actorFp, {
        display_name: csFederatedActorLabel(ev),
        fp_short: ev.actor_fp_short || csElideFp(actorFp),
        achievements: csFederatedActorBadges(ev)
      });
    });
    personActions.appendChild(person);

    if (navigator.clipboard) {
      const copy = document.createElement("button");
      copy.className = "cs-modal-cancel";
      copy.type = "button";
      copy.textContent = "Copy FP";
      copy.addEventListener("click", async () => {
        await navigator.clipboard.writeText(actorFp);
        copy.textContent = csT("common.copied", "Copied");
        setTimeout(() => { copy.textContent = "Copy FP"; }, 1200);
      });
      personActions.appendChild(copy);
    }

    const add = document.createElement("button");
    add.className = "cs-modal-cancel";
    add.type = "button";
    add.textContent = "Add to People";
    add.addEventListener("click", () => csAddFederatedPerson(ev));
    personActions.appendChild(add);
  }

  if (ev && ev.event_type === "circle.post.created") {
    const picker = document.createElement("div");
    picker.className = "cs-reaction-picker cs-federated-reaction-picker";

    const trigger = document.createElement("button");
    trigger.className = "cs-reaction-trigger";
    trigger.type = "button";
    trigger.textContent = "🙂 React";
    trigger.title = "React to this federated post";

    const menu = document.createElement("div");
    menu.className = "cs-reaction-menu";

    for (const reaction of CS_REACTIONS) {
      const btn = document.createElement("button");
      btn.className = "cs-reaction-menu-button";
      btn.type = "button";
      btn.textContent = reaction;
      btn.setAttribute("aria-label", `React ${reaction}`);
      btn.addEventListener("click", () => csReactToFederatedPost(ev, reaction, btn));
      menu.appendChild(btn);
    }

    picker.appendChild(trigger);
    picker.appendChild(menu);
    postActions.appendChild(picker);
  }

  if (personActions.children.length) {
    wrap.appendChild(personActions);
  }

  if (postActions.children.length) {
    wrap.appendChild(postActions);
  }

  return wrap.children.length ? wrap : null;
}

function csFederatedMediaRefsFromPayload(payload) {
  const p = payload && typeof payload === "object" ? payload : {};
  const preview = p.media_preview && typeof p.media_preview === "object"
    ? p.media_preview
    : null;

  return Array.isArray(p.media_refs)
    ? p.media_refs
    : (
      preview && Array.isArray(preview.refs)
        ? preview.refs
        : []
    );
}

function csFederatedPreviewUrl(ev, ref) {
  const eventId = String(ev && ev.event_id ? ev.event_id : "");
  const refId = String(ref && ref.ref_id ? ref.ref_id : "");

  if (!eventId || !refId) return "";

  const payload = ev && ev.payload && typeof ev.payload === "object" ? ev.payload : {};
  const eventJson = ev && ev.event && typeof ev.event === "object" ? ev.event : {};

  const origin = payload.origin && typeof payload.origin === "object"
    ? payload.origin
    : (
      eventJson.origin && typeof eventJson.origin === "object"
        ? eventJson.origin
        : {}
    );

  const endpoint = String(origin.preview_endpoint || "/api/v4/circlestack/federation/media-preview");
  const base = String(origin.preview_base_url || "").replace(/\/+$/, "");

  const sep = endpoint.includes("?") ? "&" : "?";
  const path = `${endpoint}${sep}event_id=${encodeURIComponent(eventId)}&ref_id=${encodeURIComponent(refId)}`;

  if (!base) return path;

  return `${base}${path.startsWith("/") ? "" : "/"}${path}`;
}


function csRenderFederatedMediaPreview(ev, payload) {
  const refs = csFederatedMediaRefsFromPayload(payload);
  if (!refs.length) return null;

  const primary = refs.find(ref => ref && ref.role === "primary") || refs[0];
  const url = csFederatedPreviewUrl(ev, primary);
  if (!url) return null;

  const wrap = document.createElement("div");
  wrap.className = "cs-federated-preview-wrap";

  const img = document.createElement("img");
  img.className = "cs-federated-preview-img";
  img.src = url;
  img.alt = "";
  img.loading = "lazy";
  img.decoding = "async";

  img.addEventListener("error", () => {
    wrap.classList.add("is-error");
    wrap.textContent = csT("federation.previewUnavailable", "Preview unavailable from origin NAS");
  }, { once: true });

  wrap.appendChild(img);
  return wrap;
}


function csFederatedMediaLabel(payload) {
  const p = payload && typeof payload === "object" ? payload : {};
  const preview = p.media_preview && typeof p.media_preview === "object"
    ? p.media_preview
    : null;

  const refs = Array.isArray(p.media_refs)
    ? p.media_refs
    : (
      preview && Array.isArray(preview.refs)
        ? preview.refs
        : []
    );

  const count = Number(p.media_count || refs.length || (p.has_media ? 1 : 0));

  if (!count) return "Media: no";

  const kinds = Array.from(new Set(
    refs
      .map(ref => ref && ref.kind ? String(ref.kind) : "")
      .filter(Boolean)
  ));

  if (kinds.length === 1) {
    return `Media: ${count} ${kinds[0]}${count === 1 ? "" : "s"}`;
  }

  if (kinds.length > 1) {
    return `Media: ${count} items`;
  }

  return `Media: ${count}`;
}


function csRenderFederatedEvent(ev) {
  const card = document.createElement("article");
  card.className = "cs-post cs-federated-post";

  const header = document.createElement("div");
  header.className = "cs-post-header";

  const titleWrap = document.createElement("div");
  titleWrap.className = "cs-federated-title-wrap";

  // FEDERATED_ACTOR_PRIMARY_HEADER_PATCH_V1
  const title = document.createElement("div");
  title.className = "cs-post-author cs-federated-actor-name";
  title.textContent = csFederatedActorLabel(ev);

  const sub = document.createElement("div");
  sub.className = "cs-federated-sub";
  sub.textContent = csFederatedTypeLabel(ev.event_type, ev.target_type);

  titleWrap.appendChild(title);
  titleWrap.appendChild(sub);

  const federatedBadgeStrip = csRenderAchievementStrip(csFederatedActorBadges(ev), { max: 3 });
  if (federatedBadgeStrip) {
    titleWrap.appendChild(federatedBadgeStrip);
  }

  const badge = document.createElement("span");
  badge.className = "cs-federated-badge";
  badge.textContent = "FEDERATED";

  header.appendChild(titleWrap);
  header.appendChild(badge);
  card.appendChild(header);

  // CIRCLESTACK_FEDERATED_REASON_HOOK_V1
  if (window.CircleStackFeedModes &&
      typeof window.CircleStackFeedModes.decorateFederatedEvent === "function") {
    window.CircleStackFeedModes.decorateFederatedEvent(card, ev, csFeedMode);
  }

  const federatedActions = csRenderFederatedActions(ev);
  if (federatedActions) {
    const personActions = federatedActions.querySelector(".cs-federated-person-actions");

    if (personActions && personActions.children.length) {
      titleWrap.classList.add("has-federated-actor-menu");
      personActions.classList.add("cs-federated-actor-menu");
      titleWrap.appendChild(personActions);
    }

    if (federatedActions.children.length) {
      card.appendChild(federatedActions);
    }
  }

  const payload = ev.payload && typeof ev.payload === "object" ? ev.payload : {};
  const previewText = String(ev.text_preview || payload.text_preview || "").trim();

  if (previewText) {
    const text = document.createElement("div");
    text.className = "cs-federated-text-preview";
    text.textContent = previewText;
    card.appendChild(text);
  }

  const lines = [];

  if (ev.event_type === "circle.post.created") {
    // FEDERATED_HIDE_REMOTE_POST_DEBUG_BODY_V1
    // Normal remote posts should read like social posts, not debug records.
    // Media is already shown by the preview block below when present.
  } else if (ev.event_type === "circle.reply.created") {
    lines.push(`Post id: ${ev.post_id || payload.post_id || "unknown"}`);
    lines.push(`Reply id: ${ev.reply_id || payload.reply_id || "unknown"}`);
    lines.push(csFederatedMediaLabel(payload));
  } else if (ev.event_type === "circle.reaction.created") {
    lines.push(`Target: ${ev.target_type || payload.target_type || "post"}`);
    lines.push(`Post id: ${ev.post_id || payload.post_id || "unknown"}`);
    if (ev.reply_id || payload.reply_id) lines.push(`Reply id: ${ev.reply_id || payload.reply_id}`);
    lines.push(`Reaction: ${ev.reaction || payload.reaction || ""}`);
  } else if (ev.event_type === "circle.reaction.removed") {
    lines.push(`Target: ${ev.target_type || payload.target_type || "post"}`);
    lines.push(`Post id: ${ev.post_id || payload.post_id || "unknown"}`);
    if (ev.reply_id || payload.reply_id) lines.push(`Reply id: ${ev.reply_id || payload.reply_id}`);
  }

  const bodyText = lines.filter(Boolean).join(" · ");
  if (bodyText) {
    const body = document.createElement("div");
    body.className = "cs-federated-body";
    body.textContent = bodyText;
    card.appendChild(body);
  }

  const mediaPreview = payload.media_preview && typeof payload.media_preview === "object"
    ? payload.media_preview
    : null;

  const mediaRefs = Array.isArray(payload.media_refs)
    ? payload.media_refs
    : (
      mediaPreview && Array.isArray(mediaPreview.refs)
        ? mediaPreview.refs
        : []
    );

  if ((payload.has_media === true || (mediaPreview && mediaPreview.has_media === true) || mediaRefs.length > 0) &&
      (!mediaPreview || mediaPreview.status !== "none")) {
    const previewEl = csRenderFederatedMediaPreview(ev, payload);
    if (previewEl) {
      card.appendChild(previewEl);
    }

    const media = document.createElement("div");
    media.className = "cs-federated-media-placeholder";

    const kinds = Array.from(new Set(
      mediaRefs
        .map(ref => ref && ref.kind ? String(ref.kind) : "")
        .filter(Boolean)
    ));

    const count = Number(payload.media_count || mediaRefs.length || (payload.has_media ? 1 : 0));
    const kindText = kinds.length ? ` · ${kinds.join(", ")}` : "";
    media.textContent = previewEl
      ? csT("federation.remoteMediaValidated", { count: count || 1, kind: kindText }, `${count || 1} remote media item${count === 1 ? "" : "s"}${kindText} · origin preview validated`)
      : `${count || 1} remote media item${count === 1 ? "" : "s"}${kindText} · preview fetch coming later`;
    card.appendChild(media);
  }

  const localReaction = csRenderFederatedLocalReaction(ev);
  if (localReaction) {
    card.appendChild(localReaction);
  }

  const meta = document.createElement("div");
  meta.className = "cs-post-meta";

  // FEDERATED_CLEAN_META_FOOTER_V1
  const createdText = ev.created_epoch
    ? new Date(ev.created_epoch * 1000).toLocaleString()
    : "";

  meta.textContent = createdText
    ? `${createdText} · Federated`
    : csT("federation.federated", "Federated");

  if (ev.event_id) {
    meta.title = `Federation event: ${ev.event_id}`;
  }

  card.appendChild(meta);

  return card;
}

async function csLoadFederatedFeed() {
  const feed = document.getElementById("csFederatedFeed");
  if (!feed) return;

  const loadSeq = ++csFederatedFeedLoadSeq;

  let data = null;
  try {
    const res = await fetch(`${CS_API}/federated/feed?limit=50`, {
      credentials: "same-origin"
    });
    data = await res.json();
  } catch (_) {
    data = { ok: false };
  }

  // If another federated-feed load started after this one, ignore this older result.
  if (loadSeq !== csFederatedFeedLoadSeq) return;

  feed.textContent = "";

  // CIRCLESTACK_FEED_MODES_HOOKS_V1
  const rawEvents = data && Array.isArray(data.events) ? data.events : [];

  let events = rawEvents;
  let emptyText = "No federated events yet.";

  if (window.CircleStackFeedModes &&
      typeof window.CircleStackFeedModes.filterFederatedEvents === "function") {
    events = await window.CircleStackFeedModes.filterFederatedEvents(rawEvents, csFeedMode);

    if (typeof window.CircleStackFeedModes.emptyMessage === "function") {
      emptyText = window.CircleStackFeedModes.emptyMessage(csFeedMode);
    }
  } else {
    await csRefreshMutedOriginSet();

    events = rawEvents.filter((ev) => {
      const origin = String(ev && ev.origin_nas ? ev.origin_nas : "").trim();
      return !origin || !csMutedFederatedOrigins.has(origin);
    });
  }

  if (!events.length) {
    const empty = document.createElement("div");
    empty.className = "cs-empty";
    empty.textContent = emptyText;
    feed.appendChild(empty);
    return;
  }

  if (typeof csLoadFederatedLocalReactionsForEvents === "function") {
    await csLoadFederatedLocalReactionsForEvents(events);
  }

  for (const ev of events) {
    feed.appendChild(csRenderFederatedEvent(ev));
  }
}

async function csSetFeedMode(mode) {
  const feedModeApi = window.CircleStackFeedModes || null;

  csFeedMode = feedModeApi && typeof feedModeApi.normalizeMode === "function"
    ? feedModeApi.normalizeMode(mode)
    : (mode === "federated" ? "federated" : "local");

  const localBtn = document.getElementById("csLocalFeedBtn");
  const fedBtn = document.getElementById("csFederatedBtn");
  const localFeed = document.getElementById("csFeed");
  const fedFeed = document.getElementById("csFederatedFeed");
  const intros = document.getElementById("csIntroductions");

  const showingLocal = csFeedMode === "local";
  const showingFederatedSurface = feedModeApi && typeof feedModeApi.isFederatedSurface === "function"
    ? feedModeApi.isFederatedSurface(csFeedMode)
    : csFeedMode === "federated";

  if (feedModeApi && typeof feedModeApi.setActiveButtons === "function") {
    feedModeApi.setActiveButtons(csFeedMode);
  } else {
    if (localBtn) localBtn.classList.toggle("is-active", csFeedMode === "local");
    if (fedBtn) fedBtn.classList.toggle("is-active", csFeedMode === "federated");
  }

  if (localFeed) {
    localFeed.hidden = !showingLocal;
    localFeed.style.display = showingLocal ? "" : "none";
  }

  if (fedFeed) {
    fedFeed.hidden = !showingFederatedSurface;
    fedFeed.style.display = showingFederatedSurface ? "" : "none";
  }

  if (intros) {
    intros.hidden = !showingLocal;
    intros.style.display = showingLocal ? "" : "none";
  }

  if (showingFederatedSurface) {
    await csLoadFederatedFeed();
  } else {
    await csLoadFeed();
  }
}


function csHideMyProfilePage() {
  const profilePage = document.getElementById("csProfilePage");
  const profileBtn = document.getElementById("csMyProfileBtn");
  const compose = document.querySelector(".cs-compose");

  if (profilePage) {
    profilePage.hidden = true;
    profilePage.style.display = "none";
  }

  if (profileBtn) {
    profileBtn.classList.remove("is-active");
  }

  if (compose) {
    compose.hidden = false;
    compose.style.display = "";
  }
}

function csFormatProfileNumber(value) {
  const n = Number(value || 0);
  if (!Number.isFinite(n)) return "0";
  return n.toLocaleString();
}

function csProfileStat(label, value) {
  const item = document.createElement("div");
  item.className = "cs-my-profile-stat";

  const num = document.createElement("div");
  num.className = "cs-my-profile-stat-value";
  num.textContent = csFormatProfileNumber(value);

  const lab = document.createElement("div");
  lab.className = "cs-my-profile-stat-label";
  lab.textContent = label;

  item.appendChild(num);
  item.appendChild(lab);
  return item;
}

function csProfileFingerprintBlock(fp) {
  const wrap = document.createElement("div");
  wrap.className = "cs-my-profile-fp-block";

  const label = document.createElement("div");
  label.className = "cs-profile-label";
  label.textContent = csT("profile.fingerprint", "Fingerprint");

  const value = document.createElement("div");
  value.className = "cs-profile-fingerprint";
  value.textContent = fp || "unknown";

  wrap.appendChild(label);
  wrap.appendChild(value);
  return wrap;
}

async function csOpenMyProfilePage() {
  // Legacy inline profile page is kept only as a fallback.
  // Toolbar/profile entry points should use the detached window.
  if (typeof csOpenMyProfileModal === "function") {
    return csOpenMyProfileModal();
  }

  // The old inline profile page is kept for compatibility, but the toolbar
  // should always open the detached My Profile window now.
  if (typeof csOpenMyProfileModal === "function") {
    return csOpenMyProfileModal();
  }

  const profilePage = document.getElementById("csProfilePage");
  if (!profilePage) {
    console.warn("Circle Stack My Profile: #csProfilePage missing");
    return;
  }

  const localFeed = document.getElementById("csFeed");
  const fedFeed = document.getElementById("csFederatedFeed");
  const intros = document.getElementById("csIntroductions");
  const compose = document.querySelector(".cs-compose");
  const profileBtn = document.getElementById("csMyProfileBtn");

  if (localFeed) {
    localFeed.hidden = true;
    localFeed.style.display = "none";
  }
  if (fedFeed) {
    fedFeed.hidden = true;
    fedFeed.style.display = "none";
  }
  if (intros) {
    intros.hidden = true;
    intros.style.display = "none";
  }
  if (compose) {
    compose.hidden = true;
    compose.style.display = "none";
  }

  document.querySelectorAll(".cs-intro-toolbar button").forEach(btn => {
    btn.classList.toggle("is-active", btn === profileBtn);
  });

  profilePage.hidden = false;
  profilePage.style.display = "";

  profilePage.innerHTML = `
    <div class="cs-my-profile-card">
      <div class="cs-my-profile-loading">Loading profile…</div>
    </div>
  `;

  let achievementsData = null;
  let me = null;

  try {
    const [achRes, usersRes] = await Promise.all([
      fetch(`${CS_API}/achievements/me`, { credentials: "same-origin" }),
      fetch(`${CS_API}/users`, { credentials: "same-origin" })
    ]);

    achievementsData = await achRes.json();
    const usersData = await usersRes.json();
    me = Array.isArray(usersData.users)
      ? usersData.users.find(u => u && u.is_me)
      : null;
  } catch (err) {
    profilePage.innerHTML = `
      <div class="cs-my-profile-card">
        <div class="cs-modal-title">${csT("profile.myProfile", "My Profile")}</div>
        <div class="cs-modal-text">Could not load profile.</div>
      </div>
    `;
    return;
  }

  const fp = String((me && me.fingerprint) || achievementsData.user_fp || "");
  const name = String((me && me.name) || (me && me.fp_short) || csElideFp(fp) || "Me");
  const role = String((me && me.role) || (achievementsData.stats && achievementsData.stats.role) || "");
  const fpShort = String((me && me.fp_short) || csElideFp(fp));
  const avatarUrl = String((me && me.avatar_url) || "");
  const stats = achievementsData.stats || {};
  const achievements = csAchievementListFrom(achievementsData.achievements);

  profilePage.textContent = "";

  const card = document.createElement("div");
  card.className = "cs-my-profile-card";

  const header = document.createElement("div");
  header.className = "cs-my-profile-header";

  const avatar = document.createElement("div");
  avatar.className = "cs-profile-avatar cs-my-profile-avatar";

  if (avatarUrl) {
    const img = document.createElement("img");
    img.src = avatarUrl;
    img.alt = "";
    avatar.appendChild(img);
  } else {
    avatar.textContent = name.slice(0, 1).toUpperCase();
  }

  const headText = document.createElement("div");
  headText.className = "cs-my-profile-title-wrap";

  const title = document.createElement("div");
  title.className = "cs-my-profile-title";
  title.textContent = csT("profile.myProfile", "My Profile");

  const nameEl = document.createElement("div");
  nameEl.className = "cs-my-profile-name";
  nameEl.textContent = name;

  const sub = document.createElement("div");
  sub.className = "cs-my-profile-sub";
  sub.textContent = role ? `${role} · ${fpShort}` : fpShort;

  const badgeStrip = csRenderAchievementStrip(achievements, {
    profile: true,
    max: 3
  });

  headText.appendChild(title);
  headText.appendChild(nameEl);
  headText.appendChild(sub);
  if (badgeStrip) headText.appendChild(badgeStrip);

  header.appendChild(avatar);
  header.appendChild(headText);
  card.appendChild(header);

  card.appendChild(csProfileFingerprintBlock(fp));

  const statsTitle = document.createElement("div");
  statsTitle.className = "cs-my-profile-section-title";
  statsTitle.textContent = csT("profile.circleStackStats", "Circle Stack stats");
  card.appendChild(statsTitle);

  const statsGrid = document.createElement("div");
  statsGrid.className = "cs-my-profile-stats-grid";
  statsGrid.appendChild(csProfileStat(csT("profile.stats.posts", "Posts"), stats.posts_total));
  statsGrid.appendChild(csProfileStat(csT("profile.stats.publicPosts", "Public posts"), stats.public_posts_total));
  statsGrid.appendChild(csProfileStat(csT("profile.stats.mediaPosts", "Media posts"), stats.media_posts_total));
  statsGrid.appendChild(csProfileStat(csT("profile.stats.repliesWritten", "Replies written"), stats.replies_total));
  statsGrid.appendChild(csProfileStat(csT("profile.stats.reactionsGiven", "Reactions given"), stats.reactions_given_total));
  statsGrid.appendChild(csProfileStat(csT("profile.stats.repliesReceived", "Replies received"), stats.replies_received_total));
  statsGrid.appendChild(csProfileStat(csT("profile.stats.reactionsReceived", "Reactions received"), stats.post_reactions_received_total));
  statsGrid.appendChild(csProfileStat(csT("profile.stats.circleConnections", "Circle connections"), stats.circle_edges_total));
  statsGrid.appendChild(csProfileStat(csT("profile.stats.accountDays", "Account days"), stats.account_age_days));
  card.appendChild(statsGrid);

  const achBlock = csRenderAchievementProfileBlock(achievements);
  if (achBlock) {
    achBlock.classList.add("cs-my-profile-achievements");
    card.appendChild(achBlock);
  } else {
    const empty = document.createElement("div");
    empty.className = "cs-empty";
    empty.textContent = csT("profile.noAchievementsUnlocked", "No achievements unlocked yet.");
    card.appendChild(empty);
  }

  const actions = document.createElement("div");
  actions.className = "cs-modal-actions cs-my-profile-actions";

  if (fp && navigator.clipboard) {
    const copy = document.createElement("button");
    copy.className = "cs-modal-cancel";
    copy.type = "button";
    copy.textContent = csT("profile.copyFingerprint", "Copy fingerprint");
    copy.addEventListener("click", async () => {
      await navigator.clipboard.writeText(fp);
      copy.textContent = csT("common.copied", "Copied");
      setTimeout(() => { copy.textContent = csT("profile.copyFingerprint", "Copy fingerprint"); }, 1200);
    });
    actions.appendChild(copy);
  }

  const back = document.createElement("button");
  back.className = "cs-modal-cancel";
  back.type = "button";
  back.textContent = "Back to feed";
  back.addEventListener("click", () => {
    if (typeof csSetFeedMode === "function") {
      csSetFeedMode(csFeedMode);
    } else {
      location.reload();
    }
  });
  actions.appendChild(back);

  card.appendChild(actions);
  profilePage.appendChild(card);
  profilePage.scrollIntoView({ block: "start", behavior: "smooth" });
}


function csInitFeedTabs() {
  if (window.CircleStackFeedModes &&
      typeof window.CircleStackFeedModes.initButtons === "function" &&
      window.CircleStackFeedModes.initButtons(csSetFeedMode)) {
    return;
  }

  const localBtn = document.getElementById("csLocalFeedBtn");
  const fedBtn = document.getElementById("csFederatedBtn");

  if (localBtn) {
    localBtn.addEventListener("click", () => {
      csSetFeedMode("local");
    });
  }

  if (fedBtn) {
    fedBtn.addEventListener("click", () => {
      csSetFeedMode("federated");
    });
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
  const achievements = csAchievementListFrom(
    (user && user.achievements) ||
    fallback.achievements ||
    fallback.owner_badges ||
    fallback.actor_badges ||
    []
  );

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

  const profileBadgeStrip = csRenderAchievementStrip(achievements, {
    profile: true,
    max: 3
  });
  if (profileBadgeStrip) {
    titleWrap.appendChild(profileBadgeStrip);
  }

  head.appendChild(avatar);
  head.appendChild(titleWrap);

  const body = document.createElement("div");
  body.className = "cs-profile-body";

  const fpLabel = document.createElement("div");
  fpLabel.className = "cs-profile-label";
  fpLabel.textContent = csT("profile.fingerprint", "Fingerprint");

  const fpValue = document.createElement("div");
  fpValue.className = "cs-profile-fingerprint";
  fpValue.textContent = safeFp || fpShort || "unknown";

  body.appendChild(fpLabel);
  body.appendChild(fpValue);

  const achievementBlock = csRenderAchievementProfileBlock(achievements);
  if (achievementBlock) {
    body.appendChild(achievementBlock);
  }

  const actions = document.createElement("div");
  actions.className = "cs-modal-actions";

  if (safeFp && navigator.clipboard) {
    const copy = document.createElement("button");
    copy.className = "cs-modal-cancel";
    copy.type = "button";
    copy.textContent = csT("profile.copyFingerprint", "Copy fingerprint");
    copy.addEventListener("click", async () => {
      await navigator.clipboard.writeText(safeFp);
      copy.textContent = csT("common.copied", "Copied");
      setTimeout(() => { copy.textContent = csT("profile.copyFingerprint", "Copy fingerprint"); }, 1200);
    });
    actions.appendChild(copy);
  }

  const closeBtn = document.createElement("button");
  closeBtn.className = "cs-modal-cancel";
  closeBtn.type = "button";
  closeBtn.textContent = csT("common.close", "Close");
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


function csClampZoom(v, min, max) {
  return Math.max(min, Math.min(max, v));
}

function csZoomableImageFromTarget(target) {
  if (!target || typeof target.closest !== "function") return null;

  return target.closest([
    "img.cs-post-media",
    "img.cs-reply-media",
    "img.cs-federated-preview-img",
    "img.cs-compose-preview-img",
    "img.cs-memory-node-image",
    "img.cs-memory-node-item-img"
  ].join(","));
}

function csOpenZoomableImage(src, alt = "") {
  const imageSrc = String(src || "").trim();
  if (!imageSrc) return;

  document.querySelectorAll(".cs-lightbox").forEach((el) => el.remove());

  const oldBodyOverflow = document.body.style.overflow;
  document.body.style.overflow = "hidden";

  const backdrop = document.createElement("div");
  backdrop.className = "cs-lightbox cs-lightbox-zoom";
  backdrop.setAttribute("role", "dialog");
  backdrop.setAttribute("aria-modal", "true");
  backdrop.setAttribute("aria-label", "Image preview");

  const toolbar = document.createElement("div");
  toolbar.className = "cs-lightbox-toolbar";

  const hint = document.createElement("div");
  hint.className = "cs-lightbox-hint";
  hint.textContent = "Wheel / pinch to zoom • drag to move";

  const reset = document.createElement("button");
  reset.className = "cs-lightbox-reset";
  reset.type = "button";
  reset.textContent = "Reset";

  const close = document.createElement("button");
  close.className = "cs-lightbox-close";
  close.type = "button";
  close.textContent = "×";
  close.setAttribute("aria-label", "Close image preview");

  toolbar.appendChild(hint);
  toolbar.appendChild(reset);

  const stage = document.createElement("div");
  stage.className = "cs-lightbox-stage";

  const img = document.createElement("img");
  img.className = "cs-lightbox-img";
  img.src = imageSrc;
  img.alt = alt || "";
  img.decoding = "async";
  img.draggable = false;

  stage.appendChild(img);
  backdrop.appendChild(toolbar);
  backdrop.appendChild(close);
  backdrop.appendChild(stage);

  let scale = 1;
  let x = 0;
  let y = 0;
  let pointerDownStart = null;
  let pointerMovedSinceDown = false;
  let pointerDownWasImage = false;
  const pointers = new Map();
  let lastPinchDistance = 0;
  let lastPinchCenter = null;

  function apply() {
    if (scale <= 1.001) {
      scale = 1;
      x = 0;
      y = 0;
    }

    img.style.transform = `translate(${x}px, ${y}px) scale(${scale})`;
    hint.textContent = scale <= 1.001
      ? "Wheel / pinch to zoom • drag to move"
      : `Zoom ${scale.toFixed(1)}× • drag to move`;
  }

  function resetZoom() {
    scale = 1;
    x = 0;
    y = 0;
    lastPinchDistance = 0;
    lastPinchCenter = null;
    apply();
  }

  function closeLightbox() {
    document.removeEventListener("keydown", onKeyDown);
    document.body.style.overflow = oldBodyOverflow;
    backdrop.remove();
  }

  function distance(a, b) {
    return Math.hypot(a.x - b.x, a.y - b.y);
  }

  function center(a, b) {
    return {
      x: (a.x + b.x) / 2,
      y: (a.y + b.y) / 2
    };
  }

  function onKeyDown(ev) {
    if (ev.key === "Escape") {
      closeLightbox();
    } else if (ev.key === "0") {
      resetZoom();
    }
  }

  close.addEventListener("pointerdown", (ev) => ev.stopPropagation());
  close.addEventListener("click", (ev) => {
    ev.preventDefault();
    ev.stopPropagation();
    closeLightbox();
  });

  reset.addEventListener("pointerdown", (ev) => ev.stopPropagation());
  reset.addEventListener("click", (ev) => {
    ev.preventDefault();
    ev.stopPropagation();
    resetZoom();
  });

  backdrop.addEventListener("click", (ev) => {
    if (ev.target === backdrop) closeLightbox();
  });

  function zoomFromWheelDelta(deltaY) {
    const factor = deltaY < 0 ? 1.14 : 0.88;
    scale = csClampZoom(scale * factor, 1, 6);
    apply();
  }

  backdrop.addEventListener("wheel", (ev) => {
    ev.preventDefault();
    ev.stopPropagation();
    zoomFromWheelDelta(ev.deltaY);
  }, { passive: false, capture: true });

  img.addEventListener("click", (ev) => {
    ev.preventDefault();
    ev.stopPropagation();

    if (!pointerMovedSinceDown) {
      closeLightbox();
    }
  });

  stage.addEventListener("dblclick", (ev) => {
    ev.preventDefault();
    ev.stopPropagation();
  });

  stage.addEventListener("pointerdown", (ev) => {
    stage.setPointerCapture(ev.pointerId);
    pointers.set(ev.pointerId, { x: ev.clientX, y: ev.clientY });
    pointerDownStart = { x: ev.clientX, y: ev.clientY };
    pointerMovedSinceDown = false;
    pointerDownWasImage = ev.target === img || (
      ev.target &&
      typeof ev.target.closest === "function" &&
      ev.target.closest("img.cs-lightbox-img")
    );
    stage.classList.add("is-dragging");

    if (pointers.size === 2) {
      const pts = Array.from(pointers.values());
      lastPinchDistance = distance(pts[0], pts[1]);
      lastPinchCenter = center(pts[0], pts[1]);
    }
  });

  stage.addEventListener("pointermove", (ev) => {
    const old = pointers.get(ev.pointerId);
    if (!old) return;

    const next = { x: ev.clientX, y: ev.clientY };
    pointers.set(ev.pointerId, next);

    if (pointerDownStart &&
        Math.hypot(next.x - pointerDownStart.x, next.y - pointerDownStart.y) > 6) {
      pointerMovedSinceDown = true;
    }

    if (pointers.size >= 2) {
      pointerMovedSinceDown = true;
      const pts = Array.from(pointers.values()).slice(0, 2);
      const d = distance(pts[0], pts[1]);
      const c = center(pts[0], pts[1]);

      if (lastPinchDistance > 0) {
        scale = csClampZoom(scale * (d / lastPinchDistance), 1, 6);
      }

      if (lastPinchCenter && scale > 1.001) {
        x += c.x - lastPinchCenter.x;
        y += c.y - lastPinchCenter.y;
      }

      lastPinchDistance = d;
      lastPinchCenter = c;
      apply();
      return;
    }

    if (scale > 1.001) {
      x += next.x - old.x;
      y += next.y - old.y;
      apply();
    }
  });

  function pointerDone(ev) {
    const wasFinalPointer = pointers.size <= 1;

    pointers.delete(ev.pointerId);

    const cleanImageClick =
      wasFinalPointer &&
      pointerDownWasImage &&
      !pointerMovedSinceDown &&
      pointers.size === 0;

    if (pointers.size < 2) {
      lastPinchDistance = 0;
      lastPinchCenter = null;
    }

    if (pointers.size === 0) {
      stage.classList.remove("is-dragging");
      pointerDownStart = null;
      pointerDownWasImage = false;
      pointerMovedSinceDown = false;
    }

    if (cleanImageClick) {
      closeLightbox();
    }
  }

  stage.addEventListener("pointerup", pointerDone);
  stage.addEventListener("pointercancel", pointerDone);
  stage.addEventListener("pointerleave", pointerDone);

  document.addEventListener("keydown", onKeyDown);
  document.body.appendChild(backdrop);
  close.focus();
  apply();
}

document.addEventListener("click", (ev) => {
  if (ev.target && typeof ev.target.closest === "function" && ev.target.closest(".cs-lightbox")) {
    return;
  }

  const img = csZoomableImageFromTarget(ev.target);
  if (!img) return;

  ev.preventDefault();
  ev.stopImmediatePropagation();

  csOpenZoomableImage(
    img.currentSrc || img.src || "",
    img.alt || "Circle Stack media"
  );
});

document.addEventListener("keydown", (ev) => {
  if (ev.key !== "Enter" && ev.key !== " ") return;

  if (ev.target && typeof ev.target.closest === "function" && ev.target.closest(".cs-lightbox")) {
    return;
  }

  const img = csZoomableImageFromTarget(ev.target);
  if (!img) return;

  ev.preventDefault();

  csOpenZoomableImage(
    img.currentSrc || img.src || "",
    img.alt || "Circle Stack media"
  );
});



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
    chip.setAttribute("aria-label", names ? `${reaction} ${names}` : reaction);

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
  trigger.textContent = post.my_reaction ? `${post.my_reaction} ${csT("reaction.react", "React")}` : `🙂 ${csT("reaction.react", "React")}`;
  trigger.title = csT("reaction.reactToPost", "React to this post");

  const menu = document.createElement("div");
  menu.className = "cs-reaction-menu";

  for (const reaction of CS_REACTIONS) {
    const isMine = post.my_reaction === reaction;

    const btn = document.createElement("button");
    btn.className = "cs-reaction-menu-button";
    if (isMine) btn.classList.add("is-active");
    btn.type = "button";
    btn.textContent = reaction;
    btn.setAttribute("aria-label", isMine ? csT("reaction.remove", "Remove reaction") : csT("reaction.reactEmoji", { reaction }, `React ${reaction}`));

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
  toggle.textContent = replies.length ? csT("reply.count", { count: replies.length }, `Reply (${replies.length})`) : csT("reply.action", "Reply");

  const updateReplyCount = () => {
    toggle.textContent = replies.length ? csT("reply.count", { count: replies.length }, `Reply (${replies.length})`) : csT("reply.action", "Reply");
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
    chip.setAttribute("aria-label", names ? `${reaction} ${names}` : reaction);

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
  trigger.textContent = reply.my_reaction ? `${reply.my_reaction} ${csT("reaction.react", "React")}` : `🙂 ${csT("reaction.react", "React")}`;
  trigger.title = csT("reaction.reactToReply", "React to this reply");

  const menu = document.createElement("div");
  menu.className = "cs-reaction-menu";

  for (const reaction of CS_REACTIONS) {
    const isMine = reply.my_reaction === reaction;

    const btn = document.createElement("button");
    btn.className = "cs-reaction-menu-button";
    if (isMine) btn.classList.add("is-active");
    btn.type = "button";
    btn.textContent = reaction;
    btn.setAttribute("aria-label", isMine ? csT("reaction.remove", "Remove reaction") : csT("reaction.reactEmoji", { reaction }, `React ${reaction}`));

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
  avatar.title = csT("profile.openPersonCard", "Open person card");

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
  name.textContent = reply.actor_display_name || reply.actor_fp_short || csT("common.unknown", "unknown");
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
    edit.textContent = csT("common.edit", "Edit");
    edit.addEventListener("click", () => {
      csOpenReplyEdit(row, reply);
    });

    const del = document.createElement("button");
    del.type = "button";
    del.textContent = csT("common.delete", "Delete");
    del.addEventListener("click", async () => {
      if (!confirm(csT("reply.deleteConfirm", "Delete this reply?"))) return;

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
  label.textContent = csT("mention.tagged", "Tagged:");
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
    <div class="cs-modal-title">${csT("mention.tagFriend", "Tag friend")}</div>
    <div class="cs-modal-text">${csT("mention.pickPeopleReply", "Pick people to tag in this reply.")}</div>
    <input id="csReplyMentionSearch" placeholder="${csT("mention.searchPeople", "Search people...")}">
    <div id="csReplyMentionResults" class="cs-mention-results"></div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel" type="button">${csT("common.close", "Close")}</button>
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
      empty.textContent = csT("mention.noPeopleFound", "No people found");
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
    toggle.textContent = count ? csT("reply.count", { count }, `Reply (${count})`) : csT("reply.action", "Reply");
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
  mediaInput.placeholder = csT("reply.optionalImagePath", "Optional image path");

  const browse = document.createElement("button");
  browse.className = "cs-reply-browse";
  browse.type = "button";
  browse.textContent = csT("common.browse", "Browse");

  const actions = document.createElement("div");
  actions.className = "cs-reply-edit-actions";

  const cancel = document.createElement("button");
  cancel.className = "cs-reply-browse";
  cancel.type = "button";
  cancel.textContent = csT("common.cancel", "Cancel");

  const save = document.createElement("button");
  save.className = "cs-reply-submit";
  save.type = "button";
  save.textContent = csT("common.save", "Save");

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
    const ok = confirm(csT("reply.discardDraft", "Discard this reply draft?"));
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
  close.title = csT("reply.closeComposer", "Close reply composer");
  close.setAttribute("aria-label", csT("reply.closeComposer", "Close reply composer"));
  close.addEventListener("click", () => {
    csRequestCloseReplyComposer(box);
  });

  const textarea = document.createElement("textarea");
  textarea.className = "cs-reply-textarea";
  textarea.placeholder = csT("reply.writePlaceholder", "Write a reply...");

  const mediaRow = document.createElement("div");
  mediaRow.className = "cs-reply-media-row";

  const mediaInput = document.createElement("input");
  mediaInput.className = "cs-reply-media-input";
  mediaInput.placeholder = csT("reply.optionalImagePath", "Optional image path");

  const browse = document.createElement("button");
  browse.className = "cs-reply-browse";
  browse.type = "button";
  browse.textContent = csT("common.browse", "Browse");

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
      remove.title = csT("mention.removeTag", "Remove tag");
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
  tagBtn.textContent = csT("mention.tagFriend", "Tag friend");
  tagBtn.addEventListener("click", async () => {
    await csOpenReplyMentionPicker(selectedMentions, (next) => {
      selectedMentions = next;
      renderSelectedReplyMentions();
    });
  });

  const submit = document.createElement("button");
  submit.className = "cs-reply-submit";
  submit.type = "button";
  submit.textContent = csT("common.send", "Send");

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
  label.textContent = csT("mention.tagged", "Tagged:");
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
    remove.title = csT("mention.removeTag", "Remove tag");
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
    <div class="cs-modal-title">${csT("mention.tagFriend", "Tag friend")}</div>
    <div class="cs-modal-text">${csT("mention.pickPeopleCircle", "Pick people from your Circle / contacts.")}</div>
    <input id="csMentionSearch" placeholder="${csT("mention.searchPeople", "Search people...")}">
    <div id="csMentionResults" class="cs-mention-results"></div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel" type="button">${csT("common.close", "Close")}</button>
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
      empty.textContent = csT("mention.noPeopleFound", "No people found");
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
  author.textContent = post.owner_display_name || post.owner_fp_short || csT("common.anon", "anon");
  author.title = csT("profile.openPersonCard", "Open person card");
  author.addEventListener("click", () => {
    csOpenPersonCard(post.owner_fp || "", {
      display_name: post.owner_display_name || "",
      fp_short: post.owner_fp_short || "",
      avatar_url: post.owner_avatar_url || "",
      achievements: post.owner_badges || []
    });
  });

  const authorWrap = document.createElement("div");
  authorWrap.className = "cs-post-author-wrap";
  authorWrap.appendChild(author);

  const postBadgeStrip = csRenderAchievementStrip(post.owner_badges || [], { max: 3 });
  if (postBadgeStrip) {
    authorWrap.appendChild(postBadgeStrip);
  }

  header.appendChild(authorWrap);

  const del = document.createElement("button");
  del.className = "cs-post-delete";
  del.type = "button";
  del.textContent = "✕";
  del.title = csT("post.delete", "Delete post");
  del.setAttribute("aria-label", csT("post.delete", "Delete post"));
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
  close.title = csT("compose.close", "Close composer");
  close.setAttribute("aria-label", csT("compose.close", "Close composer"));

  close.addEventListener("click", () => {
    if (csComposeHasDraft()) {
      const ok = confirm(csT("compose.discardDraft", "Discard this post draft?"));
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
    title.textContent = csT("deletePost.title", "Delete post?");

    const text = document.createElement("div");
    text.className = "cs-modal-text";
    text.textContent = csT("deletePost.text", "This cannot be undone.");

    const actions = document.createElement("div");
    actions.className = "cs-modal-actions";

    const cancel = document.createElement("button");
    cancel.className = "cs-modal-cancel";
    cancel.type = "button";
    cancel.textContent = csT("common.cancel", "Cancel");

    const del = document.createElement("button");
    del.className = "cs-modal-delete";
    del.type = "button";
    del.textContent = csT("common.delete", "Delete");

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




// KNOWN_REMOTE_ORIGINS_UI_PANEL_PATCH_V1

function csFormatOriginEpoch(epoch) {
  const n = Number(epoch || 0);
  if (!n) return "—";
  try {
    return new Date(n * 1000).toLocaleString();
  } catch (_) {
    return "—";
  }
}

function csOriginDisplayName(origin) {
  return (
    (origin && origin.display_name) ||
    (origin && origin.public_base_url) ||
    (origin && origin.origin_short) ||
    csElideFp(origin && origin.origin_nas) ||
    "Remote NAS"
  );
}

async function csFetchKnownOrigins() {
  const res = await fetch(`${CS_API}/federated/origins`, {
    credentials: "same-origin",
    cache: "no-store"
  });

  const data = await res.json().catch(() => ({ ok: false }));
  if (!res.ok || !data.ok) {
    throw new Error(data.detail || data.error || `HTTP ${res.status}`);
  }

  return Array.isArray(data.items) ? data.items : [];
}

async function csFetchFederationStatus() {
  const res = await fetch(`${CS_API}/federation/status`, {
    credentials: "same-origin",
    cache: "no-store"
  });

  const data = await res.json().catch(() => ({ ok: false }));
  if (!res.ok || !data.ok) {
    throw new Error(data.detail || data.error || `HTTP ${res.status}`);
  }

  return data;
}

async function csSetKnownOriginMuted(originNas, muted) {
  const res = await fetch(`${CS_API}/federated/origins/my-mute`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      origin_nas: originNas,
      muted: !!muted
    })
  });

  const data = await res.json().catch(() => ({ ok: false }));
  if (!res.ok || !data.ok) {
    const err = data.error || data.detail || `HTTP ${res.status}`;
    throw new Error(err);
  }

  return data;
}

async function csRefreshMutedOriginSet() {
  try {
    const origins = await csFetchKnownOrigins();
    csMutedFederatedOrigins = new Set(
      origins
        .filter((origin) => origin && (origin.my_muted || origin.my_hidden))
        .map((origin) => String(origin.origin_nas || "").trim())
        .filter(Boolean)
    );
    return origins;
  } catch (_) {
    csMutedFederatedOrigins = new Set();
    return [];
  }
}

async function csSetKnownOriginEnabled(originNas, enabled) {
  const res = await fetch(`${CS_API}/federated/origins/set-enabled`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      origin_nas: originNas,
      enabled: !!enabled
    })
  });

  const data = await res.json().catch(() => ({ ok: false }));
  if (!res.ok || !data.ok) {
    const err = data.error || data.detail || `HTTP ${res.status}`;
    throw new Error(err);
  }

  return data;
}

function csRenderKnownOriginRow(origin, refresh) {
  const row = document.createElement("article");
  row.className = "cs-known-origin-row";
  if (!origin.enabled) row.classList.add("is-disabled");
  if (origin.my_muted || origin.my_hidden) row.classList.add("is-muted-for-me"); // USER_ORIGIN_PREFS_MUTED_CLASS_PATCH_V1

  const main = document.createElement("div");
  main.className = "cs-known-origin-main";

  const title = document.createElement("div");
  title.className = "cs-known-origin-title";
  title.textContent = csOriginDisplayName(origin);

  const url = document.createElement("div");
  url.className = "cs-known-origin-url";
  url.textContent = origin.public_base_url || csT("origins.noPublicUrl", "No public URL saved");

  const meta = document.createElement("div");
  meta.className = "cs-known-origin-meta";
  meta.textContent = [
    csT("origins.meta.origin", { value: origin.origin_short || csElideFp(origin.origin_nas) || "unknown" }, `origin: ${origin.origin_short || csElideFp(origin.origin_nas) || "unknown"}`),
    csT("origins.meta.source", { value: origin.source || "unknown" }, `source: ${origin.source || "unknown"}`),
    csT("origins.meta.added", { value: csFormatOriginEpoch(origin.first_seen_epoch) }, `added: ${csFormatOriginEpoch(origin.first_seen_epoch)}`),
    origin.enabled ? csT("origins.enabled", "enabled") : csT("origins.disabled", "disabled"),
    origin.my_muted ? csT("origins.mutedForMe", "muted for me") : csT("origins.visibleToMe", "visible to me")
  ].join(" · ");

  main.appendChild(title);
  main.appendChild(url);
  main.appendChild(meta);

  const actions = document.createElement("div");
  actions.className = "cs-known-origin-actions";

  if (navigator.clipboard && origin.origin_nas) {
    const copyOrigin = document.createElement("button");
    copyOrigin.className = "cs-modal-cancel";
    copyOrigin.type = "button";
    copyOrigin.textContent = csT("origins.copyOrigin", "Copy origin");
    copyOrigin.addEventListener("click", async () => {
      await navigator.clipboard.writeText(origin.origin_nas);
      copyOrigin.textContent = csT("common.copied", "Copied");
      setTimeout(() => { copyOrigin.textContent = csT("origins.copyOrigin", "Copy origin"); }, 1200);
    });
    actions.appendChild(copyOrigin);
  }

  if (navigator.clipboard && origin.public_base_url) {
    const copyUrl = document.createElement("button");
    copyUrl.className = "cs-modal-cancel";
    copyUrl.type = "button";
    copyUrl.textContent = csT("origins.copyUrl", "Copy URL");
    copyUrl.addEventListener("click", async () => {
      await navigator.clipboard.writeText(origin.public_base_url);
      copyUrl.textContent = csT("common.copied", "Copied");
      setTimeout(() => { copyUrl.textContent = csT("origins.copyUrl", "Copy URL"); }, 1200);
    });
    actions.appendChild(copyUrl);
  }

  const mute = document.createElement("button");
  mute.className = origin.my_muted ? "cs-modal-cancel" : "cs-modal-cancel";
  mute.type = "button";
  mute.textContent = origin.my_muted ? csT("origins.unmuteForMe", "Unmute for me") : csT("origins.muteForMe", "Mute for me");
  mute.title = "Personal feed setting. This does not stop server polling for other users.";
  mute.addEventListener("click", async () => {
    mute.disabled = true;

    try {
      await csSetKnownOriginMuted(origin.origin_nas, !origin.my_muted);
      await refresh();

      if (csFeedMode === "federated" || csFeedMode === "my_circle") {
        await csLoadFederatedFeed();
      }
    } catch (err) {
      await csShowMessageDialog({
        title: "Could not update personal mute",
        message: err && err.message ? err.message : String(err),
        kind: "error"
      });
      mute.disabled = false;
    }
  });

  actions.appendChild(mute);

  const toggle = document.createElement("button");
  toggle.className = origin.enabled ? "cs-modal-delete" : "cs-modal-cancel";
  toggle.type = "button";
  toggle.textContent = origin.enabled ? csT("origins.disableGlobally", "Disable globally") : csT("origins.enableGlobally", "Enable globally");
  toggle.title = "Admin-only global polling control";

  toggle.addEventListener("click", async () => {
    toggle.disabled = true;

    try {
      await csSetKnownOriginEnabled(origin.origin_nas, !origin.enabled);
      await refresh();
    } catch (err) {
      await csShowMessageDialog({
        title: csT("origins.updateFailedTitle", "Could not update origin"),
        message:
          "Global enable/disable is admin-only. Normal users should later get a personal mute/unfollow control.",
        detail: err && err.message ? err.message : String(err),
        kind: "error"
      });
      toggle.disabled = false;
    }
  });

  actions.appendChild(toggle);

  row.appendChild(main);
  row.appendChild(actions);
  return row;
}

async function csOpenKnownOriginsModal() {
  const old = document.querySelector(".cs-known-origins-backdrop");
  if (old) old.remove();

  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop cs-known-origins-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal cs-known-origins-modal";
  modal.setAttribute("role", "dialog");
  modal.setAttribute("aria-modal", "true");

  const head = document.createElement("div");
  head.className = "cs-known-origins-head";

  const titleWrap = document.createElement("div");

  const title = document.createElement("div");
  title.className = "cs-modal-title";
  title.textContent = csT("origins.modalTitle", "Known remote origins");

  const sub = document.createElement("div");
  sub.className = "cs-modal-text";
  sub.textContent =
    csT("origins.modalDesc", "Remote NAS origins this server knows about. Personal mute only affects your feed. Admin global disable stops polling for everyone.");

  titleWrap.appendChild(title);
  titleWrap.appendChild(sub);

  const closeX = document.createElement("button");
  closeX.className = "cs-media-close";
  closeX.type = "button";
  closeX.textContent = "×";
  closeX.setAttribute("aria-label", csT("common.close", "Close"));

  head.appendChild(titleWrap);
  head.appendChild(closeX);

  const status = document.createElement("div");
  status.className = "cs-known-origins-status";
  status.textContent = "Loading…";

  const federationStatus = document.createElement("div");
  federationStatus.className = "cs-known-origins-status";
  federationStatus.textContent = "Loading federation status…";

  const list = document.createElement("div");
  list.className = "cs-known-origins-list";

  const actions = document.createElement("div");
  actions.className = "cs-modal-actions";

  const refreshBtn = document.createElement("button");
  refreshBtn.className = "cs-modal-cancel";
  refreshBtn.type = "button";
  refreshBtn.textContent = csT("common.refresh", "Refresh");

  const closeBtn = document.createElement("button");
  closeBtn.className = "cs-modal-cancel";
  closeBtn.type = "button";
  closeBtn.textContent = csT("common.close", "Close");

  actions.appendChild(refreshBtn);
  actions.appendChild(closeBtn);

  modal.appendChild(head);
  modal.appendChild(status);
  modal.appendChild(federationStatus);
  modal.appendChild(list);
  modal.appendChild(actions);

  backdrop.appendChild(modal);
  document.body.appendChild(backdrop);

  function close() {
    backdrop.remove();
  }

  closeX.addEventListener("click", close);
  closeBtn.addEventListener("click", close);
  backdrop.addEventListener("click", (ev) => {
    if (ev.target === backdrop) close();
  });

  async function refresh() {
    status.textContent = "Loading…";
    list.textContent = "";

    try {
      const origins = await csFetchKnownOrigins();

      if (origins.length) {
        const enabledCount = origins.filter((origin) => origin && origin.enabled !== false).length;
        const mutedCount = origins.filter((origin) => origin && (origin.my_muted || origin.my_hidden)).length;
        const missingUrlCount = origins.filter((origin) => origin && !String(origin.public_base_url || "").trim()).length;

        const bits = [
          csT("origins.summary.knownOrigins", { count: origins.length }, `${origins.length} known origin${origins.length === 1 ? "" : "s"}`),
          csT("origins.summary.enabled", { count: enabledCount }, `${enabledCount} enabled`),
          mutedCount ? `${mutedCount} muted for me` : csT("origins.summary.noneMuted", "none muted"),
          missingUrlCount ? `${missingUrlCount} without public URL` : csT("origins.summary.allHavePublicUrl", "all have public URL")
        ];

        status.textContent = bits.join(" · ");
      } else {
        status.textContent = csT("origins.noneYet", "No known remote origins yet.");
      }

      try {
        const fed = await csFetchFederationStatus();
        federationStatus.textContent = [
          csT("origins.federationStatus.known", { count: Number(fed.known_origins || 0) }, `Federation status: ${Number(fed.known_origins || 0)} known`),
          csT("origins.federationStatus.enabled", { count: Number(fed.enabled_origins || 0) }, `${Number(fed.enabled_origins || 0)} enabled`),
          csT("origins.federationStatus.mutedForMe", { count: Number(fed.muted_for_me || 0) }, `${Number(fed.muted_for_me || 0)} muted for me`),
          `${Number(fed.federated_local_reactions_for_me || 0)} remembered reaction${Number(fed.federated_local_reactions_for_me || 0) === 1 ? "" : "s"}`
        ].join(" · ");
      } catch (_) {
        federationStatus.textContent = "Federation status unavailable.";
      }

      if (!origins.length) {
        const empty = document.createElement("div");
        empty.className = "cs-empty";
        empty.textContent = csT(
          "origins.emptyHelp",
          "Add a federated person or follow a remote NAS to create the first known origin."
        );
        list.appendChild(empty);
        return;
      }

      for (const origin of origins) {
        list.appendChild(csRenderKnownOriginRow(origin, refresh));
      }
    } catch (err) {
      status.textContent = csT("origins.loadFailed", "Could not load known origins.");
      await csShowMessageDialog({
        title: csT("origins.loadFailedTitle", "Could not load known origins"),
        message: err && err.message ? err.message : String(err),
        kind: "error"
      });
    }
  }

  refreshBtn.addEventListener("click", refresh);

  await refresh();
}

// Expose federated feed helpers for inline button handlers and browser debugging.
window.csSetFeedMode = csSetFeedMode;
window.csLoadFederatedFeed = csLoadFederatedFeed;


// FEDERATED_LOCAL_REACTIONS_SERVER_UI_V1
let csFederatedLocalReactionServerCache = new Map();

async function csLoadFederatedLocalReactionsForEvents(events) {
  const eventIds = (Array.isArray(events) ? events : [])
    .filter((ev) => ev && ev.event_type === "circle.post.created")
    .map((ev) => String(ev.event_id || "").trim())
    .filter(Boolean);

  if (!eventIds.length) {
    csFederatedLocalReactionServerCache = new Map();
    return;
  }

  try {
    const res = await fetch(`${CS_API}/federated/reactions/mine/list`, {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ event_ids: eventIds })
    });

    const data = await res.json().catch(() => ({ ok: false }));
    if (!res.ok || !data.ok) {
      throw new Error(data.error || `HTTP ${res.status}`);
    }

    const next = new Map();

    for (const item of Array.isArray(data.items) ? data.items : []) {
      const eventId = String(item.remote_event_id || "").trim();
      const reaction = String(item.reaction || "").trim();

      if (!eventId || !reaction) continue;

      next.set(eventId, {
        reaction,
        origin_nas: String(item.origin_nas || ""),
        federation_event_id: String(item.federation_event_id || ""),
        created_epoch: Number(item.created_epoch || 0),
        updated_epoch: Number(item.updated_epoch || 0),
        server_side: true
      });
    }

    csFederatedLocalReactionServerCache = next;
  } catch (err) {
    console.warn("Could not load server-side federated local reactions", err);
    csFederatedLocalReactionServerCache = new Map();
  }
}

async function csStoreFederatedLocalReactionOnServer(ev, reaction, data = {}) {
  const remote_event_id = String(ev && ev.event_id ? ev.event_id : "").trim();
  if (!remote_event_id) return;

  const body = {
    remote_event_id,
    origin_nas: String(ev && ev.origin_nas ? ev.origin_nas : ""),
    reaction: String(reaction || ""),
    federation_event_id: data && data.federation_event_id
      ? String(data.federation_event_id)
      : ""
  };

  const res = await fetch(`${CS_API}/federated/reactions/mine/set`, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });

  const out = await res.json().catch(() => ({ ok: false }));
  if (!res.ok || !out.ok) {
    throw new Error(out.error || `HTTP ${res.status}`);
  }
}

function csRememberFederatedLocalReaction(ev, reaction, data = {}) {
  const eventId = String(ev && ev.event_id ? ev.event_id : "").trim();
  const value = String(reaction || "").trim();

  if (!eventId || !value) return;

  csFederatedLocalReactionServerCache.set(eventId, {
    reaction: value,
    origin_nas: String(ev && ev.origin_nas ? ev.origin_nas : ""),
    updated_epoch: Math.floor(Date.now() / 1000),
    federation_event_id: data && data.federation_event_id
      ? String(data.federation_event_id)
      : "",
    server_side: false
  });

  csStoreFederatedLocalReactionOnServer(ev, value, data).then(() => {
    const item = csFederatedLocalReactionServerCache.get(eventId);
    if (item) {
      item.server_side = true;
      csFederatedLocalReactionServerCache.set(eventId, item);
    }
  }).catch((err) => {
    console.warn("Could not persist federated local reaction on server", err);
  });
}

function csFederatedLocalReactionFor(ev) {
  const eventId = String(ev && ev.event_id ? ev.event_id : "").trim();
  if (!eventId) return null;

  const serverItem = csFederatedLocalReactionServerCache.get(eventId);
  if (serverItem && serverItem.reaction) return serverItem;

  // Transitional fallback for reactions made before the server-side table existed.
  try {
    const raw = localStorage.getItem("circlestack:federatedLocalReactions:v1");
    const parsed = raw ? JSON.parse(raw) : {};
    const item = parsed && parsed[eventId] ? parsed[eventId] : null;
    if (item && item.reaction) return item;
  } catch (_) {}

  return null;
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
  csInitFeedTabs();

  const knownOriginsBtn = document.getElementById("csKnownOriginsBtn");
  if (knownOriginsBtn) {
    knownOriginsBtn.addEventListener("click", csOpenKnownOriginsModal);
  }

  await csSetFeedMode("local");
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
          <div class="cs-modal-title">${csT("media.chooseTitle", "Choose media")}</div>
          <div class="cs-media-path">/</div>
        </div>
        <button class="cs-media-close" type="button" aria-label="${csT("common.close", "Close")}">×</button>
      </div>
      <div class="cs-media-body"></div>
      <div class="cs-modal-actions">
        <button class="cs-modal-cancel" type="button">${csT("common.cancel", "Cancel")}</button>
        <button class="cs-media-choose" type="button">${csT("common.choose", "Choose")}</button>
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

      body.textContent = csT("common.loading", "Loading…");

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
  clear.textContent = csT("media.removeImage", "Remove image");

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
  csOpenZoomableImage(src, "Circle Stack media");
}

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
    <div class="cs-modal-title">${csT("intro.title", "Introduce people")}</div>
    <div class="cs-modal-text">${csT("intro.text", "Pick two people you know.")}</div>
    <div class="cs-intro-grid">
      <select id="csIntroA"></select>
      <select id="csIntroB"></select>
    </div>
    <textarea id="csIntroMsg" placeholder="${csT("intro.optionalMessage", "Optional message")}"></textarea>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel">${csT("common.cancel", "Cancel")}</button>
      <button class="cs-modal-delete">${csT("common.send", "Send")}</button>
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
    <div class="cs-modal-title">${csT("circle.myCircle", "My Circle")}</div>
    <div class="cs-modal-body" id="csMyCircleBody"></div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel">${csT("common.close", "Close")}</button>
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
    body.innerHTML = `<div class="cs-empty">${csT("circle.empty", "Your circle is empty.")}</div>`;
  } else {
    for (const it of items) {
      const row = document.createElement("div");
      row.className = "cs-circle-row";

const name = csUserLabel(it.fp, usersByFp);

const badge =
    it.source === "circle" ? csT("circle.source.circle", "Circle") :
        it.source === "manual" ? csT("circle.source.manual", "Manual") :
            csT("circle.source.workspace", "Workspace");

row.innerHTML = `
  <span>${name}</span>
  <span class="cs-badge">${badge}</span>
  <button class="cs-circle-remove">${csT("circle.forget", "Forget")}</button>
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
    <div class="cs-modal-title">${csT("circle.removeTitle", "Remove from Circle?")}</div>
    <div class="cs-modal-text">
      ${csT("circle.removeTextHtml", { name: `<b>${name}</b>` }, `This will remove <b>${name}</b> from your circle.`)}
    </div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel">${csT("common.cancel", "Cancel")}</button>
      <button class="cs-modal-delete">${csT("common.remove", "Remove")}</button>
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
    <div class="cs-modal-title">${csT("people.findTitle", "Find people")}</div>
    <div class="cs-modal-text">${csT("people.findText", "Search users and send contact requests.")}</div>
    <div class="cs-modal-title" style="font-size:16px;margin-top:12px">${csT("people.requests", "Requests")}</div>
    <div id="csContactRequests"></div>
    <input id="csFindInput" placeholder="${csT("people.searchUsers", "Search users...")}" />
    <div id="csFindResults"></div>
    <div class="cs-modal-actions">
      <button class="cs-modal-cancel">${csT("common.close", "Close")}</button>
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
      requestsBox.innerHTML = `<div class="cs-search-hint">${csT("people.noPendingRequests", "No pending requests")}</div>`;
      return;
    }

    for (const n of notifications) {
      const row = document.createElement("div");
      row.className = "cs-search-row cs-notification-row";

      const label = document.createElement("span");
      label.className = "cs-notification-label";

      if (n.type === "contact_request") {
        label.textContent = csT("people.contactRequestFrom", { name: n.from_display_name || csElideFp(n.from_fp) }, `Contact request: ${n.from_display_name || csElideFp(n.from_fp)}`);

        const accept = document.createElement("button");
        accept.type = "button";
        accept.className = "cs-mini-action cs-mini-action-primary";
        accept.textContent = csT("common.accept", "Accept");

        const reject = document.createElement("button");
        reject.type = "button";
        reject.className = "cs-mini-action";
        reject.textContent = csT("common.reject", "Reject");

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
        title.textContent = csT("people.introductionText", {
          introducer: n.introducer_display_name || csElideFp(n.introducer_fp),
          other: n.other_display_name || csElideFp(n.other_fp)
        }, `Introduction: ${n.introducer_display_name || csElideFp(n.introducer_fp)} introduced you to ${n.other_display_name || csElideFp(n.other_fp)}`);
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
        accept.textContent = csT("common.accept", "Accept");

        const dismiss = document.createElement("button");
        dismiss.type = "button";
        dismiss.className = "cs-mini-action";
        dismiss.textContent = csT("common.dismiss", "Dismiss");

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
        label.textContent = csT("people.notification", "Notification");
        row.appendChild(label);
      }

      requestsBox.appendChild(row);
    }

    for (const r of outgoing) {
      const row = document.createElement("div");
      row.className = "cs-search-row cs-notification-row";

      const label = document.createElement("span");
      label.className = "cs-notification-label";
      label.textContent = csT("people.outgoingTo", { name: csElideFp(r.to_fp) }, `Outgoing: ${csElideFp(r.to_fp)}`);

      const status = document.createElement("span");
      status.textContent = csT("people.pending", "Pending");

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
      results.innerHTML = `<div class="cs-search-hint">${csT("people.typeAtLeast2", "Type at least 2 characters")}</div>`;
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
          <button type="button">${csT("people.sendRequest", "Send request")}</button>
        `;

        row.querySelector("button").onclick = async () => {
          await fetch("/api/v4/circlestack/people/add", {
            method: "POST",
            credentials: "same-origin",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ fp: u.fingerprint })
          });

          row.innerHTML = `<span>${name}</span><span>✓ ${csT("people.requestSent", "Request sent")}</span>`;
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
      btn.title = count === 1 ? csT("people.pendingNotificationOne", "1 pending notification") : csT("people.pendingNotificationMany", { count }, `${count} pending notifications`);
      btn.appendChild(dot);
    }
  } catch (_) {
    // Badge is best-effort. Do not break Circle Stack if notifications fail.
  }
}

csUpdateFindPeopleBadge();

document.getElementById("csFindPeopleBtn")
  ?.addEventListener("click", csOpenFindPeople);

// Robust fallback for Feed/Federated tab switching.
// This is delegated so it still works if toolbar buttons are already present
// before csInitFeedTabs() runs or if another script re-renders the toolbar.
if (!window.__circleStackFederatedTabDelegated) {
  window.__circleStackFederatedTabDelegated = true;

  document.addEventListener("click", (ev) => {
    const btn = ev.target && ev.target.closest
      ? ev.target.closest("#csLocalFeedBtn, #csFederatedBtn")
      : null;

    if (!btn) return;

    ev.preventDefault();
    ev.stopPropagation();

    csSetFeedMode(btn.id === "csFederatedBtn" ? "federated" : "local");
  }, true);
}

// Robust Feed/Federated tab fallback.
// The normal init listener should work, but this catches both pointerdown and
// click in capture phase so toolbar buttons still switch modes if another
// handler interferes.
if (!window.__circleStackFederatedTabHardFallback) {
  window.__circleStackFederatedTabHardFallback = true;

  const csHandleFeedTabEvent = (ev) => {
    const btn = ev.target && ev.target.closest
      ? ev.target.closest("#csLocalFeedBtn, #csFederatedBtn")
      : null;

    if (!btn) return;

    ev.preventDefault();
    ev.stopPropagation();

    csSetFeedMode(btn.id === "csFederatedBtn" ? "federated" : "local");
  };

  document.addEventListener("pointerdown", csHandleFeedTabEvent, true);
  document.addEventListener("click", csHandleFeedTabEvent, true);
}

// My Profile toolbar button fallback.
// Delegated so it still works if toolbar HTML changes or is re-rendered.
if (!window.__circleStackMyProfileDelegated) {
  window.__circleStackMyProfileDelegated = true;

  document.addEventListener("click", (ev) => {
    const btn = ev.target && ev.target.closest
      ? ev.target.closest("#csMyProfileBtn")
      : null;

    if (!btn) return;

    ev.preventDefault();
    ev.stopPropagation();

    csOpenMyProfileModal();
  }, true);
}

async function csOpenMyProfileModal() {
  const legacyProfilePage = document.getElementById("csProfilePage");
  if (legacyProfilePage) {
    legacyProfilePage.hidden = true;
    legacyProfilePage.style.display = "none";
    legacyProfilePage.textContent = "";
  }

  document.querySelectorAll(".cs-my-profile-modal-backdrop").forEach(el => el.remove());

  const backdrop = document.createElement("div");
  backdrop.className = "cs-modal-backdrop cs-my-profile-modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "cs-modal cs-my-profile-detached-modal";
  modal.setAttribute("role", "dialog");
  modal.setAttribute("aria-modal", "true");

  const shell = document.createElement("div");
  shell.className = "cs-my-profile-detached-shell";

  const setModalStyle = (name, value) => {
    modal.style.setProperty(name, value, "important");
  };

  const setShellStyle = (name, value) => {
    shell.style.setProperty(name, value, "important");
  };

  // Open centered as a real detached window. Dragging below will switch
  // transform to none and update left/top with important inline styles.
  setModalStyle("position", "fixed");
  setModalStyle("top", "50%");
  setModalStyle("left", "50%");
  setModalStyle("right", "auto");
  setModalStyle("bottom", "auto");
  setModalStyle("transform", "translate(-50%, -50%)");
  setModalStyle("margin", "0");
  setModalStyle("overflow", "hidden");
  setModalStyle("width", "min(760px, calc(100vw - 36px))");
  setModalStyle("max-height", "calc(100vh - 36px)");

  setShellStyle("max-height", "calc(100vh - 36px)");
  setShellStyle("overflow-y", "auto");
  setShellStyle("overflow-x", "hidden");
  setShellStyle("overscroll-behavior", "contain");

  const titlebar = document.createElement("div");
  titlebar.className = "cs-my-profile-windowbar";

  const titlebarText = document.createElement("div");
  titlebarText.className = "cs-my-profile-windowbar-title";
  titlebarText.textContent = csT("profile.myProfile", "My Profile");

  const titlebarHint = document.createElement("div");
  titlebarHint.className = "cs-my-profile-windowbar-hint";
  titlebarHint.textContent = "";
  titlebarHint.hidden = true;

  const titlebarLabel = document.createElement("div");
  titlebarLabel.className = "cs-my-profile-windowbar-label";
  titlebarLabel.appendChild(titlebarText);
  titlebarLabel.appendChild(titlebarHint);

  const closeTop = document.createElement("button");
  closeTop.className = "cs-my-profile-close";
  closeTop.type = "button";
  closeTop.textContent = "×";
  closeTop.title = csT("common.close", "Close");
  closeTop.setAttribute("aria-label", "Close profile");

  titlebar.appendChild(titlebarLabel);
  titlebar.appendChild(closeTop);

  const close = () => backdrop.remove();

  closeTop.addEventListener("click", close);
  backdrop.addEventListener("click", (ev) => {
    if (ev.target === backdrop) close();
  });

  const onKey = (ev) => {
    if (ev.key === "Escape") {
      document.removeEventListener("keydown", onKey, true);
      close();
    }
  };
  document.addEventListener("keydown", onKey, true);

  shell.appendChild(titlebar);

  const loading = document.createElement("div");
  loading.className = "cs-my-profile-loading";
  loading.textContent = "Loading profile…";
  shell.appendChild(loading);

  modal.appendChild(shell);
  backdrop.appendChild(modal);
  document.body.appendChild(backdrop);

  let dragState = null;

  titlebar.addEventListener("pointerdown", (ev) => {
    if (ev.target && ev.target.closest && ev.target.closest("button")) return;

    const rect = modal.getBoundingClientRect();

    setModalStyle("left", `${rect.left}px`);
    setModalStyle("top", `${rect.top}px`);
    setModalStyle("right", "auto");
    setModalStyle("bottom", "auto");
    setModalStyle("margin", "0");
    setModalStyle("transform", "none");

    dragState = {
      pointerId: ev.pointerId,
      startX: ev.clientX,
      startY: ev.clientY,
      left: rect.left,
      top: rect.top
    };

    try {
      titlebar.setPointerCapture(ev.pointerId);
    } catch (_) {}

    ev.preventDefault();
  });

  titlebar.addEventListener("pointermove", (ev) => {
    if (!dragState || dragState.pointerId !== ev.pointerId) return;

    const nextLeft = dragState.left + (ev.clientX - dragState.startX);
    const nextTop = dragState.top + (ev.clientY - dragState.startY);

    const maxLeft = Math.max(8, window.innerWidth - modal.offsetWidth - 8);
    const maxTop = Math.max(8, window.innerHeight - 80);

    setModalStyle("left", `${Math.max(8, Math.min(maxLeft, nextLeft))}px`);
    setModalStyle("top", `${Math.max(8, Math.min(maxTop, nextTop))}px`);
  });

  titlebar.addEventListener("pointerup", (ev) => {
    if (dragState && dragState.pointerId === ev.pointerId) {
      dragState = null;
      try {
        titlebar.releasePointerCapture(ev.pointerId);
      } catch (_) {}
    }
  });

  titlebar.addEventListener("pointercancel", () => {
    dragState = null;
  });

  let achievementsData = null;
  let me = null;

  try {
    const [achRes, usersRes] = await Promise.all([
      fetch(`${CS_API}/achievements/me`, { credentials: "same-origin" }),
      fetch(`${CS_API}/users`, { credentials: "same-origin" })
    ]);

    achievementsData = await achRes.json();
    const usersData = await usersRes.json();

    me = Array.isArray(usersData.users)
      ? usersData.users.find(u => u && u.is_me)
      : null;
  } catch (_) {
    loading.textContent = "Could not load profile.";
    return;
  }

  const stats = achievementsData.stats || {};
  const fp = String((me && me.fingerprint) || achievementsData.user_fp || "");
  const name = String((me && me.name) || (me && me.fp_short) || csElideFp(fp) || "Me");
  const role = String((me && me.role) || stats.role || "");
  const fpShort = String((me && me.fp_short) || csElideFp(fp));
  const avatarUrl = String((me && me.avatar_url) || "");
  const achievements = typeof csAchievementListFrom === "function"
    ? csAchievementListFrom(achievementsData.achievements)
    : [];

  if (typeof csPreloadBadgeIconAssets === "function") {
    await csPreloadBadgeIconAssets(achievements);
  }

  function stat(label, value) {
    const item = document.createElement("div");
    item.className = "cs-my-profile-stat";

    const num = document.createElement("div");
    num.className = "cs-my-profile-stat-value";

    const n = Number(value || 0);
    num.textContent = Number.isFinite(n) ? n.toLocaleString() : "0";

    const lab = document.createElement("div");
    lab.className = "cs-my-profile-stat-label";
    lab.textContent = label;

    item.appendChild(num);
    item.appendChild(lab);
    return item;
  }

  shell.textContent = "";
  shell.appendChild(titlebar);

  const header = document.createElement("div");
  header.className = "cs-my-profile-header";

  const avatar = document.createElement("div");
  avatar.className = "cs-profile-avatar cs-my-profile-modal-avatar";

  if (avatarUrl) {
    const img = document.createElement("img");
    img.src = avatarUrl;
    img.alt = "";
    avatar.appendChild(img);
  } else {
    avatar.textContent = name.slice(0, 1).toUpperCase();
  }

  const headText = document.createElement("div");
  headText.className = "cs-my-profile-title-wrap";

  const kicker = document.createElement("div");
  kicker.className = "cs-my-profile-title";
  kicker.textContent = csT("profile.myProfile", "My Profile");

  const nameEl = document.createElement("div");
  nameEl.className = "cs-my-profile-name";
  nameEl.textContent = name;

  const sub = document.createElement("div");
  sub.className = "cs-my-profile-sub";
  sub.textContent = role ? `${role} · ${fpShort}` : fpShort;

  headText.appendChild(kicker);
  headText.appendChild(nameEl);
  headText.appendChild(sub);

  if (typeof csRenderAchievementStrip === "function") {
    const strip = csRenderAchievementStrip(achievements, { profile: true, max: 3 });
    if (strip) headText.appendChild(strip);
  }

  header.appendChild(avatar);
  header.appendChild(headText);
  shell.appendChild(header);

  const fpBlock = document.createElement("div");
  fpBlock.className = "cs-my-profile-fp-block";

  const fpLabel = document.createElement("div");
  fpLabel.className = "cs-profile-label";
  fpLabel.textContent = csT("profile.fingerprint", "Fingerprint");

  const fpValue = document.createElement("div");
  fpValue.className = "cs-profile-fingerprint";
  fpValue.textContent = fp || "unknown";

  fpBlock.appendChild(fpLabel);
  fpBlock.appendChild(fpValue);
  shell.appendChild(fpBlock);

  const statsTitle = document.createElement("div");
  statsTitle.className = "cs-my-profile-section-title";
  statsTitle.textContent = csT("profile.circleStackStats", "Circle Stack stats");
  shell.appendChild(statsTitle);

  const statsGrid = document.createElement("div");
  statsGrid.className = "cs-my-profile-stats-grid";
  statsGrid.appendChild(stat(csT("profile.stats.posts", "Posts"), stats.posts_total));
  statsGrid.appendChild(stat(csT("profile.stats.publicPosts", "Public posts"), stats.public_posts_total));
  statsGrid.appendChild(stat(csT("profile.stats.mediaPosts", "Media posts"), stats.media_posts_total));
  statsGrid.appendChild(stat(csT("profile.stats.repliesWritten", "Replies written"), stats.replies_total));
  statsGrid.appendChild(stat(csT("profile.stats.reactionsGiven", "Reactions given"), stats.reactions_given_total));
  statsGrid.appendChild(stat(csT("profile.stats.repliesReceived", "Replies received"), stats.replies_received_total));
  statsGrid.appendChild(stat(csT("profile.stats.reactionsReceived", "Reactions received"), stats.post_reactions_received_total));
  statsGrid.appendChild(stat(csT("profile.stats.circleConnections", "Circle connections"), stats.circle_edges_total));
  statsGrid.appendChild(stat(csT("profile.stats.accountDays", "Account days"), stats.account_age_days));
  shell.appendChild(statsGrid);

  const achievementTitle = document.createElement("div");
  achievementTitle.className = "cs-my-profile-section-title";
  achievementTitle.textContent = csT("profile.earnedAchievements", "Earned achievements");
  shell.appendChild(achievementTitle);

  if (typeof csRenderAchievementProfileBlock === "function") {
    const block = csRenderAchievementProfileBlock(achievements);
    if (block) {
      block.classList.add("cs-my-profile-achievements");
      shell.appendChild(block);
    }
  }

  if (!achievements.length) {
    const empty = document.createElement("div");
    empty.className = "cs-empty";
    empty.textContent = csT("profile.noAchievementsUnlocked", "No achievements unlocked yet.");
    shell.appendChild(empty);
  }

  const lockedTitle = document.createElement("div");
  lockedTitle.className = "cs-my-profile-section-title";
  lockedTitle.textContent = csT("profile.lockedAchievements", "Locked achievements");
  if (csAchievementReviewAllEnabled()) lockedTitle.style.display = "none";

  csAchievementReviewMaybeHideLockedSection(lockedTitle.closest(".cs-profile-section") || lockedTitle.parentElement);
shell.appendChild(lockedTitle);

  const lockedBlock = csRenderLockedAchievementPlaceholders(achievements);
  if (lockedBlock) {
    shell.appendChild(lockedBlock);
  } else {
    const allUnlocked = document.createElement("div");
    allUnlocked.className = "cs-empty";
    allUnlocked.textContent = csT("profile.allVisibleAchievementsUnlocked", "All visible achievements unlocked.");
    shell.appendChild(allUnlocked);
  }

  const actions = document.createElement("div");
  actions.className = "cs-modal-actions cs-my-profile-actions";

  if (fp && navigator.clipboard) {
    const copy = document.createElement("button");
    copy.className = "cs-modal-cancel";
    copy.type = "button";
    copy.textContent = csT("profile.copyFingerprint", "Copy fingerprint");
    copy.addEventListener("click", async () => {
      await navigator.clipboard.writeText(fp);
      copy.textContent = csT("common.copied", "Copied");
      setTimeout(() => { copy.textContent = csT("profile.copyFingerprint", "Copy fingerprint"); }, 1200);
    });
    actions.appendChild(copy);
  }

  const closeBtn = document.createElement("button");
  closeBtn.className = "cs-modal-cancel";
  closeBtn.type = "button";
  closeBtn.textContent = csT("common.close", "Close");
  closeBtn.addEventListener("click", close);
  actions.appendChild(closeBtn);

  shell.appendChild(actions);

  // Keep detached profile windows opening at the top.
  // Focusing the bottom Close button makes browsers scroll the modal to bottom.
  shell.scrollTop = 0;
  modal.scrollTop = 0;

  requestAnimationFrame(() => {
    shell.scrollTop = 0;
    modal.scrollTop = 0;

    try {
      closeTop.focus({ preventScroll: true });
    } catch (_) {
      closeTop.focus();
      shell.scrollTop = 0;
      modal.scrollTop = 0;
    }
  });
}

// Detached My Profile button handler.
if (!window.__circleStackDetachedMyProfileDelegated) {
  window.__circleStackDetachedMyProfileDelegated = true;

  document.addEventListener("click", (ev) => {
    const btn = ev.target && ev.target.closest
      ? ev.target.closest("#csMyProfileBtn")
      : null;

    if (!btn) return;

    ev.preventDefault();
    ev.stopPropagation();

    csOpenMyProfileModal();
  }, true);
}

// Theme helper: tag only the small floating reaction emoji popup.
// Earlier broad detection could accidentally tag a post/card container.
(function () {
  if (window.__circleStackReactionThemeTaggerV2) return;
  window.__circleStackReactionThemeTaggerV2 = true;

  const REACTION_EMOJIS = ["👍", "❤️", "😂", "😮", "👏", "🔥"];

  function reactionHits(el) {
    const text = el && el.textContent ? el.textContent : "";
    return REACTION_EMOJIS.filter(e => text.includes(e)).length;
  }

  function looksLikeFloatingReactionMenu(el) {
    if (!el || el.nodeType !== 1) return false;

    const hits = reactionHits(el);
    if (hits < 4) return false;

    const buttons = el.querySelectorAll ? el.querySelectorAll("button") : [];
    if (buttons.length < 4 || buttons.length > 10) return false;

    const rect = el.getBoundingClientRect();
    if (!rect || rect.width <= 0 || rect.height <= 0) return false;

    // Reaction popup should be compact. This prevents post/cards from being tagged.
    if (rect.width > 360 || rect.height > 170) return false;

    const style = window.getComputedStyle(el);
    const pos = style ? style.position : "";
    const z = style ? style.zIndex : "auto";

    // Floating popup is normally absolute/fixed/sticky or has z-index.
    const floating =
      pos === "absolute" ||
      pos === "fixed" ||
      pos === "sticky" ||
      z !== "auto";

    if (!floating) return false;

    // Direct button-heavy containers are much safer than arbitrary parents.
    const directButtons = Array.from(el.children || [])
      .filter(child => child && child.tagName === "BUTTON").length;

    return directButtons >= 3;
  }

  function cleanupBadTags() {
    document.querySelectorAll(".cs-reaction-theme-menu").forEach((el) => {
      const rect = el.getBoundingClientRect();
      const tooBig = rect && (rect.width > 360 || rect.height > 170);

      // Never allow feed/post/card/layout elements to keep popup styling.
      const forbidden =
        el.classList.contains("cs-post") ||
        el.classList.contains("cs-feed") ||
        el.classList.contains("cs-shell") ||
        el.closest(".cs-post") === el ||
        tooBig;

      if (forbidden || !looksLikeFloatingReactionMenu(el)) {
        el.classList.remove("cs-reaction-theme-menu");
      }
    });
  }

  function tagReactionMenus(root) {
    cleanupBadTags();

    const base = root && root.querySelectorAll ? root : document;
    base.querySelectorAll("div, section").forEach((el) => {
      if (looksLikeFloatingReactionMenu(el)) {
        el.classList.add("cs-reaction-theme-menu");
      }
    });
  }

  tagReactionMenus(document);

  const obs = new MutationObserver((records) => {
    for (const rec of records) {
      for (const node of rec.addedNodes || []) {
        if (!node || node.nodeType !== 1) continue;

        if (looksLikeFloatingReactionMenu(node)) {
          node.classList.add("cs-reaction-theme-menu");
        }

        tagReactionMenus(node);
      }
    }
  });

  obs.observe(document.documentElement, {
    childList: true,
    subtree: true
  });

  document.addEventListener("mouseover", () => {
    setTimeout(() => tagReactionMenus(document), 0);
  }, true);

  document.addEventListener("focusin", () => {
    setTimeout(() => tagReactionMenus(document), 0);
  }, true);
})();

// Theme helper: tag federated feed labels/notes that are generated without
// stable styling hooks. Keep this conservative: exact short labels only.
(function () {
  if (window.__circleStackFederatedThemeTagger) return;
  window.__circleStackFederatedThemeTagger = true;

  function cleanText(el) {
    return String(el && el.textContent ? el.textContent : "")
      .replace(/\s+/g, " ")
      .trim();
  }

  function tagFederatedThemeBits(root) {
    const base = root && root.querySelectorAll ? root : document;

    base.querySelectorAll("span, div, button, small, p").forEach((el) => {
      const text = cleanText(el);
      if (!text) return;

      // Small pills/labels.
      if (text === csT("federation.knownOrigin", csT("federation.knownOrigin", "Known origin")) || text === csT("federation.unknownOrigin", csT("federation.unknownOrigin", "Unknown origin"))) {
        el.classList.add("cs-fed-origin-pill-theme");
      }

      if (text === "FEDERATED") {
        el.classList.add("cs-fed-status-pill-theme");
      }

      // The Why? expanded explanation.
      if (
        text.startsWith(csT("federation.originDetailIntro", "This came from a NAS origin")) || text.startsWith("This came from a NAS origin") ||
        text.includes(csT("federation.originLabel", "Origin:")) && text.includes(csT("federation.sourceLabel", "Source:")) && text.includes(csT("federation.eventLabel", "Event:"))
      ) {
        el.classList.add("cs-fed-why-note-theme");
      }

      // Media validation note under the remote preview.
      if (
        text.includes("remote media item") ||
        text.includes("origin preview validated") ||
        text.includes("origin preview") ||
        text.includes("preview validated")
      ) {
        el.classList.add("cs-fed-media-note-theme");
      }

      // Remote post/reaction subtitles.
      if (text === "Remote post" || text === csT("federation.remotePostReaction", "Remote post reaction")) {
        el.classList.add("cs-fed-subtitle-theme");
      }
    });
  }

  tagFederatedThemeBits(document);

  const obs = new MutationObserver((records) => {
    for (const rec of records) {
      for (const node of rec.addedNodes || []) {
        if (!node || node.nodeType !== 1) continue;
        tagFederatedThemeBits(node);
      }
    }
  });

  obs.observe(document.documentElement, {
    childList: true,
    subtree: true
  });
})();

// Theme helper: extra federated media/reaction tagging.
// Keeps CSS selectors exact without relying on broad post-level overrides.
(function () {
  if (window.__circleStackFederatedMediaThemeTagger) return;
  window.__circleStackFederatedMediaThemeTagger = true;

  const REACTION_EMOJIS = ["👍", "❤️", "😂", "😮", "👏", "🔥"];

  function textOf(el) {
    return String(el && el.textContent ? el.textContent : "")
      .replace(/\s+/g, " ")
      .trim();
  }

  function hasReactionEmoji(text) {
    return REACTION_EMOJIS.some(e => text.includes(e));
  }

  function tagFederatedMediaBits(root) {
    const base = root && root.querySelectorAll ? root : document;

    base.querySelectorAll("span, div, small, p").forEach((el) => {
      const text = textOf(el);
      if (!text) return;

      if (text.startsWith(csT("federation.targetLabel", "Target:")) || text.startsWith("Target post") || text.includes(csT("federation.postIdLabel", "Post id:"))) {
        el.classList.add("cs-fed-target-theme");
      }

      if (
        (text === "You" || text.startsWith("You ")) &&
        hasReactionEmoji(text)
      ) {
        el.classList.add("cs-fed-reaction-line-theme");
      }

      if (
        text.includes("remote media item") ||
        text.includes("origin preview validated") ||
        text.includes("origin preview") ||
        text.includes("preview validated")
      ) {
        el.classList.add("cs-fed-media-note-theme");

        const post = el.closest(".cs-post");
        const prev = el.previousElementSibling;

        if (prev && post && post.contains(prev)) {
          prev.classList.add("cs-fed-media-frame-theme");
          prev.querySelectorAll("img, video").forEach((media) => {
            media.classList.add("cs-fed-media-image-theme");
          });
        }
      }
    });
  }

  tagFederatedMediaBits(document);

  const obs = new MutationObserver((records) => {
    for (const rec of records) {
      for (const node of rec.addedNodes || []) {
        if (!node || node.nodeType !== 1) continue;
        tagFederatedMediaBits(node);
      }
    }
  });

  obs.observe(document.documentElement, {
    childList: true,
    subtree: true
  });
})();

// Theme helper: tag exact LOCKED pill text in achievement/profile views.
(function () {
  if (window.__circleStackLockedPillTagger) return;
  window.__circleStackLockedPillTagger = true;

  function cleanText(el) {
    return String(el && el.textContent ? el.textContent : "")
      .replace(/\s+/g, " ")
      .trim();
  }

  function tagLockedPills(root) {
    const base = root && root.querySelectorAll ? root : document;

    base.querySelectorAll("span, div, small, p, strong").forEach((el) => {
      const text = cleanText(el);
      if (!text) return;

      if (text === "LOCKED") {
        el.classList.add("cs-ach-locked-pill-theme");
      }
    });
  }

  tagLockedPills(document);

  const obs = new MutationObserver((records) => {
    for (const rec of records) {
      for (const node of rec.addedNodes || []) {
        if (!node || node.nodeType !== 1) continue;
        tagLockedPills(node);
      }
    }
  });

  obs.observe(document.documentElement, {
    childList: true,
    subtree: true
  });
})();

// Theme helper: safely tag direct locked achievement rows.
// Only .cs-profile-achievement rows after the "Locked achievements" heading are tagged.
// Do not tag inner title/desc/icon children.
(function () {
  if (window.__circleStackLockedSectionTaggerV4) return;
  window.__circleStackLockedSectionTaggerV4 = true;

  function cleanText(el) {
    return String(el && el.textContent ? el.textContent : "")
      .replace(/\s+/g, " ")
      .trim();
  }

  function follows(a, b) {
    return !!(a && b && (b.compareDocumentPosition(a) & Node.DOCUMENT_POSITION_FOLLOWING));
  }

  function tagLockedRows() {
    document.querySelectorAll(".cs-ach-locked-row-theme").forEach((row) => {
      row.classList.remove("cs-ach-locked-row-theme");
    });

    const headings = Array.from(document.querySelectorAll("h1,h2,h3,h4,div,p,strong,span"))
      .filter((el) => cleanText(el) === "Locked achievements");

    for (const heading of headings) {
      const scope =
        heading.closest(".cs-my-profile-detached-shell") ||
        heading.closest(".cs-profile-modal") ||
        heading.parentElement;

      if (!scope) continue;

      const rows = Array.from(scope.querySelectorAll(".cs-profile-achievement"))
        .filter((row) => follows(row, heading));

      for (const row of rows) {
        row.classList.add("cs-ach-locked-row-theme");

        // Hide real LOCKED text nodes/elements if they exist.
        row.querySelectorAll("span,div,small,strong").forEach((el) => {
          if (cleanText(el) === "LOCKED") {
            el.classList.add("cs-ach-locked-pill-theme");
          }
        });
      }
    }
  }

  tagLockedRows();

  const obs = new MutationObserver(() => tagLockedRows());
  obs.observe(document.documentElement, {
    childList: true,
    subtree: true
  });
})();

