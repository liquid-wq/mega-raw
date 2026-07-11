# MEGA-RAW

**RetroAchievements on Real Mega Drive Hardware**

Vollautomatisch: Login → Spiel erkennen → RA-Conditions laden → RAM-Adressen extrahieren → IPS patchen → Monitor → Achievements awarden

---

© 2026 Liqui. Alle Rechte vorbehalten.  
Privates, nicht-kommerzielles Hobby-Projekt.  
Weitergabe und Nutzung nur unter Nennung des Urhebers (Liqui).  
Keine kommerzielle Nutzung, keine Veröffentlichung unter fremdem Namen.  
Kein offizielles RetroAchievements-Produkt.

---

## Voraussetzungen

- Sega Mega Drive / Genesis mit KRIKzz EverDrive (Mega Core / Pro)
- RetroAchievements-Account (kostenlos auf retroachievements.org)
- Windows 10/11 (64-bit)

## Schnellstart

1. **Einloggen** — RA-Benutzername und Passwort eingeben
2. **ROM auswählen** — per „Durchsuchen" eine gepatchte oder ungepatchte ROM laden
3. **IPS Patch erstellen** — einmalig pro Spiel, legt eine `.ips`-Datei neben die ROM
4. **Spiel starten** — EverDrive wendet den IPS-Patch automatisch an
5. **Monitor starten** — Tool verbindet sich mit dem EverDrive und trackt Achievements live

## SD-Export

Exportiert eine komplette ROM-Sammlung auf einmal: alle Spiele mit RetroAchievements-Set werden automatisch gepatcht und in den Zielordner kopiert. Nicht patchbare Spiele werden in `mega_raw_nicht_gepatcht.txt` dokumentiert.

## TV-Blitz

Bei jeder Achievement-Freischaltung pulsiert der Bildschirm golden auf — direkt über das EverDrive MBX-Register, ohne zusätzliche Hardware.

## Update-Prüfung

Beim Start wird automatisch geprüft ob eine neuere Version verfügbar ist.  
Support und Downloads: https://liquid-wq.github.io/support/

## Hinweise

- Das Tool speichert **keine** Passwörter — der Login-Token wird nur im RAM gehalten
- Für Ransomware-Fehlalarme: AV-Modus in den Optionen aktivieren
- COM-Port des EverDrive wird automatisch erkannt
