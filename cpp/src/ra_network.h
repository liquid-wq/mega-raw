#pragma once
#include <QString>
#include <map>
#include "ra_client.h"

constexpr const char* RA_HOST = "retroachievements.org";
constexpr const char* RA_USER_AGENT = "MegaDriveRA/2.0";

// r, m, g, u, t -- keine Sonderzeichen zu erwarten, aber Prozent-Kodierung
// trotzdem korrekt implementiert).
QString ra_urlencode(const std::map<std::string, std::string>& params);

// Baut die vollstaendige Request-URL wie ra_request() in Python.
QString ra_build_url(const std::map<std::string, std::string>& params);

// Synchroner RequestFn-Adapter fuer RaClient (siehe ra_client.h), nutzt
// QNetworkAccessManager + QEventLoop. Wirft RateLimited bei HTTP 429.
RequestFn make_qt_request_fn();
