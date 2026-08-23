#include "throttle.h"
#include <algorithm>

ThrottleDecision resolve_gameid_throttled(
    RaCache& cache, RaClient& client, const std::string& md5,
    bool& ra_throttled,
    const std::function<void()>& pace_fn,
    const std::function<bool(int)>& wait_fn) {

    // h in gc -> gid = gc[h] or None (gecacht: sofort, keine RA-Anfrage)
    const Json* cached = cache.gameid_get(md5);
    if (cached) {
        std::optional<long long> gid;
        if (!cached->is_null()) {
            if (!(cached->type() == Json::Type::Int && cached->as_int() == 0))
                gid = cached->as_int();
        }
        return {true, gid, 0};
    }

    // elif ra_throttled: deferred += 1; proceed = False
    if (ra_throttled) {
        return {false, std::nullopt, 1};
    }

    // else: time.sleep(RA_REQUEST_PAUSE); gid = ra_gameid(h, raise_limit=True)
    pace_fn();
    std::optional<long long> gid;
    try {
        gid = client.ra_gameid(md5, true);
    } catch (const RateLimited& rl) {
        int wait = std::min(rl.retry_after != 0 ? rl.retry_after : 30, 60);
        bool aborted = wait_fn(wait);
        gid = std::nullopt;
        if (!aborted) {
            try {
                gid = client.ra_gameid(md5, true);
            } catch (const RateLimited&) {
                gid = std::nullopt;
            }
        }
        if (!gid) {
            ra_throttled = true;
            return {false, std::nullopt, 1};
        }
        return {true, gid, 0};
    }
    return {true, gid, 0};
}
