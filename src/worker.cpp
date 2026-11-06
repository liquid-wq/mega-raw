#include "worker.h"

#include "hook_copy.h"
#include "i18n.h"
#include "plattform.h"
#include "version.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Schlafe(int ms) { ::Sleep((DWORD)ms); }

std::string Gross(std::string s) {
    for (char& c : s) c = (char)::toupper((unsigned char)c);
    return s;
}

// QString("%1").arg(v, breite, 16, QChar('0'))
std::string Hx(unsigned long long v, int breite) { return i18n::Hex(v, breite, false); }
std::string HX(unsigned long long v, int breite) { return i18n::Hex(v, breite, true); }

// QString::number(d, 'f', n)
std::string Fix(double d, int n) {
    char t[64];
    std::snprintf(t, sizeof(t), "%.*f", n, d);
    return t;
}

std::string Num(long long n) { return std::to_string(n); }

std::string hexBytes(const Bytes& b) {
    std::string s;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i) s += ' ';
        s += HX(b[i], 2);
    }
    return s;
}

std::string Mitte(const Bytes& b, size_t ab, size_t n) {
    if (ab >= b.size()) return std::string();
    return hexBytes(Bytes(b.begin() + (long)ab, b.begin() + (long)std::min(b.size(), ab + n)));
}

std::string Join(const std::vector<std::string>& l, const char* trenner) {
    std::string s;
    for (size_t i = 0; i < l.size(); ++i) { if (i) s += trenner; s += l[i]; }
    return s;
}

std::string Datei(const char* name) { return plattform::ExeOrdner() + "/" + name; }

std::string Jetzt(bool mitDatum) {
    SYSTEMTIME t;
    ::GetLocalTime(&t);
    char s[32];
    if (mitDatum)
        std::snprintf(s, sizeof(s), "%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    else
        std::snprintf(s, sizeof(s), "%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
    return s;
}

long long JetztMsEpoch() {
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    const unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)((t - 116444736000000000ULL) / 10000ULL);
}

bool Existiert(const std::string& p) {
    std::error_code ec;
    return fs::exists(fs::u8path(p), ec);
}

long long Groesse(const std::string& p) {
    std::error_code ec;
    const auto n = fs::file_size(fs::u8path(p), ec);
    return ec ? -1 : (long long)n;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

// Testschalter aus test.ini neben der EXE (Abschnitt [test]). Fehlt die
// Datei - wie bei jedem normalen Nutzer -, ist alles automatisch.
//   weg    = W | R | ALT   erzwingt einen Weg der Werte-Auslesung
//   region = NTSC          Regionsbyte fuer Tests auf einer PAL-Konsole
//   diag   = <Pfad>        andere Zieldatei fuer die Diagnose
std::string testSchalter(const char* schluessel, bool gross = true) {
    const std::string ini = Datei("test.ini");
    if (!Existiert(ini)) return std::string();
    wchar_t wert[1024] = {};
    ::GetPrivateProfileStringW(L"test", plattform::Breit(schluessel).c_str(), L"", wert, 1023,
                               plattform::Breit(ini).c_str());
    std::string v = Trim(plattform::Utf8(wert));
    return gross ? Gross(v) : v;
}

// Wartet, bis die Freigabe des Kopierprogramms wirksam bzw. aus ist (B00D
// Bit2). Der Mapper uebernimmt sie erst beim naechsten VBlank-Vektor -
// solange laeuft das alte Programm sicher zu Ende.
bool warteKopieStatus(mdsnoop::MdSnoop& snoop, bool wirksam) {
    Uhr t;
    t.start();
    while (t.elapsed() < 1000) {
        uint8_t s = snoop.cpyStatus();
        // Beim Abschalten zusaetzlich warten, bis ein ausstehender Hauptteil
        // (Nachher-Modus, Bit 7) gelaufen ist - erst dann darf das Programm
        // ueberschrieben werden.
        if (wirksam ? (s & 0x04) != 0 : (s & 0x84) == 0) return true;
        Schlafe(20);
    }
    return false;
}

}  // namespace

MonitorWorker::MonitorWorker(std::string port, Game game, std::string user, std::string token,
                             std::shared_ptr<RaClient> client, bool hardcore)
    : hardcore_(hardcore), port_(std::move(port)), game_(std::move(game)),
      user_(std::move(user)), token_(std::move(token)), client_(std::move(client)) {}

MonitorWorker::~MonitorWorker() {
    running_ = false;
    if (thread_.joinable()) {
        if (threadLaeuft_) thread_.detach();   // laeuft er noch, darf er nicht abgeraeumt werden
        else thread_.join();
    }
}

void MonitorWorker::startThread() {
    threadLaeuft_ = true;
    thread_ = std::thread([this]() {
        start();
        threadLaeuft_ = false;
    });
}

bool MonitorWorker::warte(int ms) {
    Uhr u;
    u.start();
    while (threadLaeuft_ && u.elapsed() < ms) Schlafe(10);
    if (!threadLaeuft_ && thread_.joinable()) thread_.join();
    return !threadLaeuft_;
}

void MonitorWorker::stop() {
    running_ = false;
}

void MonitorWorker::emitLog(const Zweisprachig& m) {
    // In der Qt-Fassung hing die Diagnose als direkte Verbindung am
    // log-Signal: jede Logzeile landet zusaetzlich in der Datei - in der
    // eingestellten Sprache, wie dort.
    diagZeile("LOG " + m.text());
    if (log) log(m);
}

void MonitorWorker::writeSessionMarker(const Game& g) {
    // Markiert in recording.csv, wo eine neue Spiel-Session beginnt.
    std::ofstream rec(fs::u8path(Datei("recording.csv")), std::ios::app);
    if (rec) {
        rec << "# --- Session-Start " << Jetzt(true)
            << " | Spiel: " << g.name
            << " | RA-Game-ID: " << g.gameid << " ---\n";
    }
}

std::string MonitorWorker::schreibeBericht(const std::vector<std::string>& zeilen, bool datei) {
    const std::string pfad = Datei("pro_check.txt");
    if (datei) {
        if (std::FILE* f = _wfopen(plattform::Breit(pfad).c_str(), L"w")) {
            for (const std::string& z : zeilen) std::fprintf(f, "%s\n", z.c_str());
            std::fclose(f);
        }
    }
    // Derselbe Bericht auch in die Diagnose.
    for (const std::string& z : zeilen) diagZeile("CHECK " + z);
    if (datei) diagSichern();
    return pfad;
}

void MonitorWorker::richteKopieEin(mdsnoop::MdSnoop& snoop, const std::vector<uint32_t>& addrs, bool zuordnungOk) {
    kopierWeg_ = 0;
    ring_ = false;
    if (coreBuild_ < 128) {
        emitLog(BEIDE(Fmt(T("Werte-Auslesung: Standardverfahren (Core-Build %1)."), {Num(coreBuild_)})));
        return;
    }

    std::vector<std::string> bericht;
    bericht.push_back("MEGA-RAW value check");
    bericht.push_back("date       : " + Jetzt(true));
    bericht.push_back("tool       : " + std::string(MEGA_RAW_VERSION) + " (build " + Num(MEGA_RAW_CPP_BUILD) + ")");
    bericht.push_back("core build : " + Num(coreBuild_));
    bericht.push_back("game       : " + game_.name + " (RA " + Num(game_.gameid) + ")");
    try {
        auto offs = hookcopy::offsets(addrs);
        bericht.push_back("addresses  : " + Num((long long)offs.size()) + " bytes in " +
                          Num((long long)hookcopy::bloecke(offs).size()) + " blocks");
        // Diagnose: die vollstaendige Adressliste, damit sich das Programm
        // fuer genau dieses Set spaeter nachrechnen laesst.
        if (diagDatei_) {
            std::vector<std::string> l;
            for (uint32_t o : offs) l.push_back(HX(o, 4));
            bericht.push_back("address list : " + Join(l, " "));
        }
    } catch (...) {}

    // Nur fuer Tests: test.ini "weg" = W, R oder ALT erzwingt einen Weg.
    const std::string vorgabe = testSchalter("weg");
    if (!vorgabe.empty()) {
        emitLog(BEIDE(Fmt(T("Werte-Auslesung per test.ini vorgegeben: %1"), {vorgabe})));
        bericht.push_back("forced     : " + vorgabe);
    }

    // Diagnose: einige Sekunden den alten Mitschnitt aufzeichnen, bevor ein
    // Programm laeuft - zum Vergleich mit dem, was die neuen Wege liefern.
    auto alleBloecke = [&](const std::string& name) {
        for (auto& kv : snoop.readAll())
            bericht.push_back(Gross("mirror " + name + " " + Hx((unsigned long long)kv.first * hookcopy::kBlock + 0xFF0000, 6)) +
                              " : " + hexBytes(kv.second));
    };
    if (diagDatei_ && zuordnungOk) {
        try {
            snoop.cpyEnable(false);
            warteKopieStatus(snoop, false);
            snoop.resetMarks();
            // Unterbrechbar: ein Stopp waehrend des Einrichtens muss sofort greifen.
            Uhr warte;
            warte.start();
            while (running_ && warte.elapsed() < 3000) Schlafe(20);
            alleBloecke("OLD");
        } catch (const std::exception& e) {
            bericht.push_back(std::string("mirror OLD : ERR ") + e.what());
        }
    }

    // Reihenfolge: Bildpuffer nach dem VBlank-Teil des Spiels (ab Build 133),
    // dann Bildpuffer davor, dann ohne. Spiele ohne VBlank-Interrupt (z. B.
    // Moonwalker): der VBlank-Zaehler (B00A-B) steht, der HBlank-Zaehler
    // (D0F7) laeuft. Dann am HBlank-Vektor kopieren (ab Build 135).
    bool ohneVblank = false;
    if (coreBuild_ >= mdsnoop::kHintBuild && vorgabe != "ALT") {
        try {
            Bytes v1 = snoop.readReg(0xB00A, 2), h1 = snoop.readReg(mdsnoop::kHintCntOffset, 1);
            Schlafe(300);
            Bytes v2 = snoop.readReg(0xB00A, 2), h2 = snoop.readReg(mdsnoop::kHintCntOffset, 1);
            ohneVblank = v1 == v2 && !h1.empty() && !h2.empty() && h1 != h2;
            bericht.push_back("interrupts : vblank " + hexBytes(v1) + " -> " + hexBytes(v2) + ", hblank " +
                              hexBytes(h1) + " -> " + hexBytes(h2) +
                              (ohneVblank ? " (no vblank interrupt - using hblank)" : ""));
        } catch (...) {}
    }

    struct Versuch { int nr; bool ring; bool post; bool hint; };
    std::vector<Versuch> wege;
    if (vorgabe != "ALT" && ohneVblank) {
        if (vorgabe != "R") wege.push_back({1, true, false, true});
        if (vorgabe != "W") wege.push_back({2, true, false, true});
    } else if (vorgabe != "ALT") {
        const Versuch stufen[3] = {{0, true, true, false}, {0, true, false, false}, {0, false, false, false}};
        for (const Versuch& s : stufen) {
            if (s.post && (coreBuild_ < mdsnoop::kPostBuild || postZwang_ == 0
                           || (postZwang_ < 0 && testSchalter("post") == "0"))) continue;
            if (s.ring && (coreBuild_ < mdsnoop::kRingBuild || testSchalter("ring") == "0")) continue;
            if (!s.ring && !zuordnungOk) continue;   // ohne Bildpuffer braucht es die Blockzuordnung
            if (vorgabe != "R") wege.push_back({1, s.ring, s.post, false});
            if (vorgabe != "W" && coreBuild_ >= 129) wege.push_back({2, s.ring, s.post, false});
        }
    }

    bool zuordnungWeg = false;   // Blockzuordnung fuer den Bildpuffer umgeschrieben
    for (const Versuch& v : wege) {
        if (!running_) break;                    // Stopp waehrend des Einrichtens
        const int nr = v.nr;
        const hookcopy::Weg weg = nr == 1 ? hookcopy::Weg::W : hookcopy::Weg::R;
        const std::string name = std::string(nr == 1 ? "W" : "R") + (v.ring ? "+F" : "") + (v.post ? "+P" : "") +
                                 (v.hint ? "+H" : "");
        std::string grund;
        try {
            // Nach einem Versuch mit Bildpuffer die normale Zuordnung zurueck
            if (!v.ring && zuordnungWeg) { snoop.program(addrs); zuordnungWeg = false; }
            hookcopy::Programm p;
            try {
                p = hookcopy::erzeuge(weg, addrs, v.ring, v.post, v.hint);
            } catch (const hookcopy::HookCopyError&) {
                grund = T("zu viele Adressen");
            }
            if (grund.empty() && p.takte > (v.ring ? hookcopy::kMaxTakteRing : hookcopy::kMaxTakte))
                grund = Fmt(T("zu hohe Last (%1)"), {Num(p.takte)});

            // Erst abschalten und warten, bis es wirksam ist - danach fuehrt
            // die CPU das Programm sicher nicht mehr aus.
            if (grund.empty()) {
                snoop.cpyEnable(false);
                if (!warteKopieStatus(snoop, false)) grund = T("Cartridge reagiert nicht (Abschalten)");
            }
            if (grund.empty()) {
                const Bytes prog = hookcopy::bytes(p);
                snoop.cpyUpload(prog);
                const Bytes zurueck = snoop.cpyRead((int)prog.size());
                int abw = 0;
                for (size_t i = 0; i < prog.size(); ++i)
                    if (i >= zurueck.size() || zurueck[i] != prog[i]) ++abw;
                if (abw) grund = Fmt(T("Übertragung fehlerhaft (%1 Abweichungen)"), {Num(abw)});
            }
            if (grund.empty()) {
                if (v.ring) {
                    snoop.programRing((int)((p.groesse + hookcopy::kBlock - 1) / hookcopy::kBlock));
                    zuordnungWeg = true;
                }
                snoop.resetMarks();
                Schlafe(50);
                snoop.cpyEnable(true, v.ring, p.rsz, v.post, v.hint);
                if (!warteKopieStatus(snoop, true)) grund = T("Cartridge reagiert nicht (Einschalten)");
                else if (v.ring && (snoop.cpyStatus() & 0x78) != (0x08 | (p.rsz << 4) | (v.post ? 0x40 : 0)))
                    grund = T("Bildpuffer nicht wirksam");
            }
            if (grund.empty() && v.ring) {
                // Pruefung im zuletzt vollstaendigen Bild. Laeuft der Zaehler
                // nicht weiter, wird das Programm nicht bis zum Ende ausgefuehrt.
                Schlafe(200);
                const int g1 = snoop.ringStatus() & 0x7F;
                Schlafe(100);
                const uint8_t s2 = snoop.ringStatus();
                const int g2 = s2 & 0x7F;
                const int lauf = (g2 - g1 + 128) % 128;
                const int letzte = (g2 - 1 + 128) % 128;
                const Bytes bild = snoop.readRing(letzte, p.abschnitt, (int)p.groesse);
                const int g3 = snoop.ringStatus() & 0x7F;
                uint16_t wert = 0;
                if (bild.size() >= p.groesse)
                    wert = (uint16_t)((bild[p.pruefPos] << 8) | bild[p.pruefPos + 1]);
                const bool frisch = ((g3 - letzte + 128) % 128) < p.tiefe;

                Bytes probe;
                std::vector<uint32_t> sortiert(addrs.begin(), addrs.end());
                std::sort(sortiert.begin(), sortiert.end());
                for (size_t i = 0; i < sortiert.size() && i < 16; ++i) {
                    auto off = mdsnoop::wramOffset(sortiert[i]);
                    auto it = off ? p.lage.find(*off) : p.lage.end();
                    if (it != p.lage.end() && it->second < bild.size()) probe.push_back(bild[it->second]);
                }
                bericht.push_back("mode " + name + "   : " + Num((long long)p.worte.size()) + " words, load " +
                                  Num(p.takte) + ", check " + Hx(wert, 4) + ", frames/100ms " + Num(lauf) +
                                  ", size " + Num(p.groesse));
                bericht.push_back("values " + name + " : " + hexBytes(probe));
                if (diagDatei_) bericht.push_back("frame " + name + "  : " + hexBytes(bild));

                if (wert == hookcopy::kPruefwert && lauf > 0 && frisch) {
                    kopierWeg_ = nr;
                    ring_ = true;
                    ringLage_ = p.lage;
                    ringGroesse_ = p.groesse;
                    ringPruef_ = p.pruefPos;
                    ringSp_ = p.spPos;
                    ringStapel_ = p.stapel;
                    spHisto_.clear();                    // Ruhewert je Modus neu bestimmen
                    spRuhe_ = 0;
                    ringPost_ = v.post;
                    ringHint_ = v.hint;
                    ringSperre_ = false;
                    if (v.post || v.hint) {
                        const Bytes b4 = snoop.readReg(0xB004, 1);
                        ringBadAlt_ = b4.empty() ? 0 : b4[0];
                    }
                    ringAbschnitt_ = p.abschnitt;
                    ringTiefe_ = p.tiefe;
                    ringLetzte_ = -1;
                    ringLuecke_ = false;
                    emitLog(BEIDE(Fmt(T("Werte-Auslesung: Modus %1 aktiv (Last %2)."), {name, Num(p.takte)})));
                    bericht.push_back("frame ring : " + Num(p.tiefe) + " frames x " + Num(p.abschnitt) + " bytes");
                    bericht.push_back("result     : mode " + name + " active");
                    schreibeBericht(bericht, false);
                    return;
                }
                grund = lauf == 0 ? std::string(T("Bildzähler steht"))
                                  : Fmt(T("Prüfwert %1 statt A55A"), {HX(wert, 4)});
            }
            if (grund.empty()) {
                Schlafe(200);
                const uint32_t pa = 0xFF0000u | p.pruefOff;
                const Bytes vw = snoop.read(pa, 2);
                const bool mk = snoop.written(pa) && snoop.written(pa + 1);
                const uint16_t wert = (uint16_t)((vw[0] << 8) | vw[1]);

                Bytes probe;
                std::vector<uint32_t> sortiert(addrs.begin(), addrs.end());
                std::sort(sortiert.begin(), sortiert.end());
                for (size_t i = 0; i < sortiert.size() && i < 16; ++i) {
                    const Bytes b = snoop.read(sortiert[i], 1);
                    probe.insert(probe.end(), b.begin(), b.end());
                }

                bericht.push_back("mode " + name + "     : " + Num((long long)p.worte.size()) + " words, load " +
                                  Num(p.takte) + ", check " + Hx(wert, 4) + ", marks " + (mk ? "ok" : "missing"));
                bericht.push_back("values " + name + "   : " + hexBytes(probe));
                if (diagDatei_) alleBloecke(name);

                if (wert == hookcopy::kPruefwert && mk) {
                    kopierWeg_ = nr;
                    diagPruef_ = p.pruefOff;
                    emitLog(BEIDE(Fmt(T("Werte-Auslesung: Modus %1 aktiv (Last %2)."), {name, Num(p.takte)})));
                    bericht.push_back("result     : mode " + name + " active");
                    schreibeBericht(bericht, false);
                    return;
                }
                grund = Fmt(T("Prüfwert %1 statt A55A"), {HX(wert, 4)});
            }
        } catch (const std::exception& e) {
            grund = e.what();
        }
        emitLog(BEIDE(Fmt(T("Werte-Auslesung Modus %1 nicht verwendbar: %2"), {name, grund})));
        bericht.push_back("mode " + name + "     : not usable - " + grund);
        try { snoop.cpyEnable(false); } catch (...) {}
    }

    // Kein Weg hat getragen: Programm aus, alter Mitschnitt, Bericht.
    try {
        snoop.cpyEnable(false);
        warteKopieStatus(snoop, false);
        if (zuordnungWeg && zuordnungOk) snoop.program(addrs);
        snoop.resetMarks();
    } catch (...) {}
    try {
        bericht.push_back("183B00D    : " + hexBytes(snoop.readReg(mdsnoop::kCpyCtlOffset, 1)));
        bericht.push_back("183B001    : " + hexBytes(snoop.readReg(mdsnoop::kStatOffset, 1)));
        bericht.push_back("183B00A-B  : " + hexBytes(snoop.readReg(0xB00A, 2)));
        bericht.push_back("183D0F0-F5 : " + hexBytes(snoop.readReg(0xD0F0, 6)));
    } catch (...) {}
    bericht.push_back("platform   : " + Num(plattform_));
    bericht.push_back(altWegErlaubt() && zuordnungOk ? "result     : standard method"
                                                     : "result     : not supported on this device");
    const std::string pfad = schreibeBericht(bericht);
    if (!wege.empty() && altWegErlaubt() && zuordnungOk)
        emitLog(BEIDE(Fmt(T("Werte-Auslesung: kein Modus verwendbar - Standardverfahren. Bericht: %1"), {pfad})));
}

// Diagnosestation. Laeuft immer mit (gleiche Ablaeufe wie im Testbau), die
// Zeilen bleiben aber im Speicher; eine Datei entsteht nur bei einem Fehler.
static constexpr bool kDiagnose = true;
static constexpr size_t kDiagMax = 4 * 1024 * 1024;   // Bytes im Speicher

void MonitorWorker::diagOeffnen() {
    if (!kDiagnose || diagDatei_) return;
    diagDatei_ = true;
    diagPuffer_.clear();
    diagUhr_.start();
    diagTakt_.start();
    diagZeile("=== session " + Jetzt(true) + " | tool " + MEGA_RAW_VERSION + " (build " +
              Num(MEGA_RAW_CPP_BUILD) + ") | port " + port_);
    // Geraetekennung - zeigt, ob ein PRO oder ein CORE haengt.
    for (const SerialPortInfo& pi : availableSerialPorts()) {
        if (pi.name != port_) continue;
        diagZeile("device " + pi.beschreibung + " | " + pi.hersteller + " | vid " + Hx(pi.vid, 4) + " pid " +
                  Hx(pi.pid, 4) + " | serial " + pi.seriennummer);
    }
    diagZeile(Existiert(Datei("test.ini"))
                  ? "test.ini: weg=" + testSchalter("weg") + " region=" + testSchalter("region")
                  : std::string("test.ini: none (automatic)"));
    // Kopfzeilen getrennt halten - sie bleiben erhalten, auch wenn der
    // Speicher spaeter gekuerzt wird.
    diagKopf_ = diagPuffer_;
    diagPuffer_.clear();
}

void MonitorWorker::diagZeile(const std::string& z) {
    if (!diagDatei_) return;
    diagPuffer_ += Fix(diagUhr_.elapsed() / 1000.0, 3) + " " + z + "\n";
    // Begrenzen: wird es zu viel, die aeltere Haelfte verwerfen (an einer
    // Zeilengrenze). Die Kopfzeilen liegen getrennt in diagKopf_.
    if (diagPuffer_.size() > kDiagMax) {
        const size_t schnitt = diagPuffer_.find('\n', diagPuffer_.size() - kDiagMax / 2);
        if (schnitt != std::string::npos) diagPuffer_.erase(0, schnitt + 1);
    }
}

// Gesammelte Diagnose als diagnose_live.txt neben die EXE schreiben.
void MonitorWorker::diagSichern() {
    if (!diagDatei_) return;
    if (std::FILE* f = _wfopen(plattform::Breit(Datei("diagnose_live.txt")).c_str(), L"wb")) {
        std::fwrite(diagKopf_.data(), 1, diagKopf_.size(), f);
        std::fwrite(diagPuffer_.data(), 1, diagPuffer_.size(), f);
        std::fclose(f);
    }
}

// Zusaetzlich ueberwachte Adressen. Sonic 1 (RA 1): Bildzaehler des Spiels
// $FFFE04 (Wort) und VBlank-Zaehler $FFFE0C (Langwort) - daraus ergibt sich,
// wie viele Bilder das Spiel verpasst.
std::vector<uint32_t> MonitorWorker::diagExtra() const {
    if (!kDiagnose || game_.gameid != 1) return {};
    return {0xFFFE04, 0xFFFE05, 0xFFFE0C, 0xFFFE0D, 0xFFFE0E, 0xFFFE0F};
}

void MonitorWorker::diagSekunde(mdsnoop::MdSnoop& snoop, const RamMap& ram) {
    if (!diagDatei_ || diagTakt_.elapsed() < 1000) return;
    const long long ms = diagTakt_.restart();
    std::vector<std::string> z;
    z.push_back(std::string("mode=") + (kopierWeg_ == 1 ? "W" : kopierWeg_ == 2 ? "R" : "STD") +
                (ring_ ? "+F" : "") + (ring_ && ringPost_ ? "+P" : "") + (ring_ && ringHint_ ? "+H" : ""));
    try {
        const Bytes b00 = snoop.readReg(0xB000, 16);
        const Bytes d0 = snoop.readReg(0xD0F0, 16);
        const int hok = b00[11];
        const int vbl = diagHokAlt_ < 0 ? -1 : (hok - diagHokAlt_ + 256) % 256;
        diagHokAlt_ = hok;
        // VDP-Register, wie der Mapper sie mitschneidet: R0 Bit4 = HBlank-
        // Interrupt an, R1 Bit5 = VBlank-Interrupt an, R10 = HBlank-Abstand.
        const Bytes vr = snoop.readReg(0xC000, 11);
        if (vr.size() >= 11)
            z.push_back("vdp r0=" + Hx(vr[0], 2) + " r1=" + Hx(vr[1], 2) + " r10=" + Hx(vr[10], 2));
        z.push_back("b000=" + Mitte(b00, 0, 2));
        z.push_back("b009=" + Mitte(b00, 9, 1));
        z.push_back("b00d=" + Mitte(b00, 13, 1));
        z.push_back("vbl/s=" + (vbl < 0 ? std::string("-") : Fix(vbl * 1000.0 / (double)ms, 1)));
        z.push_back("sav,cod,ram=" + Mitte(d0, 0, 6));
        z.push_back("cmax=" + Mitte(d0, 6, 1));
        z.push_back("bus=" + Mitte(d0, 8, 4));
        z.push_back("pad=" + Mitte(d0, 12, 3));
    } catch (const std::exception& e) {
        z.push_back(std::string("regs=ERR(") + e.what() + ")");
    }
    if (ring_) {
        z.push_back("check=" + Hx(diagRingWert_, 4) + " frames=" + Num(diagRingBilder_) + " lost=" +
                    Num(diagRingVerloren_) + " resets=" + Num(diagRingResets_) + " badcheck=" + Num(diagRingBad_) +
                    " stackhit=" + Num(diagRingStapel_) + " nest=" + Num(diagRingNest_) + " sp_min=" +
                    (diagRingSpMin_ == 0xFFFFFFFF ? std::string("-") : HX(diagRingSpMin_, 6)));
        diagRingNest_ = 0;
        // Ruhewert des Stapelzeigers = haeufigster Wert bisher
        int best = 0;
        for (auto& kv : spHisto_) if (kv.second > best) { best = kv.second; spRuhe_ = kv.first; }
        z.push_back(Gross("busy=" + Num(diagBusy_) + " sp_idle=" + Hx(spRuhe_, 6)));
        diagBusy_ = 0;
        diagRingBilder_ = 0;
        diagRingVerloren_ = 0;
        diagRingResets_ = 0;
        diagRingBad_ = 0;
        diagRingStapel_ = 0;
        diagRingSpMin_ = 0xFFFFFFFF;
    } else if (kopierWeg_ != 0) {
        try {
            const uint32_t pa = 0xFF0000u | diagPruef_;
            const Bytes v = snoop.read(pa, 2);
            const bool mk = snoop.written(pa) && snoop.written(pa + 1);
            std::string h = hexBytes(v);
            h.erase(std::remove(h.begin(), h.end(), ' '), h.end());
            z.push_back("check=" + h + (mk ? "/ok" : "/nomark"));
        } catch (const std::exception& e) {
            z.push_back(std::string("check=ERR(") + e.what() + ")");
        }
    }
    z.push_back("polls=" + Num(diagPolls_) + " poll_ms=" +
                (diagPolls_ ? Fix((double)diagPollSumMs_ / diagPolls_, 1) : std::string("-")) + "/" +
                Num(diagPollMaxMs_) + " changes=" + Num(diagAenderungen_));
    diagPolls_ = 0; diagPollSumMs_ = 0; diagPollMaxMs_ = 0; diagAenderungen_ = 0;

    // Sonic: verpasste Bilder aus den beiden Zaehlern
    auto wert = [&](uint32_t a) -> int { auto it = ram.find(a); return it == ram.end() ? -1 : it->second; };
    if (wert(0xFFFE0C) >= 0 && wert(0xFFFE04) >= 0) {
        const long long vb = ((long long)wert(0xFFFE0C) << 24) | (wert(0xFFFE0D) << 16)
                           | (wert(0xFFFE0E) << 8) | wert(0xFFFE0F);
        const int fc = (wert(0xFFFE04) << 8) | wert(0xFFFE05);
        if (diagVbAlt_ >= 0) {
            const long long dv = vb - diagVbAlt_;
            const int df = (fc - diagFcAlt_ + 65536) % 65536;
            z.push_back("sonic: console=" + Num(dv) + " game=" + Num(df) + " missed=" + Num(dv - df));
        }
        diagVbAlt_ = vb;
        diagFcAlt_ = fc;
    }

    std::vector<std::string> w;
    const std::map<uint32_t, int> sortiert(ram.begin(), ram.end());
    for (auto& kv : sortiert) w.push_back(HX((unsigned)kv.second, 2));
    z.push_back("vals=" + Join(w, " "));
    diagZeile(Join(z, " "));
}

void MonitorWorker::start() {
    running_ = true;
    // Der Bildpuffer haelt nur wenige hundert Millisekunden vor. Hohe
    // Prioritaet, damit Windows die Abfrage seltener so lange anhaelt.
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    std::vector<std::string> versuche;
    auto [edPtr, foundPort] = find_everdrive(port_, &versuche);
    if (!edPtr) {
        emitLog(BEIDE(Fmt(T("Kein EverDrive gefunden (Port %1 und Scan aller Ports erfolglos)."), {port_})));
        // Diagnose: warum jeder Anschluss abgelehnt wurde.
        for (const std::string& v : versuche) emitLog(Zweisprachig("  " + v));
        if (finished) finished();
        return;
    }
    EdSerial& ed = *edPtr;
    port_ = foundPort;
    diagOeffnen();

    mdsnoop::MdSnoop snoop(ed);

    // Core-Erkennung: Build-Byte im Mapper-Fenster.
    std::string coreInfo = "Core: ?";
    bool coreOk = false;
    try {
        const uint8_t b = snoop.build();
        if (b > 0 && b < 0xFF) {
            coreInfo = Fmt(T("Core-Build %1"), {Num(b)});
            coreOk = true;
            coreBuild_ = b;
            if (b >= 132) {
                const Bytes pf = snoop.readReg(0xB003, 1);
                plattform_ = pf.empty() ? 0 : pf[0];
                diagZeile("platform " + Num(plattform_) + " (1 CORE, 2 PRO, 0 unknown)");
                if (plattform_ == 2)
                    emitLog(BEIDE(T("Gerät: Mega EverDrive PRO - Unterstützung testweise, das Ingame-Menü geht dort noch nicht.")));
                else if (plattform_ == 1)
                    emitLog(BEIDE(T("Gerät: Mega EverDrive CORE")));
            }
        } else {
            coreInfo = T("Core: kein RA-Mapper geladen");
        }
    } catch (...) {
        coreInfo = T("Core: Fenster nicht lesbar");
    }
    emitLog(BEIDE(Fmt(T("Verbunden mit %1 (auto-erkannt) | %2"), {port_, coreInfo})));

    // Hardcore-Pruefung: nur zulaessig, wenn In-Game-Menue UND Cheats im
    // EverDrive-System-Menue abgeschaltet sind (B009). Wiederholt sich bei
    // jedem neuen Set, periodisch und direkt vor jeder Hardcore-Buchung. Eine
    // einmal zurueckgestufte Sitzung bleibt Softcore.
    bool hcGeprueft = false;
    auto pruefeHardcore = [&]() {
        if (!hardcore_) return;
        try {
            const uint8_t cs = snoop.ctrlStatus();
            const bool ssOn = cs & 0x01;   // In-Game-Menu erlaubt
            const bool ggOn = cs & 0x02;   // Cheats an
            if (ssOn || ggOn) {
                std::string liste;
                if (ssOn) liste += T("Savestates (\"In Game Menu\")");
                if (ggOn) { if (!liste.empty()) liste += " + "; liste += "Cheats"; }
                hardcore_ = false;
                if (!hcGeprueft)
                    emitLog(BEIDE(Fmt(T("ACHTUNG: %1 sind am EverDrive aktiviert - nur Softcore. Für Hardcore bitte im EverDrive-System-Menü (nicht im In-Game-Menü) deaktivieren, dann Monitor neu starten."), {liste})));
                else
                    emitLog(BEIDE(std::string(T("Hardcore beendet - am EverDrive aktiv: ")) + liste));
                if (hardcoreDowngraded) hardcoreDowngraded(liste);
            }
        } catch (const std::exception& e) {
            // B009 nicht lesbar: kein Hardcore erzwingen, aber warnen.
            hardcore_ = false;
            emitLog(BEIDE(Fmt(T("Hardcore-Status nicht lesbar (%1) - Sitzung läuft als Softcore."), {e.what()})));
            if (hardcoreDowngraded) hardcoreDowngraded(std::string());
        }
        hcGeprueft = true;
    };
    if (coreOk) pruefeHardcore();
    Uhr hcTakt;
    hcTakt.start();

    emitLog(BEIDE(hardcore_ ? T("Modus: Hardcore") : T("Modus: Softcore")));

    // Der ROM-Header wird NICHT ueber USB gelesen; die Verifikation stuetzt
    // sich allein auf das Build-Byte.
    coreVerified_ = coreOk ? 1 : 0;

    // Ohne unseren Mapper gibt es keinen Spiegel. Weiterzulaufen wuerde
    // lauter Nullen liefern - deshalb hier abbrechen.
    if (!coreOk) {
        emitLog(BEIDE(T("ABBRUCH: Der RA-Mapper läuft nicht auf der Konsole.")));
        emitLog(BEIDE(T("Das Spiel muss mit unserem Core gestartet werden:")));
        emitLog(BEIDE(T("  über USB   -  edlink run --file SPIEL.md --fpga mega-core.x25   (PRO: mega-pro.rbf)")));
        emitLog(BEIDE(T("  über SD    -  mega-core.x25 (CORE) bzw. mega-pro.rbf (PRO) in denselben Ordner wie das ROM legen")));
        if (coreMissing) coreMissing();
        ed.close();
        if (finished) finished();
        return;
    }

    const std::string recPath = Datei("recording.csv");
    if (Groesse(recPath) > 2000000) {
        const std::string oldPath = Datei("recording_old.csv");
        std::error_code ec;
        fs::remove(fs::u8path(oldPath), ec);
        fs::rename(fs::u8path(recPath), fs::u8path(oldPath), ec);
    }

    // Spielwechsel: Verlaesst der Nutzer das Spiel, faellt das Build-Byte auf
    // 00/FF. Die Dauerabfrage muss dann enden - laeuft sie weiter, bleibt das
    // naechste Spiel beim Laden schwarz. Zwei Lesungen in Folge.
    Uhr coreTakt;
    coreTakt.start();
    int coreWeg = 0;
    int fehlerFolge = 0;   // Lesefehler in Folge; ab 3 stoppt der Monitor

    while (running_) {
        try {
            // Nur Test (test.ini "wechsel" = Sekunden): abwechselnd mit und
            // ohne Nachher-Modus einrichten.
            static const int wechselS = std::atoi(testSchalter("wechsel").c_str());
            if (wechselS > 0 && kopierWeg_ != 0 && !mapDirty_) {
                if (!wechselUhr_.isValid()) wechselUhr_.start();
                if (wechselUhr_.elapsed() >= wechselS * 1000LL) {
                    postZwang_ = ringPost_ ? 0 : 1;
                    diagZeile("WECHSEL post=" + Num(postZwang_));
                    mapDirty_ = true;
                    wechselUhr_.restart();
                }
            }
            if (coreTakt.elapsed() >= 1000) {
                coreTakt.restart();
                const uint8_t b = snoop.build();
                coreWeg = (b == 0 || b == 0xFF) ? coreWeg + 1 : 0;
                if (coreWeg >= 2) {
                    emitLog(BEIDE(T("Spiel verlassen - Monitor gestoppt. Neues Spiel starten, dann Monitor erneut starten.")));
                    if (gameLeft) gameLeft();
                    break;
                }
            }

            std::unique_lock<std::mutex> lk(gameMtx_);

            if (game_.addr_map.empty()) {
                lk.unlock();
                Schlafe(50);
                continue;
            }

            // Blockzuordnung schreiben, sobald ein neues Set geladen wurde.
            if (mapDirty_) {
                std::vector<uint32_t> addrs;
                addrs.reserve(game_.addr_map.size());
                for (auto& kv : game_.addr_map) addrs.push_back(kv.first);
                for (uint32_t a : diagExtra())
                    if (!game_.addr_map.count(a)) addrs.push_back(a);
                // Die Blockzuordnung braucht nur der alte Mitschnitt und die
                // Wege ohne Bildpuffer.
                bool zuordnungOk = true;
                std::string zuordnungFehler;
                try {
                    auto m = snoop.program(addrs);
                    snoop.resetMarks();
                    emitLog(BEIDE(Fmt(T("Blockzuordnung geschrieben: %1 Adressen in %2 Blöcken."),
                                {Num((long long)addrs.size()), Num((long long)m.size())})));
                } catch (const mdsnoop::SnoopError& e) {
                    zuordnungOk = false;
                    zuordnungFehler = e.what();
                }
                richteKopieEin(snoop, addrs, zuordnungOk);
                if (!running_) {
                    mapFailed_ = true;                   // Stopp waehrend des Einrichtens
                } else if (kopierWeg_ != 0) {
                    mapFailed_ = false;
                } else if (!zuordnungOk) {
                    mapFailed_ = true;
                    emitLog(BEIDE(Fmt(T("Set passt nicht in den Spiegel: %1"), {zuordnungFehler})));
                } else if (!altWegErlaubt()) {
                    // Ohne Kopierprogramm bliebe nur der alte Mitschnitt - der
                    // liefert ausserhalb des CORE keine verlaesslichen Werte.
                    mapFailed_ = true;
                    emitLog(BEIDE(Fmt(T("Dieses Spiel wird auf diesem Gerät noch nicht unterstützt - Achievements werden nicht ausgewertet, damit nichts falsch freigeschaltet wird. Bericht: %1"),
                                {Datei("pro_check.txt")})));
                } else {
                    mapFailed_ = false;
                }
                mapDirty_ = false;
                pruefeHardcore();   // neues Set = moeglicherweise neues Spiel
            }

            if (mapFailed_) {
                lk.unlock();
                Schlafe(500);
                continue;
            }

            // Solange das In-Game-Menue offen ist, NICHT pollen (USB/Menue-
            // Buskonflikt, Build 22).
            if (snoop.menuActive()) {
                lk.unlock();
                Schlafe(100);
                continue;
            }

            if (hardcore_ && hcTakt.elapsed() >= 1000) {
                pruefeHardcore();
                hcTakt.restart();
            }

            // Einen Stand auswerten. liesByte liefert das Byte einer
            // ueberwachten Adresse oder -1. Mit Bildpuffer laeuft das je Bild
            // einmal, in Reihenfolge - wie ein Emulator, der jedes Bild prueft.
            auto verarbeite = [&](const std::function<int(uint32_t)>& liesByte, bool anzeigen) {
                RamMap ram;
                for (auto& kv : game_.addr_map) {
                    const int b = liesByte(kv.first);
                    ram[kv.first] = b < 0 ? 0 : b;
                }

                // Anzeige: genau die ueberwachten Bytes, nach Adresse sortiert.
                if (anzeigen && bramDump) {
                    Bytes dump;
                    dump.reserve(ram.size());
                    const std::map<uint32_t, int> sortedRam(ram.begin(), ram.end());
                    for (auto& kv : sortedRam) dump.push_back((uint8_t)kv.second);
                    bramDump(dump);
                }

                // Nur fuer Tests auf einer PAL-Konsole: test.ini "region" = NTSC
                // setzt RA 0xfff9 Bit6 auf 0.
                static const bool testNtsc = testSchalter("region") == "NTSC";
                if (testNtsc) {
                    auto itr = ram.find(ra_byte_to_phys(0xfff9));
                    if (itr != ram.end()) itr->second &= ~0x40;
                }

                if (havePrevRam_) {
                    // Region: RA 0xfff9 Bit6 (1=PAL/50Hz) wenn ueberwacht
                    const uint32_t rphys = ra_byte_to_phys(0xfff9);
                    auto itp = ram.find(rphys);
                    if (itp != ram.end()) {
                        const bool isPal = (itp->second & 0x40) != 0;
                        if ((isPal ? 1 : 0) != consolePal_) {
                            consolePal_ = isPal ? 1 : 0;
                            if (palState) palState(isPal);
                        }
                    }
                    if (ram != prevRam_) {
                        std::ofstream rec(fs::u8path(recPath), std::ios::app);
                        if (rec) {
                            // volle Millisekunden
                            rec << Fix(JetztMsEpoch() / 1000.0, 3);
                            const std::map<uint32_t, int> sorted(ram.begin(), ram.end());
                            for (auto& kv : sorted) rec << ',' << HX((unsigned)kv.second, 2);
                            rec << '\n';
                        }
                    }
                    for (auto& ac : game_.achievements) {
                        if (ac.triggered || ac.unsupported) continue;
                        const bool hit = ac.rt->update(ram, prevRam_);
                        if (!hit) continue;
                        ac.triggered = true;
                        {
                            // Neben die EXE, nicht in den Arbeitsordner des Aufrufers
                            std::ofstream tl(fs::u8path(Datei("trigger_log.txt")), std::ios::app);
                            if (tl) {
                                tl << "\n[" << Jetzt(false) << "] * " << ac.title << " (ID " << ac.id << ")\n";
                                tl << "  MemAddr: " << ac.mem << "\n";
                            }
                            // Diagnose: Bedingung und alle Werte des ausloesenden Bilds
                            diagZeile("TRIGGER " + ac.title + " (ID " + Num(ac.id) + ") MemAddr: " + ac.mem);
                            std::vector<std::string> w;
                            for (auto& kv : std::map<uint32_t, int>(ram.begin(), ram.end()))
                                w.push_back(HX(kv.first, 6) + "=" + HX((unsigned)kv.second, 2));
                            diagZeile("TRIGGER frame " + Join(w, " "));
                        }
                        // Anti-Cheat: bei nachgewiesen fremder ROM NICHT buchen.
                        if (coreVerified_ == 0) {
                            emitLog(BEIDE(Fmt(T("! %1 NICHT gebucht - ROM-Verifikation fehlgeschlagen"), {ac.title})));
                            if (unlocked) unlocked(ac.title, ac.points, ac.id);
                            continue;
                        }
                        // Nur Test (test.ini "buchen" = 0): nichts an RetroAchievements senden.
                        static const bool nichtBuchen = testSchalter("buchen") == "0";
                        if (nichtBuchen) {
                            emitLog("TEST: " + ac.title + " - not submitted (test.ini buchen=0)");
                            continue;
                        }
                        pruefeHardcore();
                        const bool ok = client_->ra_award(ac.id, user_, token_, hardcore_ ? 1 : 0);
                        if (unlocked) unlocked(ac.title, ac.points, ac.id);
                        emitLog(BEIDE(Fmt(T("Achievement freigeschaltet: %1 (%2)"),
                                    {ac.title, ok ? std::string(hardcore_ ? T("gebucht, Hardcore") : T("gebucht, Softcore"))
                                                  : std::string(T("Buchung fehlgeschlagen, Retry nötig"))})));
                        if (!client_->last_award_reply.empty())
                            emitLog(BEIDE(std::string(T("RA-Antwort: ")) + client_->last_award_reply));
                    }
                }
                // Diagnose: einmal je Sekunde eine Zeile, mit den Zusatzadressen
                if (diagDatei_) {
                    if (havePrevRam_ && ram != prevRam_) ++diagAenderungen_;
                    RamMap diagRam = ram;
                    for (uint32_t a : diagExtra()) {
                        const int b = liesByte(a);
                        if (b >= 0) diagRam[a] = b;
                    }
                    diagSekunde(snoop, diagRam);
                }

                prevRam_ = ram;
                havePrevRam_ = true;
            };

            Uhr pollUhr;
            pollUhr.start();
            auto pollZeit = [&]() {
                const long long d = pollUhr.elapsed();
                ++diagPolls_;
                diagPollSumMs_ += d;
                if (d > diagPollMaxMs_) diagPollMaxMs_ = d;
            };

            if (ring_) {
                // Bildpuffer: Lauf g liegt in Abschnitt g mod Tiefe. Vollstaendig
                // sind alle Laeufe vor dem aktuellen Zaehlerstand; einer, den
                // das Programm waehrend des Lesens schon wieder ueberschrieben
                // haben kann, wird verworfen statt falsch ausgewertet.
                const int tiefe = ringTiefe_;
                const int g1 = snoop.ringStatus() & 0x7F;
                const int neueste = (g1 + 127) % 128;
                // Nach einer Pause von ueber 1,5 s ist der 7-Bit-Zaehler
                // womoeglich uebergelaufen - dann beim neuesten Bild aufsetzen.
                if (ringLetzte_ < 0 || !ringUhr_.isValid() || ringUhr_.elapsed() > 1500) {
                    if (ringLetzte_ >= 0 && ringUhr_.isValid()) {
                        diagRingVerloren_ += (int)(ringUhr_.elapsed() / 17);   // geschaetzt
                        ringLuecke_ = true;
                    }
                    ringLetzte_ = (neueste + 127) % 128;
                }
                ringUhr_.restart();
                int anzahl = (neueste - ringLetzte_ + 128) % 128;
                if (anzahl == 0) {
                    lk.unlock();
                    Schlafe(4);
                    continue;
                }
                if (anzahl > tiefe - 1) {
                    diagRingVerloren_ += anzahl - (tiefe - 1);
                    anzahl = tiefe - 1;
                    ringLuecke_ = true;
                }
                const int erste = (neueste - anzahl + 1 + 128) % 128;
                std::vector<std::pair<int, Bytes>> bilder;
                for (int k = 0; k < anzahl; ++k) {
                    const int g = (erste + k) % 128;
                    bilder.push_back({g, snoop.readRing(g, ringAbschnitt_, (int)ringGroesse_)});
                }
                const int g2 = snoop.ringStatus() & 0x7F;
                if (ringPost_ || ringHint_) {
                    // Kam ein neuer VBlank, waehrend der Hauptteil noch kopierte,
                    // ist ein Abschnitt unvollstaendig. Alle Bilder dieser Abfrage
                    // und die naechsten drei Laeufe verwerfen.
                    const Bytes b4 = snoop.readReg(0xB004, 1);
                    if (!b4.empty() && b4[0] != ringBadAlt_) {
                        diagZeile("NEST vblank during copy (" + Num(ringBadAlt_) + " -> " + Num(b4[0]) +
                                  "), frames up to " + Num((g2 + 3) % 128) + " dropped");
                        ringBadAlt_ = b4[0];
                        ringSperreBis_ = (g2 + 3) % 128;
                        ringSperre_ = true;
                    }
                }
                pollZeit();
                for (size_t k = 0; k < bilder.size(); ++k) {
                    const int g = bilder[k].first;
                    const Bytes& bild = bilder[k].second;
                    ringLetzte_ = g;
                    if (ringSperre_) {
                        if (((ringSperreBis_ - g + 128) % 128) < 64) {
                            ++diagRingNest_;
                            ringLuecke_ = true;
                            continue;
                        }
                        ringSperre_ = false;
                    }
                    if (((g2 - g + 128) % 128) >= tiefe || bild.size() < ringGroesse_) {
                        ++diagRingVerloren_;
                        ringLuecke_ = true;
                        continue;
                    }
                    // Nach einer Luecke fehlen Bilder, in denen eine
                    // Ruecksetzbedingung gegolten haben kann: alle Trefferzaehler
                    // auf 0. Verglichen wird das erste Bild danach mit dem letzten
                    // gueltigen davor - sonst ginge ein Wechsel, der in die Luecke
                    // fiel (z. B. Ringe 19 -> 23), fuer immer verloren.
                    if (ringLuecke_) {
                        for (auto& ac : game_.achievements)
                            if (ac.rt) ac.rt->reset();
                        ringLuecke_ = false;
                        ++diagRingResets_;
                    }
                    ++diagRingBilder_;
                    diagRingWert_ = (uint16_t)((bild[ringPruef_] << 8) | bild[ringPruef_ + 1]);
                    if (diagRingWert_ != hookcopy::kPruefwert) {
                        // Uebertragung dieses Bilds nicht gesichert - verwerfen
                        ++diagRingBad_;
                        ringLuecke_ = true;
                        continue;
                    }
                    if (ringSp_) {
                        // Reicht der Sicherungsbereich unter dem Stapelzeiger
                        // in eine ueberwachte Adresse, stammt deren Wert in
                        // diesem Bild von uns - verwerfen.
                        uint32_t sp = ((uint32_t)bild[ringSp_] << 24) | ((uint32_t)bild[ringSp_ + 1] << 16)
                                    | ((uint32_t)bild[ringSp_ + 2] << 8) | bild[ringSp_ + 3];
                        sp &= 0xFFFFFF;
                        if (sp < diagRingSpMin_) diagRingSpMin_ = sp;
                        // Lagbilder: Wartet das Spiel beim Bildwechsel in Ruhe,
                        // steht der Stapelzeiger immer gleich.
                        ++spHisto_[sp];
                        if (spRuhe_ && sp != spRuhe_) ++diagBusy_;
                        if (sp >= 0xFF0000u) {
                            const uint32_t unten = (sp - (uint32_t)ringStapel_) & 0xFFFF;
                            const uint32_t oben = sp & 0xFFFF;
                            auto it = ringLage_.lower_bound(unten);
                            if (it != ringLage_.end() && it->first < oben) {
                                ++diagRingStapel_;
                                diagZeile(Gross("STACK sp=" + Hx(sp, 6) + " covers ") + Hx(0xFF0000 | unten, 6) + "-" +
                                          Hx(0xFF0000 | (oben - 1), 6) + Gross(", first watched ") +
                                          Hx(0xFF0000 | it->first, 6) + Gross(" - frame dropped"));
                                ringLuecke_ = true;
                                continue;
                            }
                        }
                    }
                    verarbeite([&](uint32_t a) -> int {
                        auto off = mdsnoop::wramOffset(a);
                        if (!off) return -1;
                        auto it = ringLage_.find(*off);
                        if (it == ringLage_.end()) return -1;
                        return bild[it->second];
                    }, k + 1 == bilder.size());
                }
            } else {
                // Nur Test (test.ini "beobachte" = Adresse, z. B. FFFB06): Der
                // Mapper markiert jeden Schreibzugriff des Spiels.
                static const std::string beob = testSchalter("beobachte");
                if (!beob.empty()) {
                    const uint32_t wa = (uint32_t)std::strtoul(beob.c_str(), nullptr, 16);
                    if (snoop.written(wa)) {
                        const Bytes wv = snoop.read(wa, 1);
                        diagZeile(Gross("WATCH " + Hx(wa, 6) + " written by game, value now ") + hexBytes(wv));
                        snoop.resetMarks();
                    }
                }
                // Alle belegten Bloecke am Stueck lesen.
                std::map<int, Bytes> blocks = snoop.readAll();
                pollZeit();
                auto liesAlt = [&](uint32_t a) -> int {
                    auto off = mdsnoop::wramOffset(a);
                    if (!off) return -1;
                    auto it = blocks.find((int)(*off / mdsnoop::kBlockSize));
                    const size_t idx = *off % mdsnoop::kBlockSize;
                    if (it == blocks.end() || idx >= it->second.size()) return -1;
                    return it->second[idx];
                };
                // Dieser Weg hat keinen Pruefwert je Bild. Stehen alle
                // ueberwachten Werte auf FF oder alle auf 00 (toter Bus beim
                // Ausschalten), wird nicht ausgewertet und nicht ueber die
                // Luecke hinweg verglichen.
                bool alleFF = true, alle00 = true;
                int gelesen = 0;
                for (auto& kv : game_.addr_map) {
                    const int b = liesAlt(kv.first);
                    if (b < 0) continue;
                    ++gelesen;
                    if (b != 0xFF) alleFF = false;
                    if (b != 0x00) alle00 = false;
                }
                if (gelesen >= 4 && (alleFF || alle00)) {
                    // Nach dem Start steht der Spiegel auf 00, bis das Spiel schreibt -
                    // melden nur, wenn vorher schon echte Werte da waren.
                    if (!altUnplausibel_ && havePrevRam_)
                        emitLog(BEIDE(T("Unplausible Werte (alle FF oder alle 00) - nicht ausgewertet.")));
                    altUnplausibel_ = true;
                    havePrevRam_ = false;
                } else {
                    altUnplausibel_ = false;
                    verarbeite(liesAlt, true);
                }
            }

            lk.unlock();
            fehlerFolge = 0;
            Schlafe(ring_ ? 8 : 16);
        } catch (const std::exception& e) {
            if (connectionLost) connectionLost(e.what());
            emitLog(BEIDE(std::string(T("Verbindung verloren: ")) + e.what()));
            diagFehler_ = true;
            // Konsole aus oder USB weg: nach etwa 3 s aufgeben, Spielzustand
            // verwerfen wie bei einem Stopp von Hand.
            if (++fehlerFolge >= 3) {
                if (gameLeft) gameLeft();   // "Monitor gestoppt." folgt am Ende
                break;
            }
            Schlafe(1000);
        }
    }
    // Kopierprogramm abschalten - aber nur, solange unser Core noch laeuft.
    if (kopierWeg_ != 0) {
        try {
            if (snoop.build() == coreBuild_) snoop.cpyEnable(false);
        } catch (...) {}
        kopierWeg_ = 0;
        ring_ = false;
    }
    ed.close();
    emitLog(BEIDE(T("Monitor gestoppt.")));
    if (diagFehler_) diagSichern();
    if (finished) finished();
}
