#include "batch_patch_worker.h"
#include "archive_extract.h"
#include "rom_patch.h"
#include "ra_engine.h"
#include "md5.h"
#include "throttle.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <fstream>
#include <vector>
#include <set>
#include <tuple>

BatchPatchWorker::BatchPatchWorker(QString folder, QString user, QString token,
                                   bool avSafe, std::shared_ptr<RaClient> client)
    : folder_(std::move(folder)), user_(std::move(user)), token_(std::move(token)),
      avSafe_(avSafe), client_(std::move(client)) {}

void BatchPatchWorker::stop() { running_ = false; }

namespace {
std::vector<uint8_t> readFileB(const QString& path) {
    std::ifstream f(path.toStdString(), std::ios::binary);
    f.seekg(0, std::ios::end);
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size > 0 ? static_cast<size_t>(size) : 0);
    if (size > 0) f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}
void writeFileB(const QString& path, const std::vector<uint8_t>& data) {
    std::ofstream o(path.toStdString(), std::ios::binary);
    o.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}
QString appPath(const QString& name) {
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}
}

void BatchPatchWorker::start() {
    const QStringList romExts = {"md","bin","gen","smd"};
    const QStringList arcExts = {"zip","rar"};

    QDir dir(folder_);
    QStringList names = dir.entryList(QDir::Files, QDir::Name);
    QStringList files;
    for (const QString& n : names) {
        QString ext = QFileInfo(n).suffix().toLower();
        if (romExts.contains(ext) || arcExts.contains(ext)) files << dir.filePath(n);
    }
    if (files.isEmpty()) {
        emit finishedSummary("KEINE ROMs im Ordner gefunden.");
        return;
    }

    int ok = 0, skip = 0, nArchive = 0;
    // reasons-Toepfe
    QStringList rUnbekannt, rKeinSet, rUnsupported, rKeinPlatz, rFehler;
    QStringList vEchtLeer;                                   // verify: set_echt_leer
    std::vector<std::pair<QString,long long>> vLuecke;       // verify: set_engine_luecke
    std::vector<std::tuple<long long,QString,long long,long long>> patchedIds; // gid,name,rc,ok_n
    std::vector<std::tuple<QString,QString,std::vector<std::string>>> bootRiskList; // stufe,base,gruende
    bool raThrottled = false, stopped = false;

    std::ofstream logf(appPath("batch_log.txt").toStdString());
    logf << "Batch ueber " << folder_.toStdString() << " (" << files.size() << " Dateien)\n\n";

    for (int i = 0; i < files.size(); ++i) {
        if (!running_) { stopped = true; break; }
        const QString f = files[i];
        const QString base = QFileInfo(f).fileName();
        emit progress(QString("[%1/%2] %3 ...").arg(i+1).arg(files.size()).arg(base));
        emit progressPercent(i + 1, files.size());

        try {
            const QString ext = QFileInfo(f).suffix().toLower();
            QString romFile = f;
            if (arcExts.contains(ext)) {   //
                auto ex = extract_archive_rom(f);
                if (!ex) {
                    nArchive++;
                    logf << "ARCHIV-FEHLER  " << base.toStdString()
                         << "  (keine ROM enthalten oder RAR ohne WinRAR)\n";
                    continue;
                }
                romFile = *ex;
            }

            const QString ipsPath = romFile.left(romFile.length() - QFileInfo(romFile).suffix().length() - 1) + ".ips";
            if (QFile::exists(ipsPath)) {
                skip++;
                // Game-ID aus Cache fuer Abgleich erfassen (ohne Server-Abruf)
                try {
                    std::string hh = md5_file(romFile.toStdString());
                    if (const Json* g = client_->cachePtr()->gameid_get(hh)) {
                        if (g->type() == Json::Type::Int)
                            patchedIds.emplace_back(g->as_int(),
                                QFileInfo(base).completeBaseName(), -1, -1);
                    }
                } catch (...) {}
                continue;
            }

            std::string h = md5_file(romFile.toStdString());
            // Throttle-Logik: 1 Retry mit Wartezeit, bei anhaltender Drossel Batch stoppen
            auto dec = resolve_gameid_throttled(
                *client_->cachePtr(), *client_, h, raThrottled,
                []{ QThread::msleep(400); },                    // RA_REQUEST_PAUSE 0.4s
                [](int s){ QThread::sleep(std::min(s>0?s:15, 60)); return false; });
            if (!dec.proceed) {
                logf << "\n!! RATE-LIMIT bei " << base.toStdString()
                     << " -- Batch gestoppt. Spaeter erneut (Cache + gepatchte werden uebersprungen).\n";
                stopped = true;
                break;
            }
            if (!dec.gid) {
                rUnbekannt << base;
                logf << "HASH-UNBEKANNT  " << base.toStdString() << "  md5=" << h << "\n";
                continue;
            }
            long long gid = *dec.gid;
            std::optional<Json> patch;
            try {
                patch = client_->ra_patch(gid, user_.toStdString(), token_.toStdString());
            } catch (const RateLimited&) {
                logf << "\n!! RATE-LIMIT (patch) bei " << base.toStdString() << " -- Batch gestoppt.\n";
                stopped = true;
                break;
            }
            Game g = build_game_from_patch(gid, h, patch);
            if (g.valid && g.no_set) {
                rUnsupported << base;
                logf << "KEIN-DUMP-MATCH  " << base.toStdString()
                     << "  (Beta/Proto/Alt-Version oder andere Region)\n";
                continue;
            }
            if (!g.valid || g.addr_list.empty()) {
                rKeinSet << base;
                long long raw = g.valid ? g.raw_core : 0;
                if (raw == 0) {
                    vEchtLeer << base;
                    logf << "KEIN-SET-VERIFIZIERT  " << base.toStdString()
                         << "  (RA fuehrt 0 Achievements -- berechtigt)\n";
                } else {
                    vLuecke.emplace_back(base, raw);
                    logf << "KEIN-SET-ABER-" << raw << "-VORHANDEN  " << base.toStdString()
                         << "  (Engine konnte keine parsen -- UNSER THEMA, nicht leer!)\n";
                }
                continue;
            }
            auto rom = readFileB(romFile);
            size_t sum = 0;
            for (const auto& al : g.addr_list) sum += size_t(al.second);
            size_t need = 60 + 10*sum + 100;
            if (find_stub_addr(rom, need).stub_addr < 0) {
                rKeinPlatz << base;
                logf << "KEIN-STUB-PLATZ  " << base.toStdString()
                     << "  (" << stub_failure_reason(rom, need) << ")\n";
                continue;
            }
            if (avSafe_) QThread::msleep(400);
            PatchResult r = patch_rom(rom, romFile.toStdString(), g, false);
            if (r.ok) {
                ok++;
                writeFileB(QString::fromStdString(r.ips_path), r.ips_bytes);
                long long nu = g.n_unsupported, rc = g.raw_core;
                if (rc > 0 && rc - nu == 0)
                    logf << "WARNUNG-0-LESBAR  " << base.toStdString()
                         << "  (gepatcht, aber KEIN Achievement von der Engine lesbar"
                            " -- Pointer-/Sonderkonstrukte)\n";
                if (r.has_risk)
                    bootRiskList.emplace_back(QString::fromStdString(r.risk.level), base, r.risk.reasons);
                patchedIds.emplace_back(g.gameid, QString::fromStdString(g.name), rc, rc-nu);
                if (nu)
                    logf << "OK  " << base.toStdString() << "  " << r.msg
                         << "  (" << (rc-nu) << "/" << rc << " Achievements parsebar, " << nu << " nicht)\n";
                else
                    logf << "OK  " << base.toStdString() << "  " << r.msg
                         << "  (" << rc << "/" << rc << " Achievements parsebar)\n";
            } else {
                rKeinPlatz << base;
                logf << "PATCH-FEHLER  " << base.toStdString() << "  " << r.msg << "\n";
            }
        } catch (const std::exception& e) {
            rFehler << base;
            logf << "EXCEPTION  " << base.toStdString() << "  " << e.what() << "\n";
        }
    }

    // ---- Verifikations-Report ----
    {
        std::ofstream vf(appPath("verify_report.txt").toStdString());
        vf << "=== VERIFIKATIONS-REPORT  " << folder_.toStdString() << " ===\n\n";
        vf << "Gepatcht: " << ok << "\n\n";
        vf << "[KEIN-SET -- verifiziert berechtigt: RA fuehrt 0 Achievements] " << vEchtLeer.size() << "\n";
        for (const QString& b : vEchtLeer) vf << "  " << b.toStdString() << "\n";
        vf << "\n[KEIN-SET -- ABER Achievements vorhanden, Engine-Luecke!] " << vLuecke.size() << "\n";
        for (const auto& [b,n] : vLuecke) vf << "  " << b.toStdString() << "  (" << n << " Achievements)\n";
        vf << "\n[KEIN PASSENDES SET FUER DIESEN DUMP -- Beta/Proto/Alt-Version "
              "oder andere Region; RA kennt nur den finalen Verkaufs-Dump] " << rUnsupported.size() << "\n";
        for (const QString& b : rUnsupported) vf << "  " << b.toStdString() << "\n";
        vf << "\n[HASH UNBEKANNT -- Bad Dump/Hack, verifiziert nicht in RA-DB] " << rUnbekannt.size() << "\n";
        for (const QString& b : rUnbekannt) vf << "  " << b.toStdString() << "\n";
        vf << "\n[ECHTE FEHLER] " << (rKeinPlatz.size()+rFehler.size()+nArchive) << "\n";
        for (const QString& b : rKeinPlatz) vf << "  PATCH-FEHLER  " << b.toStdString() << "\n";
        for (const QString& b : rFehler)    vf << "  EXCEPTION  "   << b.toStdString() << "\n";
        if (nArchive) vf << "  ARCHIVE NICHT ENTPACKBAR: " << nArchive << "\n";
    }
    logf.close();
    client_->cachePtr()->save(true);

    // ---- patched_games.txt ----
    {
        std::ofstream pf(appPath("patched_games.txt").toStdString());
        pf << "# Erfolgreich gepatchte Spiele dieses Laufs: " << patchedIds.size() << "\n";
        pf << "# GameID\tParsebar/Gesamt\tTitel\n";
        std::sort(patchedIds.begin(), patchedIds.end());
        std::set<long long> seen;
        for (const auto& [gid,name,rc,okn] : patchedIds) {
            if (seen.count(gid)) continue;
            seen.insert(gid);
            std::string quote = (rc < 0) ? "vorhanden" : (std::to_string(okn)+"/"+std::to_string(rc));
            pf << gid << "\t" << quote << "\t" << name.toStdString() << "\n";
        }
        pf << "\n# Einzigartige Game-IDs gesamt: " << seen.size() << "\n";
    }

    // ---- boot_risk.txt ----
    {
        int nH=0,nP=0,nO=0;
        for (const auto& [st,b,gr] : bootRiskList) { if(st=="hoch")nH++; else if(st=="pruefen")nP++; else nO++; }
        std::ofstream bf(appPath("boot_risk.txt").toStdString());
        bf << "=== BOOT-RISIKO-ANALYSE (statisch, ersetzt keinen echten Konsolentest) ===\n\n";
        bf << "Unauffaellig (hohe Boot-Wahrscheinlichkeit): " << nO << "\n";
        bf << "An Konsole testen (Risiko-Indikator):        " << nP << "\n";
        bf << "Hohes Risiko (bekannte Crash-Klasse):        " << nH << "\n\n";
        if (nH) {
            bf << "--- HOHES RISIKO (zuerst testen) ---\n";
            for (const auto& [st,b,gr] : bootRiskList) if (st=="hoch") {
                bf << "  " << b.toStdString() << "\n";
                for (const auto& g2 : gr) bf << "      - " << g2 << "\n";
            }
        }
        if (nP) {
            bf << "\n--- AN KONSOLE TESTEN ---\n";
            for (const auto& [st,b,gr] : bootRiskList) if (st=="pruefen") {
                bf << "  " << b.toStdString() << "\n";
                for (const auto& g2 : gr) bf << "      - " << g2 << "\n";
            }
        }
    }

    QString head = stopped ? (raThrottled ? "RA-DROSSEL -- GESTOPPT" : "ABGEBROCHEN") : "FERTIG";
    QString s = QString("%1\nGepatcht: %2  Uebersprungen (schon .ips): %3\n"
                        "Kein Set: %4  Kein Dump-Match: %5  Hash unbekannt: %6\n"
                        "Fehler: %7  Archive nicht entpackbar: %8\n"
                        "Reports: batch_log.txt, verify_report.txt, patched_games.txt, boot_risk.txt")
                .arg(head).arg(ok).arg(skip)
                .arg(rKeinSet.size()).arg(rUnsupported.size()).arg(rUnbekannt.size())
                .arg(rKeinPlatz.size()+rFehler.size()).arg(nArchive);
    emit finishedSummary(s);
}
