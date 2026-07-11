#pragma once
#include "ra_engine.h"
#include "ra_cache.h"
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <utility>

// HOOK_DB: bekannte MD5 -> fester Stub-Offset (spart Scan). 1:1 aus md_ra_tool.py.
struct HookEntry { uint32_t stub; std::string name; };
const std::unordered_map<std::string, HookEntry>& hook_db();

// --- Remote Hook-Updates (per Knopfdruck, GitHub Pages) ---
// Erlaubt es, nach einem Release verifizierte Hash-Hooks (z.B. fuer Boot-Probleme bei
// einzelnen 2MB-Spielen) nachzuliefern, ohne dass Nutzer ein neues Programm-Release
// brauchen. Die eingebaute hook_db() bleibt IMMER unveraendert als Fallback bestehen -
// diese Funktionen aendern nichts an bestehendem Verhalten, solange sie nicht
// aktiv (Knopfdruck) aufgerufen werden.
//
// Aufrufreihenfolge im GUI:
//  1) Beim Start optional load_remote_hooks_cache(pfad) - liest zuletzt bekannten Stand
//     von Platte, KEIN Netzwerk.
//  2) Bei Knopfdruck "Hook-Liste aktualisieren": GUI laedt hooks.json per
//     QNetworkAccessManager von https://liquid-wq.github.io/support/hooks.json,
//     ruft dann load_remote_hooks_from_json(text) und danach
//     save_remote_hooks_cache(pfad, text) auf.
//
// JSON-Format:
// {"version":1,"hooks":[{"md5":"...", "addr":"0x1FE210", "name":"Aero the Acro-Bat 2 (Europe)"}]}

// Sucht zuerst in den geladenen Remote-Hooks, dann in der eingebauten hook_db().
// Rueckgabe nullptr, wenn kein Hook fuer diese MD5 bekannt ist.
const HookEntry* find_hook(const std::string& md5);

// Parsed JSON-Text und ersetzt die aktuell geladene Remote-Hook-Liste im Speicher.
// Rueckgabe: Anzahl geladener Eintraege, oder -1 bei Parse-Fehler (dann bleibt die
// bisherige Remote-Liste unveraendert erhalten).
int load_remote_hooks_from_json(const std::string& json_text);

// Schreibt json_text unveraendert als lokalen Cache auf Platte (z.B. neben der exe).
bool save_remote_hooks_cache(const std::string& cache_path, const std::string& json_text);

// Laedt zuvor gecachten JSON-Text von Platte (falls vorhanden) - ohne Netzwerk.
// Rueckgabe: Anzahl geladener Eintraege, 0 wenn keine Cache-Datei existiert, -1 bei Parse-Fehler.
int load_remote_hooks_cache(const std::string& cache_path);

// Namen aller aktuell geladenen Remote-Hooks (nach load_remote_hooks_from_json/
// load_remote_hooks_cache) - fuer Anzeige im GUI, welche Spiele aktualisiert wurden.
std::vector<std::string> remote_hook_names();

// Offizielles ROM-Ende laut Header (0x1A4).
uint32_t rom_end_from_header(const std::vector<uint8_t>& data);

// Nutzt das Spiel SSF-Bank-Register (0xA130F3..0xA130FF)?
bool uses_banking(const std::vector<uint8_t>& data);

// Rueckgabe: (stub_addr, outside_checksum). stub_addr = -1 -> kein Platz (Python None).
struct StubAddrResult { int64_t stub_addr; bool outside; };
StubAddrResult find_stub_addr(const std::vector<uint8_t>& data, size_t needed);

std::string stub_failure_reason(const std::vector<uint8_t>& data, size_t needed);

struct BootRisk { std::string level; std::vector<std::string> reasons; }; // level: "ok"/"pruefen"/"hoch"
BootRisk boot_risk(const std::vector<uint8_t>& rom_data, int64_t stub_addr, bool outside, bool did_checksum);

struct PatchResult {
    bool ok;
    std::string msg;
    bool has_risk = false;
    BootRisk risk;
    std::vector<uint8_t> ips_bytes;       // vom Aufrufer auf Platte zu schreiben
    std::string ips_path;                 // vorgeschlagener Pfad (rom_path minus Ext + .ips)
    uint32_t stub_addr = 0;
    uint32_t orig_vblank = 0;
    bool outside = false;
};

// rom_data wird als bereits eingelesener Byte-Puffer uebergeben, IPS-Bytes
// werden zurueckgegeben statt geschrieben (Aufrufer entscheidet ueber I/O).
PatchResult patch_rom(const std::vector<uint8_t>& rom_data, const std::string& rom_path,
                       const Game& game, bool use_slim = false);

// --- SD-Export-Ordnergruppierung (_letter_groups etc.) ---
constexpr int EVERDRIVE_FOLDER_LIMIT = 800;

// item: (rom_path, hat_ips)
using PlacedItem = std::pair<std::string, bool>;

int entry_count(const std::vector<PlacedItem>& items);
std::string key2(const std::string& rom, int n = 2);
std::string letter_of(const std::string& rom);
std::vector<std::pair<std::string, std::vector<PlacedItem>>> subsplit(
    const std::vector<PlacedItem>& items, int limit);
std::vector<std::pair<std::string, std::vector<PlacedItem>>> letter_groups(
    const std::vector<PlacedItem>& placed, int limit = EVERDRIVE_FOLDER_LIMIT);

std::string serial_from_bram(const std::vector<uint8_t>& data);


