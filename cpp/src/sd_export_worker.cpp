#include "sd_export_worker.h"
#include "archive_extract.h"
#include <QThread>
#include "rom_patch.h"
#include "md5.h"
#include "throttle.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <fstream>
#include <vector>

SdExportWorker::SdExportWorker(QString src, QString dst, bool doIps, bool doBucket,
                                QString user, QString token, std::shared_ptr<RaClient> client,
                                bool delArc)
    : src_(std::move(src)), dst_(std::move(dst)), user_(std::move(user)),
      token_(std::move(token)), doIps_(doIps), doBucket_(doBucket),
      client_(std::move(client)), delArc_(delArc) {}

void SdExportWorker::stop() { running_ = false; }

namespace {
std::vector<uint8_t> readFile(const QString& path) {
    std::ifstream f(path.toStdString(), std::ios::binary);
    f.seekg(0, std::ios::end);
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size > 0 ? static_cast<size_t>(size) : 0);
    if (size > 0) f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}
void writeFile(const QString& path, const std::vector<uint8_t>& data) {
    std::ofstream o(path.toStdString(), std::ios::binary);
    o.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}
}

void SdExportWorker::start() {
    const QStringList romExts = {"md","bin","gen","smd"};
    const QStringList archiveExts = {"zip","rar"};

    QStringList files;
    QDirIterator it(src_, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString p = it.next();
        QString ext = QFileInfo(p).suffix().toLower();
        if (romExts.contains(ext) || archiveExts.contains(ext)) files << p;
    }
    std::sort(files.begin(), files.end(), [](const QString& a, const QString& b){
        return QFileInfo(a).fileName().toLower() < QFileInfo(b).fileName().toLower();
    });

    if (files.isEmpty()) {
        emit finishedSummary("KEINE ROMs/Archive im Quellordner gefunden (auch nicht in Unterordnern).");
        return;
    }
    if (QFileInfo(dst_).absoluteFilePath() == QFileInfo(src_).absoluteFilePath()) {
        emit finishedSummary("FEHLER: Quelle und Ziel identisch - bitte anderes Ziel waehlen.");
        return;
    }

    QString staging = QDir(dst_).filePath("_mega_raw_tmp");
    QDir().mkpath(staging);

    std::vector<PlacedItem> placed;
    int copied=0, ips=0, fail=0, skip=0, deferred=0;
    int nUnknown=0, nNoset=0, nUnsupported=0, nLuecke=0, nArchiveSkip=0;
    bool raThrottled = false;
    std::vector<std::pair<QString,QString>> problems; // (Name, Grund) - nur Spiele MIT Set, nicht gepatcht

    for (int i = 0; i < files.size(); ++i) {
        if (!running_) break;
        QString f = files[i];
        QString base = QFileInfo(f).fileName();
        emit progress(QString("[%1/%2] %3").arg(i+1).arg(files.size()).arg(base));
        emit progressPercent(i + 1, files.size());

        QString ext = QFileInfo(f).suffix().toLower();
        QString dstRom;
        if (archiveExts.contains(ext)) {
            // 
            // Quelle), ROM ins Staging verschieben, Temp restlos loeschen.
            QDir dstDir(dst_);
            QString tdir = dstDir.filePath(QString("_mraw_x_%1").arg(i));
            QDir().mkpath(tdir);
            auto ex = extract_archive_rom(f, tdir);
            if (!ex) { QDir(tdir).removeRecursively(); nArchiveSkip++; fail++; continue; }
            dstRom = QDir(staging).filePath(QFileInfo(*ex).fileName());
            if (QFile::exists(dstRom)) { QDir(tdir).removeRecursively(); skip++; continue; }
            QFile::rename(*ex, dstRom);
            QDir(tdir).removeRecursively();
            if (delArc_) QFile::remove(f);   // Quell-Archiv nach Entpacken loeschen
            copied++;
        } else {
            dstRom = QDir(staging).filePath(base);
            if (QFileInfo(dstRom).absoluteFilePath() == QFileInfo(f).absoluteFilePath()) { skip++; continue; }
            if (QFile::exists(dstRom)) { skip++; continue; }
            if (!QFile::copy(f, dstRom)) { fail++; continue; }
            copied++;
        }

        QString ipsPath;
        if (doIps_) {
            std::string h = md5_file(dstRom.toStdString());
            auto dec = resolve_gameid_throttled(
                *client_->cachePtr(), *client_, h, raThrottled,
                []{ QThread::msleep(400); },                 // RA_REQUEST_PAUSE 0.4s, Pflicht
                [](int sec){ QThread::sleep(std::min(sec>0?sec:30, 60)); return false; });
            if (!dec.proceed) { deferred += dec.deferred_delta; }
            else if (!dec.gid) { nUnknown++; }
            else {
                long long gid = *dec.gid;
                std::optional<Json> patch;
                const bool inCache =
                    client_->cachePtr()->patch_get(std::to_string(gid)) != nullptr;
                if (raThrottled && !inCache) {
                    deferred++;                      // Drossel: Patch nicht abrufen
                } else {
                    if (!inCache) QThread::msleep(400);   // 2. Anfrage pacen
                    try {
                        patch = client_->ra_patch(gid, user_.toStdString(), token_.toStdString());
                    } catch (const RateLimited&) { raThrottled = true; deferred++; }
                }
                if (raThrottled && !patch) { /* schon gezaehlt */ }
                else {
                    Game g = build_game_from_patch(gid, h, patch);
                    if (g.valid && g.no_set) nUnsupported++;
                    else if (!g.valid || g.addr_list.empty()) {
                        if (!g.valid || g.raw_core == 0) nNoset++;
                        else {
                            nLuecke++;
                            problems.emplace_back(base, QString("Set vorhanden (%1 Achievements), aber Engine parste keine Adresse").arg(g.raw_core));
                        }
                    } else {
                        auto rom = readFile(dstRom);
                        PatchResult r = patch_rom(rom, dstRom.toStdString(), g, false);
                        if (r.ok) {
                            writeFile(QString::fromStdString(r.ips_path), r.ips_bytes);
                            ipsPath = QString::fromStdString(r.ips_path);
                            ips++;
                        } else {
                            fail++;
                            problems.emplace_back(base, r.msg.empty() ? QString("Patch fehlgeschlagen (meist kein Platz im ROM)") : QString::fromStdString(r.msg));
                        }
                    }
                }
            }
        }
        placed.emplace_back(dstRom.toStdString(), !ipsPath.isEmpty());
    }

    bool cancelled = !running_;
    QString head = cancelled ? "ABGEBROCHEN" : "FERTIG";
    int moved = 0;

    if (doBucket_) {
        emit progress("Sortiere in Ordner und verschiebe...");
        auto groups = letter_groups(placed);
        int totalToMove = 0;
        for (auto& g : groups) totalToMove += static_cast<int>(g.second.size());
        int movedSoFar = 0;
        for (auto& g : groups) {
            QString folder = QDir(dst_).filePath(QString::fromStdString(g.first));
            QDir().mkpath(folder);
            for (auto& item : g.second) {
                QString rom = QString::fromStdString(item.first);
                QStringList toMove; toMove << rom;
                if (item.second) {
                    QString base2 = QFileInfo(rom).completeBaseName();
                    toMove << QDir(QFileInfo(rom).path()).filePath(base2 + ".ips");
                }
                for (const auto& p : toMove) {
                    if (!QFile::exists(p)) continue;
                    QString tgt = QDir(folder).filePath(QFileInfo(p).fileName());
                    if (QFileInfo(tgt).absoluteFilePath() != QFileInfo(p).absoluteFilePath()) {
                        QFile::rename(p, tgt);
                    }
                }
                moved++;
                movedSoFar++;
                emit progressPercent(movedSoFar, totalToMove);
            }
        }
        emit finishedSummary(QString("%1: %2 kopiert, %3 mit IPS, %4 Spiele in %5 Ordner, "
            "%6 uebersprungen, %7 Fehler, %8 Archive nicht entpackbar%9")
            .arg(head).arg(copied).arg(ips).arg(moved).arg(groups.size())
            .arg(skip).arg(fail).arg(nArchiveSkip)
            .arg(deferred ? QString(" | %1 wegen RA-Drossel offen -> erneut ausfuehren").arg(deferred) : ""));
    } else {
        for (auto& item : placed) {
            QString rom = QString::fromStdString(item.first);
            QStringList toMove; toMove << rom;
            if (item.second) {
                QString base2 = QFileInfo(rom).completeBaseName();
                toMove << QDir(QFileInfo(rom).path()).filePath(base2 + ".ips");
            }
            for (const auto& p : toMove) {
                if (!QFile::exists(p)) continue;
                QString tgt = QDir(dst_).filePath(QFileInfo(p).fileName());
                if (QFileInfo(tgt).absoluteFilePath() != QFileInfo(p).absoluteFilePath())
                    QFile::rename(p, tgt);
            }
            moved++;
        }
        emit finishedSummary(QString("%1: %2 kopiert, %3 mit IPS, %4 uebersprungen, %5 Fehler, "
            "%6 Archive nicht entpackbar%7")
            .arg(head).arg(copied).arg(ips).arg(skip).arg(fail).arg(nArchiveSkip)
            .arg(deferred ? QString(" | %1 offen").arg(deferred) : ""));
    }
    QDir().rmdir(staging);

    if (!problems.empty()) {
        QString lp = QDir(dst_).filePath("mega_raw_nicht_gepatcht.txt");
        std::ofstream lf(lp.toStdString(), std::ios::out | std::ios::trunc);
        if (lf) {
            lf << "Spiele MIT RetroAchievements-Set, die NICHT gepatcht wurden:\n";
            lf << "(alles andere ohne Set ist normal und hier absichtlich nicht gelistet)\n\n";
            for (auto& pr : problems)
                lf << pr.first.toStdString() << "  -  " << pr.second.toStdString() << "\n";
        }
    }

    // Detail-Aufschlüsselung
    if (doIps_ && !cancelled) {
        QString detail = QString("%1 Spiele mit Achievements gepatcht").arg(ips);
        if (skip) detail += QString(", %1 schon vorhanden (uebersprungen)").arg(skip);

        if (fail > 0) {
            detail += QString("\n\n⚠ %1 Spiele mit RA-Set NICHT gepatcht (meist kein Platz im ROM fuer den Stub) — "
                "siehe %2").arg(fail).arg(QDir(dst_).filePath("mega_raw_nicht_gepatcht.txt"));
        }
        if (nLuecke > 0) {
            detail += QString("\n\n⚠ %1 Spiele mit RA-Set, deren Adressen die Engine (noch) nicht lesen kann.").arg(nLuecke);
        }

        detail += QString("\n\nOhne Set (kein Fehler, nichts zu tun):\n"
            "  %1 in RA gelistet, aber (noch) ohne Achievements\n"
            "  %2 kein Set fuer genau diese ROM-Version\n"
            "       (Beta / Proto / andere Region)\n"
            "  %3 ROM von RA nicht erkannt (Bad Dump / Hack / Homebrew)")
            .arg(nNoset).arg(nUnsupported).arg(nUnknown);

        emit finishedDetail(detail);
    }
}
