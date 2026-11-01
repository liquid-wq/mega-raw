#include "monitor_worker.h"
#include "i18n.h"
#include <thread>
#include <QTime>
#include "ra_engine.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <fstream>
#include <map>
#include <vector>
#include <QThread>
#include <QStringList>
#include <QTextStream>
#include <QSettings>
#include <QSerialPortInfo>
#include <algorithm>
#include <functional>
#include "hook_copy.h"
#include "version.h"

MonitorWorker::MonitorWorker(QString port, Game game, QString user, QString token,
                             std::shared_ptr<RaClient> client, long long, bool hardcore)
    : hardcore_(hardcore), port_(std::move(port)), game_(std::move(game)),
      user_(std::move(user)), token_(std::move(token)), client_(std::move(client)) {}

void MonitorWorker::stop() {
    running_ = false;
}

void MonitorWorker::writeSessionMarker(const Game& g) {
    // Markiert in recording.csv, wo eine neue Spiel-Session beginnt, damit
    // sich im Nachhinein nachvollziehen laesst, welcher Abschnitt der
    // Rohdaten zu welchem Spiel gehoert.
    std::string path = QDir(QCoreApplication::applicationDirPath())
        .filePath("recording.csv").toStdString();
    std::ofstream rec(path, std::ios::app);
    if (rec) {
        rec << "# --- Session-Start " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString()
            << " | Spiel: " << g.name
            << " | RA-Game-ID: " << g.gameid << " ---\n";
    }
}

// Testschalter aus test.ini neben der EXE (Abschnitt [test]). Fehlt die
// Datei - wie bei jedem normalen Nutzer -, ist alles automatisch.
//   weg    = W | R | ALT   erzwingt einen Weg der Werte-Auslesung
//   region = NTSC          Regionsbyte fuer Tests auf einer PAL-Konsole
//   diag   = <Pfad>        andere Zieldatei fuer die Diagnose
static QString testSchalter(const char* schluessel, bool gross = true) {
    QString ini = QDir(QCoreApplication::applicationDirPath()).filePath("test.ini");
    if (!QFileInfo::exists(ini)) return QString();
    QSettings s(ini, QSettings::IniFormat);
    QString v = s.value(QString("test/") + schluessel).toString().trimmed();
    return gross ? v.toUpper() : v;
}

// Wartet, bis die Freigabe des Kopierprogramms wirksam bzw. aus ist (B00D
// Bit2). Der Mapper uebernimmt sie erst beim naechsten VBlank-Vektor -
// solange laeuft das alte Programm sicher zu Ende.
static bool warteKopieStatus(mdsnoop::MdSnoop& snoop, bool wirksam) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 1000) {
        uint8_t s = snoop.cpyStatus();
        // Beim Abschalten zusaetzlich warten, bis ein ausstehender Hauptteil
        // (Nachher-Modus, Bit 7) gelaufen ist - erst dann darf das Programm
        // ueberschrieben werden.
        if (wirksam ? (s & 0x04) != 0 : (s & 0x84) == 0) return true;
        QThread::msleep(20);
    }
    return false;
}

static QString hexBytes(const QByteArray& b) {
    QStringList l;
    for (char c : b) l << QString("%1").arg(static_cast<uint8_t>(c), 2, 16, QChar('0')).toUpper();
    return l.join(' ');
}

QString MonitorWorker::schreibeBericht(const QStringList& zeilen, bool datei) {
    QString pfad = QDir(QCoreApplication::applicationDirPath()).filePath("pro_check.txt");
    if (datei) {
        QFile f(pfad);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream ts(&f);
            for (const QString& z : zeilen) ts << z << "\n";
        }
    }
    // Derselbe Bericht auch in die Diagnose.
    for (const QString& z : zeilen) diagZeile("CHECK " + z);
    if (datei) diagSichern();
    return pfad;
}

void MonitorWorker::richteKopieEin(mdsnoop::MdSnoop& snoop, const std::vector<uint32_t>& addrs, bool zuordnungOk) {
    kopierWeg_ = 0;
    ring_ = false;
    if (coreBuild_ < 128) {
        emit log(QString(T("Werte-Auslesung: Standardverfahren (Core-Build %1).")).arg(coreBuild_));
        return;
    }

    QStringList bericht;
    bericht << "MEGA-RAW value check"
            << "date       : " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
            << "tool       : " + QString(MEGA_RAW_VERSION) + " (build " + QString::number(MEGA_RAW_CPP_BUILD) + ")"
            << "core build : " + QString::number(coreBuild_)
            << "game       : " + QString::fromStdString(game_.name) + " (RA " + QString::number(game_.gameid) + ")";
    try {
        auto offs = hookcopy::offsets(addrs);
        bericht << QString("addresses  : %1 bytes in %2 blocks").arg(offs.size()).arg(hookcopy::bloecke(offs).size());
        // Diagnose: die vollstaendige Adressliste, damit sich das Programm
        // fuer genau dieses Set spaeter nachrechnen laesst.
        if (diagDatei_.isOpen()) {
            QStringList l;
            for (uint32_t o : offs) l << QString("%1").arg(o, 4, 16, QChar('0')).toUpper();
            bericht << "address list : " + l.join(' ');
        }
    } catch (...) {}

    // Nur fuer Tests: test.ini "weg" = W, R oder ALT erzwingt einen Weg.
    QString vorgabe = testSchalter("weg");
    if (!vorgabe.isEmpty()) {
        emit log(QString(T("Werte-Auslesung per test.ini vorgegeben: %1")).arg(vorgabe));
        bericht << "forced     : " + vorgabe;
    }

    // Diagnose: einige Sekunden den alten Mitschnitt aufzeichnen, bevor ein
    // Programm laeuft - zum Vergleich mit dem, was die neuen Wege liefern.
    auto alleBloecke = [&](const QString& name) {
        for (auto& kv : snoop.readAll())
            bericht << QString("mirror %1 %2 : %3").arg(name)
                           .arg(kv.first * hookcopy::kBlock + 0xFF0000, 6, 16, QChar('0')).toUpper()
                           .arg(hexBytes(kv.second));
    };
    if (diagDatei_.isOpen() && zuordnungOk) {
        try {
            snoop.cpyEnable(false);
            warteKopieStatus(snoop, false);
            snoop.resetMarks();
            // Unterbrechbar: ein Stopp waehrend des Einrichtens muss sofort
            // greifen, sonst raeumt das Fenster einen laufenden Thread ab.
            QElapsedTimer warte;
            warte.start();
            while (running_ && warte.elapsed() < 3000) QThread::msleep(20);
            alleBloecke("OLD");
        } catch (const std::exception& e) {
            bericht << QString("mirror OLD : ERR %1").arg(QString::fromUtf8(e.what()));
        }
    }

    // Reihenfolge: Bildpuffer nach dem VBlank-Teil des Spiels (ab Build 133,
    // gleicher Zeitpunkt wie ein Emulator), dann Bildpuffer davor, dann ohne.
    // Spiele ohne VBlank-Interrupt (z. B. Moonwalker): der VBlank-Zaehler
    // (B00A-B) steht, der HBlank-Zaehler (D0F7) laeuft. Dann am HBlank-Vektor
    // kopieren (ab Build 135), alle anderen Stufen greifen dort nicht.
    bool ohneVblank = false;
    if (coreBuild_ >= mdsnoop::kHintBuild && vorgabe != "ALT") {
        try {
            QByteArray v1 = snoop.readReg(0xB00A, 2), h1 = snoop.readReg(mdsnoop::kHintCntOffset, 1);
            QThread::msleep(300);
            QByteArray v2 = snoop.readReg(0xB00A, 2), h2 = snoop.readReg(mdsnoop::kHintCntOffset, 1);
            ohneVblank = v1 == v2 && !h1.isEmpty() && !h2.isEmpty() && h1 != h2;
            bericht << QString("interrupts : vblank %1 -> %2, hblank %3 -> %4%5")
                           .arg(hexBytes(v1), hexBytes(v2), hexBytes(h1), hexBytes(h2))
                           .arg(ohneVblank ? " (no vblank interrupt - using hblank)" : "");
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
        const QString name = QString(nr == 1 ? "W" : "R") + (v.ring ? "+F" : "") + (v.post ? "+P" : "") + (v.hint ? "+H" : "");
        QString grund;
        try {
            // Nach einem Versuch mit Bildpuffer die normale Zuordnung zurueck
            if (!v.ring && zuordnungWeg) { snoop.program(addrs); zuordnungWeg = false; }
            hookcopy::Programm p;
            try {
                p = hookcopy::erzeuge(weg, addrs, v.ring, v.post, v.hint);
            } catch (const hookcopy::HookCopyError&) {
                grund = T("zu viele Adressen");
            }
            if (grund.isEmpty() && p.takte > (v.ring ? hookcopy::kMaxTakteRing : hookcopy::kMaxTakte))
                grund = QString(T("zu hohe Last (%1)")).arg(p.takte);

            // Erst abschalten und warten, bis es wirksam ist - danach fuehrt
            // die CPU das Programm sicher nicht mehr aus und es darf
            // ueberschrieben werden.
            if (grund.isEmpty()) {
                snoop.cpyEnable(false);
                if (!warteKopieStatus(snoop, false)) grund = T("Cartridge reagiert nicht (Abschalten)");
            }
            if (grund.isEmpty()) {
                std::vector<uint8_t> b = hookcopy::bytes(p);
                QByteArray prog(reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()));
                snoop.cpyUpload(prog);
                QByteArray zurueck = snoop.cpyRead(prog.size());
                int abw = 0;
                for (int i = 0; i < prog.size(); ++i)
                    if (i >= zurueck.size() || zurueck[i] != prog[i]) ++abw;
                if (abw)
                    grund = QString(T("Uebertragung fehlerhaft (%1 Abweichungen)")).arg(abw);
            }
            if (grund.isEmpty()) {
                if (v.ring) {
                    snoop.programRing(static_cast<int>((p.groesse + hookcopy::kBlock - 1) / hookcopy::kBlock));
                    zuordnungWeg = true;
                }
                snoop.resetMarks();
                QThread::msleep(50);
                snoop.cpyEnable(true, v.ring, p.rsz, v.post, v.hint);
                if (!warteKopieStatus(snoop, true)) grund = T("Cartridge reagiert nicht (Einschalten)");
                else if (v.ring && (snoop.cpyStatus() & 0x78) != (0x08 | (p.rsz << 4) | (v.post ? 0x40 : 0)))
                    grund = T("Bildpuffer nicht wirksam");
            }
            if (grund.isEmpty() && v.ring) {
                // Pruefung im zuletzt vollstaendigen Bild. Laeuft der Zaehler
                // nicht weiter, wird das Programm nicht bis zum Ende ausgefuehrt.
                QThread::msleep(200);
                int g1 = snoop.ringStatus() & 0x7F;
                QThread::msleep(100);
                uint8_t s2 = snoop.ringStatus();
                int g2 = s2 & 0x7F;
                int lauf = (g2 - g1 + 128) % 128;
                int letzte = (g2 - 1 + 128) % 128;
                QByteArray bild = snoop.readRing(letzte, p.abschnitt, static_cast<int>(p.groesse));
                int g3 = snoop.ringStatus() & 0x7F;
                uint16_t wert = 0;
                if (bild.size() >= static_cast<int>(p.groesse))
                    wert = static_cast<uint16_t>((static_cast<uint8_t>(bild[p.pruefPos]) << 8)
                                                 | static_cast<uint8_t>(bild[p.pruefPos + 1]));
                bool frisch = ((g3 - letzte + 128) % 128) < p.tiefe;

                QByteArray probe;
                std::vector<uint32_t> sortiert(addrs.begin(), addrs.end());
                std::sort(sortiert.begin(), sortiert.end());
                for (size_t i = 0; i < sortiert.size() && i < 16; ++i) {
                    auto off = mdsnoop::wramOffset(sortiert[i]);
                    auto it = off ? p.lage.find(*off) : p.lage.end();
                    if (it != p.lage.end() && static_cast<int>(it->second) < bild.size())
                        probe.append(bild[it->second]);
                }
                bericht << QString("mode %1   : %2 words, load %3, check %4, frames/100ms %5, size %6")
                               .arg(name).arg(p.worte.size()).arg(p.takte)
                               .arg(wert, 4, 16, QChar('0')).arg(lauf).arg(p.groesse)
                        << QString("values %1 : %2").arg(name).arg(hexBytes(probe));
                if (diagDatei_.isOpen())
                    bericht << QString("frame %1  : %2").arg(name).arg(hexBytes(bild));

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
                        QByteArray b4 = snoop.readReg(0xB004, 1);
                        ringBadAlt_ = b4.isEmpty() ? 0 : static_cast<uint8_t>(b4[0]);
                    }
                    ringAbschnitt_ = p.abschnitt;
                    ringTiefe_ = p.tiefe;
                    ringLetzte_ = -1;
                    ringLuecke_ = false;
                    emit log(QString(T("Werte-Auslesung: Modus %1 aktiv (Last %2)."))
                                 .arg(name).arg(p.takte));
                    bericht << QString("frame ring : %1 frames x %2 bytes").arg(p.tiefe).arg(p.abschnitt)
                            << "result     : mode " + name + " active";
                    schreibeBericht(bericht, false);
                    return;
                }
                grund = lauf == 0 ? T("Bildzaehler steht")
                                  : QString(T("Pruefwert %1 statt A55A"))
                                        .arg(QString("%1").arg(wert, 4, 16, QChar('0')).toUpper());
            }
            if (grund.isEmpty()) {
                QThread::msleep(200);
                const uint32_t pa = 0xFF0000u | p.pruefOff;
                QByteArray v = snoop.read(pa, 2);
                bool mk = snoop.written(pa) && snoop.written(pa + 1);
                uint16_t wert = static_cast<uint16_t>((static_cast<uint8_t>(v[0]) << 8) | static_cast<uint8_t>(v[1]));

                QByteArray probe;
                std::vector<uint32_t> sortiert(addrs.begin(), addrs.end());
                std::sort(sortiert.begin(), sortiert.end());
                for (size_t i = 0; i < sortiert.size() && i < 16; ++i) probe.append(snoop.read(sortiert[i], 1));

                bericht << QString("mode %1     : %2 words, load %3, check %4, marks %5")
                               .arg(name).arg(p.worte.size()).arg(p.takte)
                               .arg(wert, 4, 16, QChar('0')).arg(mk ? "ok" : "missing")
                        << QString("values %1   : %2").arg(name).arg(hexBytes(probe));
                if (diagDatei_.isOpen()) alleBloecke(name);

                if (wert == hookcopy::kPruefwert && mk) {
                    kopierWeg_ = nr;
                    diagPruef_ = p.pruefOff;
                    emit log(QString(T("Werte-Auslesung: Modus %1 aktiv (Last %2)."))
                                 .arg(name).arg(p.takte));
                    bericht << "result     : mode " + name + " active";
                    schreibeBericht(bericht, false);
                    return;
                }
                grund = QString(T("Pruefwert %1 statt A55A"))
                            .arg(QString("%1").arg(wert, 4, 16, QChar('0')).toUpper());
            }
        } catch (const std::exception& e) {
            grund = QString::fromUtf8(e.what());
        }
        emit log(QString(T("Werte-Auslesung Modus %1 nicht verwendbar: %2")).arg(name).arg(grund));
        bericht << "mode " + name + "     : not usable - " + grund;
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
        bericht << "183B00D    : " + hexBytes(snoop.readReg(mdsnoop::kCpyCtlOffset, 1))
                << "183B001    : " + hexBytes(snoop.readReg(mdsnoop::kStatOffset, 1))
                << "183B00A-B  : " + hexBytes(snoop.readReg(0xB00A, 2))
                << "183D0F0-F5 : " + hexBytes(snoop.readReg(0xD0F0, 6));
    } catch (...) {}
    bericht << QString("platform   : %1").arg(plattform_)
            << (altWegErlaubt() && zuordnungOk ? "result     : standard method"
                                               : "result     : not supported on this device");
    QString pfad = schreibeBericht(bericht);
    if (!wege.empty() && altWegErlaubt() && zuordnungOk)
        emit log(QString(T("Werte-Auslesung: kein Modus verwendbar - Standardverfahren. Bericht: %1")).arg(pfad));
}

// Diagnosestation. Laeuft immer mit (gleiche Ablaeufe wie im Testbau), die
// Zeilen bleiben aber im Speicher; eine Datei entsteht nur bei einem Fehler.
static constexpr bool kDiagnose = true;
static constexpr int kDiagMax = 4 * 1024 * 1024;   // Bytes im Speicher

void MonitorWorker::diagOeffnen() {
    if (!kDiagnose || diagDatei_.isOpen()) return;
    if (!diagDatei_.open(QIODevice::WriteOnly)) return;
    diagUhr_.start();
    diagTakt_.start();
    // Jede Logzeile des Werkzeugs landet zusaetzlich in der Datei.
    connect(this, &MonitorWorker::log, this, [this](const QString& m) { diagZeile("LOG " + m); },
            Qt::DirectConnection);
    diagZeile(QString("=== session %1 | tool %2 (build %3) | port %4")
                  .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))
                  .arg(MEGA_RAW_VERSION).arg(MEGA_RAW_CPP_BUILD).arg(port_));
    // Geraetekennung - zeigt, ob ein PRO oder ein CORE haengt.
    for (const QSerialPortInfo& pi : QSerialPortInfo::availablePorts()) {
        if (pi.portName() != port_) continue;
        diagZeile(QString("device %1 | %2 | vid %3 pid %4 | serial %5")
                      .arg(pi.description(), pi.manufacturer())
                      .arg(pi.vendorIdentifier(), 4, 16, QChar('0'))
                      .arg(pi.productIdentifier(), 4, 16, QChar('0'))
                      .arg(pi.serialNumber()));
    }
    {
        QString ini = QDir(QCoreApplication::applicationDirPath()).filePath("test.ini");
        diagZeile(QFileInfo::exists(ini)
                      ? QString("test.ini: weg=%1 region=%2").arg(testSchalter("weg"), testSchalter("region"))
                      : QString("test.ini: none (automatic)"));
    }
    // Kopfzeilen getrennt halten - sie bleiben erhalten, auch wenn der
    // Speicher spaeter gekuerzt wird.
    diagKopf_ = diagDatei_.buffer();
    diagDatei_.buffer().clear();
    diagDatei_.seek(0);
}

void MonitorWorker::diagZeile(const QString& z) {
    if (!diagDatei_.isOpen()) return;
    QString zeit = QString::number(diagUhr_.elapsed() / 1000.0, 'f', 3);
    diagDatei_.write((zeit + " " + z + "\n").toUtf8());
    // Begrenzen: wird es zu viel, die aeltere Haelfte verwerfen (an einer
    // Zeilengrenze). Die Kopfzeilen liegen getrennt in diagKopf_.
    QByteArray& b = diagDatei_.buffer();
    if (b.size() > kDiagMax) {
        int schnitt = b.indexOf('\n', b.size() - kDiagMax / 2);
        if (schnitt > 0) b.remove(0, schnitt + 1);
        diagDatei_.seek(b.size());
    }
}

// Gesammelte Diagnose als diagnose_live.txt neben die EXE schreiben.
void MonitorWorker::diagSichern() {
    if (!diagDatei_.isOpen()) return;
    QFile f(QDir(QCoreApplication::applicationDirPath()).filePath("diagnose_live.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(diagKopf_);
    f.write(diagDatei_.buffer());
}

// Zusaetzlich ueberwachte Adressen. Sonic 1 (RA 1): Bildzaehler des Spiels
// $FFFE04 (Wort) und VBlank-Zaehler $FFFE0C (Langwort) aus der Disassembly -
// daraus ergibt sich, wie viele Bilder das Spiel verpasst.
std::vector<uint32_t> MonitorWorker::diagExtra() const {
    if (!kDiagnose || game_.gameid != 1) return {};
    return {0xFFFE04, 0xFFFE05, 0xFFFE0C, 0xFFFE0D, 0xFFFE0E, 0xFFFE0F};
}

void MonitorWorker::diagSekunde(mdsnoop::MdSnoop& snoop, const RamMap& ram) {
    if (!diagDatei_.isOpen() || diagTakt_.elapsed() < 1000) return;
    const qint64 ms = diagTakt_.restart();
    QStringList z;
    z << QString("mode=%1%2").arg(kopierWeg_ == 1 ? "W" : kopierWeg_ == 2 ? "R" : "STD")
                             .arg(QString(ring_ ? "+F" : "") + (ring_ && ringPost_ ? "+P" : "") + (ring_ && ringHint_ ? "+H" : ""));
    try {
        QByteArray b00 = snoop.readReg(0xB000, 16);
        QByteArray d0  = snoop.readReg(0xD0F0, 16);
        int hok = static_cast<uint8_t>(b00[11]);
        int vbl = diagHokAlt_ < 0 ? -1 : (hok - diagHokAlt_ + 256) % 256;
        diagHokAlt_ = hok;
        // VDP-Register, wie der Mapper sie mitschneidet (auf dem CORE
        // verlaesslich): R0 Bit4 = HBlank-Interrupt an, R1 Bit5 = VBlank-
        // Interrupt an, R10 = HBlank-Abstand in Zeilen.
        QByteArray vr = snoop.readReg(0xC000, 11);
        if (vr.size() >= 11)
            z << QString("vdp r0=%1 r1=%2 r10=%3")
                     .arg(static_cast<uint8_t>(vr[0]), 2, 16, QChar('0'))
                     .arg(static_cast<uint8_t>(vr[1]), 2, 16, QChar('0'))
                     .arg(static_cast<uint8_t>(vr[10]), 2, 16, QChar('0'));
        z << "b000=" + hexBytes(b00.left(2)) << "b009=" + hexBytes(b00.mid(9, 1))
          << "b00d=" + hexBytes(b00.mid(13, 1))
          << QString("vbl/s=%1").arg(vbl < 0 ? QString("-") : QString::number(vbl * 1000.0 / ms, 'f', 1))
          << "sav,cod,ram=" + hexBytes(d0.left(6)) << "cmax=" + hexBytes(d0.mid(6, 1))
          << "bus=" + hexBytes(d0.mid(8, 4)) << "pad=" + hexBytes(d0.mid(12, 3));
    } catch (const std::exception& e) {
        z << QString("regs=ERR(%1)").arg(QString::fromUtf8(e.what()));
    }
    if (ring_) {
        // Bilder: je Sekunde ausgewertet bzw. nicht rechtzeitig gelesen
        z << QString("check=%1 frames=%2 lost=%3 resets=%4 badcheck=%5 stackhit=%6 nest=%8 sp_min=%7")
                 .arg(diagRingWert_, 4, 16, QChar('0')).arg(diagRingBilder_).arg(diagRingVerloren_)
                 .arg(diagRingResets_).arg(diagRingBad_).arg(diagRingStapel_)
                 .arg(diagRingSpMin_ == 0xFFFFFFFF ? QString("-")
                                                   : QString("%1").arg(diagRingSpMin_, 6, 16, QChar('0')).toUpper())
                 .arg(diagRingNest_);
        diagRingNest_ = 0;
        // Ruhewert des Stapelzeigers = haeufigster Wert bisher
        int best = 0;
        for (auto& kv : spHisto_) if (kv.second > best) { best = kv.second; spRuhe_ = kv.first; }
        z << QString("busy=%1 sp_idle=%2").arg(diagBusy_)
                 .arg(spRuhe_, 6, 16, QChar('0')).toUpper();
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
            QByteArray v = snoop.read(pa, 2);
            bool mk = snoop.written(pa) && snoop.written(pa + 1);
            z << "check=" + hexBytes(v).remove(' ') + (mk ? "/ok" : "/nomark");
        } catch (const std::exception& e) {
            z << QString("check=ERR(%1)").arg(QString::fromUtf8(e.what()));
        }
    }
    z << QString("polls=%1 poll_ms=%2/%3 changes=%4")
             .arg(diagPolls_)
             .arg(diagPolls_ ? QString::number(double(diagPollSumMs_) / diagPolls_, 'f', 1) : QString("-"))
             .arg(diagPollMaxMs_).arg(diagAenderungen_);
    diagPolls_ = 0; diagPollSumMs_ = 0; diagPollMaxMs_ = 0; diagAenderungen_ = 0;

    // Sonic: verpasste Bilder aus den beiden Zaehlern
    auto wert = [&](uint32_t a) -> int { auto it = ram.find(a); return it == ram.end() ? -1 : it->second; };
    if (wert(0xFFFE0C) >= 0 && wert(0xFFFE04) >= 0) {
        long long vb = (static_cast<long long>(wert(0xFFFE0C)) << 24) | (wert(0xFFFE0D) << 16)
                     | (wert(0xFFFE0E) << 8) | wert(0xFFFE0F);
        int fc = (wert(0xFFFE04) << 8) | wert(0xFFFE05);
        if (diagVbAlt_ >= 0) {
            long long dv = vb - diagVbAlt_;
            int df = (fc - diagFcAlt_ + 65536) % 65536;
            z << QString("sonic: console=%1 game=%2 missed=%3").arg(dv).arg(df).arg(dv - df);
        }
        diagVbAlt_ = vb;
        diagFcAlt_ = fc;
    }

    QStringList w;
    std::map<uint32_t, int> sortiert(ram.begin(), ram.end());
    for (auto& kv : sortiert)
        w << QString("%1").arg(kv.second, 2, 16, QChar('0')).toUpper();
    z << "vals=" + w.join(' ');
    diagZeile(z.join(' '));
}

void MonitorWorker::start() {
    running_ = true;
    // Der Bildpuffer haelt nur wenige hundert Millisekunden vor. Hohe
    // Prioritaet, damit Windows die Abfrage seltener so lange anhaelt.
    QThread::currentThread()->setPriority(QThread::HighestPriority);

    auto [edPtr, foundPort] = find_everdrive(port_);
    if (!edPtr) {
        emit log(QString(T("Kein EverDrive gefunden (Port %1 und Scan aller Ports erfolglos).")).arg(port_));
        emit finished();
        return;
    }
    EdSerial& ed = *edPtr;
    port_ = foundPort;
    diagOeffnen();

    mdsnoop::MdSnoop snoop(ed);

    // Core-Erkennung: Build-Byte im Mapper-Fenster.
    QString coreInfo = "Core: ?";
    bool coreOk = false;
    try {
        uint8_t b = snoop.build();
        if (b > 0 && b < 0xFF) {
            coreInfo = QString(T("Core-Build %1")).arg(b);
            coreOk = true;
            coreBuild_ = b;
            if (b >= 132) {
                QByteArray pf = snoop.readReg(0xB003, 1);
                plattform_ = pf.isEmpty() ? 0 : static_cast<uint8_t>(pf[0]);
                diagZeile(QString("platform %1 (1 CORE, 2 PRO, 0 unknown)").arg(plattform_));
                if (plattform_ == 2)
                    emit log(T("Geraet: Mega EverDrive PRO - Unterstuetzung testweise, das Ingame-Menue geht dort noch nicht."));
                else if (plattform_ == 1)
                    emit log(T("Geraet: Mega EverDrive CORE"));
            }
        } else {
            coreInfo = T("Core: kein RA-Mapper geladen");
        }
    } catch (...) {
        coreInfo = T("Core: Fenster nicht lesbar");
    }
    emit log(QString(T("Verbunden mit %1 (auto-erkannt) | %2")).arg(port_).arg(coreInfo));

    // Hardcore-Pruefung (Build 20): Der Mapper hat jetzt ein In-Game-Menue
    // mit Savestates. Hardcore ist nur zulaessig, wenn der Nutzer das
    // In-Game-Menue UND Cheats in Krikzz' EverDrive-System-Menue
    // abgeschaltet hat. Der Mapper spiegelt beides nach B009. Die
    // Entscheidung faellt hier in der Software, nicht im Mapper.
    // Die Pruefung laeuft nicht nur beim Start: waehrend einer Sitzung kann
    // der Nutzer ueber das EverDrive-Menue ein anderes Spiel mit anderen
    // Einstellungen starten. Deshalb wiederholt sie sich bei jedem neuen
    // Set, periodisch und direkt vor jeder Hardcore-Buchung. Eine einmal
    // zurueckgestufte Sitzung bleibt Softcore.
    bool hcGeprueft = false;
    auto pruefeHardcore = [&]() {
        if (!hardcore_) return;
        try {
            uint8_t cs = snoop.ctrlStatus();
            bool ssOn = cs & 0x01;   // In-Game-Menu erlaubt
            bool ggOn = cs & 0x02;   // Cheats an
            if (ssOn || ggOn) {
                QString liste;
                if (ssOn) liste += T("Savestates (\"In Game Menu\")");
                if (ggOn) { if (!liste.isEmpty()) liste += " + "; liste += "Cheats"; }
                hardcore_ = false;
                if (!hcGeprueft)
                    emit log(QString(T("ACHTUNG: %1 sind am EverDrive aktiviert - nur Softcore. Fuer Hardcore bitte im EverDrive-System-Menue (nicht im In-Game-Menue) deaktivieren, dann Monitor neu starten.")).arg(liste));
                else
                    emit log(T("Hardcore beendet - am EverDrive aktiv: ") + liste);
                emit hardcoreDowngraded(liste);
            }
        } catch (const std::exception& e) {
            // B009 nicht lesbar: kein Hardcore erzwingen, aber warnen.
            hardcore_ = false;
            emit log(QString(T("Hardcore-Status nicht lesbar (%1) - Sitzung laeuft als Softcore."))
                     .arg(QString::fromUtf8(e.what())));
            emit hardcoreDowngraded(QString());
        }
        hcGeprueft = true;
    };
    if (coreOk) pruefeHardcore();
    QElapsedTimer hcTakt;
    hcTakt.start();

    emit log(hardcore_ ? T("Modus: Hardcore") : T("Modus: Softcore"));

    // Der ROM-Header wird NICHT ueber USB gelesen: Konsole und PI greifen auf
    // dasselbe PSRAM zu, der Mapper vermittelt das nicht, und ein Lesezugriff
    // bei laufendem Spiel fuehrt zum Absturz (Adressfehler auf dem 68000).
    // Die Verifikation stuetzt sich daher allein auf das Build-Byte.
    coreVerified_ = coreOk ? 1 : 0;

    // Ohne unseren Mapper gibt es keinen Spiegel. Weiterzulaufen wuerde
    // lauter Nullen liefern und wie ein Fehler im Set oder im Werkzeug
    // aussehen - deshalb hier abbrechen statt still falsch zu arbeiten.
    if (!coreOk) {
        emit log(T("ABBRUCH: Der RA-Mapper laeuft nicht auf der Konsole."));
        emit log(T("Das Spiel muss mit unserem Core gestartet werden:"));
        emit log(T("  ueber USB   -  edlink run --file SPIEL.md --fpga mega-core.x25   (PRO: mega-pro.rbf)"));
        emit log(T("  ueber SD    -  mega-core.x25 (CORE) bzw. mega-pro.rbf (PRO) in denselben Ordner wie das ROM legen"));
        emit coreMissing();
        ed.close();
        emit finished();
        return;
    }

    std::string recPath_ = QDir(QCoreApplication::applicationDirPath())
        .filePath("recording.csv").toStdString();
    {
        QFileInfo fi(QString::fromStdString(recPath_));
        if (fi.exists() && fi.size() > 2000000) {
            QString oldPath = QDir(QCoreApplication::applicationDirPath()).filePath("recording_old.csv");
            QFile::remove(oldPath);
            QFile::rename(QString::fromStdString(recPath_), oldPath);
        }
    }

    // Spielwechsel: Verlaesst der Nutzer das Spiel, laedt das EverDrive sein
    // eigenes System, und das Build-Byte faellt auf 00/FF. Die Dauerabfrage
    // muss dann enden - laeuft sie weiter, bleibt das naechste Spiel beim
    // Laden schwarz. Zwei Lesungen in Folge, damit ein einzelner Ausreisser
    // die Sitzung nicht beendet.
    QElapsedTimer coreTakt;
    coreTakt.start();
    int coreWeg = 0;

    while (running_) {
        try {
            // Nur Test (test.ini "wechsel" = Sekunden): abwechselnd mit und ohne
            // Nachher-Modus einrichten - ein Spiellauf vergleicht beide.
            static const int wechselS = testSchalter("wechsel").toInt();
            if (wechselS > 0 && kopierWeg_ != 0 && !mapDirty_) {
                if (!wechselUhr_.isValid()) wechselUhr_.start();
                if (wechselUhr_.elapsed() >= wechselS * 1000LL) {
                    postZwang_ = ringPost_ ? 0 : 1;
                    diagZeile(QString("WECHSEL post=%1").arg(postZwang_));
                    mapDirty_ = true;
                    wechselUhr_.restart();
                }
            }
            if (coreTakt.elapsed() >= 1000) {
                coreTakt.restart();
                uint8_t b = snoop.build();
                coreWeg = (b == 0 || b == 0xFF) ? coreWeg + 1 : 0;
                if (coreWeg >= 2) {
                    emit log(T("Spiel verlassen - Monitor gestoppt. Neues Spiel starten, dann Monitor erneut starten."));
                    emit gameLeft();
                    break;
                }
            }

            QMutexLocker lk(&gameMtx_);

            if (game_.addr_map.empty()) {
                lk.unlock();
                QThread::msleep(50);
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
                // Wege ohne Bildpuffer. Passt sie nicht (mehr als 128 Bloecke),
                // kann der Bildpuffer das Set trotzdem tragen.
                bool zuordnungOk = true;
                QString zuordnungFehler;
                try {
                    auto m = snoop.program(addrs);
                    snoop.resetMarks();
                    emit log(QString(T("Blockzuordnung geschrieben: %1 Adressen in %2 Bloecken."))
                             .arg(addrs.size()).arg(m.size()));
                } catch (const mdsnoop::SnoopError& e) {
                    zuordnungOk = false;
                    zuordnungFehler = QString::fromUtf8(e.what());
                }
                richteKopieEin(snoop, addrs, zuordnungOk);
                if (!running_) {
                    mapFailed_ = true;                   // Stopp waehrend des Einrichtens
                } else if (kopierWeg_ != 0) {
                    mapFailed_ = false;
                } else if (!zuordnungOk) {
                    mapFailed_ = true;
                    emit log(QString(T("Set passt nicht in den Spiegel: %1")).arg(zuordnungFehler));
                } else if (!altWegErlaubt()) {
                    // Ohne Kopierprogramm bliebe nur der alte Mitschnitt - der
                    // liefert ausserhalb des CORE keine verlaesslichen Werte.
                    mapFailed_ = true;
                    emit log(QString(T("Dieses Spiel wird auf diesem Geraet noch nicht unterstuetzt - Achievements werden nicht ausgewertet, damit nichts falsch freigeschaltet wird. Bericht: %1"))
                                 .arg(QDir(QCoreApplication::applicationDirPath()).filePath("pro_check.txt")));
                } else {
                    mapFailed_ = false;
                }
                mapDirty_ = false;
                pruefeHardcore();   // neues Set = moeglicherweise neues Spiel
            }

            if (mapFailed_) {
                lk.unlock();
                QThread::msleep(500);
                continue;
            }

            // Build 22: Solange das In-Game-Menue offen ist, NICHT pollen.
            // Der Menue-Einsprung schaltet VRAM/Register um; ein gleichzeitiger
            // USB-Lesezugriff kollidiert damit und friert die Konsole ein
            // (Sprite flackert, dann Freeze). Der Poll wartet, bis das Menue
            // wieder zu ist - Achievements pausieren solange ohnehin sinnvoll.
            if (snoop.menuActive()) {
                lk.unlock();
                QThread::msleep(100);
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
                int b = liesByte(kv.first);
                ram[kv.first] = b < 0 ? 0 : b;
            }

            // Anzeige: genau die ueberwachten Bytes, nach Adresse sortiert -
            // nicht ein beliebiger Ausschnitt eines Blocks.
            if (anzeigen) {
                QByteArray dump;
                dump.reserve(static_cast<int>(ram.size()));
                std::map<uint32_t,int> sortedRam(ram.begin(), ram.end());
                for (auto& kv : sortedRam) dump.append(static_cast<char>(kv.second));
                emit bramDump(dump);
            }

            // Nur fuer Tests auf einer PAL-Konsole: test.ini "region" = NTSC setzt
            // RA 0xfff9 Bit6 auf 0, damit regionsgebundene Achievements
            // pruefbar sind. Ohne die Variable bleibt alles unveraendert.
            static const bool testNtsc =
                testSchalter("region") == "NTSC";
            if (testNtsc) {
                auto itr = ram.find(ra_byte_to_phys(0xfff9));
                if (itr != ram.end()) itr->second &= ~0x40;
            }

            if (havePrevRam_) {
                // Region: RA 0xfff9 Bit6 (1=PAL/50Hz) wenn ueberwacht
                uint32_t rphys = ra_byte_to_phys(0xfff9);
                auto itp = ram.find(rphys);
                if (itp != ram.end()) {
                    bool isPal = (itp->second & 0x40) != 0;
                    if ((isPal ? 1 : 0) != consolePal_) { consolePal_ = isPal ? 1 : 0; emit palState(isPal); }
                }
                if (ram != prevRam_) {
                    std::ofstream rec(recPath_, std::ios::app);
                    if (rec) {
                        // volle Millisekunden - der Standard (6 Stellen) rundet auf ~1000 s
                        rec << QString::number(QDateTime::currentMSecsSinceEpoch() / 1000.0, 'f', 3).toStdString();
                        std::map<uint32_t,int> sorted(ram.begin(), ram.end());
                        for (auto& kv : sorted)
                            rec << ',' << QString("%1").arg(kv.second,2,16,QChar('0')).toUpper().toStdString();
                        rec << '\n';
                    }
                }
                for (auto& ac : game_.achievements) {
                    if (ac.triggered || ac.unsupported) continue;
                    bool hit = ac.rt->update(ram, prevRam_);
                    if (hit) {
                        ac.triggered = true;
                        {
                            // Neben die EXE, nicht in den Arbeitsordner des Aufrufers
                            std::ofstream tl(QDir(QCoreApplication::applicationDirPath())
                                                 .filePath("trigger_log.txt").toStdString(), std::ios::app);
                            if (tl) {
                                tl << "\n[" << QTime::currentTime().toString("HH:mm:ss").toStdString()
                                   << "] * " << ac.title << " (ID " << ac.id << ")\n";
                                tl << "  MemAddr: " << ac.mem << "\n";
                            }
                            // Diagnose: Bedingung und alle Werte des ausloesenden Bilds
                            diagZeile(QString("TRIGGER %1 (ID %2) MemAddr: %3")
                                          .arg(QString::fromStdString(ac.title)).arg(ac.id)
                                          .arg(QString::fromStdString(ac.mem)));
                            QStringList w;
                            for (auto& kv : std::map<uint32_t, int>(ram.begin(), ram.end()))
                                w << QString("%1=%2").arg(kv.first, 6, 16, QChar('0')).toUpper()
                                                     .arg(kv.second, 2, 16, QChar('0')).toUpper();
                            diagZeile("TRIGGER frame " + w.join(' '));
                        }
                        // Anti-Cheat: bei nachgewiesen fremder ROM NICHT buchen.
                        // coreVerified_==0 blockiert; -1 (nicht pruefbar) failsafe bucht.
                        // Hardcore wird unmittelbar vor der Buchung noch
                        // einmal gegen die EverDrive-Einstellungen geprueft.
                        if (coreVerified_ == 0) {
                            emit log(QString(T("! %1 NICHT gebucht - ROM-Verifikation fehlgeschlagen"))
                                     .arg(QString::fromStdString(ac.title)));
                            emit unlocked(QString::fromStdString(ac.title), ac.points, ac.id);
                            continue;
                        }
                        // Nur Test (test.ini "buchen" = 0): ausloesen und
                        // protokollieren, aber nichts an RetroAchievements senden.
                        static const bool nichtBuchen = testSchalter("buchen") == "0";
                        if (nichtBuchen) {
                            emit log(QString("TEST: %1 - not submitted (test.ini buchen=0)")
                                         .arg(QString::fromStdString(ac.title)));
                            continue;
                        }
                        pruefeHardcore();
                        bool ok = client_->ra_award(ac.id, user_.toStdString(), token_.toStdString(),
                                                    hardcore_ ? 1 : 0);
                        emit unlocked(QString::fromStdString(ac.title), ac.points, ac.id);
                        emit log(QString(T("Achievement freigeschaltet: %1 (%2)"))
                            .arg(QString::fromStdString(ac.title))
                            .arg(ok ? (hardcore_ ? T("gebucht, Hardcore") : T("gebucht, Softcore"))
                                 : T("Buchung fehlgeschlagen, Retry noetig")));
                        if (!client_->last_award_reply.empty())
                            emit log(T("RA-Antwort: ") + QString::fromStdString(client_->last_award_reply));
                    }
                }
            }
            // Diagnose: einmal je Sekunde eine Zeile, mit den Zusatzadressen
            if (diagDatei_.isOpen()) {
                if (havePrevRam_ && ram != prevRam_) ++diagAenderungen_;
                RamMap diagRam = ram;
                for (uint32_t a : diagExtra()) {
                    int b = liesByte(a);
                    if (b >= 0) diagRam[a] = b;
                }
                diagSekunde(snoop, diagRam);
            }

            prevRam_ = ram;
            havePrevRam_ = true;
            };

            QElapsedTimer pollUhr;
            pollUhr.start();
            auto pollZeit = [&]() {
                qint64 d = pollUhr.elapsed();
                ++diagPolls_;
                diagPollSumMs_ += d;
                if (d > diagPollMaxMs_) diagPollMaxMs_ = d;
            };

            if (ring_) {
                // Bildpuffer: Lauf g liegt in Abschnitt g & 7. Vollstaendig
                // sind alle Laeufe vor dem aktuellen Zaehlerstand; einer, den
                // das Programm waehrend des Lesens schon wieder ueberschrieben
                // haben kann, wird verworfen statt falsch ausgewertet.
                const int tiefe = ringTiefe_;
                int g1 = snoop.ringStatus() & 0x7F;
                int neueste = (g1 + 127) % 128;
                // Nach einer Pause von ueber 1,5 s ist der 7-Bit-Zaehler
                // womoeglich uebergelaufen - dann beim neuesten Bild aufsetzen.
                if (ringLetzte_ < 0 || !ringUhr_.isValid() || ringUhr_.elapsed() > 1500) {
                    if (ringLetzte_ >= 0 && ringUhr_.isValid()) {
                        diagRingVerloren_ += static_cast<int>(ringUhr_.elapsed() / 17);   // geschaetzt
                        ringLuecke_ = true;
                    }
                    ringLetzte_ = (neueste + 127) % 128;
                }
                ringUhr_.restart();
                int anzahl = (neueste - ringLetzte_ + 128) % 128;
                if (anzahl == 0) {
                    lk.unlock();
                    QThread::msleep(4);
                    continue;
                }
                if (anzahl > tiefe - 1) {
                    diagRingVerloren_ += anzahl - (tiefe - 1);
                    anzahl = tiefe - 1;
                    ringLuecke_ = true;
                }
                int erste = (neueste - anzahl + 1 + 128) % 128;
                std::vector<std::pair<int, QByteArray>> bilder;
                for (int k = 0; k < anzahl; ++k) {
                    int g = (erste + k) % 128;
                    bilder.push_back({g, snoop.readRing(g, ringAbschnitt_, static_cast<int>(ringGroesse_))});
                }
                int g2 = snoop.ringStatus() & 0x7F;
                if (ringPost_ || ringHint_) {
                    // Nachher-Modus / HBlank-Betrieb: Kam ein neuer VBlank, waehrend der Hauptteil
                    // noch kopierte, ist ein Abschnitt unvollstaendig. Alle Bilder
                    // dieser Abfrage und die naechsten drei Laeufe verwerfen.
                    QByteArray b4 = snoop.readReg(0xB004, 1);
                    if (!b4.isEmpty() && static_cast<uint8_t>(b4[0]) != ringBadAlt_) {
                        diagZeile(QString("NEST vblank during copy (%1 -> %2), frames up to %3 dropped")
                                      .arg(ringBadAlt_).arg(static_cast<uint8_t>(b4[0])).arg((g2 + 3) % 128));
                        ringBadAlt_ = static_cast<uint8_t>(b4[0]);
                        ringSperreBis_ = (g2 + 3) % 128;
                        ringSperre_ = true;
                    }
                }
                pollZeit();
                for (size_t k = 0; k < bilder.size(); ++k) {
                    const int g = bilder[k].first;
                    const QByteArray& bild = bilder[k].second;
                    ringLetzte_ = g;
                    if (ringSperre_) {
                        if (((ringSperreBis_ - g + 128) % 128) < 64) {
                            ++diagRingNest_;
                            ringLuecke_ = true;
                            continue;
                        }
                        ringSperre_ = false;
                    }
                    if (((g2 - g + 128) % 128) >= tiefe || bild.size() < static_cast<int>(ringGroesse_)) {
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
                    diagRingWert_ = static_cast<uint16_t>((static_cast<uint8_t>(bild[ringPruef_]) << 8)
                                                          | static_cast<uint8_t>(bild[ringPruef_ + 1]));
                    if (diagRingWert_ != hookcopy::kPruefwert) {
                        // Uebertragung dieses Bilds nicht gesichert - verwerfen
                        ++diagRingBad_;
                        ringLuecke_ = true;
                        continue;
                    }
                    if (ringSp_) {
                        // Das Programm sichert Register unter dem Stapelzeiger
                        // des Spiels und kopiert erst danach. Reicht dieser
                        // Bereich in eine ueberwachte Adresse, stammt deren Wert
                        // in diesem Bild von uns, nicht vom Spiel - verwerfen.
                        uint32_t sp = (static_cast<uint8_t>(bild[ringSp_]) << 24)
                                    | (static_cast<uint8_t>(bild[ringSp_ + 1]) << 16)
                                    | (static_cast<uint8_t>(bild[ringSp_ + 2]) << 8)
                                    | static_cast<uint8_t>(bild[ringSp_ + 3]);
                        sp &= 0xFFFFFF;
                        if (sp < diagRingSpMin_) diagRingSpMin_ = sp;
                        // Lagbilder generisch: Wartet das Spiel beim Bildwechsel in
                        // Ruhe, steht der Stapelzeiger immer gleich (haeufigster
                        // Wert). Jeder andere Wert = Spiel steckte noch in seiner
                        // Logik = ausgelassenes Bild.
                        ++spHisto_[sp];
                        if (spRuhe_ && sp != spRuhe_) ++diagBusy_;
                        if (sp >= 0xFF0000u) {
                            uint32_t unten = (sp - static_cast<uint32_t>(ringStapel_)) & 0xFFFF;
                            uint32_t oben  = sp & 0xFFFF;
                            auto it = ringLage_.lower_bound(unten);
                            if (it != ringLage_.end() && it->first < oben) {
                                ++diagRingStapel_;
                                diagZeile(QString("STACK sp=%1 covers %2-%3, first watched %4 - frame dropped")
                                              .arg(sp, 6, 16, QChar('0')).toUpper()
                                              .arg(0xFF0000 | unten, 6, 16, QChar('0'))
                                              .arg(0xFF0000 | (oben - 1), 6, 16, QChar('0'))
                                              .arg(0xFF0000 | it->first, 6, 16, QChar('0')));
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
                        return static_cast<uint8_t>(bild[it->second]);
                    }, k + 1 == bilder.size());
                }
            } else {
                // Nur Test (test.ini "beobachte" = Adresse, z. B. FFFB06): Der
                // Mapper markiert jeden Schreibzugriff des Spiels (alter Weg,
                // auf dem CORE verlaesslich). Gesetzte Markierung = das Spiel
                // hat seit der letzten Abfrage dorthin geschrieben.
                static const QString beob = testSchalter("beobachte");
                if (!beob.isEmpty()) {
                    const uint32_t wa = beob.toUInt(nullptr, 16);
                    if (snoop.written(wa)) {
                        QByteArray wv = snoop.read(wa, 1);
                        diagZeile(QString("WATCH %1 written by game, value now %2")
                                      .arg(wa, 6, 16, QChar('0')).toUpper()
                                      .arg(hexBytes(wv)));
                        snoop.resetMarks();
                    }
                }
                // Alle belegten Bloecke am Stueck lesen.
                std::map<int, QByteArray> blocks = snoop.readAll();
                pollZeit();
                verarbeite([&](uint32_t a) -> int {
                    auto off = mdsnoop::wramOffset(a);
                    if (!off) return -1;
                    auto it = blocks.find(static_cast<int>(*off / mdsnoop::kBlockSize));
                    int idx = static_cast<int>(*off % mdsnoop::kBlockSize);
                    if (it == blocks.end() || idx >= it->second.size()) return -1;
                    return static_cast<uint8_t>(it->second[idx]);
                }, true);
            }

            lk.unlock();
            QThread::msleep(ring_ ? 8 : 16);
        } catch (const std::exception& e) {
            emit connectionLost(QString::fromUtf8(e.what()));
            emit log(T("Verbindung verloren: ") + QString::fromUtf8(e.what()));
            diagFehler_ = true;
            QThread::msleep(1000);
        }
    }
    // Kopierprogramm abschalten - aber nur, solange unser Core noch laeuft.
    // Nach einem Spielwechsel steht dort ein anderer Mapper.
    if (kopierWeg_ != 0) {
        try {
            if (snoop.build() == coreBuild_) snoop.cpyEnable(false);
        } catch (...) {}
        kopierWeg_ = 0;
        ring_ = false;
    }
    ed.close();
    emit log(T("Monitor gestoppt."));
    if (diagFehler_) diagSichern();
    emit finished();
}
