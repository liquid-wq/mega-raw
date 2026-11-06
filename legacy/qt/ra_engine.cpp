#include "ra_engine.h"
#include <fstream>
#include <regex>
#include <cctype>
#include <map>
#include <algorithm>

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

struct SizeInfo { int nbytes; std::string special; bool big_endian; };

const std::map<std::string, SizeInfo>& size_table() {
    static const std::map<std::string, SizeInfo> t = {
        {"",  {2, "", false}},
        {"h", {1, "", false}},
        {"x", {4, "", false}},
        {"w", {3, "", false}},
        {"i", {2, "", true}},
        {"j", {3, "", true}},
        {"g", {4, "", true}},
        {"m", {1, "bit0", false}},
        {"n", {1, "bit1", false}},
        {"o", {1, "bit2", false}},
        {"p", {1, "bit3", false}},
        {"q", {1, "bit4", false}},
        {"r", {1, "bit5", false}},
        {"s", {1, "bit6", false}},
        {"t", {1, "bit7", false}},
        {"l", {1, "low4", false}},
        {"u", {1, "up4", false}},
        {"k", {1, "cnt", false}},
    };
    return t;
}

} // namespace

Operand parse_operand(const std::string& token_in) {
    std::string token = trim(token_in);
    Operand op;
    if (token.empty()) { op.valid = false; return op; }

    static const std::regex re(
        R"(^([dpb]?)(?:0x([hHxXwWiIjJgGmMnNoOpPqQrRsStTlLuUkK ]?)([0-9a-fA-F]+)|h([0-9a-fA-F]+)|(\d+(?:\.\d+)?)|f([0-9.]+))(?:\*([0-9.]+))?$)");
    std::smatch m;
    if (!std::regex_match(token, m, re)) { op.valid = false; return op; }

    char mod = 0;
    if (m[1].matched && !m[1].str().empty()) mod = m[1].str()[0];

    double mult = 1;
    if (m[7].matched && !m[7].str().empty()) mult = std::stod(m[7].str());

    if (m[4].matched) { // h1234 (const_hex)
        op.type = Operand::Type::Const;
        op.value = static_cast<double>(std::stoll(m[4].str(), nullptr, 16));
        op.mult = mult; op.mod = mod;
        return op;
    }
    if (m[5].matched) { // const_dec
        op.type = Operand::Type::Const;
        op.value = std::stod(m[5].str());
        op.mult = mult; op.mod = mod;
        return op;
    }
    if (m[6].matched) { // f... (const_f, nicht abbildbar)
        op.type = Operand::Type::Const;
        op.value = 0; op.mult = mult; op.mod = mod;
        op.unsupported = true;
        return op;
    }

    // mem: 0x-Adresse
    uint32_t addr = static_cast<uint32_t>(std::stoul(m[3].str(), nullptr, 16));
    std::string size_c = m[2].matched ? m[2].str() : "";
    std::string size_key;
    if (!size_c.empty() && size_c != " ") {
        size_key = size_c;
        size_key[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(size_key[0])));
    } // sonst: size_key bleibt "" ("" oder " " -> Default-Groesse 2)

    const auto& tbl = size_table();
    auto it = tbl.find(size_key);
    SizeInfo si = (it != tbl.end()) ? it->second : SizeInfo{1, "", false};

    op.type = Operand::Type::Mem;
    op.ra = addr;
    op.nbytes = si.nbytes;
    op.special = si.special;
    op.big_endian = si.big_endian;
    op.mod = mod;
    op.mult = mult;
    if (addr >= 0x10000) op.unsupported = true;
    return op;
}

namespace {
std::vector<std::string> split_S(const std::string& s) {
    // re.split(r'(?<!0x)S', memaddr): std::regex kann kein Lookbehind ->
    // manuell: 'S' trennt, ausser wenn direkt "0x" davor steht.
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == 'S') {
            bool preceded_by_0x = (i >= 2 && s[i - 2] == '0' && s[i - 1] == 'x');
            if (!preceded_by_0x) {
                parts.push_back(s.substr(start, i - start));
                start = i + 1;
            }
        }
    }
    parts.push_back(s.substr(start));
    return parts;
}

std::vector<std::string> split_char(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == sep) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(s.substr(start));
    return parts;
}

std::string trim2(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
} // namespace

Condition parse_condition(const std::string& cond_in) {
    std::string cond = cond_in;
    Condition c;

    static const std::regex flag_re(R"(^([A-Za-z]):(.*)$)");
    std::smatch fm;
    if (std::regex_match(cond, fm, flag_re)) {
        char flag = fm[1].str()[0];
        if (flag != 'Q') {
            c.flag = flag;
            c.has_flag = true;
        }
        cond = fm[2].str();
    }

    static const std::regex target_re(R"(\.(\d+)\.$)");
    std::smatch tm;
    if (std::regex_search(cond, tm, target_re)) {
        c.target = std::stoi(tm[1].str());
        cond = cond.substr(0, tm.position(0));
    }

    static const std::regex cmp_re(R"(!=|<=|>=|<|>|=)");
    std::smatch cm;
    if (std::regex_search(cond, cm, cmp_re)) {
        c.op = cm.str(0);
        c.left = parse_operand(cond.substr(0, cm.position(0)));
        c.right = parse_operand(cond.substr(cm.position(0) + cm.length(0)));
        c.has_right = true;
    } else {
        c.left = parse_operand(cond);
        c.has_right = false;
    }
    c.hits = 0;
    return c;
}

std::vector<std::vector<Condition>> parse_memaddr(const std::string& memaddr) {
    std::vector<std::vector<Condition>> groups;
    for (const auto& grp : split_S(memaddr)) {
        std::vector<Condition> conds;
        for (const auto& c_raw : split_char(grp, '_')) {
            std::string c = trim2(c_raw);
            if (!c.empty()) conds.push_back(parse_condition(c));
        }
        if (!conds.empty()) groups.push_back(std::move(conds));
    }
    return groups;
}

namespace {
// SUPPORTED_FLAGS = {None,'A','B','N','O','Z','P','R','T','M','C','D'}
bool flag_supported(bool has_flag, char flag) {
    if (!has_flag) return true;
    static const std::string ok = "ABNOZPRTMCD";
    return ok.find(flag) != std::string::npos;
}
} // namespace

bool is_unsupported(const std::vector<std::vector<Condition>>& groups) {
    if (groups.empty()) return true;
    for (const auto& g : groups) {
        for (const auto& c : g) {
            if (!flag_supported(c.has_flag, c.flag)) return true;
            if (!c.left.valid) return true;
            if (!c.op.empty() && !c.right.valid) return true;
            if (c.left.valid && c.left.unsupported) return true;
            if (c.has_right && c.right.valid && c.right.unsupported) return true;
        }
    }
    return false;
}

uint32_t ra_byte_to_phys(uint32_t ra_addr) {
    return 0xFF0000u + ((ra_addr & 0xFFFF) ^ 1);
}

std::vector<uint32_t> collect_addresses(const std::vector<std::string>& memaddrs) {
    std::vector<uint32_t> addrs_vec;
    std::unordered_map<uint32_t, bool> seen;
    for (const auto& mem : memaddrs) {
        for (const auto& grp : parse_memaddr(mem)) {
            for (const auto& c : grp) {
                const Operand* sides[2] = {&c.left, c.has_right ? &c.right : nullptr};
                for (const Operand* side : sides) {
                    if (side && side->valid && side->type == Operand::Type::Mem && !side->unsupported) {
                        for (int i = 0; i < side->nbytes; ++i) {
                            uint32_t phys = ra_byte_to_phys(side->ra + i);
                            if (!seen.count(phys)) { seen[phys] = true; addrs_vec.push_back(phys); }
                        }
                    }
                }
            }
        }
    }
    std::sort(addrs_vec.begin(), addrs_vec.end());
    return addrs_vec;
}

int64_t bcd_decode(int64_t v, int nbytes) {
    int64_t total = 0;
    for (int shift = (nbytes * 2 - 1) * 4; shift >= 0; shift -= 4) {
        total = total * 10 + ((v >> shift) & 0xF);
    }
    return total;
}

namespace {
int map_get(const RamMap& m, uint32_t addr) {
    auto it = m.find(addr);
    return it != m.end() ? it->second : 0;
}
int popcount8(int v) {
    v &= 0xFF;
    int c = 0;
    while (v) { c += v & 1; v >>= 1; }
    return c;
}
} // namespace

double read_operand(const Operand& op, const RamMap& ram, const RamMap& prev, const RamMap* prior) {
    if (op.type == Operand::Type::Const) {
        return op.value * op.mult;
    }
    const RamMap* src = &ram;
    if (op.mod == 'd') {
        src = &prev;
    } else if (op.mod == 'p') {
        src = (prior != nullptr) ? prior : &prev;
    }
    int64_t v = 0;
    if (op.big_endian) {
        for (int i = 0; i < op.nbytes; ++i)
            v = (v << 8) | map_get(*src, ra_byte_to_phys(op.ra + i));
    } else {
        for (int i = 0; i < op.nbytes; ++i)
            v |= static_cast<int64_t>(map_get(*src, ra_byte_to_phys(op.ra + i))) << (8 * i);
    }
    if (!op.special.empty()) {
        if (op.special.rfind("bit", 0) == 0) {
            int bitno = op.special[3] - '0';
            v = (v >> bitno) & 1;
        } else if (op.special == "low4") {
            v = v & 0xF;
        } else if (op.special == "up4") {
            v = (v >> 4) & 0xF;
        } else if (op.special == "cnt") {
            v = popcount8(static_cast<int>(v));
        }
    }
    if (op.mod == 'b') {
        v = bcd_decode(v, op.nbytes);
    }
    return static_cast<double>(v) * op.mult;
}

bool cmp_values(double a, const std::string& op, double b) {
    if (op == "=")  return a == b;
    if (op == "!=") return a != b;
    if (op == ">")  return a > b;
    if (op == ">=") return a >= b;
    if (op == "<")  return a < b;
    if (op == "<=") return a <= b;
    return false;
}

AchievementRuntime::AchievementRuntime(const std::string& memaddr) {
    groups_ = parse_memaddr(memaddr);
    unsupported_ = is_unsupported(groups_);
}

void AchievementRuntime::reset() {
    for (auto& g : groups_)
        for (auto& c : g)
            c.hits = 0;
}

namespace {
enum class ChainOp { None, And, Or };
}

WalkResult AchievementRuntime::walk(std::vector<Condition>& group, const RamMap& ram,
                                     const RamMap& prev, bool pause_pass) {
    double accum = 0;
    int hit_pool = 0;
    std::optional<bool> chain_val;
    ChainOp chain_op = ChainOp::None;
    bool reset_next = false;
    bool paused = false;
    bool satisfied = true;
    bool any_countable = false;
    bool reset = false;

    for (auto& c : group) {
        bool has_flag = c.has_flag;
        char flag = c.flag;
        double raw_val = read_operand(c.left, ram, prev, &prior_);

        if (has_flag && flag == 'A') { accum += raw_val; continue; }
        if (has_flag && flag == 'B') { accum -= raw_val; continue; }

        double lval = raw_val + accum;
        bool raw;
        if (!c.op.empty()) {
            raw = cmp_values(lval, c.op, read_operand(c.right, ram, prev, &prior_));
        } else {
            raw = true;
        }

        bool combined;
        if (!chain_val.has_value()) combined = raw;
        else if (chain_op == ChainOp::Or) combined = *chain_val || raw;
        else combined = *chain_val && raw;

        if (has_flag && flag == 'N') { chain_val = combined; chain_op = ChainOp::And; accum = 0; continue; }
        if (has_flag && flag == 'O') { chain_val = combined; chain_op = ChainOp::Or; accum = 0; continue; }
        if (has_flag && flag == 'Z') {
            if (combined) reset_next = true;
            chain_val.reset(); chain_op = ChainOp::None; accum = 0; continue;
        }
        if (has_flag && (flag == 'C' || flag == 'D')) {
            if (!pause_pass) {
                if (reset_next) c.hits = 0;
                if (combined && (c.target == 0 || c.hits < c.target)) c.hits += 1;
            }
            hit_pool += (flag == 'C') ? c.hits : -c.hits;
            reset_next = false;
            chain_val.reset(); chain_op = ChainOp::None; accum = 0;
            continue;
        }

        // reale Bedingung (kein Flag / P / R / T / M)
        accum = 0;
        chain_val.reset(); chain_op = ChainOp::None;

        bool is_pause = (has_flag && flag == 'P');
        bool do_hits = (pause_pass && is_pause) || (!pause_pass && !is_pause);

        bool sat;
        if (do_hits) {
            if (reset_next) c.hits = 0;
            if (c.target > 0) {
                if (combined && c.hits < c.target) c.hits += 1;
                sat = (c.hits + hit_pool) >= c.target;
            } else {
                sat = combined;
            }
        } else {
            if (c.target > 0) {
                sat = (c.hits + hit_pool) >= c.target;
            } else {
                sat = combined;
            }
        }
        hit_pool = 0;
        reset_next = false;

        if (is_pause) {
            if (pause_pass && sat) paused = true;
            continue;
        }
        if (pause_pass) continue;

        if (has_flag && flag == 'R') {
            if (sat) reset = true;
            continue;
        }

        any_countable = true;
        if (!sat) satisfied = false;
    }

    WalkResult res;
    if (pause_pass) {
        res.paused = paused;
    } else {
        res.satisfied = any_countable ? satisfied : true;
        res.reset = reset;
    }
    return res;
}

void AchievementRuntime::track_prior(const RamMap& ram) {
    for (const auto& kv : ram) {
        uint32_t addr = kv.first;
        int val = kv.second;
        auto it = last_.find(addr);
        if (it != last_.end() && it->second != val) {
            prior_[addr] = it->second;
        }
        last_[addr] = val;
    }
}

bool AchievementRuntime::update(const RamMap& ram, const std::optional<RamMap>& prev) {
    if (unsupported_ || !prev.has_value()) {
        track_prior(ram);
        return false;
    }
    track_prior(ram);

    std::vector<bool> paused;
    paused.reserve(groups_.size());
    for (auto& g : groups_) paused.push_back(walk(g, ram, *prev, true).paused);

    std::vector<bool> results;
    results.reserve(groups_.size());
    bool reset_all = false;
    for (size_t i = 0; i < groups_.size(); ++i) {
        if (paused[i]) { results.push_back(false); continue; }
        WalkResult r = walk(groups_[i], ram, *prev, false);
        results.push_back(r.satisfied);
        if (r.reset) reset_all = true;
    }

    if (reset_all) { reset(); return false; }

    bool core = results.empty() ? false : results[0];
    bool any_alt = false;
    for (size_t i = 1; i < results.size(); ++i) if (results[i]) any_alt = true;
    bool has_alts = results.size() > 1;
    return core && (!has_alts || any_alt);
}

bool evaluate_achievement(const std::string& memaddr, const RamMap& ram, const std::optional<RamMap>& prev) {
    AchievementRuntime rt(memaddr);
    bool prev_falsy = !prev.has_value() || prev->empty();
    RamMap actual_prev = prev_falsy ? ram : *prev;
    return rt.update(ram, actual_prev);
}

namespace {
std::string json_get_string(const Json& obj, const std::string& key, const std::string& def) {
    const Json* v = obj.find(key);
    if (!v || v->is_null() || v->type() != Json::Type::String) return def;
    return v->as_string();
}
long long json_get_int(const Json& obj, const std::string& key, long long def) {
    const Json* v = obj.find(key);
    if (!v || v->is_null() || v->type() != Json::Type::Int) return def;
    return v->as_int();
}
std::string to_lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
bool json_truthy(const std::optional<Json>& j) {
    if (!j.has_value() || j->is_null()) return false;
    if (j->type() == Json::Type::Object && j->entries().empty()) return false;
    if (j->type() == Json::Type::Array && j->items().empty()) return false;
    return true;
}
} // namespace

Game build_game_from_patch(long long gid, const std::string& md5, const std::optional<Json>& patch_opt) {
    Game g;
    if (!json_truthy(patch_opt)) return g; // valid=false -> Python None

    const Json& patch = *patch_opt;
    std::string title = json_get_string(patch, "Title", "#" + std::to_string(gid));

    if (to_lower(title).find("unsupported game version") != std::string::npos) {
        g.valid = true;
        g.name = title; g.gameid = gid; g.md5 = md5; g.no_set = true;
        g.raw_core = 0; g.n_unsupported = 0;
        return g;
    }

    std::vector<AchievementInfo> achs;
    long long raw_core = 0, n_unsupported = 0;
    const Json* achs_json = patch.find("Achievements");
    if (achs_json && achs_json->type() == Json::Type::Array) {
        for (const auto& ac : achs_json->items()) {
            long long flags = json_get_int(ac, "Flags", -1); // fehlt -> nie ==3
            long long id = json_get_int(ac, "ID", 0);
            if (flags != 3 || id >= 101000000) continue;
            raw_core += 1;
            std::string mem = json_get_string(ac, "MemAddr", "");
            auto rt = std::make_shared<AchievementRuntime>(mem);
            if (rt->unsupported()) {
                n_unsupported += 1;
                std::ofstream dbg("ra_debug.txt", std::ios::app);
                if (dbg) dbg << "UNSUPPORTED [" << id << "] "
                             << json_get_string(ac, "Title", "") << ": " << mem << "\n";
            }
            AchievementInfo ai;
            ai.id = id;
            ai.title = json_get_string(ac, "Title", "");
            ai.desc = json_get_string(ac, "Description", "");
            ai.points = static_cast<int>(json_get_int(ac, "Points", 0));
            ai.mem = mem;
            ai.badge = json_get_string(ac, "BadgeName", "");
            ai.triggered = false;
            ai.rt = rt;
            ai.unsupported = rt->unsupported();
            achs.push_back(std::move(ai));
        }
    }

    std::vector<std::string> mems;
    for (const auto& a : achs) mems.push_back(a.mem);
    auto phys = collect_addresses(mems);

    std::map<uint32_t, int> addr_map;
    std::vector<std::pair<uint32_t, int>> addr_list;
    int b = 1;
    for (uint32_t a : phys) {
        addr_map[a] = b;
        b += 2;
        addr_list.emplace_back(a, 1);
    }

    g.valid = true;
    g.name = title; g.gameid = gid; g.md5 = md5;
    g.achievements = std::move(achs);
    g.addr_map = std::move(addr_map);
    g.addr_list = std::move(addr_list);
    g.raw_core = raw_core;
    g.n_unsupported = n_unsupported;
    return g;
}
