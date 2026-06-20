# Operator branding assets

This directory is reserved for deployment-provided white-label branding assets.

The product should not expose a normal end-user/admin UI editor for these files.
For operator deployments, branding is installed as part of a paid deployment or managed service package.

Typical installed paths:

/opt/pqnas/static/branding/logo-dark.png
/opt/pqnas/static/branding/logo-bright.png
/opt/pqnas/static/branding/favicon.png

Typical branding.json values:

{
  "enabled": true,
  "product_name": "Operator Cloud",
  "product_short_name": "Operator Cloud",
  "company_name": "Operator Ltd",
  "copyright": "© Operator Ltd",
  "hide_upstream_brand": true,
  "logo_dark": "/static/branding/logo-dark.png",
  "logo_bright": "/static/branding/logo-bright.png",
  "favicon": "/static/branding/favicon.png",
  "presentation_url": "",
  "show_presentation_link": false
}

Do not commit customer logos unless they are explicitly licensed for inclusion.
