# MEGA-RAW — RetroAchievements, raw on real Mega Drive hardware

**[ Deutsch ](#deutsch)  |  [ English ](#english)**

---

<a name="deutsch"></a>

*RAW: weil es auf echter Hardware laeuft, nicht im Emulator.*

**Schalte RetroAchievements auf echter Sega-Mega-Drive-/Genesis-Hardware frei — über das Mega EverDrive.**

Dieses Tool patcht Mega-Drive-ROMs so, dass der Spielstand während des Spielens
per USB ausgelesen wird, wertet die RetroAchievements-Bedingungen live aus und
bucht freigeschaltete Achievements auf dein RA-Konto — alles auf der echten
Konsole, kein Emulator.

> **WARNUNG:** Experimentelles Hobby-Projekt. Nutzung auf eigene Verantwortung.
> Kein garantierter Support. Es bucht ausschliesslich im Softcore-Modus.
> Kein offizielles RetroAchievements-Produkt.

*by Liqui*

---

## Was es kann

- ROMs fuer RetroAchievements patchen (einzeln, ganzer Ordner, oder beim Kopieren auf SD)
- Live-Ueberwachung des Spielstands ueber die EverDrive-USB-Verbindung
- Automatische Freischaltung und Buchung auf dein RA-Konto
- Achievement-Liste mit Original-Badges (Icons) wie auf der RA-Website;
  Klick auf ein Achievement oeffnet seine Seite auf retroachievements.org
- Goldener Bildschirm-Blitz auf dem TV bei jedem Unlock
- Automatische Spielerkennung, Boot-Risiko-Analyse, Verifikations-Reports
- Deutsch und Englisch (in den Optionen umschaltbar)

## Abdeckung (gemessen, nicht geschaetzt)

- Engine wertet 89 % der Mega-Drive-Sets vollstaendig aus, 11 % teilweise, 0 % gar nicht
- Von erkannten Spielen mit Set: ~98 % patchbar
- Nicht kompatibel: 4-MB-Maximalspiele (~1 %, z. B. Mortal Kombat 3) — technische
  Grenze des Mega-Drive-Adressraums
- Verbleibende Teil-Luecken betreffen fast nur Pointer-Konstrukte

---

## INSTALLATION (Schritt fuer Schritt)

### 1. Alle Dateien in EINEN Ordner

Diese Dateien gehoeren zusammen in denselben Ordner. Ohne eine davon startet
das Tool nicht:

| Datei | Aufgabe |
|-------|---------|
| `md_ra_tool.py` | Das Tool (Oberflaeche) |
| `ra_condition.py` | Die Achievement-Engine — OHNE DIESE STARTET NICHTS |
| `ed_serial.py` | Die USB-Verbindung zum EverDrive |
| `edlink.exe` | USB-Werkzeug von KRIKzz — NICHT enthalten, separat herunterladen (siehe unten) |
| `md_set_scan.py` | (optional) Set-Abdeckung scannen |
| `mega_raw_intro.html` | (optional) die Startanimation |
| `install.bat` | installiert die benoetigten Pakete (siehe unten) |
| `requirements.txt` | Liste der Pakete (wird von install.bat genutzt) |

Der Ordner darf ueberall liegen (Desktop, C:\megalink, USB-Stick) — das Tool
findet seine Dateien selbst, relativ zum eigenen Ort. Du kannst den Ordner
jederzeit verschieben.

**edlink.exe** ist nicht enthalten und muss separat von KRIKzz heruntergeladen
und in denselben Ordner gelegt werden. Sie ist Teil des offiziellen Megalink-
bzw. Dev-Pakets von KRIKzz: https://github.com/krikzz/mega-ed-pub
(Datei `edlink.exe`). Ohne sie laeuft das Tool, aber die Verbindung zur Konsole
(Monitor) funktioniert nicht.

### 2. Python installieren

1. Von python.org den Windows-Installer holen und starten.
2. WICHTIG: Beim Installer ganz unten das Haekchen **"Add python.exe to PATH"**
   setzen, BEVOR du auf Install klickst. Fehlt das Haekchen, funktioniert weder
   der Doppelklick noch das Install-Skript.

### 3. Pakete installieren — einfach `install.bat` doppelklicken

Doppelklick auf **`install.bat`**. Das Skript erledigt alles Noetige automatisch.

**Was install.bat genau macht:**
1. Es prueft, ob Python ueberhaupt installiert ist. Fehlt es, sagt es dir das
   und verweist auf python.org (es installiert Python NICHT selbst).
2. Es installiert die drei Bibliotheken, die das Tool braucht:
   - **pyserial** — fuer die USB-Verbindung zum EverDrive
   - **capstone** — fuer die Analyse der ROM beim Patchen
   - **pywebview** — fuer die Startanimation
3. Danach meldet es, ob alles geklappt hat.

Das Skript aendert nichts an deinem System ausser diesen drei Python-Paketen
und braucht keine Administratorrechte.

**Warum keine .exe?** Aus Gruenden der Transparenz wird MEGA-RAW nicht als
fertige .exe verteilt, sondern als einzelne, lesbare Python-Dateien. So kann
jeder nachvollziehen, was das Tool tut. Das Install-Skript ist nur dazu da, dir
die Einrichtung trotzdem so einfach wie moeglich zu machen.

Wer es lieber von Hand macht, kann stattdessen in PowerShell eingeben:

```
pip install pyserial capstone pywebview
```

(Fuer RAR-Archive zusaetzlich: `pip install rarfile` + WinRAR installiert.)

### 4. Starten

Doppelklick auf `md_ra_tool.py` — oder in PowerShell:

```
cd "PFAD\ZU\DEINEM\ORDNER"
python md_ra_tool.py
```

---

## PROBLEMLOESUNG (haeufige Stolpersteine)

**Beim Doppelklick blitzt nur kurz ein schwarzes Fenster auf und verschwindet.**
Das Tool startet und stuerzt sofort ab — fast immer wegen fehlender Pakete.
Loesung: `install.bat` doppelklicken (siehe Schritt 3). Um den genauen Fehler
zu sehen, eine Datei `start.bat` im Ordner anlegen mit:
```
@echo off
cd /d "%~dp0"
python md_ra_tool.py
pause
```
Doppelklick darauf — das Fenster bleibt offen und zeigt die Fehlermeldung.

**"python wird nicht erkannt" / "is not recognized".**
Das PATH-Haekchen bei der Python-Installation fehlte. Python-Installer nochmal
starten -> "Modify" -> "Add Python to environment variables" aktivieren.

**Die Startanimation erscheint nicht.**
Die Animation braucht das Paket pywebview (wird von install.bat mitinstalliert)
und die Datei `mega_raw_intro.html` im Ordner. Fehlt eines davon, startet das
Tool trotzdem normal — nur eben ohne Animation.

**Antiviren-Fehlalarm beim Stapel-Patchen.**
Das Tool schreibt viele Dateien schnell — manche Scanner (z. B. G DATA
AntiRansomware/BEAST) werten das als Ransomware (Fehlalarm, es wird nichts
verschluesselt). Loesung: Tool-Ordner als Ausnahme eintragen, oder den
Verhaltensschutz fuer die Dauer des Stapellaufs pausieren, oder den
"AV-schonenden Modus" in den Optionen aktivieren.

**"kein EverDrive gefunden" beim Monitor-Start.**
Das Tool sucht den COM-Port automatisch — wenn gar keiner gefunden wird, fehlt
meist der USB-Treiber des EverDrive, oder die Konsole/das Kabel ist nicht
angeschlossen. Konsole muss zum STARTEN des Tools nicht an sein, aber fuer den
MONITOR schon. Wird das Kabel waehrend des Monitorings gezogen, zeigt das Tool
"DISCONNECT" an.

**Grafikfehler / Flackern bei einem bestimmten Spiel.**
In seltenen Faellen ist der eingefuegte VBlank-Stub fuer ein besonders
zeitkritisches Spiel zu gross und verursacht Grafikfehler (z. B. flackernde
oder doppelte Bildteile). Loesung: dieses Spiel einzeln erneut patchen und vor
dem Patchen die Checkbox **"Light-Stub"** aktivieren. Der Light-Stub verteilt
das Auslesen des Speichers rotierend ueber mehrere Frames und senkt die Last pro
Frame deutlich. Hinweis: Mit dem Light-Stub erscheint kein goldener TV-Blitz
beim Unlock; Achievements und Spielerkennung funktionieren normal weiter.

---

## Voraussetzungen

- Mega EverDrive **CORE** oder **PRO** (mit USB) von krikzz.com.
  (Entwickelt und getestet mit dem CORE. Der PRO nutzt dieselbe USB-Schnittstelle
  und sollte daher ebenfalls funktionieren, ist aber noch nicht getestet. Andere
  EverDrive-Modelle ohne USB-Port werden nicht unterstuetzt.)
- Sega Mega Drive / Genesis (echte Hardware)
- Windows-PC mit Python 3
- Ein RetroAchievements-Konto (retroachievements.org)

## Schnellstart (Bedienung)

1. Tool starten, mit RA-Konto einloggen
2. ROM laden (DURCHSUCHEN) -> IPS PATCH ERSTELLEN
3. ROM UND die erzeugte `.ips` zusammen auf die EverDrive-SD legen
4. Spiel auf der Konsole starten, im Tool MONITOR STARTEN
5. Spielen — Achievements schalten sich live frei

**Ganzer Ordner / SD-Export:** Beim Patchen ganzer Ordner oder beim SD-Export
sortiert das Tool die Spiele automatisch in Buchstaben-Ordner (max. 800 Dateien
pro Ordner, ein Buchstabe wird nie geteilt) und zeigt am Ende eine Aufschluesselung.
Spiele mit RA-Set, die nicht gepatcht werden konnten (meist kein Platz im ROM),
landen in `mega_raw_nicht_gepatcht.txt` auf der SD. Spiele ohne Set sind normal
und werden dort nicht gelistet.

## PAL / NTSC

Einige Sets verlangen 60 Hz (NTSC). Auf einer 50-Hz-PAL-Konsole sind diese
Achievements gesperrt (RA-Vorgabe, nicht durch das Tool). Das Tool erkennt den
Hz-Status der laufenden Konsole und zeigt betroffene Achievements an.

---

## Fairness & Cheat-Prevention

MEGA-RAW ist ein **inoffizieller** Client und kein Produkt von
RetroAchievements. Damit das Freischalten auf echter Hardware fair bleibt, ist
das Tool bewusst mit mehreren Einschraenkungen gebaut:

- **Nur Softcore.** Das Tool bucht ausschliesslich im Softcore-Modus. Hardcore
  ist vorerst gesperrt, da eine genauere Ueberpruefung der Hardcore-Anforderungen
  noch aussteht.

- **Integritaetspruefung.** Das Tool stellt waehrend des Spielens sicher, dass
  auf der Konsole die unveraenderte, selbst gepatchte ROM laeuft. Schlaegt diese
  Pruefung fehl, werden keine Achievements gebucht.

- **Session-Protokoll.** Jede Ueberwachungs-Session wird mitgeschrieben, sodass
  Freischaltungen nachvollziehbar bleiben.

- **Server-Schonung.** Die Anfragen an die RA-Server sind fest gedrosselt.

Dieses Tool ist fuer den Einsatz mit deinen eigenen, legalen ROMs gedacht.
Bitte respektiere die Regeln und die Community von RetroAchievements.

## Lizenz

(c) Liqui. Nutzung und Weitergabe fuer private, nicht-kommerzielle Zwecke
gestattet. Keine Gewaehrleistung. Kein offizielles RetroAchievements-Produkt.

---
---

<a name="english"></a>

*RAW: because it runs on real hardware, not in an emulator.*

**Unlock RetroAchievements on real Sega Mega Drive / Genesis hardware — via the Mega EverDrive.**

This tool patches Mega Drive ROMs so the game state can be read out via USB while
you play, evaluates the RetroAchievements conditions live, and unlocks earned
achievements on your RA account — all on the real console, no emulator.

> **WARNING:** Experimental hobby project. Use at your own risk. No guaranteed
> support. It unlocks in Softcore mode only. Not an official RetroAchievements
> product.

*by Liqui*

---

## What it does

- Patch ROMs for RetroAchievements (single, whole folder, or while copying to SD)
- Live monitoring of the game state via the EverDrive USB connection
- Automatic unlocking and submission to your RA account
- Achievement list with original badges (icons) like on the RA website;
  clicking an achievement opens its page on retroachievements.org
- Golden screen flash on the TV on every unlock
- Automatic game detection, boot-risk analysis, verification reports
- German and English (switchable in the options)

## Coverage (measured, not estimated)

- The engine fully evaluates 89% of Mega Drive sets, 11% partially, 0% not at all
- Of detected games with a set: ~98% patchable
- Not compatible: 4 MB maximum-size games (~1%, e.g. Mortal Kombat 3) — a
  technical limit of the Mega Drive address space
- Remaining partial gaps almost only concern pointer constructs

---

## INSTALLATION (step by step)

### 1. All files in ONE folder

These files belong together in the same folder. Without any one of them, the
tool won't start:

| File | Purpose |
|------|---------|
| `md_ra_tool.py` | The tool (interface) |
| `ra_condition.py` | The achievement engine — WITHOUT THIS NOTHING STARTS |
| `ed_serial.py` | The USB connection to the EverDrive |
| `edlink.exe` | USB tool by KRIKzz — NOT included, download separately (see below) |
| `mega_raw_intro.html` | (optional) the startup animation |
| `install.bat` | installs the required packages (see below) |
| `requirements.txt` | list of packages (used by install.bat) |

The folder can be located anywhere (desktop, C:\megalink, USB stick) — the tool
finds its files itself, relative to its own location. You can move the folder
anytime.

**edlink.exe** is not included and must be downloaded separately from KRIKzz and
placed in the same folder. It is part of KRIKzz' official Megalink / dev package:
https://github.com/krikzz/mega-ed-pub (file `edlink.exe`). Without it the tool
runs, but the connection to the console (monitor) will not work.

### 2. Install Python

1. Get the Windows installer from python.org and run it.
2. IMPORTANT: At the bottom of the installer, tick **"Add python.exe to PATH"**
   BEFORE clicking Install. Without that tick, neither the double-click nor the
   install script will work.

### 3. Install packages — just double-click `install.bat`

Double-click **`install.bat`**. The script does everything needed automatically.

**What install.bat does exactly:**
1. It checks whether Python is installed at all. If not, it tells you and points
   to python.org (it does NOT install Python itself).
2. It installs the three libraries the tool needs:
   - **pyserial** — for the USB connection to the EverDrive
   - **capstone** — for analyzing the ROM during patching
   - **pywebview** — for the startup animation
3. It then reports whether everything worked.

The script changes nothing on your system except these three Python packages and
requires no administrator rights.

**Why no .exe?** For the sake of transparency, MEGA-RAW is not distributed as a
finished .exe but as individual, readable Python files. That way anyone can see
what the tool does. The install script just exists to make setup as easy as
possible anyway.

If you prefer to do it by hand, you can instead type in PowerShell:

```
pip install pyserial capstone pywebview
```

(For RAR archives additionally: `pip install rarfile` + WinRAR installed.)

### 4. Start

Double-click `md_ra_tool.py` — or in PowerShell:

```
cd "PATH\TO\YOUR\FOLDER"
python md_ra_tool.py
```

---

## TROUBLESHOOTING (common pitfalls)

**On double-click, a black window only flashes briefly and disappears.**
The tool starts and crashes immediately — almost always due to missing packages.
Solution: double-click `install.bat` (see step 3). To see the exact error,
create a file `start.bat` in the folder containing:
```
@echo off
cd /d "%~dp0"
python md_ra_tool.py
pause
```
Double-click it — the window stays open and shows the error message.

**"python is not recognized".**
The PATH tick was missing during Python installation. Run the Python installer
again -> "Modify" -> enable "Add Python to environment variables".

**The startup animation doesn't appear.**
The animation needs the pywebview package (installed by install.bat) and the
file `mega_raw_intro.html` in the folder. If either is missing, the tool still
starts normally — just without the animation.

**Antivirus false positive during batch patching.**
The tool writes many files quickly — some scanners (e.g. G DATA
AntiRansomware/BEAST) interpret this as ransomware (false positive, nothing is
encrypted). Solution: add the tool folder as an exception, or pause the behavior
protection for the duration of the batch run, or enable "AV mode" in the options.

**"no EverDrive found" when starting the monitor.**
The tool searches the COM port automatically — if none is found at all, the
EverDrive's USB driver is usually missing, or the console/cable isn't connected.
The console doesn't need to be on to START the tool, but it does for the MONITOR.
If the cable is pulled during monitoring, the tool shows "DISCONNECT".

**Graphics glitches / flicker in a particular game.**
In rare cases the inserted VBlank stub is too large for an especially
timing-critical game and causes graphics glitches (e.g. flickering or
duplicated parts of the picture). Solution: re-patch that game individually and
tick the **"Light stub"** checkbox before patching. The light stub spreads the
memory read-out across multiple frames in rotation and significantly lowers the
per-frame load. Note: with the light stub there is no golden TV flash on unlock;
achievements and game detection keep working normally.

---

## Requirements

- Mega EverDrive **CORE** or **PRO** (with USB) from krikzz.com.
  (Developed and tested with the CORE. The PRO uses the same USB interface and
  should therefore work as well, but has not been tested yet. Other EverDrive
  models without a USB port are not supported.)
- Sega Mega Drive / Genesis (real hardware)
- Windows PC with Python 3
- A RetroAchievements account (retroachievements.org)

## Quick start (usage)

1. Start the tool, log in with your RA account
2. Load a ROM (BROWSE) -> CREATE IPS PATCH
3. Put the ROM AND the generated `.ips` together on the EverDrive SD card
4. Start the game on the console, click START MONITOR in the tool
5. Play — achievements unlock live

**Whole folder / SD export:** When patching a whole folder or exporting to SD,
the tool auto-sorts games into letter folders (max. 800 files per folder, a letter
is never split) and shows a breakdown at the end. Games with an RA set that could
not be patched (usually no room in the ROM) are written to
`mega_raw_nicht_gepatcht.txt` on the SD. Games without a set are normal and are
not listed there.

## PAL / NTSC

Some sets require 60 Hz (NTSC). On a 50 Hz PAL console these achievements are
locked (an RA rule, not by the tool). The tool detects the Hz status of the
running console and marks affected achievements.

---

## Fairness & Cheat Prevention

MEGA-RAW is an **unofficial** client and not a product of RetroAchievements. To
keep unlocking on real hardware fair, the tool is deliberately built with several
restrictions:

- **Softcore only.** The tool unlocks in Softcore mode exclusively. Hardcore is
  locked for now, as a closer review of the hardcore requirements is still
  pending.

- **Integrity check.** While you play, the tool ensures that the console is
  running the unaltered ROM you patched yourself. If this check fails, no
  achievements are submitted.

- **Session log.** Every monitoring session is recorded, so unlocks remain
  traceable.

- **Server-friendly.** Requests to the RA servers are firmly rate-limited.

This tool is intended for use with your own, legal ROMs. Please respect the rules
and the community of RetroAchievements.

## License

(c) Liqui. Use and distribution permitted for private, non-commercial purposes.
No warranty. Not an official RetroAchievements product.
