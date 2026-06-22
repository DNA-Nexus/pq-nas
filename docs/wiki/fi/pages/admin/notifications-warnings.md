# Ilmoitukset ja varoitukset

DNA-Nexus / PQ-NAS voi lähettää ylläpitäjälle ilmoituksia ja operatiivisia varoituksia.

Asetukset löytyvät kohdasta:

`Ylläpito → Asetukset → Notifications + Warnings`

## Mihin tätä käytetään

Viestit jakautuvat kahteen ryhmään:

**Ilmoitukset** ovat rauhallisia ylläpitoyhteenvetoja. Ensimmäinen MVP lähettää viikoittaisen Telegram-yhteenvedon, jossa näkyy:

- käyttäjämäärä ja kasvu edelliseen yhteenvetoon verrattuna
- `pqnas.service`-palvelun tila
- palvelun käynnistysaika
- tallennustilan yhteenveto

**Varoitukset** ovat kiireellisempiä operatiivisia tarkistuksia. Ensimmäinen MVP tarkistaa 15 minuutin välein:

- onko vapaa levytila vähissä
- onko `pqnas.service` aktiivinen
- epäonnistuuko tallennustilan tarkistus

Samaa varoitusta ei lähetetä jatkuvasti uudestaan, vaan varoituksissa on throttlaus.

## Lähetyskanavat

Nykyinen MVP tukee Telegram-lähetystä.

Email-valinnat näkyvät jo käyttöliittymässä, mutta email-lähetys ei ole vielä käytössä. Email toteutetaan myöhemmin joko SMTP-asetuksilla tai paikallisella sendmail/msmtp-ratkaisulla.

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

Asetukset tallennetaan tiedostoon:

`/etc/pqnas/notifications.json`

Odotetut oikeudet:

```text
600 pqnas:pqnas /etc/pqnas/notifications.json
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

Telegramin voi testata workerillä näin:

```bash
sudo -u pqnas /usr/local/libexec/pqnas/pqnas_notify.py --test-telegram
```

## Turvallisuushuomiot

Telegram bot token on salaisuus. Älä committaa sitä Gitiin, äläkä liitä tuotantotokenia lokeihin, tiketteihin tai julkisiin chatteihin.

Jos token paljastuu, vaihda se BotFatherissa ja päivitä uusi token admin-käyttöliittymästä.
