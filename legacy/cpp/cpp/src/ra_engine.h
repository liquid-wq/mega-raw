#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <unordered_map>
#include <map>
#include <memory>
#include "json.h"


struct Operand {
    enum class Type { Const, Mem } type = Type::Const;
    double value = 0;      // fuer Const
    double mult = 1;       // Multiplikator
    char mod = 0;          // 'd' delta, 'p' prior, 'b' bcd, 0 = keiner
    uint32_t ra = 0;       // RA-Byte-Adresse (fuer Mem)
    int nbytes = 1;
    std::string special;   // "", "bit0".."bit7", "low4", "up4", "cnt"
    bool big_endian = false;
    bool unsupported = false;
    bool valid = true;     // false = Parserfehler (entspricht Python None)
};

// Parst einen einzelnen Operanden-Token (RA-Feld-Syntax).
Operand parse_operand(const std::string& token);

struct Condition {
    char flag = 0;              // 0 = kein Flag (Python None), sonst 'A','B','N',...
    bool has_flag = false;
    Operand left;
    std::string op;             // "", "!=", "<=", ">=", "<", ">", "="
    Operand right;
    bool has_right = false;
    int target = 0;
    int hits = 0;
};

Condition parse_condition(const std::string& cond);

// Bedingungen innerhalb einer Gruppe an '_' getrennt).
std::vector<std::vector<Condition>> parse_memaddr(const std::string& memaddr);

bool is_unsupported(const std::vector<std::vector<Condition>>& groups);

uint32_t ra_byte_to_phys(uint32_t ra_addr);

// Vereinfachte Signatur ggue. Python (dort: Liste von Achievement-Dicts) --
// nimmt direkt die MemAddr-Strings entgegen.
std::vector<uint32_t> collect_addresses(const std::vector<std::string>& memaddrs);

using RamMap = std::unordered_map<uint32_t, int>;

int64_t bcd_decode(int64_t v, int nbytes);

// prior kann nullptr sein (kein Prior-Tracking)
double read_operand(const Operand& op, const RamMap& ram, const RamMap& prev, const RamMap* prior);

bool cmp_values(double a, const std::string& op, double b);

struct WalkResult {
    bool paused = false;
    bool satisfied = true;
    bool reset = false;
};

class AchievementRuntime {
public:
    explicit AchievementRuntime(const std::string& memaddr);

    void reset();
    // ram: aktueller Frame, prev: leer (nullopt) = Python "prev is None"
    // (erster Frame, keine Auswertung, nur Prior-Tracking).
    bool update(const RamMap& ram, const std::optional<RamMap>& prev);

    bool unsupported() const { return unsupported_; }

private:
    std::vector<std::vector<Condition>> groups_;
    bool unsupported_;
    RamMap prior_;
    RamMap last_;

    WalkResult walk(std::vector<Condition>& group, const RamMap& ram, const RamMap& prev, bool pause_pass);
    void track_prior(const RamMap& ram);
};

// Stateless-Wrapper wie evaluate_achievement() in Python.
bool evaluate_achievement(const std::string& memaddr, const RamMap& ram, const std::optional<RamMap>& prev);

// --- build_game() Port (md_ra_tool.py) ---

struct AchievementInfo {
    long long id = 0;
    std::string title;
    std::string desc;
    int points = 0;
    std::string mem;
    std::string badge;
    bool triggered = false;
    bool owned = false;   // war schon vor Monitor-Start freigeschaltet (RA-Unlocks)
    std::shared_ptr<AchievementRuntime> rt;
    bool unsupported = false;
};

struct Game {
    std::string name;
    long long gameid = 0;
    std::string md5;
    bool no_set = false;
    bool valid = false; // false = Python "None" (kein Patch/keine Daten)
    std::vector<AchievementInfo> achievements;
    std::map<uint32_t, int> addr_map;                 // phys-Adresse -> BRAM-Offset (ungerade, 1,3,5,...)
    std::vector<std::pair<uint32_t, int>> addr_list;   // fuer build_stub: (phys-Adresse, nbytes=1)
    long long raw_core = 0;
    long long n_unsupported = 0;
};

// patch: bereits abgerufene PatchData (z.B. via RaClient::ra_patch). Reine
// Logik, kein Netzwerkzugriff
Game build_game_from_patch(long long gid, const std::string& md5, const std::optional<Json>& patch);





