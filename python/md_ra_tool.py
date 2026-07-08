#!/usr/bin/env python3
"""
MEGA-RAW — RetroAchievements on Real Mega Drive Hardware
Vollautomatisch: Login → Spiel erkennen → RA-Conditions laden →
RAM-Adressen extrahieren → IPS patchen → Monitor → Achievements awarden

------------------------------------------------------------------
(c) 2026 Liqui. Alle Rechte vorbehalten.
Dieses Projekt ist ein privates, nicht-kommerzielles Hobby-Projekt.
Weitergabe und Nutzung nur unter Nennung des Urhebers (Liqui).
Keine kommerzielle Nutzung, keine Veroeffentlichung unter fremdem Namen.
Kein offizielles RetroAchievements-Produkt.
------------------------------------------------------------------
"""

import tkinter as tk
from tkinter import filedialog, messagebox
import hashlib, struct, os, sys, threading, time, json, subprocess, tempfile
import urllib.request, urllib.parse, urllib.error
from capstone import *
import ra_condition as racond
try:
    from ed_serial import EdSerial, find_everdrive
except ImportError:
    EdSerial = None

# =============================================
# KONFIGURATION
# =============================================
# Pfade relativ zum Tool-Ordner -> laeuft auf jedem Rechner, egal wo die
# Dateien liegen. edlink.exe muss im selben Ordner wie dieses Tool liegen.
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EDLINK   = os.path.join(SCRIPT_DIR, "edlink.exe")
PORT     = "COM5"
BRAM_OUT = os.path.join(SCRIPT_DIR, "bram_out.bin")
RA_HOST  = "retroachievements.org"
RA_REQUEST_PAUSE = 0.4  # Pflicht-Pause zwischen RA-Abrufen (Server-Schonung, fix)

# ===== Sprache / Language =====
LANG = "de"   # wird beim Start aus settings.json gesetzt

# Nur DE->EN. Fehlt ein Schluessel, wird der deutsche Text unveraendert gezeigt
# (failsafe: nie ein leerer Button).
_TR = {
    # Buttons / Panels
    "DURCHSUCHEN": "BROWSE",
    "ORDNER PATCHEN": "PATCH FOLDER",
    "SD-EXPORT": "SD EXPORT",
    "IPS PATCH ERSTELLEN": "CREATE IPS PATCH",
    "MONITOR STARTEN": "START MONITOR",
    "MONITOR STOPPEN": "STOP MONITOR",
    "TV-TEST": "TV TEST",
    "EINLOGGEN": "LOG IN",
    "⚙ OPTIONEN": "⚙ OPTIONS",
    "OPTIONEN": "OPTIONS",
    "SPEICHERN": "SAVE",
    "START": "START",
    "SCHLIESSEN": "CLOSE",
    "LAEUFT...": "RUNNING...",
    # Options-Menue
    "Hardcore-Modus (vorerst gesperrt — nur Softcore)": "Hardcore mode (locked for now — Softcore only)",
    "Vorerst gesperrt: Eine genauere Ueberpruefung der Hardcore-Anforderungen steht noch aus. Bis dahin bucht der Client nur im Softcore-Modus.":
        "Locked for now: a closer review of the hardcore requirements is still pending. Until then the client unlocks in Softcore mode only.",
    "AV-Modus (kann Ransomware-Fehlalarme verhindern)": "AV mode (can prevent ransomware false positives)",
    "Schreibt Patches mit kurzer Pause — verhindert moeglicherweise Ransomware-Fehlalarme des Virenscanners beim Stapel-Patchen (nicht abschliessend getestet).":
        "Writes patches with a short pause — may prevent ransomware false positives from the virus scanner during batch patching (not conclusively tested).",
    "TV-Goldblitz bei Freischaltung": "TV gold flash on unlock",
    "SD-Export Buchstaben-Gruppen:": "SD export letter groups:",
    "Sprache / Language": "Sprache / Language",
    # SD-Export-Fenster
    "IPS gleich miterstellen": "Create IPS at the same time",
    "In Buchstaben-Ordner sortieren (EverDrive-Dateilimit)":
        "Sort into letter folders (EverDrive file limit)",
    "Quell-Archiv (RAR/ZIP) nach Entpacken loeschen":
        "Delete source archive (RAR/ZIP) after extraction",
    "Granularitaet:": "Granularity:",
    "Schnellstart:  1. Einloggen   2. Spiel einmalig patchen   3. Spiel starten   4. Monitor starten":
        "Quick start:  1. Log in   2. Patch the game once   3. Start the game   4. Start monitor",
    "Verbinde...": "Connecting...",
    "Support\n\naber falls du darueber nachdenkst,\nlies bitte 'about the cat'": "Support\n\nbut if you're thinking about it,\nplease read 'about the cat'",
    # Statuszeilen / Meldungen (haeufig)
    "Bitte zuerst einloggen.": "Please log in first.",
    "Hinweis": "Notice",
    "Fehler": "Error",
    "Fertig": "Done",
    "Erst MONITOR STARTEN (Verbindung noetig) —\nder Blitz braucht zudem eine Gen-44-IPS auf der Konsole.":
        "START MONITOR first (connection required) —\nthe flash also needs a Gen-44 IPS on the console.",
    "TV-Blitz gesendet — Bildschirm sollte golden aufleuchten":
        "TV flash sent — screen should light up golden",
    "RAR braucht das Modul 'rarfile':  pip install rarfile  (+ WinRAR installiert)":
        "RAR needs the 'rarfile' module:  pip install rarfile  (+ WinRAR installed)",
    "Keine MD-ROM (.md/.bin/.gen/.smd) im Archiv gefunden":
        "No MD ROM (.md/.bin/.gen/.smd) found in the archive",
    "Spiel-Daten werden nachgeladen — bitte gleich erneut klicken...":
        "Loading game data — please click again in a moment...",
    "Kein Platz fuer Stub (ROM > 4MB?).": "No space for stub (ROM > 4MB?).",
    "IPS erstellt:\n{}\n\nAuf SD-Karte neben die ROM.":
        "IPS created:\n{}\n\nPut it next to the ROM on the SD card.",
    "FEHLER — Details in error_log.txt": "ERROR — details in error_log.txt",
    "Bitte zuerst einloggen (fuer optionales IPS-Erstellen).":
        "Please log in first (for optional IPS creation).",
    "Nicht eingeloggt": "Not logged in",
    "Keine ROM": "No ROM",
    "LOGIN": "LOG IN",
    "Zuerst ein ROM laden (DURCHSUCHEN).": "Load a ROM first (BROWSE).",
    "Bitte zuerst einloggen, dann ROM laden.": "Please log in first, then load a ROM.",
    "Spiel erkannt, aber nicht eingeloggt": "Game detected, but not logged in",
    "auto-erkannt — Achievements laufen automatisch (Zusatz-Infos erscheinen, sobald das Spiel einmal gepatcht wurde)":
        "auto-detected — achievements run automatically (extra info appears once the game has been patched)",
    # Tooltips
    "EIN Spiel waehlen (ROM/ZIP/RAR), pruefen und einzeln patchen.":
        "Select ONE game (ROM/ZIP/RAR), check and patch it individually.",
    "Ganzen ORDNER patchen: jede ROM identifizieren und IPS daneben erstellen. Schon gepatchte werden uebersprungen. Schreibt batch_log.txt + verify_report.txt.":
        "Patch a whole FOLDER: identify each ROM and create the IPS next to it. Already-patched ones are skipped. Writes batch_log.txt + verify_report.txt.",
    "Spiele von Quelle auf die SD KOPIEREN — optional gleich patchen und in Buchstaben-Ordner (A-E ...) sortieren gegen das EverDrive-Dateilimit.":
        "COPY games from source to the SD — optionally patch them right away and sort into letter folders (A-E ...) against the EverDrive file limit.",
    "Erstellt die IPS-Patchdatei fuer das aktuell geladene Spiel (neben die ROM). Auf SD-Karte zur ROM legen.":
        "Creates the IPS patch file for the currently loaded game (next to the ROM). Put it on the SD card beside the ROM.",
    "Startet die Live-Ueberwachung: liest per USB den Spielstand und schaltet Achievements frei. Spiel auf der Konsole starten.":
        "Starts live monitoring: reads game state via USB and unlocks achievements. Start the game on the console.",
    "Testet den goldenen Bildschirm-Blitz, der bei einem freigeschalteten Achievement auf dem Fernseher erscheint.":
        "Tests the golden screen flash shown on the TV when an achievement unlocks.",
    "Erstellt fuer jede kopierte ROM gleich die IPS-Patchdatei im Ziel. Ohne Haken werden ROMs nur kopiert, nicht gepatcht.":
        "Creates the IPS patch for every copied ROM in the target. Without the check, ROMs are only copied, not patched.",
    "Legt im Ziel Ordner wie A-E, F-J an und sortiert die Spiele hinein. Verhindert, dass das EverDrive-Menue bei zu vielen Dateien pro Ordner Spiele nicht mehr anzeigt.":
        "Creates folders like A-E, F-J in the target and sorts games into them. Prevents the EverDrive menu from hiding games when a folder has too many files.",
    "ACHTUNG: Loescht das originale RAR/ZIP, nachdem die ROM erfolgreich ins Ziel kopiert wurde. Nur einschalten, wenn du die Archive nicht mehr brauchst. Standard: aus.":
        "WARNING: Deletes the original RAR/ZIP after the ROM was successfully copied to the target. Only enable if you no longer need the archives. Default: off.",
    "A-E: 5er-Gruppen (A-E, F-J, ...) — wenige Ordner, ~100 Spiele je Ordner. A-Z: ein Ordner je Buchstabe — mehr Ordner, weniger Spiele je Ordner. Bei sehr grossen Sammlungen A-Z waehlen.":
        "A-E: groups of 5 (A-E, F-J, ...) — few folders, ~100 games each. A-Z: one folder per letter — more folders, fewer games each. Choose A-Z for very large collections.",
}

def T(s):
    if LANG == "de":
        return s
    return _TR.get(s, s)   # failsafe: unuebersetzt -> deutscher Text


# --- Light-Stub-Meldungen (einmal definiert -> Code + Uebersetzung identisch) ---
# Gemeinsamer technischer Hinweis (Massen-Patch, SD-Export, Einzel-Patch).
MSG_STUB_HINT = (
    "Hinweis: In seltenen Faellen ist der eingefuegte VBlank-Stub fuer ein "
    "besonders zeitkritisches Spiel zu gross und verursacht Grafikfehler "
    "(Flackern). Solche Spiele muessen einzeln mit dem LIGHT-STUB erneut "
    "gepatcht werden. Der Light-Stub verteilt die RAM-Spiegelung rotierend "
    "ueber mehrere Frames und senkt die VBlank-Last deutlich "
    "(kein TV-Goldblitz).")
MSG_LIGHT_LABEL = "Light-Stub (nur bei Grafikfehlern noetig)"
MSG_LIGHT_TIP = (
    "Light-Stub: verteilt die RAM-Spiegelung rotierend ueber mehrere Frames "
    "-> deutlich weniger VBlank-Last, behebt Flackern bei zeitkritischen "
    "Spielen. Kein TV-Goldblitz; Werte aktualisieren rotierend (fuer "
    "Achievements irrelevant). Nur aktivieren, wenn ein Spiel mit dem "
    "normalen Patch Grafikfehler zeigt.")
MSG_LIGHT_ACTIVE = "\n\nLIGHT-Stub aktiv: kein TV-Goldblitz."
_TR.update({
    MSG_STUB_HINT: (
        "Note: In rare cases the inserted VBlank stub is too large for an "
        "especially timing-critical game and causes graphics glitches "
        "(flicker). Such games must be re-patched individually with the "
        "LIGHT STUB. The light stub spreads the RAM mirroring across "
        "multiple frames in rotation and significantly lowers the VBlank "
        "load (no TV gold flash)."),
    MSG_LIGHT_LABEL: "Light stub (only needed on graphics glitches)",
    MSG_LIGHT_TIP: (
        "Light stub: spreads the RAM mirroring across multiple frames in "
        "rotation -> much less VBlank load, fixes flicker on timing-critical "
        "games. No TV gold flash; values update in rotation (irrelevant for "
        "achievements). Only enable if a game shows graphics glitches with "
        "the normal patch."),
    MSG_LIGHT_ACTIVE: "\n\nLIGHT stub active: no TV gold flash.",
    "ABBRECHEN": "CANCEL",
    "BRECHE AB...": "CANCELLING...",
})


VERSION = "v5.84 (202. Version)"  # Ko-fi-Link/Text durch eigene Support-Seite ersetzt
VERSION_SUFFIX_DE = " und immer noch nicht perfekt"
VERSION_SUFFIX_EN = " and still not perfect"
def version_str():
    """Versionsnummer + uebersetzter buggy-Zusatz, je nach Sprache."""
    return VERSION + (VERSION_SUFFIX_DE if LANG == "de" else VERSION_SUFFIX_EN)
STUB_GEN = 0x48  # Kennung die der Stub ins BRAM[197] schreibt (0x43 = Stub-Gen 4.3)
# Alternativ-Stub (Light-Patch): Adressen pro Frame (Rest rotiert ueber Folge-Frames).
STUB_SLIM_CHUNK = 1
def _p(name):
    """Pfad neben dem Tool, unabhaengig vom Startverzeichnis."""
    return os.path.join(SCRIPT_DIR, name)
def _asset(name):
    """Sucht eine Asset-Datei zuerst im Unterordner assets/, dann im
    Hauptordner (Fallback). So bleibt der Hauptordner aufgeraeumt, das Tool
    findet die Datei aber auch, wenn sie noch daneben liegt."""
    in_assets = os.path.join(SCRIPT_DIR, "assets", name)
    if os.path.isfile(in_assets):
        return in_assets
    return os.path.join(SCRIPT_DIR, name)
USER_AGENT = "MegaDriveRA/2.0"

# BRAM kann bis 128KB — wir nutzen die ersten 128 Bytes für Werte
BRAM_LEN = 208  # gelesene Bytes pro Frame (Watch-Adressen + Kennung 197 + Game-ID 203/205)

# Hook-Overrides für bekannte Spiele (Hook/Stub-Adressen)
HOOK_DB = {
    "1bc674be034e43c96b86487ac69d9293": {"stub":0x068156, "name":"Sonic The Hedgehog"},
    "04d2c7da9aac3fbeadf6bb8ccd27b560": {"stub":0x329AA, "name":"Golden Axe"},
    "5a8f7c6437d239690b4a15287d841c26": {"stub":0x0FBF20, "name":"Shinobi III"},
}

# ROM-Serial → RA Game-ID. Für Auto-Erkennung ohne lokale ROM-Datei.
# Serial kommt direkt vom ROM (FCI-Bus 0x180). RA-ID einmalig nachschlagen + cachen.
SERIAL_DB = {
    "00001009": {"gameid":1,  "name":"Sonic The Hedgehog"},
    "00054018": {"gameid":18, "name":"Golden Axe"},
    # Weitere: Serial → RA Game-ID
}

NOP = bytes([0x4E, 0x71])

# =============================================
# RA API (dorequest.php)
# =============================================
class RateLimited(Exception):
    def __init__(self, retry_after=0):
        super().__init__("rate limited")
        self.retry_after = retry_after

_ra_cache = None
def _cache():
    global _ra_cache
    if _ra_cache is None:
        try:
            _ra_cache = json.load(open(_p("ra_cache.json"), encoding="utf-8"))
        except Exception:
            _ra_cache = {"gameid": {}, "patch": {}}
        _ra_cache.setdefault("gameid", {}); _ra_cache.setdefault("patch", {})
    return _ra_cache

_cache_dirty = 0
def _cache_save(force=False):
    """Gebuendelt schreiben statt nach jeder Anfrage — sonst triggert das
    staendige Neuschreiben den Ransomware-Schutz (G DATA BEAST/AntiRansomware)."""
    global _cache_dirty
    _cache_dirty += 1
    if not force and _cache_dirty < 25:
        return
    _cache_dirty = 0
    try:
        tmp = _p("ra_cache.json.tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(_ra_cache, f)
        os.replace(tmp, _p("ra_cache.json"))   # atomar, ein einziger Schreibvorgang
    except Exception:
        pass

def ra_request(params):
    query = urllib.parse.urlencode(params)
    url = f"https://{RA_HOST}/dorequest.php?{query}"
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=12) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        if e.code == 429:
            # Hoefliches Backoff: Retry-After respektieren, sonst kurz warten
            ra = e.headers.get("Retry-After") if e.headers else None
            raise RateLimited(int(ra) if (ra and ra.isdigit()) else 0)
        raise

def ra_login(user, pw):
    d = ra_request({"r":"login2","u":user,"p":pw})
    return d.get("Token") if d.get("Success") else None

def ra_gameid(md5, raise_limit=False):
    cache = _cache()["gameid"]
    if md5 in cache:
        return cache[md5] or None
    try:
        d = ra_request({"r":"gameid","m":md5})
        gid = d.get("GameID") or None
        cache[md5] = gid; _cache_save()
        return gid
    except RateLimited:
        if raise_limit: raise
        return None
    except: return None

def ra_patch(gameid, user, token):
    cache = _cache()["patch"]
    key = str(gameid)
    if key in cache:
        return cache[key]
    try:
        d = ra_request({"r":"patch","g":gameid,"u":user,"t":token})
        pd = d.get("PatchData")
        if pd:
            cache[key] = pd; _cache_save()
        return pd
    except RateLimited:
        raise
    except: return None

MBX_ADDR = 0x01800300
GOLD = 0x0AE   # Genesis BGR: kraeftiges Gold

def tv_flash():
    """TV-Animation beim Achievement: goldenes Aufpulsen, halten, weiches
    Ausfaden auf Schwarz (Backdrop-Farbe via MBX -> Stub, frameweise)."""
    ed = _ed_link
    if not ed: return
    up   = [0x002, 0x024, 0x048, 0x06A, 0x08C, 0x0AE]
    hold = [0x0AE] * 10
    down = [0x0AE, 0x08C, 0x06A, 0x048, 0x024, 0x002, 0x000]
    try:
        for col in up + hold + down:
            ed.memwr(MBX_ADDR, struct.pack(">H", 0xA000 | col))
            time.sleep(0.045)
        ed.memwr(MBX_ADDR, b"\x00\x00")   # idle, kein VDP-Zugriff mehr
        _tv_log("TV-Blitz: alle MBX-Schreibzugriffe OK (bleibt der Schirm schwarz: Stub/IPS prueffen)")
    except Exception as e:
        _tv_log(f"TV-Blitz FEHLER beim MBX-Schreiben: {e}")

def _tv_log(msg):
    try:
        with open(_p("error_log.txt"),"a",encoding="utf-8") as f:
            f.write(f"{time.strftime('%H:%M:%S')} {msg}\n")
    except Exception:
        pass

def ra_unlocks(gameid, user, token):
    """Bereits freigeschaltete Achievement-IDs vom Account holen.
    Tolerant gegen Antwort-Varianten (Unlocks/HardcoreUnlocks, dict/int)."""
    try:
        d = ra_request({"r":"startsession","g":gameid,"u":user,"t":token}) or {}
        ids = set()
        for k, v in d.items():
            if "unlock" in str(k).lower() and isinstance(v, list):
                for e in v:
                    if isinstance(e, dict):
                        i = e.get("ID") or e.get("AchievementID") or e.get("id")
                        if i is not None:
                            ids.add(int(i))
                    else:
                        try: ids.add(int(e))
                        except (TypeError, ValueError): pass
        try:
            with open(_p("ra_debug.txt"),"a",encoding="utf-8") as f:
                f.write(f"\nstartsession-Antwort-Keys: {list(d.keys())}\n")
                f.write(f"erkannte Unlock-IDs: {sorted(ids)}\n")
        except Exception:
            pass
        return ids
    except Exception:
        return set()

def ra_award(achid, user, token, hardcore=0):
    try:
        d = ra_request({"r":"awardachievement","a":achid,"h":hardcore,"u":user,"t":token})
        return d.get("Success", False)
    except: return False

# =============================================
# ROM / PATCH
# =============================================
def md5_file(path):
    with open(path,"rb") as f:
        return hashlib.md5(f.read()).hexdigest()



def build_game(gid, md5, user, token):
    """RA-Patch holen und ein vollstaendiges game-Dict bauen (GUI-frei).
    Liefert game-Dict oder None. Bei leerem/unsupported Set sind die
    Verifikations-Felder gesetzt: raw_core (Zahl offizieller Achievements),
    n_unsupported (davon nicht parsebar)."""
    patch = ra_patch(gid, user, token)
    if not patch:
        return None
    title = patch.get("Title", f"#{gid}")
    if "unsupported game version" in title.lower():
        return {"name": title, "gameid": gid, "md5": md5, "no_set": True,
                "addr_list": [], "raw_core": 0, "n_unsupported": 0}
    achs = []
    raw_core = 0
    n_unsupported = 0
    for ac in patch.get("Achievements", []):
        if ac.get("Flags") != 3 or ac.get("ID", 0) >= 101000000:
            continue
        raw_core += 1
        rt = racond.AchievementRuntime(ac.get("MemAddr", ""))
        if rt.unsupported:
            n_unsupported += 1
        achs.append({"id": ac["ID"], "title": ac["Title"], "desc": ac.get("Description",""),
                     "points": ac.get("Points",0), "mem": ac.get("MemAddr",""),
                     "triggered": False, "rt": rt, "unsupported": rt.unsupported})
    phys = racond.collect_addresses(achs)
    addr_map, addr_list, b = {}, [], 1
    for a in phys:
        addr_map[a] = b; b += 2; addr_list.append((a, 1))
    return {"name": title, "gameid": gid, "md5": md5, "achievements": achs,
            "addr_map": addr_map, "addr_list": addr_list,
            "raw_core": raw_core, "n_unsupported": n_unsupported}


def extract_archive_rom(arc, dest_dir=None):
    """Erste MD-ROM aus ZIP/RAR entpacken. Liefert Pfad oder None."""
    exts = (".md", ".bin", ".gen", ".smd")
    dest_dir = dest_dir or os.path.dirname(arc)
    if arc.lower().endswith(".zip"):
        import zipfile; af = zipfile.ZipFile(arc)
    elif arc.lower().endswith(".rar"):
        try:
            import rarfile
        except ImportError:
            return None
        for p in (r"C:\\Program Files\\WinRAR\\UnRAR.exe",
                  r"C:\\Program Files (x86)\\WinRAR\\UnRAR.exe"):
            if os.path.exists(p):
                rarfile.UNRAR_TOOL = p; break
        af = rarfile.RarFile(arc)
    else:
        return arc  # ist schon eine ROM
    name = next((n for n in af.namelist() if n.lower().endswith(exts)), None)
    if not name:
        return None
    out = os.path.join(dest_dir, os.path.basename(name))
    with af.open(name) as src, open(out, "wb") as dst:
        dst.write(src.read())
    return out


# EverDrive-Menue (Krikzz-Handbuch CORE/PRO, Stand 2026): bei aktiver Sortierung
# max. 800 sichtbare Eintraege je Ordner. Jedes gepatchte Spiel belegt 2 Eintraege
# (ROM + .ips), ungepatchte 1.
EVERDRIVE_FOLDER_LIMIT = 800

def _entry_count(items):
    return sum(2 if ips else 1 for _, ips in items)

def _key2(rom, n=2):
    s = os.path.splitext(os.path.basename(rom))[0].upper()
    s = "".join(c for c in s if c.isalnum())
    return (s or "0")[:n]

def _letter_of(rom):
    c = os.path.basename(rom)[:1].upper()
    return c if c.isalpha() else "0-9"   # Ziffern/Symbole -> eigener Ordner "0-9"

def _subsplit(items, limit):
    """Ein einzelner Buchstabe ist zu gross -> in Teilstuecke <= limit teilen,
    benannt nach 2-Zeichen-Praefix (z.B. SA-SM / SN-SZ). Paare bleiben zusammen."""
    chunks, cur, n = [], [], 0
    for rom, ips in items:
        e = 2 if ips else 1
        if cur and n + e > limit:
            chunks.append(cur); cur, n = [], 0
        cur.append((rom, ips)); n += e
    if cur: chunks.append(cur)
    out = []
    for ch in chunks:
        lo, hi = _key2(ch[0][0]), _key2(ch[-1][0])
        out.append((lo if lo == hi else f"{lo}-{hi}", ch))
    return out

def _letter_groups(placed, limit=EVERDRIVE_FOLDER_LIMIT):
    """Sortiert alphabetisch in 'von-bis'-Ordner, aber IMMER an Buchstabengrenzen:
    ein Buchstabe wird nie ueber zwei Ordner verstreut. Ganze Buchstaben werden
    gierig zusammengelegt, solange die Summe <= limit bleibt; nur wenn ein
    EINZELNER Buchstabe > limit ist, wird genau dieser feiner geteilt. ROM/.ips
    zaehlen zusammen (2 Eintraege) und bleiben stets im selben Ordner.
    Liefert Liste (ordnername, items)."""
    from collections import OrderedDict
    buckets = OrderedDict()
    for rom, ips in placed:
        buckets.setdefault(_letter_of(rom), []).append((rom, ips))
    groups = []
    cur_items, cur_letters, cur_n = [], [], 0
    def flush():
        nonlocal cur_items, cur_letters, cur_n
        if cur_items:
            name = cur_letters[0] if len(cur_letters) == 1 else f"{cur_letters[0]}-{cur_letters[-1]}"
            groups.append((name, cur_items))
            cur_items, cur_letters, cur_n = [], [], 0
    for L, items in buckets.items():
        n = _entry_count(items)
        if L == "0-9":                       # Ziffern/Symbole: eigener Ordner
            flush()
            groups.extend(_subsplit(items, limit) if n > limit else [("0-9", items)])
            continue
        if n > limit:                        # einzelner Buchstabe zu gross -> teilen
            flush(); groups.extend(_subsplit(items, limit)); continue
        if cur_items and cur_n + n > limit:  # naechster Buchstabe passt nicht mehr
            flush()
        cur_items += items; cur_letters.append(L); cur_n += n
    flush()
    # Namen eindeutig machen (Failsafe gegen Kollisionen)
    seen, final = {}, []
    for name, items in groups:
        if name in seen:
            seen[name] += 1; name = f"{name}_{seen[name]}"
        else:
            seen[name] = 0
        final.append((name, items))
    return final


def patch_rom_file(rom_path, game):
    """ROM gegen game-Dict patchen, IPS daneben schreiben. GUI-frei.
    game braucht: addr_list, md5, gameid, name. Liefert (ok:bool, msg:str)."""
    if not game.get("addr_list"):
        return False, "kein Set / keine Achievements", None
    with open(rom_path, "rb") as f:
        orig = bytearray(f.read())
    h = game.get("md5", "")
    orig_vblank = struct.unpack_from(">I", orig, 0x78)[0]
    needed = 60 + 10 * sum(n for _, n in game["addr_list"]) + 100
    rom_end = rom_end_from_header(orig)
    if h in HOOK_DB and HOOK_DB[h].get("stub"):
        stub_addr, outside = HOOK_DB[h]["stub"], False
    else:
        stub_addr, outside = find_stub_addr(orig, needed)
    if not stub_addr:
        return False, "kein Platz fuer Stub", None
    stub = build_stub(stub_addr, orig_vblank, game["addr_list"], game.get("gameid", 0))
    patches = [(0x78, struct.pack(">I", stub_addr)), (stub_addr, stub)]
    did_check = False
    if not outside:
        stored = struct.unpack_from(">H", orig, 0x18E)[0]
        if stored == calc_checksum(orig, rom_end):
            patched = bytearray(max(len(orig), stub_addr + len(stub)))
            patched[:len(orig)] = orig
            patched[stub_addr:stub_addr+len(stub)] = stub
            struct.pack_into(">I", patched, 0x78, stub_addr)
            patches.insert(0, (0x018E, struct.pack(">H", calc_checksum(patched, rom_end))))
            did_check = True
    ips = make_ips(patches)
    ips_path = os.path.splitext(rom_path)[0] + ".ips"
    with open(ips_path, "wb") as f:
        f.write(ips)
    try:
        gc = {}
        if os.path.exists(_p("games_cache.json")):
            gc = json.load(open(_p("games_cache.json"), encoding="utf-8"))
        gc[str(game["gameid"])] = {"name": game["name"], "rom": rom_path,
            "md5": h, "compat": f"VBlank 0x{orig_vblank:06X} | Stub 0x{stub_addr:06X}"
            f"{' (angehaengt)' if outside else ' (in ROM)'}"}
        json.dump(gc, open(_p("games_cache.json"), "w", encoding="utf-8"), indent=1)
    except Exception:
        pass
    risk, reasons = boot_risk(orig, stub_addr, outside, did_check)
    return True, f"IPS ok (Stub {len(stub)}B @0x{stub_addr:06X})", (risk, reasons)



def boot_risk(rom_data, stub_addr, outside, did_checksum):
    """Statische Boot-Risiko-Einschaetzung einer gepatchten ROM.
    Liefert (stufe, gruende): stufe in {"ok","pruefen","hoch"}.
    Basiert auf den ueber alle Sessions gefundenen Crash-Klassen.
    Ersetzt KEINEN echten Konsolen-Test, grenzt ihn aber ein."""
    import re as _re
    gruende = []
    stufe = "ok"

    # 1) Eigene Checksum-Routine? (Referenz auf $18E im Code, Headdy-Klasse)
    #    Gefaehrlich nur, wenn wir die Checksum NICHT korrigiert haben und der
    #    Stub im geprueften Bereich liegt.
    has_own_check = b"\x00\x00\x01\x8E" in bytes(rom_data)
    stored = struct.unpack_from(">H", rom_data, 0x18E)[0] if len(rom_data) > 0x190 else 0
    std_sum = calc_checksum(rom_data, rom_end_from_header(rom_data))
    nonstandard_sum = (stored != std_sum)
    if has_own_check and nonstandard_sum and not outside:
        gruende.append("Eigene Checksum-Routine erkannt + Stub im ROM-Bereich "
                       "(Headdy-Klasse) — Boot unsicher")
        stufe = "hoch"
    elif has_own_check and nonstandard_sum:
        gruende.append("Eigene Checksum-Routine, Stub aber ausserhalb (angehaengt) "
                       "— wahrscheinlich ok")

    # 2) Banking + Stub nahe/ueber 2MB-Grenze
    banking = _uses_banking(rom_data)
    if banking and stub_addr is not None and stub_addr >= 0x1F0000:
        # Empirisch entwarnt: Dick Vitale (0x1FF340) und Wolverine (0x1FB8E0)
        # booten beide trotz Stub nahe der 2MB-Grenze. Daher nur Hinweis,
        # nicht mehr "hoch".
        gruende.append(f"Banking-Spiel, Stub bei 0x{stub_addr:06X} (nahe 2MB) "
                       "— in Tests unkritisch, zur Sicherheit pruefen")
        if stufe == "ok": stufe = "pruefen"

    # 3) VBlank-Vektor in ungewoehnlicher Region (sehr frueh/sehr hoch)
    vb = struct.unpack_from(">I", rom_data, 0x78)[0] if len(rom_data) > 0x7C else 0
    if vb < 0x200 or vb > len(rom_data):
        gruende.append(f"VBlank-Vektor 0x{vb:06X} ausserhalb des Codebereichs "
                       "— Hook-Annahme unsicher")
        if stufe == "ok": stufe = "pruefen"

    # 4) Checksum korrigiert, aber Spiel traegt eigene Routine -> Konflikt-Risiko
    if did_checksum and has_own_check and nonstandard_sum:
        gruende.append("Checksum wurde korrigiert, obwohl Spiel eigene Pruefung "
                       "nutzt — moeglicher Konflikt")
        if stufe == "ok": stufe = "pruefen"

    if not gruende:
        gruende.append("Keine bekannten Crash-Indikatoren — Stub angehaengt/sicher platziert")
    return stufe, gruende

def find_hook(data):
    vblank = struct.unpack_from(">I", data, 0x78)[0]
    cs = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
    insns = list(cs.disasm(bytes(data[vblank:vblank+256]), vblank))
    found = False
    for i in insns:
        if i.mnemonic.startswith('movem') and not found:
            found = True; continue
        if found and len(i.bytes) >= 6:
            return i.address, bytes(i.bytes)
    for i in insns:
        if len(i.bytes) >= 6:
            return i.address, bytes(i.bytes)
    return None, None

def rom_end_from_header(data):
    """Offizielles ROM-Ende laut Header (0x1A4). Dahinter: sicher fuer Stubs."""
    try:
        end = struct.unpack_from(">I", data, 0x1A4)[0]
    except Exception:
        end = 0
    if end == 0 or end >= len(data):
        end = len(data) - 1
    return end

def _uses_banking(data):
    """Nutzt das Spiel die SSF-Bank-Register (0xA130F3..0xA130FF)? Nur dann
    ist das 2MB-Banking-Fenster aktiv und ein Stub darueber gefaehrlich.
    Lineare ROMs (auch >2MB, z.B. Gargoyles 3MB) sind sicher anzuhaengen."""
    import re as _re
    return _re.search(rb"\x00\xA1\x30[\xF3-\xFF]", bytes(data)) is not None

def find_stub_addr(data, needed):
    """Universelle Stub-Platzierung (KRIKzz-Prinzip):
    1) hinter dem Dateiende anhaengen (IPS erweitert die Datei)
    2) Padding/Luecke im ROM (Checksum-Patch noetig)
    Rueckgabe: (stub_addr, outside_checksum)."""
    rom_end = rom_end_from_header(data)
    banking = _uses_banking(data)
    # Obergrenze: bei Banking-Spielen strikt unter 2MB (Banking-Fenster blendet
    # dort BRAM ein — Lehre aus Dynamite Headdy). Lineare ROMs duerfen bis 4MB
    # anhaengen (Lehre aus Gargoyles 3MB: kein Banking, voller linearer Raum).
    ceiling = 0x200000 if banking else 0x400000
    # 1) Standard: hinter dem Dateiende anhaengen (ausserhalb jeder Checksum) —
    # ausser die ROM ist bereits exakt eine Zweierpotenz (EverDrive laedt dann
    # nur diese Groesse, ein Stub dahinter waere unsichtbar -> Black Screen).
    is_pow2 = len(data) > 0 and (len(data) & (len(data)-1)) == 0
    if not is_pow2:
        addr = (len(data) + 0xF) & ~0xF
        if addr + needed <= ceiling:
            return addr, True
    # 2) Padding am Dateiende (Checksum-Patch noetig)
    j = len(data)
    while j > 0 and data[j-1] in (0x00, 0xFF):
        j -= 1
    pad_start = (j + 0xF) & ~0xF
    if len(data) - pad_start >= needed and pad_start + needed <= ceiling:
        return pad_start, (pad_start > rom_end)
    # 3) Groesste 0x00/0xFF-Luecke im ROM-Inneren (Bank-Padding) nutzen
    import re as _re
    best = None
    for m in _re.finditer(rb"\xFF{512,}|\x00{512,}", bytes(data)):
        if best is None or (m.end()-m.start()) > (best.end()-best.start()):
            best = m
    if best:
        a = (best.start() + 0x1F) & ~0xF          # Abstand zum Datenrand + gerade
        if best.end() - a >= needed + 16 and a + needed <= ceiling:
            return a, False                        # im Pruefsummenbereich
    return None, False

def stub_failure_reason(data, needed):
    """Praezise Begruendung, warum kein Stub-Platz gefunden wurde."""
    if len(data) >= 0x400000:
        return ("4MB-Vollausbau (Maximalgroesse des Mega Drive) ohne freien Platz "
                "und ohne Banking — Adressraum erschoepft, technische Grenze")
    if _uses_banking(data):
        return "Banking-Spiel, kein Platz unter 2MB"
    return "kein ausreichender freier Bereich in der ROM"

def build_stub(stub_addr, orig_vblank, addr_list, gameid=0):
    """Vektor-Methode, Lesezeitpunkt am VBlank-ENDE:
    Teil 1 legt eine kuenstliche Ruecksprungadresse auf den Stack und springt
    in den ORIGINALEN VBlank-Handler. Dessen rte kehrt zu Teil 2 zurueck,
    der die RAM-Werte liest (jetzt NACH der Handler-Logik — Emulator-aequivalent),
    ins BRAM schreibt und selbst rte macht. Kein Patch am Handler-Code.
    addr_list: Liste von (ram_addr, nbytes)."""
    part2 = stub_addr + 14
    stub  = bytes([0x2F,0x3C]) + struct.pack(">I", part2)      # move.l #part2,-(sp)
    stub += bytes([0x40,0xE7])                                  # move sr,-(sp)
    stub += bytes([0x4E,0xF9]) + struct.pack(">I", orig_vblank) # jmp Original-Handler
    # ---- Teil 2 (Ruecksprungziel des Original-rte) ----
    stub += bytes([0x48,0xE7,0xE0,0xC0,                         # movem.l d0-d1/a0,-(a7)
                   0x13,0xFC,0x00,0x01,0x00,0xA1,0x30,0x01])    # SRAM unlock
    stub += NOP*2
    stub += bytes([0x41,0xF9]) + struct.pack(">I", 0x200001)    # lea ($200001),a0
    off = 0
    for ram_addr, nbytes in addr_list:
        for b in range(nbytes):
            byte_addr = ram_addr + b
            stub += bytes([0x10,0x39]) + struct.pack(">I", byte_addr)  # move.b (addr),d0
            if off == 0:
                stub += bytes([0x10,0x80])
            else:
                stub += bytes([0x11,0x40]) + struct.pack(">H", off)
            off += 2
    # TV-Notification: MBX ($A130DE) lesen. Traegt es das Magic 0xA in den
    # oberen 4 Bits, wird der 9-bit-Farbwert als CRAM-Farbe 0 (Backdrop)
    # geschrieben. Im Idle (kein Magic) wird das VDP nicht beruehrt.
    stub += bytes([0x30,0x39]) + struct.pack(">I", 0xA130DE)   # move.w (MBX).l,d0
    # Gen 0x45: Vollpaletten-Blitz mit Sichern/Wiederherstellen aller 64
    # CRAM-Farben (Puffer: BRAM $200101.., Flag $2000C9). Annahme: VDP-
    # Auto-Increment = 2 (Standard in praktisch allen Spielen).
    stub += bytes([0x32,0x00])                                  # move.w d0,d1
    stub += bytes([0x02,0x41,0xF0,0x00])                        # andi.w #$F000,d1
    stub += bytes([0x0C,0x41,0xA0,0x00])                        # cmpi.w #$A000,d1
    stub += bytes([0x66,0x14])                                  # bne.s +20 (skip)
    stub += bytes([0x02,0x40,0x0E,0xEE])                        # andi.w #$0EEE,d0
    stub += bytes([0x23,0xFC,0xC0,0x00,0x00,0x00,0x00,0xC0,0x00,0x04])  # CRAM addr 0
    stub += bytes([0x33,0xC0,0x00,0xC0,0x00,0x00])              # move.w d0,(VDP data)
    # Stub-Generations-Kennung nach PC-BRAM[197] (sichtbar im Tool)
    stub += bytes([0x13,0xFC,0x00,STUB_GEN]) + struct.pack(">I", 0x2000C5)
    if gameid > 0xFFFF: gameid = 0   # RA-Platzhalter-IDs nicht schreiben
    stub += bytes([0x13,0xFC,0x00,gameid & 0xFF]) + struct.pack(">I", 0x2000CB)        # Game-ID lo
    stub += bytes([0x13,0xFC,0x00,(gameid >> 8) & 0xFF]) + struct.pack(">I", 0x2000CD) # Game-ID hi
    # Frame-Handshake: 0x00 nach PC-BRAM[199] (PC setzt 0xAA, wartet auf 0)
    stub += bytes([0x13,0xFC,0x00,0x00]) + struct.pack(">I", 0x2000C7)
    stub += bytes([0x4C,0xDF,0x03,0x07])                        # movem.l (a7)+,d0-d2/a0-a1
    stub += bytes([0x4E,0x73])                                  # rte
    return stub

def build_stub_slim(stub_addr, orig_vblank, addr_list, gameid=0):
    """ALTERNATIV-STUB (Light-Patch) fuer Spiele mit extrem engem VBlank
    (Grafikfehler/Flackern beim vollen Stub, z.B. Treasure-Titel). Spiegelt
    pro Frame nur STUB_SLIM_CHUNK Adressen, rotierend ueber alle Frames,
    jeweils in den FESTEN BRAM-Slot (PC liest exakt wie beim vollen Stub).
    Minimaler Overhead -> deutlich weniger VBlank-Zeit. Kein MBX/CRAM ->
    KEIN TV-Goldblitz. gameid bleibt erhalten (Auto-Erkennung). SRAM jeden
    Frame (SRAM-Spiele kompatibel). Lesezeitpunkt VBlank-ENDE via Trampolin.
    Werte aktualisieren rotierend (paar Frames Verzoegerung, fuer
    Achievement-Polling irrelevant)."""
    if gameid > 0xFFFF:
        gameid = 0
    CHUNK = max(1, STUB_SLIM_CHUNK)
    slots = []
    for ram_addr, nbytes in addr_list:
        for b in range(nbytes):
            slots.append(ram_addr + b)
    if not slots:
        slots = [0xFF0000]
    TOTAL = len(slots)
    G = (TOTAL + CHUNK - 1) // CHUNK
    # Marker ueber die Rotations-Bloecke verteilen (kein Frame mit Lastspitze)
    markers = [(0x2000C5, STUB_GEN), (0x2000C7, 0x00),
               (0x2000CB, gameid & 0xFF), (0x2000CD, (gameid >> 8) & 0xFF)]
    block_markers = {g: [] for g in range(G)}
    for i, m in enumerate(markers):
        block_markers[i % G].append(m)
    part2 = stub_addr + 14
    stub  = bytes([0x2F,0x3C]) + struct.pack(">I", part2)       # move.l #part2,-(sp)
    stub += bytes([0x40,0xE7])                                  # move sr,-(sp)
    stub += bytes([0x4E,0xF9]) + struct.pack(">I", orig_vblank) # jmp Original-Handler
    # --- Dispatch: Frame-Zaehler (BRAM $200203), Wrap per Bitmaske ---
    G2 = 1 << ((G - 1).bit_length()) if G > 1 else 1           # naechste 2er-Potenz >= G
    disp  = bytes([0x48,0xE7,0x80,0xC0])                        # movem.l d0/a0-a1,-(a7)
    disp += bytes([0x13,0xFC,0x00,0x01,0x00,0xA1,0x30,0x01])    # SRAM unlock (jeden Frame)
    disp += bytes([0x41,0xF9]) + struct.pack(">I", 0x200001)    # lea ($200001),a0
    disp += bytes([0x10,0x39]) + struct.pack(">I", 0x200203)    # move.b ($200203),d0
    disp += bytes([0x52,0x00])                                  # addq.b #1,d0
    disp += bytes([0x02,0x40]) + struct.pack(">H", G2 - 1)      # andi.w #(G2-1),d0 (Wrap + saeubert d0)
    disp += bytes([0x13,0xC0]) + struct.pack(">I", 0x200203)    # move.b d0,($200203)
    disp += bytes([0xE5,0x48])                                  # lsl.w #2,d0
    jt_lea_pos = len(disp) + 2
    disp += bytes([0x43,0xF9]) + struct.pack(">I", 0)           # lea JUMPTAB,a1 (Patch)
    disp += bytes([0x22,0x71,0x00,0x00])                        # movea.l (a1,d0.w),a1
    disp += bytes([0x4E,0xD1])                                  # jmp (a1)
    # --- Rotations-Bloecke: je CHUNK Spiegelungen + ggf. ein Marker ---
    base_blocks = part2 + len(disp)
    block_addrs = []
    blocks = bytearray()
    jmp_exit_pos = []
    for g in range(G):
        block_addrs.append(base_blocks + len(blocks))
        for k in range(g*CHUNK, min((g+1)*CHUNK, TOTAL)):
            blocks += bytes([0x10,0x39]) + struct.pack(">I", slots[k])  # move.b (src),d0
            blocks += bytes([0x11,0x40]) + struct.pack(">H", 2*k)       # move.b d0,(2k,a0)
        for (maddr, mval) in block_markers[g]:
            blocks += bytes([0x13,0xFC,0x00, mval & 0xFF]) + struct.pack(">I", maddr)
        jmp_exit_pos.append(len(blocks) + 2)
        blocks += bytes([0x4E,0xF9]) + struct.pack(">I", 0)             # jmp EXIT (Patch)
    EXIT = base_blocks + len(blocks)
    for pos in jmp_exit_pos:
        blocks[pos:pos+4] = struct.pack(">I", EXIT)
    exit_code  = bytes([0x4C,0xDF,0x03,0x01])                  # movem.l (a7)+,d0/a0-a1
    exit_code += bytes([0x4E,0x73])                            # rte
    JUMPTAB = EXIT + len(exit_code)
    disp = bytearray(disp)
    disp[jt_lea_pos:jt_lea_pos+4] = struct.pack(">I", JUMPTAB)
    # Sprungtabelle auf G2 (2er-Potenz): Phantom-Eintraege G..G2-1 -> EXIT (No-op-Frame)
    jt = b"".join(struct.pack(">I", a) for a in (block_addrs + [EXIT] * (G2 - G)))
    return stub + bytes(disp) + bytes(blocks) + exit_code + jt

def check_compat(rom_path, achievements=None):
    """Statische Kompatibilitaetspruefung. Liefert (ok, zeilen)."""
    with open(rom_path, "rb") as f:
        rom = f.read()
    lines, ok = [], True

    # 1) Eigenes SRAM? (Header 0x1B0 = "RA") -> Kollision mit unserem BRAM
    if rom[0x1B0:0x1B2] == b"RA":
        ok = False
        lines.append("✗ Spiel nutzt eigenes Batterie-SRAM — Speicherstaende "
                     "wuerden ueberschrieben. NICHT KOMPATIBEL.")
    else:
        lines.append("✓ Kein eigenes SRAM (BRAM frei nutzbar)")

    # 2) VBlank-Vektor + Handler erreicht ein rte
    try:
        vec = struct.unpack_from(">I", rom, 0x78)[0]
        if vec == 0 or vec % 2:
            ok = False
            lines.append(f"✗ VBlank-Vektor ungueltig (0x{vec:06X})")
        else:
            region = rom[vec:vec+0x400] if vec < len(rom) else b""
            has_rte = b"\x4e\x73" in region
            if vec >= len(rom):
                lines.append(f"~ VBlank-Vektor zeigt ins RAM (0x{vec:06X}) — "
                             "Trampolin, sollte funktionieren")
            elif has_rte:
                lines.append(f"✓ VBlank-Handler 0x{vec:06X} endet mit rte")
            else:
                lines.append(f"~ Kein rte nahe Handler 0x{vec:06X} — untypisch, "
                             "Test noetig")
    except Exception:
        ok = False
        lines.append("✗ Header nicht lesbar")

    # 3) Stub-Platz
    needed = 60 + 10 * 40 + 100  # grosszuegige Schaetzung
    addr, outside = find_stub_addr(rom, needed)
    if addr:
        where = "hinter Dateiende (angehaengt)" if addr >= len(rom) else f"Padding 0x{addr:06X}"
        lines.append(f"✓ Stub-Platz: {where}")
    else:
        ok = False
        lines.append("✗ Kein Platz fuer Stub (ROM zu gross)")

    # 4) Engine-Abdeckung
    if achievements:
        n = len(achievements)
        uns = sum(1 for a in achievements if a.get("unsupported"))
        if uns == 0:
            lines.append(f"✓ Alle {n} Achievements von der Engine unterstuetzt")
        else:
            lines.append(f"~ {n-uns}/{n} Achievements unterstuetzt "
                         f"({uns} mit nicht abbildbaren Bedingungen)")
    return ok, lines

def make_ips(patches):
    ips = b"PATCH"
    for off, d in patches:
        ips += off.to_bytes(3,'big') + len(d).to_bytes(2,'big') + d
    ips += b"EOF"
    return ips

def calc_checksum(data, rom_end=None):
    cs = 0
    end = min(len(data), (rom_end + 1) if rom_end else len(data))
    if end % 2:
        end -= 1
    for i in range(0x200, end, 2):
        cs = (cs + struct.unpack_from(">H", data, i)[0]) & 0xFFFF
    return cs

# =============================================
# BRAM
# =============================================
_ed_link = None  # persistente Direktverbindung (pyserial)

def verify_stub_live(ed, game):
    """Anti-Cheat Stufe 1: Liest den Stub-Bereich der LAUFENDEN ROM per USB
    zurueck und prueft die charakteristische Stub-Signatur. Beweist, dass die
    Konsole genau unsere gepatchte ROM faehrt (kein fremder/manipulierter Patch).
    Liefert (ok, info)."""
    try:
        gc = {}
        if os.path.exists(_p("games_cache.json")):
            gc = json.load(open(_p("games_cache.json"), encoding="utf-8"))
        info = gc.get(str(game.get("gameid", 0)), {})
        compat = info.get("compat", "")
        # Stub-Adresse aus dem Kompat-Vermerk ziehen (Format: "... Stub 0xXXXXXX ...")
        import re as _re
        m = _re.search(r"Stub 0x([0-9A-Fa-f]+)", compat)
        if not m:
            return None, "Stub-Adresse unbekannt (Spiel mit aktueller Version neu patchen)"
        stub_addr = int(m.group(1), 16)
        # Stub-Signatur aus der laufenden ROM lesen (ADDR_ROM = 0x0 im FCI-Raum)
        data = ed.memrd(stub_addr, 8)
        # Erwartet: 2F 3C (move.l #imm,-(sp)) gefolgt von der part2-Adresse,
        # dann 40 E7 (move sr,-(sp)). Das ist unsere eindeutige Stub-Kennung.
        if data[0:2] == b"\x2F\x3C" and data[6:8] == b"\x40\xE7":
            part2 = struct.unpack(">I", data[2:6])[0]
            if part2 == stub_addr + 14:
                return True, f"Stub verifiziert @0x{stub_addr:06X} (Konsole faehrt unsere ROM)"
            return False, f"Stub-Signatur teilweise, aber part2-Adresse falsch (0x{part2:06X})"
        return False, "Stub-Signatur nicht gefunden — fremde/ungepatchte ROM laeuft"
    except Exception as e:
        return None, f"Verifikation nicht moeglich ({e})"


def open_ed_link():
    """Oeffnet die schnelle Direktverbindung. None bei Fehler -> Fallback."""
    global _ed_link, _frame_sync_ok
    if EdSerial is None:
        return None
    try:
        ed, found_port = find_everdrive(PORT)
        if ed is None:
            _ed_link = None
            return None
        globals()["PORT"] = found_port
        _ed_link = ed
        # Frame-Sync-Selbsttest: aendert sich der Stub-Frame-Zaehler
        # (BRAM[199]) zweimal hintereinander, traegt die IPS den Zaehler
        # -> lueckenloses Frame-Sync moeglich. Sonst Double-Read.
        try:
            g = ed.memrd(0x01080000 + 196, 2)[1]
        except Exception:
            g = 0
        if g == STUB_GEN:
            # Kennung beweist: IPS traegt den Handshake-Stub. Auch wenn das
            # Spiel gerade in einer Ladephase steckt (Stub stumm), bleibt
            # Frame-Sync aktiv — der Lesepfad ueberbrueckt Blindphasen.
            _frame_sync_ok = True
        else:
            try:
                _frame_sync_ok = ed.wait_bram_frame() and ed.wait_bram_frame()
            except Exception:
                _frame_sync_ok = False
        return ed
    except Exception:
        _ed_link = None
        return None

def close_ed_link():
    global _ed_link
    if _ed_link:
        _ed_link.close()
        _ed_link = None

_frame_sync_ok = False  # True wenn die IPS den Frame-Zaehler traegt
_blind_gap = False       # True wenn der Stub gerade stumm war (Interrupts maskiert)

def read_bram():
    global _frame_sync_ok
    if _ed_link:
        # Primaer: Frame-Sync ueber Stub-Zaehler — wartet bis der VBlank-Stub
        # fertig geschrieben hat, liest dann konfliktfrei (bis zum naechsten
        # VBlank ist ~14 ms Ruhe). Erfasst jeden Frame genau einmal.
        if _frame_sync_ok:
            if _ed_link.wait_bram_frame():
                return _ed_link.read_bram(BRAM_LEN)
            # Stub stumm -> Spiel hat Interrupts maskiert (Ladephase).
            # Warten bis er zurueck ist; die Aenderungen aus der Blindphase
            # werden danach als Mikro-Schritte ausgewertet.
            global _blind_gap
            _blind_gap = True
            t0 = time.time()
            while time.time() - t0 < 30:
                if _ed_link.wait_bram_frame():
                    return _ed_link.read_bram(BRAM_LEN)
            return _ed_link.read_bram(BRAM_LEN)
        # Fallback: Double-Read-Verify gegen Tearing
        last = _ed_link.read_bram(BRAM_LEN)
        for _ in range(4):
            cur = _ed_link.read_bram(BRAM_LEN)
            if cur == last:
                return cur
            last = cur
        return last
    # Fallback: edlink.exe Subprocess (~270ms/Read)
    subprocess.run([EDLINK,".link","--port",PORT,
                    "memrd","--addr","0x01080000","--len",str(BRAM_LEN),"--file",BRAM_OUT],
                   capture_output=True)
    with open(BRAM_OUT,"rb") as f:
        return f.read()

def serial_from_bram(data):
    """Liest ROM-Serial aus BRAM-Offset 0x31,0x33,... (vom Stub geschrieben).
    Sicher: M68K schreibt sie, PC liest nur BRAM — kein Bus-Konflikt."""
    try:
        chars = []
        off = 0x31
        for i in range(8):
            if off < len(data):
                c = data[off]
                if 32 <= c < 127:
                    chars.append(chr(c))
            off += 2
        return "".join(chars)
    except:
        return ""

def play_unlock_sound():
    """Achievement-Sound: spielt achievement.wav aus dem Tool-Ordner,
    falls vorhanden. Sonst Beep-Melodie (angelehnt an das
    Monkey-Island-Hauptthema)."""
    def _p():
        try:
            import winsound
            wav = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "achievement.wav")
            if os.path.isfile(wav):
                winsound.PlaySound(wav, winsound.SND_FILENAME | winsound.SND_ASYNC)
                return
            melody = [(587,140),(659,140),(698,140),(784,140),(880,320),
                      (1175,200),(880,160),(698,140),(784,140),(880,420)]
            for f, d in melody:
                winsound.Beep(f, d)
        except Exception:
            pass
    threading.Thread(target=_p, daemon=True).start()

# =============================================
# GUI
# =============================================
class _Tooltip:
    def __init__(self, widget, text):
        self.widget = widget; self.text = text; self.tip = None
        widget.bind("<Enter>", self._show); widget.bind("<Leave>", self._hide)
    def _show(self, _):
        if self.tip: return
        x = self.widget.winfo_rootx() + 10
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.tip = tk.Toplevel(self.widget); self.tip.wm_overrideredirect(True)
        self.tip.wm_geometry(f"+{x}+{y}")
        tk.Label(self.tip, text=self.text, font=("Courier",8), justify="left",
                 bg="#ffffcc", fg="#000000", relief="solid", borderwidth=1,
                 wraplength=360, padx=6, pady=4).pack()
    def _hide(self, _):
        if self.tip: self.tip.destroy(); self.tip = None

def add_tip(widget, text):
    _Tooltip(widget, T(text))


def _enable_dpi_awareness():
    """Behebt unscharfe Darstellung auf hochaufloesenden Displays (Surface).
    Sagt Windows: das Programm skaliert selbst -> kein verwaschenes Hochskalieren."""
    try:
        import ctypes
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(2)   # Per-Monitor v2
        except Exception:
            ctypes.windll.user32.SetProcessDPIAware()        # aelteres Windows
    except Exception:
        pass


class App(tk.Tk):
    def __init__(self):
        _enable_dpi_awareness()
        super().__init__()
        self.title(f"MEGA-RAW {version_str()}")
        # tk-Skalierung an die echte DPI koppeln (scharfe Schrift)
        try:
            dpi = self.winfo_fpixels("1i")     # Pixel pro Zoll
            self.tk.call("tk", "scaling", dpi / 72.0)
        except Exception:
            pass
        self.geometry("740x840")
        self.minsize(700, 600)
        self.configure(bg="#0a0a0f")
        self.resizable(True, True)   # Maximieren/Groesse aendern erlauben

        self.rom_path = None
        self.game = None          # {name, gameid, achievements:[], addr_map:{ram_addr:bram_offset}}
        self.monitoring = False
        self.prev_ram = {}
        self.ra_token = None
        self.user = tk.StringVar()
        self.pw = tk.StringVar()
        # --- Einstellungen (persistent in settings.json) ---
        self.settings = {"hardcore": False, "av_safe": False, "tv_flash": True,
                         "bucket": "A-E", "language": "de"}
        try:
            self.settings.update(json.load(open(_p("settings.json"), encoding="utf-8")))
        except Exception:
            pass
        global LANG
        LANG = self.settings.get("language", "de")

        self.C = {"bg":"#0a0a0f","panel":"#12121a","gold":"#f0c040","blue":"#4080ff",
                  "green":"#40c060","red":"#e04040","gray":"#606080","fg":"#d0d0e0","cyan":"#40d0d0"}
        self._build()

    def _panel(self, title, fn, expand=False):
        f = tk.Frame(self, bg=self.C["panel"], padx=10, pady=6)
        f.pack(fill="both" if expand else "x", expand=expand, padx=15, pady=3)
        tk.Label(f, text=title, font=("Courier",7), fg=self.C["gray"], bg=self.C["panel"]).pack(anchor="w")
        fn(f); return f

    def _save_settings(self):
        try:
            json.dump(self.settings, open(_p("settings.json"), "w", encoding="utf-8"), indent=1)
        except Exception:
            pass

    def _open_options(self):
        win = tk.Toplevel(self); win.title(T("OPTIONEN")); win.configure(bg=self.C["bg"])
        win.geometry("460x560")
        tk.Label(win, text=T("OPTIONEN"), font=("Courier",11,"bold"),
                 fg=self.C["gold"], bg=self.C["bg"]).pack(pady=8)
        hc = tk.BooleanVar(value=self.settings["hardcore"])
        av = tk.BooleanVar(value=self.settings["av_safe"])
        tv = tk.BooleanVar(value=self.settings["tv_flash"])
        bucket = tk.StringVar(value=self.settings["bucket"])
        def row(parent):
            f = tk.Frame(parent, bg=self.C["bg"]); f.pack(anchor="w", padx=14, pady=3, fill="x"); return f
        def cb(var, text, tip, disabled=False):
            f = row(win)
            state = "disabled" if disabled else "normal"
            fg = self.C["gray"]
            tk.Checkbutton(f, text=T(text), variable=var, bg=self.C["bg"], fg=fg,
                           selectcolor=self.C["panel"], font=("Courier",9), state=state,
                           disabledforeground=self.C["gray"],
                           activebackground=self.C["bg"], activeforeground=self.C["fg"]).pack(side="left")
            if tip:
                tk.Label(win, text="   "+T(tip), font=("Courier",8,"italic"), fg=self.C["fg"],
                         bg=self.C["bg"], justify="left", wraplength=410).pack(anchor="w", padx=20)
        # Hardcore ist bis zur Anti-Cheat-Stufe-1 + KRIKzz-Firmware-Bestaetigung
        # bewusst gesperrt (Softcore-Zwang fuer diesen inoffiziellen Client).
        hc.set(False)
        cb(hc, "Hardcore-Modus (vorerst gesperrt — nur Softcore)",
           "Vorerst gesperrt: Eine genauere Ueberpruefung der Hardcore-"
           "Anforderungen steht noch aus. Bis dahin bucht der Client nur "
           "im Softcore-Modus.", disabled=True)
        cb(av, "AV-Modus (kann Ransomware-Fehlalarme verhindern)",
           "Schreibt Patches mit kurzer Pause — verhindert moeglicherweise "
           "Ransomware-Fehlalarme des Virenscanners beim Stapel-Patchen "
           "(nicht abschliessend getestet).")
        cb(tv, "TV-Goldblitz bei Freischaltung", "")
        # (RA-Anfrage-Pause ist bewusst NICHT konfigurierbar: Server-Schonung
        #  ist Pflicht, keine Option. Fest im Code verankert.)
        # Bucket
        f = row(win)
        tk.Label(f, text=T("SD-Export Buchstaben-Gruppen:"), bg=self.C["bg"], fg=self.C["fg"],
                 font=("Courier",9)).pack(side="left")
        for s in ("A-E","A-Z"):
            tk.Radiobutton(f, text=s, variable=bucket, value=s, bg=self.C["bg"], fg=self.C["fg"],
                           selectcolor=self.C["panel"], font=("Courier",9)).pack(side="left")
        # Sprache / Language
        lang = tk.StringVar(value=self.settings.get("language","de"))
        f2 = row(win)
        tk.Label(f2, text=T("Sprache / Language")+":", bg=self.C["bg"], fg=self.C["fg"],
                 font=("Courier",9)).pack(side="left")
        for code, lbl in (("de","Deutsch"),("en","English")):
            tk.Radiobutton(f2, text=lbl, variable=lang, value=code, bg=self.C["bg"], fg=self.C["fg"],
                           selectcolor=self.C["panel"], font=("Courier",9)).pack(side="left")
        def save():
            old_lang = self.settings.get("language","de")
            self.settings.update({"hardcore":False, "av_safe":av.get(), "tv_flash":tv.get(),
                                  "bucket":bucket.get(), "language":lang.get()})
            self._save_settings()
            global LANG; LANG = lang.get()
            if lang.get() != old_lang:
                messagebox.showinfo(T("Hinweis"),
                    "Sprache geaendert. Bitte das Tool neu starten, damit alle "
                    "Texte umgestellt werden.\n\n"
                    "Language changed. Please restart the tool so all texts update.")
            self.log.set(f"Optionen gespeichert — Modus: {'HARDCORE' if hc.get() else 'Softcore'}")
            win.destroy()
        tk.Button(win, text=T("SPEICHERN"), font=("Courier",9,"bold"), fg=self.C["cyan"],
                  bg=self.C["panel"], command=save).pack(pady=12)

        # Support-Link
        def open_support():
            import webbrowser
            try: webbrowser.open("https://liquid-wq.github.io/support/")
            except Exception: pass
        support_lbl = tk.Label(win, text=T("Support\n\naber falls du darueber nachdenkst,\nlies bitte 'about the cat'"),
                  font=("Courier",9), fg=self.C["gray"], bg=self.C["bg"],
                  cursor="hand2", wraplength=400, justify="center")
        support_lbl.pack(pady=(12,2))
        support_lbl.bind("<Button-1>", lambda e: open_support())

        # "about the cat" — Aufklaerung + Widmung
        def open_cat_thing():
            cw = tk.Toplevel(win); cw.title("about the cat")
            cw.configure(bg=self.C["bg"]); cw.geometry("520x560")
            tk.Label(cw, text="about the cat", font=("Courier",11,"bold"),
                     fg=self.C["gold"], bg=self.C["bg"]).pack(pady=(14,8))
            txt_de = (
                "Ich habe mich fuer meine Katze als Programmlogo entschieden. "
                "Die folgenden Zeilen sind ihr gewidmet — eine kleine Hommage "
                "und ein Gedenken.\n\n"
                "Jede dritte Katze ab einem Alter von 10–12 Jahren leidet an CNI "
                "(chronischer Niereninsuffizienz). Leider wird die Krankheit oft "
                "erst sichtbar, wenn sie sich bereits dem Endstadium zuneigt. "
                "Viele Aerzte weisen nicht darauf hin.\n\n"
                "Fuehlen Sie sich also nicht sicher, nur weil Sie regelmaessig und "
                "verantwortungsvoll zum Tierarzt gehen und dieser sagt, es sei "
                "alles in Ordnung. Bitte verlangen Sie regelmaessig einen Bluttest. "
                "Nur dieser kann die Katze retten und ihr Leben nachweislich "
                "wertvoll verlaengern. Frueherkennung macht den Unterschied.\n\n"
                "Mein Kater bekam Anfang 2025 die Diagnose, nachdem es "
                "Auffaelligkeiten in seinem Verhalten gab: mehr Durst, der Drang, "
                "ungewoehnliche Wasserquellen aufzusuchen. Wir entschieden uns "
                "fuer eine Behandlung in einer Tierklinik, und waehrend dieser "
                "Zeit haben ihn sich gleich mehrere Aerzte angeschaut — und jeder "
                "war anderer Meinung. Es ist wichtig, auch Ihren eigenen Sinnen "
                "zu trauen, da Sie Ihre Katze am besten kennen.\n\n"
                "Am 11. Juni 2025 ist er gestorben.\n\n"
                "Die Katze im Intro ist ein Pixel-Portrait zur Erinnerung an "
                "meinen Kater, abgeleitet von einem echten Foto aus dem Jahr "
                "2010 — als wir ihn aus dem Tierheim holten.\n\n"
                "Auch wenn dieses Projekt einen Spenden-Button hat: Die groesste "
                "Freude, die Sie mir machen koennen, ist, Ihre Katze auf diese "
                "Krankheit untersuchen zu lassen.\n\n"
                "* CNI betrifft nicht nur Katzen, sondern auch andere Tiere.\n\n"
                "Und wenn das jetzt auch nur einer macht, hat dieser Text seinen "
                "Sinn und Zweck in einer Software, die damit eigentlich gar nichts "
                "zu tun hat, schon vollends erfuellt.\n\n"
                "Ich danke euch fuers Lesen.\n\n"
                "In memory of Jason — 2010–2025.")
            txt_en = (
                "I chose my cat as the program's logo. The following lines are "
                "dedicated to her — a small homage and a remembrance.\n\n"
                "One in three cats aged 10–12 or older suffers from CKD (chronic "
                "kidney disease). Sadly, the illness often only becomes visible "
                "once it is already nearing its final stage. Many vets don't point "
                "this out.\n\n"
                "So don't feel safe just because you take your cat to the vet "
                "regularly and responsibly and they tell you everything is fine. "
                "Please ask for a blood test regularly. Only that can save your cat "
                "and measurably extend its life in a meaningful way. Early "
                "detection makes the difference.\n\n"
                "My cat was diagnosed in early 2025, after changes in his "
                "behaviour: more thirst, the urge to seek out unusual sources of "
                "water. We decided on treatment at an animal clinic, and during "
                "that time several vets examined him — and each had a different "
                "opinion. It is important to trust your own senses too, because "
                "you know your cat best.\n\n"
                "He passed away on June 11, 2025.\n\n"
                "The cat in the intro is a pixel portrait in memory of my cat, "
                "derived from a real photo from 2010 — when we brought him home "
                "from the shelter.\n\n"
                "Even though this project has a donation button: the greatest gift "
                "you can give me is to have your own cat checked for this illness.\n\n"
                "* CKD does not only affect cats, but other animals too.\n\n"
                "And if even just one person does this, then this text has fully "
                "served its purpose — in a piece of software that actually has "
                "nothing to do with it.\n\n"
                "Thank you for reading.\n\n"
                "In memory of Jason — 2010–2025.")
            txt = txt_de if LANG == "de" else txt_en
            # OK-Button zuerst, unten verankert -> immer sichtbar
            tk.Button(cw, text="OK", font=("Courier",9), fg=self.C["cyan"], bg=self.C["panel"],
                      command=cw.destroy).pack(side="bottom", pady=10)
            # Scrollbarer Textbereich darueber
            tf = tk.Frame(cw, bg=self.C["bg"]); tf.pack(fill="both", expand=True, padx=20, pady=8)
            sb = tk.Scrollbar(tf); sb.pack(side="right", fill="y")
            tw = tk.Text(tf, font=("Courier",9), fg=self.C["fg"], bg=self.C["bg"],
                         wrap="word", relief="flat", yscrollcommand=sb.set,
                         padx=4, pady=4, highlightthickness=0, bd=0)
            tw.pack(side="left", fill="both", expand=True)
            sb.config(command=tw.yview)
            tw.insert("1.0", txt)
            tw.config(state="disabled")
        # "about the cat thing" — eigener, abgesetzter Bereich ganz unten
        tk.Frame(win, height=1, bg=self.C["gray"]).pack(fill="x", padx=40, pady=(10,0))
        tk.Button(win, text="· about the cat ·", font=("Courier",9,"bold"),
                  fg=self.C["gold"], bg=self.C["bg"], relief="flat", cursor="hand2",
                  bd=0, command=open_cat_thing).pack(pady=(6,12))

    def _build(self):
        tk.Label(self, text="MEGA-RAW",
                 font=("Courier",13,"bold"), fg=self.C["gold"], bg=self.C["bg"]).pack(pady=(10,0))
        tk.Label(self, text=f"— {version_str()} —",
                 font=("Courier",10,"bold"), fg=self.C["cyan"], bg=self.C["bg"]).pack(pady=(0,2))
        tk.Label(self, text="© 2026 Liqui",
                 font=("Courier",7), fg=self.C["gray"], bg=self.C["bg"]).pack(pady=(0,8))
        tk.Button(self, text=T("⚙ OPTIONEN"), font=("Courier",8), fg=self.C["gray"],
                  bg=self.C["bg"], relief="flat", cursor="hand2",
                  command=self._open_options).place(relx=0.97, y=12, anchor="ne")
        # Schnellstart fuer neue Nutzer
        # wraplength wird unten dynamisch an die Fensterbreite gekoppelt, damit
        # die Zeile auf schmalen/hochskalierten Bildschirmen (z.B. Surface)
        # umbricht statt abgeschnitten zu werden.
        self._quickstart = tk.Label(self, text=T("Schnellstart:  1. Einloggen   2. Spiel einmalig patchen   3. Spiel starten   4. Monitor starten"),
                 font=("Courier",8), fg=self.C["green"], bg=self.C["bg"],
                 justify="center", wraplength=700)
        self._quickstart.pack(pady=(0,6), fill="x")
        # Umbruchbreite an die jeweilige Fensterbreite anpassen
        def _wrap_quickstart(event):
            if event.widget is self:
                self._quickstart.configure(wraplength=max(300, event.width - 40))
        self.bind("<Configure>", _wrap_quickstart)
        # Statuszeile zuerst und am unteren Rand verankert -> immer sichtbar
        engv = getattr(racond, "ENGINE_VERSION", "ALT (v1?) — ra_condition.py aktualisieren!")
        self.log = tk.StringVar(value=f"Bereit. {version_str()} | Engine {engv} — Datei: {os.path.abspath(__file__)}")
        tk.Label(self, textvariable=self.log, font=("Courier",8),
                 fg=self.C["gray"], bg=self.C["bg"], wraplength=700).pack(side="bottom", pady=4)
        self._panel("LOGIN", self._login_ui)
        self._panel("PATCH", self._rom_ui)
        self._buttons()   # direkt unter ROM, wie urspruenglich
        self._panel("LIVE BRAM", self._bram_ui)
        self._panel("ACHIEVEMENTS", self._ac_ui, expand=True)

    def _login_ui(self, p):
        r = tk.Frame(p, bg=self.C["panel"]); r.pack(fill="x", pady=3)
        for lab,var,sh in [("User:",self.user,""),("Pass:",self.pw,"*")]:
            tk.Label(r,text=lab,font=("Courier",8),fg=self.C["gray"],bg=self.C["panel"],width=6).pack(side="left")
            tk.Entry(r,textvariable=var,font=("Courier",9),bg="#1a1a2a",fg=self.C["fg"],
                     insertbackground=self.C["fg"],relief="flat",width=18,show=sh).pack(side="left",padx=4)
        tk.Button(r,text=T("LOGIN"),font=("Courier",8,"bold"),fg=self.C["green"],bg=self.C["panel"],
                  relief="flat",cursor="hand2",command=self._login).pack(side="right")
        self.login_lbl = tk.Label(p,text=T("Nicht eingeloggt"),font=("Courier",8),fg=self.C["gray"],bg=self.C["panel"])
        self.login_lbl.pack(anchor="w")

    def _rom_ui(self, p):
        r = tk.Frame(p, bg=self.C["panel"]); r.pack(fill="x", pady=3)
        self.rom_lbl = tk.Label(r,text=T("Keine ROM"),font=("Courier",9),fg=self.C["fg"],bg=self.C["panel"],anchor="w")
        self.rom_lbl.pack(side="left",fill="x",expand=True)
        b_durch = tk.Button(r,text=T("DURCHSUCHEN"),font=("Courier",8),fg=self.C["gold"],bg=self.C["bg"],
                  relief="flat",cursor="hand2",command=self._choose); b_durch.pack(side="right")
        b_batch = tk.Button(r,text=T("ORDNER PATCHEN"),font=("Courier",8),fg=self.C["cyan"],bg=self.C["bg"],
                  relief="flat",cursor="hand2",command=self._batch_patch); b_batch.pack(side="right",padx=(0,8))
        b_sd = tk.Button(r,text=T("SD-EXPORT"),font=("Courier",8),fg=self.C["cyan"],bg=self.C["bg"],
                  relief="flat",cursor="hand2",command=self._sd_export); b_sd.pack(side="right",padx=(0,8))
        add_tip(b_durch, "EIN Spiel waehlen (ROM/ZIP/RAR), pruefen und einzeln patchen.")
        add_tip(b_batch, "Ganzen ORDNER patchen: jede ROM identifizieren und IPS daneben "
                         "erstellen. Schon gepatchte werden uebersprungen. Schreibt batch_log.txt "
                         "+ verify_report.txt.")
        add_tip(b_sd, "Spiele von Quelle auf die SD KOPIEREN — optional gleich patchen und in "
                      "Buchstaben-Ordner (A-E ...) sortieren gegen das EverDrive-Dateilimit.")
        self.game_lbl = tk.Label(p,text="",font=("Courier",9),fg=self.C["green"],bg=self.C["panel"])
        self.game_lbl.pack(anchor="w")
        self.compat_var = tk.StringVar(value="")
        self.compat_lbl = tk.Label(p,textvariable=self.compat_var,font=("Courier",8),
                                   fg=self.C["gray"],bg=self.C["panel"],justify="left",anchor="w")
        self.compat_lbl.pack(anchor="w")
        hr = tk.Frame(p,bg=self.C["panel"]); hr.pack(fill="x")
        tk.Label(hr,text="Hash:",font=("Courier",7),fg=self.C["gray"],bg=self.C["panel"],width=7,anchor="w").pack(side="left")
        self.hash_var = tk.StringVar(value="—")
        tk.Label(hr,textvariable=self.hash_var,font=("Courier",7),fg=self.C["fg"],bg=self.C["panel"]).pack(side="left")
        self.hash_st = tk.StringVar(value="")
        self.hash_st_lbl = tk.Label(hr,textvariable=self.hash_st,font=("Courier",7),fg=self.C["gray"],bg=self.C["panel"])
        self.hash_st_lbl.pack(side="left",padx=8)

    def _buttons(self):
        # Light-Stub-Auswahl (Standard: voller Stub) + kurze Hinweiszeile
        self.use_light = tk.BooleanVar(value=False)
        hr = tk.Frame(self, bg=self.C["bg"]); hr.pack(fill="x", padx=15, pady=(2,0))
        cbx = tk.Checkbutton(hr, text=T(MSG_LIGHT_LABEL), variable=self.use_light,
                             bg=self.C["bg"], fg=self.C["gray"], selectcolor=self.C["panel"],
                             font=("Courier",8), activebackground=self.C["bg"],
                             activeforeground=self.C["fg"])
        cbx.pack(side="left")
        add_tip(cbx, MSG_LIGHT_TIP)
        r = tk.Frame(self,bg=self.C["bg"]); r.pack(fill="x",padx=15,pady=4)
        self.patch_btn = tk.Button(r,text=T("IPS PATCH ERSTELLEN"),font=("Courier",8,"bold"),
                                   fg=self.C["blue"],bg=self.C["panel"],relief="flat",cursor="hand2",
                                   command=self._patch,state="normal")
        self.patch_btn.pack(side="left",fill="x",expand=True,padx=(0,4))
        self.mon_btn = tk.Button(r,text=T("MONITOR STARTEN"),font=("Courier",8,"bold"),
                                 fg=self.C["green"],bg=self.C["panel"],relief="flat",cursor="hand2",
                                 command=self._toggle_mon,state="normal")
        self.mon_btn.pack(side="left",fill="x",expand=True)
        self.tv_btn = tk.Button(r,text=T("TV-TEST"),font=("Courier",8,"bold"),
                                fg=self.C["gold"],bg=self.C["panel"],relief="flat",cursor="hand2",
                                command=self._tv_test)
        self.tv_btn.pack(side="left",padx=(4,0))
        add_tip(self.patch_btn, "Erstellt die IPS-Patchdatei fuer das aktuell geladene Spiel "
                                "(neben die ROM). Auf SD-Karte zur ROM legen.")
        add_tip(self.mon_btn, "Startet die Live-Ueberwachung: liest per USB den Spielstand und "
                              "schaltet Achievements frei. Spiel auf der Konsole starten.")
        add_tip(self.tv_btn, "Testet den goldenen Bildschirm-Blitz, der bei einem freigeschalteten "
                             "Achievement auf dem Fernseher erscheint.")

    def _tv_test(self):
        if not _ed_link:
            messagebox.showinfo(T("Hinweis"),T("Erst MONITOR STARTEN (Verbindung noetig) —\n"
                                "der Blitz braucht zudem eine Gen-44-IPS auf der Konsole."))
            return
        self.log.set(T("TV-Blitz gesendet — Bildschirm sollte golden aufleuchten"))
        threading.Thread(target=tv_flash,daemon=True).start()

    def _bram_ui(self, p):
        self.bram_var = tk.StringVar(value="—")
        self.bram_lbl = tk.Label(p,textvariable=self.bram_var,font=("Courier",8),fg=self.C["cyan"],
                 bg=self.C["panel"],anchor="w",justify="left")
        self.bram_lbl.pack(fill="x")

    def _ac_ui(self, p):
        # Scrollbare Liste: Canvas + inneres Frame
        wrap = tk.Frame(p,bg=self.C["panel"]); wrap.pack(fill="both",expand=True)
        self._ac_canvas = tk.Canvas(wrap,bg=self.C["panel"],highlightthickness=0,height=320)
        sb = tk.Scrollbar(wrap,orient="vertical",command=self._ac_canvas.yview)
        self._ac_canvas.configure(yscrollcommand=sb.set)
        sb.pack(side="right",fill="y"); self._ac_canvas.pack(side="left",fill="both",expand=True)
        self.ac_frame = tk.Frame(self._ac_canvas,bg=self.C["panel"])
        self._ac_canvas.create_window((0,0),window=self.ac_frame,anchor="nw")
        self.ac_frame.bind("<Configure>",
            lambda e: self._ac_canvas.configure(scrollregion=self._ac_canvas.bbox("all")))
        self._ac_canvas.bind_all("<MouseWheel>",
            lambda e: self._ac_canvas.yview_scroll(-1 if e.delta > 0 else 1, "units"))
        self.ac_labels = {}

    # ---- Actions ----
    def _login(self):
        u,pw = self.user.get().strip(), self.pw.get().strip()
        if not u or not pw: return
        self.login_lbl.config(text=T("Verbinde..."),fg=self.C["gray"])
        threading.Thread(target=self._login_t,args=(u,pw),daemon=True).start()

    def _login_t(self,u,pw):
        try:
            tok = ra_login(u,pw)
        except Exception as e:
            self.after(0,self.login_lbl.config,{"text":f"Login-Fehler: {e}","fg":self.C["red"]})
            self.after(0,self.log.set,f"RA-Login fehlgeschlagen: {e}")
            return
        if tok:
            self.ra_token = tok
            self.after(0,self.login_lbl.config,{"text":f"✓ {u}","fg":self.C["green"]})
            self.after(0,self.log.set,"Login OK")
            if self.game and not self.game.get("achievements"):
                self.after(0,self.log.set,"Login OK — lade Achievements nach...")
                threading.Thread(target=self._load_achievements,daemon=True).start()
        else:
            self.after(0,self.login_lbl.config,{"text":"Login fehlgeschlagen","fg":self.C["red"]})

    def _choose(self):
        path = filedialog.askopenfilename(filetypes=[("MD ROMs / Archive","*.md *.bin *.gen *.zip *.rar"),("Alle","*.*")])
        if not path: return
        if path.lower().endswith((".zip", ".rar")):
            path = self._extract_rom(path)
            if not path: return
        self.rom_path = path
        self.rom_lbl.config(text=os.path.basename(path))
        threading.Thread(target=self._identify,daemon=True).start()

    def _extract_rom(self, arc):
        """Erste MD-ROM aus ZIP/RAR neben das Archiv entpacken, Pfad zurueckgeben."""
        exts = (".md", ".bin", ".gen", ".smd")
        try:
            if arc.lower().endswith(".zip"):
                import zipfile; af = zipfile.ZipFile(arc)
            else:
                try:
                    import rarfile
                except ImportError:
                    self.log.set(T("RAR braucht das Modul 'rarfile':  pip install rarfile  (+ WinRAR installiert)"))
                    return None
                for p in (r"C:\Program Files\WinRAR\UnRAR.exe",
                          r"C:\Program Files (x86)\WinRAR\UnRAR.exe"):
                    if os.path.exists(p):
                        rarfile.UNRAR_TOOL = p; break
                af = rarfile.RarFile(arc)
            name = next((n for n in af.namelist() if n.lower().endswith(exts)), None)
            if not name:
                self.log.set(T("Keine MD-ROM (.md/.bin/.gen/.smd) im Archiv gefunden")); return None
            out = os.path.join(os.path.dirname(arc), os.path.basename(name))
            with af.open(name) as src, open(out, "wb") as dst:
                dst.write(src.read())
            self.log.set(f"Aus Archiv entpackt: {os.path.basename(out)}")
            return out
        except Exception as e:
            self.log.set(f"Archiv-Fehler: {e}")
            return None

    def _identify(self):
        h = md5_file(self.rom_path)
        self.after(0,self.hash_var.set,h)
        self.after(0,self.log.set,"Spiel-ID abfragen...")
        gid = ra_gameid(h)
        if not gid:
            self.after(0,self.hash_st.set,"unbekannt")
            self.after(0,self.hash_st_lbl.config,{"fg":self.C["red"]})
            self.after(0,self.log.set,f"Spiel nicht in RA-DB. MD5: {h[:12]}...")
            return
        self.after(0,self.hash_st.set,f"RA-ID {gid} ✓")
        self.after(0,self.hash_st_lbl.config,{"fg":self.C["green"]})
        self.game = {"name":f"#{gid}","gameid":gid,"md5":h,"achievements":[],"addr_map":{}}
        self.after(0,self.game_lbl.config,{"text":f"RA-ID {gid} erkannt"})
        if self.ra_token:
            self._load_achievements()
        else:
            self.after(0,self.log.set,f"ID {gid} erkannt — bitte einloggen für Achievements")
            self.after(0,self.mon_btn.config,{"state":"normal"})

    def _load_achievements(self):
        self.after(0,self.log.set,"Achievement-Daten laden...")
        patch = ra_patch(self.game["gameid"], self.user.get(), self.ra_token)
        if not patch:
            self.after(0,self.log.set,"Patch-Daten konnten nicht geladen werden")
            return
        self.game["name"] = patch.get("Title","?")
        if "unsupported game version" in self.game["name"].lower():
            self.game["no_set"] = True
            self.after(0,self.log.set,"STOPP: Dieser ROM-Dump ist bei RA KEINEM Set zugeordnet "
                       "(andere Region noetig, meist USA-Dump)")
            return
        achievements = []
        for ac in patch.get("Achievements",[]):
            if ac.get("Flags") != 3:  # nur core (offiziell veroeffentlicht), kein unofficial
                continue
            if ac.get("ID", 0) >= 101000000:  # RA-Pseudo-Eintraege (Emulator-Warnung)
                continue
            rt = racond.AchievementRuntime(ac.get("MemAddr",""))
            achievements.append({
                "id": ac["ID"], "title": ac["Title"], "desc": ac["Description"],
                "points": ac.get("Points",0), "mem": ac.get("MemAddr",""),
                "badge": ac.get("BadgeName",""),
                "triggered": False, "rt": rt, "unsupported": rt.unsupported,
            })
            if rt.unsupported:
                try:
                    with open(_p("ra_debug.txt"),"a",encoding="utf-8") as f:
                        f.write(f"UNSUPPORTED [{ac['ID']}] {ac['Title']}: {ac.get('MemAddr','')}\n")
                except Exception:
                    pass
        owned = ra_unlocks(self.game["gameid"], self.user.get(), self.ra_token)
        n_owned = 0
        for a in achievements:
            if a["id"] in owned:
                a["triggered"] = True
                a["owned"] = True
                n_owned += 1
        if n_owned:
            self.after(0, self.log.set,
                       f"{n_owned} Achievement(s) bereits freigeschaltet — golden markiert")
        # Spieldaten vollstaendig -> IPS-Erstellung freigeben
        self.after(0, self.patch_btn.config, {"state":"normal"})
        self.game["achievements"] = achievements

        # RAM-Adressen aus allen Conditions sammeln → BRAM-Mapping
        # collect_addresses liefert sortierte physische BYTE-Adressen (Byteswap inkl.)
        phys_bytes = racond.collect_addresses(achievements)
        addr_map = {}
        bram_byte = 1  # BRAM ungerade Bytes: 1,3,5,...
        addr_list = []
        for ram_addr in phys_bytes:
            addr_map[ram_addr] = bram_byte
            bram_byte += 2
            addr_list.append((ram_addr, 1))
        self.game["addr_map"] = addr_map
        self.game["addr_list"] = addr_list

        # DEBUG: MemAddr-Bedingungen + Mapping in Datei schreiben
        try:
            with open(_p("ra_debug.txt"),"w",encoding="utf-8") as dbg:
                dbg.write(f"=== {self.game['name']} (ID {self.game['gameid']}) ===\n")
                dbg.write(f"\naddr_list (Stub schreibt): {[(hex(a),n) for a,n in addr_list]}\n")
                dbg.write(f"\naddr_map (Monitor liest):\n")
                for a,o in sorted(addr_map.items()):
                    dbg.write(f"  0x{a:06X} -> BRAM[{o}]\n")
                dbg.write(f"\n=== Achievements ===\n")
                for ac in achievements:
                    dbg.write(f"\n[{ac['id']}] {ac['title']}\n")
                    dbg.write(f"  {ac['desc']}\n")
                    dbg.write(f"  MemAddr: {ac['mem']}\n")
        except Exception as e:
            pass

        self.after(0,self.game_lbl.config,{"text":f"✓ {self.game['name']}"})
        try:
            have_rom = bool(self.rom_path) and os.path.exists(self.rom_path)
            if have_rom:
                ok, lines = check_compat(self.rom_path, achievements)
            else:
                # Kein ROM-Pfad (z.B. Auto-Start): Aussagen aus Achievement-Daten
                ok, lines = True, []
                n = len(achievements) if achievements else 0
                uns = sum(1 for a in (achievements or []) if a.get("unsupported"))
                pal = sum(1 for a in (achievements or [])
                          if "0xS00fff9=0" in a.get("mem","") or "O:0xS00fff9!=0" in a.get("mem",""))
                lines.append(f"Achievements gesamt: {n}")
                lines.append(f"Von der Engine lesbar: {n-uns}/{n}")
                if uns: lines.append(f"Nicht auswertbar (Pointer/Sonderkonstrukt): {uns}")
                if pal: lines.append(f"Region-abhaengig (60Hz noetig): {pal}")
                lines.append("ROM-Datei nicht im Gedaechtnis — fuer Stub-/Boot-Pruefung")
                lines.append("einmal manuell 'DURCHSUCHEN' auf diese ROM.")
            head = "KOMPATIBEL" if ok else "NICHT KOMPATIBEL"
            col = self.C["green"] if ok else self.C["red"]
            self.after(0,self.compat_var.set, f"[{head}]\n" + "\n".join(lines))
            self.after(0,self.compat_lbl.config,{"fg":col})
        except Exception:
            pass
        if not achievements:
            self.game["no_set"] = True
            self.after(0,self.log.set,
                       f"{self.game['name']}: Dieses Spiel hat KEINE Core-Achievements auf RA — nichts zu tun.")
        else:
            self.after(0,self.log.set,f"{self.game['name']} — {len(achievements)} Achievements, {len(addr_map)} RAM-Bytes")
        self.after(0,self.patch_btn.config,{"state":"normal"})
        self.after(0,self.mon_btn.config,{"state":"normal"})
        self.after(0,self._build_ac_list)

    def _load_badge(self, badge):
        """Laedt ein RA-Achievement-Badge (mit lokalem Cache) und gibt ein
        kleines tk.PhotoImage zurueck. Ohne Pillow — nutzt PNG + subsample.
        Bei Fehler/ohne Badge: None (dann zeigt die Liste nur Text)."""
        if not badge:
            return None
        if not hasattr(self, "_badge_imgs"):
            self._badge_imgs = {}        # haelt Referenzen, sonst weg-gc't
        if badge in self._badge_imgs:
            return self._badge_imgs[badge]
        cache_dir = _p("badge_cache")
        try:
            os.makedirs(cache_dir, exist_ok=True)
        except Exception:
            pass
        local = os.path.join(cache_dir, f"{badge}.png")
        if not os.path.isfile(local):
            try:
                url = f"https://media.retroachievements.org/Badge/{badge}.png"
                req = urllib.request.Request(url, headers={
                    "User-Agent": "Mozilla/5.0 (MEGA-RAW Achievement Badge Loader)"})
                with urllib.request.urlopen(req, timeout=8) as r:
                    data = r.read()
                with open(local, "wb") as f:
                    f.write(data)
            except Exception:
                return None
        try:
            img = tk.PhotoImage(file=local)
            # proportional auf ~32px verkleinern (subsample ist ganzzahlig)
            w = img.width()
            factor = max(1, round(w / 32))
            if factor > 1:
                img = img.subsample(factor, factor)
            self._badge_imgs[badge] = img
            return img
        except Exception:
            return None

    def _build_ac_list(self):
        for w in self.ac_frame.winfo_children(): w.destroy()
        self.ac_labels = {}
        if not self.game.get("achievements"):
            empty_txt = ("Keine Core-Achievements auf RA fuer dieses Spiel"
                         if self.game and self.game.get("no_set")
                         else "Keine Achievements (Login nötig)")
            tk.Label(self.ac_frame,text=empty_txt,font=("Courier",8),
                     fg=self.C["gray"],bg=self.C["panel"]).pack(anchor="w")
            return
        for ac in self.game["achievements"]:
            row = tk.Frame(self.ac_frame,bg=self.C["bg"]); row.pack(fill="x")
            mem = ac.get("mem","")
            has_region = ("0xS00fff9=0" in mem) or ("O:0xS00fff9!=0" in mem)
            cpal = getattr(self, "_console_pal", None)
            if ac.get("owned"):
                mark, suffix, color = "★", "  [bereits freigeschaltet]", self.C["gold"]
            elif ac.get("unsupported"):
                mark, suffix, color = "✕", "  [nicht unterstuetzt]", self.C["gray"]
            elif has_region and cpal is True:
                mark, suffix, color = "◐", "  [PAL-GESPERRT: Konsole laeuft 50Hz]", self.C["red"]
            elif has_region and cpal is None:
                mark, suffix, color = "○", "  [region-abhaengig: braucht 60Hz]", self.C["gold"]
            else:
                mark, suffix, color = "○", "", self.C["gray"]
            # Badge-Icon (Platzhalter-Label, wird im Hintergrund mit Bild gefuellt)
            icon = tk.Label(row, bg=self.C["bg"])
            icon.pack(side="left", padx=(4,2))
            lbl = tk.Label(row,text=f"{mark}  {ac['title']} ({ac['points']}p) — {ac['desc']}{suffix}",
                           font=("Courier",8),fg=color,bg=self.C["bg"],anchor="w",justify="left",
                           cursor="hand2")
            lbl.pack(side="left",fill="x",expand=True,padx=2)
            self.ac_labels[ac["id"]] = lbl
            self._ac_icons = getattr(self, "_ac_icons", {})
            self._ac_icons[ac["id"]] = icon
            # Klick auf Icon oder Text -> Achievement-Seite auf retroachievements.org
            def open_ra(_e, aid=ac["id"]):
                import webbrowser
                try: webbrowser.open(f"https://retroachievements.org/achievement/{aid}")
                except Exception: pass
            icon.configure(cursor="hand2")
            icon.bind("<Button-1>", open_ra)
            lbl.bind("<Button-1>", open_ra)
        # Badges im Hintergrund laden, damit die GUI nicht einfriert
        threading.Thread(target=self._load_badges_bg, daemon=True).start()

    def _load_badges_bg(self):
        """Laedt die Badge-Icons nacheinander im Hintergrund und setzt sie in
        die bereits sichtbaren Zeilen. Friert die GUI nicht ein."""
        for ac in list(self.game.get("achievements", [])):
            img = self._load_badge(ac.get("badge",""))
            if img is not None:
                icon = getattr(self, "_ac_icons", {}).get(ac["id"])
                if icon is not None:
                    self.after(0, lambda ic=icon, im=img: ic.config(image=im))

    def _patch(self):
        if not self.rom_path or not self.game:
            messagebox.showwarning(T("Hinweis"),T("Zuerst ein ROM laden (DURCHSUCHEN)."))
            return
        if not self.game.get("addr_list"):
            if self.game.get("no_set"):
                messagebox.showinfo("Hinweis",
                    "Dieses Spiel hat keine Core-Achievements auf RetroAchievements.\n"
                    "Es gibt nichts zu patchen oder zu ueberwachen.")
                return
            if self.ra_token and self.game:
                self.log.set(T("Spiel-Daten werden nachgeladen — bitte gleich erneut klicken..."))
                threading.Thread(target=self._load_achievements,daemon=True).start()
            else:
                messagebox.showwarning(T("Hinweis"),T("Bitte zuerst einloggen, dann ROM laden."))
            return
        with open(self.rom_path,"rb") as f:
            orig = bytearray(f.read())
        h = self.game["md5"]
        # KRIKzz-Methode: VBlank-Vektor umleiten statt Handler patchen
        orig_vblank = struct.unpack_from(">I", orig, 0x78)[0]
        needed = 60 + 10 * sum(n for _, n in self.game["addr_list"]) + 100
        # Light-Stub-Auswahl aus der Checkbox (Standard: voller Stub)
        use_slim = bool(self.use_light.get())
        build_fn = build_stub_slim if use_slim else build_stub
        if use_slim:
            probe = build_stub_slim(0x200000, orig_vblank, self.game["addr_list"],
                                    self.game.get("gameid", 0))
            needed = max(needed, len(probe) + 32)   # Light-Stub ist groesser
        rom_end = rom_end_from_header(orig)
        if h in HOOK_DB and HOOK_DB[h].get("stub"):
            stub_addr, outside = HOOK_DB[h]["stub"], False
        else:
            stub_addr, outside = find_stub_addr(orig, needed)
        if not stub_addr:
            messagebox.showerror(T("Fehler"),T("Kein Platz fuer Stub (ROM > 4MB?).")); return

        stub = build_fn(stub_addr, orig_vblank, self.game["addr_list"], self.game.get("gameid", 0))
        patches = [(0x78, struct.pack(">I", stub_addr)),  # VBlank-Vektor → Stub
                   (stub_addr, stub)]
        if not outside:
            # $18E nur korrigieren, wenn das Spiel die STANDARD-Summe traegt
            # (0x200..Header-Ende). Anderer Wert = eigene Routine ueber eigenen
            # Bereich (Dynamite Headdy: nur bis 0x60001) — dann zerstoert die
            # "Korrektur" den Boot. Die Konsole selbst prueft $18E nie.
            stored = struct.unpack_from(">H", orig, 0x18E)[0]
            if stored == calc_checksum(orig, rom_end):
                patched = bytearray(max(len(orig), stub_addr + len(stub)))
                patched[:len(orig)] = orig
                patched[stub_addr:stub_addr+len(stub)] = stub
                struct.pack_into(">I", patched, 0x78, stub_addr)
                patches.insert(0, (0x018E, struct.pack(">H", calc_checksum(patched, rom_end))))
        ips = make_ips(patches)
        ips_path = os.path.splitext(self.rom_path)[0] + ".ips"
        with open(ips_path,"wb") as f:
            f.write(ips)
        try:
            gc = {}
            if os.path.exists(_p("games_cache.json")):
                gc = json.load(open(_p("games_cache.json"),encoding="utf-8"))
            gc[str(self.game["gameid"])] = {"name": self.game["name"],
                "rom": self.rom_path, "md5": self.game.get("md5",""),
                "compat": f"VBlank 0x{orig_vblank:06X} | Stub 0x{stub_addr:06X}"
                          f"{' (angehaengt)' if outside else ' (in ROM)'}",
                "compat_block": self.compat_var.get()}
            json.dump(gc, open(_p("games_cache.json"),"w",encoding="utf-8"), indent=1)
        except Exception:
            pass
        self.log.set(f"IPS: {os.path.basename(ips_path)} (Stub {len(stub)}B{', LIGHT' if use_slim else ''})")
        messagebox.showinfo(T("Fertig"),
            T("IPS erstellt:\n{}\n\nAuf SD-Karte neben die ROM.").format(ips_path)
            + (T(MSG_LIGHT_ACTIVE) if use_slim else ""))

    def _batch_patch(self):
        if not self.ra_token:
            messagebox.showwarning(T("Hinweis"),T("Bitte zuerst einloggen.")); return
        folder = filedialog.askdirectory(title="Ordner mit ROMs/Archiven waehlen")
        if not folder: return
        win = tk.Toplevel(self); win.title("ORDNER PATCHEN"); win.configure(bg=self.C["bg"])
        win.transient(self); win.lift(); win.focus_force()   # ueber dem Hauptfenster
        del_arc = tk.BooleanVar(value=False)
        tk.Label(win,text=f"Ordner: {folder}",bg=self.C["bg"],fg=self.C["green"],
                 font=("Courier",8),justify="left").pack(anchor="w",padx=8,pady=4)
        def _confirm_del_arc():
            if del_arc.get():   # nur beim AKTIVIEREN nachfragen
                if not messagebox.askyesno("Sicher?",
                        "Sind Sie sicher, dass das Quell-Archiv (RAR/ZIP) nach dem "
                        "Entpacken geloescht werden soll?\n\n"
                        "Das Original auf dem PC ist danach weg."):
                    del_arc.set(False)
        cb = tk.Checkbutton(win,text=T("Quell-Archiv (RAR/ZIP) nach Entpacken loeschen"),
                       variable=del_arc,bg=self.C["bg"],fg=self.C["red"],command=_confirm_del_arc,
                       selectcolor=self.C["bg"],font=("Courier",8)); cb.pack(anchor="w",padx=8)
        pv = tk.StringVar(value="")
        tk.Label(win,textvariable=pv,bg=self.C["bg"],fg=self.C["gold"],
                 font=("Courier",8),justify="left").pack(anchor="w",padx=8,pady=6)
        pbtn = tk.Button(win,text=T("START"),font=("Courier",8,"bold"),
                 fg=self.C["cyan"],bg=self.C["bg"])
        def run():
            cb.config(state="disabled")
            pbtn.config(state="disabled", text=T("LAEUFT..."))
            threading.Thread(target=self._batch_patch_t,
                             args=(folder, del_arc.get(), pv, win, pbtn), daemon=True).start()
        pbtn.config(command=run); pbtn.pack(anchor="w",padx=8,pady=6)
        self._center_over(win)

    def _center_over(self, win):
        """Fenster mittig ueber das Hauptfenster setzen (statt oben links)."""
        try:
            win.update_idletasks()
            w, h = win.winfo_reqwidth(), win.winfo_reqheight()
            px, py = self.winfo_rootx(), self.winfo_rooty()
            pw, ph = self.winfo_width(), self.winfo_height()
            x = px + max(0, (pw - w)//2); y = py + max(0, (ph - h)//2)
            win.geometry(f"+{x}+{y}")
        except Exception:
            pass

    def _batch_patch_t(self, folder, del_arc=False, pv=None, pwin=None, pbtn=None):
        def _pset(msg):
            if pv is not None: self.after(0, pv.set, msg)
        def _pfin(msg):
            _pset(msg)
            def _f():
                try: pbtn.config(state="normal", text=T("SCHLIESSEN"), command=pwin.destroy)
                except Exception: pass
            if pbtn is not None and pwin is not None: self.after(0, _f)
        exts = (".md",".bin",".gen",".smd",".zip",".rar")
        files = [os.path.join(folder,f) for f in sorted(os.listdir(folder))
                 if f.lower().endswith(exts)]
        ok = skip = 0
        # Fehler nach Grund aufschluesseln statt in einen Topf
        reasons = {"unbekannt":[], "kein_set":[], "unsupported_version":[],
                   "kein_platz":[], "archiv":[], "fehler":[]}
        # Verifikation: kein_set sicher leer vs. nur Engine-Luecke trennen
        verify = {"set_echt_leer":[], "set_engine_luecke":[], "set_voll_ok":[]}
        patched_ids = []   # (gameid, name, n_core, n_parsebar) je erfolgreich gepatchtem Spiel
        boot_risk_list = []  # (stufe, base, gruende) je gepatchtem Spiel
        logf = open(_p("batch_log.txt"), "w", encoding="utf-8")
        logf.write(f"Batch ueber {folder} ({len(files)} Dateien)\n\n")
        for i, f in enumerate(files, 1):
            base = os.path.basename(f)
            self.after(0,self.log.set,f"[{i}/{len(files)}] {base} ...")
            _pset(f"[{i}/{len(files)}]\n{base}")
            try:
                rom = f
                if f.lower().endswith((".zip",".rar")):
                    rom = extract_archive_rom(f)
                    if not rom:
                        reasons["archiv"].append(base); logf.write(f"ARCHIV-LEER  {base}\n"); continue
                    if del_arc and rom != f and os.path.exists(rom):
                        try: os.remove(f); logf.write(f"ARCHIV-GELOESCHT  {base}\n")
                        except OSError as de: logf.write(f"ARCHIV-LOESCH-FEHLER  {base}  {de}\n")
                if os.path.exists(os.path.splitext(rom)[0] + ".ips"):
                    skip += 1
                    # Bereits gepatcht: Game-ID trotzdem fuer den Abgleich erfassen
                    # (aus Cache, ohne neuen Server-Abruf).
                    try:
                        hh = md5_file(rom)
                        gid_c = _cache()["gameid"].get(hh)
                        if gid_c:
                            patched_ids.append((gid_c, os.path.splitext(base)[0], -1, -1))
                    except Exception:
                        pass
                    continue
                h = md5_file(rom)
                cached = h in _cache()["gameid"]
                if not cached:
                    time.sleep(RA_REQUEST_PAUSE)   # feste Server-Schonung, nicht abschaltbar
                gid = None
                for attempt in range(2):
                    try:
                        gid = ra_gameid(h, raise_limit=True)
                        break
                    except RateLimited as rl:
                        wait = rl.retry_after or (15 if attempt == 0 else 0)
                        if attempt == 0 and wait:
                            self.after(0,self.log.set,f"RA drosselt — warte {wait}s und mache weiter...")
                            time.sleep(min(wait, 60)); continue
                        logf.write(f"\n!! RATE-LIMIT bei {base} — Batch gestoppt. "
                                   f"Spaeter erneut (Cache + gepatchte werden uebersprungen).\n")
                        logf.close()
                        self.after(0,self.log.set,"RA-Drossel — Batch gestoppt (siehe batch_log.txt)")
                        _pfin(f"RA-Drossel — gestoppt.\nBis hier gepatcht: {ok}\nSpaeter erneut ausfuehren.")
                        self.after(0,lambda: messagebox.showwarning("Rate-Limit",
                            f"RetroAchievements drosselt anhaltend.\n\n"
                            f"Bis hier gepatcht: {ok}\n\n"
                            f"Spaeter ORDNER PATCHEN erneut — dank Cache und uebersprungenen "
                            f"Patches laeuft es zuegig weiter."))
                        return
                if not gid:
                    reasons["unbekannt"].append(base); logf.write(f"HASH-UNBEKANNT  {base}  md5={h}\n"); continue
                game = build_game(gid, h, self.user.get(), self.ra_token)
                if game and game.get("no_set"):
                    reasons["unsupported_version"].append(base)
                    logf.write(f"KEIN-DUMP-MATCH  {base}  (Beta/Proto/Alt-Version oder andere Region)\n"); continue
                if not game or not game.get("addr_list"):
                    reasons["kein_set"].append(base)
                    raw = (game or {}).get("raw_core", 0)
                    if raw == 0:
                        verify["set_echt_leer"].append(base)
                        logf.write(f"KEIN-SET-VERIFIZIERT  {base}  (RA fuehrt 0 Achievements — berechtigt)\n")
                    else:
                        verify["set_engine_luecke"].append((base, raw))
                        logf.write(f"KEIN-SET-ABER-{raw}-VORHANDEN  {base}  "
                                   f"(Engine konnte keine parsen — UNSER THEMA, nicht leer!)\n")
                    continue
                # Vorab: ist es die 4MB-Grenze? Dann ehrlich benennen, nicht "Fehler".
                with open(rom, "rb") as _rf:
                    _data = _rf.read()
                _need = 60 + 10 * sum(n for _, n in game["addr_list"]) + 100
                if find_stub_addr(_data, _need)[0] is None:
                    grund = stub_failure_reason(_data, _need)
                    reasons["kein_platz"].append(base)
                    logf.write(f"KEIN-STUB-PLATZ  {base}  ({grund})\n")
                    continue
                if self.settings.get("av_safe"):
                    time.sleep(0.4)    # AV-schonend: Schreibrate VOR dem Patch druecken
                good, msg, risk = patch_rom_file(rom, game)
                if good:
                    ok += 1
                    nu = game.get("n_unsupported", 0); rc = game.get("raw_core", 0)
                    if rc > 0 and rc - nu == 0:
                        logf.write(f"WARNUNG-0-LESBAR  {base}  (gepatcht, aber KEIN Achievement "
                                   f"von der Engine lesbar — Pointer-/Sonderkonstrukte)\n")
                    if risk:
                        stufe, gruende = risk
                        boot_risk_list.append((stufe, base, gruende))
                    patched_ids.append((game.get("gameid",0), game.get("name","?"), rc, rc-nu))
                    if nu:
                        logf.write(f"OK  {base}  {msg}  ({rc-nu}/{rc} Achievements parsebar, {nu} nicht)\n")
                    else:
                        logf.write(f"OK  {base}  {msg}  ({rc}/{rc} Achievements parsebar)\n")
                else:
                    reasons["kein_platz"].append(base); logf.write(f"PATCH-FEHLER  {base}  {msg}\n")
            except Exception as e:
                reasons["fehler"].append(base); logf.write(f"EXCEPTION  {base}  {e}\n")
        # ---- Verifikations-Report ----
        try:
            with open(_p("verify_report.txt"), "w", encoding="utf-8") as vf:
                vf.write(f"=== VERIFIKATIONS-REPORT  {folder} ===\n\n")
                vf.write(f"Gepatcht: {ok}\n\n")
                vf.write(f"[KEIN-SET — verifiziert berechtigt: RA fuehrt 0 Achievements] "
                         f"{len(verify['set_echt_leer'])}\n")
                for b in verify["set_echt_leer"]: vf.write(f"  {b}\n")
                vf.write(f"\n[KEIN-SET — ABER Achievements vorhanden, Engine-Luecke!] "
                         f"{len(verify['set_engine_luecke'])}\n")
                for b, n in verify["set_engine_luecke"]: vf.write(f"  {b}  ({n} Achievements)\n")
                vf.write(f"\n[KEIN PASSENDES SET FUER DIESEN DUMP — Beta/Proto/Alt-Version "
                         f"oder andere Region; RA kennt nur den finalen Verkaufs-Dump] "
                         f"{len(reasons['unsupported_version'])}\n")
                for b in reasons["unsupported_version"]: vf.write(f"  {b}\n")
                vf.write(f"\n[HASH UNBEKANNT — Bad Dump/Hack, verifiziert nicht in RA-DB] "
                         f"{len(reasons['unbekannt'])}\n")
                for b in reasons["unbekannt"]: vf.write(f"  {b}\n")
                vf.write(f"\n[ECHTE FEHLER] "
                         f"{len(reasons['kein_platz'])+len(reasons['fehler'])+len(reasons['archiv'])}\n")
                for b in reasons["kein_platz"]: vf.write(f"  PATCH-FEHLER  {b}\n")
                for b in reasons["fehler"]: vf.write(f"  EXCEPTION  {b}\n")
                for b in reasons["archiv"]: vf.write(f"  ARCHIV  {b}\n")
        except Exception:
            pass
        logf.close()
        _cache_save(force=True)   # restlichen Cache sicher schreiben
        try:
            with open(_p("patched_games.txt"), "w", encoding="utf-8") as pf:
                pf.write(f"# Erfolgreich gepatchte Spiele dieses Laufs: {len(patched_ids)}\n")
                pf.write("# GameID\tParsebar/Gesamt\tTitel\n")
                seen_ids = set()
                for gid, name, rc, ok_n in sorted(patched_ids):
                    if gid in seen_ids: continue
                    seen_ids.add(gid)
                    quote = "vorhanden" if rc < 0 else f"{ok_n}/{rc}"
                    pf.write(f"{gid}\t{quote}\t{name}\n")
                pf.write(f"\n# Einzigartige Game-IDs gesamt: {len(seen_ids)}\n")
        except Exception:
            pass
        # Boot-Risiko-Report
        try:
            hoch = [x for x in boot_risk_list if x[0] == "hoch"]
            pruef = [x for x in boot_risk_list if x[0] == "pruefen"]
            oks = [x for x in boot_risk_list if x[0] == "ok"]
            with open(_p("boot_risk.txt"), "w", encoding="utf-8") as bf:
                bf.write("=== BOOT-RISIKO-ANALYSE (statisch, ersetzt keinen echten Konsolentest) ===\n\n")
                bf.write(f"Unauffaellig (hohe Boot-Wahrscheinlichkeit): {len(oks)}\n")
                bf.write(f"An Konsole testen (Risiko-Indikator):        {len(pruef)}\n")
                bf.write(f"Hohes Risiko (bekannte Crash-Klasse):        {len(hoch)}\n\n")
                if hoch:
                    bf.write("--- HOHES RISIKO (zuerst testen) ---\n")
                    for _, b, gr in hoch:
                        bf.write(f"  {b}\n")
                        for g in gr: bf.write(f"      - {g}\n")
                if pruef:
                    bf.write("\n--- AN KONSOLE TESTEN ---\n")
                    for _, b, gr in pruef:
                        bf.write(f"  {b}\n")
                        for g in gr: bf.write(f"      - {g}\n")
                bf.write(f"\n--- UNAUFFAELLIG ({len(oks)}) ---\n")
                for _, b, gr in oks:
                    bf.write(f"  {b}\n")
        except Exception:
            pass
        n_luecke = len(verify["set_engine_luecke"])
        nset = len(reasons["kein_set"]); nreg = len(reasons["unsupported_version"])
        nunk = len(reasons["unbekannt"]); nerr = len(reasons["kein_platz"])+len(reasons["fehler"])+len(reasons["archiv"])
        self.after(0,self.log.set,f"Batch fertig: {ok} gepatcht — Details in batch_log.txt")
        _pfin(f"FERTIG: {ok} gepatcht, {skip} schon vorhanden")
        self.after(0,lambda: messagebox.showinfo("Batch-Patch",
            f"{ok} ROMs gepatcht, {skip} schon vorhanden\n\n"
            f"Nicht gepatcht (legitim):\n"
            f"  {nset} ohne Achievement-Set\n"
            f"  {nreg} kein Set fuer diesen Dump (meist Beta/Proto/Alt-Version)\n"
            f"  {nunk} Hash nicht in RA-Datenbank (Bad Dump/Hack)\n\n"
            f"Echte Fehler: {nerr}\n"
            f"davon \"kein Set\" mit DOCH vorhandenen Achievements (Engine-Luecke): {n_luecke}\n\n"
            f"Details: batch_log.txt + verify_report.txt"))

    def _sd_export(self):
        if not self.ra_token:
            messagebox.showwarning(T("Hinweis"),T("Bitte zuerst einloggen (fuer optionales IPS-Erstellen).")); return
        src = filedialog.askdirectory(title="QUELLE: Ordner mit Spielen")
        if not src: return
        dst = filedialog.askdirectory(title="ZIEL: EverDrive-SD (Export)")
        if not dst: return
        win = tk.Toplevel(self); win.title(T("SD-EXPORT")); win.configure(bg=self.C["bg"])
        win.transient(self)             # bleibt ueber dem Hauptfenster
        win.lift(); win.focus_force()   # sofort in den Vordergrund
        do_ips = tk.BooleanVar(value=True)
        do_bucket = tk.BooleanVar(value=True)
        del_arc = tk.BooleanVar(value=False)   # Standard: Archive BEHALTEN (sicher)
        tk.Label(win,text=f"Quelle: {src}\nZiel: {dst}",bg=self.C["bg"],fg=self.C["green"],
                 font=("Courier",8),justify="left").pack(anchor="w",padx=8,pady=4)
        cb1 = tk.Checkbutton(win,text=T("IPS gleich miterstellen"),variable=do_ips,bg=self.C["bg"],
                       fg=self.C["green"],selectcolor=self.C["bg"],font=("Courier",8)); cb1.pack(anchor="w",padx=8)
        cb2 = tk.Checkbutton(win,text=T("In Ordner sortieren (automatisch, EverDrive-Dateilimit)"),
                       variable=do_bucket,bg=self.C["bg"],fg=self.C["green"],
                       selectcolor=self.C["bg"],font=("Courier",8)); cb2.pack(anchor="w",padx=8)
        def _confirm_del_arc():
            if del_arc.get():   # nur beim AKTIVIEREN nachfragen
                if not messagebox.askyesno("Sicher?",
                        "Sind Sie sicher, dass das Quell-Archiv (RAR/ZIP) nach dem "
                        "Entpacken geloescht werden soll?\n\n"
                        "Das Original auf dem PC ist danach weg."):
                    del_arc.set(False)   # abgelehnt -> Haken zurueck
        cb3 = tk.Checkbutton(win,text=T("Quell-Archiv (RAR/ZIP) nach Entpacken loeschen"),
                       variable=del_arc,bg=self.C["bg"],fg=self.C["red"],command=_confirm_del_arc,
                       selectcolor=self.C["bg"],font=("Courier",8)); cb3.pack(anchor="w",padx=8)
        add_tip(cb1, "Erstellt fuer jede kopierte ROM gleich die IPS-Patchdatei im Ziel. "
                     "Ohne Haken werden ROMs nur kopiert, nicht gepatcht.")
        add_tip(cb2, "Sortiert die Spiele automatisch in 'von-bis'-Ordner und fuellt jeden "
                     "nur bis zum EverDrive-Menuelimit (21 Eintraege/Seite x 40 Seiten = 840 "
                     "Eintraege je Ordner). ROM und .ips zaehlen zusammen und bleiben immer "
                     "im selben Ordner. So versteckt das Menue keine Spiele mehr.")
        add_tip(cb3, "ACHTUNG: Loescht das originale RAR/ZIP, nachdem die ROM erfolgreich "
                     "ins Ziel kopiert wurde. Nur einschalten, wenn du die Archive nicht "
                     "mehr brauchst. Standard: aus.")
        logv = tk.StringVar(value="")
        tk.Label(win,textvariable=logv,bg=self.C["bg"],fg=self.C["gold"],font=("Courier",8)).pack(anchor="w",padx=8,pady=4)
        stop = threading.Event()
        start_btn = tk.Button(win,text=T("START"),font=("Courier",8,"bold"),fg=self.C["cyan"],bg=self.C["bg"])
        def do_cancel():
            stop.set()
            try: start_btn.config(state="disabled", text=T("BRECHE AB..."))
            except Exception: pass
        def run():
            stop.clear()
            start_btn.config(text=T("ABBRECHEN"), command=do_cancel)
            threading.Thread(target=self._sd_export_t,
                args=(src,dst,do_ips.get(),do_bucket.get(),logv,win,del_arc.get(),start_btn,stop),
                daemon=True).start()
        def on_close():
            # Fenster schliessen = laufenden Export wirklich stoppen (nicht heimlich
            # weiterlaufen lassen). Worker beendet sauber und schliesst das Fenster.
            stop.set()
            if start_btn["text"] in (T("START"), T("SCHLIESSEN")):
                win.destroy()           # laeuft gerade nichts -> sofort zu
            else:
                win._closing = True     # Worker schliesst nach sauberem Abbruch
                try: start_btn.config(state="disabled", text=T("BRECHE AB..."))
                except Exception: pass
        win.protocol("WM_DELETE_WINDOW", on_close)
        start_btn.config(command=run); start_btn.pack(anchor="w",padx=8,pady=6)
        self._center_over(win)

    def _sd_export_t(self, src, dst, do_ips, do_bucket, logv, win, del_arc=False, start_btn=None, stop=None):
        import shutil
        exts = (".md",".bin",".gen",".smd",".zip",".rar")
        # Rekursiv suchen — ROMs liegen oft in Unterordnern
        files = []
        for root, _dirs, fnames in os.walk(src):
            for fn in sorted(fnames):
                if fn.lower().endswith(exts):
                    files.append(os.path.join(root, fn))
        files.sort(key=lambda p: os.path.basename(p).lower())
        if not files:
            self.after(0,logv.set,"KEINE ROMs/Archive im Quellordner gefunden (auch nicht in Unterordnern)")
            return
        if os.path.abspath(dst) == os.path.abspath(src):
            self.after(0,logv.set,"FEHLER: Quelle und Ziel sind identisch — bitte anderes Ziel waehlen")
            return
        # --- Pass 1: flach ins Staging kopieren + patchen ---
        staging = os.path.join(dst, "_mega_raw_tmp")
        os.makedirs(staging, exist_ok=True)
        placed = []   # (rom_path_im_staging, ips_path|None)
        copied = ips = fail = skip = deferred = 0
        n_unknown = n_noset = n_unsupported = n_luecke = 0
        problems = []   # nur Spiele MIT RA-Set, die NICHT gepatcht wurden (Name, Grund)
        ra_throttled = False   # nach erster anhaltender RA-Drossel: keine RA-Anfragen mehr
        for i, f in enumerate(files, 1):
            if stop is not None and stop.is_set():
                break
            base = os.path.basename(f)
            self.after(0,logv.set,f"[{i}/{len(files)}] {base}")
            try:
                rom = f
                was_archive = f.lower().endswith((".zip",".rar"))
                if was_archive:
                    # Archiv in EIGENEN Temp-Ordner entpacken (NIE in den Quellordner),
                    # nur die ROM ins Staging holen, Temp danach restlos loeschen.
                    # Der PC-Quellordner behaelt so nur die RAR/ZIP.
                    tdir = tempfile.mkdtemp(dir=dst, prefix="_mraw_x_")
                    try:
                        ext = extract_archive_rom(f, dest_dir=tdir)
                        if not ext:
                            fail += 1; continue
                        dst_rom = os.path.join(staging, os.path.basename(ext))
                        if os.path.exists(dst_rom):
                            skip += 1; continue
                        shutil.move(ext, dst_rom)
                    finally:
                        shutil.rmtree(tdir, ignore_errors=True)
                    copied += 1
                    if del_arc:
                        try: os.remove(f)          # nur das Original-Archiv (optional)
                        except OSError: pass
                else:
                    dst_rom = os.path.join(staging, os.path.basename(rom))
                    if os.path.abspath(dst_rom) == os.path.abspath(rom):
                        skip += 1; continue
                    if os.path.exists(dst_rom):     # gleicher Dateiname schon da
                        skip += 1; continue
                    shutil.copy2(rom, dst_rom); copied += 1
                ips_path = None
                if do_ips:
                    h = md5_file(dst_rom)
                    gc = _cache()["gameid"]
                    gid = None; proceed = True
                    if h in gc:
                        gid = gc[h] or None          # gecacht: sofort, keine RA-Anfrage
                    elif ra_throttled:
                        deferred += 1; proceed = False   # Drosselzustand: NICHT mehr anfragen
                    else:
                        time.sleep(RA_REQUEST_PAUSE)     # feste Server-Schonung
                        try:
                            gid = ra_gameid(h, raise_limit=True)
                        except RateLimited as rl:
                            # EIN Mal Retry-After abwarten (RA Zeit geben), dann EIN
                            # Retry. Bleibt es gedrosselt -> ab hier fuer den REST des
                            # Exports keine RA-Anfragen mehr (jede Anfrage im Drossel-
                            # zustand verlaengert sonst das Wartefenster).
                            wait = min(rl.retry_after or 30, 60)
                            self.after(0,logv.set,
                                f"[{i}/{len(files)}] RA drosselt — warte {wait}s, dann Rest offen lassen...")
                            gid = None
                            if not (stop is not None and stop.wait(wait)):
                                try: gid = ra_gameid(h, raise_limit=True)
                                except RateLimited: gid = None
                            if gid is None:
                                ra_throttled = True; deferred += 1; proceed = False
                    if proceed and gid:
                        if ra_throttled and str(gid) not in _cache()["patch"]:
                            deferred += 1                # Drosselzustand: Patch nicht abrufen
                        else:
                            if str(gid) not in _cache()["patch"]:
                                time.sleep(RA_REQUEST_PAUSE)   # 2. Anfrage pacen
                            try:
                                game = build_game(gid, h, self.user.get(), self.ra_token)
                            except RateLimited:
                                ra_throttled = True; deferred += 1; game = "DEFER"
                            if game == "DEFER":
                                pass                              # schon als offen gezaehlt
                            elif game and game.get("no_set"):
                                n_unsupported += 1                # RA kennt Spiel, aber diese ROM-Version hat kein Set
                            elif (not game) or (not game.get("addr_list")):
                                if (game or {}).get("raw_core", 0) == 0:
                                    n_noset += 1                  # Spiel in RA, aber 0 Achievements (berechtigt leer)
                                else:
                                    n_luecke += 1                 # Set da, Engine konnte nicht parsen (unser Thema)
                                    problems.append((base,
                                        f"Set vorhanden ({game.get('raw_core',0)} Achievements), "
                                        f"aber Engine parste keine Adresse"))
                            else:
                                good, msg, _ = patch_rom_file(dst_rom, game)
                                if good:
                                    cand = os.path.splitext(dst_rom)[0] + ".ips"
                                    if os.path.exists(cand):
                                        ips_path = cand; ips += 1
                                else:
                                    fail += 1
                                    problems.append((base, msg or "Patch fehlgeschlagen (meist kein Platz im ROM)"))
                    elif proceed and gid is None:
                        n_unknown += 1                            # ROM-Hash RA unbekannt
                placed.append((dst_rom, ips_path))
            except Exception as e:
                self.after(0,logv.set,f"{base}: {e}"); fail += 1
        # --- Pass 2: zaehlen, gruppieren, verschieben (auch bei Abbruch: das
        #     bereits Kopierte wird sauber einsortiert -> keine Waisen) ---
        cancelled = bool(stop is not None and stop.is_set())
        head = "ABGEBROCHEN" if cancelled else "FERTIG"
        moved = 0
        if do_bucket:
            self.after(0,logv.set,"Sortiere in Ordner und verschiebe...")
            placed.sort(key=lambda t: os.path.basename(t[0]).lower())
            groups = _letter_groups(placed)
            total = len(placed); spin = "|/-\\"
            for name, group in groups:
                folder = os.path.join(dst, name)
                os.makedirs(folder, exist_ok=True)
                for rom, ips_path in group:
                    for p in (rom, ips_path):
                        if not p: continue
                        tgt = os.path.join(folder, os.path.basename(p))
                        if os.path.abspath(tgt) != os.path.abspath(p):
                            try: os.replace(p, tgt)
                            except OSError: shutil.move(p, tgt)
                    moved += 1
                    if moved % 10 == 0:
                        self.after(0,logv.set,
                            f"Sortiere und verschiebe... {spin[(moved//10)%4]} {moved*100//total}%")
            self.after(0,logv.set,
                f"{head}: {copied} kopiert, {ips} mit IPS, {moved} Spiele in {len(groups)} Ordner "
                f"(max {EVERDRIVE_FOLDER_LIMIT}/Ordner), {skip} uebersprungen, {fail} Fehler"
                + (f" | {deferred} wegen RA-Drossel offen -> SD-Export erneut ausfuehren (Cache macht es schnell)" if deferred else ""))
        else:
            total = len(placed); spin = "|/-\\"
            for rom, ips_path in placed:
                for p in (rom, ips_path):
                    if not p: continue
                    tgt = os.path.join(dst, os.path.basename(p))
                    if os.path.abspath(tgt) != os.path.abspath(p):
                        try: os.replace(p, tgt)
                        except OSError: shutil.move(p, tgt)
                moved += 1
                if moved % 10 == 0:
                    self.after(0,logv.set,
                        f"Verschiebe... {spin[(moved//10)%4]} {moved*100//total}%")
            self.after(0,logv.set,
                f"{head}: {copied} kopiert, {ips} mit IPS, {skip} uebersprungen, {fail} Fehler"
                + (f" | {deferred} wegen RA-Drossel offen -> SD-Export erneut ausfuehren (Cache macht es schnell)" if deferred else ""))
        # Staging entfernen (sollte leer sein)
        try: os.rmdir(staging)
        except OSError: pass
        # Fenster: hat der Nutzer das X gedrueckt -> schliessen; sonst Ergebnis
        # stehen lassen und Knopf auf SCHLIESSEN setzen.
        def _finish():
            try:
                if getattr(win, "_closing", False):
                    win.destroy()
                elif start_btn is not None:
                    start_btn.config(state="normal", text=T("SCHLIESSEN"), command=win.destroy)
            except Exception:
                pass
        self.after(0, _finish)
        log_hint = ""
        if problems:
            try:
                lp = os.path.join(dst, "mega_raw_nicht_gepatcht.txt")
                with open(lp, "w", encoding="utf-8") as lf:
                    lf.write("Spiele MIT RetroAchievements-Set, die NICHT gepatcht wurden:\n")
                    lf.write("(alles andere ohne Set ist normal und hier absichtlich nicht gelistet)\n\n")
                    for nm, gr in problems:
                        lf.write(f"{nm}  —  {gr}\n")
                log_hint = f"\n\nListe der {len(problems)} Spiele mit Set, die NICHT gepatcht wurden:\n{lp}"
            except OSError:
                pass
        if do_ips and not cancelled:
            self.after(0, lambda: messagebox.showinfo("SD-Export",
                f"{ips} Spiele mit Achievements gepatcht"
                + (f", {skip} schon vorhanden (uebersprungen)" if skip else "")
                + "\n\nNicht gepatcht — alles normal, kein Fehler:\n"
                f"  {n_noset} in RA gelistet, aber (noch) ohne Achievements\n"
                f"  {n_unsupported} kein Set fuer genau diese ROM-Version\n"
                f"       (Beta / Proto / andere Region)\n"
                f"  {n_unknown} ROM von RA nicht erkannt (Bad Dump / Hack / Homebrew)\n\n"
                f"Set vorhanden, aber von der Engine (noch) nicht abgedeckt: {n_luecke}\n"
                f"Echte Fehler (meist kein Platz im ROM fuer den Stub): {fail}"
                + log_hint
                + (f"\n\n{deferred} wegen RA-Drossel offen -> SD-Export erneut ausfuehren "
                   f"(Cache macht es schnell)" if deferred else "")))

    def _toggle_mon(self):
        if self.monitoring:
            self.monitoring = False
            close_ed_link()
            self.mon_btn.config(text=T("MONITOR STARTEN"),fg=self.C["green"])
        else:
            if not self.ra_token:
                messagebox.showwarning(T("Hinweis"),T("Bitte zuerst einloggen."))
                return
            self.monitoring = True
            self.prev_ram = {}
            if self.game:
                # Unlock-Stand FRISCH vom Server (RA-seitige Resets zaehlen sofort)
                try:
                    owned_now = ra_unlocks(self.game["gameid"], self.user.get(), self.ra_token)
                    for a in self.game["achievements"]:
                        a["owned"] = a["id"] in owned_now
                    self.after(0, self._build_ac_list)
                except Exception as e:
                    self.after(0,self.log.set,f"WARNUNG: RA-Abgleich fehlgeschlagen ({e}) — Stand evtl. veraltet")
                for ac in self.game["achievements"]:
                    ac["triggered"] = bool(ac.get("owned"))
                    if ac.get("rt"): ac["rt"].reset()
                self._build_ac_list()
            self.mon_btn.config(text=T("MONITOR STOPPEN"),fg=self.C["red"])
            try:
                try:
                    if os.path.getsize(_p("recording.csv")) > 2_000_000:
                        os.replace(_p("recording.csv"), _p("recording_old.csv"))
                except OSError:
                    pass
                with open(_p("recording.csv"),"a",encoding="utf-8") as rec:
                    rec.write(f"# SESSION {time.strftime('%Y-%m-%d %H:%M:%S')} "
                              f"Spiel={self.game['name'] if self.game else '?'}\n")
                    if self.game and self.game.get("addr_map"):
                        addrs = ",".join(f"{a:06X}" for a in sorted(self.game["addr_map"]))
                        rec.write(f"# ADDRS,{addrs}\n")
            except Exception:
                pass
            ed = open_ed_link()
            if ed:
                mode = (f"{PORT} direkt + Frame-Sync" if _frame_sync_ok
                        else f"{PORT} direkt + Double-Read (IPS ohne Zaehler?)")
            else:
                mode = "langsam (edlink.exe-Fallback)"
            # --- Anti-Cheat Stufe 1: Stub-Rueckverifikation ---
            self.stub_verified = None
            if ed and self.game:
                ok_v, info_v = verify_stub_live(ed, self.game)
                self.stub_verified = ok_v
                if ok_v is True:
                    self.after(0,self.log.set, f"✓ {info_v}")
                elif ok_v is False:
                    self.after(0,self.log.set, f"⚠ ANTI-CHEAT: {info_v} — Awards gesperrt")
                # ok_v is None -> Verifikation nicht moeglich, Awards laufen (failsafe)
            stub_info = "Stub: ?"
            if ed:
                try:
                    g = ed.memrd(0x01080000 + 196, 2)[1]
                    if g == STUB_GEN:
                        stub_info = f"Stub-Gen {g:02X}"
                        self.after(0,self.bram_lbl.config,{"fg":self.C["cyan"]})
                    else:
                        stub_info = f"Stub: ALT ({g:02X}) — IPS NEU ERSTELLEN!"
                        self.after(0,self.bram_lbl.config,{"fg":self.C["red"]})
                except Exception:
                    pass
            self._mode = f"{mode} | {stub_info}"
            mode = self._mode
            self.bram_var.set(f"[{mode}]")
            self.log.set(f"Monitor läuft [{mode}] — Spiel wird automatisch erkannt...")
            threading.Thread(target=self._mon_loop,daemon=True).start()

    def _mon_loop(self):
        serial_checked = False
        while self.monitoring:
            try:
                data = read_bram()
                dump = " ".join(f"{data[i]:02X}" for i in range(1,min(24,len(data)),2))
                self.after(0,self.bram_var.set,f"[{getattr(self,'_mode','?')}]  [{dump}]")

                # Spiel-Auto-Erkennung: Stub (ab Gen 0x48) schreibt die RA-Game-ID
                # nach BRAM 203/205 — Tool laedt das Spiel ohne manuelle Auswahl.
                if len(data) > 205 and data[197] == STUB_GEN:
                    gid = data[203] | (data[205] << 8)
                    if gid and gid != getattr(self, "_auto_gid_tried", None) and \
                       (not self.game or self.game.get("gameid") != gid):
                        self._auto_gid_tried = gid
                        self.after(0,self.log.set,f"Spiel erkannt (RA-ID {gid}) — lade automatisch...")
                        self.after(0,self._auto_load_game, gid, f"#{gid}")

                if self.game and self.game.get("addr_map"):
                    # BRAM-Bytes zurück auf RAM-Adressen mappen
                    ram = {}
                    for ram_addr, bram_off in self.game["addr_map"].items():
                        ram[ram_addr] = data[bram_off] if bram_off < len(data) else 0
                    # Live-Region: $FFFFF8 Bit6 (1=PAL/50Hz, 0=NTSC/60Hz), wenn ueberwacht
                    rphys = racond.ra_byte_to_phys(0xfff9)
                    if rphys in ram:
                        is_pal = bool(ram[rphys] & 0x40)
                        if is_pal != getattr(self, "_console_pal", None):
                            self._console_pal = is_pal
                            self.after(0, self._build_ac_list)   # Markierungen neu faerben

                    if ram != self.prev_ram:
                        try:
                            with open(_p("recording.csv"),"a",encoding="utf-8") as rec:
                                vals = ",".join(f"{ram[a]:02X}" for a in sorted(ram))
                                rec.write(f"{time.time():.3f},{vals}\n")
                        except Exception:
                            pass
                    global _blind_gap
                    eval_frames = [ram]
                    if self.prev_ram and _blind_gap:
                        _blind_gap = False
                        changed = sorted(a for a in ram if ram[a] != self.prev_ram.get(a))
                        if 1 < len(changed) <= 16:
                            eval_frames = []
                            step = dict(self.prev_ram)
                            for a in changed:
                                step = dict(step); step[a] = ram[a]
                                eval_frames.append(step)
                            try:
                                with open(_p("recording.csv"),"a",encoding="utf-8") as rec:
                                    rec.write(f"# BLINDGAP {len(changed)} Bytes als Mikro-Schritte\n")
                            except Exception: pass
                    if self.prev_ram:
                        for ac in self.game["achievements"]:
                            if ac["triggered"] or ac.get("unsupported"): continue
                            prev_step = self.prev_ram
                            hit = False
                            for f in eval_frames:
                                if ac["rt"].update(f, prev_step):
                                    hit = True
                                prev_step = f
                            if hit:
                                if self.settings.get("tv_flash", True):
                                    threading.Thread(target=tv_flash,daemon=True).start()
                                ac["triggered"] = True
                                try:
                                    with open(_p("trigger_log.txt"),"a",encoding="utf-8") as tl:
                                        tl.write(f"\n[{time.strftime('%H:%M:%S')}] ★ {ac['title']} (ID {ac['id']})\n")
                                        tl.write(f"  MemAddr: {ac['mem']}\n")
                                        tl.write(f"  RAM jetzt:  {{" + ", ".join(f"{a:06X}:{v:02X}" for a,v in sorted(ram.items())) + "}}\n")
                                        tl.write(f"  RAM vorher: {{" + ", ".join(f"{a:06X}:{v:02X}" for a,v in sorted(self.prev_ram.items())) + "}}\n")
                                except Exception:
                                    pass
                                self.after(0,self._unlock,ac)
                    self.prev_ram = ram
                time.sleep(0.001 if (_ed_link and _frame_sync_ok) else (0.02 if _ed_link else 0.3))
            except Exception as e:
                # Verbindungsverlust (Kabel ab / EverDrive aus): Status klar anzeigen
                self.after(0, self.bram_var.set, "[DISCONNECT — Kabel/Verbindung pruefen]")
                self.after(0, self.bram_lbl.config, {"fg": self.C["red"]})
                self.after(0, self.log.set, f"Verbindung verloren: {e}")
                time.sleep(1)

    def _auto_load_game(self, gameid, name):
        """Lädt Spiel automatisch anhand erkannter Serial → RA Game-ID."""
        if not self.ra_token:
            self.log.set(T("Spiel erkannt, aber nicht eingeloggt"))
            return
        self.game = {"name":name,"gameid":gameid,"md5":"","achievements":[],"addr_map":{}}
        try:
            gc = json.load(open(_p("games_cache.json"),encoding="utf-8")).get(str(gameid))
        except Exception:
            gc = None
        if gc:
            self.game["md5"] = gc.get("md5","")
            self.hash_var.set(gc.get("md5","-"))
            if gc.get("rom") and os.path.exists(gc["rom"]):
                self.rom_path = gc["rom"]
            extra = f"   [{gc['compat']}]" if gc.get("compat") else ""
            self.rom_lbl.config(text=f"{gc.get('name','?')}  (auto-erkannt){extra}")
            if gc.get("compat_block"):
                self.compat_var.set(gc["compat_block"] + "\n(Stand: letzte IPS-Erstellung)")
                self.compat_lbl.config(fg=self.C["green"])
        else:
            self.rom_lbl.config(text=T("auto-erkannt — Achievements laufen automatisch (Zusatz-Infos erscheinen, sobald das Spiel einmal gepatcht wurde)"))
        threading.Thread(target=self._load_achievements, daemon=True).start()

    def _unlock(self, ac):
        # Reihenfolge: Award (wichtigstes) -> GUI -> Sound. Jede Stufe isoliert,
        # damit ein Fehler in einer Stufe die anderen nie mitreissen kann.
        # Anti-Cheat Stufe 1: Bei nachweislich fehlgeschlagener Stub-Verifikation
        # (fremde/manipulierte ROM) wird NICHT gebucht. None (nicht pruefbar) =
        # failsafe gebucht, damit echte Spieler nie faelschlich gesperrt werden.
        if getattr(self, "stub_verified", None) is False:
            self.after(0,self.log.set,
                f"⚠ {ac['title']} NICHT gebucht — Stub-Verifikation fehlgeschlagen")
            return
        if self.ra_token:
            def aw():
                ok = ra_award(ac["id"], self.user.get(), self.ra_token, hardcore=0)
                self.after(0,self.log.set,f"★ {ac['title']} — {'✓ RA gebucht' if ok else 'RA Fehler'}")
            threading.Thread(target=aw,daemon=True).start()
        try:
            lbl = self.ac_labels.get(ac["id"])
            if lbl: lbl.config(text=f"★  {ac['title']} ({ac['points']}p) — {ac['desc']}",fg=self.C["gold"])
            self.log.set(f"★ {ac['title']}")
        except Exception:
            pass
        try:
            threading.Thread(target=play_unlock_sound,daemon=True).start()
        except Exception:
            pass

    def report_callback_exception(self, exc, val, tb):
        """GUI-(Mainloop-)Fehler sichtbar machen statt still sterben."""
        import traceback
        txt = "".join(traceback.format_exception(exc, val, tb))
        try:
            with open(_p("error_log.txt"), "a", encoding="utf-8") as f:
                f.write(f"\n=== GUI {time.strftime('%H:%M:%S')} ===\n{txt}")
        except Exception:
            pass
        try:
            self.log.set(T("FEHLER — Details in error_log.txt"))
        except Exception:
            pass

def _thread_excepthook(args):
    """Jeder Fehler in Hintergrund-Threads landet sichtbar in error_log.txt."""
    import traceback
    txt = "".join(traceback.format_exception(args.exc_type, args.exc_value, args.exc_traceback))
    try:
        with open(_p("error_log.txt"), "a", encoding="utf-8") as f:
            f.write(f"\n=== {time.strftime('%H:%M:%S')} ===\n{txt}")
    except Exception:
        pass
    print(txt)

threading.excepthook = _thread_excepthook


# =============================================
def show_intro_html():
    """Zeigt mega_raw_intro.html (Canvas-Animation inkl. Miau-Bilder) in einem
    rahmenlosen Fenster via pywebview, schliesst nach Ablauf automatisch.
    Braucht 'pip install pywebview'. Fehlt pywebview oder die HTML, wird das
    Intro stillschweigend uebersprungen (Tool startet normal)."""
    html = _asset("mega_raw_intro.html")
    if not os.path.isfile(html):
        return
    try:
        import webview
    except Exception:
        return
    try:
        import threading
        with open(html, "r", encoding="utf-8") as f:
            html_content = f.read()

        # Steuerzustand zwischen JS-Signal, Sicherheits-Timer und Fenster
        state = {"win": None, "closed": False}

        def _close():
            if state["closed"]:
                return
            state["closed"] = True
            try:
                state["win"].destroy()
            except Exception:
                pass

        # Wird vom HTML aufgerufen, wenn die Animation komplett durch ist
        # (window.pywebview.api.intro_fertig()).
        class _Api:
            def intro_fertig(self):
                _close()

        win = webview.create_window(
            "", html=html_content,
            width=740, height=840, frameless=True,
            resizable=False, on_top=True,
            js_api=_Api()
        )
        state["win"] = win

        # Sicherheits-Notausstieg: Falls das JS-Signal nie kommt (z.B. JS-Fehler
        # oder sehr langsames WebView2), schliesst sich das Intro spaetestens
        # nach MAX_INTRO Sekunden von selbst. Bei normalem Lauf greift vorher
        # das intro_fertig()-Signal aus dem HTML.
        MAX_INTRO = 40.0
        def _watchdog():
            import time as _t
            _t.sleep(MAX_INTRO)
            _close()
        threading.Thread(target=_watchdog, daemon=True).start()

        # Festes WebView2-Profil statt eines temporaeren: dann muss pywebview
        # beim Schliessen nichts loeschen -> die harmlose, aber irritierende
        # "Failed to delete user data folder / lockfile"-Meldung entfaellt.
        # (private_mode=False + fester storage_path; Profil wird wiederverwendet.)
        wv_store = os.path.join(tempfile.gettempdir(), "mega_raw_webview")
        try:
            webview.start(private_mode=False, storage_path=wv_store)
        except TypeError:
            # Aeltere pywebview-Version ohne diese Parameter -> normaler Start
            try: webview.start()
            except Exception: pass
        except Exception:
            pass
    except Exception:
        pass
    # pywebview/WebView2 kann beim Aufraeumen Meldungen erzeugen, die das Tool
    # nicht betreffen. Sie werden hier bewusst ignoriert; das Tool startet danach
    # in jedem Fall normal weiter.


if __name__ == "__main__":
    # Intro-Modus: nur die Animation zeigen und beenden (eigener Prozess).
    if "--intro" in sys.argv:
        show_intro_html()
        sys.exit(0)
    # Das Intro (WebView2) initialisiert COM des Prozesses so, dass danach der
    # Windows-ORDNER-Dialog (askdirectory -> Ordner-Patch/SD-Export) haengt,
    # der Datei-Dialog aber nicht. Loesung: Intro in EIGENEM Prozess starten;
    # dann stirbt dieser COM-Zustand mit dem Intro und der Hauptprozess bleibt
    # sauber. Schlaegt der Start fehl, wird das Intro einfach uebersprungen.
    try:
        _flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        subprocess.run([sys.executable, os.path.abspath(__file__), "--intro"],
                       timeout=60, creationflags=_flags)
    except Exception:
        pass
    app = App()
    app.mainloop()
