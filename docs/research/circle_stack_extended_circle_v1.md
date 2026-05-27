# Circle Stack Extended Circle v1 Design

## Purpose

Extended Circle v1 is a feed-selection and discovery layer for Circle Stack federation.

It should build on the currently working Circle Stack federation v1 without changing the Nodus event transport, origin discovery worker, media preview flow, or remote reaction flow.

The goal is to make public federated content useful without flooding the user with every public post from every unknown NAS.

## Current foundation

Circle Stack federation v1 already supports:

- public NAS ↔ NAS post federation through Nodus
- remote origin media previews
- known remote origins
- personal mute/unmute
- admin global origin enable/disable
- feed modes:
  - Feed
  - Federated
  - My Circle
- remote reactions applied back to the origin NAS
- server-side “You reacted” state for federated reactions
- read-only federation status diagnostics

Extended Circle v1 should not break any of this.

## Definition

Extended Circle v1 decides which public federated posts are shown to the local user, and why.

It does not make private content federated.

It does not publish the user’s full social graph.

It does not require a new Nodus protocol in v1.

It does not require a federation worker refactor.

## Feed buckets

Extended Circle v1 should classify public federated posts into these buckets.

### 1. My Circle

Posts from people/origins the user explicitly knows, follows, or has added through Circle Stack people/contact actions.

This is the most trusted bucket.

Examples:

- remote NAS is in known origins
- remote person has been added to People
- origin was created through a successful federated person add/follow flow

### 2. Extended Circle

Posts from public origins that are connected indirectly through known origins, introductions, or community relationships.

Examples:

- a known remote person introduced another remote person
- a known origin references another origin
- a remote post comes from an origin discovered through a trusted/known path

This bucket is useful for discovery, but should be lower priority than My Circle.

### 3. Wider Public

Public posts from unknown remote origins.

These should be shown rarely and clearly marked as public discovery.

This bucket prevents the system from becoming an echo chamber, but must not dominate the feed.

### 4. Muted or Disabled

Muted origins are hidden for the local user.

Globally disabled origins should not be polled or should be excluded from active federation display depending on the current server behavior.

## Feed modes

The current modes should remain stable.

### Feed

Local/default feed.

### My Circle

Strict known-circle view.

Shows only local circle content and federated content from known origins/people.

### Federated

Broad federated feed.

Shows all allowed public federated content, excluding personally muted or globally disabled origins.

### Future: Discover

A future mode may be added after v1 design is stable.

Discover would show:

- My Circle posts
- Extended Circle posts
- a small amount of Wider Public posts

This should be explainable and user-controlled.

## Ranking rules

The first implementation should use simple deterministic scoring, not an opaque algorithm.

Suggested priority order:

1. local posts and direct circle posts
2. known people / known origins
3. introduced origins
4. origins with previous positive interaction
5. wider public posts
6. muted origins hidden
7. globally disabled origins hidden or excluded

Possible scoring fields:

- origin is known
- origin is enabled
- origin is muted for this user
- origin has public base URL
- actor is in People
- actor has been introduced
- user has reacted to this origin before
- post has recent local interaction
- post age

## Explainability requirement

Every federated post shown outside the plain Federated feed should be able to explain why it appeared.

Example labels:

- Shown because this NAS is in your Known origins.
- Shown because this person is in your Circle.
- Shown because this origin was introduced by a known contact.
- Shown from wider public discovery.
- Hidden because this origin is muted for you.
- Hidden because this origin is disabled globally.

The reason label should be short by default, with more detail available on hover or click.

## Privacy rules

Extended Circle v1 must not publish the user’s full social graph.

Local server may use local data for ranking, such as:

- people_contacts
- known_remote_origins
- circle_user_origin_prefs
- local circle edges
- local reaction state
- remote feed event metadata

But it should not broadcast:

- the complete list of people a user follows
- the complete list of origins a user trusts
- private circle membership
- private reactions or private browsing behavior

Public federation events may contain public metadata needed for display and safe media preview, but not private relationship data.

## Data sources for v1

Useful existing data:

- known_remote_origins
- circle_user_origin_prefs
- circle_federated_local_reactions
- people_contacts
- circle_edges
- circle_federation_remote_feed
- local posts/replies/reactions

No schema change should be required for the first design pass.

## Safe implementation sequence

### Step 1: Design only

Create this document and keep current federation behavior unchanged.

### Step 2: Add internal Discover mode placeholder

Add a new internal feed mode name only if needed.

At first, it can behave the same as Federated.

No ranking logic yet.

### Step 3: Add explainable ranking helper

Create a small helper that classifies a remote event into:

- my_circle
- extended_circle
- wider_public
- hidden_muted
- hidden_disabled

The helper should return both bucket and reason text.

### Step 4: Apply Discover filtering

Use the helper only in Discover mode.

Do not change existing Federated behavior.

### Step 5: Tune distribution

Start with conservative ratios:

- mostly My Circle
- some Extended Circle
- rare Wider Public

The user should be able to mute origins immediately from any discovered post.

## Manual test cases

Before enabling Extended Circle behavior broadly, verify:

- My Circle mode still shows known origins.
- Federated mode still shows all allowed public federated posts.
- Muted origins stay hidden.
- Unmuting restores visibility.
- Global disable still prevents/blocks the origin as designed.
- Wider Public posts are clearly marked.
- Discover mode never shows private/circle-only content.
- Remote previews still load from origin NAS.
- Remote reactions still apply back to the origin NAS.
- “You reacted” state still survives reload.
- Existing federation status endpoint still reports correct counts.

## Non-goals for v1

Extended Circle v1 should not include:

- global popularity algorithm
- ads
- engagement-maximizing opaque ranking
- publishing user social graphs
- federation worker rewrite
- new Nodus event transport
- private post federation
- automatic trust of unknown origins

## Future ideas

Possible later extensions:

- user-adjustable discovery slider
- per-origin trust level
- community/topic tags
- opt-in public profile cards
- origin reputation based on user-controlled trust signals
- federated introductions with explicit consent
- per-post “why am I seeing this?” details
- admin policy for maximum Wider Public content
