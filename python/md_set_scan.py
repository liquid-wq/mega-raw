"""md_set_scan.py — Unterstuetzungs-Report ueber ALLE Mega-Drive-Sets auf RA.
Aufruf:  python md_set_scan.py BENUTZER PASSWORT
Zieht die komplette MD-Spieleliste, laedt jedes Set und prueft jede Bedingung
gegen die lokale Engine (ra_condition.py). Ergebnis: md_set_report.txt
Dauer: ~15-25 min (hoefliches Tempo gegenueber dem RA-Server).

(c) 2026 Liqui — privates, nicht-kommerzielles Projekt. Nutzung nur unter
Nennung des Urhebers, keine Veroeffentlichung unter fremdem Namen.
"""
import sys, time, json, collections
import urllib.request, urllib.parse
import ra_condition as rc

HOST = "retroachievements.org"

import os, urllib.error
_CACHE_FILE = "md_set_cache.json"
try:
    _cache = json.load(open(_CACHE_FILE, encoding="utf-8"))
except Exception:
    _cache = {}

def _cache_save():
    try:
        json.dump(_cache, open(_CACHE_FILE, "w", encoding="utf-8"))
    except Exception:
        pass

def req(params, cache_key=None):
    if cache_key and cache_key in _cache:
        return _cache[cache_key]
    url = f"https://{HOST}/dorequest.php?" + urllib.parse.urlencode(params)
    rq = urllib.request.Request(url, headers={"User-Agent": "MDRATool/1.0"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(rq, timeout=30) as r:
                data = json.loads(r.read().decode())
            if cache_key:
                _cache[cache_key] = data
            return data
        except urllib.error.HTTPError as e:
            if e.code == 429:
                ra = e.headers.get("Retry-After") if e.headers else None
                wait = int(ra) if (ra and str(ra).isdigit()) else (20 * (attempt + 1))
                print(f"    [429] RA drosselt — warte {wait}s ({attempt+1}/4)...")
                time.sleep(min(wait, 90)); continue
            raise
    raise RuntimeError("RA drosselt anhaltend — spaeter erneut starten (Cache bleibt erhalten)")

def main():
    user, pw = sys.argv[1], sys.argv[2]
    d = req({"r": "login2", "u": user, "p": pw})
    token = d.get("Token")
    if not token:
        print("Login fehlgeschlagen"); return
    print("Login OK — lade MD-Spieleliste...")
    # Konsole 1 = Mega Drive / Genesis
    games = req({"r": "gameslist", "c": 1, "u": user, "t": token}).get("Response", {})
    ids = sorted(set(int(g) for g in games))
    print(f"{len(ids)} Spiele mit Eintrag. Scanne Sets...")

    full, partial, none, empty, pal_locked = [], [], [], [], []
    feat = collections.Counter()
    for n, gid in enumerate(ids, 1):
        ckey = f"patch_{gid}"
        was_cached = ckey in _cache
        try:
            patch = req({"r": "patch", "g": gid, "u": user, "t": token}, cache_key=ckey).get("PatchData", {})
        except RuntimeError as e:
            print(e); _cache_save(); break
        except Exception:
            continue
        rp = (patch.get("RichPresencePatch") or "")
        if "pal" in rp.lower() or "50hz" in rp.lower():
            pal_locked.append((gid, patch.get("Title", str(gid))))
        achs = [a for a in patch.get("Achievements", []) if a.get("Flags") == 3]
        if not achs:
            empty.append(gid); continue
        uns = 0
        for a in achs:
            rt = rc.AchievementRuntime(a.get("MemAddr", ""))
            if rt.unsupported:
                uns += 1
                for f in ("I:", "K:", "G:", "f", "p0x"):
                    if f in a.get("MemAddr", ""):
                        feat[f] += 1
        title = patch.get("Title", str(gid))
        if uns == 0: full.append((gid, title, len(achs)))
        elif uns < len(achs): partial.append((gid, title, len(achs)-uns, len(achs)))
        else: none.append((gid, title, len(achs)))
        if n % 50 == 0:
            print(f"  {n}/{len(ids)}  (voll {len(full)} | teil {len(partial)} | keins {len(none)})")
            _cache_save()
        if not was_cached:
            time.sleep(0.4)
    _cache_save()

    with open("md_set_report.txt", "w", encoding="utf-8") as f:
        tot = len(full)+len(partial)+len(none)
        f.write(f"Mega-Drive-Sets gesamt: {tot} (+{len(empty)} ohne Achievements)\n")
        f.write(f"VOLL unterstuetzt:    {len(full)} ({100*len(full)//max(tot,1)}%)\n")
        f.write(f"TEILWEISE:            {len(partial)}\n")
        f.write(f"NICHT unterstuetzt:   {len(none)}\n")
        f.write(f"\nFehlende Konstrukte (Haeufigkeit): {dict(feat)}\n")
        f.write(f"\nPAL/50Hz in Rich Presence erwaehnt (mutmassliche Sperre/Behandlung): {len(pal_locked)}\n")
        for gid, t in pal_locked: f.write(f"  [{gid}] {t}\n")
        f.write("\n--- VOLL ---\n")
        for gid, t, n in full: f.write(f"  [{gid}] {t} ({n})\n")
        f.write("\n--- TEILWEISE ---\n")
        for gid, t, ok, n in partial: f.write(f"  [{gid}] {t} ({ok}/{n})\n")
        f.write("\n--- KEINS ---\n")
        for gid, t, n in none: f.write(f"  [{gid}] {t} ({n})\n")
    print("Fertig -> md_set_report.txt")

if __name__ == "__main__":
    main()
