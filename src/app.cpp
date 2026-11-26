#include "app.h"

#include "archiv.h"
#include "eigene_kerne.h"
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
#include <chrono>
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

// Stammt eine Kerndatei dieser Groesse von MEGA-RAW (alte oder aktuelle Fassung)?
// Solche Dateien sind keine Originale: nicht sichern, nicht zurueckschreiben.
bool UsbEigenerKern(uint64_t groesse, const std::vector<size_t>& aktuell) {
    for (size_t g : aktuell) if (g != 0 && g == groesse) return true;
    for (uint32_t g : kEigeneKernGroessen) if (g == groesse) return true;
    return false;
}

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

// Nur Testbau (test.ini neben der Exe): Zeile sofort nach test_log.txt
static void TestLogZeile(const std::string& text) {
    static const bool testLog = [] {
        std::error_code ec;
        return fs::exists(fs::u8path(plattform::ExeOrdner() + "/test.ini"), ec);
    }();
    if (!testLog) return;
    static std::mutex m;
    std::lock_guard<std::mutex> l(m);
    static std::ofstream f(fs::u8path(plattform::ExeOrdner() + "/test_log.txt"), std::ios::app);
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char zeit[16] = {};
    std::strftime(zeit, sizeof(zeit), "%H:%M:%S ", std::localtime(&t));
    f << zeit << text << "\n";
    f.flush();
}

void App::appendLog(const Zweisprachig& line) {
    std::lock_guard<std::mutex> l(logMutex_);
    log_.push_back(line);
    if (log_.size() > 20000) log_.pop_front();
    TestLogZeile(line.text());
}

// Leerlauf-Verbindung des automatischen Starts: bleibt offen (wie RAW-NES),
// statt alle 3 s neu geoeffnet zu werden. Vor jedem anderen Zugriff auf den
// Anschluss (find_everdrive) wird sie geschlossen.
namespace {
struct LeerlaufVerbindung {
    std::mutex m;
    std::unique_ptr<EdSerial> ed;
};
LeerlaufVerbindung& Leerlauf() { static LeerlaufVerbindung l; return l; }
// Steckt ein Mega EverDrive am USB (Kennung 38DF:0018)? Dann darf die
// Einrichtung nicht still auf das Laufwerk ausweichen, wenn er nicht antwortet.
bool EverDriveAmUsb() {
    for (const auto& p : availableSerialPorts())
        if (p.hatKennung && p.vid == 0x38DF && p.pid == 0x0018) return true;
    return false;
}
std::string LHx(int v) { char t[8]; std::snprintf(t, sizeof(t), "%02x", v & 0xFF); return t; }
void LeerlaufSchliessen() {
    std::lock_guard<std::mutex> g(Leerlauf().m);
    if (Leerlauf().ed) {
        Leerlauf().ed->close();
        Leerlauf().ed.reset();
        TestLogZeile("LEERLAUF: Anschluss freigegeben");
    }
}
}  // namespace

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
    // waeren das dutzende Ordner - deshalb automatisch. Zuerst ueber USB
    // (Karte bleibt im EverDrive); ohne Verbindung ueber das Laufwerk.
    std::vector<uint8_t> src = coreDaten();
    if (!src.empty()) { einrichtenUsbSuchen(std::move(src)); return; }
    onSetupMapperLaufwerk({});
}

void App::onSetupMapperLaufwerk(std::vector<uint8_t> src) {
    if (src.empty()) src = coreDaten();
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
            if (fs::exists(dst, ec) && !UsbEigenerKern(fs::file_size(dst, ec), {k.second.size()})) {
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
             (saved ? std::string("\n\n") + T("Vorhandene Kerne wurden gesichert und lassen "
                                               "sich in den Optionen wiederherstellen.")
                    : std::string()));
}

// ----------------------------------------------------------------------------
// Einrichtung ueber USB: die Karte bleibt im EverDrive, kein Laufwerk noetig.
// Nur im EverDrive-Menue (nicht bei laufendem Spiel).
// ----------------------------------------------------------------------------
namespace {

const std::set<std::string> kRomExt = {".md", ".bin", ".gen", ".smd", ".68k", ".sgd", ".zip", ".7z", ".rar"};
const char* const kKernNamen[] = {"mega-core.x25", "mega-pro.rbf"};

std::string UsbPfad(const std::string& eltern, const std::string& name) {
    return (eltern.empty() || eltern == "/") ? name : eltern + "/" + name;
}

std::string UsbEndung(const std::string& name) {
    const size_t p = name.rfind('.');
    return p == std::string::npos ? std::string() : Kleinschrift(name.substr(p));
}

// Ordner der Wurzel, die nie Spielordner sind: Krikzz' System (MEGA, darin
// liegen seine eigenen Kerne unter demselben Namen), unsere Sicherung und
// Windows-Verwaltung.
bool UsbAusgenommen(const std::string& eltern, const std::string& name) {
    if (name.empty() || name[0] == '.' || name[0] == '$') return true;
    if (eltern != "/") return false;
    const std::string n = Kleinschrift(name);
    return n == "mega" || n == Kleinschrift(kBackupDir) || n == "system volume information";
}

// Ordner einer Datei ("ROMS/S/mega-core.x25" -> "ROMS/S", "a.x25" -> "/").
std::string UsbOrdnerVon(const std::string& datei) {
    const size_t p = datei.rfind('/');
    return p == std::string::npos ? std::string("/") : datei.substr(0, p);
}

// Laeuft unser Kern (Spiel gestartet)? 0/FF = EverDrive-Menue oder fremder Kern.
bool UsbSpielLaeuft(EdSerial& ed) {
    try {
        const Bytes b = ed.memrd(mdsnoop::kAddrSnoop + mdsnoop::kCtlOffset, 1);
        return !b.empty() && b[0] > 0 && b[0] < 0xFF;
    } catch (...) {
        return false;
    }
}

// Ordnerkette anlegen; 8 = gibt es schon.
bool UsbOrdnerKette(EdSerial& ed, const std::string& pfad) {
    std::string teil;
    size_t a = 0;
    while (a <= pfad.size()) {
        const size_t e = pfad.find('/', a);
        teil = pfad.substr(0, e == std::string::npos ? std::string::npos : e);
        const int c = ed.ordnerAnlegen(teil);
        if (c != 0 && c != 8) return false;
        if (e == std::string::npos) break;
        a = e + 1;
    }
    return true;
}

}  // namespace

void App::einrichtenUsbSuchen(std::vector<uint8_t> core) {
    if (monitoring_) {
        Info(T("Mapper einrichten"),
             T("Bitte zuerst den Monitor stoppen und die Konsole ins EverDrive-Menü zurückbringen "
               "(Reset), dann erneut einrichten."));
        return;
    }
    if (usbEinrichtungLaeuft_.exchange(true)) return;
    stopMonitorIfRunning();
    appendLog(BEIDE(T("Suche die SD-Karte über USB...")));
    std::thread([this, core]() {
        auto [ed, port] = find_everdrive();
        if (!ed && EverDriveAmUsb()) {
            usbEinrichtungLaeuft_ = false;
            Post([this]() { AnschlussBelegt(T("Mapper einrichten")); });
            return;
        }
        if (!ed) {
            usbEinrichtungLaeuft_ = false;
            Post([this, core]() {
                appendLog(BEIDE(T("Kein EverDrive über USB gefunden - Einrichtung über das Laufwerk.")));
                onSetupMapperLaufwerk(core);
            });
            return;
        }
        if (UsbSpielLaeuft(*ed)) {
            ed->close();
            usbEinrichtungLaeuft_ = false;
            Post([this]() {
                Info(T("Mapper einrichten"),
                     T("Auf der Konsole läuft gerade ein Spiel. Bitte Reset drücken, damit das "
                       "EverDrive-Menü erscheint, und dann erneut einrichten."));
            });
            return;
        }

        // Alle Ordner durchgehen. Ziel ist jeder Ordner mit einem ROM - und
        // jeder, in dem schon ein Kern liegt (sonst bliebe dort ein alter
        // Stand stehen, z. B. in ROMS selbst).
        std::vector<std::string> ziele;
        const std::vector<uint8_t> pro = proDaten();
        int schonAktuell = 0;   // Ordner, in denen die aktuellen Kerne schon liegen
        bool fehler = false;
        std::vector<std::pair<std::string, int>> offen{{"/", 0}};
        int gelesen = 0;
        while (!offen.empty() && gelesen < 3000) {
            const auto [pfad, tiefe] = offen.back();
            offen.pop_back();
            std::vector<EdSerial::Eintrag> l;
            const int c = ed->ordnerListe(pfad, l);
            if (c < 0) { fehler = true; break; }
            if (c != 0) continue;
            ++gelesen;
            usbZaehler_ = gelesen;
            bool ziel = false;
            for (const auto& e : l) {
                if (e.ordner) {
                    if (tiefe < 8 && !UsbAusgenommen(pfad, e.name)) offen.push_back({UsbPfad(pfad, e.name), tiefe + 1});
                    continue;
                }
                const std::string n = Kleinschrift(e.name);
                if (kRomExt.count(UsbEndung(n)) || n == kKernNamen[0] || n == kKernNamen[1]) ziel = true;
            }
            if (!ziel) continue;
            // Liegen dort schon beide aktuellen Kerne (Inhalt verglichen - gleiche
            // Groesse allein beweist nichts), bleibt der Ordner unberuehrt.
            bool aktuell = true;
            for (const auto* k : {&core, &pro}) {
                if (k->empty()) continue;
                const std::string name = k == &core ? kKernNamen[0] : kKernNamen[1];
                const EdSerial::Eintrag* da = nullptr;
                for (const auto& e : l)
                    if (!e.ordner && Kleinschrift(e.name) == name) da = &e;
                Bytes inhalt;
                if (!da || da->groesse != k->size() ||
                    ed->dateiLesen(UsbPfad(pfad, name), (uint32_t)k->size(), inhalt) != 0 || inhalt != *k) {
                    aktuell = false;
                    break;
                }
            }
            if (aktuell) ++schonAktuell; else ziele.push_back(pfad);
        }
        ed->close();
        std::sort(ziele.begin(), ziele.end(), [](const std::string& a, const std::string& b) {
            return Kleinschrift(a) < Kleinschrift(b);
        });
        usbEinrichtungLaeuft_ = false;

        const std::string anschluss = port;
        Post([this, ziele, core, fehler, anschluss, schonAktuell]() {
            if (fehler) {
                Warnung(T("Mapper einrichten"),
                        T("Die SD-Karte ließ sich über USB nicht vollständig lesen. Es wurde nichts "
                          "geschrieben. Bitte das USB-Kabel kurz trennen und erneut versuchen."));
                return;
            }
            if (ziele.empty() && schonAktuell > 0) {
                appendLog(BEIDE(Fmt(T("Alle %1 Ordner haben schon den aktuellen MEGA-RAW-Kern."),
                                    {Zahl((long long)schonAktuell)})));
                Info(T("Mapper einrichten"),
                     Fmt(T("Alle %1 Ordner mit Spielen haben schon den aktuellen MEGA-RAW-Kern. "
                           "Es gibt nichts zu tun."), {Zahl((long long)schonAktuell)}));
                return;
            }
            if (ziele.empty()) {
                Info(T("Mapper einrichten"), T("Auf der SD-Karte wurden keine ROMs gefunden."));
                return;
            }
            if (schonAktuell > 0)
                appendLog(BEIDE(Fmt(T("%1 Ordner haben schon den aktuellen MEGA-RAW-Kern und bleiben unverändert."),
                                    {Zahl((long long)schonAktuell)})));
            appendLog(BEIDE(Fmt(T("SD-Karte über USB gelesen (%1): %2 Ordner mit Spielen."),
                                {anschluss, Zahl((long long)ziele.size())})));
            std::vector<std::string> anzeige;
            for (const std::string& z : ziele) anzeige.push_back(z == "/" ? "/" : "/" + z);
            Bestaetigen(T("Mapper einrichten"),
                        Fmt(T("Jetzt wird der MEGA-RAW-Kern in die Ordner kopiert, in denen er fehlt "
                              "oder veraltet ist.\n\n"
                              "Das Tool hat diese %1 Ordner auf der SD-Karte selbst gefunden. Du musst "
                              "nichts auswählen - die Liste ist nur zur Kontrolle.\n\n"
                              "Deine Spiele und Spielstände bleiben unverändert - es kommt nur je eine "
                              "Mapper-Datei dazu.\n\n"
                              "Fremde Mapper-Dateien werden vorher gesichert und lassen sich jederzeit "
                              "unter Optionen > \"Mapper wiederherstellen\" zurückholen.\n\n"
                              "Mit OK geht es los."),
                            {Zahl((long long)ziele.size())}),
                        anzeige, [this, ziele, core]() { einrichtenUsbSchreiben(ziele, core); });
        });
    }).detach();
}

void App::einrichtenUsbSchreiben(std::vector<std::string> ziele, std::vector<uint8_t> core) {
    if (usbEinrichtungLaeuft_.exchange(true)) return;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> kerne;
    kerne.push_back({kKernNamen[0], std::move(core)});
    std::vector<uint8_t> pro = proDaten();
    if (!pro.empty()) kerne.push_back({kKernNamen[1], std::move(pro)});
    appendLog(BEIDE(Fmt(T("Schreibe die Kerne in %1 Ordner..."), {Zahl((long long)ziele.size())})));

    std::thread([this, ziele, kerne]() {
        auto ende = [this](const std::string& titel, const std::string& text, bool warnung) {
            usbEinrichtungLaeuft_ = false;
            Post([this, titel, text, warnung]() {
                if (warnung) Warnung(titel, text); else Info(titel, text);
            });
        };
        auto [ed, port] = find_everdrive();
        if (!ed) {
            ende(T("Mapper einrichten"), T("Die Verbindung zum EverDrive ist weg. Es wurde nichts geschrieben."), true);
            return;
        }
        if (UsbSpielLaeuft(*ed)) {
            ed->close();
            ende(T("Mapper einrichten"),
                 T("Auf der Konsole läuft inzwischen ein Spiel. Es wurde nichts geschrieben. "
                   "Bitte Reset drücken und erneut einrichten."), true);
            return;
        }

        int ok = 0, fail = 0, saved = 0;
        bool abbruch = false;
        usbGesamt_ = (int)ziele.size();
        for (size_t i = 0; i < ziele.size() && !abbruch; ++i) {
            usbZaehler_ = (int)i;
            const std::string& d = ziele[i];
            std::vector<EdSerial::Eintrag> l;
            if (ed->ordnerListe(d, l) < 0) { abbruch = true; break; }
            bool ordnerOk = true;
            for (const auto& k : kerne) {
                const std::string dst = UsbPfad(d, k.first);
                const EdSerial::Eintrag* vorhanden = nullptr;
                for (const auto& e : l)
                    if (!e.ordner && Kleinschrift(e.name) == k.first) vorhanden = &e;

                // Fremde Datei (keine Groesse eines MEGA-RAW-Kerns) erst sichern -
                // nur beim ersten Mal. Gelingt das nicht, bleibt sie stehen.
                if (vorhanden && !UsbEigenerKern(vorhanden->groesse, {k.second.size()})) {
                    const std::string bakDir = d == "/" ? std::string(kBackupDir) : std::string(kBackupDir) + "/" + d;
                    const std::string bak = bakDir + "/" + k.first;
                    EdSerial::Eintrag bi;
                    const int ci = ed->dateiInfo(bak, bi);
                    if (ci < 0) { abbruch = true; break; }
                    if (ci != 0) {
                        Bytes alt;
                        const bool gesichert = UsbOrdnerKette(*ed, bakDir) &&
                                               ed->dateiLesen(dst, vorhanden->groesse, alt) == 0 &&
                                               alt.size() == vorhanden->groesse &&
                                               ed->dateiSchreiben(bak, alt) == 0;
                        if (!gesichert) { ordnerOk = false; continue; }
                        ++saved;
                    }
                }
                // Schreiben und zur Kontrolle zuruecklesen.
                Bytes zurueck;
                int c = ed->dateiSchreiben(dst, k.second);
                if (c == 0) c = ed->dateiLesen(dst, (uint32_t)k.second.size(), zurueck);
                if (c < 0) { abbruch = true; break; }
                if (c != 0 || zurueck != k.second) ordnerOk = false;
            }
            if (abbruch) break;
            if (ordnerOk) ++ok; else ++fail;
            appendLog(Fmt("%1/%2  /%3  %4", {Zahl((long long)i + 1), Zahl((long long)ziele.size()),
                                             d == "/" ? std::string() : d, ordnerOk ? "ok" : T("FEHLER")}));
        }
        ed->close();

        if (saved > 0)
            appendLog(BEIDE(Fmt(T("%1 vorhandene Datei(en) nach %2 gesichert."), {Zahl(saved), kBackupDir})));
        appendLog(BEIDE(Fmt(T("Mapper eingerichtet: %1 Ordner beschrieben, %2 fehlgeschlagen."), {Zahl(ok), Zahl(fail)})));
        std::string text = Fmt(kerne.size() > 1 ? T("mega-core.x25 und mega-pro.rbf in %1 Ordner kopiert.")
                                                : T("mega-core.x25 in %1 Ordner kopiert."), {Zahl(ok)});
        if (fail) text += Fmt(T("\n%1 Ordner konnten nicht beschrieben werden."), {Zahl(fail)});
        if (abbruch)
            text += std::string("\n\n") + T("Die Verbindung brach ab. Bitte das USB-Kabel kurz trennen und "
                                            "die Einrichtung wiederholen.");
        if (saved)
            text += std::string("\n\n") + T("Vorhandene Kerne wurden gesichert und lassen "
                                            "sich in den Optionen wiederherstellen.");
        ende(T("Mapper einrichten"), text, abbruch || fail);
    }).detach();
}

// Gegenstueck zum Einrichten: holt die gesicherten Originale zurueck. Zuerst
// ueber USB (nur im EverDrive-Menue), ohne Verbindung ueber das Laufwerk.
void App::onRestoreMappers() {
    if (monitoring_) {
        Info(T("Mapper wiederherstellen"),
             T("Bitte zuerst den Monitor stoppen und die Konsole ins EverDrive-Menü zurückbringen "
               "(Reset), dann erneut versuchen."));
        return;
    }
    if (usbEinrichtungLaeuft_.exchange(true)) return;
    stopMonitorIfRunning();
    const std::map<std::string, size_t> groesse = {{kKernNamen[0], coreDaten().size()},
                                                   {kKernNamen[1], proDaten().size()}};
    appendLog(BEIDE(T("Suche die SD-Karte über USB...")));
    std::thread([this, groesse]() {
        auto [ed, port] = find_everdrive();
        if (!ed && EverDriveAmUsb()) {
            usbEinrichtungLaeuft_ = false;
            Post([this]() { AnschlussBelegt(T("Mapper wiederherstellen")); });
            return;
        }
        if (!ed) {
            usbEinrichtungLaeuft_ = false;
            Post([this]() {
                appendLog(BEIDE(T("Kein EverDrive über USB gefunden - Wiederherstellen über das Laufwerk.")));
                onRestoreMappersLaufwerk();
            });
            return;
        }
        if (UsbSpielLaeuft(*ed)) {
            ed->close();
            usbEinrichtungLaeuft_ = false;
            Post([this]() {
                Info(T("Mapper wiederherstellen"),
                     T("Auf der Konsole läuft gerade ein Spiel. Bitte Reset drücken, damit das "
                       "EverDrive-Menü erscheint, und dann erneut versuchen."));
            });
            return;
        }

        // Gesicherte Originale (im Sicherungsordner) und unsere eigenen Kerne
        // (gleiche Groesse wie der eingebettete) auf der restlichen Karte.
        std::vector<std::pair<std::string, uint32_t>> gesichert;
        std::vector<std::string> eigene;
        bool fehler = false;
        std::vector<size_t> aktuell;
        for (const auto& g : groesse) aktuell.push_back(g.second);
        EdSerial& edr = *ed;
        const std::string bakRoot = kBackupDir;
        auto lauf = [&](const std::string& start, bool imBackup) {
            std::vector<std::pair<std::string, int>> offen{{start, 0}};
            int n = 0;
            while (!offen.empty() && n < 3000) {
                const auto [pfad, tiefe] = offen.back();
                offen.pop_back();
                std::vector<EdSerial::Eintrag> l;
                const int c = edr.ordnerListe(pfad, l);
                if (c < 0) { fehler = true; return; }
                if (c != 0) continue;   // z. B. Sicherungsordner gibt es nicht
                ++n;
                for (const auto& e : l) {
                    if (e.ordner) {
                        const bool weiter = imBackup ? !e.name.empty() && e.name[0] != '.' : !UsbAusgenommen(pfad, e.name);
                        if (tiefe < 9 && weiter) offen.push_back({UsbPfad(pfad, e.name), tiefe + 1});
                        continue;
                    }
                    const auto it = groesse.find(Kleinschrift(e.name));
                    if (it == groesse.end()) continue;
                    // Gesicherte alte MEGA-RAW-Kerne sind keine Originale.
                    const bool eigen = UsbEigenerKern(e.groesse, aktuell);
                    if (imBackup) { if (!eigen) gesichert.push_back({UsbPfad(pfad, e.name), e.groesse}); }
                    else if (eigen) eigene.push_back(UsbPfad(pfad, e.name));
                }
            }
        };
        lauf("/", false);
        if (!fehler) lauf(bakRoot, true);
        ed->close();
        usbEinrichtungLaeuft_ = false;

        Post([this, gesichert, eigene, fehler, bakRoot]() {
            const char* titel = T("Mapper wiederherstellen");
            if (fehler) {
                Warnung(titel, T("Die SD-Karte ließ sich über USB nicht vollständig lesen. Es wurde nichts "
                                 "geändert. Bitte das USB-Kabel kurz trennen und erneut versuchen."));
                return;
            }
            if (gesichert.empty() && eigene.empty()) {
                Info(titel, T("Auf der SD-Karte gibt es nichts zurückzusetzen."));
                return;
            }
            // In Ordnern zaehlen, nicht in Dateien (je Ordner CORE + PRO).
            std::set<std::string> ordnerEigen, ordnerOrig;
            for (const auto& e : eigene) ordnerEigen.insert(UsbOrdnerVon(e));
            for (const auto& g : gesichert) ordnerOrig.insert(UsbOrdnerVon(g.first.substr(bakRoot.size() + 1)));
            std::set<std::string> alle = ordnerEigen;
            alle.insert(ordnerOrig.begin(), ordnerOrig.end());
            std::vector<std::string> anzeige;
            for (const std::string& o : alle) {
                std::string z = o == "/" ? "/" : "/" + o;
                if (ordnerOrig.count(o)) z += std::string("  -  ") + T("MEGA-RAW-Mapper raus, früheren Mapper zurück");
                else z += std::string("  -  ") + T("nur den MEGA-RAW-Mapper entfernen");
                anzeige.push_back(z);
            }
            std::string text = ordnerEigen.size() == 1
                                   ? std::string(T("Aus 1 Ordner wird der MEGA-RAW-Mapper wieder entfernt."))
                                   : Fmt(T("Aus %1 Ordnern wird der MEGA-RAW-Mapper wieder entfernt."),
                                         {Zahl((long long)ordnerEigen.size())});
            text += std::string("\n") + T("Deine Spiele und Spielstände bleiben unangetastet - gelöscht "
                                          "werden nur die Mapper-Dateien von MEGA-RAW.");
            if (ordnerOrig.size() == 1)
                text += std::string("\n") + T("In 1 Ordner wird der Mapper zurückgeschrieben, der vor "
                                              "MEGA-RAW dort lag.");
            else if (!ordnerOrig.empty())
                text += std::string("\n") + Fmt(T("In %1 Ordnern wird der Mapper zurückgeschrieben, der vor "
                                                  "MEGA-RAW dort lag."), {Zahl((long long)ordnerOrig.size())});
            text += std::string("\n\n") + T("Das Tool hat alles selbst gefunden - die Liste ist nur zur "
                                            "Kontrolle.\n\nMit OK geht es los.");
            Bestaetigen(titel, text, anzeige,
                        [this, gesichert, eigene]() { wiederherstellenUsbSchreiben(gesichert, eigene); });
        });
    }).detach();
}

void App::wiederherstellenUsbSchreiben(std::vector<std::pair<std::string, uint32_t>> gesichert,
                                       std::vector<std::string> eigene) {
    if (usbEinrichtungLaeuft_.exchange(true)) return;
    std::thread([this, gesichert, eigene]() {
        const char* titel = T("Mapper wiederherstellen");
        auto ende = [this, titel](const std::string& text, bool warnung) {
            usbEinrichtungLaeuft_ = false;
            const std::string t = titel;
            Post([this, t, text, warnung]() { if (warnung) Warnung(t, text); else Info(t, text); });
        };
        auto [ed, port] = find_everdrive();
        if (!ed) { ende(T("Die Verbindung zum EverDrive ist weg. Es wurde nichts geändert."), true); return; }
        if (UsbSpielLaeuft(*ed)) {
            ed->close();
            ende(T("Auf der Konsole läuft inzwischen ein Spiel. Es wurde nichts geändert. "
                   "Bitte Reset drücken und erneut versuchen."), true);
            return;
        }
        const std::string bakRoot = kBackupDir;
        bool abbruch = false;
        // Erst unsere Kopien entfernen, dann Originale zurueckschreiben.
        std::set<std::string> ordnerEntfernt, ordnerZurueck;
        for (const std::string& f : eigene) {
            const int c = ed->loeschen(f);
            if (c < 0) { abbruch = true; break; }
            if (c == 0) ordnerEntfernt.insert(UsbOrdnerVon(f));
        }
        int ok = 0, fail = 0;
        for (size_t i = 0; i < gesichert.size() && !abbruch; ++i) {
            const std::string& bak = gesichert[i].first;
            const std::string dst = bak.substr(bakRoot.size() + 1);
            const size_t schnitt = dst.rfind('/');
            Bytes daten, zurueck;
            int c = ed->dateiLesen(bak, gesichert[i].second, daten);
            if (c == 0 && daten.size() != gesichert[i].second) c = 1;
            if (c == 0 && schnitt != std::string::npos && !UsbOrdnerKette(*ed, dst.substr(0, schnitt))) c = 1;
            if (c == 0) c = ed->dateiSchreiben(dst, daten);
            if (c == 0) c = ed->dateiLesen(dst, (uint32_t)daten.size(), zurueck);
            if (c < 0) { abbruch = true; break; }
            const bool gut = c == 0 && zurueck == daten;
            if (gut) { ++ok; ordnerZurueck.insert(UsbOrdnerVon(dst)); } else ++fail;
            appendLog(Fmt("%1/%2  /%3  %4", {Zahl((long long)i + 1), Zahl((long long)gesichert.size()), dst,
                                             gut ? "ok" : T("FEHLER")}));
        }
        ed->close();
        const long long nEnt = (long long)ordnerEntfernt.size(), nZur = (long long)ordnerZurueck.size();
        appendLog(BEIDE(Fmt(T("Zurücksetzen: MEGA-RAW aus %1 Ordnern entfernt, früherer Mapper in %2 Ordnern "
                              "zurück, %3 Datei(en) fehlgeschlagen."), {Zahl(nEnt), Zahl(nZur), Zahl(fail)})));
        std::string text = nEnt ? std::string(T("Die MEGA-RAW-Mapper wurden erfolgreich entfernt."))
                                : std::string(T("Auf der SD-Karte lagen keine MEGA-RAW-Mapper."));
        if (nZur) text += std::string("\n\n") + T("Die Kerne, die vorher dort lagen, wurden zurückgespielt.");
        if (fail) text += Fmt(T("\n%1 Datei(en) konnten nicht zurückgeschrieben werden."), {Zahl(fail)});
        if (abbruch)
            text += std::string("\n\n") + T("Die Verbindung brach ab. Bitte das USB-Kabel kurz trennen und "
                                            "erneut versuchen.");
        ende(text, abbruch || fail);
    }).detach();
}

void App::onRestoreMappersLaufwerk() {
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

    std::vector<size_t> aktuell;
    for (const auto& g : unsereGroesse) aktuell.push_back(g.second);
    std::vector<fs::path> gesichert;
    std::error_code ec;
    if (fs::is_directory(bakRoot, ec))
        for (const fs::path& f : alleKerne(bakRoot))
            if (!UsbEigenerKern(fs::file_size(f, ec), aktuell)) gesichert.push_back(f);   // alte MEGA-RAW-Kerne sind keine Originale

    std::vector<fs::path> eigene;
    for (const fs::path& f : alleKerne(root)) {
        if (Kleinschrift(Generisch(f)).rfind(bakText, 0) == 0) continue;
        // Krikzz' Systemordner nie anfassen (dort liegen seine eigenen Kerne).
        if (Kleinschrift(Generisch(f.lexically_relative(root))).rfind("mega/", 0) == 0) continue;
        if (!UsbEigenerKern(fs::file_size(f, ec), aktuell)) continue;
        eigene.push_back(f);
    }

    if (gesichert.empty() && eigene.empty()) {
        Info(T("Mapper wiederherstellen"), Fmt(T("In %1 gibt es nichts zurückzusetzen."), {rootStr}));
        return;
    }

    std::string frage;
    if (!gesichert.empty())
        frage += Fmt(T("%1 gesicherte Originaldatei(en) zurückschreiben."), {Zahl((long long)gesichert.size())}) + "\n";
    if (!eigene.empty())
        frage += Fmt(T("%1 von MEGA-RAW angelegte Datei(en) entfernen."), {Zahl((long long)eigene.size())}) + "\n";
    frage += std::string("\n") + T("Fortfahren?");

    Frage(T("Mapper wiederherstellen"), frage, [this, root, bakRoot, gesichert, eigene]() {
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

        appendLog(BEIDE(Fmt(T("Zurücksetzen: %1 frühere(r) Kern(e) zurück, %2 MEGA-RAW-Mapper entfernt, %3 fehlgeschlagen."),
                      {Zahl(ok), Zahl(entfernt), Zahl(fail)})));
        std::string text = entfernt ? std::string(T("Die MEGA-RAW-Mapper wurden erfolgreich entfernt."))
                                    : std::string(T("Auf der SD-Karte lagen keine MEGA-RAW-Mapper."));
        if (ok) text += std::string("\n\n") + T("Die Kerne, die vorher dort lagen, wurden zurückgespielt.");
        if (fail) text += Fmt(T("\n%1 Datei(en) konnten nicht zurückgeschrieben werden."), {Zahl(fail)});
        Info(T("Mapper wiederherstellen"), text);
    });
}

// ============================================================================
// Spielerkennung und Set laden
// ============================================================================

// Erkennung ueber den MD5, den der Mapper selbst rechnet. Ueber USB gehen
// dabei 16 Byte statt zwei Megabyte.
// Erkennung ueber die SD-Karte (wie RAW-NES): Pfad des laufenden ROMs vom
// EverDrive, Datei per USB lesen, MD5 am PC. Die Datei muss zum laufenden
// Spiel passen (VBlank-Vektor an $78 = vom Mapper festgehaltener Vektor).
// Einmal gelesene Dateien merkt sich das Tool (Pfad|Groesse -> MD5).
// Klappt etwas nicht, folgt die Erkennung aus dem Mapper (detectGame).
void App::sdErkennungStarten() {
    appendLog(BEIDE(T("Erkenne laufendes Spiel...")));
    sdErk_ = 1;
    usbEinrichtungLaeuft_ = true;   // "Bitte warten" ab 3 s, keine Leerlauf-Abfrage
    const std::map<std::string, std::string> cache = settings_.md5_cache;
    auto ziel = post_;
    App* self = this;
    std::thread([ziel, self, cache]() {
        std::string md5, schluessel, name, grund, wie;
        long long kb = 0, ms = 0;
        try {
            auto [ed, port] = find_everdrive();
            if (!ed) {
                grund = "kein EverDrive";
            } else {
                const Bytes b = ed->memrdNoRecover(mdsnoop::kAddrSnoop + mdsnoop::kCtlOffset, 1);
                std::string pfad;
                EdSerial::Eintrag info;
                const Bytes vek = ed->memrdNoRecover(mdsnoop::kAddrSnoop + 0xB005, 4);   // Vektor $78 (Mapper)
                if (b.empty() || b[0] == 0 || b[0] == 0xFF) grund = "kein Spiel mit MEGA-RAW-Kern";
                else if (ed->romPfad(pfad) != 0 || pfad.empty()) grund = "Pfad des Spiels nicht lesbar";
                else if (ed->dateiInfo(pfad, info) != 0 || info.groesse < 0x200 || info.groesse > 0x800000)
                    grund = "Datei nicht lesbar";
                else {
                    const size_t s = pfad.find_last_of('/');
                    name = s == std::string::npos ? pfad : pfad.substr(s + 1);
                    Bytes kopf;
                    if (ed->dateiLesen(pfad, 0x200, kopf) != 0 || kopf.size() != 0x200) grund = "Datei nicht lesbar";
                    else if (vek.size() != 4 || !std::equal(vek.begin(), vek.end(), kopf.begin() + 0x78))
                        grund = "Datei passt nicht zum laufenden Spiel";
                    else {
                        schluessel = pfad + "|" + std::to_string(info.groesse);
                        const auto t0 = std::chrono::steady_clock::now();
                        auto c = cache.find(schluessel);
                        if (c != cache.end()) {
                            md5 = c->second;
                            wie = "gemerkt";
                        } else {
                            Bytes daten;
                            if (ed->dateiLesen(pfad, info.groesse, daten) != 0 || daten.size() != info.groesse)
                                grund = "Datei nicht lesbar";
                            else {
                                md5 = md5_hex(std::vector<uint8_t>(daten.begin(), daten.end()));
                                wie = "gelesen";
                            }
                        }
                        kb = info.groesse / 1024;
                        ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
                    }
                }
                ed->close();
            }
        } catch (const std::exception& e) {
            grund = e.what();
        }
        std::lock_guard<std::mutex> l(ziel->m);
        if (!ziel->app) return;
        ziel->auftraege.push_back([self, md5, schluessel, name, grund, wie, kb, ms]() {
            self->usbEinrichtungLaeuft_ = false;
            if (!md5.empty()) {
                self->sdMd5_ = md5;
                self->sdErk_ = 2;
                self->appendLog(BEIDE(Fmt(T("Erkannt über die Datei auf der SD-Karte: %1 (%2 KB, %3 ms)"),
                                          {name, Zahl(kb), Zahl(ms)})));
                self->appendLog("MD5: " + md5);
                if (wie == "gelesen") {
                    self->settings_.md5_cache[schluessel] = md5;
                    self->settings_.save(self->settingsPath_);
                }
            } else {
                self->sdErk_ = 3;
                self->appendLog(BEIDE(T("Datei auf der SD-Karte nicht verwendbar - Erkennung über den Mapper.")));
                if (!grund.empty()) self->appendLog(BEIDE("  (" + std::string(T(grund.c_str())) + ")"));
            }
            if (!self->monitoring_) {
                self->onToggleMonitor();
                // Kein Set / gescheitert: fuer dieses Spiel nicht von selbst neu
                if (!self->monitoring_) self->autoPause_ = true;
            } else {
                self->sdErk_ = 0;
            }
        });
    }).detach();
}

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
        // Stopp von Hand: fuer dieses Spiel nicht von selbst neu starten
        autoPause_ = true;
        spielSeitMs_ = 0;
        edStatus_ = BEIDE(T("Monitor angehalten - startet beim nächsten Spiel von selbst"));
        naechsteProbeMs_ = JetztMs() + 3000;
        return;
    }
    if (token_.empty()) { appendLog(BEIDE(T("Bitte zuerst einloggen."))); return; }
    verbindungWeg_ = false;

    // Kein Spiel gewaehlt: erst ueber die Datei auf der SD-Karte (wie RAW-NES,
    // im Hintergrund, ohne Zugriff auf den Konsolenbus), sonst aus dem Mapper.
    if (!setHasSet_) {
        if (sdErk_ == 1) return;                 // laeuft noch
        if (sdErk_ == 0) { sdErkennungStarten(); return; }
        const int ergebnis = sdErk_;
        sdErk_ = 0;
        bool ok = false;
        if (ergebnis == 2) {
            romPath_.clear();
            romPathText_ = BEIDE(T("(von der Konsole erkannt)"));
            game_.reset();
            setHasSet_ = false;
            erkennungErklaert_ = false;
            tryLoadGame(sdMd5_);
            ok = setHasSet_;
        } else {
            ok = detectGame();
        }
        if (!ok) {
            // Nur wenn die Ursache nicht schon genannt wurde.
            if (game_ && game_->raw_core == 0)
                appendLog(BEIDE(T("Für dieses Spiel gibt es bei RetroAchievements keine Achievements.")));
            else if (!erkennungErklaert_)
                appendLog(BEIDE(T("Erkennung fehlgeschlagen - bitte ROM von Hand wählen.")));
            return;
        }
    }

    const std::string port;  // leer -> Autoerkennung scannt alle Ports

    Game gm = game_ ? *game_ : Game{};
    monWorker_ = std::make_unique<MonitorWorker>(port, gm, userBuf_, token_, client_,
                                                 kHardcoreAktiv && settings_.hardcore, settings_.popup_in_game);
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
            a.autoPause_ = false;   // das naechste Spiel startet den Monitor von selbst
            a.spielSeitMs_ = 0;
        }, false);
    };
    w.finished = [weiter]() { weiter([](App& a) { a.onWorkerFinished(); }, false); };

    w.startThread();
    monitoring_ = true;
    edStatus_ = BEIDE(T("EverDrive wird beim Monitor-Start automatisch gesucht"));
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
            T("Auf der Konsole läuft nicht der MEGA-RAW-Mapper, sondern der "
              "Standard-Mapper des EverDrive. Ohne ihn gibt es keine Achievements.\n\n"
              "So geht es:\n"
              "1. Konsole ins EverDrive-Menü bringen (Reset)\n"
              "2. Im Tool \"Mapper auf SD-Karte einrichten...\" wählen\n"
              "3. Das Spiel neu starten"));
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

// Ohne Monitor alle 3 s nachsehen, was die Konsole macht (nur der Anschluss
// mit der EverDrive-Kennung 38DF:0018 - fremde Anschluesse, z. B. Bluetooth,
// koennen beim Oeffnen lange haengen). Ablauf wie RAW-NES: Anschluss bleibt
// offen; Systeminfo (antwortet das EverDrive nur bei eingeschalteter Konsole -
// beim N8 PRO gemessen, beim Mega EverDrive NICHT, daher Rueckfall ohne sie);
// dann Build-Byte des Kerns, und nur mit unserem Kern die Bildzaehler.
void App::autoStartPruefen() {
    g_vorFindEverdrive = LeerlaufSchliessen;   // jeder andere Zugriff gibt den Anschluss frei
    if (monitoring_ || token_.empty() || probeLaeuft_ || usbEinrichtungLaeuft_) return;
    const long long jetzt = JetztMs();
    if (jetzt < naechsteProbeMs_) return;
    naechsteProbeMs_ = jetzt + 3000;
    std::string port;
    for (const auto& p : availableSerialPorts())
        if (p.hatKennung && p.vid == 0x38DF && p.pid == 0x0018) { port = p.name; break; }
    if (port.empty()) { LeerlaufSchliessen(); autoZustand(0, -1); return; }
    probeLaeuft_ = true;
    auto ziel = post_;
    App* self = this;
    std::thread([ziel, self, port]() {
        int z = 0, zaehler = -1;
        std::string mess;
        {
            std::lock_guard<std::mutex> g(Leerlauf().m);
            auto& ed = Leerlauf().ed;
            try {
                if (!ed || !ed->isOpen() || ed->portName() != port) {
                    ed = std::make_unique<EdSerial>(port);
                    ed->open();
                    mess += "geoeffnet ";
                }
                Bytes si = ed->sysInfoNoRecover();
                if (si.size() != 64 && ed->neuOeffnen()) { si = ed->sysInfoNoRecover(); mess += "neu ";  }
                mess += si.size() == 64 ? "si=ok(pwr " + LHx(si[53]) + ") " : "si=keine ";
                try {
                    const Bytes b = ed->memrdNoRecover(mdsnoop::kAddrSnoop + mdsnoop::kCtlOffset, 1);
                    mess += "build=" + (b.empty() ? std::string("-") : LHx(b[0])) + " ";
                    z = (!b.empty() && b[0] != 0 && b[0] != 0xFF) ? 2 : 1;
                    if (z == 2) {
                        const Bytes v = ed->memrdNoRecover(mdsnoop::kAddrSnoop + 0xB00A, 2);
                        const Bytes h = ed->memrdNoRecover(mdsnoop::kAddrSnoop + mdsnoop::kHintCntOffset, 1);
                        if (v.size() == 2 && h.size() == 1) {
                            zaehler = (v[0] << 16) | (v[1] << 8) | h[0];
                            mess += "vb=" + LHx(v[0]) + LHx(v[1]) + " hb=" + LHx(h[0]) + " ";
                        }
                    }
                } catch (...) {
                    z = 3;   // EverDrive antwortet, der Speicher nicht: Konsole aus
                    mess += "memrd fehlgeschlagen ";
                }
            } catch (const std::exception& e) {
                z = 0;       // Anschluss belegt oder weg
                mess += std::string("Fehler: ") + e.what() + " ";
                if (ed) ed->close();
                ed.reset();
            }
        }
        TestLogZeile("LEERLAUF: " + mess + "-> " + std::to_string(z));
        std::lock_guard<std::mutex> l(ziel->m);
        if (!ziel->app) return;
        ziel->auftraege.push_back([self, z, zaehler]() {
            self->probeLaeuft_ = false;
            self->autoZustand(z, zaehler);
        });
    }).detach();
}

void App::AnschlussBelegt(const char* titel) {
    appendLog(BEIDE(T("Das EverDrive steckt am USB, antwortet aber nicht - der Anschluss ist belegt.")));
    Info(titel, T("Das EverDrive ist per USB angeschlossen, aber der Anschluss ist belegt. Läuft noch "
                  "ein zweites MEGA-RAW-Fenster oder ein anderes Programm mit dem EverDrive? Bitte "
                  "schließen und erneut versuchen."));
}

void App::autoZustand(int z, int zaehler) {
    if (monitoring_) return;
    const long long jetzt = JetztMs();
    // "Spiel laeuft" erst, wenn sich ein Bildzaehler (VBlank- oder HBlank-
    // Abrufe) zwischen zwei Abfragen bewegt - der Kern ist schon waehrend des
    // Spielstarts geladen, der Hash in der Startphase kann stoeren. Zaehlt gar
    // nichts (Spiel ohne Interrupts), nach 20 s trotzdem.
    bool laeuft = false;
    if (z != 2) {
        spielSeitMs_ = 0;
        kernSeitMs_ = 0;
        letzterZaehler_ = -1;
        autoPause_ = false;   // Spiel verlassen: das naechste startet von selbst
    } else {
        if (kernSeitMs_ == 0) kernSeitMs_ = jetzt;
        laeuft = zaehler >= 0 && letzterZaehler_ >= 0 && zaehler != letzterZaehler_;
        letzterZaehler_ = zaehler;
        if (!laeuft && jetzt - kernSeitMs_ >= 20000) laeuft = true;
        if (!laeuft) spielSeitMs_ = 0;
        else if (spielSeitMs_ == 0) spielSeitMs_ = jetzt;
    }
    leerlauf_ = z;
    switch (z) {
        case 0: edStatus_ = BEIDE(T("Kein EverDrive gefunden - ist das USB-Kabel angeschlossen?")); break;
        case 1: edStatus_ = BEIDE(T("EverDrive-Menü - der Monitor startet beim Spielstart von selbst")); break;
        case 3: edStatus_ = BEIDE(T("Konsole aus")); break;
        default:
            edStatus_ = autoPause_ ? BEIDE(T("Monitor angehalten - startet beim nächsten Spiel von selbst"))
                      : laeuft    ? BEIDE(T("Spiel läuft - Monitor startet..."))
                                  : BEIDE(T("Spiel startet - warte, bis es läuft..."));
            break;
    }
    if (z == 2 && laeuft && !autoPause_ && jetzt - spielSeitMs_ >= 2500) {
        onToggleMonitor();
        // Kein Set / Erkennung gescheitert: fuer dieses Spiel nicht staendig neu
        // erkennen (der Hash belastet die Konsole), erst beim naechsten Spiel.
        // (Laeuft die Erkennung ueber die SD-Karte noch, entscheidet ihr Ende.)
        if (!monitoring_ && sdErk_ != 1) {
            autoPause_ = true;
            edStatus_ = BEIDE(T("Monitor angehalten - startet beim nächsten Spiel von selbst"));
        }
    }
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

void App::Bestaetigen(const std::string& titel, const std::string& text, std::vector<std::string> liste,
                      std::function<void()> ok) {
    Dialog d;
    d.art = Dialog::Art::Bestaetigen;
    d.titel = titel;
    d.text = text;
    d.liste = std::move(liste);
    d.ja = std::move(ok);
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

// Waehrend die SD-Karte ueber USB gelesen oder beschrieben wird: kleines
// Fenster in der Mitte mit pulsierendem "Bitte warten" und Fortschritt.
void App::ZeichneWarten() {
    const bool aktiv = usbEinrichtungLaeuft_;
    if (aktiv && !usbWarAktiv_) { usbZaehler_ = 0; usbGesamt_ = 0; usbSeitMs_ = JetztMs(); }
    usbWarAktiv_ = aktiv;
    // Erst ab 3 s - kurze Zugriffe sollen nicht aufblitzen.
    if (!aktiv || !dialoge_.empty() || JetztMs() - usbSeitMs_ < 3000) return;
    const float s = dpi_;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * s, 16 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * s);
    const ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##usbwarten", nullptr, fl)) {
        const float puls = 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 4.0f);
        ImVec4 farbe = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        farbe.w = puls;
        ImGui::PushStyleColor(ImGuiCol_Text, farbe);
        ImGui::TextUnformatted(T("Bitte warten ..."));
        ImGui::PopStyleColor();
        ImGui::TextUnformatted(T("Das Tool arbeitet über USB mit der SD-Karte."));
        const int n = usbZaehler_, g = usbGesamt_;
        if (g > 0) {
            ImGui::ProgressBar((float)n / (float)g, ImVec2(300 * s, 0),
                               Fmt(T("%1 von %2 Ordnern"), {Zahl((long long)n), Zahl((long long)g)}).c_str());
        } else if (n > 0) {
            ImGui::TextUnformatted(Fmt(T("%1 Ordner gelesen"), {Zahl((long long)n)}).c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

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
        if (d.art == Dialog::Art::Optionen) { d.sprache = i18n::Sprache(); d.popupImSpiel = settings_.popup_in_game; }
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
            case Dialog::Art::Bestaetigen: {
                // Text, Liste im Rollbereich, OK/Abbrechen. Escape = Abbrechen.
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440 * s);
                ImGui::TextUnformatted(d.text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 4 * s));
                const float zeile = ImGui::GetTextLineHeightWithSpacing();
                const float h = std::min(260 * s, zeile * (float)d.liste.size() + 8 * s);
                ImGui::BeginChild("##liste", ImVec2(440 * s, h), ImGuiChildFlags_Borders);
                for (const std::string& z : d.liste) ImGui::TextUnformatted(z.c_str());
                ImGui::EndChild();
                ImGui::Dummy(ImVec2(0, 8 * s));
                const int r = Knoepfe(s, "OK", "Cancel");
                if (r == 1) { danach = d.ja; schliessen = true; }
                if (r == 2 || ImGui::IsKeyPressed(ImGuiKey_Escape)) schliessen = true;
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
                ImGui::Checkbox(T("Achievement-Einblendung im Spiel"), &d.popupImSpiel);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", T("Zeigt freigeschaltete Achievements direkt auf dem Fernseher an. Das Spiel hält dafür etwa 3 Sekunden an und läuft danach von selbst weiter (Mega EverDrive CORE)."));
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
                if (ImGui::Button(T("Mapper wiederherstellen"), ImVec2(breit, 0))) {
                    wiederherstellen = true;
                    speichern = true;      // dlg.accept()
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", T("Entfernt MEGA-RAW von der SD-Karte und schreibt die Kerne "
                                              "zurück, die vorher dort lagen."));

                if (ImGui::Button(T("über die Katze"), ImVec2(breit, 0))) ImGui::OpenPopup(kKatzeInnen);
                ZeichneKatze(kKatzeInnen);

                if (speichern) {
                    // Die Sprache wirkt sofort - jedes Bild wird neu uebersetzt.
                    // Den Neustart-Hinweis der Qt-Fassung braucht es deshalb nicht.
                    i18n::SpracheSetzen(d.sprache);
                    settings_.language = i18n::Sprache();
                    settings_.popup_in_game = d.popupImSpiel;   // wirkt beim naechsten Monitorstart
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
                                             "auf die SD-Karte des EverDrive.\n\n"
                                             "1. Konsole einschalten und im EverDrive-Menü lassen\n"
                                             "2. EverDrive per USB mit dem Computer verbinden\n"
                                             "3. \"Mapper jetzt einrichten\" klicken\n\n"
                                             "Den Rest erledigt das Programm. Ohne USB-Verbindung fragt es "
                                             "nach der SD-Karte im Kartenleser."));
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
    // Nur Test (test.ini popuptest=1): Einblendung von Hand ausloesen
    if (monWorker_ && monWorker_->popupTestErlaubt()) {
        ImGui::SameLine();
        if (ImGui::Button("Popup testen###popuptest")) monWorker_->popupTest();
    }
    // Nur Test (test.ini sdtest=1): SD-Karte ueber USB beschreiben und pruefen
    if (sdTestErlaubt_ < 0) {
        const std::string ini = LeseDatei(plattform::ExeOrdner() + "/test.ini");
        std::string k;
        for (char c : ini) if (c != ' ' && c != '\t' && c != '\r') k.push_back((char)::tolower((unsigned char)c));
        sdTestErlaubt_ = k.find("\nsdtest=1") != std::string::npos || k.rfind("sdtest=1", 0) == 0 ? 1 : 0;
    }
    if (sdTestErlaubt_ == 1) {
        ImGui::SameLine();
        ImGui::BeginDisabled(sdTestLaeuft_ || monitoring_);
        if (ImGui::Button("SD per USB testen###sdtest")) sdTestUsb();
        ImGui::SameLine();
        if (ImGui::Button("SD aufräumen###sdauf")) sdAufraeumenUsb();
        ImGui::EndDisabled();
    }
}

// Messbau: Datei- und Ordnerbefehle ueber USB (ohne Laufwerk). Schreibt den
// Kern in einen Testordner, liest ihn zurueck, vergleicht, misst die Zeit und
// raeumt wieder auf. Nur bei gestopptem Monitor (Anschluss ist exklusiv).
void App::sdTestUsb() {
    if (sdTestLaeuft_.exchange(true)) return;
    const std::vector<uint8_t> kern = coreDaten();
    std::thread([this, kern]() {
        auto L = [this](const std::string& s) { appendLog("TEST SD: " + s); };
        auto ms = [](std::chrono::steady_clock::time_point t0) {
            return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0).count();
        };
        auto code = [](int c) {
            return c == 0 ? std::string("ok") : c < 0 ? "keine Antwort (" + std::to_string(c) + ")"
                                                      : "Fehler " + std::to_string(c);
        };
        std::vector<std::string> versuche;
        auto [ed, port] = find_everdrive(std::string(), &versuche);
        if (!ed) {
            L("kein EverDrive gefunden");
            for (const auto& v : versuche) L("  " + v);
            sdTestLaeuft_ = false;
            return;
        }
        L("verbunden mit " + port + ", Kern " + std::to_string(kern.size()) + " Byte");
        try {
            const uint8_t b = ed->memrd(mdsnoop::kAddrSnoop + mdsnoop::kCtlOffset, 1)[0];
            L(b > 0 && b < 0xFF ? "laeuft: Spiel mit unserem Kern " + std::to_string(b)
                                : "laeuft: EverDrive-Menue oder fremder Kern (Build-Byte " + std::to_string(b) + ")");
        } catch (...) { L("Build-Byte nicht lesbar"); }

        auto t0 = std::chrono::steady_clock::now();
        std::vector<EdSerial::Eintrag> liste;
        int c = ed->ordnerListe("/", liste);
        L("Wurzel lesen: " + code(c) + ", " + std::to_string(liste.size()) + " Eintraege, " +
          std::to_string(ms(t0)) + " ms");
        if (c < 0) { L("abgebrochen (Antwort fehlt - ggf. USB-Kabel ab/an)"); sdTestLaeuft_ = false; return; }
        for (size_t i = 0; i < liste.size() && i < 40; ++i)
            L(std::string("  ") + (liste[i].ordner ? "[D] " : "    ") + liste[i].name +
              (liste[i].ordner ? "" : " (" + std::to_string(liste[i].groesse) + ")"));

        t0 = std::chrono::steady_clock::now();
        c = ed->ordnerListe("ROMS", liste);
        int ordner = 0;
        for (const auto& e : liste) if (e.ordner) ++ordner;
        L("ROMS lesen: " + code(c) + ", " + std::to_string(liste.size()) + " Eintraege, davon " +
          std::to_string(ordner) + " Ordner, " + std::to_string(ms(t0)) + " ms");

        // Inhalt der Sicherung mit Groessen (welche Kerne liegen dort?).
        {
            std::vector<std::pair<std::string, int>> offen{{kBackupDir, 0}};
            while (!offen.empty()) {
                const auto [pfad, tiefe] = offen.back();
                offen.pop_back();
                std::vector<EdSerial::Eintrag> b;
                if (ed->ordnerListe(pfad, b) != 0) continue;
                for (const auto& e : b) {
                    if (e.ordner) { if (tiefe < 9) offen.push_back({pfad + "/" + e.name, tiefe + 1}); continue; }
                    L("Sicherung: " + pfad + "/" + e.name + " (" + std::to_string(e.groesse) + " Byte" +
                      (UsbEigenerKern(e.groesse, {kern.size(), proDaten().size()}) ? ", MEGA-RAW" : ", unbekannt") + ")");
                }
            }
        }

        const std::string dir = "MEGA-RAW-TEST";
        const std::string datei = dir + "/mega-core.x25";
        c = ed->ordnerAnlegen(dir);
        L("Ordner anlegen: " + code(c) + (c == 8 ? " (gab es schon)" : ""));

        t0 = std::chrono::steady_clock::now();
        c = ed->dateiSchreiben(datei, kern);
        const long long tw = ms(t0);
        L("schreiben: " + code(c) + ", " + std::to_string(tw) + " ms");

        EdSerial::Eintrag info;
        c = ed->dateiInfo(datei, info);
        L("Dateiinfo: " + code(c) + ", Groesse " + std::to_string(info.groesse) +
          (info.groesse == kern.size() ? " (stimmt)" : " (FALSCH)"));

        Bytes zurueck;
        t0 = std::chrono::steady_clock::now();
        c = ed->dateiLesen(datei, (uint32_t)kern.size(), zurueck);
        const long long tr = ms(t0);
        L("zuruecklesen: " + code(c) + ", " + std::to_string(tr) + " ms, Inhalt " +
          (zurueck == kern ? "GLEICH" : "ABWEICHEND (" + std::to_string(zurueck.size()) + " Byte)"));

        c = ed->loeschen(datei);
        L("Datei loeschen: " + code(c));
        c = ed->loeschen(dir);
        L("Ordner loeschen: " + code(c));
        L("fertig");
        ed->close();
        sdTestLaeuft_ = false;
    }).detach();
}

// Testknopf: raeumt die Entwicklerkarte auf - alte Speicherstaende der
// geteilten Slots in der Wurzel (rawslotN.sst/.dat, seit 04.09.2026 unbenutzt),
// rawtest.txt, MEGA-RAW-TEST und gesicherte alte MEGA-RAW-Kerne in der
// Sicherung (keine Originale). Nur im EverDrive-Menue, Liste + OK.
void App::sdAufraeumenUsb() {
    if (monitoring_ || usbEinrichtungLaeuft_.exchange(true)) return;
    const std::vector<size_t> aktuell = {coreDaten().size(), proDaten().size()};
    std::thread([this, aktuell]() {
        auto L = [this](const std::string& s) { appendLog("TEST AUFRAEUMEN: " + s); };
        auto [ed, port] = find_everdrive();
        if (!ed) { L("kein EverDrive gefunden"); usbEinrichtungLaeuft_ = false; return; }
        if (UsbSpielLaeuft(*ed)) {
            L("es laeuft ein Spiel - bitte Reset (EverDrive-Menue)");
            ed->close(); usbEinrichtungLaeuft_ = false; return;
        }
        std::vector<std::string> dateien, ordner;   // ordner: tiefste zuerst loeschen
        bool fehler = false;
        std::vector<EdSerial::Eintrag> l;
        if (ed->ordnerListe("/", l) < 0) fehler = true;
        for (const auto& e : l) {
            const std::string n = Kleinschrift(e.name);
            const bool slot = n.rfind("rawslot", 0) == 0 &&
                              (UsbEndung(n) == ".sst" || UsbEndung(n) == ".dat");
            if (!e.ordner && (slot || n == "rawtest.txt")) dateien.push_back(e.name);
            if (e.ordner && n == "mega-raw-test") {
                std::vector<EdSerial::Eintrag> t;
                if (ed->ordnerListe(e.name, t) == 0)
                    for (const auto& x : t) if (!x.ordner) dateien.push_back(e.name + "/" + x.name);
                ordner.push_back(e.name);
            }
        }
        // Sicherung: alte MEGA-RAW-Kerne entfernen, leere Ordner danach.
        std::vector<std::pair<std::string, int>> offen{{kBackupDir, 0}};
        std::vector<std::string> bakOrdner;
        while (!fehler && !offen.empty()) {
            const auto [pfad, tiefe] = offen.back();
            offen.pop_back();
            std::vector<EdSerial::Eintrag> b;
            const int c = ed->ordnerListe(pfad, b);
            if (c < 0) { fehler = true; break; }
            if (c != 0) continue;
            bakOrdner.push_back(pfad);
            for (const auto& e : b) {
                if (e.ordner) { if (tiefe < 9) offen.push_back({pfad + "/" + e.name, tiefe + 1}); continue; }
                const std::string n = Kleinschrift(e.name);
                if ((n == kKernNamen[0] || n == kKernNamen[1]) && UsbEigenerKern(e.groesse, aktuell))
                    dateien.push_back(pfad + "/" + e.name);
            }
        }
        ed->close();
        usbEinrichtungLaeuft_ = false;
        // Tiefste Ordner zuerst; nicht leere bleiben beim Loeschen einfach stehen.
        std::sort(bakOrdner.begin(), bakOrdner.end(), [](const std::string& a, const std::string& b) {
            return std::count(a.begin(), a.end(), '/') > std::count(b.begin(), b.end(), '/');
        });
        ordner.insert(ordner.end(), bakOrdner.begin(), bakOrdner.end());
        if (fehler) { L("Karte nicht vollstaendig lesbar - nichts geaendert"); return; }
        if (dateien.empty()) { L("nichts aufzuraeumen"); return; }
        Post([this, dateien, ordner]() {
            std::vector<std::string> anzeige;
            for (const auto& d : dateien) anzeige.push_back("/" + d);
            Bestaetigen("SD aufräumen",
                        "Diese " + Zahl((long long)dateien.size()) +
                            " Dateien werden gelöscht: alte Speicherstände der geteilten Slots, Testdateien "
                            "und gesicherte alte MEGA-RAW-Kerne (keine Originale). Danach werden leere "
                            "Ordner der Sicherung entfernt.\n\nMit OK geht es los.",
                        anzeige, [this, dateien, ordner]() {
                            if (usbEinrichtungLaeuft_.exchange(true)) return;
                            std::thread([this, dateien, ordner]() {
                                auto L = [this](const std::string& s) { appendLog("TEST AUFRAEUMEN: " + s); };
                                auto [ed, port] = find_everdrive();
                                if (!ed || UsbSpielLaeuft(*ed)) {
                                    L("abgebrochen (keine Verbindung oder Spiel laeuft)");
                                    usbEinrichtungLaeuft_ = false;
                                    return;
                                }
                                int weg = 0, fehl = 0, ow = 0;
                                for (const auto& d : dateien) { if (ed->loeschen(d) == 0) ++weg; else ++fehl; }
                                for (const auto& o : ordner) if (ed->loeschen(o) == 0) ++ow;
                                ed->close();
                                L(std::to_string(weg) + " Dateien geloescht, " + std::to_string(fehl) +
                                  " fehlgeschlagen, " + std::to_string(ow) + " leere Ordner entfernt");
                                usbEinrichtungLaeuft_ = false;
                            }).detach();
                        });
        });
    }).detach();
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
    autoStartPruefen();
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
        // Wie RAW-NES: Grundzustand verbindet und erkennt in einem Schritt;
        // nach einem Stopp von Hand, solange dasselbe Spiel laeuft: fortsetzen.
        const std::string lbl = T(monitoring_ ? "■ Monitor stoppen"
                                  : (autoPause_ && leerlauf_ == 2) ? "Monitor fortsetzen"
                                                                   : "Spiel erkennen & Monitor starten");
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

    ZeichneWarten();
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
