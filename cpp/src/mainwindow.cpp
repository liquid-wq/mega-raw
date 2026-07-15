#include "mainwindow.h"
#include "version.h"
#include "i18n.h"
#include "compat.h"
#include "badge_loader.h"
#include "archive_extract.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif
#include "md5.h"
#include "ra_network.h"
#include "rom_patch.h"
#include "sd_export_worker.h"
#include "batch_patch_worker.h"
#include "ed_serial_qt.h"
#include <QTimer>
#include <QProgressBar>
#include <QPixmap>
#include <QDir>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFileDialog>
#include <QLabel>
#include <QFileInfo>
#include <QFile>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QMessageBox>
#include <QGroupBox>
#include <QListWidget>
#include <QIcon>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QMenuBar>
#include "cat_text.h"
#include <QTextEdit>
#include <QDesktopServices>
#include <QUrl>
#include <fstream>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    settingsPath_ = QDir(QCoreApplication::applicationDirPath()).filePath("settings.json");
    settings_ = Settings::load(settingsPath_);
    g_lang = settings_.language;

    setWindowTitle(QString("MEGA-RAW v%1  (C++ Build %2)").arg(MEGA_RAW_VERSION).arg(MEGA_RAW_CPP_BUILD));

    auto* central = new QWidget(this);
    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(8, 2, 8, 8);
    vbox->setSpacing(4);

    // Pixelart (Jason) ueber dem Copyright-Hinweis
    QString jasonPath = QDir(QCoreApplication::applicationDirPath()).filePath("assets/jason_pixel.png");
    QPixmap jasonPm(jasonPath);
    if (!jasonPm.isNull()) {
        auto* jasonLabel = new QLabel(central);
        jasonLabel->setPixmap(jasonPm.scaledToHeight(53, Qt::SmoothTransformation));
        jasonLabel->setAlignment(Qt::AlignCenter);
        vbox->addWidget(jasonLabel);
    }

    // Urheberrechtshinweis
    auto* copyrightLabel = new QLabel(QString::fromUtf8("MEGA-RAW \u2014 \u00a9 2026 Liqui"), central);
    copyrightLabel->setAlignment(Qt::AlignCenter);
    copyrightLabel->setStyleSheet("color: #888888; font-size: 8pt;");
    vbox->addWidget(copyrightLabel);

    // Schnellstart fuer neue Nutzer
    auto* quickstartLabel = new QLabel(
        T("Schnellstart:  1. Einloggen   2. Stub-Datenbank aktualisieren   3. Spiel einmalig patchen   4. Spiel starten   5. Monitor starten"),
        central);
    quickstartLabel->setAlignment(Qt::AlignCenter);
    quickstartLabel->setWordWrap(true);
    quickstartLabel->setStyleSheet("color: #40c060; font-family: Courier; font-size: 8pt;");
    vbox->addWidget(quickstartLabel);

    // --- Login ---
    auto* loginBox = new QGroupBox(T("RetroAchievements-Login"), central);
    auto* loginLayout = new QHBoxLayout();
    userEdit_ = new QLineEdit(loginBox);
    userEdit_->setPlaceholderText(T("Benutzername"));
    passEdit_ = new QLineEdit(loginBox);
    passEdit_->setPlaceholderText(T("Passwort"));
    passEdit_->setEchoMode(QLineEdit::Password);
    loginBtn_ = new QPushButton(T("Login"), loginBox);
    loginLayout->addWidget(userEdit_);
    loginLayout->addWidget(passEdit_);
    loginLayout->addWidget(loginBtn_);
    loginBox->setLayout(loginLayout);
    vbox->addWidget(loginBox);
    loginStatus_ = new QLabel(T("Nicht eingeloggt."), central);
    vbox->addWidget(loginStatus_);

    // --- Stub-Datenbank (Remote-Hooks) ---
    // Laedt/aktualisiert nach dem Release verifizierte Stub-Adressen fuer
    // einzelne Problemspiele von liquid-wq.github.io/data/hooks.json.
    // Rein additiv: aendert nichts, solange nicht geklickt. Fallback bleibt
    // immer die eingebaute hook_db().
    updateHooksBtn_ = new QPushButton(T("Stub-Datenbank aktualisieren"), central);
    vbox->addWidget(updateHooksBtn_);

    // --- ROM ---
    auto* hbox = new QHBoxLayout();
    romPathEdit_ = new QLineEdit(central);
    romPathEdit_->setReadOnly(true);
    romPathEdit_->setPlaceholderText(T("Keine ROM gewaehlt"));
    chooseBtn_ = new QPushButton(T("ROM waehlen zum Patchen..."), central);
    hbox->addWidget(romPathEdit_);
    hbox->addWidget(chooseBtn_);
    vbox->addLayout(hbox);

    statusLabel_ = new QLabel(T("Bereit."), central);
    vbox->addWidget(statusLabel_);

    lightStubCheck_ = new QCheckBox(T("Light-Stub (nur bei Grafikfehlern noetig)"), central);
    vbox->addWidget(lightStubCheck_);

    patchBtn_ = new QPushButton(T("Patchen (IPS erstellen)"), central);
    patchBtn_->setEnabled(false);
    vbox->addWidget(patchBtn_);

    batchPatchBtn_ = new QPushButton(T("Ordner patchen (in-place, .ips daneben)..."), central);
    sdExportBtn_ = new QPushButton(T("SD-Export (Ordner batch-patchen)..."), central);
    sdCancelBtn_ = new QPushButton(T("Export abbrechen"), central);
    sdCancelBtn_->setEnabled(false);
    sdCancelBtn_->setVisible(false);
    vbox->addWidget(batchPatchBtn_);
    vbox->addWidget(sdExportBtn_);

    // --- Monitor / Hardware ---
    auto* monBox = new QGroupBox(T("Live-Monitor (EverDrive)"), central);
    auto* monLayout = new QHBoxLayout();
    monitorBtn_ = new QPushButton(T("Verbinden && Monitor starten"), monBox);
    monitorBtn_->setEnabled(false);
    auto* tvBtn = new QPushButton(T("TV-Test"), monBox);
    edStatusLabel_ = new QLabel(T("EverDrive wird beim Monitor-Start automatisch gesucht"), monBox);
    monLayout->addWidget(edStatusLabel_);
    monitorHint_ = new QLabel(T("Monitor: zuerst einloggen."), monBox);
    monLayout->addWidget(monitorHint_);
    monLayout->addWidget(monitorBtn_);
    monLayout->addWidget(tvBtn);
    connect(tvBtn, &QPushButton::clicked, this, &MainWindow::onTvTest);
    monBox->setLayout(monLayout);
    vbox->addWidget(monBox);
    bramLabel_ = new QLabel(T("BRAM: -"), central);
    bramLabel_->setStyleSheet(
        "background-color: #0a0e14; border: 1px solid #2a3a4a; "
        "border-radius: 4px; padding: 6px 10px; color: #3dd68c;");
    bramLabel_->setMinimumHeight(28);
    vbox->addWidget(bramLabel_);

    acList_ = new QListWidget(central);
    acList_->setAlternatingRowColors(true);
    vbox->addWidget(acList_, 1);
    connect(acList_, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it){
        qlonglong aid = it->data(Qt::UserRole).toLongLong();
        if (aid > 0) QDesktopServices::openUrl(
            QUrl(QString("https://retroachievements.org/achievement/%1").arg(aid)));
    });

    log_ = new QPlainTextEdit(central);
    log_->setReadOnly(true);
    vbox->addWidget(log_);
    busyIndicator_ = new QProgressBar(central);
    busyIndicator_->setRange(0, 100);
    busyIndicator_->setTextVisible(true);
    busyIndicator_->setVisible(false);
    busyIndicator_->setMaximumHeight(18);
    vbox->addWidget(busyIndicator_);
    vbox->addWidget(sdCancelBtn_);

    central->setLayout(vbox);
    setCentralWidget(central);
    resize(700, 820);

    cache_ = std::make_shared<RaCache>("ra_cache.json");
    client_ = std::make_shared<RaClient>(*cache_, make_qt_request_fn());

    auto* optBtn = menuBar()->addAction(T("Optionen"));
    connect(optBtn, &QAction::triggered, this, &MainWindow::onOptions);

    connect(chooseBtn_, &QPushButton::clicked, this, &MainWindow::onChooseRom);
    connect(patchBtn_, &QPushButton::clicked, this, &MainWindow::onPatch);
    connect(loginBtn_, &QPushButton::clicked, this, &MainWindow::onLogin);
    connect(updateHooksBtn_, &QPushButton::clicked, this, &MainWindow::onUpdateHooks);
    connect(monitorBtn_, &QPushButton::clicked, this, &MainWindow::onToggleMonitor);
    connect(sdExportBtn_, &QPushButton::clicked, this, &MainWindow::onSdExport);
    connect(batchPatchBtn_, &QPushButton::clicked, this, &MainWindow::onBatchPatch);

    // Tooltips
    chooseBtn_->setToolTip(T("EIN Spiel waehlen (ROM/ZIP/RAR), pruefen und einzeln patchen."));
    patchBtn_->setToolTip(T("Erstellt die IPS-Patchdatei fuer das aktuell geladene Spiel (neben die ROM). Auf SD-Karte zur ROM legen."));
    batchPatchBtn_->setToolTip(T("Ganzen ORDNER patchen: jede ROM identifizieren und IPS daneben erstellen. Schon gepatchte werden uebersprungen. Schreibt batch_log.txt + verify_report.txt."));
    sdExportBtn_->setToolTip(T("Spiele von Quelle auf die SD KOPIEREN — optional gleich patchen und in Buchstaben-Ordner (A-E ...) sortieren gegen das EverDrive-Dateilimit."));
    monitorBtn_->setToolTip(T("Startet die Live-Ueberwachung: liest per USB den Spielstand und schaltet Achievements frei. Spiel auf der Konsole starten."));
    tvBtn->setToolTip(T("Testet den goldenen Bildschirm-Blitz, der bei einem freigeschalteten Achievement auf dem Fernseher erscheint."));
    lightStubCheck_->setToolTip(T("Light-Stub: verteilt die RAM-Spiegelung rotierend ueber mehrere Frames -> deutlich weniger VBlank-Last, behebt Flackern bei zeitkritischen Spielen. Kein TV-Goldblitz; Werte aktualisieren rotierend (fuer Achievements irrelevant). Nur aktivieren, wenn ein Spiel mit dem normalen Patch Grafikfehler zeigt."));
    updateHooksBtn_->setToolTip(T("Laedt nach dem Release verifizierte Korrekturen fuer einzelne Spiele nach (z.B. bessere Stub-Platzierung), ohne dass ein neues Programm-Update noetig ist. Funktioniert nur mit Internetverbindung."));

    // Stub-Datenbank: zuletzt bekannten Stand von Platte laden (kein Netzwerk).
    // Wirkt sich nur aus, wenn vorher schon einmal "aktualisieren" geklickt wurde.
    load_remote_hooks_cache(hooksCachePath().toStdString());

    // Update-Check 2s nach Start (async, blockiert UI nicht)
    QTimer::singleShot(2000, this, &MainWindow::checkForUpdate);
}

QString MainWindow::hooksCachePath() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("hooks_cache.json");
}

void MainWindow::onUpdateHooks() {
    updateHooksBtn_->setEnabled(false);
    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl("https://liquid-wq.github.io/data/hook_datenbank.json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "MEGA-RAW-HooksUpdate");
    QNetworkReply* reply = nam->get(req);
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(8000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, timer]() {
        timer->stop(); timer->deleteLater();
        updateHooksBtn_->setEnabled(true);
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, T("Stub-Datenbank"),
                T("Konnte Stub-Datenbank nicht laden (kein Internet oder Server nicht erreichbar). "
                  "Bisheriger Stand bleibt aktiv."));
            reply->deleteLater(); nam->deleteLater();
            return;
        }
        std::string text = QString::fromUtf8(reply->readAll()).toStdString();
        int n = load_remote_hooks_from_json(text);
        if (n < 0) {
            QMessageBox::warning(this, T("Stub-Datenbank"),
                T("Antwort vom Server war ungueltig. Bisheriger Stand bleibt aktiv."));
        } else {
            save_remote_hooks_cache(hooksCachePath().toStdString(), text);
            QString msg = QString(g_lang == "en" ? "Updated: %1 verified fixes loaded."
                                                  : "Aktualisiert: %1 verifizierte Fixes geladen.").arg(n);
            if (n > 0) {
                auto names = remote_hook_names();
                msg += "\n";
                for (auto& name : names) msg += "\n- " + QString::fromStdString(name);
            }
            QMessageBox::information(this, T("Stub-Datenbank"), msg);
        }
        reply->deleteLater(); nam->deleteLater();
    });
}

void MainWindow::checkForUpdate() {
    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl("https://liquid-wq.github.io/data/version_cpp.txt"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "MEGA-RAW-UpdateCheck");
    QNetworkReply* reply = nam->get(req);
    // 8s Timeout
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(8000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, timer]() {
        timer->stop(); timer->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QString body = QString::fromUtf8(reply->readAll()).trimmed();
            bool ok = false;
            int remote = body.toInt(&ok);
            if (ok && remote > MEGA_RAW_CPP_BUILD) {
                QMessageBox::information(this, T("Update verfuegbar"),
                    QString(g_lang == "en"
                        ? "A new version is available (Build %1, you have Build %2).\n\nVisit the support page to download."
                        : "Eine neue Version ist verfuegbar (Build %1, du hast Build %2).\n\nBesuche die Support-Seite zum Download.")
                    .arg(remote).arg(MEGA_RAW_CPP_BUILD));
            }
        }
        reply->deleteLater(); nam->deleteLater();
    });
}

MainWindow::~MainWindow() {
    stopMonitorIfRunning();
}

void MainWindow::appendLog(const QString& line) {
    log_->appendPlainText(line);
}

void MainWindow::rebuildAcList() {
    acList_->clear();
    if (!game_ || game_->achievements.empty()) {
        acList_->addItem(game_ && game_->no_set
            ? T("Keine Core-Achievements auf RA fuer dieses Spiel")
            : T("Keine Achievements geladen."));
        return;
    }
    for (const auto& ac : game_->achievements) {
        QString mark, suffix; QColor color;
        bool hasRegion = (ac.mem.find("0xS00fff9=0") != std::string::npos) ||
                         (ac.mem.find("O:0xS00fff9!=0") != std::string::npos);
        if (ac.owned) {
            mark = QChar(0x2605); suffix = "  [bereits freigeschaltet]"; color = QColor("#d4af37");
        } else if (ac.triggered) {
            mark = QChar(0x2605); suffix = "  [gerade freigeschaltet]"; color = QColor("#d4af37");
        } else if (ac.unsupported) {
            mark = QChar(0x2717); suffix = "  [nicht unterstuetzt]"; color = QColor("#888888");
        } else if (hasRegion && consolePal_ == 1) {
            mark = QChar(0x25D0); suffix = "  [PAL-GESPERRT: Konsole laeuft 50Hz]"; color = QColor("#c04040");
        } else if (hasRegion && consolePal_ < 0) {
            mark = QChar(0x25CB); suffix = "  [region-abhaengig: braucht 60Hz]"; color = QColor("#d4af37");
        } else {
            mark = QChar(0x25CB); suffix = ""; color = QColor("#cccccc");
        }
        QString text = QString("%1  %2 (%3p) \u2014 %4%5")
            .arg(mark)
            .arg(QString::fromStdString(ac.title))
            .arg(ac.points)
            .arg(QString::fromStdString(ac.desc))
            .arg(suffix);
        auto* item = new QListWidgetItem(text, acList_);
        item->setForeground(color);
        item->setData(Qt::UserRole, static_cast<qlonglong>(ac.id));
        item->setSizeHint(QSize(0, 32)); // mehr Zeilenhoehe fuer Badge-Icons
        if (!ac.badge.empty()) {
            QPixmap pm = badges_.get(QString::fromStdString(ac.badge));
            if (!pm.isNull()) item->setIcon(QIcon(pm.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
    }
}

void MainWindow::onLogin() {
    std::string user = userEdit_->text().toStdString();
    std::string pw = passEdit_->text().toStdString();
    if (user.empty() || pw.empty()) {
        loginStatus_->setText(T("Bitte Benutzername und Passwort eingeben."));
        return;
    }
    loginBtn_->setEnabled(false);
    loginStatus_->setText(T("Login laeuft..."));
    auto* thread = new QThread(this);
    QObject::connect(thread, &QThread::started, thread, [this, user, pw, thread]() {
        QString statusText, styleSheet;
        QString newToken;
        bool ok = false;
        try {
            auto tok = client_->ra_login(user, pw);
            if (tok) {
                newToken = QString::fromStdString(*tok);
                statusText = QString::fromUtf8("\u2713 ") + QString::fromStdString(user);
                styleSheet = "color: #00ff00;";
                ok = true;
            } else {
                statusText = T("Login fehlgeschlagen (falsche Zugangsdaten?).");
            }
        } catch (const RateLimited& e) {
            statusText = QString("RA drosselt, bitte %1s warten.").arg(e.retry_after);
        } catch (const std::exception& e) {
            statusText = QString("Netzwerkfehler: %1").arg(e.what());
        }
        QMetaObject::invokeMethod(this, [this, statusText, styleSheet, newToken, ok]() {
            loginBtn_->setEnabled(true);
            loginStatus_->setText(statusText);
            if (ok) {
                loginStatus_->setStyleSheet(styleSheet);
                token_ = newToken;
                appendLog("Login erfolgreich.");
                updateMonitorState();
            }
        }, Qt::QueuedConnection);
        thread->quit();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::onChooseRom() {
    QString path = QFileDialog::getOpenFileName(this, "ROM waehlen", QString(),
        "Mega Drive ROMs / Archive (*.md *.bin *.gen *.smd *.zip *.rar);;Alle Dateien (*)");
    if (path.isEmpty()) return;

    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "zip" || ext == "rar") {
        auto ex = extract_archive_rom(path);   // neben Archiv entpacken
        if (!ex) { appendLog(T("Keine MD-ROM im Archiv gefunden (oder RAR ohne WinRAR).")); return; }
        appendLog(QString("Aus Archiv entpackt: %1").arg(QFileInfo(*ex).fileName()));
        path = *ex;
    }

    romPath_ = path;
    romPathEdit_->setText(path);
    game_.reset();
    patchBtn_->setEnabled(false);
    setHasSet_ = false;
    updateMonitorState();

    if (!QFile::exists(path)) {
        statusLabel_->setText(T("Fehler: Datei existiert nicht."));
        appendLog("Fehler: Datei nicht gefunden: " + path);
        return;
    }
    std::string md5 = md5_file(path.toStdString());
    // Stub-Adresse fuer Anti-Cheat-Verifikation vormerken
    {
        std::ifstream rf(path.toStdString(), std::ios::binary);
        rf.seekg(0, std::ios::end);
        std::streamsize rb_sz = rf.tellg();
        rf.seekg(0, std::ios::beg);
        std::vector<uint8_t> rb(rb_sz > 0 ? static_cast<size_t>(rb_sz) : 0);
        if (rb_sz > 0) rf.read(reinterpret_cast<char*>(rb.data()), rb_sz);
        auto sa = find_stub_addr(rb, 60 + 10*40 + 100);
        stubAddrForVerify_ = sa.stub_addr;
    }
    statusLabel_->setText(QString("MD5: %1 - suche Achievement-Set...").arg(QString::fromStdString(md5)));
    appendLog("ROM geladen: " + path);
    appendLog("MD5: " + QString::fromStdString(md5));

    tryLoadGame(md5);
}

void MainWindow::tryLoadGame(const std::string& md5) {
    try {
        auto gid = client_->ra_gameid(md5, true);
        if (!gid) {
            statusLabel_->setText(T("Kein RetroAchievements-Set fuer diese ROM gefunden."));
            appendLog("RA kennt diese ROM nicht (MD5 unbekannt).");
            return;
        }
        appendLog(QString("RA GameID: %1").arg(*gid));

        if (token_.isEmpty()) {
            statusLabel_->setText(T("GameID gefunden, aber kein Login -> kein Patch-Data-Abruf."));
            appendLog("Bitte zuerst einloggen, um das Achievement-Set zu laden.");
            return;
        }

        auto patch = client_->ra_patch(*gid, userEdit_->text().toStdString(), token_.toStdString());
        Game g = build_game_from_patch(*gid, md5, patch);
        if (!g.valid) {
            statusLabel_->setText(T("Kein Patch-Data von RA erhalten."));
            appendLog("ra_patch lieferte kein gueltiges PatchData.");
            return;
        }
        if (g.no_set) {
            statusLabel_->setText(QString::fromStdString(g.name) + " - keine unterstuetzte ROM-Version.");
            appendLog("Unsupported game version laut RA.");
            return;
        }

        // Bereits freigeschaltete Achievements golden markieren
        try {
            auto owned = client_->ra_unlocks(*gid, userEdit_->text().toStdString(), token_.toStdString());
            int nOwned = 0;
            for (auto& a : g.achievements) {
                if (owned.count(a.id)) { a.owned = true; a.triggered = true; nOwned++; }
            }
            if (nOwned) appendLog(QString("%1 Achievement(s) bereits freigeschaltet.").arg(nOwned));
        } catch (...) {}

        game_ = g;
        // Game-Info
        {
            int n = static_cast<int>(g.raw_core);
            int uns = static_cast<int>(g.n_unsupported);
            QString info = QString::fromUtf8("\u2713 ") + QString::fromStdString(g.name) + "\n";
            // Compat-Check
            bool compat = true;
            if (!romPath_.isEmpty()) {
                std::ifstream rf(romPath_.toStdString(), std::ios::binary);
                rf.seekg(0, std::ios::end);
        std::streamsize rb_sz = rf.tellg();
        rf.seekg(0, std::ios::beg);
        std::vector<uint8_t> rb(rb_sz > 0 ? static_cast<size_t>(rb_sz) : 0);
        if (rb_sz > 0) rf.read(reinterpret_cast<char*>(rb.data()), rb_sz);
                CompatResult cr = check_compat(rb, n, uns);
                compat = cr.ok;
                for (const QString& l : cr.lines) appendLog(l);
            }
            info += QString("[%1]\n").arg(compat ? "KOMPATIBEL" : "NICHT KOMPATIBEL");
            info += QString("Achievements gesamt: %1\n").arg(n);
            info += QString("Von der Engine lesbar: %1/%2").arg(n - uns).arg(n);
            statusLabel_->setText(info);
            statusLabel_->setStyleSheet(compat
                ? "color: #00ff00; font-family: Courier; font-size: 9pt;"
                : "color: #ff4444; font-family: Courier; font-size: 9pt;");
        }
        appendLog(QString("Set geladen: %1 Core-Achievements, %2 RAM-Bytes zu spiegeln")
            .arg(g.raw_core).arg(g.addr_list.size()));
        patchBtn_->setEnabled(!g.addr_list.empty());
        setHasSet_ = !g.addr_map.empty();
        updateMonitorState();
        rebuildAcList();
    } catch (const RateLimited& e) {
        statusLabel_->setText(QString("RA drosselt, bitte %1s warten.").arg(e.retry_after));
    } catch (const std::exception& e) {
        statusLabel_->setText(QString("Netzwerkfehler: %1").arg(e.what()));
    }
}

void MainWindow::onPatch() {
    if (!game_ || romPath_.isEmpty()) return;

    std::ifstream f(romPath_.toStdString(), std::ios::binary);
    f.seekg(0, std::ios::end);
        std::streamsize rom_sz = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> rom(rom_sz > 0 ? static_cast<size_t>(rom_sz) : 0);
        if (rom_sz > 0) f.read(reinterpret_cast<char*>(rom.data()), rom_sz);
    if (rom.empty()) {
        QMessageBox::warning(this, T("Fehler"), "ROM konnte nicht gelesen werden.");
        return;
    }

    PatchResult r = patch_rom(rom, romPath_.toStdString(), *game_, lightStubCheck_->isChecked());
    if (!r.ok) {
        appendLog("Patch fehlgeschlagen: " + QString::fromStdString(r.msg));
        QMessageBox::warning(this, T("Patch fehlgeschlagen"), QString::fromStdString(r.msg));
        return;
    }

    std::ofstream out(r.ips_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(r.ips_bytes.data()),
              static_cast<std::streamsize>(r.ips_bytes.size()));
    out.close();

    appendLog(QString::fromStdString(r.msg));
    appendLog("IPS geschrieben: " + QString::fromStdString(r.ips_path));
    appendLog(QString("Boot-Risiko: %1").arg(QString::fromStdString(r.risk.level)));
    for (auto& reason : r.risk.reasons) appendLog("  - " + QString::fromStdString(reason));

    QMessageBox::information(this, T("Fertig"),
        QString::fromStdString(r.msg) + "\n\nIPS: " + QString::fromStdString(r.ips_path));
}

void MainWindow::onSdExport() {
    if (token_.isEmpty()) {
        QMessageBox::warning(this, T("Login noetig"),
            "Bitte zuerst einloggen (fuer Achievement-Set-Abruf).");
        return;
    }
    QString src = QFileDialog::getExistingDirectory(this, T("Quellordner (ROMs) waehlen"));
    if (src.isEmpty()) return;
    QString dst = QFileDialog::getExistingDirectory(this, T("Zielordner (SD-Karte) waehlen"));
    if (dst.isEmpty()) return;

    QDialog dlg(this);
    dlg.setWindowTitle(T("SD-EXPORT"));
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(QString("Quelle: %1\nZiel: %2").arg(src, dst), &dlg));
    auto* cbIps = new QCheckBox(T("IPS gleich miterstellen"), &dlg); cbIps->setChecked(true);
    auto* cbBucket = new QCheckBox(T("In Ordner sortieren (automatisch, EverDrive-Dateilimit)"), &dlg);
    auto* cbDel = new QCheckBox(T("Quell-Archiv (RAR/ZIP) nach Entpacken loeschen"), &dlg);
    v->addWidget(cbIps); v->addWidget(cbBucket); v->addWidget(cbDel);
    auto* row = new QHBoxLayout();
    auto* startBtn = new QPushButton(T("START"), &dlg);
    auto* cancelBtn = new QPushButton(T("SCHLIESSEN"), &dlg);
    row->addWidget(startBtn); row->addWidget(cancelBtn);
    v->addLayout(row);
    connect(startBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    bool doIps = cbIps->isChecked();
    bool doBucket = cbBucket->isChecked();
    bool delArc = cbDel->isChecked();

    auto* thread = new QThread(this);
    auto* worker = new SdExportWorker(src, dst, doIps, doBucket,
                                      userEdit_->text(), token_, client_, delArc);
    worker->moveToThread(thread);
    sdCancelBtn_->setEnabled(true); sdCancelBtn_->setVisible(true);
    busyIndicator_->setValue(0);
    busyIndicator_->setVisible(true);
    connect(sdCancelBtn_, &QPushButton::clicked, worker, &SdExportWorker::stop, Qt::DirectConnection);
    connect(thread, &QThread::started, worker, &SdExportWorker::start);
    connect(worker, &SdExportWorker::progress, this, [this](const QString& l){ appendLog(l); });
    connect(worker, &SdExportWorker::progressPercent, this, [this](int cur, int total){
        if (total > 0) busyIndicator_->setValue(cur * 100 / total);
    });
    connect(worker, &SdExportWorker::finishedSummary, this, [this, thread, worker](const QString& s){
        appendLog(s);
        sdCancelBtn_->setEnabled(false); sdCancelBtn_->setVisible(false);
        busyIndicator_->setVisible(false);
        QMessageBox::information(this, T("SD-Export fertig"), s);
        thread->quit(); thread->wait(2000);
        worker->deleteLater(); thread->deleteLater();
    });
    connect(worker, &SdExportWorker::finishedDetail, this, [this](const QString& d){
        QMessageBox::information(this, T("SD-Export fertig"), d);
    });
    appendLog("SD-Export gestartet: " + src + " -> " + dst);
    thread->start();
}

void MainWindow::onBatchPatch() {
    if (token_.isEmpty()) {
        QMessageBox::warning(this, T("Login noetig"),
            "Bitte zuerst einloggen (fuer Achievement-Set-Abruf).");
        return;
    }
    QString folder = QFileDialog::getExistingDirectory(this, T("Ordner mit ROMs waehlen"));
    if (folder.isEmpty()) return;

    auto* thread = new QThread(this);
    auto* worker = new BatchPatchWorker(folder, userEdit_->text(), token_,
                                        settings_.av_safe, client_);
    worker->moveToThread(thread);
    sdCancelBtn_->setEnabled(true); sdCancelBtn_->setVisible(true);
    busyIndicator_->setValue(0);
    busyIndicator_->setVisible(true);
    auto conn = connect(sdCancelBtn_, &QPushButton::clicked, worker, &BatchPatchWorker::stop, Qt::DirectConnection);
    connect(thread, &QThread::started, worker, &BatchPatchWorker::start);
    connect(worker, &BatchPatchWorker::progress, this, [this](const QString& l){ appendLog(l); });
    connect(worker, &BatchPatchWorker::progressPercent, this, [this](int cur, int total){
        if (total > 0) busyIndicator_->setValue(cur * 100 / total);
    });
    connect(worker, &BatchPatchWorker::finishedSummary, this, [this, thread, worker, conn](const QString& s){
        appendLog(s);
        disconnect(conn);
        sdCancelBtn_->setEnabled(false); sdCancelBtn_->setVisible(false);
        busyIndicator_->setVisible(false);
        QMessageBox::information(this, T("Ordner patchen fertig"), s);
        thread->quit(); thread->wait(2000);
        worker->deleteLater(); thread->deleteLater();
    });
    appendLog("Ordner patchen gestartet: " + folder);
    thread->start();
}

void MainWindow::onTvTest() {
    // Wenn Monitor läuft, über bestehende Verbindung
    if (monitoring_ && monWorker_) {
        monWorker_->requestTvFlash();
        appendLog("TV-Test ueber Monitor-Verbindung gesendet...");
        return;
    }
    // Sonst eigene Verbindung aufbauen
    auto* thread = new QThread(this);
    QObject::connect(thread, &QThread::started, thread, [this, thread]() {
        QString msg;
        try {
            auto [edPtr, foundPort] = find_everdrive(QString());
            if (!edPtr) {
                msg = "TV-Test: Kein EverDrive gefunden (Monitor laeuft nicht, kein Port offen).";
            } else {
                edPtr->tvFlash();
                edPtr->close();
                msg = "TV-Test gesendet auf " + foundPort + " (Schirm sollte golden aufpulsen).";
            }
        } catch (const std::exception& e) {
            msg = QString("TV-Test fehlgeschlagen: %1").arg(e.what());
        }
        QMetaObject::invokeMethod(this, [this, msg]{ appendLog(msg); }, Qt::QueuedConnection);
        thread->quit();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    appendLog("TV-Test startet...");
    thread->start();
}

void MainWindow::onOptions() {
    QDialog dlg(this);
    dlg.setWindowTitle(T("Optionen"));
    auto* v = new QVBoxLayout(&dlg);

    auto* hc = new QCheckBox(T("Hardcore-Modus (vorerst gesperrt - nur Softcore)"), &dlg);
    hc->setChecked(false); hc->setEnabled(false);
    auto* av = new QCheckBox(T("AV-Modus (kann Ransomware-Fehlalarme verhindern)"), &dlg);
    av->setChecked(settings_.av_safe);
    auto* tv = new QCheckBox(T("TV-Goldblitz bei Freischaltung"), &dlg);
    tv->setChecked(settings_.tv_flash);
    v->addWidget(hc); v->addWidget(av); v->addWidget(tv);

    auto* bucketRow = new QHBoxLayout();
    bucketRow->addWidget(new QLabel(T("SD-Export Buchstaben-Gruppen:"), &dlg));
    auto* bAE = new QRadioButton("A-E", &dlg);
    auto* bAZ = new QRadioButton("A-Z", &dlg);
    (settings_.bucket == "A-Z" ? bAZ : bAE)->setChecked(true);
    bucketRow->addWidget(bAE); bucketRow->addWidget(bAZ);
    v->addLayout(bucketRow);

    auto* langRow = new QHBoxLayout();
    langRow->addWidget(new QLabel(T("Sprache / Language:"), &dlg));
    auto* lDe = new QRadioButton("Deutsch", &dlg);
    auto* lEn = new QRadioButton("English", &dlg);
    (settings_.language == "en" ? lEn : lDe)->setChecked(true);
    langRow->addWidget(lDe); langRow->addWidget(lEn);
    v->addLayout(langRow);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    v->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // Support + "über die Katze"
    auto* supportLbl = new QLabel(
        "<a href=\"https://liquid-wq.github.io/data/\">Support</a><br><i>" +
        T("aber falls du darueber nachdenkst,\nlies bitte zuerst 'ueber die Katze'").replace("\n", "<br>") +
        "</i>", &dlg);
    supportLbl->setOpenExternalLinks(true);
    supportLbl->setAlignment(Qt::AlignCenter);
    v->addWidget(supportLbl);

    auto* catBtn = new QPushButton(T("ueber die Katze"), &dlg);
    v->addWidget(catBtn);
    connect(catBtn, &QPushButton::clicked, &dlg, [this, &dlg]() {
        QDialog cw(&dlg);
        cw.setWindowTitle(T("ueber die Katze"));
        cw.resize(520, 560);
        auto* cv = new QVBoxLayout(&cw);
        auto* browser = new QTextEdit(&cw);
        browser->setReadOnly(true);
        browser->setPlainText(settings_.language == "en" ? catTextEn() : catTextDe());
        cv->addWidget(browser);
        auto* ok = new QPushButton(T("OK"), &cw);
        connect(ok, &QPushButton::clicked, &cw, &QDialog::accept);
        cv->addWidget(ok);
        cw.exec();
    });

    if (dlg.exec() == QDialog::Accepted) {
        settings_.av_safe = av->isChecked();
        settings_.tv_flash = tv->isChecked();
        settings_.bucket = bAZ->isChecked() ? "A-Z" : "A-E";
        settings_.language = lEn->isChecked() ? "en" : "de";
        g_lang = settings_.language;
        QMessageBox::information(this, T("Hinweis"),
            g_lang=="en" ? "Language set. Restart the tool to apply all texts."
                         : "Sprache gesetzt. Tool neu starten, damit alle Texte umgestellt sind.");
        settings_.save(settingsPath_);
        appendLog("Einstellungen gespeichert.");
    }
}

void MainWindow::stopMonitorIfRunning() {
    if (monThread_) {
        if (monWorker_) monWorker_->stop();
        monThread_->quit();
        monThread_->wait(3000);
        monThread_->deleteLater();
        monThread_ = nullptr;
        monWorker_ = nullptr;
    }
}

void MainWindow::onToggleMonitor() {
    if (monitoring_) {
        stopMonitorIfRunning();
        monitoring_ = false;
        monitorBtn_->setText(T("Verbinden && Monitor starten"));
        appendLog("Monitor gestoppt.");
        return;
    }
    if (token_.isEmpty()) { appendLog("Bitte zuerst einloggen."); return; }

    QString port;  // leer -> Autoerkennung scannt alle Ports

    monThread_ = new QThread(this);
    Game gm = game_ ? *game_ : Game{};
    monWorker_ = new MonitorWorker(port, gm, userEdit_->text(), token_, client_, stubAddrForVerify_, settings_.tv_flash);
    monWorker_->moveToThread(monThread_);

    connect(monThread_, &QThread::started, monWorker_, &MonitorWorker::start);
    connect(monWorker_, &MonitorWorker::log, this, &MainWindow::onWorkerLog);
    connect(monWorker_, &MonitorWorker::bramDump, this, &MainWindow::onWorkerBram);
    connect(monWorker_, &MonitorWorker::palState, this, [this](bool pal){
        consolePal_ = pal ? 1 : 0; rebuildAcList();
    }, Qt::QueuedConnection);
    connect(monWorker_, &MonitorWorker::gameDetected, this, [this](int gid){
        if (game_ && game_->gameid == gid) return;
        try {
            auto patch = client_->ra_patch(gid, userEdit_->text().toStdString(), token_.toStdString());
            Game g = build_game_from_patch(gid, "", patch);
            if (!g.valid) {
                appendLog(QString("Auto-Laden: kein gueltiges Patch-Data fuer RA-ID %1.").arg(gid));
                return;
            }
            if (g.addr_map.empty()) {
                appendLog(QString("Auto-Laden: RA-ID %1 hat %2 Achievement(s), aber keine davon "
                    "hat eine von der Engine lesbare Adresse.").arg(gid).arg(g.raw_core));
                return;
            }
            {
                try {
                    auto owned = client_->ra_unlocks(gid, userEdit_->text().toStdString(), token_.toStdString());
                    int nOwned = 0;
                    for (auto& a : g.achievements)
                        if (owned.count(a.id)) { a.owned = true; a.triggered = true; nOwned++; }
                    if (nOwned) appendLog(QString("%1 Achievement(s) bereits freigeschaltet.").arg(nOwned));
                } catch (...) {}
                game_ = g;
                setHasSet_ = true;
                // Worker direkt updaten
                if (monWorker_) monWorker_->updateGame(g);
                int n = static_cast<int>(g.raw_core);
                int uns = static_cast<int>(g.n_unsupported);
                QString info = QString::fromUtf8("\u2713 ") + QString::fromStdString(g.name) + "\n";
                info += QString("[KOMPATIBEL]\n");
                info += QString("Achievements gesamt: %1\n").arg(n);
                info += QString("Von der Engine lesbar: %1/%2").arg(n - uns).arg(n);
                statusLabel_->setText(info);
                statusLabel_->setStyleSheet("color: #00ff00; font-family: Courier; font-size: 9pt;");
                appendLog(QString("Set automatisch geladen: %1").arg(QString::fromStdString(g.name)));
                rebuildAcList();
            }
        } catch (const std::exception& e) {
            appendLog(QString("Auto-Laden fehlgeschlagen: ") + e.what());
        }
    }, Qt::QueuedConnection);
    connect(monWorker_, &MonitorWorker::unlocked, this, &MainWindow::onWorkerUnlocked);
    connect(monWorker_, &MonitorWorker::connectionLost, this, &MainWindow::onWorkerConnectionLost);
    connect(monWorker_, &MonitorWorker::finished, this, &MainWindow::onWorkerFinished);
    connect(monWorker_, &MonitorWorker::finished, monWorker_, &QObject::deleteLater);

    monThread_->start();
    monitoring_ = true;
    monitorBtn_->setText(T("Monitor stoppen"));
    monitorHint_->setText(QString::fromUtf8("\u2713 Verbunden ") + port);
    monitorHint_->setStyleSheet("color: #00ff00;");
    appendLog("Verbinde mit " + port + " ...");
}

void MainWindow::updateMonitorState() {
    const bool loggedIn = !token_.isEmpty();
    monitorBtn_->setEnabled(loggedIn);
    if (!loggedIn)
        monitorHint_->setText(T("Monitor: zuerst einloggen."));
    else if (!setHasSet_)
        monitorHint_->setText(T("Monitor bitte nach Spielstart starten."));
    else {
        monitorHint_->setText(QString::fromUtf8("\u2713 ") + T("Monitor bereit."));
        monitorHint_->setStyleSheet("color: #00ff00;");
    }
}

void MainWindow::onWorkerLog(const QString& msg) { appendLog(msg); }
void MainWindow::onWorkerBram(const QByteArray& rawBytes) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastByteChangeMs_.size() < rawBytes.size())
        lastByteChangeMs_.resize(rawBytes.size(), 0);

    QString html = "<div style=\"font-family:'Consolas','Courier New',monospace; font-size:11pt; "
                   "letter-spacing:3px;\">";
    for (int i = 0; i < rawBytes.size(); ++i) {
        uint8_t b = static_cast<uint8_t>(rawBytes[i]);
        QString hex = QString("%1").arg(b, 2, 16, QChar('0')).toUpper();
        bool changed = (i >= lastBramBytes_.size()) ||
                       (static_cast<uint8_t>(lastBramBytes_[i]) != b);
        if (changed && !lastBramBytes_.isEmpty()) lastByteChangeMs_[i] = now;

        qint64 age = now - lastByteChangeMs_[i];
        if (lastByteChangeMs_[i] > 0 && age < 2000) {
            // 0-1000ms: voll rot. 1000-2000ms: Verlauf rot -> gruen.
            double t = (age < 1000) ? 0.0 : (age - 1000) / 1000.0;
            int r = static_cast<int>(0xc0 + t * (0x3d - 0xc0));
            int g = static_cast<int>(0x39 + t * (0xd6 - 0x39));
            int bch = static_cast<int>(0x2b + t * (0x8c - 0x2b));
            html += QString("<span style=\"color:#%1%2%3; font-weight:bold;\">%4</span> ")
                .arg(r, 2, 16, QChar('0')).arg(g, 2, 16, QChar('0')).arg(bch, 2, 16, QChar('0')).arg(hex);
        } else {
            html += QString("<span style=\"color:#3dd68c;\">%1</span> ").arg(hex);
        }
    }
    html += "</div>";
    lastBramBytes_ = rawBytes;
    bramLabel_->setTextFormat(Qt::RichText);
    bramLabel_->setText(html);
}

void MainWindow::onWorkerUnlocked(const QString& title, int points, qlonglong achId) {
    appendLog(QString("*** FREIGESCHALTET: %1 (%2 Punkte, ID %3) ***").arg(title).arg(points).arg(achId));
    playUnlockSound();
    if (game_) {
        for (auto& a : game_->achievements)
            if (a.id == achId) { a.triggered = true; break; }
        rebuildAcList();
    }
}

void MainWindow::onWorkerConnectionLost(const QString& reason) {
    bramLabel_->setTextFormat(Qt::PlainText);
    bramLabel_->setText(T("BRAM: [Verbindung verloren - ") + reason + "]");
    bramLabel_->setStyleSheet(
        "background-color: #1a0a0a; border: 1px solid #6a2a2a; "
        "border-radius: 4px; padding: 6px 10px; color: #ff4444; font-weight: bold;");
    monitorHint_->setText(QString::fromUtf8("\u26A0 Verbindung verloren: ") + reason);
    monitorHint_->setStyleSheet("color: #ff4444;");
}

void MainWindow::onWorkerFinished() {
    monWorker_ = nullptr;
    if (monitoring_) {
        monitoring_ = false;
        monitorBtn_->setText(T("Verbinden && Monitor starten"));
        monitorHint_->setStyleSheet("");
        bramLabel_->setTextFormat(Qt::PlainText);
        bramLabel_->setText(T("BRAM: -"));
        bramLabel_->setStyleSheet(
            "background-color: #0a0e14; border: 1px solid #2a3a4a; "
            "border-radius: 4px; padding: 6px 10px; color: #3dd68c;");
        updateMonitorState();
    }
}






void MainWindow::playUnlockSound() {
    // 1:1 zu play_unlock_sound(): achievement.wav neben der exe, sonst Beep-Melodie.
    QString wav = QDir(QCoreApplication::applicationDirPath()).filePath("achievement.wav");
    if (QFile::exists(wav)) {
#ifdef Q_OS_WIN
        PlaySoundW(reinterpret_cast<LPCWSTR>(wav.utf16()), nullptr, SND_FILENAME | SND_ASYNC);
        return;
#endif
    }
#ifdef Q_OS_WIN
    static const int melody[][2] = {{587,140},{659,140},{698,140},{784,140},{880,320},
        {1175,200},{880,160},{698,140},{784,140},{880,420}};
    QThread* t = QThread::create([]{
        for (auto& n : melody) Beep(n[0], n[1]);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
#endif
}
