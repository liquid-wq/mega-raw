# MEGA-RAW (C++) — RetroAchievements, raw on real Mega Drive hardware

**[ Deutsch ](#deutsch)  |  [ English ](#english)**

---

<a name="deutsch"></a>

*RAW: weil es auf echter Hardware laeuft, nicht im Emulator.*

**Schalte RetroAchievements auf echter Sega-Mega-Drive-/Genesis-Hardware frei — über das Mega EverDrive.**

Dieses Tool patcht Mega-Drive-ROMs so, dass der Spielstand während des Spielens
per USB ausgelesen wird, wertet die RetroAchievements-Bedingungen live aus und
bucht freigeschaltete Achievements auf dein RA-Konto — alles auf der echten
Konsole, kein Emulator.

Dies ist die **C++/Qt-Portierung** von MEGA-RAW — funktional weitgehend
angeglichen an die Python-Version, aber als eigenstaendige `.exe` ohne
Python-Installation.

> **Empfehlung:** Fuer die meisten Nutzer ist diese C++-Version aufgrund der
> Kompatibilitaet zu empfehlen — einfach entpacken und starten, keine
> Python-Installation noetig. Die Python-Version bleibt parallel verfuegbar,
> u. a. fuer alle, die den Quellcode ohne kompilierte `.exe` direkt lesen und
> ausfuehren moechten.

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
- Automatische Spielerkennung, Verifikations-Reports
- Deutsch und Englisch (in den Optionen umschaltbar)
- Prueft beim Start automatisch auf neue Versionen

## Abdeckung

- Engine wertet 89 % der Mega-Drive-Sets vollstaendig aus, 11 % teilweise, 0 % gar nicht
- Theoretisch ist eine Patch-Abdeckung von 90 %+ moeglich — in der Praxis haengt
  das davon ab, wie vollstaendig die Stub-Datenbank bekannte Problemfaelle abdeckt
  (Spiele, bei denen die automatische Stub-Platzierung fehlschlaegt). Diese
  Datenbank waechst laufend durch Tests und kann per Knopfdruck aktualisiert werden.
- Nicht kompatibel: 4-MB-Maximalspiele (~1 %, z. B. Mortal Kombat 3) — technische
  Grenze des Mega-Drive-Adressraums
- Verbleibende Luecken betreffen ueberwiegend Pointer-Konstrukte und noch nicht
  erfasste Einzelfaelle

---

## INSTALLATION (Schritt fuer Schritt)

### 1. Herunterladen und entpacken

Die `.exe` und alle mitgelieferten Dateien gehoeren zusammen in denselben
Ordner. Der Ordner darf ueberall liegen (Desktop, C:\megatest, USB-Stick) —
das Tool findet seine Dateien selbst, relativ zum eigenen Ort. Du kannst den
Ordner jederzeit verschieben.

### 2. Visual C++ Redistributable (falls noch nicht installiert)

Falls beim Start eine Fehlermeldung wie "VCRUNTIME140.dll fehlt" erscheint,
fehlt das Microsoft Visual C++ Redistributable. Kostenloser Download von
Microsoft: `vc_redist.x64.exe` (offizieller Link auf der Support-Seite).
Einmal installieren, danach betrifft es alle Programme die es brauchen.

### 3. Starten

Doppelklick auf `mega_raw_gui.exe`. Keine weitere Installation, kein Python
noetig.

**Warum eine .exe und keine Python-Dateien wie beim Original?** Diese Version
ist eine C++/Qt-Portierung mit weitgehend angeglichener Funktion, damit das
Tool ohne Python-Installation
und ohne PyInstaller-bedingte Antiviren-Fehlalarme laeuft. Der Quellcode liegt
bei, damit jeder selbst nachvollziehen kann, dass keine Schadsoftware
enthalten ist — nicht zur freien Weiterverwendung. Es gilt weiterhin die
Lizenz unten (private, nicht-kommerzielle Nutzung).

---

## PROBLEMLOESUNG (haeufige Stolpersteine)

**"VCRUNTIME140.dll fehlt" oder aehnliche DLL-Fehler beim Start.**
Microsoft Visual C++ Redistributable fehlt. Siehe Installation Schritt 2.

**Antiviren-Fehlalarm.**
Manche Scanner werten unbekannte/neue .exe-Dateien als verdaechtig ein
(Fehlalarm, es wird nichts verschluesselt). Loesung: Tool-Ordner als Ausnahme
eintragen, oder den "AV-schonenden Modus" in den Optionen aktivieren.

**"kein EverDrive gefunden" beim Monitor-Start.**
Das Tool sucht den COM-Port automatisch — wenn gar keiner gefunden wird, fehlt
meist der USB-Treiber des EverDrive, oder die Konsole/das Kabel ist nicht
angeschlossen. Konsole muss zum STARTEN des Tools nicht an sein, aber fuer den
MONITOR schon. Wird das Kabel waehrend des Monitorings gezogen, zeigt das Tool
die Verbindung als verloren an.

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
- Windows 10/11 (64-bit)
- Ein RetroAchievements-Konto (retroachievements.org)

## Schnellstart (Bedienung)

1. Tool starten, mit RA-Konto einloggen
2. ROM waehlen -> IPS PATCH ERSTELLEN
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

Manche Sets verlangen 60 Hz (NTSC). Auf einer 50-Hz-PAL-Konsole sind diese
Achievements gesperrt (eine RA-Regel, nicht durch das Tool). Das Tool erkennt
den Hz-Status der laufenden Konsole und markiert betroffene Achievements.

---

## Fairness & Cheat-Schutz

MEGA-RAW ist ein **inoffizieller** Client und kein Produkt von
RetroAchievements. Um Freischaltungen auf echter Hardware fair zu halten, ist
das Tool bewusst mit mehreren Einschraenkungen gebaut:

- **Nur Softcore.** Das Tool schaltet ausschliesslich im Softcore-Modus frei.
  Hardcore ist vorerst gesperrt, da eine genauere Pruefung der
  Hardcore-Anforderungen noch aussteht.

- **Integritaetspruefung.** Waehrend du spielst, stellt das Tool sicher, dass
  die Konsole die unveraenderte, selbst gepatchte ROM ausfuehrt. Schlaegt diese
  Pruefung fehl, werden keine Achievements gebucht.

- **Sitzungsprotokoll.** Jede Monitoring-Sitzung wird protokolliert, damit
  Freischaltungen nachvollziehbar bleiben.

- **Server-schonend.** Anfragen an die RA-Server sind fest gedrosselt.

Dieses Tool ist fuer die Nutzung mit eigenen, legalen ROMs gedacht. Bitte
respektiere die Regeln und die Community von RetroAchievements.

## Lizenz

© 2026 Liqui. Nutzung und Weitergabe nur fuer private, nicht-kommerzielle
Zwecke gestattet. Keine Gewaehrleistung. Kein offizielles
RetroAchievements-Produkt.

---

<a name="english"></a>

*RAW: because it runs on real hardware, not in an emulator.*

**Unlock RetroAchievements on real Sega Mega Drive / Genesis hardware — via the Mega EverDrive.**

This tool patches Mega Drive ROMs so the game state is read out via USB while
you play, evaluates RetroAchievements conditions live, and submits unlocked
achievements to your RA account — all on real console hardware, no emulator.

This is the **C++/Qt port** of MEGA-RAW — functionally closely aligned with
the Python version, but a standalone `.exe` with no Python installation
required.

> **Recommendation:** For most users, this C++ version is recommended for its
> compatibility — just unpack and run, no Python installation needed. The
> Python version remains available in parallel, e.g. for anyone who wants to
> read and run the source code directly without a compiled `.exe`.

> **WARNING:** Experimental hobby project. Use at your own risk. No guaranteed
> support. It only submits in Softcore mode. Not an official RetroAchievements
> product.

*by Liqui*

---

## What it can do

- Patch ROMs for RetroAchievements (individually, a whole folder, or while copying to SD)
- Live monitoring of game state over the EverDrive USB connection
- Automatic unlocking and submission to your RA account
- Achievement list with original badges (icons) like on the RA website;
  clicking an achievement opens its page on retroachievements.org
- Golden screen flash on the TV on every unlock
- Automatic game detection, verification reports
- German and English (switchable in the options)
- Automatically checks for new versions on startup

## Coverage

- The engine fully evaluates 89% of Mega Drive sets, 11% partially, 0% not at all
- A patch coverage of 90%+ is theoretically possible — in practice this depends
  on how completely the stub database covers known problem cases (games where
  automatic stub placement fails). This database grows continuously through
  testing and can be updated with a single click.
- Not compatible: 4 MB maximum-size games (~1%, e.g. Mortal Kombat 3) — a
  technical limit of the Mega Drive address space
- Remaining gaps mostly concern pointer constructs and not-yet-covered edge cases

---

## INSTALLATION (step by step)

### 1. Download and unpack

The `.exe` and all included files belong together in the same folder. The
folder can be located anywhere (desktop, C:\megatest, USB stick) — the tool
finds its files itself, relative to its own location. You can move the folder
anytime.

### 2. Visual C++ Redistributable (if not already installed)

If you see an error like "VCRUNTIME140.dll is missing" on startup, the
Microsoft Visual C++ Redistributable is missing. Free download from Microsoft:
`vc_redist.x64.exe` (official link on the support page). Install it once, it
then covers all programs that need it.

### 3. Start

Double-click `mega_raw_gui.exe`. No further installation, no Python needed.

**Why an .exe and not Python files like the original?** This version is a
C++/Qt port with closely aligned functionality, so the tool runs without a
Python installation and without PyInstaller-related antivirus false positives.
The source code is included so anyone can see for themselves that there's no
malware inside — not for free reuse. The license below still applies
(private, non-commercial use).

---

## TROUBLESHOOTING (common pitfalls)

**"VCRUNTIME140.dll is missing" or similar DLL errors on startup.**
Microsoft Visual C++ Redistributable is missing. See installation step 2.

**Antivirus false positive.**
Some scanners flag unknown/new .exe files as suspicious (false positive,
nothing is encrypted). Solution: add the tool folder as an exception, or
enable "AV mode" in the options.

**"no EverDrive found" when starting the monitor.**
The tool searches the COM port automatically — if none is found at all, the
EverDrive's USB driver is usually missing, or the console/cable isn't
connected. The console doesn't need to be on to START the tool, but it does
for the MONITOR. If the cable is pulled during monitoring, the tool shows the
connection as lost.

**Graphics glitches / flicker in a particular game.**
In rare cases the inserted VBlank stub is too large for an especially
timing-critical game and causes graphics glitches (e.g. flickering or
duplicated parts of the picture). Solution: re-patch that game individually
and tick the **"Light stub"** checkbox before patching. The light stub spreads
the memory read-out across multiple frames in rotation and significantly
lowers the per-frame load. Note: with the light stub there is no golden TV
flash on unlock; achievements and game detection keep working normally.

---

## Requirements

- Mega EverDrive **CORE** or **PRO** (with USB) from krikzz.com.
  (Developed and tested with the CORE. The PRO uses the same USB interface and
  should therefore work as well, but has not been tested yet. Other EverDrive
  models without a USB port are not supported.)
- Sega Mega Drive / Genesis (real hardware)
- Windows 10/11 (64-bit)
- A RetroAchievements account (retroachievements.org)

## Quick start (usage)

1. Start the tool, log in with your RA account
2. Choose a ROM -> CREATE IPS PATCH
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

© 2026 Liqui. Use and distribution permitted for private, non-commercial
purposes. No warranty. Not an official RetroAchievements product.
