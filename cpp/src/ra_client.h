#pragma once
#include "json.h"
#include "ra_cache.h"
#include <string>
#include <map>
#include <functional>
#include <optional>
#include <stdexcept>
#include <set>

// Der eigentliche HTTP-Call ist injizierbar (RequestFn), damit diese Logik
// ohne echtes Netzwerk testbar ist. Fuer die Qt-Phase wird RequestFn durch
// einen echten QNetworkAccessManager-Aufruf ersetzt (ra_request()-Aequivalent).

struct RateLimited : std::runtime_error {
    int retry_after;
    explicit RateLimited(int ra) : std::runtime_error("rate limited"), retry_after(ra) {}
};

// RequestFn wirft RateLimited bei 429, std::exception bei anderen Fehlern
// oder liefert das geparste JSON-Ergebnis.
using RequestFn = std::function<Json(const std::map<std::string, std::string>& params)>;

class RaClient {
public:
    RaClient(RaCache& cache, RequestFn request_fn);

    // ra_gameid(md5, raise_limit): Rueckgabe leer (nullopt) bei "kein GameID"
        // Wirft RateLimited weiter, wenn raise_limit=true und die Anfrage drosselt.
    std::optional<long long> ra_gameid(const std::string& md5, bool raise_limit);

    // ra_patch(gameid, user, token): liefert PatchData als Json oder leer.
    // Wirft RateLimited immer weiter.
    std::optional<Json> ra_patch(long long gameid, const std::string& user, const std::string& token);

    // ra_login(user, pw): Token bei Erfolg, sonst leer. Kein Caching.
    std::optional<std::string> ra_login(const std::string& user, const std::string& pw);

    // ra_award(achid, user, token, hardcore): true bei Erfolg, sonst false.
    bool ra_award(long long achid, const std::string& user, const std::string& token, int hardcore = 0);

    // ra_unlocks(gameid, user, token): Menge bereits freigeschalteter IDs.
    // Tolerant gegen Antwortformat (Unlocks/HardcoreUnlocks als Liste von
    // Objekten {ID:..} oder rohen Zahlen). Fehler -> leere Menge.
    std::set<long long> ra_unlocks(long long gameid, const std::string& user, const std::string& token);

    RaCache* cachePtr() { return &cache_; }

private:
    RaCache& cache_;
    RequestFn request_;
};
