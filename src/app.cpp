#include "app.h"

#include "archiv.h"
#include "erzeugt_katze.h"
#include "i18n.h"
#include "md5.h"
#include "md_romread.h"
#include "md_snoop.h"
#include "plattform.h"
#include "popup.h"
#include "ra_http.h"
#include "schrift.h"
#include "version.h"

#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace megaraw {
namespace {

// Hardcore erst einschalten, wenn RetroAchievements das Programm dafuer
// freigegeben hat. Bis dahin ist die Option verborgen und jede Sitzung Softcore.
constexpr bool kHardcoreAktiv = false;

// Ordnername der Sicherung. Liegt in dem Wurzelverzeichnis, das beim
// Einrichten gewaehlt wurde, damit Sicherung und Karte zusammenbleiben.
const char* kBackupDir = "MEGA-RAW_ORIG_BACKUP";

constexpr int kCoreRessource = 100;    // mega-core.x25, siehe app.rc.in
constexpr int kProRessource = 101;     // mega-pro.rbf
constexpr int kJasonRessource = 300;   // assets/jason_pixel.png

// --- Farben ---------------------------------------------------------------
// Die Qt-Fassung setzt kein eigenes Farbschema; alles ausser den fest
// eingefaerbten Elementen kommt aus der Windows-Palette (dunkel). Die
// fest eingefaerbten Werte stammen woertlich aus mainwindow.cpp.
constexpr ImU32 Rgb(unsigned rgb, unsigned a = 255) {
    return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, a);
}
const ImU32 kText = Rgb(0xffffff);
const ImU32 kGedimmt = Rgb(0x9d9d9d);
const ImU32 kRahmen = Rgb(0x9a9a9a);    // QGroupBox-Rahmen
const ImU32 kFenster = Rgb(0x202020);
const ImU32 kFeld = Rgb(0x2b2b2b);
const ImU32 kListe = Rgb(0x2b2b2b);
const ImU32 kListeAlt = Rgb(0x323232);
const ImU32 kOk = Rgb(0x00ff00);       // loginStatus_/monitorHint_ "color: #00ff00"
const ImU32 kRot = Rgb(0xff4444);
const ImU32 kSchnellstart = Rgb(0x40c060);
const ImU32 kCopyright = Rgb(0x888888);

ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

long long JetztMs() { return (long long)::GetTickCount64(); }

std::string Zahl(long long n) { return std::to_string(n); }

// "Verbinden && Monitor starten": && ist das Qt-Zeichen fuer ein einzelnes &.
std::string OhneMnemonik(std::string s) {
    for (size_t p = 0; (p = s.find("&&", p)) != std::string::npos; ++p) s.erase(p, 1);
    return s;
}

std::string Generisch(const fs::path& p) { return p.generic_u8string(); }

std::string Kleinschrift(std::string s) {
    for (char& c : s) c = (char)::tolower((unsigned char)c);
    return s;
}

bool Versteckt(const fs::path& p) {
    const DWORD a = ::GetFileAttributesW(p.wstring().c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
}

// QDirIterator(root, Dirs | NoDotAndDotDot, Subdirectories): alle Ordner
// unterhalb, ohne versteckte und Systemordner.
void OrdnerSammeln(const fs::path& d, std::vector<fs::path>& aus) {
    std::error_code ec;
    for (fs::directory_iterator it(d, fs::directory_options::skip_permission_denied, ec), ende; it != ende;
         it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_directory(ec2) || Versteckt(it->path())) continue;
        aus.push_back(it->path());
        OrdnerSammeln(it->path(), aus);
    }
}

std::vector<fs::path> DateienIn(const fs::path& d) {
    std::vector<fs::path> aus;
    std::error_code ec;
    for (fs::directory_iterator it(d, fs::directory_options::skip_permission_denied, ec), ende; it != ende;
         it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (it->is_regular_file(ec2) && !Versteckt(it->path())) aus.push_back(it->path());
    }
    return aus;
}

std::string LeseDatei(const std::string& pfad) {
    std::ifstream f(fs::u8path(pfad), std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool SchreibeDatei(const fs::path& pfad, const std::vector<uint8_t>& daten) {
    std::ofstream o(pfad, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write((const char*)daten.data(), (std::streamsize)daten.size());
    return o.good();
}

}  // namespace

// ============================================================================

App::App(void* fenster, float dpi, const Schriften& schriften)
    : fenster_(fenster), dpi_(dpi), f_(schriften) {
    post_ = std::make_shared<PostZiel>();
    post_->app = this;

    settingsPath_ = plattform::ExeOrdner() + "/settings.json";
    {
        std::error_code ec;
        ersterStart_ = !fs::exists(fs::u8path(settingsPath_), ec);
    }
    settings_ = Settings::load(settingsPath_);
    i18n::SpracheSetzen(settings_.language);

    cache_ = std::make_shared<RaCache>("ra_cache.json");
    client_ = std::make_shared<RaClient>(*cache_, make_winhttp_request_fn());

    loginStatus_ = BEIDE(T("Nicht eingeloggt."));
    merken_ = settings_.save_login;
    if (settings_.save_login && !settings_.user.empty())
        std::snprintf(userBuf_, sizeof(userBuf_), "%s", settings_.user.c_str());
    statusText_ = BEIDE(T("Bereit."));
    edStatus_ = BEIDE(T("EverDrive wird beim Monitor-Start automatisch gesucht"));
    monitorHint_ = BEIDE(T("Monitor: zuerst einloggen."));
    ramText_ = BEIDE(T("RAM: -"));
}

App::~App() {
    {
        std::lock_guard<std::mutex> l(post_->m);
        post_->app = nullptr;
        post_->auftraege.clear();
    }
    stopMonitorIfRunning();
    {
        std::lock_guard<std::mutex> l(badgeMutex_);
        badgeEnde_ = true;
    }
    badgeCv_.notify_all();
    if (badgeThread_.joinable()) badgeThread_.join();
    for (auto& t : hilfsThreads_) if (t.joinable()) t.detach();
    for (auto& kv : badges_) bilder::Freigeben(kv.second.tex);
    bilder::Freigeben(jason_);
    bilder::Freigeben(jasonGross_);
}

std::string App::Fenstertitel() {
    return Fmt("MEGA-RAW FPGA v%1  (Build %2)", {MEGA_RAW_VERSION, Zahl(MEGA_RAW_CPP_BUILD)});
}

void App::Starten() {
    const void* d = nullptr;
    size_t n = 0;
    // jasonPm.scaledToHeight(53, Qt::SmoothTransformation)
    if (bilder::Ressource(kJasonRessource, &d, &n))
        jason_ = bilder::PngHochladenSkaliert(d, n, (int)std::lround(53 * dpi_));
    // Gross fuer das Katzenfenster (wie RAW-NES: Portrait doppelt gross).
    if (bilder::Ressource(kJasonRessource, &d, &n))
        jasonGross_ = bilder::PngHochladenSkaliert(d, n, (int)std::lround(212 * dpi_));
    badgeThread_ = std::thread([this]() { BadgeThread(); });
    if (ersterStart_) {
        // Vorauswahl: Sprache von Windows, sofern angeboten, sonst englisch.
        static const std::pair<int, const char*> windows[] = {
            {LANG_GERMAN, "de"}, {LANG_ENGLISH, "en"}, {LANG_SPANISH, "es"}, {LANG_PORTUGUESE, "pt"},
            {LANG_FRENCH, "fr"}, {LANG_ITALIAN, "it"}, {LANG_POLISH, "pl"}};
        const int primaer = PRIMARYLANGID(::GetUserDefaultUILanguage());
        std::string vorschlag = "en";
        for (const auto& w : windows)
            if (w.first == primaer) vorschlag = w.second;
        i18n::SpracheSetzen(vorschlag);
        Dialog d;
        d.art = Dialog::Art::Ersteinrichtung;
        d.sprache = vorschlag;
        d.nummer = ++dialogNummer_;
        dialoge_.push_front(std::move(d));
    }
    // Gespeicherter Schluessel: beim Start bei RA pruefen und damit anmelden.
    if (settings_.save_login && !settings_.user.empty() && !settings_.token.empty()) onLogin();
    // Update-Check 2s nach Start (blockiert die Oberflaeche nicht)
    checkForUpdate();
}

void App::Post(std::function<void()> f) {
    std::lock_guard<std::mutex> l(post_->m);
    if (post_->app) post_->auftraege.push_back(std::move(f));
}

std::vector<std::string> App::LogZeilen() const {
    std::lock_guard<std::mutex> l(logMutex_);
    std::vector<std::string> aus;
    for (const Zweisprachig& z : log_) aus.push_back(z.text());
    return aus;
}

void App::checkForUpdate() {
    auto ziel = post_;
    hilfsThreads_.emplace_back([ziel]() {
        ::Sleep(2000);
        std::string body;
        if (!http_get("liquid-wq.github.io", "/data/version_fpga.txt", 8000, "MEGA-RAW-UpdateCheck", &body)) return;
        size_t a = 0;
        while (a < body.size() && (unsigned char)body[a] <= ' ') ++a;
        size_t e = a;
        while (e < body.size() && body[e] >= '0' && body[e] <= '9') ++e;
        size_t r = e;
        while (r < body.size() && (unsigned char)body[r] <= ' ') ++r;
        if (e == a || r != body.size()) return;     // toInt(&ok): nur eine Zahl
        const int remote = std::atoi(body.substr(a, e - a).c_str());
        if (remote <= MEGA_RAW_CPP_BUILD) return;
        std::lock_guard<std::mutex> l(ziel->m);
        if (!ziel->app) return;
        App* app = ziel->app;
        ziel->auftraege.push_back([app, remote]() {
            app->Info(T("Update verfügbar"),
                      Fmt(T("Eine neue Version ist verfügbar (Build %1, du hast Build %2).\n\nBesuche die Support-Seite zum Download."),
                          {Zahl(remote), Zahl(MEGA_RAW_CPP_BUILD)}));
        });
    });
    NachrichtHolen();
}

// Datei auf der Seite, z. B.:
//   { "id": 1, "de": "...", "en": "...", "link": "https://...",
//     "ab_build": 28, "bis_build": 40 }
// id: fuer jede neue Nachricht hochzaehlen (0 oder fehlend = keine).
// link, ab_build, bis_build: optional. Angezeigt wird reiner Text.
// Unabhaengig von version_fpga.txt, die nur die Build-Nummer enthaelt.
void App::NachrichtHolen() {
    auto ziel = post_;
    const long long gelesen = settings_.message_read;
    hilfsThreads_.emplace_back([ziel, gelesen]() {
        ::Sleep(2500);
        std::string body;
        if (!http_get("liquid-wq.github.io", "/data/nachricht_megaraw.json", 8000, "MEGA-RAW-Nachricht", &body))
            return;
        // Eine mit Stueckliste (BOM) gespeicherte Datei trotzdem lesen.
        if (body.size() >= 3 && (unsigned char)body[0] == 0xEF && (unsigned char)body[1] == 0xBB &&
            (unsigned char)body[2] == 0xBF)
            body.erase(0, 3);
        long long id = 0;
        std::string de, en, link;
        try {
            Json j = Json::parse(body);
            if (j.type() != Json::Type::Object) return;
            auto zahl = [&](const char* k, long long vorgabe) {
                const Json* v = j.find(k);
                return (v && v->type() == Json::Type::Int) ? v->as_int() : vorgabe;
            };
            auto text = [&](const char* k) {
                const Json* v = j.find(k);
                return (v && v->type() == Json::Type::String) ? v->as_string() : std::string();
            };
            id = zahl("id", 0);
            if (id <= 0 || id == gelesen) return;
            if (MEGA_RAW_CPP_BUILD < zahl("ab_build", 0) || MEGA_RAW_CPP_BUILD > zahl("bis_build", 1000000)) return;
            de = text("de");
            en = text("en");
            link = text("link");
            if (link.rfind("https://", 0) != 0) link.clear();   // nur sichere Adressen
            if (de.empty() && en.empty()) return;
            if (de.size() > 4000) de.resize(4000);
            if (en.size() > 4000) en.resize(4000);
        } catch (...) {
            return;
        }
        std::lock_guard<std::mutex> l(ziel->m);
        if (!ziel->app) return;
        App* app = ziel->app;
        ziel->auftraege.push_back([app, id, de, en, link]() {
            Dialog d;
            d.art = Dialog::Art::Nachricht;
            // Beide Sprachen mitgeben; angezeigt wird die eingestellte, fehlt
            // eine, die andere.
            d.text = de.empty() ? en : de;
            d.textEn = en.empty() ? de : en;
            d.link = link;
            d.nummer = ++app->dialogNummer_;
            d.ja = [app, id]() {
                app->settings_.message_read = id;
                app->settings_.save(app->settingsPath_);
            };
            app->dialoge_.push_back(std::move(d));
        });
    });
}

void App::appendLog(const Zweisprachig& line) {
    std::lock_guard<std::mutex> l(logMutex_);
    log_.push_back(line);
    if (log_.size() > 20000) log_.pop_front();
}

// ============================================================================
// Achievement-Liste
// ============================================================================

void App::rebuildAcList() {
    liste_.clear();
    if (!game_ || game_->achievements.empty()) {
        ListZeile z;
        z.text = (game_ && game_->no_set) ? TK("Keine Core-Achievements auf RA für dieses Spiel")
                                          : TK("Keine Achievements geladen.");
        z.farbe = 0;
        z.id = -1;   // Kennzeichen: Text wird bei der Anzeige uebersetzt
        liste_.push_back(z);
        return;
    }
    for (const auto& ac : game_->achievements) {
        std::string mark, suffix;
        ImU32 color;
        const bool hasRegion = (ac.mem.find("0xS00fff9=0") != std::string::npos) ||
                               (ac.mem.find("O:0xS00fff9!=0") != std::string::npos);
        if (ac.owned) {
            mark = "\xE2\x98\x85"; suffix = T("  [bereits freigeschaltet]"); color = Rgb(0xd4af37);
        } else if (ac.triggered) {
            mark = "\xE2\x98\x85"; suffix = T("  [gerade freigeschaltet]"); color = Rgb(0xd4af37);
        } else if (ac.unsupported) {
            mark = "\xE2\x9C\x97"; suffix = T("  [nicht unterstützt]"); color = Rgb(0x888888);
        } else if (hasRegion && consolePal_ == 1) {
            mark = "\xE2\x97\x90"; suffix = T("  [PAL-GESPERRT: Konsole läuft 50Hz]"); color = Rgb(0xc04040);
        } else if (hasRegion && consolePal_ < 0) {
            mark = "\xE2\x97\x8B"; suffix = T("  [region-abhängig: braucht 60Hz]"); color = Rgb(0xd4af37);
        } else {
            mark = "\xE2\x97\x8B"; suffix = ""; color = Rgb(0xcccccc);
        }
        ListZeile z;
        z.text = Fmt("%1  %2 (%3p) \xE2\x80\x94 %4%5", {mark, ac.title, Zahl(ac.points), ac.desc, suffix});
        z.farbe = color;
        z.id = ac.id;
        z.badge = ac.badge;
        liste_.push_back(z);
    }
}

// ============================================================================
// Login
// ============================================================================

// Statuszeile unter dem Login. Nach einer Anmeldung kurz eine Einblendung wie
// in der Qt-Fassung 1.3.0: mittig, fett, acht ruhige Pulse (9,5 s), 0,6 s
// stehen, dann in 0,7 s nach links gleiten; danach bleibt "✓ Name".
void App::ZeichneLoginStatus() {
    const double kBlink = 9.5, kPause = 0.6, kGleiten = 0.7;
    const double t = anmeldStart_ < 0 ? -1.0 : ImGui::GetTime() - anmeldStart_;
    if (t < 0 || t >= kBlink + kPause + kGleiten || token_.empty()) {
        anmeldStart_ = -1.0;
        ImGui::PushStyleColor(ImGuiCol_Text, V(loginOk_ ? kOk : kText));
        ImGui::TextUnformatted(loginStatus_.text().c_str());
        ImGui::PopStyleColor();
        return;
    }
    // Deckkraft: Stuetzwerte 0 -> 1 -> 0,25 -> 1 ... (letzter Puls bleibt 1),
    // ueber den ganzen Ablauf mit InOutSine verteilt.
    float alpha = 1.0f;
    if (t < kBlink) {
        const double p = t / kBlink;
        const double e = -(std::cos(3.14159265358979 * p) - 1.0) / 2.0;
        const int kPulse = 8;
        const double pos = e * 2 * kPulse;              // 0 .. 16
        const int k = std::min((int)pos, 2 * kPulse - 1);
        const double f = pos - k;
        auto stuetz = [&](int i) -> double {
            if (i == 0) return 0.0;
            if (i % 2 == 1) return 1.0;
            return i == 2 * kPulse ? 1.0 : 0.25;
        };
        alpha = (float)(stuetz(k) + (stuetz(k + 1) - stuetz(k)) * f);
    }
    const std::string& text = anmeldText_.text();
    ImGui::PushFont(f_.fett, ImGui::GetFontSize());
    const ImVec2 groesse = ImGui::CalcTextSize(text.c_str());
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float breite = ImGui::GetContentRegionAvail().x;
    const float xMitte = std::max(0.0f, (breite - groesse.x) / 2);
    float x = xMitte;
    if (t > kBlink + kPause) {
        double p = (t - kBlink - kPause) / kGleiten;
        p = p < 0.5 ? 4 * p * p * p : 1 - std::pow(-2 * p + 2, 3) / 2;   // InOutCubic
        x = (float)(xMitte * (1.0 - p));
    }
    ImU32 farbe = kOk;
    farbe = (farbe & ~IM_COL32_A_MASK) | ((ImU32)(alpha * 255.0f) << IM_COL32_A_SHIFT);
    ImGui::GetWindowDrawList()->AddText(ImVec2(start.x + x, start.y), farbe, text.c_str());
    ImGui::Dummy(ImVec2(breite, groesse.y));
    ImGui::PopFont();
}

// Nur der Schluessel, nie das Passwort - und nur mit gesetztem Haken.
void App::speichereLogin() {
    settings_.save_login = merken_;
    if (settings_.save_login && !token_.empty()) {
        settings_.user = userBuf_;
        settings_.token = token_;
    } else if (!settings_.save_login) {
        settings_.user.clear();
        settings_.token.clear();
    }
    settings_.save(settingsPath_);
}

void App::onLogout() {
    stopMonitorIfRunning();
    token_.clear();
    loginOk_ = false;
    anmeldStart_ = -1.0;
    settings_.token.clear();
    settings_.save(settingsPath_);
    loginStatus_ = BEIDE(T("Nicht eingeloggt."));
    appendLog(BEIDE(T("Abgemeldet.")));
    updateMonitorState();
}

void App::onLogin() {
    if (!token_.empty()) { onLogout(); return; }   // Knopf zeigt dann "Abmelden"
    const std::string user = userBuf_;
    const std::string pw = passBuf_;
    // Ohne eingegebenes Passwort: mit dem gespeicherten Schluessel anmelden.
    const std::string schluessel = (pw.empty() && user == settings_.user) ? settings_.token : std::string();
    if (user.empty() || (pw.empty() && schluessel.empty())) {
        loginStatus_ = BEIDE(T("Bitte Benutzername und Passwort eingeben."));
        return;
    }
    loginLaeuft_ = true;
    loginStatus_ = BEIDE(T("Login läuft..."));
    auto client = client_;
    auto ziel = post_;
    hilfsThreads_.emplace_back([client, ziel, user, pw, schluessel]() {
        Zweisprachig statusText;
        std::string newToken;
        bool ok = false;
        try {
            auto tok = pw.empty() ? client->ra_login_token(user, schluessel) : client->ra_login(user, pw);
            if (tok) {
                newToken = *tok;
                statusText = "\xE2\x9C\x93 " + user;
                ok = true;
            } else {
                statusText = pw.empty() ? BEIDE(T("Anmeldung abgelaufen -- bitte das Passwort eingeben."))
                                        : BEIDE(T("Login fehlgeschlagen (falsche Zugangsdaten?)."));
            }
        } catch (const RateLimited& e) {
            statusText = BEIDE(Fmt(T("RA drosselt, bitte %1s warten."), {Zahl(e.retry_after)}));
        } catch (const std::exception& e) {
            // RA lehnt eine falsche Anmeldung mit 401/403 ab - das ist kein
            // Netzwerkfehler. Ohne Internet bleibt es bei "Netzwerkfehler".
            const std::string w = e.what();
            if (w == "HTTP 401" || w == "HTTP 403")
                statusText = pw.empty() ? BEIDE(T("Anmeldung abgelaufen -- bitte das Passwort eingeben."))
                                        : BEIDE(T("Falsche Anmeldedaten."));
            else
                statusText = BEIDE(Fmt(T("Netzwerkfehler: %1"), {e.what()}));
        }
        const bool automatisch = pw.empty();
        std::lock_guard<std::mutex> l(ziel->m);
        if (!ziel->app) return;
        App* app = ziel->app;
        ziel->auftraege.push_back([app, statusText, newToken, ok, automatisch, user]() {
            app->loginLaeuft_ = false;
            app->loginStatus_ = statusText;
            if (ok) {
                app->loginOk_ = true;
                app->token_ = newToken;
                std::memset(app->passBuf_, 0, sizeof(app->passBuf_));   // wird nicht weiter gebraucht
                // Einblendung: mittig, ruhig pulsierend, gleitet nach links
                // und bleibt als "✓ Name" stehen (siehe Zeichnen).
                app->anmeldText_ = automatisch
                    ? BEIDE("\xE2\x9C\x93 " + Fmt(T("Automatisch angemeldet als %1 (per Sitzungstoken)"), {user}))
                    : BEIDE("\xE2\x9C\x93 " + Fmt(T("Angemeldet als %1"), {user}));
                app->anmeldStart_ = ImGui::GetTime();
                app->speichereLogin();
                app->appendLog(BEIDE(T("Login erfolgreich.")));
                app->updateMonitorState();
            }
        });
    });
}

// ============================================================================
// Mapper einrichten / zuruecksetzen
// ============================================================================

std::vector<uint8_t> App::coreDaten() const {
    // mega-core.x25 neben der Anwendung hat Vorrang - so laesst sich ein
    // anderer Kern einsetzen, ohne neu zu bauen. Sonst der eingebettete.
    const std::string neben = LeseDatei(plattform::ExeOrdner() + "/mega-core.x25");
    if (!neben.empty()) return std::vector<uint8_t>(neben.begin(), neben.end());
    const void* d = nullptr;
    size_t n = 0;
    if (bilder::Ressource(kCoreRessource, &d, &n))
        return std::vector<uint8_t>((const uint8_t*)d, (const uint8_t*)d + n);
    return {};
}

std::vector<uint8_t> App::proDaten() const {
    // Wie coreDaten(): mega-pro.rbf neben der Anwendung hat Vorrang.
    const std::string neben = LeseDatei(plattform::ExeOrdner() + "/mega-pro.rbf");
    if (!neben.empty()) return std::vector<uint8_t>(neben.begin(), neben.end());
    const void* d = nullptr;
    size_t n = 0;
    if (bilder::Ressource(kProRessource, &d, &n))
        return std::vector<uint8_t>((const uint8_t*)d, (const uint8_t*)d + n);
    return {};
}

void App::onSetupMapper() {
    // Die Konsole laedt den Custom-Mapper nur, wenn mega-core.x25 im selben
    // Ordner wie das ROM liegt. Bei nach Buchstaben sortierten Sammlungen
    // waeren das dutzende Ordner - deshalb automatisch.
    std::vector<uint8_t> src = coreDaten();
    if (src.empty()) {
        // Rueckfall wie in der Qt-Fassung: Kern von Hand waehlen.
        const std::string pfad = plattform::DateiWaehlen(fenster_, T("mega-core.x25 wählen"),
                                                         {{"EverDrive Core", "*.x25;*.rbf"}},
                                                         plattform::ExeOrdner());
        if (pfad.empty()) return;
        const std::string inhalt = LeseDatei(pfad);
        src.assign(inhalt.begin(), inhalt.end());
    }

    // Vor der Auswahl erklaeren, was gemeint ist.
    Info(T("Mapper einrichten"),
         T("Bitte im nächsten Fenster die SD-Karte auswählen - also das "
           "Laufwerk selbst (z.B. H:\\), das den Ordner MEGA enthält, "
           "keinen Unterordner.\n\n"
           "Der Kern wird dann in alle Ordner kopiert, in denen Spiele liegen. "
           "Bereits vorhandene Dateien werden vorher gesichert."),
         [this, src]() {
             const std::string root = plattform::OrdnerWaehlen(
                 fenster_, T("SD-Karte wählen - das Laufwerk selbst (mit dem Ordner MEGA), z.B. H:\\"),
                 settings_.rom_root);
             if (root.empty()) return;
             settings_.rom_root = root;
             settings_.save(settingsPath_);
             setupMapperIn(root, src);
         });
}

void App::setupMapperIn(const std::string& rootStr, const std::vector<uint8_t>& src) {
    const fs::path root = fs::u8path(rootStr);

    static const std::set<std::string> romExt = {".md", ".bin", ".gen", ".smd", ".68k", ".sgd",
                                                 ".zip", ".7z", ".rar"};

    // Jeden Ordner sammeln, in dem mindestens eine ROM liegt.
    std::vector<fs::path> dirs;
    dirs.push_back(root);
    OrdnerSammeln(root, dirs);

    // Krikzz' Systemordner aussparen. In MEGA\edapp\* und MEGA\syscore\*
    // liegen seine eigenen Kerne unter demselben Namen mega-core.x25 - ohne
    // diese Ausnahme wurden sie als Spielordner gewertet und ueberschrieben.
    auto istSystemordner = [](const fs::path& pfad) {
        const std::string p = Kleinschrift(Generisch(pfad));
        auto endet = [&](const char* s) {
            const size_t n = std::strlen(s);
            return p.size() >= n && p.compare(p.size() - n, n, s) == 0;
        };
        return p.find("/mega/edapp/") != std::string::npos || endet("/mega/edapp")
            || p.find("/mega/syscore/") != std::string::npos || endet("/mega/syscore")
            || p.find("/mega/mappers") != std::string::npos || p.find("/mega/sysdata") != std::string::npos;
    };

    std::vector<fs::path> targets;
    for (const fs::path& d : dirs) {
        if (istSystemordner(d)) continue;
        for (const fs::path& f : DateienIn(d)) {
            if (romExt.count(Kleinschrift(f.extension().u8string()))) { targets.push_back(d); break; }
        }
    }

    if (targets.empty()) {
        Info(T("Mapper einrichten"), T("In diesem Verzeichnis wurden keine ROMs gefunden."));
        return;
    }

    // Beide Kerne neben jedes ROM: mega-core.x25 laedt das CORE,
    // mega-pro.rbf das PRO (fpga/README.md). Jedes Geraet nimmt nur seine
    // eigene Datei, die andere bleibt unbeachtet - so muss das Geraet nicht
    // erkannt werden, und die Karte passt in beide.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> kerne;
    kerne.push_back({"mega-core.x25", src});
    std::vector<uint8_t> pro = proDaten();
    if (!pro.empty()) kerne.push_back({"mega-pro.rbf", std::move(pro)});

    // Vor dem Ueberschreiben sichern - nur beim ersten Mal je Ordner, und nur,
    // was NICHT schon unser eigener Kern ist.
    const fs::path bakRoot = root / kBackupDir;
    int ok = 0, fail = 0, saved = 0;
    for (const fs::path& d : targets) {
        bool ordnerOk = true;
        for (const auto& k : kerne) {
            const fs::path dst = d / k.first;
            std::error_code ec;
            if (fs::exists(dst, ec) && fs::file_size(dst, ec) != k.second.size()) {
                const fs::path rel = d.lexically_relative(root);
                const fs::path bakDir = (rel.empty() || rel == ".") ? bakRoot : bakRoot / rel;
                const fs::path bak = bakDir / k.first;
                if (!fs::exists(bak, ec)) {
                    fs::create_directories(bakDir, ec);
                    if (fs::copy_file(dst, bak, ec)) ++saved;
                }
            }
            fs::remove(dst, ec);
            if (!SchreibeDatei(dst, k.second)) ordnerOk = false;
        }
        if (ordnerOk) ++ok; else ++fail;
    }

    if (saved > 0)
        appendLog(BEIDE(Fmt(T("%1 vorhandene Datei(en) nach %2 gesichert."), {Zahl(saved), kBackupDir})));
    appendLog(BEIDE(Fmt(T("Mapper eingerichtet: %1 Ordner beschrieben, %2 fehlgeschlagen."), {Zahl(ok), Zahl(fail)})));
    Info(T("Mapper einrichten"),
         Fmt(kerne.size() > 1 ? T("mega-core.x25 und mega-pro.rbf in %1 Ordner kopiert.")
                              : T("mega-core.x25 in %1 Ordner kopiert."), {Zahl(ok)}) +
             (fail ? Fmt(T("\n%1 Ordner konnten nicht beschrieben werden."), {Zahl(fail)}) : std::string()) +
             (saved ? std::string("\n\n") + T("Vorhandene Originale wurden gesichert und lassen "
                                               "sich in den Optionen wiederherstellen.")
                    : std::string()));
}

// Gegenstueck zum Einrichten: holt die gesicherten Originale zurueck.
void App::onRestoreMappers() {
    const std::string root = plattform::OrdnerWaehlen(
        fenster_, T("SD-Karte wählen - das Laufwerk selbst (mit dem Ordner MEGA), z.B. H:\\"), settings_.rom_root);
    if (root.empty()) return;
    restoreIn(root);
}

void App::restoreIn(const std::string& rootStr) {
    // Unsere eigenen Dateien zum Vergleich - alles, was genauso gross ist,
    // stammt von uns und darf entfernt werden, wenn es kein Original gab.
    // Beide Kerne: mega-core.x25 (CORE) und mega-pro.rbf (PRO).
    const std::map<std::string, size_t> unsereGroesse = {
        {"mega-core.x25", coreDaten().size()}, {"mega-pro.rbf", proDaten().size()}};
    const fs::path root = fs::u8path(rootStr);
    const fs::path bakRoot = root / kBackupDir;
    const std::string bakText = Kleinschrift(Generisch(bakRoot));

    auto alleKerne = [&unsereGroesse](const fs::path& start) {
        std::vector<fs::path> dirs{start};
        OrdnerSammeln(start, dirs);
        std::vector<fs::path> aus;
        for (const fs::path& d : dirs)
            for (const fs::path& f : DateienIn(d))
                if (unsereGroesse.count(Kleinschrift(f.filename().u8string()))) aus.push_back(f);
        return aus;
    };

    std::vector<fs::path> gesichert;
    std::error_code ec;
    if (fs::is_directory(bakRoot, ec)) gesichert = alleKerne(bakRoot);

    std::vector<fs::path> eigene;
    for (const fs::path& f : alleKerne(root)) {
        if (Kleinschrift(Generisch(f)).rfind(bakText, 0) == 0) continue;
        const size_t soll = unsereGroesse.at(Kleinschrift(f.filename().u8string()));
        if (soll == 0 || fs::file_size(f, ec) != soll) continue;
        eigene.push_back(f);
    }

    if (gesichert.empty() && eigene.empty()) {
        Info(T("Original-Mapper wiederherstellen"), Fmt(T("In %1 gibt es nichts zurückzusetzen."), {rootStr}));
        return;
    }

    std::string frage;
    if (!gesichert.empty())
        frage += Fmt(T("%1 gesicherte Originaldatei(en) zurückschreiben."), {Zahl((long long)gesichert.size())}) + "\n";
    if (!eigene.empty())
        frage += Fmt(T("%1 von MEGA-RAW angelegte Datei(en) entfernen."), {Zahl((long long)eigene.size())}) + "\n";
    frage += std::string("\n") + T("Fortfahren?");

    Frage(T("Original-Mapper wiederherstellen"), frage, [this, root, bakRoot, gesichert, eigene]() {
        // Erst unsere Kopien entfernen, dann Originale zurueckschreiben.
        std::error_code ec;
        int entfernt = 0;
        for (const fs::path& f : eigene)
            if (fs::remove(f, ec)) ++entfernt;

        int ok = 0, fail = 0;
        for (const fs::path& b : gesichert) {
            const fs::path dst = root / b.lexically_relative(bakRoot);
            fs::create_directories(dst.parent_path(), ec);
            fs::remove(dst, ec);
            if (fs::copy_file(b, dst, ec)) ++ok; else ++fail;
        }

        appendLog(BEIDE(Fmt(T("Zurücksetzen: %1 Original(e) zurück, %2 eigene entfernt, %3 fehlgeschlagen."),
                      {Zahl(ok), Zahl(entfernt), Zahl(fail)})));
        Info(T("Original-Mapper wiederherstellen"),
             Fmt(T("%1 Originaldatei(en) zurückgeschrieben, %2 eigene entfernt."), {Zahl(ok), Zahl(entfernt)}) +
                 (fail ? Fmt(T("\n%1 fehlgeschlagen."), {Zahl(fail)}) : std::string()));
    });
}

// ============================================================================
// Spielerkennung und Set laden
// ============================================================================

// Erkennung ueber den MD5, den der Mapper selbst rechnet. Ueber USB gehen
// dabei 16 Byte statt zwei Megabyte.
bool App::detectGame() {
    erkennungErklaert_ = false;
    std::vector<std::string> versuche;
    auto [edPtr, port] = find_everdrive(std::string(), &versuche);
    if (!edPtr) {
        appendLog(BEIDE(T("Kein EverDrive gefunden. Ist das USB-Kabel angeschlossen? Ist noch ein anderes Programm offen, das den Anschluss belegt?")));
        // Diagnose: warum jeder Anschluss abgelehnt wurde.
        for (const std::string& v : versuche) appendLog("  " + v);
        erkennungErklaert_ = true;
        return false;
    }

    uint8_t build = 0;
    try {
        build = edPtr->memrd(mdsnoop::kAddrSnoop + mdsnoop::kCtlOffset, 1)[0];
    } catch (...) {
        edPtr->close();
        // EverDrive antwortet, der Speicher nicht: beim Ausschalten so beobachtet.
        appendLog(BEIDE(T("Das EverDrive antwortet, die Konsole nicht. Ist die Konsole eingeschaltet?")));
        erkennungErklaert_ = true;
        return false;
    }
    if (build == 0 || build == 0xFF) {
        edPtr->close();
        // Konsole an, aber EverDrive-Menue oder ein Spiel ohne unseren Kern.
        appendLog(BEIDE(T("Die Konsole läuft, aber ohne RA-Mapper. Steht sie noch im EverDrive-Menü? Dann ein Spiel starten. Läuft schon ein Spiel: mega-core.x25 (CORE) bzw. mega-pro.rbf (PRO) muss neben dem ROM liegen.")));
        erkennungErklaert_ = true;
        return false;
    }

    // Kopfblock holen, daraus die ROM-Groesse (Headerfeld bei 0x1A4).
    Bytes head = mdrom::readHeaderSafe(*edPtr, mdsnoop::kAddrSnoop);
    uint32_t size = mdrom::romSizeFromHeader(head);
    if (size > 0) {
        // Header-Groesse gegen die tatsaechlichen Daten pruefen.
        const uint32_t echt = mdrom::romSizeProbe(*edPtr, mdsnoop::kAddrSnoop, size);
        if (echt > size) {
            appendLog(BEIDE(Fmt(T("Header meldet %1 KB, tatsächlich %2 KB - korrigiert."),
                          {Zahl(size / 1024), Zahl(echt / 1024)})));
            size = echt;
        }
    }
    if (size == 0) {
        edPtr->close();
        appendLog(BEIDE(T("ROM-Header nicht lesbar - läuft ein Spiel?")));
        return false;
    }
    appendLog(BEIDE(Fmt(T("ROM %1 KB - Hash wird im Mapper gerechnet..."), {Zahl(size / 1024)})));

    Bytes dig = mdrom::romMd5(*edPtr, size, mdsnoop::kAddrSnoop);
    edPtr->close();

    if (dig.size() != 16) {
        appendLog(BEIDE(T("Hash nicht fertig geworden.")));
        return false;
    }

    std::string md5;
    for (uint8_t b : dig) md5 += i18n::Hex(b, 2, false);
    appendLog("MD5: " + md5);

    romPath_.clear();
    romPathText_ = BEIDE(T("(von der Konsole erkannt)"));
    game_.reset();
    setHasSet_ = false;
    tryLoadGame(md5);
    return setHasSet_;
}

void App::onChooseRom() {
    const std::string path = plattform::DateiWaehlen(
        fenster_, "ROM wählen",
        {{"Mega Drive ROMs / Archive", "*.md;*.bin;*.gen;*.smd;*.zip;*.rar"}, {"Alle Dateien", "*.*"}});
    if (path.empty()) return;
    loadRom(path);
}

void App::loadRom(std::string path) {
    std::string ext = Kleinschrift(fs::u8path(path).extension().u8string());
    if (ext == ".zip" || ext == ".rar") {
        auto ex = extract_archive_rom(path);   // neben Archiv entpacken
        if (!ex) { appendLog(BEIDE(T("Keine MD-ROM im Archiv gefunden (oder RAR ohne WinRAR)."))); return; }
        appendLog(BEIDE(Fmt(T("Aus Archiv entpackt: %1"), {fs::u8path(*ex).filename().u8string()})));
        path = *ex;
    }

    romPath_ = path;
    romPathText_ = path;
    game_.reset();
    setHasSet_ = false;
    updateMonitorState();

    std::error_code ec;
    if (!fs::exists(fs::u8path(path), ec)) {
        statusText_ = BEIDE(T("Fehler: Datei existiert nicht."));
        appendLog(BEIDE(T("Fehler: Datei nicht gefunden: ") + path));
        return;
    }
    const std::string inhalt = LeseDatei(path);
    const std::string md5 = md5_hex(std::vector<uint8_t>(inhalt.begin(), inhalt.end()));
    statusText_ = BEIDE(Fmt(T("MD5: %1 - suche Achievement-Set..."), {md5}));
    appendLog(BEIDE(T("ROM geladen: ") + path));
    appendLog("MD5: " + md5);

    tryLoadGame(md5);
}

void App::tryLoadGame(const std::string& md5) {
    try {
        auto gid = client_->ra_gameid(md5, true);
        if (!gid) {
            statusText_ = BEIDE(T("Kein RetroAchievements-Set für diese ROM gefunden."));
            appendLog(BEIDE(T("RA kennt diese ROM nicht (MD5 unbekannt).")));
            return;
        }
        appendLog(BEIDE(Fmt(T("RA GameID: %1"), {Zahl(*gid)})));

        if (token_.empty()) {
            statusText_ = BEIDE(T("GameID gefunden, aber kein Login -> kein Patch-Data-Abruf."));
            appendLog(BEIDE(T("Bitte zuerst einloggen, um das Achievement-Set zu laden.")));
            return;
        }

        auto patch = client_->ra_patch(*gid, userBuf_, token_);
        Game g = build_game_from_patch(*gid, md5, patch);
        if (!g.valid) {
            statusText_ = BEIDE(T("Kein Patch-Data von RA erhalten."));
            appendLog(BEIDE(T("ra_patch lieferte kein gültiges PatchData.")));
            return;
        }
        if (g.no_set) {
            statusText_ = BEIDE(g.name + T(" - keine unterstützte ROM-Version."));
            appendLog(BEIDE(T("Unsupported game version laut RA.")));
            return;
        }

        // Bereits freigeschaltete Achievements golden markieren
        try {
            auto owned = client_->ra_unlocks(*gid, userBuf_, token_, kHardcoreAktiv && settings_.hardcore);
            int nOwned = 0;
            for (auto& a : g.achievements) {
                if (owned.count(a.id)) { a.owned = true; a.triggered = true; nOwned++; }
            }
            if (nOwned) appendLog(BEIDE(Fmt(T("%1 Achievement(s) bereits freigeschaltet."), {Zahl(nOwned)})));
        } catch (...) {}

        game_ = g;
        {
            const int n = (int)g.raw_core;
            const int uns = (int)g.n_unsupported;
            // Compat-Check (compat.cpp): bei der FPGA-Variante wird die ROM
            // nicht angefasst, uebrig bleibt die Frage der Engine.
            bool compat = true;
            if (!romPath_.empty()) {
                appendLog("\xE2\x9C\x93 Keine ROM-Änderung nötig (Custom-Mapper liest den Work-RAM direkt)");
                if (uns == 0)
                    appendLog(Fmt("\xE2\x9C\x93 Alle %1 Achievements von der Engine unterstützt", {Zahl(n)}));
                else
                    appendLog(Fmt("~ %1/%2 Achievements unterstützt (%3 mit nicht abbildbaren Bedingungen)",
                                  {Zahl(n - uns), Zahl(n), Zahl(uns)}));
            }
            statusText_ = i18n::Beide([&]() {
                std::string info = "\xE2\x9C\x93 " + g.name + "\n";
                info += "[" + std::string(compat ? T("KOMPATIBEL") : T("NICHT KOMPATIBEL")) + "]\n";
                info += Fmt(T("Achievements gesamt: %1\n"), {Zahl(n)});
                info += Fmt(T("Von der Engine lesbar: %1/%2"), {Zahl(n - uns), Zahl(n)});
                return info;
            });
            statusStil_ = compat ? StatusStil::Gruen : StatusStil::Rot;
        }
        appendLog(BEIDE(Fmt(T("Set geladen: %1 Core-Achievements, %2 RAM-Bytes zu spiegeln"),
                      {Zahl(g.raw_core), Zahl((long long)g.addr_list.size())})));
        setHasSet_ = !g.addr_map.empty();
        updateMonitorState();
        rebuildAcList();
    } catch (const RateLimited& e) {
        statusText_ = BEIDE(Fmt(T("RA drosselt, bitte %1s warten."), {Zahl(e.retry_after)}));
    } catch (const std::exception& e) {
        statusText_ = BEIDE(Fmt(T("Netzwerkfehler: %1"), {e.what()}));
    }
}

// ============================================================================
// Monitor
// ============================================================================

void App::stopMonitorIfRunning() {
    if (!monWorker_) return;
    ++monGeneration_;              // spaete Rueckmeldungen des alten Workers verwerfen
    monWorker_->stop();
    // Ein Thread darf nie abgeraeumt werden, solange er laeuft. Laeuft er
    // nach der Wartezeit noch, bleibt das Objekt stehen und endet selbst.
    if (monWorker_->warte(5000)) monWorker_.reset();
    else monWorker_.release();
}

void App::onToggleMonitor() {
    if (monitoring_) {
        stopMonitorIfRunning();
        monitoring_ = false;
        // Spielzustand verwerfen, sonst arbeitet der naechste Start mit dem
        // alten Spiel weiter.
        game_.reset();
        setHasSet_ = false;
        rebuildAcList();   // Liste des alten Spiels nicht stehen lassen
        statusText_ = BEIDE(T("Kein Spiel geladen."));
        statusStil_ = StatusStil::Grau;
        updateMonitorState();
        appendLog(BEIDE(T("Monitor gestoppt.")));
        return;
    }
    if (token_.empty()) { appendLog(BEIDE(T("Bitte zuerst einloggen."))); return; }
    verbindungWeg_ = false;

    // Kein Spiel gewaehlt: aus dem Mapper erkennen.
    if (!setHasSet_) {
        appendLog(BEIDE(T("Erkenne laufendes Spiel...")));
        if (!detectGame()) {
            // Nur wenn die Ursache nicht schon genannt wurde.
            if (!erkennungErklaert_)
                appendLog(BEIDE(T("Erkennung fehlgeschlagen - bitte ROM von Hand wählen.")));
            return;
        }
    }

    const std::string port;  // leer -> Autoerkennung scannt alle Ports

    Game gm = game_ ? *game_ : Game{};
    monWorker_ = std::make_unique<MonitorWorker>(port, gm, userBuf_, token_, client_,
                                                 kHardcoreAktiv && settings_.hardcore);
    const int gen = ++monGeneration_;
    MonitorWorker& w = *monWorker_;
    App* self = this;
    auto ziel = post_;
    // Rueckruf aus dem Worker-Thread -> Auftrag im Oberflaechen-Thread.
    // aktuell: nur, solange dieser Worker der laufende ist.
    auto weiter = [ziel, self, gen](std::function<void(App&)> f, bool immer) {
        std::lock_guard<std::mutex> l(ziel->m);
        if (!ziel->app) return;
        ziel->auftraege.push_back([self, gen, f, immer]() {
            if (immer || self->monGeneration_ == gen) f(*self);
        });
    };
    w.log = [weiter](const Zweisprachig& m) { weiter([m](App& a) { a.onWorkerLog(m); }, true); };
    w.bramDump = [weiter](const Bytes& b) { weiter([b](App& a) { a.onWorkerRam(b); }, false); };
    w.palState = [weiter](bool pal) {
        weiter([pal](App& a) { a.consolePal_ = pal ? 1 : 0; a.rebuildAcList(); }, false);
    };
    w.unlocked = [weiter](const std::string& t, int p, long long id) {
        weiter([t, p, id](App& a) { a.onWorkerUnlocked(t, p, id); }, false);
    };
    w.connectionLost = [weiter](const std::string& r) { weiter([r](App& a) { a.onWorkerConnectionLost(r); }, false); };
    w.coreMissing = [weiter]() { weiter([](App& a) { a.onWorkerCoreMissing(); }, false); };
    w.gameLeft = [weiter]() {
        weiter([](App& a) {
            // Wie ein Stopp von Hand: Spielzustand verwerfen.
            a.game_.reset();
            a.setHasSet_ = false;
            a.rebuildAcList();
            a.statusText_ = BEIDE(T("Kein Spiel geladen."));
            a.statusStil_ = StatusStil::Grau;
        }, false);
    };
    w.finished = [weiter]() { weiter([](App& a) { a.onWorkerFinished(); }, false); };

    w.startThread();
    monitoring_ = true;
    monitorHint_ = BEIDE(std::string("\xE2\x9C\x93 ") + T("Verbunden ") + port);
    monitorHintFarbe_ = kOk;
    appendLog(BEIDE(T("Verbinde mit ") + port + " ..."));
}

void App::updateMonitorState() {
    const bool loggedIn = !token_.empty();
    monitorBtnEnabled_ = loggedIn;
    if (!loggedIn) {
        monitorHint_ = BEIDE(T("Monitor: zuerst einloggen."));
    } else if (!setHasSet_) {
        monitorHint_ = BEIDE(T("Monitor bitte nach Spielstart starten."));
    } else {
        monitorHint_ = BEIDE(std::string("\xE2\x9C\x93 ") + T("Monitor bereit."));
        monitorHintFarbe_ = kOk;
    }
}

void App::onWorkerLog(const Zweisprachig& msg) { appendLog(msg); }

void App::onWorkerRam(const Bytes& raw) {
    const long long now = JetztMs();
    if (lastByteChangeMs_.size() < raw.size()) lastByteChangeMs_.resize(raw.size(), 0);
    for (size_t i = 0; i < raw.size(); ++i) {
        const bool changed = (i >= lastRamBytes_.size()) || lastRamBytes_[i] != raw[i];
        if (changed && !lastRamBytes_.empty()) lastByteChangeMs_[i] = now;
    }
    lastRamBytes_ = raw;
    ramStil_ = RamStil::Bytes;
}

void App::onWorkerUnlocked(const std::string& title, int points, long long achId) {
    appendLog(BEIDE(Fmt(T("*** FREIGESCHALTET: %1 (%2 Punkte, ID %3) ***"), {title, Zahl(points), Zahl(achId)})));
    std::string desc, badgeName;
    if (game_) {
        for (auto& a : game_->achievements)
            if (a.id == achId) {
                a.triggered = true;
                desc = a.desc;
                badgeName = a.badge;
                break;
            }
        rebuildAcList();
    }
    // Freischalt-Popup unten rechts.
    const std::string png = badgeName.empty() ? std::string() : BadgePng(badgeName);
    // Der Ton erklingt mit der Einblendung, nicht schon beim Anstellen.
    popup::Zeigen(title, desc, points, png, T("ACHIEVEMENT FREIGESCHALTET!"), T("Pkt"), dpi_,
                  []() { playUnlockSound(); });

    // Diagnose: meldet, ob das Fenster wirklich erzeugt, positioniert und
    // sichtbar ist (ein Achievement wurde am 23.08. gebucht, ohne dass das
    // Popup zu sehen war).
    {
        int x = 0, y = 0, w = 0, h = 0;
        const bool sichtbar = popup::Lage(&x, &y, &w, &h);
        appendLog(Fmt("Popup: sichtbar=%1 pos=%2,%3 groesse=%4x%5 badge=%6",
                      {sichtbar ? "1" : "0", Zahl(x), Zahl(y), Zahl(w), Zahl(h), png.empty() ? "leer" : "geladen"}));
    }
}

void App::onWorkerCoreMissing() {
    ramStil_ = RamStil::Fehler;
    ramText_ = BEIDE(T("RAM: [RA-Mapper nicht geladen]"));
    Warnung(T("RA-Mapper nicht geladen"),
            T("Auf der Konsole läuft nicht unser Custom-Mapper, sondern der "
              "Standard-Core. Ohne ihn gibt es keinen Speicherspiegel und keine "
              "Achievements.\n\n"
              "Das Spiel so starten:\n\n"
              "über USB:\n"
              "  edlink run --file SPIEL.md --fpga mega-core.x25   (PRO: mega-pro.rbf)\n\n"
              "über SD-Karte:\n"
              "  mega-core.x25 (CORE) bzw. mega-pro.rbf (PRO) in denselben Ordner wie das ROM legen"));
}

void App::onWorkerConnectionLost(const std::string& reason) {
    ramStil_ = RamStil::Fehler;
    ramText_ = BEIDE(T("RAM: [Verbindung verloren - ") + reason + "]");
    monitorHint_ = BEIDE("\xE2\x9A\xA0 " + Fmt(T("Verbindung verloren: %1"), {reason}));
    monitorHintFarbe_ = kRot;
    verbindungWeg_ = true;
}

void App::onWorkerFinished() {
    // Hat sich der Worker selbst beendet (Spiel verlassen, Core fehlt),
    // laeuft sein Thread noch bis zum Ende des Rueckrufs.
    if (monWorker_) {
        if (monWorker_->warte(5000)) monWorker_.reset();
        else monWorker_.release();
    }
    if (monitoring_) {
        monitoring_ = false;
        monitorHintFarbe_ = 0;
        ramStil_ = RamStil::Text;
        ramText_ = BEIDE(T("RAM: -"));
        updateMonitorState();
        if (verbindungWeg_) {   // rot stehen lassen, damit der Grund sichtbar bleibt
            monitorHint_ = BEIDE("\xE2\x9A\xA0 " + std::string(T("Verbindung verloren - Monitor gestoppt.")));
            monitorHintFarbe_ = kRot;
        }
    }
    verbindungWeg_ = false;
}

void App::playUnlockSound() {
    // achievement.wav neben der exe, sonst Beep-Melodie.
    const std::string wav = plattform::ExeOrdner() + "/achievement.wav";
    std::error_code ec;
    if (fs::exists(fs::u8path(wav), ec)) {
        ::PlaySoundW(plattform::Breit(wav).c_str(), nullptr, SND_FILENAME | SND_ASYNC);
        return;
    }
    std::thread([]() {
        static const int melody[][2] = {{587,140},{659,140},{698,140},{784,140},{880,320},
            {1175,200},{880,160},{698,140},{784,140},{880,420}};
        for (auto& n : melody) ::Beep((DWORD)n[0], (DWORD)n[1]);
    }).detach();
}

// ============================================================================
// Badges -- wie badge_loader.cpp: Cache-Ordner badge_cache neben der Exe,
// 8 s Zeitgrenze. Die Liste laedt in einem eigenen Thread, damit ein
// langsames Netz das Fenster nicht anhaelt.
// ============================================================================

App::Badge* App::BadgeHolen(const std::string& name) {
    if (name.empty()) return nullptr;
    auto it = badges_.find(name);
    if (it != badges_.end()) return &it->second;
    Badge& b = badges_[name];
    {
        std::lock_guard<std::mutex> l(badgeMutex_);
        badgeWarteschlange_.push_back(name);
    }
    badgeCv_.notify_one();
    return &b;
}

std::string App::BadgePng(const std::string& name) {
    const std::string ordner = plattform::ExeOrdner() + "/badge_cache";
    std::error_code ec;
    fs::create_directories(fs::u8path(ordner), ec);
    const std::string lokal = ordner + "/" + name + ".png";
    std::string png = LeseDatei(lokal);
    if (png.empty()) {
        std::string body;
        if (http_get("media.retroachievements.org", "/Badge/" + name + ".png", 8000,
                     "Mozilla/5.0 (MEGA-RAW Achievement Badge Loader)", &body)) {
            std::ofstream f(fs::u8path(lokal), std::ios::binary);
            f.write(body.data(), (std::streamsize)body.size());
            png = std::move(body);
        }
    }
    return png;
}

void App::BadgeThread() {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        std::string name;
        {
            std::unique_lock<std::mutex> l(badgeMutex_);
            badgeCv_.wait(l, [this]() { return badgeEnde_ || !badgeWarteschlange_.empty(); });
            if (badgeEnde_) break;
            name = badgeWarteschlange_.front();
            badgeWarteschlange_.pop_front();
        }
        const std::string png = BadgePng(name);
        BadgeErgebnis e;
        e.name = name;
        // pm.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        e.ok = !png.empty() && bilder::PngDekodierenSkaliert(png.data(), png.size(), (int)std::lround(24 * dpi_),
                                                             &e.w, &e.h, &e.rgba);
        std::lock_guard<std::mutex> l(badgeMutex_);
        badgeFertig_.push_back(std::move(e));
    }
    ::CoUninitialize();
}

// ============================================================================
// Dialoge
// ============================================================================

void App::Info(const std::string& titel, const std::string& text, std::function<void()> ok) {
    Dialog d;
    d.art = Dialog::Art::Info;
    d.titel = titel;
    d.text = text;
    d.ja = std::move(ok);
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::Warnung(const std::string& titel, const std::string& text) {
    Dialog d;
    d.art = Dialog::Art::Warnung;
    d.titel = titel;
    d.text = text;
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::Frage(const std::string& titel, const std::string& text, std::function<void()> ja) {
    Dialog d;
    d.art = Dialog::Art::Frage;
    d.titel = titel;
    d.text = text;
    d.ja = std::move(ja);
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

namespace {

// Knoepfe mittig nebeneinander (QDialogButtonBox mit setCenterButtons).
int Knoepfe(float s, const char* a, const char* b) {
    const float w = 80 * s, abstand = 6 * s;
    const float gesamt = b ? 2 * w + abstand : w;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - gesamt) * 0.5f);
    int r = 0;
    if (ImGui::Button(a, ImVec2(w, 0))) r = 1;
    if (b) {
        ImGui::SameLine(0, abstand);
        if (ImGui::Button(b, ImVec2(w, 0))) r = 2;
    }
    return r;
}

void Zentriert(const std::string& text, ImFont* f, float groesse, ImU32 farbe) {
    if (f) ImGui::PushFont(f, schrift::Px(f, groesse));
    if (farbe) ImGui::PushStyleColor(ImGuiCol_Text, V(farbe));
    const float verfuegbar = ImGui::GetContentRegionAvail().x;
    const float start = ImGui::GetCursorPosX();
    size_t a = 0;
    for (;;) {
        const size_t e = text.find('\n', a);
        const std::string zeile = text.substr(a, e == std::string::npos ? std::string::npos : e - a);
        const float b = ImGui::CalcTextSize(zeile.c_str()).x;
        ImGui::SetCursorPosX(start + std::max(0.0f, (verfuegbar - b) * 0.5f));
        ImGui::TextUnformatted(zeile.c_str());
        if (e == std::string::npos) break;
        a = e + 1;
    }
    if (farbe) ImGui::PopStyleColor();
    if (f) ImGui::PopFont();
}

// Mehrzeilig umbrochen, jede Zeile zentriert (QLabel mit AlignCenter + wordWrap).
void ZentriertUmbrochen(const std::string& text) {
    ImFont* f = ImGui::GetFont();
    const float gr = ImGui::GetFontSize();
    const float breite = ImGui::GetContentRegionAvail().x;
    const float start = ImGui::GetCursorPosX();
    const char* a = text.c_str();
    const char* e = a + text.size();
    while (a < e) {
        const char* br = f->CalcWordWrapPosition(gr, a, e, breite);
        if (br <= a) br = a + 1;
        const char* z = br;
        while (z > a && z[-1] == ' ') --z;
        const float w = ImGui::CalcTextSize(a, z).x;
        ImGui::SetCursorPosX(start + std::max(0.0f, (breite - w) * 0.5f));
        ImGui::TextUnformatted(a, z);
        a = br;
        while (a < e && *a == ' ') ++a;
    }
}

// Eingabefeld im Stil von Windows 11: Linie unten, aktiv in Akzentfarbe.
void Unterstrich(float s) {
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const bool aktiv = ImGui::IsItemActive();
    const float d = aktiv ? 2 * s : 1 * s;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x + 1, b.y - d), ImVec2(b.x - 1, b.y),
                                              aktiv ? Rgb(0x4ca0e8) : Rgb(0xb0b0b0));
}

}  // namespace

// Feste Kennungen der beiden Katzenfenster (aus den Optionen / ueber Ko-fi).
static constexpr const char* kKatzeInnen = "###katze_innen";
static constexpr const char* kKatzeDirekt = "###katze_direkt";

void App::ZeichneDialog() {
    if (dialoge_.empty()) return;
    Dialog& d = dialoge_.front();
    const float s = dpi_;
    const std::string id = "##dlg" + Zahl(d.nummer);
    std::string titel;
    switch (d.art) {
        case Dialog::Art::Optionen: titel = T("Optionen"); break;
        case Dialog::Art::Kofi: titel = T("Ko-fi"); break;
        case Dialog::Art::Nachricht: titel = T("Nachricht"); break;
        case Dialog::Art::Ersteinrichtung: titel = "MEGA-RAW"; break;
        default: titel = d.titel; break;
    }
    // Feste Kennung: der Titel darf sich aendern (Sprachwahl), ohne dass das
    // Fenster neu aufgeht.
    titel += "###" + id;
    if (!d.geoeffnet) {
        ImGui::OpenPopup(titel.c_str());
        d.geoeffnet = true;
        if (d.art == Dialog::Art::Optionen) d.sprache = i18n::Sprache();
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 12 * s));

    bool schliessen = false;
    std::function<void()> danach;
    // Optionen und Ko-fi bekommen ein X oben rechts (wie RAW-SNES). Das X
    // schliesst ohne zu speichern, wie Abbrechen.
    bool offen = true;
    const bool mitX = d.art == Dialog::Art::Optionen || d.art == Dialog::Art::Kofi;
    if (ImGui::BeginPopupModal(titel.c_str(), mitX ? &offen : nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        switch (d.art) {
            case Dialog::Art::Info:
            case Dialog::Art::Warnung:
            case Dialog::Art::Frage: {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440 * s);
                ImGui::TextUnformatted(d.text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 8 * s));
                if (d.art == Dialog::Art::Frage) {
                    // QMessageBox::question: Yes/No (ohne Qt-Uebersetzungsdateien englisch)
                    const int r = Knoepfe(s, "Yes", "No");
                    if (r == 1) { danach = d.ja; schliessen = true; }
                    if (r == 2 || ImGui::IsKeyPressed(ImGuiKey_Escape)) schliessen = true;
                } else {
                    if (Knoepfe(s, "OK", nullptr) == 1 || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        danach = d.ja;
                        schliessen = true;
                    }
                }
                break;
            }
            case Dialog::Art::Nachricht: {
                // Reiner Text; ein Link oeffnet sich erst auf Klick.
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440 * s);
                ImGui::TextUnformatted((i18n::Englisch() ? d.textEn : d.text).c_str());
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 8 * s));
                if (!d.link.empty()) {
                    if (ImGui::Button(T("Mehr erfahren"))) plattform::UrlOeffnen(d.link);
                    ImGui::SameLine();
                }
                if (ImGui::Button("OK", ImVec2(80 * s, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    danach = d.ja;      // erst jetzt gilt sie als gelesen
                    schliessen = true;
                }
                break;
            }
            case Dialog::Art::Kofi: {
                // Kein Link im Text: erst OK oeffnet die Seite.
                Zentriert(T("aber falls du darüber nachdenkst,\nlies bitte zuerst 'über die Katze'"),
                          f_.kursiv, 12.0f, 0);
                ImGui::Dummy(ImVec2(0, 8 * s));
                const int r = Knoepfe(s, "OK", "Cancel");
                if (r == 1) { plattform::UrlOeffnen("https://ko-fi.com/liqui69747"); schliessen = true; }
                if (r == 2 || ImGui::IsKeyPressed(ImGuiKey_Escape)) schliessen = true;
                break;
            }
            case Dialog::Art::Optionen: {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(T("Sprache / Language:"));
                ImGui::SameLine();
                {
                    // Jede Sprache in ihrem eigenen Namen.
                    std::string anzeige = d.sprache;
                    for (const i18n::SpracheInfo& si : i18n::Sprachen())
                        if (d.sprache == si.kennung) anzeige = si.name;
                    ImGui::SetNextItemWidth(170 * s);
                    if (ImGui::BeginCombo("##sprache", anzeige.c_str())) {
                        for (const i18n::SpracheInfo& si : i18n::Sprachen())
                            if (ImGui::Selectable(si.name, d.sprache == si.kennung)) d.sprache = si.kennung;
                        ImGui::EndCombo();
                    }
                }
                ImGui::Dummy(ImVec2(0, 2 * s));

                const float breit = std::max(300 * s, ImGui::GetContentRegionAvail().x);
                bool speichern = false;
                const int r = Knoepfe(s, "Save", "Cancel");
                if (r == 1) speichern = true;
                if (r == 2 || ImGui::IsKeyPressed(ImGuiKey_Escape)) schliessen = true;

                if (ImGui::Button(T("Fehler melden"), ImVec2(breit, 0)))
                    plattform::UrlOeffnen("https://github.com/liquid-wq/mega-raw/issues");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Öffnet die Fehlerliste auf GitHub im Browser."));

                bool wiederherstellen = false;
                if (ImGui::Button(T("Original-Mapper wiederherstellen"), ImVec2(breit, 0))) {
                    wiederherstellen = true;
                    speichern = true;      // dlg.accept()
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", T("Schreibt die beim Einrichten gesicherten Originaldateien "
                                              "auf die SD-Karte zurück."));

                if (ImGui::Button(T("über die Katze"), ImVec2(breit, 0))) ImGui::OpenPopup(kKatzeInnen);
                ZeichneKatze(kKatzeInnen);

                if (speichern) {
                    // Die Sprache wirkt sofort - jedes Bild wird neu uebersetzt.
                    // Den Neustart-Hinweis der Qt-Fassung braucht es deshalb nicht.
                    i18n::SpracheSetzen(d.sprache);
                    settings_.language = i18n::Sprache();
                    settings_.save(settingsPath_);
                    rebuildAcList();   // Zusaetze an den Achievements in der neuen Sprache
                    schliessen = true;
                    danach = [this, wiederherstellen]() {
                        if (wiederherstellen) onRestoreMappers();
                        appendLog(BEIDE(T("Einstellungen gespeichert.")));
                    };
                }
                break;
            }
            case Dialog::Art::Katze:
                schliessen = true;
                break;
            case Dialog::Art::Ersteinrichtung: {
                // Wie RAW-TURN: beim allerersten Start zuerst die Sprache,
                // dann der Weg zum Einrichten der SD-Karte.
                auto knopf = [&](const char* text) {
                    const float w = std::max(110 * s, ImGui::CalcTextSize(text).x + 24 * s);
                    return ImGui::Button(text, ImVec2(w, 0));
                };
                if (d.schritt == 0) {
                    ImGui::TextUnformatted("Sprache / Language / Idioma / Langue / Lingua / J\xC4\x99zyk");
                    ImGui::Dummy(ImVec2(0, 4 * s));
                    for (const i18n::SpracheInfo& si : i18n::Sprachen()) {
                        if (ImGui::Selectable(si.name, d.sprache == si.kennung, 0, ImVec2(320 * s, 0))) {
                            d.sprache = si.kennung;
                            i18n::SpracheSetzen(d.sprache);   // sofort sichtbar
                        }
                    }
                    ImGui::Dummy(ImVec2(0, 8 * s));
                    if (knopf(T("Weiter")) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        i18n::SpracheSetzen(d.sprache);
                        settings_.language = i18n::Sprache();
                        settings_.save(settingsPath_);
                        d.schritt = 1;
                    }
                } else {
                    Zentriert(T("Willkommen bei MEGA-RAW"), f_.fett, 15.0f, 0);
                    ImGui::Dummy(ImVec2(0, 6 * s));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440 * s);
                    ImGui::TextUnformatted(T("Damit Achievements funktionieren, müssen die Mapper-Dateien einmalig "
                                             "auf die SD-Karte des EverDrive (mega-core.x25 für das CORE, "
                                             "mega-pro.rbf für das PRO).\n\n"
                                             "1. SD-Karte aus der Konsole nehmen\n"
                                             "2. In den Kartenleser am Computer stecken\n"
                                             "3. Im nächsten Fenster die Karte auswählen\n\n"
                                             "Den Rest erledigt das Programm."));
                    ImGui::PopTextWrapPos();
                    ImGui::Dummy(ImVec2(0, 8 * s));
                    if (knopf(T("Mapper jetzt einrichten"))) {
                        schliessen = true;
                        danach = [this]() { onSetupMapper(); };
                    }
                    ImGui::SameLine();
                    if (knopf(T("Später")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        schliessen = true;
                        danach = [this]() {
                            appendLog(BEIDE(T("Die Einrichtung geht jederzeit über \"Mapper auf SD-Karte einrichten...\".")));
                        };
                    }
                }
                break;
            }
        }
        if (schliessen) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    if (mitX && !offen) schliessen = true;   // X: wie Abbrechen
    if (schliessen) {
        dialoge_.pop_front();
        if (danach) danach();
    }
}

// ============================================================================
// Zeichnen
// ============================================================================

void App::GruppeBeginn(const char* id, const std::string& titel) {
    // QGroupBox: Rahmen, Titel links oben im Rahmen.
    const float s = dpi_;
    ImGui::PushID(id);
    Gruppe g;
    g.start = ImGui::GetCursorScreenPos();
    g.breite = ImGui::GetContentRegionAvail().x;
    g.titel = titel;
    const ImVec2 ts = ImGui::CalcTextSize(titel.c_str());
    g.titelBreite = ts.x;
    g.titelHoehe = ts.y;
    ImGui::SetCursorScreenPos(ImVec2(g.start.x + 9 * s, g.start.y + ts.y + 10 * s));
    ImGui::BeginGroup();
    gruppen_.push_back(std::move(g));
}

void App::GruppeEnde() {
    const float s = dpi_;
    ImGui::EndGroup();
    const Gruppe g = gruppen_.back();
    gruppen_.pop_back();
    const float unten = ImGui::GetItemRectMax().y + 10 * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = g.start.x, y = g.start.y;
    dl->AddRect(ImVec2(x + 0.5f, y + g.titelHoehe * 0.5f), ImVec2(x + g.breite - 0.5f, unten), kRahmen, 4 * s);
    dl->AddRectFilled(ImVec2(x + 7 * s, y), ImVec2(x + 11 * s + g.titelBreite, y + g.titelHoehe), kFenster);
    dl->AddText(ImVec2(x + 9 * s, y), kText, g.titel.c_str());
    ImGui::SetCursorScreenPos(ImVec2(x, unten));
    ImGui::Dummy(ImVec2(g.breite, 0.5f));
    ImGui::PopID();
}

void App::ZeichneMenue() {
    if (!ImGui::BeginMenuBar()) return;
    // QMenuBar: Eintraege mit breitem Abstand.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(30 * dpi_, ImGui::GetStyle().ItemSpacing.y));
    if (ImGui::MenuItem(T("Optionen"))) {
        Dialog d;
        d.art = Dialog::Art::Optionen;
        d.nummer = ++dialogNummer_;
        dialoge_.push_back(std::move(d));
    }
    if (ImGui::MenuItem(T("Ko-fi"))) katzeDirekt_ = true;
    ImGui::PopStyleVar();
    ImGui::EndMenuBar();
}

void App::ZeichneRam(float breite) {
    // ramLabel_: "background-color: #0a0e14; border: 1px solid #2a3a4a;
    // border-radius: 4px; padding: 6px 10px; color: #3dd68c;", mindestens
    // 28 hoch, bricht um.
    const float s = dpi_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float padX = 10 * s, padY = 6 * s;
    const float innen = breite - 2 * padX;

    ImU32 grund = Rgb(0x0a0e14), rand = Rgb(0x2a3a4a);
    if (ramStil_ == RamStil::Fehler) { grund = Rgb(0x1a0a0a); rand = Rgb(0x6a2a2a); }

    float hoehe = 0;
    struct Teil { std::string t; ImVec2 pos; ImU32 farbe; bool fett; };
    std::vector<Teil> teile;
    ImFont* sf = nullptr;
    float gr = 0;

    if (ramStil_ == RamStil::Bytes) {
        // "font-family:'Consolas'; font-size:11pt; letter-spacing:3px", je
        // Byte zwei Ziffern und ein Leerzeichen.
        sf = f_.consolas;
        gr = schrift::Px(sf, 11.0f * 96.0f / 72.0f) * s;
        const float sperr = 3.0f * s;
        const float zw = sf->CalcTextSizeA(gr, FLT_MAX, 0, "0").x + sperr;
        const float zh = sf->CalcTextSizeA(gr, FLT_MAX, 0, "0").y;
        const long long now = JetztMs();
        float x = 0, y = 0;
        for (size_t i = 0; i < lastRamBytes_.size(); ++i) {
            const float w = 3 * zw;   // zwei Ziffern + Leerzeichen
            if (x > 0 && x + 2 * zw > innen) { x = 0; y += zh; }
            const long long seit = i < lastByteChangeMs_.size() ? lastByteChangeMs_[i] : 0;
            const long long age = now - seit;
            ImU32 c = Rgb(0x3dd68c);
            bool fett = false;
            if (seit > 0 && age < 2000) {
                // 0-1000ms: voll rot. 1000-2000ms: Verlauf rot -> gruen.
                const double t = (age < 1000) ? 0.0 : (age - 1000) / 1000.0;
                const int r = (int)(0xc0 + t * (0x3d - 0xc0));
                const int g = (int)(0x39 + t * (0xd6 - 0x39));
                const int b = (int)(0x2b + t * (0x8c - 0x2b));
                c = IM_COL32(r, g, b, 255);
                fett = true;
            }
            const std::string hex = i18n::Hex(lastRamBytes_[i], 2, true);
            teile.push_back({hex.substr(0, 1), ImVec2(x, y), c, fett});
            teile.push_back({hex.substr(1, 1), ImVec2(x + zw, y), c, fett});
            x += w;
        }
        hoehe = (lastRamBytes_.empty() ? zh : y + zh) + 2 * padY;
    } else {
        sf = ramStil_ == RamStil::Fehler ? f_.fett : f_.ui;
        gr = schrift::Px(sf, 12.0f) * s;
        const std::string text = ramText_.text();
        const ImVec2 ts = sf->CalcTextSizeA(gr, FLT_MAX, innen, text.c_str());
        teile.push_back({text, ImVec2(0, 0), ramStil_ == RamStil::Fehler ? kRot : Rgb(0x3dd68c), false});
        hoehe = ts.y + 2 * padY;
    }
    hoehe = std::max(hoehe, 28 * s);
    const ImVec2 p1(p0.x + breite, p0.y + hoehe);
    dl->AddRectFilled(p0, p1, grund, 4 * s);
    dl->AddRect(p0, p1, rand, 4 * s);
    for (const Teil& t : teile) {
        ImFont* f = (t.fett && ramStil_ == RamStil::Bytes) ? f_.consolasFett : sf;
        dl->AddText(f, gr, ImVec2(p0.x + padX + t.pos.x, p0.y + padY + t.pos.y), t.farbe, t.t.c_str(), nullptr,
                    ramStil_ == RamStil::Bytes ? 0.0f : innen);
    }
    ImGui::Dummy(ImVec2(breite, hoehe));
}

void App::ZeichneListe(float hoehe) {
    // acList_: QListWidget mit wechselnden Zeilenfarben, 32 hohe Zeilen,
    // Badge 24x24, Doppelklick oeffnet die Seite des Achievements.
    const float s = dpi_;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kListe));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1 * s, 1 * s));
    ImGui::BeginChild("acList", ImVec2(0, hoehe), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const float zh = 32 * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < (int)liste_.size(); ++i) {
        const ListZeile& z = liste_[(size_t)i];
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = std::max(ImGui::GetContentRegionAvail().x, ImGui::CalcTextSize(z.text.c_str()).x + 40 * s);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + zh), (i % 2) ? kListeAlt : kListe);
        ImGui::PushID(i);
        if (ImGui::Selectable("##z", auswahl_ == i, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(w, zh))) {
            auswahl_ = i;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && z.id > 0)
                plattform::UrlOeffnen("https://retroachievements.org/achievement/" + Zahl(z.id));
        }
        ImGui::PopID();
        float x = p.x + 4 * s;
        if (!z.badge.empty()) {
            Badge* b = BadgeHolen(z.badge);
            if (b && b->zustand == Badge::Zustand::Fertig && b->tex.ok()) {
                const float k = 24 * s;
                const float sc = std::min(k / b->tex.w, k / b->tex.h);
                const float bw = b->tex.w * sc, bh = b->tex.h * sc;
                const ImVec2 b0(std::floor(x + (k - bw) / 2), std::floor(p.y + (zh - bh) / 2));
                dl->AddImage((ImTextureID)(intptr_t)b->tex.id, b0, ImVec2(b0.x + bw, b0.y + bh), ImVec2(0, 0),
                             ImVec2(b->tex.u, b->tex.v));
            }
            x += 24 * s + 4 * s;
        }
        const std::string text = z.id < 0 ? std::string(T(z.text.c_str())) : z.text;
        const float th = ImGui::GetTextLineHeight();
        dl->AddText(ImVec2(x, p.y + (zh - th) / 2), z.farbe ? z.farbe : kText, text.c_str());
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::ZeichneLog(float hoehe) {
    // log_: QPlainTextEdit, nur lesen, bricht an der Breite um.
    const float s = dpi_;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kFeld));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * s, 3 * s));
    const float knopfHoehe = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("log", ImVec2(0, std::max(40 * s, hoehe - knopfHoehe)),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushTextWrapPos(0.0f);
    size_t anzahl = 0;
    {
        std::lock_guard<std::mutex> l(logMutex_);
        for (const Zweisprachig& z : log_) ImGui::TextUnformatted(z.text().c_str());
        anzahl = log_.size();
    }
    ImGui::PopTextWrapPos();
    ImGui::PopStyleVar();
    // Neue Zeilen: ans Ende scrollen, solange man unten war.
    if (anzahl != logZeilenGezeichnet_) {
        if (logAmEnde_) ImGui::SetScrollHereY(1.0f);
        logZeilenGezeichnet_ = anzahl;
    } else {
        logAmEnde_ = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    // Ganzes Log in die Zwischenablage (fuer Fehlermeldungen), kurz "Kopiert!".
    const bool frisch = logKopiertZeit_ > 0 && ImGui::GetTime() - logKopiertZeit_ < 2.0;
    const std::string knopf = std::string(frisch ? T("Kopiert!") : T("Log kopieren")) + "###logkopieren";
    if (ImGui::Button(knopf.c_str())) {
        std::string alles;
        {
            std::lock_guard<std::mutex> l(logMutex_);
            for (const Zweisprachig& z : log_) { alles += z.text(); alles += "\r\n"; }
        }
        ImGui::SetClipboardText(alles.c_str());
        logKopiertZeit_ = ImGui::GetTime();
    }
}

void App::Zeichnen(ImVec2 pos, ImVec2 groesse) {
    // Auftraege aus Worker und Hilfsthreads.
    {
        std::vector<std::function<void()>> q;
        {
            std::lock_guard<std::mutex> l(post_->m);
            q.swap(post_->auftraege);
        }
        for (auto& f : q) f();
    }
    // Fertig geladene Badges als Textur hochladen (nur hier: OpenGL-Thread).
    {
        std::vector<BadgeErgebnis> fertig;
        {
            std::lock_guard<std::mutex> l(badgeMutex_);
            fertig.swap(badgeFertig_);
        }
        for (auto& e : fertig) {
            Badge& b = badges_[e.name];
            if (e.ok) {
                b.tex = bilder::Hochladen(e.rgba.data(), e.w, e.h, true);
                b.zustand = Badge::Zustand::Fertig;
            } else {
                b.zustand = Badge::Zustand::Fehlt;
            }
        }
    }

    const float s = dpi_;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(groesse);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 2 * s));   // setContentsMargins(8, 2, 8, 8)
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * s, 4 * s));     // setSpacing(4)
    // Hoehe der Menueleiste = Schrift + 2 x FramePadding.y (QMenuBar unter Windows 11).
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 8 * s));
    // QMenuBar hat keine Trennlinie nach unten; Dear ImGui zieht sie bei
    // FrameBorderSize > 0.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::Begin("##megaraw", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PopStyleVar(2);   // FramePadding und Rahmen der Menueleiste
    ZeichneMenue();

    // Pixelart (Jason) ueber dem Copyright-Hinweis, 53 hoch.
    if (jason_.ok()) {
        // Bereits auf Zielgroesse skaliert: 1:1 und auf ganze Pixel gesetzt.
        const float h = (float)jason_.h, w = (float)jason_.w;
        ImGui::SetCursorPosX(std::floor(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - w) * 0.5f));
        ImGui::Image((ImTextureID)(intptr_t)jason_.id, ImVec2(w, h), ImVec2(0, 0), ImVec2(jason_.u, jason_.v));
    }
    // Urheberrechtshinweis: "color: #888888; font-size: 8pt;"
    Zentriert("MEGA-RAW FPGA \xE2\x80\x94 \xC2\xA9 2026 Liqui", f_.ui, 8.0f * 96 / 72, kCopyright);

    // Schnellstart: "color: #40c060; font-family: Courier; font-size: 8pt;", bricht um.
    {
        ImGui::PushFont(f_.courier, schrift::Px(f_.courier, 8.0f * 96 / 72));
        ImGui::PushStyleColor(ImGuiCol_Text, V(kSchnellstart));
        // setAlignment(Qt::AlignCenter) + setWordWrap(true): jede Zeile zentriert.
        ZentriertUmbrochen(T("Schnellstart:  1. Mapper auf SD einrichten   2. Einloggen   3. Spiel auf der Konsole starten   4. Monitor starten"));
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    const float sp = ImGui::GetStyle().ItemSpacing.x;

    // --- Login ---
    GruppeBeginn("login", T("RetroAchievements-Login"));
    {
        const float innen = ImGui::GetContentRegionAvail().x - 9 * s;
        const bool angemeldet = !token_.empty();
        const char* lbl = angemeldet ? T("Abmelden") : T("Login");
        const float bw = std::max(std::max(ImGui::CalcTextSize(T("Login")).x, ImGui::CalcTextSize(T("Abmelden")).x) +
                                      2 * ImGui::GetStyle().FramePadding.x,
                                  75 * s);
        const char* chk = T("Eingeloggt bleiben");
        const float cw = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(chk).x;
        const float ew = (innen - bw - cw - 3 * sp) / 2;
        ImGui::BeginDisabled(angemeldet || loginLaeuft_);
        ImGui::SetNextItemWidth(ew);
        ImGui::InputTextWithHint("##user", T("Benutzername"), userBuf_, sizeof(userBuf_));
        Unterstrich(s);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ew);
        ImGui::InputTextWithHint("##pass", T("Passwort"), passBuf_, sizeof(passBuf_), ImGuiInputTextFlags_Password);
        Unterstrich(s);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(loginLaeuft_);
        if (ImGui::Button((std::string(lbl) + "###login").c_str(), ImVec2(bw, 0))) onLogin();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Checkbox((std::string(chk) + "###merken").c_str(), &merken_)) speichereLogin();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", T("Benutzername und Anmeldeschlüssel speichern (das Passwort wird nicht gespeichert)"));
    }
    GruppeEnde();
    ZeichneLoginStatus();

    // --- ROM ---
    {
        const char* lbl = T("ROM wählen...");
        const float bw = ImGui::CalcTextSize(lbl).x + 2 * ImGui::GetStyle().FramePadding.x + 16 * s;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - sp);
        std::string anzeige = romPathText_.text();
        std::vector<char> puffer(anzeige.begin(), anzeige.end());
        puffer.push_back(0);
        ImGui::InputTextWithHint("##rom", T("Keine ROM gewählt"), puffer.data(), puffer.size(),
                                 ImGuiInputTextFlags_ReadOnly);
        Unterstrich(s);
        ImGui::SameLine();
        if (ImGui::Button((std::string(lbl) + "###rom").c_str(), ImVec2(bw, 0))) onChooseRom();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Spiel wählen (ROM/ZIP/RAR) und Achievement-Set laden."));
    }

    if (ImGui::Button((std::string(T("Mapper auf SD-Karte einrichten...")) + "###mapper").c_str(),
                      ImVec2(-FLT_MIN, 0)))
        onSetupMapper();
    if (ImGui::IsItemHovered()) {
        ImGui::SetNextWindowSize(ImVec2(420 * s, 0));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(T("Durchsucht die SD-Karte nach Ordnern mit ROMs und legt "
                                 "mega-core.x25 (CORE) und mega-pro.rbf (PRO) in jeden davon. "
                                 "Ohne diese Dateien lädt die Konsole den Standard-Mapper und "
                                 "es gibt keine Achievements."));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    // statusLabel_: Courier 9pt, gruen/rot/grau, sonst Standard.
    {
        const bool courier = statusStil_ != StatusStil::Normal;
        if (courier) ImGui::PushFont(f_.courier, schrift::Px(f_.courier, 12.0f));
        const ImU32 c = statusStil_ == StatusStil::Gruen ? kOk
                      : statusStil_ == StatusStil::Rot   ? kRot
                      : statusStil_ == StatusStil::Grau  ? Rgb(0xcccccc) : kText;
        ImGui::PushStyleColor(ImGuiCol_Text, V(c));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(statusText_.text().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (courier) ImGui::PopFont();
    }

    // --- Monitor / Hardware ---
    GruppeBeginn("monitor", T("Live-Monitor (EverDrive)"));
    {
        const float innen = ImGui::GetContentRegionAvail().x - 9 * s;
        const float x0 = ImGui::GetCursorPosX();
        const std::string lbl = OhneMnemonik(T(monitoring_ ? "Monitor stoppen" : "Verbinden && Monitor starten"));
        const float bw = ImGui::CalcTextSize(lbl.c_str()).x + 2 * ImGui::GetStyle().FramePadding.x + 16 * s;
        // Lange Statustexte brechen links vom Knopf um, statt ihn hinauszuschieben.
        const ImVec2 start = ImGui::GetCursorPos();
        ImGui::PushTextWrapPos(x0 + std::max(80 * s, innen - bw - 12 * s));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(edStatus_.text().c_str());
        ImGui::SameLine(0, 12 * s);
        ImGui::PushStyleColor(ImGuiCol_Text, V(monitorHintFarbe_ ? monitorHintFarbe_ : kText));
        ImGui::TextUnformatted(monitorHint_.text().c_str());
        ImGui::PopStyleColor();
        // Hardcore-Haken: verborgen, solange kHardcoreAktiv false ist.
        if (kHardcoreAktiv) {
            ImGui::SameLine(0, 12 * s);
            if (ImGui::Checkbox(T("Hardcore"), &settings_.hardcore)) settings_.save(settingsPath_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", T("Hardcore erfordert, dass In-Game-Menü (Savestates) UND Cheats "
                                          "im EverDrive-System-Menü abgeschaltet sind. Ist eines aktiv, "
                                          "läuft die Sitzung als Softcore. Wirkt ab dem nächsten Monitor-Start."));
        }
        ImGui::PopTextWrapPos();
        const float yText = ImGui::GetCursorPosY();
        // Rechtsbuendig im Rahmen, auf Hoehe der ersten Zeile.
        ImGui::SetCursorPos(ImVec2(x0 + innen - bw, start.y));
        ImGui::BeginDisabled(!monitorBtnEnabled_);
        if (ImGui::Button((lbl + "###monitor").c_str(), ImVec2(bw, 0))) onToggleMonitor();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetNextWindowSize(ImVec2(420 * s, 0));
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(T("Startet die Live-Überwachung: liest per USB den Spielstand und schaltet Achievements frei. Spiel auf der Konsole starten."));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        // Ist der Text hoeher als der Knopf, den Rest mit einem Platzhalter
        // belegen (ImGui verlangt nach dem Versetzen ein Element).
        const float fehlt = yText - ImGui::GetCursorPosY();
        if (fehlt > 0) ImGui::Dummy(ImVec2(1, fehlt - ImGui::GetStyle().ItemSpacing.y));
    }
    GruppeEnde();

    ZeichneRam(ImGui::GetContentRegionAvail().x);

    // Liste mit Stretch 1, Log in seiner Standardhoehe darunter.
    const float logHoehe = 188 * s;
    const float rest = ImGui::GetContentRegionAvail().y - logHoehe - ImGui::GetStyle().ItemSpacing.y - 8 * s;
    ZeichneListe(std::max(60 * s, rest));
    ZeichneLog(logHoehe);

    ImGui::End();
    ImGui::PopStyleVar(2);

    ZeichneDialog();
    if (katzeDirekt_) { ImGui::OpenPopup(kKatzeDirekt); katzeDirekt_ = false; }
    ZeichneKatze(kKatzeDirekt);
}

// Fenster "ueber die Katze": Katzentext unveraendert, darunter der Hinweis
// auf Tierheime und - nur zusaetzlich - der Link zu Ko-fi.
void App::ZeichneKatze(const char* id) {
    const float s = dpi_;
    ImGui::SetNextWindowSize(ImVec2(520 * s, 720 * s), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const std::string kt = std::string(T("über die Katze")) + id;
    bool offen = true;   // X oben rechts
    if (!ImGui::BeginPopupModal(kt.c_str(), &offen, ImGuiWindowFlags_NoSavedSettings)) return;
    // Portrait gross, darunter Name und Jahre (wie RAW-NES).
    if (jasonGross_.ok()) {
        const float w = (float)jasonGross_.w, h = (float)jasonGross_.h;
        ImGui::SetCursorPosX(std::floor(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - w) * 0.5f));
        ImGui::Image((ImTextureID)(intptr_t)jasonGross_.id, ImVec2(w, h), ImVec2(0, 0),
                     ImVec2(jasonGross_.u, jasonGross_.v));
    }
    Zentriert("In memory of Jason", f_.fett, 14.0f * 96 / 72, kOk);
    Zentriert("2010 \xE2\x80\x93 2025", f_.ui, 11.0f * 96 / 72, kCopyright);
    ImGui::Dummy(ImVec2(0, 4 * s));
    const float unten = ImGui::GetFrameHeightWithSpacing() + 6 * s;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kFeld));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * s, 6 * s));
    ImGui::BeginChild("katzentext", ImVec2(0, ImGui::GetContentRegionAvail().y - unten),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushTextWrapPos(0.0f);
    // Der Text bleibt wortwoertlich wie geschrieben: deutsch
    // oder englisch, fuer alle weiteren Sprachen englisch.
    ImGui::TextUnformatted(settings_.language == "de" ? kKatzeText : kKatzeTextEn);
    ImGui::Dummy(ImVec2(0, 6 * s));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(T("Falls Sie kein Tier in Ihrer Familie haben: Auch die Tiere in den Tierheimen freuen sich, denn sie sind ebenfalls auf Hilfe angewiesen."));
    {
        // "zusaetzlich" unterstreichen: erst die Tiere, das Projekt nur obendrein.
        const char* satz = T("Und wer zusätzlich dieses Projekt unterstützen oder aus Wertschätzung etwas dalassen möchte, kann das hier tun:");
        const char* wort = T("zusätzlich");
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::TextUnformatted(satz);
        const std::string st(satz);
        const size_t pos = st.find(wort);
        if (pos != std::string::npos) {
            const float x0 = ImGui::CalcTextSize(satz, satz + pos).x;
            const float x1 = x0 + ImGui::CalcTextSize(wort).x;
            if (x1 <= ImGui::GetContentRegionAvail().x) {   // Wort steht in der ersten Zeile
                const float y = p.y + ImGui::GetFontSize();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + x0, y), ImVec2(p.x + x1, y),
                                                    ImGui::GetColorU32(ImGuiCol_Text), 1.0f * s);
            }
        }
    }
    ImGui::PopStyleColor();
    if (ImGui::TextLink("Ko-fi")) plattform::UrlOeffnen("https://ko-fi.com/liqui69747");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (ImGui::Button(T("OK"), ImVec2(-FLT_MIN, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace megaraw
