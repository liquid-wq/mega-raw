#include "rom_patch.h"
#include "stub.h"
#include <cctype>
#include <algorithm>
#include <cstdio>
#include <unordered_map>

const std::unordered_map<std::string, HookEntry>& hook_db() {
    static const std::unordered_map<std::string, HookEntry> db = {
        {"1bc674be034e43c96b86487ac69d9293", {0x068156, "Sonic The Hedgehog"}},
        {"04d2c7da9aac3fbeadf6bb8ccd27b560", {0x0329AA, "Golden Axe"}},
        {"5a8f7c6437d239690b4a15287d841c26", {0x0FBF20, "Shinobi III"}},
        {"9f54481697efc9e6b96fcd054883ae70", {0x1FFB80, "Aaahh!!! Real Monsters (USA)"}},
        {"dce9b99a3d692cb4463f9f35d278659b", {0x1FBB40, "Road Rash 3 (USA, Europe)"}},
    };
    return db;
}

namespace {
std::unordered_map<std::string, HookEntry> g_remote_hooks; // leer bis explizit geladen

// Minimaler, abhaengigkeitsfreier JSON-Parser NUR fuer das feste hooks.json-Schema:
// {"version":N,"hooks":[{"md5":"...","addr":"0x...","name":"..."}, ...]}
// Bewusst kein QJson/nlohmann, um rom_patch.cpp frei von zusaetzlichen Abhaengigkeiten
// zu halten. Bei irgendeiner Abweichung vom erwarteten Format -> Abbruch mit Fehler,
// nichts wird teilweise uebernommen.
std::string json_extract_string(const std::string& obj, const std::string& key, bool& ok) {
    std::string pat = "\"" + key + "\"";
    size_t p = obj.find(pat);
    if (p == std::string::npos) { ok = false; return ""; }
    p = obj.find(':', p + pat.size());
    if (p == std::string::npos) { ok = false; return ""; }
    p = obj.find('"', p);
    if (p == std::string::npos) { ok = false; return ""; }
    size_t end = obj.find('"', p + 1);
    if (end == std::string::npos) { ok = false; return ""; }
    ok = true;
    return obj.substr(p + 1, end - p - 1);
}

bool parse_hooks_json(const std::string& text, std::unordered_map<std::string, HookEntry>& out) {
    size_t arr_key = text.find("\"hooks\"");
    if (arr_key == std::string::npos) return false;
    size_t arr_start = text.find('[', arr_key);
    if (arr_start == std::string::npos) return false;
    size_t arr_end = text.find(']', arr_start);
    if (arr_end == std::string::npos) return false;

    std::unordered_map<std::string, HookEntry> parsed;
    size_t pos = arr_start + 1;
    while (pos < arr_end) {
        size_t obj_start = text.find('{', pos);
        if (obj_start == std::string::npos || obj_start > arr_end) break;
        size_t obj_end = text.find('}', obj_start);
        if (obj_end == std::string::npos || obj_end > arr_end) break;
        std::string obj = text.substr(obj_start, obj_end - obj_start + 1);

        bool ok_md5 = false, ok_addr = false, ok_name = false;
        std::string md5 = json_extract_string(obj, "md5", ok_md5);
        std::string addr_str = json_extract_string(obj, "addr", ok_addr);
        std::string name = json_extract_string(obj, "name", ok_name);
        if (!ok_md5 || !ok_addr || md5.empty() || addr_str.empty()) return false; // strikt: kein Teilerfolg

        uint32_t addr = 0;
        try {
            addr = static_cast<uint32_t>(std::stoul(addr_str, nullptr, 16));
        } catch (...) {
            return false;
        }
        parsed[md5] = HookEntry{addr, ok_name ? name : md5};
        pos = obj_end + 1;
    }
    out = std::move(parsed);
    return true;
}
} // namespace

const HookEntry* find_hook(const std::string& md5) {
    auto rit = g_remote_hooks.find(md5);
    if (rit != g_remote_hooks.end()) return &rit->second;
    auto& db = hook_db();
    auto it = db.find(md5);
    if (it != db.end()) return &it->second;
    return nullptr;
}

int load_remote_hooks_from_json(const std::string& json_text) {
    std::unordered_map<std::string, HookEntry> parsed;
    if (!parse_hooks_json(json_text, parsed)) return -1;
    g_remote_hooks = std::move(parsed);
    return static_cast<int>(g_remote_hooks.size());
}

bool save_remote_hooks_cache(const std::string& cache_path, const std::string& json_text) {
    std::FILE* f = std::fopen(cache_path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(json_text.data(), 1, json_text.size(), f) == json_text.size();
    std::fclose(f);
    return ok;
}

int load_remote_hooks_cache(const std::string& cache_path) {
    std::FILE* f = std::fopen(cache_path.c_str(), "rb");
    if (!f) return 0; // kein Cache vorhanden - kein Fehler
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    return load_remote_hooks_from_json(text);
}

std::vector<std::string> remote_hook_names() {
    std::vector<std::string> names;
    for (auto& kv : g_remote_hooks) names.push_back(kv.second.name);
    return names;
}

namespace {
bool read_u32_be(const std::vector<uint8_t>& data, size_t off, uint32_t& out) {
    if (off + 4 > data.size()) return false;
    out = (static_cast<uint32_t>(data[off]) << 24) | (static_cast<uint32_t>(data[off+1]) << 16) |
          (static_cast<uint32_t>(data[off+2]) << 8) | static_cast<uint32_t>(data[off+3]);
    return true;
}
bool read_u16_be(const std::vector<uint8_t>& data, size_t off, uint16_t& out) {
    if (off + 2 > data.size()) return false;
    out = static_cast<uint16_t>((data[off] << 8) | data[off+1]);
    return true;
}
void write_u32_be(std::vector<uint8_t>& data, size_t off, uint32_t v) {
    data[off] = (v >> 24) & 0xFF; data[off+1] = (v >> 16) & 0xFF;
    data[off+2] = (v >> 8) & 0xFF; data[off+3] = v & 0xFF;
}
void write_u16_be(std::vector<uint8_t>& data, size_t off, uint16_t v) {
    data[off] = (v >> 8) & 0xFF; data[off+1] = v & 0xFF;
}
} // namespace

uint32_t rom_end_from_header(const std::vector<uint8_t>& data) {
    uint32_t end = 0;
    read_u32_be(data, 0x1A4, end);
    if (end == 0 || end >= data.size()) {
        end = data.empty() ? 0 : static_cast<uint32_t>(data.size() - 1);
    }
    return end;
}

bool uses_banking(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return false;
    for (size_t i = 0; i + 3 < data.size(); ++i) {
        if (data[i] == 0x00 && data[i+1] == 0xA1 && data[i+2] == 0x30 &&
            data[i+3] >= 0xF3 && data[i+3] <= 0xFF) {
            return true;
        }
    }
    return false;
}

StubAddrResult find_stub_addr(const std::vector<uint8_t>& data, size_t needed) {
    bool banking = uses_banking(data);
    size_t ceiling = banking ? 0x200000u : 0x400000u;

    // Schritt 1: hinter Dateiende anhängen (ausserhalb jeder Checksum)
    // Ausnahme: exakt 2MB-ROMs - Stub bei 0x200000 fuehrt auf EverDrive/Hardware
    // reproduzierbar zu Crash (PC verdoppelt bei 0x400000, LINE-1111-Trap).
    // Direkt zu Schritt 3 (interne Luecke).
    bool is_2mb = (data.size() == 0x200000);
    if (!is_2mb) {
        size_t addr = (data.size() + 0xF) & ~static_cast<size_t>(0xF);
        if (addr + needed <= ceiling) {
            return {static_cast<int64_t>(addr), true};
        }
    }

    // Schritt 2: Padding am Dateiende (0x00/0xFF)
    size_t j = data.size();
    while (j > 0 && (data[j-1] == 0x00 || data[j-1] == 0xFF)) j--;
    size_t pad_start = (j + 0xF) & ~static_cast<size_t>(0xF);
    uint32_t rom_end2 = rom_end_from_header(data);
    if (data.size() >= pad_start && data.size() - pad_start >= needed && pad_start + needed <= ceiling) {
        return {static_cast<int64_t>(pad_start), pad_start > rom_end2};
    }

    // Schritt 3: größte interne 0x00/0xFF-Lücke
    size_t best_start = 0, best_len = 0;
    size_t i = 0;
    while (i < data.size()) {
        uint8_t v = data[i];
        if (v == 0x00 || v == 0xFF) {
            size_t start = i;
            while (i < data.size() && data[i] == v) i++;
            size_t len = i - start;
            if (len >= 512 && len > best_len) { best_start = start; best_len = len; }
        } else {
            i++;
        }
    }
    if (best_len > 0) {
        size_t a = (best_start + 0x1F) & ~static_cast<size_t>(0xF);
        size_t best_end = best_start + best_len;
        if (best_end >= a && best_end - a >= needed + 16 && a + needed <= ceiling) {
            return {static_cast<int64_t>(a), false};
        }
    }
    return {-1, false};
}

std::string stub_failure_reason(const std::vector<uint8_t>& data, size_t needed) {
    (void)needed;
    if (data.size() >= 0x400000) {
        return "4MB-Vollausbau (Maximalgroesse des Mega Drive) ohne freien Platz "
               "und ohne Banking - Adressraum erschoepft, technische Grenze";
    }
    if (uses_banking(data)) {
        return "Banking-Spiel, kein Platz unter 2MB";
    }
    return "kein ausreichender freier Bereich in der ROM";
}

BootRisk boot_risk(const std::vector<uint8_t>& rom_data, int64_t stub_addr, bool outside, bool did_checksum) {
    std::vector<std::string> gruende;
    std::string stufe = "ok";

    bool has_own_check = false;
    for (size_t i = 0; i + 3 < rom_data.size(); ++i) {
        if (rom_data[i] == 0x00 && rom_data[i+1] == 0x00 && rom_data[i+2] == 0x01 && rom_data[i+3] == 0x8E) {
            has_own_check = true; break;
        }
    }
    uint16_t stored = 0;
    if (rom_data.size() > 0x190) read_u16_be(rom_data, 0x18E, stored);
    uint32_t std_sum = calc_checksum(rom_data, static_cast<int64_t>(rom_end_from_header(rom_data)));
    bool nonstandard_sum = (stored != std_sum);

    if (has_own_check && nonstandard_sum && !outside) {
        gruende.push_back("Eigene Checksum-Routine erkannt + Stub im ROM-Bereich "
                           "(Headdy-Klasse) - Boot unsicher");
        stufe = "hoch";
    } else if (has_own_check && nonstandard_sum) {
        gruende.push_back("Eigene Checksum-Routine, Stub aber ausserhalb (angehaengt) "
                           "- wahrscheinlich ok");
    }

    bool banking = uses_banking(rom_data);
    if (banking && stub_addr >= 0 && stub_addr >= 0x1F0000) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "Banking-Spiel, Stub bei 0x%06llX (nahe 2MB) - in Tests unkritisch, zur Sicherheit pruefen",
            static_cast<unsigned long long>(stub_addr));
        gruende.push_back(buf);
        if (stufe == "ok") stufe = "pruefen";
    }

    uint32_t vb = 0;
    if (rom_data.size() > 0x7C) read_u32_be(rom_data, 0x78, vb);
    if (vb < 0x200 || vb > rom_data.size()) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "VBlank-Vektor 0x%06X ausserhalb des Codebereichs - Hook-Annahme unsicher", vb);
        gruende.push_back(buf);
        if (stufe == "ok") stufe = "pruefen";
    }

    if (did_checksum && has_own_check && nonstandard_sum) {
        gruende.push_back("Checksum wurde korrigiert, obwohl Spiel eigene Pruefung "
                           "nutzt - moeglicher Konflikt");
        if (stufe == "ok") stufe = "pruefen";
    }

    if (gruende.empty()) {
        gruende.push_back("Keine bekannten Crash-Indikatoren - Stub angehaengt/sicher platziert");
    }
    return {stufe, gruende};
}

PatchResult patch_rom(const std::vector<uint8_t>& rom_data_in, const std::string& rom_path,
                       const Game& game, bool use_slim) {
    PatchResult res;
    if (game.addr_list.empty()) {
        res.ok = false; res.msg = "kein Set / keine Achievements";
        return res;
    }

    std::vector<uint8_t> orig = rom_data_in;
    uint32_t orig_vblank = 0;
    read_u32_be(orig, 0x78, orig_vblank);

    size_t sum_n = 0;
    for (auto& p : game.addr_list) sum_n += static_cast<size_t>(p.second);
    size_t needed = 60 + 10 * sum_n + 100;

    if (use_slim) {
        auto probe = build_stub_slim(0x200000, orig_vblank, game.addr_list,
                                     static_cast<uint32_t>(game.gameid));
        needed = std::max(needed, probe.size() + 32);
    }

    uint32_t rom_end = rom_end_from_header(orig);

    int64_t stub_addr; bool outside;
    const HookEntry* hit = find_hook(game.md5);
    if (hit && hit->stub != 0) {
        stub_addr = hit->stub; outside = false;
    } else {
        auto r = find_stub_addr(orig, needed);
        stub_addr = r.stub_addr; outside = r.outside;
    }
    if (stub_addr < 0) {
        res.ok = false; res.msg = "kein Platz fuer Stub";
        return res;
    }

    std::vector<uint8_t> stub = use_slim
        ? build_stub_slim(static_cast<uint32_t>(stub_addr), orig_vblank,
                          game.addr_list, static_cast<uint32_t>(game.gameid))
        : build_stub(static_cast<uint32_t>(stub_addr), orig_vblank,
                     game.addr_list, static_cast<uint32_t>(game.gameid));

    std::vector<uint8_t> stub_addr_bytes(4);
    write_u32_be(stub_addr_bytes, 0, static_cast<uint32_t>(stub_addr));
    std::vector<IpsPatch> patches = {
        {0x78, stub_addr_bytes},
        {static_cast<uint32_t>(stub_addr), stub},
    };

    bool did_check = false;
    if (!outside) {
        uint16_t stored = 0;
        read_u16_be(orig, 0x18E, stored);
        uint32_t cs_orig = calc_checksum(orig, static_cast<int64_t>(rom_end));
        if (stored == cs_orig) {
            size_t patched_len = std::max(orig.size(), static_cast<size_t>(stub_addr) + stub.size());
            std::vector<uint8_t> patched(patched_len, 0);
            std::copy(orig.begin(), orig.end(), patched.begin());
            std::copy(stub.begin(), stub.end(), patched.begin() + stub_addr);
            write_u32_be(patched, 0x78, static_cast<uint32_t>(stub_addr));
            uint32_t cs_patched = calc_checksum(patched, static_cast<int64_t>(rom_end));
            std::vector<uint8_t> csb(2);
            write_u16_be(csb, 0, static_cast<uint16_t>(cs_patched));
            patches.insert(patches.begin(), {0x018E, csb});
            did_check = true;
        }
    }

    res.ips_bytes = make_ips(patches);
    std::string base = rom_path;
    auto dot = base.find_last_of('.');
    auto slash = base.find_last_of("/\\");
    std::string ips_path;
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        ips_path = base.substr(0, dot) + ".ips";
    else
        ips_path = base + ".ips";
    res.ips_path = ips_path;
    res.stub_addr = static_cast<uint32_t>(stub_addr);
    res.orig_vblank = orig_vblank;
    res.outside = outside;

    res.risk = boot_risk(orig, stub_addr, outside, did_check);
    res.has_risk = true;
    res.ok = true;
    char msgbuf[160];
    std::snprintf(msgbuf, sizeof(msgbuf), "IPS ok (Stub %zuB @0x%06X)", stub.size(), res.stub_addr);
    res.msg = msgbuf;
    return res;
}

namespace {
std::string basename_of(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}
std::string splitext_stem(const std::string& base) {
    auto pos = base.find_last_of('.');
    if (pos == std::string::npos || pos == 0) return base;
    return base.substr(0, pos);
}
std::string to_upper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}
} // namespace

int entry_count(const std::vector<PlacedItem>& items) {
    int n = 0;
    for (auto& it : items) n += it.second ? 2 : 1;
    return n;
}

std::string key2(const std::string& rom, int n) {
    std::string s = to_upper(splitext_stem(basename_of(rom)));
    std::string alnum;
    for (char c : s) if (std::isalnum(static_cast<unsigned char>(c))) alnum.push_back(c);
    if (alnum.empty()) alnum = "0";
    if (static_cast<int>(alnum.size()) > n) alnum = alnum.substr(0, n);
    return alnum;
}

std::string letter_of(const std::string& rom) {
    std::string base = basename_of(rom);
    if (base.empty()) return "0-9";
    char c = static_cast<char>(std::toupper(static_cast<unsigned char>(base[0])));
    return std::isalpha(static_cast<unsigned char>(c)) ? std::string(1, c) : "0-9";
}

std::vector<std::pair<std::string, std::vector<PlacedItem>>> subsplit(
    const std::vector<PlacedItem>& items, int limit) {
    std::vector<std::vector<PlacedItem>> chunks;
    std::vector<PlacedItem> cur;
    int n = 0;
    for (auto& it : items) {
        int e = it.second ? 2 : 1;
        if (!cur.empty() && n + e > limit) { chunks.push_back(cur); cur.clear(); n = 0; }
        cur.push_back(it); n += e;
    }
    if (!cur.empty()) chunks.push_back(cur);

    std::vector<std::pair<std::string, std::vector<PlacedItem>>> out;
    for (auto& ch : chunks) {
        std::string lo = key2(ch.front().first);
        std::string hi = key2(ch.back().first);
        std::string name = (lo == hi) ? lo : (lo + "-" + hi);
        out.emplace_back(name, ch);
    }
    return out;
}

std::vector<std::pair<std::string, std::vector<PlacedItem>>> letter_groups(
    const std::vector<PlacedItem>& placed, int limit) {
    std::vector<std::string> letter_order;
    std::unordered_map<std::string, std::vector<PlacedItem>> buckets;
    for (auto& p : placed) {
        std::string L = letter_of(p.first);
        if (!buckets.count(L)) letter_order.push_back(L);
        buckets[L].push_back(p);
    }

    std::vector<std::pair<std::string, std::vector<PlacedItem>>> groups;
    std::vector<PlacedItem> cur_items;
    std::vector<std::string> cur_letters;
    int cur_n = 0;

    auto flush = [&]() {
        if (!cur_items.empty()) {
            std::string name = (cur_letters.size() == 1) ? cur_letters.front()
                                : (cur_letters.front() + "-" + cur_letters.back());
            groups.emplace_back(name, cur_items);
            cur_items.clear(); cur_letters.clear(); cur_n = 0;
        }
    };

    for (auto& L : letter_order) {
        auto& items = buckets[L];
        int n = entry_count(items);
        if (L == "0-9") {
            flush();
            if (n > limit) {
                auto sub = subsplit(items, limit);
                for (auto& s : sub) groups.push_back(s);
            } else {
                groups.emplace_back("0-9", items);
            }
            continue;
        }
        if (n > limit) {
            flush();
            auto sub = subsplit(items, limit);
            for (auto& s : sub) groups.push_back(s);
            continue;
        }
        if (!cur_items.empty() && cur_n + n > limit) flush();
        cur_items.insert(cur_items.end(), items.begin(), items.end());
        cur_letters.push_back(L);
        cur_n += n;
    }
    flush();

    // Namen eindeutig machen
    std::unordered_map<std::string, int> seen;
    std::vector<std::pair<std::string, std::vector<PlacedItem>>> final_groups;
    for (auto& kv : groups) {
        std::string name = kv.first;
        if (seen.count(name)) {
            seen[name] += 1;
            name = name + "_" + std::to_string(seen[name]);
        } else {
            seen[name] = 0;
        }
        final_groups.emplace_back(name, kv.second);
    }
    return final_groups;
}

std::string serial_from_bram(const std::vector<uint8_t>& data) {
    std::string out;
    size_t off = 0x31;
    for (int i = 0; i < 8; ++i) {
        if (off < data.size()) {
            uint8_t c = data[off];
            if (c >= 32 && c < 127) out.push_back(static_cast<char>(c));
        }
        off += 2;
    }
    return out;
}
