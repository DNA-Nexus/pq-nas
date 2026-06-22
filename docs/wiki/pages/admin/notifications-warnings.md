# Notifications + Warnings

DNA-Nexus / PQ-NAS can send administrator notifications and operational warnings.

This feature is configured from:

`Admin → Settings → Notifications + Warnings`

## What it is for

There are two types of messages:

**Notifications** are calm operational summaries. The first MVP sends a weekly Telegram summary with:

- user count and user growth since the previous summary
- `pqnas.service` status
- service start time
- storage usage summary

**Warnings** are urgent operational checks. The first MVP checks every 15 minutes for:

- low free storage space
- `pqnas.service` not being active
- storage check failures

Warnings are throttled so the same warning is not sent repeatedly too often.

## Delivery channels

The current MVP supports Telegram delivery.

Email options are already visible in the UI, but email sending is not active yet. Email delivery will be implemented later with SMTP or a local sendmail/msmtp policy.

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

Settings are stored in:

`/etc/pqnas/notifications.json`

Expected permissions:

```text
600 pqnas:pqnas /etc/pqnas/notifications.json
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

Test Telegram from the worker:

```bash
sudo -u pqnas /usr/local/libexec/pqnas/pqnas_notify.py --test-telegram
```

## Security notes

Telegram bot tokens are secrets. Do not commit them to Git and do not paste production tokens into logs, tickets, or public chats.

If a token is exposed, rotate it in BotFather and update the setting in the admin UI.
