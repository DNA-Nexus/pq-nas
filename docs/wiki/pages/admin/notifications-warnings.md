# Notifications + Warnings

DNA-Nexus / PQ-NAS can send administrator notifications and operational warnings.

This feature is configured from:

`Admin → Settings → Notifications + Warnings`

## What it is for

There are two types of messages:

**Notifications** are calm operational summaries. The MVP sends a weekly summary with:

- user count and user growth since the previous summary
- `pqnas.service` status
- service start time
- storage usage summary

**Warnings** are urgent operational checks. The MVP checks every 15 minutes for:

- low free storage space
- `pqnas.service` not being active
- storage check failures

Warnings are throttled so the same warning is not sent repeatedly too often.

## Delivery channels

The MVP supports:

- Telegram
- Email through SMTP
- Email through local `sendmail` / `msmtp` if configured on the server

Administrators can enable email, Telegram, or both separately for notifications and warnings.

## Email setup

Email delivery is configured server-side. Users and admins do not need to provide their own mailbox passwords.

For SMTP delivery, set these values in `/etc/pqnas/pqnas.env`:

```text
PQNAS_SMTP_HOST="smtp.example.com"
PQNAS_SMTP_PORT="587"
PQNAS_SMTP_TLS="starttls"
PQNAS_SMTP_USER="admin@example.com"
PQNAS_SMTP_PASSWORD="app-password-or-smtp-password"
PQNAS_SMTP_FROM="DNA-Nexus <admin@example.com>"
```

If `PQNAS_SMTP_HOST` is not set, the worker falls back to a local `sendmail` or `msmtp` command if one is installed and configured.

After changing `/etc/pqnas/pqnas.env`, restart the service:

```bash
sudo systemctl restart pqnas.service
```

Test email delivery:

```bash
sudo bash -c 'set -a; . /etc/pqnas/pqnas.env; set +a; exec sudo -E -u pqnas /usr/local/libexec/pqnas/pqnas_notify.py --test-email'
```

The admin UI also includes **Send test email**.

## Telegram setup

To use Telegram:

1. Create or use a Telegram bot.
2. Get the bot token from BotFather.
3. Get the target chat ID.
4. Open `Admin → Settings → Notifications + Warnings`.
5. Enable Telegram for Notifications and/or Warnings.
6. Enter the bot token and chat ID.
7. Save settings.
8. Use **Send test Telegram**.

The Telegram token is stored on the server and is never returned raw to the browser. The UI only shows a masked token placeholder after saving.

## Runtime files

Notification settings are stored in:

`/etc/pqnas/notifications.json`

Expected permissions:

```text
600 pqnas:pqnas /etc/pqnas/notifications.json
```

SMTP secrets are stored in:

`/etc/pqnas/pqnas.env`

Expected permissions:

```text
600 root:root /etc/pqnas/pqnas.env
```

Runtime state for warning throttling and weekly counters is stored under:

`/var/lib/pqnas/notifications/`

## systemd timers

The installer enables two timers:

```text
pqnas-notify-warnings.timer
pqnas-notify-weekly.timer
```

Warnings run every 15 minutes.

Weekly summaries run on Mondays at 08:00.

Check timer status:

```bash
systemctl list-timers 'pqnas-notify-*' --no-pager
systemctl status pqnas-notify-warnings.timer --no-pager
systemctl status pqnas-notify-weekly.timer --no-pager
```

Run checks manually:

```bash
sudo systemctl start pqnas-notify-warnings.service
sudo journalctl -u pqnas-notify-warnings.service -n 80 --no-pager

sudo systemctl start pqnas-notify-weekly.service
sudo journalctl -u pqnas-notify-weekly.service -n 80 --no-pager
```

## Security notes

Telegram bot tokens and SMTP passwords are secrets. Do not commit them to Git and do not paste production secrets into logs, tickets, or public chats.

For consumer mailboxes such as AOL or Gmail, use an app password instead of the normal account password.
