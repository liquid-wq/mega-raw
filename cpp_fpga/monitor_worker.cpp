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
#include <fstream>
#include <map>
#include <vector>
#include <QThread>
#include <algorithm>

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

void MonitorWorker::start() {
    running_ = true;

    auto [edPtr, foundPort] = find_everdrive(port_);
    if (!edPtr) {
        emit log(QString(T("Kein EverDrive gefunden (Port %1 und Scan aller Ports erfolglos).")).arg(port_));
        emit finished();
        return;
    }
    EdSerial& ed = *edPtr;
    port_ = foundPort;

    mdsnoop::MdSnoop snoop(ed);

    // Core-Erkennung: Build-Byte im Mapper-Fenster.
    QString coreInfo = "Core: ?";
    bool coreOk = false;
    try {
        uint8_t b = snoop.build();
        if (b > 0 && b < 0xFF) {
            coreInfo = QString(T("Core-Build %1")).arg(b);
            coreOk = true;
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
    if (hardcore_ && coreOk) {
        try {
            uint8_t cs = snoop.ctrlStatus();
            bool ssOn = cs & 0x01;   // In-Game-Menu erlaubt
            bool ggOn = cs & 0x02;   // Cheats an
            if (ssOn || ggOn) {
                QString liste;
                if (ssOn) liste += T("Savestates (\"In Game Menu\")");
                if (ggOn) { if (!liste.isEmpty()) liste += " + "; liste += "Cheats"; }
                hardcore_ = false;
                emit log(QString(T("ACHTUNG: %1 sind am EverDrive aktiviert - nur Softcore. Fuer Hardcore bitte im EverDrive-System-Menue (nicht im In-Game-Menue) deaktivieren, dann Monitor neu starten.")).arg(liste));
                emit hardcoreDowngraded(liste);
            }
        } catch (const std::exception& e) {
            // B009 nicht lesbar: kein Hardcore erzwingen, aber warnen.
            hardcore_ = false;
            emit log(QString(T("Hardcore-Status nicht lesbar (%1) - Sitzung laeuft als Softcore."))
                     .arg(QString::fromUtf8(e.what())));
            emit hardcoreDowngraded(QString());
        }
    }

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
        emit log(T("  ueber USB   -  edlink run --file SPIEL.md --fpga mega-core.x25"));
        emit log(T("  ueber SD    -  mega-core.x25 in denselben Ordner wie das ROM legen"));
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

    while (running_) {
        try {
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
                try {
                    auto m = snoop.program(addrs);
                    snoop.resetMarks();
                    mapFailed_ = false;
                    emit log(QString(T("Blockzuordnung geschrieben: %1 Adressen in %2 Bloecken."))
                             .arg(addrs.size()).arg(m.size()));
                } catch (const mdsnoop::SnoopError& e) {
                    mapFailed_ = true;
                    emit log(QString(T("Set passt nicht in den Spiegel: %1"))
                             .arg(QString::fromUtf8(e.what())));
                }
                mapDirty_ = false;
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

            // Alle belegten Bloecke am Stueck lesen.
            std::map<int, QByteArray> blocks = snoop.readAll();

            RamMap ram;
            for (auto& kv : game_.addr_map) {
                auto off = mdsnoop::wramOffset(kv.first);
                if (!off) { ram[kv.first] = 0; continue; }
                auto it = blocks.find(static_cast<int>(*off / mdsnoop::kBlockSize));
                int idx = static_cast<int>(*off % mdsnoop::kBlockSize);
                ram[kv.first] = (it != blocks.end() && idx < it->second.size())
                                ? static_cast<uint8_t>(it->second[idx]) : 0;
            }

            // Anzeige: genau die ueberwachten Bytes, nach Adresse sortiert -
            // nicht ein beliebiger Ausschnitt eines Blocks.
            {
                QByteArray dump;
                dump.reserve(static_cast<int>(ram.size()));
                std::map<uint32_t,int> sortedRam(ram.begin(), ram.end());
                for (auto& kv : sortedRam) dump.append(static_cast<char>(kv.second));
                emit bramDump(dump);
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
                        rec << QDateTime::currentMSecsSinceEpoch()/1000.0;
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
                            std::ofstream tl("trigger_log.txt", std::ios::app);
                            if (tl) {
                                tl << "\n[" << QTime::currentTime().toString("HH:mm:ss").toStdString()
                                   << "] * " << ac.title << " (ID " << ac.id << ")\n";
                                tl << "  MemAddr: " << ac.mem << "\n";
                            }
                        }
                        // Anti-Cheat: bei nachgewiesen fremder ROM NICHT buchen.
                        // coreVerified_==0 blockiert; -1 (nicht pruefbar) failsafe bucht.
                        // Im Hardcore-Modus zaehlt die Bedingung als erfuellt,
                        // weil der Mapper Savestates und Cheats gar nicht
                        // enthaelt. Ohne Mapper wird ohnehin abgebrochen.
                        if (coreVerified_ == 0) {
                            emit log(QString(T("! %1 NICHT gebucht - ROM-Verifikation fehlgeschlagen"))
                                     .arg(QString::fromStdString(ac.title)));
                            emit unlocked(QString::fromStdString(ac.title), ac.points, ac.id);
                            continue;
                        }
                        bool ok = client_->ra_award(ac.id, user_.toStdString(), token_.toStdString(),
                                                    hardcore_ ? 1 : 0);
                        emit unlocked(QString::fromStdString(ac.title), ac.points, ac.id);
                        emit log(QString(T("Achievement freigeschaltet: %1 (%2)"))
                            .arg(QString::fromStdString(ac.title))
                            .arg(ok ? (hardcore_ ? T("gebucht, Hardcore") : T("gebucht, Softcore"))
                                 : T("Buchung fehlgeschlagen, Retry noetig")));
                    }
                }
            }
            prevRam_ = ram;
            havePrevRam_ = true;

            lk.unlock();
            QThread::msleep(16);
        } catch (const std::exception& e) {
            emit connectionLost(QString::fromUtf8(e.what()));
            emit log(T("Verbindung verloren: ") + QString::fromUtf8(e.what()));
            QThread::msleep(1000);
        }
    }
    ed.close();
    emit log(T("Monitor gestoppt."));
    emit finished();
}
