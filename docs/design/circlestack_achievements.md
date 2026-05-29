# Circle Stack Achievements

Status: v2 implemented on `feature/circle-stack-nodus-federation`

Circle Stack achievements are GitHub-style profile badges for DNA-Nexus / PQ-NAS users.

Achievements are separate from Proof of Humanity. Some achievement and activity signals may later become weak inputs into a broader Proof-of-Humanity score, but badges are not PoH by themselves.

## Goals

Achievements should:

- make Circle Stack profiles feel alive and personal
- reward real user history and meaningful activity
- work locally and across NAS-NAS federation via Nodus
- avoid exposing private raw statistics unnecessarily
- avoid making Proof of Humanity depend on running a NAS
- avoid encouraging obvious spam behavior

## Current implementation

Backend files:

- `server/src/achievements.h`
- `server/src/achievements.cpp`
- `server/src/circle_stack_routes.cpp`

Frontend files:

- `apps/bundled/circlestack/src/www/app.js`
- `apps/bundled/circlestack/src/www/app.css`
- `apps/bundled/circlestack/src/www/badges/*.svg`

## Main API routes

### GET `/api/v4/circlestack/achievements/me`

Returns the authenticated user's current achievements, current stats, and server-side newly unlocked badges.

Important response fields:

- `achievements`: all currently earned badges
- `newly_unlocked`: earned badges whose unlock modal has not yet been dismissed
- `stats`: current private stats for the authenticated user
- `schema`: currently `pqnas.achievements.v1`

### POST `/api/v4/circlestack/achievements/dismiss`

Marks one achievement unlock modal as dismissed for the authenticated user.

Request body:

    {
      "achievement_id": "account.node_steward"
    }

Response:

    {
      "ok": true,
      "achievement_id": "account.node_steward",
      "dismissed": true
    }

## Server-side unlock history

Unlock history is stored in:

- `/srv/pqnas/circlestack.db`

Table:

- `user_achievement_unlocks`

Columns:

- `user_fp`
- `achievement_id`
- `unlocked_epoch`
- `first_seen_epoch`
- `last_seen_epoch`
- `dismissed_epoch`
- `visible`
- `pinned`

Meaning:

- `unlocked_epoch`: when the server first detected that the user qualified
- `first_seen_epoch`: first server-side achievement sync
- `last_seen_epoch`: last achievement sync/check
- `dismissed_epoch`: when the user dismissed the unlock modal
- `visible`: future profile visibility toggle
- `pinned`: future pinned badge control

## Current badge conditions

| Badge | ID | Condition |
|---|---|---|
| Node Steward | `account.node_steward` | User role is `admin` |
| Established Signal | `account.established_signal` | Account age is 100+ days |
| Old Guard | `account.old_guard` | Account age is 500+ days |
| Legacy Node | `account.legacy_node` | Account age is 1000+ days |
| First Signal | `circlestack.first_signal` | User has created 1+ Circle Stack post |
| Signal Sender | `circlestack.signal_sender` | User has created 100+ Circle Stack posts |
| Broadcast Node | `circlestack.broadcast_node` | User has created 500+ Circle Stack posts |
| Anchor Voice | `circlestack.anchor_voice` | User has created 1000+ Circle Stack posts |
| Public Voice | `circlestack.public_voice` | User has created 100+ public posts |
| Media Runner | `circlestack.media_runner` | User has created 100+ posts with media |
| Conversation Spark | `circlestack.conversation_spark` | User has written 100+ replies |
| Signal Amplifier | `circlestack.signal_amplifier` | User has given 100+ reactions |
| Crowd Spark | `circlestack.crowd_spark` | User's posts have received 100+ reactions from others |
| Thread Starter | `circlestack.thread_starter` | User's posts have received 100+ replies from others |
| Circle Builder | `circlestack.circle_builder` | User has 10+ Circle connections |

## Current stats sources

Achievements currently read mostly from `/srv/pqnas/circlestack.db`.

Current counters:

- posts by user
- public posts by user
- media posts by user
- replies written by user
- post reactions given by user
- reply reactions given by user
- reactions received from other users on user's posts
- replies received from other users on user's posts
- Circle connections
- account age
- account role

## SVG badge icons

Badges use bundled SVG assets instead of relying only on emoji.

SVG directory:

- `apps/bundled/circlestack/src/www/badges/`

Backend returns fields such as:

- `icon`
- `icon_key`
- `icon_asset`

Frontend prefers known local SVG assets and falls back to emoji.

Security rule:

- remote federated events may include badge metadata
- frontend should prefer local known assets by badge ID
- frontend must not trust arbitrary remote SVG URLs

## Federation / Nodus behavior

For public Circle Stack federation, badge metadata is included in signed federation event payloads.

Current event payload fields:

- `payload.owner_badges`
- `payload.actor_badges`

Remote NAS reads those fields and exposes them in:

- `GET /api/v4/circlestack/federated/feed`

The remote Circle Stack UI renders badges on federated posts and remote person cards.

Privacy rule:

- federated events carry public badge metadata only
- raw private stats such as exact storage usage, upload totals, or full activity history should not be federated

## Current unlock modal behavior

Current frontend behavior:

- fetches `/api/v4/circlestack/achievements/me`
- reads `newly_unlocked`
- shows one modal for the first pending achievement
- dismisses that one achievement via `/achievements/dismiss`

If multiple achievements are unlocked at once:

- first modal is shown
- only that badge is dismissed
- remaining badges stay pending server-side
- remaining badges will show on a later achievement check/reload

Achievements are not lost, but UX should be improved with a modal queue.

## Recommended next improvements

### 1. Modal queue

Show all pending achievements one-by-one:

- show badge 1
- user clicks Nice
- dismiss badge 1
- show badge 2
- dismiss badge 2

### 2. Achievements page / trophy cabinet

Add a profile or settings page section:

- earned badges
- pinned badges
- hidden badges
- unlock date
- visible/private toggle

### 3. Locked badge strategy

Do not reveal every exact threshold publicly.

Recommended model:

- show exact requirements for safe badges
- show vague requirements for spam-sensitive badges
- keep some badges secret/mystery

Safe exact examples:

- First Signal: create your first Circle Stack post
- Established Signal: keep your account active for 100+ days

Spam-sensitive vague example:

- Signal Sender: keep contributing meaningful Circle Stack posts over time

### 4. Badge visibility and pinning

Use existing database fields:

- `visible`
- `pinned`

Future API ideas:

- `POST /api/v4/circlestack/achievements/visibility`
- `POST /api/v4/circlestack/achievements/pin`

### 5. More achievement sources

Not implemented yet:

- shares created
- upload/storage size tiers
- Drop Zone created
- Drop Zone uploads received
- Echo Stack archived pages
- Reel Stack videos
- Photo Gallery photos
- Neonwave tracks
- trusted devices
- federation-specific achievements
- account security achievements

### 6. Anti-spam hardening

Before making raw count badges prominent, consider safer counting rules:

- cap counted posts per day
- require posts to remain undeleted
- require activity over multiple days
- count unique users for received replies/reactions
- exclude self-reactions
- exclude known spam/deleted/hidden content
- make interaction/quality badges more prestigious than raw volume badges

### 7. Proof of Humanity connection

Achievements are not Proof of Humanity.

They may later become weak inputs into a broader PoH system, together with:

- account age
- trusted devices
- community vouching
- long-lived identity keys
- normal interaction history
- abuse/spam history
- optional attestations

Running a NAS must not be required for Proof of Humanity.
