#include "ra_client.h"

RaClient::RaClient(RaCache& cache, RequestFn request_fn)
    : cache_(cache), request_(std::move(request_fn)) {}

namespace {
std::optional<long long> falsy_or_none(const Json* v) {
    if (!v || v->is_null()) return std::nullopt;
    if (v->type() == Json::Type::Int && v->as_int() == 0) return std::nullopt;
    if (v->type() == Json::Type::Int) return v->as_int();
    return std::nullopt; // unerwarteter Typ ->
}
}

std::optional<long long> RaClient::ra_gameid(const std::string& md5, bool raise_limit) {
    const Json* cached = cache_.gameid_get(md5);
    if (cached) {
        return falsy_or_none(cached);
    }
    try {
        Json d = request_({{"r", "gameid"}, {"m", md5}});
        const Json* gid_raw = d.find("GameID");
        std::optional<long long> gid = falsy_or_none(gid_raw);
        cache_.gameid_set(md5, gid ? Json::integer(*gid) : Json::null());
        cache_.save(false);
        return gid;
    } catch (const RateLimited&) {
        if (raise_limit) throw;
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> RaClient::ra_login(const std::string& user, const std::string& pw) {
    Json d = request_({{"r", "login2"}, {"u", user}, {"p", pw}});
    const Json* success = d.find("Success");
    bool ok = success && success->type() == Json::Type::Bool && success->as_bool();
    if (!ok) return std::nullopt;
    const Json* token = d.find("Token");
    if (!token || token->type() != Json::Type::String) return std::nullopt;
    return token->as_string();
}

std::optional<Json> RaClient::ra_patch(long long gameid, const std::string& user, const std::string& token) {
    std::string key = std::to_string(gameid);
    const Json* cached = cache_.patch_get(key);
    if (cached) {
        return *cached;
    }
    try {
        Json d = request_({{"r", "patch"}, {"g", key}, {"u", user}, {"t", token}});
        const Json* pd = d.find("PatchData");
        if (!pd || pd->is_null()) {
            return std::nullopt;
        }
        // AUSSERHALB dieses Blocks -> falsy PatchData (leeres Objekt/Array/""/0)
        // wird dennoch zurueckgegeben, nur eben nicht gecacht.
        bool truthy = true;
        if (pd->type() == Json::Type::Object && pd->entries().empty()) truthy = false;
        else if (pd->type() == Json::Type::Array && pd->items().empty()) truthy = false;
        else if (pd->type() == Json::Type::String && pd->as_string().empty()) truthy = false;
        else if (pd->type() == Json::Type::Int && pd->as_int() == 0) truthy = false;
        else if (pd->type() == Json::Type::Bool && !pd->as_bool()) truthy = false;
        if (truthy) {
            cache_.patch_set(key, *pd);
            cache_.save(false);
        }
        return *pd;
    } catch (const RateLimited&) {
        throw;
    } catch (...) {
        return std::nullopt;
    }
}

bool RaClient::ra_award(long long achid, const std::string& user, const std::string& token, int hardcore) {
    try {
        Json d = request_({{"r", "awardachievement"}, {"a", std::to_string(achid)},
                            {"h", std::to_string(hardcore)}, {"u", user}, {"t", token}});
        const Json* success = d.find("Success");
        return success && success->type() == Json::Type::Bool && success->as_bool();
    } catch (...) {
        return false;
    }
}

namespace {
std::optional<long long> json_truthy_id(const Json& obj, const std::vector<std::string>& keys) {
    for (const auto& k : keys) {
        const Json* v = obj.find(k);
        if (!v || v->is_null()) continue;
        if (v->type() == Json::Type::Int) {
            if (v->as_int() != 0) return v->as_int();
            continue; // 0 ist falsy -> naechster Key
        }
        if (v->type() == Json::Type::String && !v->as_string().empty()) {
            try { return std::stoll(v->as_string()); } catch (...) { continue; }
        }
    }
    return std::nullopt;
}
std::string to_lower_str(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
}

std::set<long long> RaClient::ra_unlocks(long long gameid, const std::string& user, const std::string& token) {
    std::set<long long> ids;
    try {
        Json d = request_({{"r", "startsession"}, {"g", std::to_string(gameid)},
                            {"u", user}, {"t", token}});
        for (const auto& kv : d.entries()) {
            if (to_lower_str(kv.first).find("unlock") == std::string::npos) continue;
            if (kv.second.type() != Json::Type::Array) continue;
            for (const auto& e : kv.second.items()) {
                if (e.type() == Json::Type::Object) {
                    auto id = json_truthy_id(e, {"ID", "AchievementID", "id"});
                    if (id) ids.insert(*id);
                } else if (e.type() == Json::Type::Int) {
                    ids.insert(e.as_int());
                } else if (e.type() == Json::Type::String) {
                    try { ids.insert(std::stoll(e.as_string())); } catch (...) {}
                }
            }
        }
    } catch (...) {
        return {};
    }
    return ids;
}
