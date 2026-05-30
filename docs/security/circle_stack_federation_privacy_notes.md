# Circle Stack Federation Privacy Notes

Circle Stack federation is designed for **public** posts and public discovery between DNA-Nexus / PQ-NAS servers.

This document describes what metadata is intentionally exposed when federation is enabled.

## Public federation model

When a user publishes a public Circle Stack post, reply, or reaction, the server may publish a federation event through Nodus.

The actual media file does **not** go through Nodus.

Instead, the event contains metadata and optional media references. Remote servers use those references to request a preview directly from the origin server.

## Metadata exposed in federation events

A public federation event may include:

- `origin_nas`: the Nodus/NAS identity fingerprint of the publishing NAS.
- `origin.preview_base_url`: the public base URL of the publishing NAS.
- `origin.preview_endpoint`: the public endpoint used for media previews.
- `payload.origin_label`: display label for the origin/user.
- `payload.owner_display_name`: display name of the posting user.
- `payload.owner_fp`: user fingerprint of the posting user.
- `payload.owner_fp_short`: shortened user fingerprint.
- `payload.text_preview`: text preview of the public post/reply.
- `payload.media_refs`: references to public media previews, if media exists.
- timestamps and event ids.

This means anyone who can observe the public federation network may be able to correlate:

- which NAS published public content,
- which public URL serves previews,
- which user fingerprint authored public content,
- when public activity happened.

## Media privacy

Federated media previews are fetched from the origin NAS through:

`/api/v4/circlestack/federation/media-preview?event_id=...&ref_id=...`

The endpoint is public by design for public federation.

The preview endpoint should only serve media connected to posts/replies that are currently public. If a post is no longer public, the endpoint must reject preview requests.

## Operator guidance

Only enable Circle Stack federation on a server if the operator accepts that public posts reveal NAS-level and user-level metadata.

For privacy-sensitive deployments:

- keep federation disabled,
- avoid posting personal data to public Circle Stack,
- consider using non-federated local circles only,
- use a public base URL that does not reveal a private home address or personal domain,
- understand that user fingerprints may be stable identifiers across events.

## Future hardening ideas

Possible future changes:

- publish `preview_base_url` through a separate NAS presence record instead of every event,
- omit full `owner_fp` and publish only `owner_fp_short`,
- add per-user privacy settings for federated public posting,
- add a clear admin UI warning when federation is enabled,
- add event signing so origin identity cannot be forged.
