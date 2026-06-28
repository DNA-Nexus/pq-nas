# Contacts Product Requirements

## Status

Draft.

## Summary

Contacts is the DNA-Nexus address book and lightweight customer registry.

It allows a DNA-Nexus user to store private contact records for people, companies, customers, suppliers, family members, local DNA-Nexus users, external DNA identities, and manually created contacts.

Contacts should work as a practical business tool, not only as a technical identity list.

## Product Goal

Make it easy and safe to manage useful contact and customer information inside DNA-Nexus.

Contacts should be useful for:

- private users managing personal contacts
- small businesses managing customers and suppliers
- clubs managing members and partners
- operators offering DNA-Nexus Server as a business-friendly private cloud product
- File Manager workspace users sharing contact details
- future mobile clients importing selected phone contacts

## Core Value

DNA-Nexus already has identity concepts such as fingerprints, users, workspaces, and external participants.

Contacts adds a human and business-friendly layer on top of those identities.

Technical identity becomes a useful address book record. Address book records can then be copied, exported, imported, and shared as standalone contact cards.

## Target Users

### Owner

The owner is a normal DNA-Nexus user who creates and manages contact records.

Owner needs:

- create manual contacts
- edit contact details
- store customer and supplier information
- search and filter contacts
- import/export contacts
- copy contact details
- share contact cards into workspace messages
- delete old or incorrect contacts

### Workspace Member

A workspace member may receive a shared contact card in File Manager workspace messages.

Workspace member needs:

- see contact details clearly
- copy email, phone, address, or full contact card
- open website links when present

Workspace member must not need access to the sender's Contacts app or full address book.

### External Workspace User

An external workspace user may receive a shared contact card in an external workspace message.

External user needs:

- see the contact card directly in the message board
- copy relevant details
- open website links when present

External user must not need:

- DNA-Nexus account access
- Contacts app access
- File Manager internal app permissions
- visibility into private contact storage

### Admin

The admin needs safe defaults and abuse controls.

Admin needs:

- storage and database safety
- rate limits for write endpoints
- safe error behavior
- future audit visibility if needed
- no accidental exposure of internal technical details

## Core Requirements

### Contact Records

A contact record should support:

- display name
- nickname
- contact type
- company
- title
- email
- phone
- mobile
- website
- street address
- postal code
- city
- country
- delivery address fields
- tags
- status
- notes
- subject kind
- subject fingerprint

Supported subject kinds:

- `manual_contact`
- `fingerprint`
- `local_user`
- `external_dna`

### Contact Types

Useful contact types include:

- person
- company
- customer
- supplier
- family
- other

### Status

Useful status values include:

- active
- inactive
- archived

Archived contacts should remain stored but should be visually distinguishable from active contacts.

### Manual Contacts

Manual contacts must not depend on a real DNA-Nexus user account.

Creating a manual contact must not create a DNA-Nexus user account.

When a manual contact is created, the app may generate a private stable anchor or fingerprint for that record so it can be updated without overwriting other manual contacts.

### Local User Contacts

Contacts may link to enabled local DNA-Nexus users.

The UI may help the owner choose a local user from a candidate list.

Backend code should not rely on UI visibility as a security boundary. If local user linking becomes security-sensitive, the backend must validate that the referenced local user exists and is allowed.

## Import and Export

Contacts should support import from:

- CSV
- vCard

Contacts should support export to:

- CSV
- vCard

Import safety requirements:

- reject oversized files
- limit number of imported contacts
- avoid unbounded memory usage
- parse malformed files safely
- show clear user-facing errors
- do not expose internal parser or database diagnostics

CSV export must prevent spreadsheet formula injection.

Fields beginning with dangerous spreadsheet formula characters should be exported as text.

Dangerous prefixes include:

- =
- +
- -
- @
- tab
- newline
- carriage return

## Duplicate Detection

The UI should warn about possible duplicates.

Duplicate detection may use:

- display name
- company
- email
- phone
- mobile
- website
- normalized text comparisons

Duplicate detection is a usability feature, not a security boundary.

The owner may still choose to save a possible duplicate.

## Quick Actions

Contacts should offer quick actions where useful:

- copy full contact card
- copy email
- copy phone
- copy mobile
- copy address
- open website

Actions must use safe DOM APIs and must not render contact data as trusted HTML.

## Contact Card Copy

The app should support copying a standalone DNA-Nexus contact marker.

Example marker:

[DNA-NEXUS-CONTACT]
Name: Example Person
Company: Example Oy
Title: CEO
Email: person@example.com
Phone: +358...
Mobile: +358...
Website: https://example.com
Address: Example Street 1, 23500 Uusikaupunki, Finland
Tags: customer, printing
Identity: abc123...
[/DNA-NEXUS-CONTACT]

The marker format should remain text-based, easy to paste, and safe to render.

## Workspace Integration

File Manager workspace messages should detect pasted DNA-Nexus contact markers and render them as standalone contact cards.

External workspace messages should also render pasted DNA-Nexus contact markers as standalone cards.

External users must not need access to the Contacts app to read shared contact details.

Opening the original Contacts app record is not a core requirement.

Reasons:

- external users may not have an account
- workspace members may not have Contacts app permissions
- the sender's private address book must not become browsable
- standalone shared contact data is safer and simpler

## Security Requirements

Contacts handles personal and business data, so it must be treated as sensitive user data.

Required protections:

- authenticated access for private contact APIs
- owner isolation for contact records
- no UI-only security boundaries
- parameterized SQL
- no shell execution
- bounded request body sizes
- field length limits
- rate limits on write endpoints
- safe generic user-facing errors
- no raw SQL/database errors in normal responses
- safe import limits
- CSV formula injection protection
- no trusted rendering of contact data as HTML
- no exposure of private contacts to external workspace users unless explicitly shared by the owner

## API Limits

Contacts write routes should enforce server-side limits.

Examples:

- maximum request body size for save
- maximum request body size for delete
- maximum field lengths
- maximum notes length
- rate limit for save operations
- rate limit for delete operations

Frontend-only limits are not enough.

## Rate Limiting

Contacts write endpoints should be rate-limited to reduce abuse and accidental rapid repeated saves.

Rate-limit keys should include at least:

- route or action
- authenticated actor
- client address where available

Rate-limited responses should return HTTP 429 with a safe generic message.

## Error Handling

User-facing errors should be clear but safe.

Good examples:

- failed to save person
- contact field is too large
- too many requests

Bad examples:

- raw SQLite error
- filesystem path
- SQL statement
- internal table name
- stack trace

## Data Exposure

Contacts data must remain private by default.

Sharing a contact card into a workspace message is an explicit user action.

Only the shared card content is visible to receivers.

The receiver must not gain access to the sender's full Contacts database.

## UX Requirements

Good UX:

- clear contact list
- clear edit form
- business-friendly fields
- visible duplicate warnings
- quick copy actions
- import and export buttons
- polished custom confirmation modals
- theme-aware styling
- mobile-friendly layout where practical

Bad UX:

- raw JSON editing
- native browser confirm dialogs
- exposing fingerprints as the main visible identity
- technical database errors
- hidden import limits
- hard-to-read transparent modals

## Styling Requirements

Contacts must follow DNA-Nexus theme rules.

Rules:

- use theme tokens
- avoid hardcoded RGB or hex colors in app CSS
- avoid important overrides
- keep custom UI readable in dark, bright, classic, and branded themes
- avoid overly transparent surfaces for important dialogs

## Non-Goals

Contacts is not:

- a CRM automation system
- an email marketing platform
- a public directory
- a user provisioning flow
- an account creation flow
- a permission management tool
- a replacement for workspace membership
- a phone contact sync engine in the first version
- a CardDAV server in the first version

## Business Value

Contacts makes DNA-Nexus more useful for business customers.

It supports practical workflows such as:

- storing customer information
- sharing a supplier contact inside a workspace
- sending customer details to an external collaborator
- exporting contacts for business administration
- importing contacts from existing tools

For local operators selling DNA-Nexus Server, Contacts helps the product feel more like a business-ready private cloud instead of only a file manager.

## Future Ideas

Possible later additions:

- merge duplicate contacts
- CardDAV support
- Android selected-contact import
- contact groups
- richer company records
- customer-specific notes
- interaction history
- audit/activity entries for major contact actions
- import preview screen
- per-field validation hints
- encrypted offline mobile cache
- contact attachments
- optional organization-wide address book
- admin-configurable import/export policies

## Open Questions

- Should Contacts support organization-wide shared contacts?
- Should there be separate personal and business contact books?
- Should imports require preview or confirmation before saving?
- Should duplicate merge be automatic, manual, or never automatic?
- Should contact cards support custom fields?
- Should external workspace contact cards hide some fields by default?
- Should vCard export include the generated manual contact identity?
- Should deleted contacts go to a trash or recovery state or be permanently deleted?
- Should admin have any visibility into contact metadata for abuse handling?
