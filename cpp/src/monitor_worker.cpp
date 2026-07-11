#include "monitor_worker.h"
#include <thread>
#include "stub.h"
#include <QTime>
#include "ra_engine.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <fstream>
#include <map>
#include <QThread>
#include <algorithm>

MonitorWorker::MonitorWorker(QString port, Game game, QString user, QString token,
                              std::shared_ptr<RaClient> client, long long stubAddr, bool tvFlash)
    : port_(std::move(port)), game_(std::move(game)), user_(std::move(user)),
      token_(std::move(token)), client_(std::move(client)), stubAddr_(stubAddr), tvFlash_(tvFlash) {}

void MonitorWorker::stop() {
    running_ = false;
}

void MonitorWorker::start() {
    running_ = true;
    // Autoerkennung
    // dann alle vorhandenen Ports scannen.
    auto [edPtr, foundPort] = find_everdrive(port_);
    if (!edPtr) {
        emit log("Kein EverDrive gefunden (Port " + port_ + " und Scan aller Ports erfolglos).");
        emit finished();
        return;
    }
    EdSerial& ed = *edPtr;
    port_ = foundPort;

    // Stub-Generation aus BRAM[196..197] pruefen
    QString stubInfo = "Stub: ?";
    try {
        QByteArray g = ed.readBram(200);
        if (g.size() > 197) {
            uint8_t gen = static_cast<uint8_t>(g[197]);
            if (gen == STUB_GEN) stubInfo = QString("Stub-Gen %1").arg(gen, 2, 16, QChar('0')).toUpper();
            else stubInfo = QString("Stub: ALT (%1) - IPS NEU ERSTELLEN!").arg(gen, 2, 16, QChar('0')).toUpper();
        }
    } catch (...) {}
    emit log("Verbunden mit " + port_ + " (auto-erkannt) | " + stubInfo);

    // Anti-Cheat Stufe 1: Stub-Signatur der laufenden ROM per USB pruefen.
    if (stubAddr_ > 0) {
        try {
            QByteArray d = ed.memrd(static_cast<uint32_t>(stubAddr_), 8);
            if (d.size() == 8 &&
                static_cast<uint8_t>(d[0]) == 0x2F && static_cast<uint8_t>(d[1]) == 0x3C &&
                static_cast<uint8_t>(d[6]) == 0x40 && static_cast<uint8_t>(d[7]) == 0xE7) {
                uint32_t part2 = (uint8_t(d[2])<<24)|(uint8_t(d[3])<<16)|(uint8_t(d[4])<<8)|uint8_t(d[5]);
                if (part2 == static_cast<uint32_t>(stubAddr_) + 14) {
                    stubVerified_ = 1;
                    emit log(QString("Anti-Cheat: Stub verifiziert @0x%1 (Konsole faehrt unsere ROM)")
                             .arg(stubAddr_, 6, 16, QChar('0')).toUpper());
                } else {
                    stubVerified_ = 0;
                    emit log(QString("Anti-Cheat: part2-Adresse falsch (0x%1) - fremder Patch?")
                             .arg(part2, 6, 16, QChar('0')).toUpper());
                }
            } else {
                stubVerified_ = 0;
                emit log("Anti-Cheat: Stub-Signatur nicht gefunden - fremde/ungepatchte ROM laeuft.");
            }
        } catch (...) {
            emit log("Anti-Cheat: Verifikation nicht moeglich (Lesefehler).");
        }
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
        // TV-Flash-Anfrage vom UI-Thread (TV-Test-Button) — eigener Thread
        if (tvFlashRequested_.exchange(false)) {
            std::thread([&ed, this](){
                try { ed.tvFlash(); emit log("TV-Test gesendet (golden aufpulsen)."); }
                catch (...) { emit log("TV-Flash fehlgeschlagen."); }
            }).detach();
        }
        try {
            bool frameSync = false;
            try { frameSync = ed.waitBramFrame(200); } catch (...) {}
            QByteArray data = ed.readBram(BRAM_LEN);

            QByteArray bramSlice = data.left(std::min(24, static_cast<int>(data.size())));
            emit bramDump(bramSlice);

            // Auto-Spielerkennung: Stub schreibt RA-Game-ID nach BRAM[203/205]
            if (data.size() > 205 && static_cast<uint8_t>(data[197]) == STUB_GEN) {
                int gid = static_cast<uint8_t>(data[203]) | (static_cast<uint8_t>(data[205]) << 8);
                if (gid && gid != autoGidTried_) {
                    autoGidTried_ = gid;
                    emit log(QString("Spiel erkannt (RA-ID %1) - lade automatisch...").arg(gid));
                    emit gameDetected(gid);
                }
            }

            {
                QMutexLocker lk(&gameMtx_);
            if (!game_.addr_map.empty()) {
                RamMap ram;
                for (auto& kv : game_.addr_map) {
                    int off = kv.second;
                    ram[kv.first] = (off < data.size()) ? static_cast<uint8_t>(data[off]) : 0;
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
                            if (tvFlash_) { std::thread([&ed](){ try { ed.tvFlash(); } catch (...) {} }).detach(); }
                            {
                                std::ofstream tl("trigger_log.txt", std::ios::app);
                                if (tl) {
                                    tl << "\n[" << QTime::currentTime().toString("HH:mm:ss").toStdString()
                                       << "] * " << ac.title << " (ID " << ac.id << ")\n";
                                    tl << "  MemAddr: " << ac.mem << "\n";
                                }
                            }
                            // Anti-Cheat: bei nachgewiesen fremder ROM NICHT buchen.
                            // stubVerified_==0 (false) blockiert; -1 (nicht pruefbar) failsafe bucht.
                            if (stubVerified_ == 0) {
                                emit log(QString("! %1 NICHT gebucht - Stub-Verifikation fehlgeschlagen")
                                         .arg(QString::fromStdString(ac.title)));
                                emit unlocked(QString::fromStdString(ac.title), ac.points, ac.id);
                                continue;
                            }
                            bool ok = client_->ra_award(ac.id, user_.toStdString(), token_.toStdString());
                            emit unlocked(QString::fromStdString(ac.title), ac.points, ac.id);
                            emit log(QString("Achievement freigeschaltet: %1 (%2)")
                                .arg(QString::fromStdString(ac.title))
                                .arg(ok ? "gebucht" : "Buchung fehlgeschlagen, Retry noetig"));
                        }
                    }
                }
                prevRam_ = ram;
                havePrevRam_ = true;
            }
            } // gameMtx_ unlock
            QThread::msleep(frameSync ? 1 : 20);
        } catch (const std::exception& e) {
            emit connectionLost(QString::fromUtf8(e.what()));
            emit log("Verbindung verloren: " + QString::fromUtf8(e.what()));
            QThread::msleep(1000);
        }
    }
    ed.close();
    emit log("Monitor gestoppt.");
    emit finished();
}
