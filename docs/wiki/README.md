# DNA-Nexus Wiki Starter

This is a static HTML starter wiki for DNA-Nexus documentation.

## Files

- `index.html` — wiki front page
- `assets/css/wiki.css` — shared style
- `pages/getting-started/server-installation.html` — first installation article
- `pages/getting-started/first-login.html` — first login article
- `pages/administration/create-users.html` — creating users article
- `pages/page-template.html` — reusable page template

## Screenshot workflow

Place screenshots under:

`assets/images/installation/`

Recommended naming:

- `01-boot-installer.png`
- `02-network.png`
- `03-storage-selection.png`
- `04-admin-account.png`
- `05-login-page.png`

Then replace the placeholder boxes with image tags like:

```html
<img class="wiki-img" src="../../assets/images/installation/01-boot-installer.png" alt="Installer boot screen">
```