#pragma once
#include "ra_client.h"
#include <functional>
#include <optional>

// (md_ra_tool.py, Zeilen ~2140-2163): Cache-Treffer / bereits gedrosselt /
// ein Retry-After-Wait + ein Retry / danach globale Drossel fuer den Rest
// des Exports. Reine Entscheidungslogik, ROM-Patching bleibt aussen vor.
struct ThrottleDecision {
    bool proceed;                       // false = ueberspringen (deferred)
    std::optional<long long> gid;       // GameID, falls ermittelt
    int deferred_delta;                 // 0 oder 1 (Zaehler-Inkrement)
};

// pace_fn: entspricht time.sleep(RA_REQUEST_PAUSE) vor der ersten Anfrage.
// wait_fn(seconds): entspricht "stop is not None and stop.wait(seconds)".
//   Rueckgabe true = Abbruch angefordert waehrend des Wartens -> KEIN Retry.
//   Wenn es kein stop-Objekt gibt, muss wait_fn sofort false liefern
//   OHNE Wartezeit -- dieses Verhalten wird hier bewusst nachgebildet).
ThrottleDecision resolve_gameid_throttled(
    RaCache& cache, RaClient& client, const std::string& md5,
    bool& ra_throttled,
    const std::function<void()>& pace_fn,
    const std::function<bool(int)>& wait_fn);
