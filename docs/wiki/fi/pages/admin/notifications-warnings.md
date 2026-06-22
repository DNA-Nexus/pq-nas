# Ilmoitukset ja varoitukset

DNA-Nexus / PQ-NAS voi lähettää ylläpitäjälle ilmoituksia ja operatiivisia varoituksia.

Asetukset löytyvät kohdasta:

`Ylläpito → Asetukset → Notifications + Warnings`

## Mihin tätä käytetään

Viestit jakautuvat kahteen ryhmään:

**Ilmoitukset** ovat rauhallisia ylläpitoyhteenvetoja. MVP lähettää viikoittaisen yhteenvedon, jossa näkyy:

- käyttäjämäärä ja kasvu edelliseen yhteenvetoon verrattuna
- `pqnas.service`-palvelun tila
- palvelun käynnistysaika
- tallennustilan yhteenveto

**Varoitukset** ovat kiireellisempiä operatiivisia tarkistuksia. MVP tarkistaa 15 minuutin välein:

- onko vapaa levytila vähissä
- onko `pqnas.service` aktiivinen
- epäonnistuuko tallennustilan tarkistus

Samaa varoitusta ei lähetetä jatkuvasti uudestaan, vaan varoituksissa on throttlaus.

## Lähetyskanavat

MVP tukee:

- Telegramia
- email-lähetystä SMTP:n kautta
- email-lähetystä paikallisen `sendmail` / `msmtp` -komennon kautta, jos sellainen on konfiguroitu serverille

Ylläpitäjä voi ottaa emailin, Telegramin tai molemmat käyttöön erikseen ilmoituksille ja varoituksille.

## Emailin käyttöönotto

Email-lähetys konfiguroidaan serverille keskitetysti. Käyttäjien ja adminien ei tarvitse antaa omien sähköpostitiliensä salasanoja.

SMTP-lähetystä varten lisää nämä arvot tiedostoon `/etc/pqnas/pqnas.env`:

```text
PQNAS_SMTP_HOST="smtp.example.com"
PQNAS_SMTP_PORT="587"
PQNAS_SMTP_TLS="starttls"
PQNAS_SMTP_USER="admin@example.com"
PQNAS_SMTP_PASSWORD="app-password-or-smtp-password"
PQNAS_SMTP_FROM="DNA-Nexus <admin@example.com>"
```

Jos `PQNAS_SMTP_HOST` ei ole asetettu, worker yrittää käyttää paikallista `sendmail`- tai `msmtp`-komentoa, jos sellainen on asennettu ja konfiguroitu.

Kun `/etc/pqnas/pqnas.env` muuttuu, käynnistä palvelu uudelleen:

```bash
sudo systemctl restart pqnas.service
```

Testaa email-lähetys:

```bash
sudo bash -c 'set -a; . /etc/pqnas/pqnas.env; set +a; exec sudo -E -u pqnas /usr/local/libexec/pqnas/pqnas_notify.py --test-email'
```

Admin-käyttöliittymässä on myös **Send test email** -painike.

## Telegramin käyttöönotto

Telegramin käyttö:

1. Luo tai käytä olemassa olevaa Telegram-bottia.
2. Hae bot token BotFatherilta.
3. Hae kohdechatin chat ID.
4. Avaa `Ylläpito → Asetukset → Notifications + Warnings`.
5. Ota Telegram käyttöön ilmoituksille ja/tai varoituksille.
6. Syötä bot token ja chat ID.
7. Tallenna asetukset.
8. Testaa painikkeella **Send test Telegram**.

Telegram-token tallennetaan serverille, eikä sitä palauteta selaimeen raakana. Tallennuksen jälkeen UI näyttää vain maskatun tokenin.

## Runtime-tiedostot

Ilmoitusasetukset tallennetaan tiedostoon:

`/etc/pqnas/notifications.json`

Odotetut oikeudet:

```text
600 pqnas:pqnas /etc/pqnas/notifications.json
```

SMTP-salaisuudet tallennetaan tiedostoon:

`/etc/pqnas/pqnas.env`

Odotetut oikeudet:

```text
600 root:root /etc/pqnas/pqnas.env
```

Varoitusten throttlaus ja viikkoyhteenvetojen laskurit tallennetaan tänne:

`/var/lib/pqnas/notifications/`

## systemd-timerit

Installer ottaa käyttöön kaksi timeria:

```text
pqnas-notify-warnings.timer
pqnas-notify-weekly.timer
```

Varoitukset ajetaan 15 minuutin välein.

Viikkoyhteenveto ajetaan maanantaisin klo 08:00.

Timerien tilan voi tarkistaa näin:

```bash
systemctl list-timers 'pqnas-notify-*' --no-pager
systemctl status pqnas-notify-warnings.timer --no-pager
systemctl status pqnas-notify-weekly.timer --no-pager
```

Tarkistukset voi ajaa käsin näin:

```bash
sudo systemctl start pqnas-notify-warnings.service
sudo journalctl -u pqnas-notify-warnings.service -n 80 --no-pager

sudo systemctl start pqnas-notify-weekly.service
sudo journalctl -u pqnas-notify-weekly.service -n 80 --no-pager
```

## Turvallisuushuomiot

Telegram bot tokenit ja SMTP-salasanat ovat salaisuuksia. Älä committaa niitä Gitiin, äläkä liitä tuotantosalaisuuksia lokeihin, tiketteihin tai julkisiin chatteihin.

Kuluttajasähköposteissa, kuten AOL tai Gmail, käytä app passwordia normaalin tilisalasanan sijasta.
