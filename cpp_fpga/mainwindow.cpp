#include "mainwindow.h"
#include "version.h"
#include "i18n.h"
#include "compat.h"
#include "badge_loader.h"
#include "achievement_popup.h"
#include "archive_extract.h"
#include "md_snoop.h"
#include "md_romindex.h"
#include "md_romread.h"
#include "md5.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif
#include "md5.h"
#include "ra_network.h"
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
#include <QDirIterator>
#include <QInputDialog>
#include <QSet>
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

    setWindowTitle(QString("MEGA-RAW FPGA v%1  (Build %2)").arg(MEGA_RAW_VERSION).arg(MEGA_RAW_CPP_BUILD));

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
    auto* copyrightLabel = new QLabel(QString::fromUtf8("MEGA-RAW FPGA \u2014 \u00a9 2026 Liqui"), central);
    copyrightLabel->setAlignment(Qt::AlignCenter);
    copyrightLabel->setStyleSheet("color: #888888; font-size: 8pt;");
    vbox->addWidget(copyrightLabel);

    // Schnellstart fuer neue Nutzer
    auto* quickstartLabel = new QLabel(
        T("Schnellstart:  1. Mapper auf SD einrichten   2. Einloggen   3. Spiel auf der Konsole starten   4. Monitor starten"),
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

    // --- ROM ---
    auto* hbox = new QHBoxLayout();
    romPathEdit_ = new QLineEdit(central);
    romPathEdit_->setReadOnly(true);
    romPathEdit_->setPlaceholderText(T("Keine ROM gewaehlt"));
    chooseBtn_ = new QPushButton(T("ROM waehlen..."), central);
    hbox->addWidget(romPathEdit_);
    hbox->addWidget(chooseBtn_);
    vbox->addLayout(hbox);

    setupMapperBtn_ = new QPushButton(T("Mapper auf SD-Karte einrichten..."), central);
    vbox->addWidget(setupMapperBtn_);

    statusLabel_ = new QLabel(T("Bereit."), central);
    vbox->addWidget(statusLabel_);

    // --- Monitor / Hardware ---
    auto* monBox = new QGroupBox(T("Live-Monitor (EverDrive)"), central);
    auto* monLayout = new QHBoxLayout();
    monitorBtn_ = new QPushButton(T("Verbinden && Monitor starten"), monBox);
    monitorBtn_->setEnabled(false);
    edStatusLabel_ = new QLabel(T("EverDrive wird beim Monitor-Start automatisch gesucht"), monBox);
    monLayout->addWidget(edStatusLabel_);
    monitorHint_ = new QLabel(T("Monitor: zuerst einloggen."), monBox);
    monLayout->addWidget(monitorHint_);

    // Hardcore direkt neben dem Verbinden-Knopf: die Einstellung wirkt beim
    // Monitor-Start, also gehoert sie dorthin und nicht in einen Dialog.
    hardcoreChk_ = new QCheckBox(T("Hardcore"), monBox);
    hardcoreChk_->setChecked(settings_.hardcore);
    hardcoreChk_->setToolTip(T("Im Custom-Mapper gibt es keine Savestates und keine Cheats - "
                               "die Hardcore-Bedingung ist damit erfuellt, solange er laeuft. "
                               "Aenderung wirkt ab dem naechsten Monitor-Start."));
    connect(hardcoreChk_, &QCheckBox::toggled, this, [this](bool an) {
        settings_.hardcore = an;
        settings_.save(settingsPath_);
    });
    monLayout->addWidget(hardcoreChk_);
    monLayout->addWidget(monitorBtn_);
    monBox->setLayout(monLayout);
    vbox->addWidget(monBox);
    ramLabel_ = new QLabel(T("RAM: -"), central);
    ramLabel_->setStyleSheet(
        "background-color: #0a0e14; border: 1px solid #2a3a4a; "
        "border-radius: 4px; padding: 6px 10px; color: #3dd68c;");
    ramLabel_->setMinimumHeight(28);
    vbox->addWidget(ramLabel_);

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

    central->setLayout(vbox);
    setCentralWidget(central);
    resize(700, 820);

    cache_ = std::make_shared<RaCache>("ra_cache.json");
    client_ = std::make_shared<RaClient>(*cache_, make_qt_request_fn());

    auto* optBtn = menuBar()->addAction(T("Optionen"));
    connect(optBtn, &QAction::triggered, this, &MainWindow::onOptions);

    auto* kofiBtn = menuBar()->addAction(T("Ko-fi"));
    connect(kofiBtn, &QAction::triggered, this, [this]() {
        // QMessageBox zentriert Text nicht ueber CSS - dafuer braucht es ein
        // eigenes Fenster mit ausgerichtetem Label.
        QDialog kd(this);
        kd.setWindowTitle(T("Ko-fi"));
        auto* kv = new QVBoxLayout(&kd);
        auto* lbl = new QLabel(
            "<i>" + T("aber falls du darueber nachdenkst,\nlies bitte zuerst 'ueber die Katze'")
                     .replace("\n", "<br>") + "</i>", &kd);
        lbl->setTextFormat(Qt::RichText);
        lbl->setAlignment(Qt::AlignCenter);
        kv->addWidget(lbl);

        // Kein Link im Text: erst OK oeffnet die Seite. Wer abbricht,
        // landet nirgendwo.
        auto* kb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &kd);
        kb->setCenterButtons(true);
        connect(kb, &QDialogButtonBox::accepted, &kd, &QDialog::accept);
        connect(kb, &QDialogButtonBox::rejected, &kd, &QDialog::reject);
        kv->addWidget(kb);
        if (kd.exec() == QDialog::Accepted)
            QDesktopServices::openUrl(QUrl("https://ko-fi.com/liqui69747"));
    });

    connect(chooseBtn_, &QPushButton::clicked, this, &MainWindow::onChooseRom);
    connect(setupMapperBtn_, &QPushButton::clicked, this, &MainWindow::onSetupMapper);
    connect(loginBtn_, &QPushButton::clicked, this, &MainWindow::onLogin);
    connect(monitorBtn_, &QPushButton::clicked, this, &MainWindow::onToggleMonitor);

    // Tooltips
    chooseBtn_->setToolTip(T("Spiel waehlen (ROM/ZIP/RAR) und Achievement-Set laden."));
    setupMapperBtn_->setToolTip(T("Durchsucht die SD-Karte nach Ordnern mit ROMs und legt "
                                  "mega-core.x25 in jeden davon. Ohne diese Datei laedt die "
                                  "Konsole den Standard-Mapper und es gibt keine Achievements."));
    monitorBtn_->setToolTip(T("Startet die Live-Ueberwachung: liest per USB den Spielstand und schaltet Achievements frei. Spiel auf der Konsole starten."));


    // Update-Check 2s nach Start (async, blockiert UI nicht)
    QTimer::singleShot(2000, this, &MainWindow::checkForUpdate);
}

void MainWindow::checkForUpdate() {
    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl("https://liquid-wq.github.io/data/version_fpga.txt"));
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
                appendLog(T("Login erfolgreich."));
                updateMonitorState();
            }
        }, Qt::QueuedConnection);
        thread->quit();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// Ordnername der Sicherung. Liegt in dem Wurzelverzeichnis, das beim
// Einrichten gewaehlt wurde, damit Sicherung und Karte zusammenbleiben.
static const char* kBackupDir = "MEGA-RAW_ORIG_BACKUP";

void MainWindow::onSetupMapper() {
    // Die Konsole laedt den Custom-Mapper nur, wenn mega-core.x25 im selben
    // Ordner wie das ROM liegt (fpga/README.md). Bei nach Buchstaben
    // sortierten Sammlungen waeren das dutzende Ordner - deshalb automatisch.
    const QString appDir = QCoreApplication::applicationDirPath();
    QString src = QDir(appDir).filePath("mega-core.x25");
    if (!QFile::exists(src)) {
        src = QFileDialog::getOpenFileName(this, T("mega-core.x25 waehlen"), appDir,
                                           "EverDrive Core (*.x25 *.rbf)");
        if (src.isEmpty()) return;
    }

    // Vor der Auswahl erklaeren, was gemeint ist. Ein Nutzer weiss sonst
    // nicht, ob das Laufwerk oder ein Unterordner erwartet wird - und bei
    // falscher Wahl landet der Kern an einer Stelle, von der die Konsole
    // nicht laedt.
    QMessageBox::information(this, T("Mapper einrichten"),
        T("Bitte im naechsten Fenster die SD-Karte auswaehlen - also das "
          "Laufwerk selbst (z.B. H:\\), das den Ordner MEGA enthaelt, "
          "keinen Unterordner.\n\n"
          "Der Kern wird dann in alle Ordner kopiert, in denen Spiele liegen. "
          "Bereits vorhandene Dateien werden vorher gesichert."));

    const QString root = QFileDialog::getExistingDirectory(this,
        T("SD-Karte waehlen - das Laufwerk selbst (mit dem Ordner MEGA), z.B. H:\\"), settings_.rom_root);
    if (root.isEmpty()) return;
    settings_.rom_root = root;
    settings_.save();

    static const QStringList romExt = {
        "*.md", "*.bin", "*.gen", "*.smd", "*.68k", "*.sgd",
        "*.zip", "*.7z", "*.rar"
    };

    // Jeden Ordner sammeln, in dem mindestens eine ROM liegt.
    QSet<QString> targets;
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    QStringList dirs; dirs << root;
    while (it.hasNext()) dirs << it.next();

    // Krikzz' Systemordner aussparen. In MEGA\\edapp\\* und MEGA\\syscore\\*
    // liegen seine eigenen Kerne unter demselben Namen mega-core.x25, und
    // daneben Dateien mit Endungen aus romExt (.bin) - ohne diese Ausnahme
    // wurden sie als Spielordner gewertet und die Systemkerne ueberschrieben.
    // Auf Hardware passiert: alle fuenf Systemkerne waren durch unseren
    // ersetzt, die EverDrive-Systemanwendungen damit unbrauchbar.
    auto istSystemordner = [](const QString& pfad) {
        const QString p = QDir::fromNativeSeparators(pfad).toLower();
        return p.contains("/mega/edapp/") || p.endsWith("/mega/edapp")
            || p.contains("/mega/syscore/") || p.endsWith("/mega/syscore")
            || p.contains("/mega/mappers") || p.contains("/mega/sysdata");
    };

    for (const QString& d : dirs) {
        if (istSystemordner(d)) continue;
        QDir dir(d);
        if (!dir.entryList(romExt, QDir::Files).isEmpty()) targets.insert(d);
    }

    if (targets.isEmpty()) {
        QMessageBox::information(this, T("Mapper einrichten"),
            T("In diesem Verzeichnis wurden keine ROMs gefunden."));
        return;
    }

    // Vor dem Ueberschreiben sichern. Auf der Karte liegen echte Originale
    // (Krikzz' Kerne), die sonst ersatzlos verloren waeren. Gesichert wird
    // nur beim ersten Mal je Ordner - ein zweiter Durchlauf darf die bereits
    // gesicherte Originaldatei nicht durch unsere eigene ueberschreiben.
    const QString bakRoot = QDir(root).filePath(kBackupDir);
    int ok = 0, fail = 0, saved = 0;
    for (const QString& d : targets) {
        const QString dst = QDir(d).filePath("mega-core.x25");

        // Nur sichern, was NICHT schon unser eigener Kern ist. Sonst legt
        // ein zweiter Durchlauf lauter Sicherungen unserer eigenen Datei an -
        // eine Wiederherstellung waere dann wirkungslos.
        if (QFile::exists(dst) && QFileInfo(dst).size() != QFileInfo(src).size()) {
            const QString rel = QDir(root).relativeFilePath(d);
            const QString bakDir = (rel == "." || rel.isEmpty())
                                 ? bakRoot : QDir(bakRoot).filePath(rel);
            const QString bak = QDir(bakDir).filePath("mega-core.x25");
            if (!QFile::exists(bak)) {
                QDir().mkpath(bakDir);
                if (QFile::copy(dst, bak)) ++saved;
            }
        }

        QFile::remove(dst);
        if (QFile::copy(src, dst)) ++ok; else ++fail;
    }

    if (saved > 0) {
        appendLog(QString(T("%1 vorhandene Datei(en) nach %2 gesichert."))
                  .arg(saved).arg(kBackupDir));
    }
    appendLog(QString(T("Mapper eingerichtet: %1 Ordner beschrieben, %2 fehlgeschlagen."))
              .arg(ok).arg(fail));
    QMessageBox::information(this, T("Mapper einrichten"),
        QString(T("mega-core.x25 in %1 Ordner kopiert.")).arg(ok)
        + (fail ? QString(T("\n%1 Ordner konnten nicht beschrieben werden.")).arg(fail)
                : QString())
        + (saved ? QString("\n\n") + T("Vorhandene Originale wurden gesichert und lassen "
                                        "sich in den Optionen wiederherstellen.")
                 : QString()));
}

// Gegenstueck zum Einrichten: holt die gesicherten Originale zurueck.
// Ohne diesen Weg waere ein Nutzer, der die Karte wieder ohne MEGA-RAW
// benutzen will, auf ein eigenes Backup angewiesen.
void MainWindow::onRestoreMappers() {
    // Unsere eigene Datei zum Vergleich - alles, was genauso gross ist,
    // stammt von uns und darf entfernt werden, wenn es kein Original gab.
    const QString srcRef = QDir(QCoreApplication::applicationDirPath())
                           .filePath("mega-core.x25");

    const QString root = QFileDialog::getExistingDirectory(this,
        T("SD-Karte waehlen - das Laufwerk selbst (mit dem Ordner MEGA), z.B. H:\\"), settings_.rom_root);
    if (root.isEmpty()) return;

    const QString bakRoot = QDir(root).filePath(kBackupDir);

    // Gesicherte Originale einsammeln - es muss keine geben. Auf einer
    // unberuehrten Karte liegt in den Spielordnern nichts, dort ist beim
    // Einrichten nichts ueberschrieben worden. Zurueckzusetzen ist dann
    // trotzdem etwas: unsere eigenen Kopien.
    QStringList gesichert;
    if (QDir(bakRoot).exists()) {
        QDirIterator bit(bakRoot, QStringList{"mega-core.x25"}, QDir::Files,
                         QDirIterator::Subdirectories);
        while (bit.hasNext()) gesichert << bit.next();
    }

    // Unsere eigenen Kopien einsammeln: gleiche Groesse wie die Datei neben
    // der Anwendung, ausserhalb des Sicherungsordners.
    const qint64 unsereGroesse = QFileInfo::exists(srcRef) ? QFileInfo(srcRef).size() : 0;
    QStringList eigene;
    if (unsereGroesse > 0) {
        QDirIterator uit(root, QStringList{"mega-core.x25"}, QDir::Files,
                         QDirIterator::Subdirectories);
        while (uit.hasNext()) {
            const QString f = uit.next();
            if (f.startsWith(bakRoot)) continue;
            if (QFileInfo(f).size() != unsereGroesse) continue;
            eigene << f;
        }
    }

    if (gesichert.isEmpty() && eigene.isEmpty()) {
        QMessageBox::information(this, T("Original-Mapper wiederherstellen"),
            QString(T("In %1 gibt es nichts zurueckzusetzen.")).arg(root));
        return;
    }

    QString frage;
    if (!gesichert.isEmpty())
        frage += QString(T("%1 gesicherte Originaldatei(en) zurueckschreiben.")).arg(gesichert.size()) + "\n";
    if (!eigene.isEmpty())
        frage += QString(T("%1 von MEGA-RAW angelegte Datei(en) entfernen.")).arg(eigene.size()) + "\n";
    frage += "\n" + T("Fortfahren?");

    if (QMessageBox::question(this, T("Original-Mapper wiederherstellen"), frage)
        != QMessageBox::Yes) return;

    // Erst unsere Kopien entfernen, dann Originale zurueckschreiben - so
    // ueberschreibt das Zurueckschreiben nicht versehentlich sich selbst.
    int entfernt = 0;
    for (const QString& f : eigene) {
        if (QFile::remove(f)) ++entfernt;
    }

    int ok = 0, fail = 0;
    for (const QString& b : gesichert) {
        const QString rel = QDir(bakRoot).relativeFilePath(b);
        const QString dst = QDir(root).filePath(rel);
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QFile::remove(dst);
        if (QFile::copy(b, dst)) ++ok; else ++fail;
    }

    appendLog(QString(T("Zuruecksetzen: %1 Original(e) zurueck, %2 eigene entfernt, %3 fehlgeschlagen."))
              .arg(ok).arg(entfernt).arg(fail));
    QMessageBox::information(this, T("Original-Mapper wiederherstellen"),
        QString(T("%1 Originaldatei(en) zurueckgeschrieben, %2 eigene entfernt."))
            .arg(ok).arg(entfernt)
        + (fail ? QString(T("\n%1 fehlgeschlagen.")).arg(fail) : QString()));
}

void MainWindow::onDetectGame() {
    if (monThread_) {
        appendLog(T("Erkennung nicht moeglich, solange der Monitor laeuft."));
        return;
    }
    detectGame();
}

// Erkennung ueber den MD5, den der Mapper selbst rechnet. Ueber USB gehen
// dabei 16 Byte statt zwei Megabyte - das ROM zu uebertragen dauerte 25
// Sekunden, so sind es unter fuenf. Es braucht weder Sammlungspfad noch
// Stichproben: die Konsole allein genuegt.
bool MainWindow::detectGame() {
    auto [edPtr, port] = find_everdrive(QString());
    if (!edPtr) { appendLog(T("Kein EverDrive gefunden.")); return false; }

    uint8_t build = 0;
    try {
        build = static_cast<uint8_t>(
            edPtr->memrd(mdsnoop::kAddrSnoop + mdsnoop::kCtlOffset, 1)[0]);
    } catch (...) {
        edPtr->close();
        appendLog(T("Mapper nicht lesbar."));
        return false;
    }
    if (build == 0 || build == 0xFF) {
        edPtr->close();
        appendLog(T("Der RA-Mapper laeuft nicht - mega-core.x25 muss neben dem ROM liegen."));
        return false;
    }

    // Kopfblock holen, daraus die ROM-Groesse (Headerfeld bei 0x1A4).
    mdrom::Stats st;
    QByteArray head = mdrom::readHeaderSafe(*edPtr, mdsnoop::kAddrSnoop);
    uint32_t size = mdrom::romSizeFromHeader(head);
    if (size > 0) {
        // Header-Groesse gegen die tatsaechlichen Daten pruefen. Manche ROMs
        // melden zu wenig (Zero Wing Europe: 512 KB gemeldet, 1 MB gross).
        const uint32_t echt = mdrom::romSizeProbe(*edPtr, mdsnoop::kAddrSnoop, size);
        if (echt > size) {
            appendLog(QString(T("Header meldet %1 KB, tatsaechlich %2 KB - korrigiert."))
                      .arg(size / 1024).arg(echt / 1024));
            size = echt;
        }
    }
    if (size == 0) {
        edPtr->close();
        appendLog(T("ROM-Header nicht lesbar - laeuft ein Spiel?"));
        return false;
    }
    appendLog(QString(T("ROM %1 KB - Hash wird im Mapper gerechnet...")).arg(size / 1024));
    QCoreApplication::processEvents();

    QByteArray dig = mdrom::romMd5(*edPtr, size, mdsnoop::kAddrSnoop);
    edPtr->close();

    if (dig.size() != 16) {
        appendLog(T("Hash nicht fertig geworden."));
        return false;
    }

    const QString md5 = dig.toHex();
    appendLog("MD5: " + md5);

    romPath_.clear();
    romPathEdit_->setText(T("(von der Konsole erkannt)"));
    game_.reset();
    setHasSet_ = false;
    tryLoadGame(md5.toStdString());
    return setHasSet_;
}
void MainWindow::onChooseRom() {
    QString path = QFileDialog::getOpenFileName(this, "ROM waehlen", QString(),
        "Mega Drive ROMs / Archive (*.md *.bin *.gen *.smd *.zip *.rar);;Alle Dateien (*)");
    if (path.isEmpty()) return;
    loadRom(path);
}

void MainWindow::loadRom(QString path) {
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "zip" || ext == "rar") {
        auto ex = extract_archive_rom(path);   // neben Archiv entpacken
        if (!ex) { appendLog(T("Keine MD-ROM im Archiv gefunden (oder RAR ohne WinRAR).")); return; }
        appendLog(QString(T("Aus Archiv entpackt: %1")).arg(QFileInfo(*ex).fileName()));
        path = *ex;
    }

    romPath_ = path;
    romPathEdit_->setText(path);
    game_.reset();
    setHasSet_ = false;
    updateMonitorState();

    if (!QFile::exists(path)) {
        statusLabel_->setText(T("Fehler: Datei existiert nicht."));
        appendLog(T("Fehler: Datei nicht gefunden: ") + path);
        return;
    }
    std::string md5 = md5_file(path.toStdString());
    statusLabel_->setText(QString(T("MD5: %1 - suche Achievement-Set...")).arg(QString::fromStdString(md5)));
    appendLog(T("ROM geladen: ") + path);
    appendLog("MD5: " + QString::fromStdString(md5));

    tryLoadGame(md5);
}

void MainWindow::tryLoadGame(const std::string& md5) {
    try {
        auto gid = client_->ra_gameid(md5, true);
        if (!gid) {
            statusLabel_->setText(T("Kein RetroAchievements-Set fuer diese ROM gefunden."));
            appendLog(T("RA kennt diese ROM nicht (MD5 unbekannt)."));
            return;
        }
        appendLog(QString(T("RA GameID: %1")).arg(*gid));

        if (token_.isEmpty()) {
            statusLabel_->setText(T("GameID gefunden, aber kein Login -> kein Patch-Data-Abruf."));
            appendLog(T("Bitte zuerst einloggen, um das Achievement-Set zu laden."));
            return;
        }

        auto patch = client_->ra_patch(*gid, userEdit_->text().toStdString(), token_.toStdString());
        Game g = build_game_from_patch(*gid, md5, patch);
        if (!g.valid) {
            statusLabel_->setText(T("Kein Patch-Data von RA erhalten."));
            appendLog(T("ra_patch lieferte kein gueltiges PatchData."));
            return;
        }
        if (g.no_set) {
            statusLabel_->setText(QString::fromStdString(g.name) + T(" - keine unterstuetzte ROM-Version."));
            appendLog(T("Unsupported game version laut RA."));
            return;
        }

        // Bereits freigeschaltete Achievements golden markieren
        try {
            auto owned = client_->ra_unlocks(*gid, userEdit_->text().toStdString(), token_.toStdString(), settings_.hardcore);
            int nOwned = 0;
            for (auto& a : g.achievements) {
                if (owned.count(a.id)) { a.owned = true; a.triggered = true; nOwned++; }
            }
            if (nOwned) appendLog(QString(T("%1 Achievement(s) bereits freigeschaltet.")).arg(nOwned));
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
            info += QString("[%1]\n").arg(compat ? T("KOMPATIBEL") : T("NICHT KOMPATIBEL"));
            info += QString(T("Achievements gesamt: %1\n")).arg(n);
            info += QString(T("Von der Engine lesbar: %1/%2")).arg(n - uns).arg(n);
            statusLabel_->setText(info);
            statusLabel_->setStyleSheet(compat
                ? "color: #00ff00; font-family: Courier; font-size: 9pt;"
                : "color: #ff4444; font-family: Courier; font-size: 9pt;");
        }
        appendLog(QString(T("Set geladen: %1 Core-Achievements, %2 RAM-Bytes zu spiegeln"))
            .arg(g.raw_core).arg(g.addr_list.size()));
        setHasSet_ = !g.addr_map.empty();
        updateMonitorState();
        rebuildAcList();
    } catch (const RateLimited& e) {
        statusLabel_->setText(QString(T("RA drosselt, bitte %1s warten.")).arg(e.retry_after));
    } catch (const std::exception& e) {
        statusLabel_->setText(QString(T("Netzwerkfehler: %1")).arg(e.what()));
    }
}

void MainWindow::onOptions() {
    QDialog dlg(this);
    dlg.setWindowTitle(T("Optionen"));
    auto* v = new QVBoxLayout(&dlg);

    auto* langRow = new QHBoxLayout();
    langRow->addWidget(new QLabel(T("Sprache / Language:"), &dlg));
    auto* lDe = new QRadioButton("Deutsch", &dlg);
    auto* lEn = new QRadioButton("English", &dlg);
    (settings_.language == "en" ? lEn : lDe)->setChecked(true);
    langRow->addWidget(lDe); langRow->addWidget(lEn);
    v->addLayout(langRow);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    btns->setCenterButtons(true);
    v->addWidget(btns);

    auto* bugBtn = new QPushButton(T("Fehler melden"), &dlg);
    bugBtn->setToolTip(T("Oeffnet die Fehlerliste auf GitHub im Browser."));
    connect(bugBtn, &QPushButton::clicked, &dlg, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/liquid-wq/mega-raw/issues"));
    });
    v->addWidget(bugBtn);

    auto* restBtn = new QPushButton(T("Original-Mapper wiederherstellen"), &dlg);
    restBtn->setToolTip(T("Schreibt die beim Einrichten gesicherten Originaldateien "
                          "auf die SD-Karte zurueck."));
    connect(restBtn, &QPushButton::clicked, &dlg, [this, &dlg]() {
        dlg.accept();
        onRestoreMappers();
    });
    v->addWidget(restBtn);
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

        // Den Neustart-Hinweis nur zeigen, wenn die Sprache wirklich
        // gewechselt wurde. Vorher erschien er bei jedem Speichern, auch
        // wenn nur der Hardcore-Haken geaendert wurde.
        const QString neueSprache = lEn->isChecked() ? "en" : "de";
        const bool gewechselt = (neueSprache != settings_.language);
        settings_.language = neueSprache;
        g_lang = settings_.language;
        if (gewechselt) {
            QMessageBox::information(this, T("Hinweis"),
                g_lang=="en" ? "Language set. Restart the tool to apply all texts."
                             : "Sprache gesetzt. Tool neu starten, damit alle Texte umgestellt sind.");
        }
        settings_.save(settingsPath_);
        appendLog(T("Einstellungen gespeichert."));
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
        // Spielzustand verwerfen. Der Start weiter unten erkennt nur, wenn
        // setHasSet_ false ist - ohne das Zuruecksetzen wird beim naechsten
        // Start das ALTE Spiel weiterbenutzt, auch wenn inzwischen ein
        // anderes in der Konsole steckt (Anzeige und Blockzuordnung bleiben
        // stehen, der Mapper spiegelt fremde Adressen).
        game_.reset();
        setHasSet_ = false;
        statusLabel_->setText(T("Kein Spiel geladen."));
        statusLabel_->setStyleSheet("color: #cccccc; font-family: Courier; font-size: 9pt;");
        updateMonitorState();
        appendLog(T("Monitor gestoppt."));
        return;
    }
    if (token_.isEmpty()) { appendLog(T("Bitte zuerst einloggen.")); return; }

    // Kein Spiel gewaehlt: aus dem Mapper erkennen. Der Nutzer soll nicht
    // wissen muessen, welche Datei gerade in der Konsole steckt.
    if (!setHasSet_) {
        appendLog(T("Erkenne laufendes Spiel..."));
        if (!detectGame()) {
            appendLog(T("Erkennung fehlgeschlagen - bitte ROM von Hand waehlen."));
            return;
        }
    }

    QString port;  // leer -> Autoerkennung scannt alle Ports

    monThread_ = new QThread(this);
    Game gm = game_ ? *game_ : Game{};
    monWorker_ = new MonitorWorker(port, gm, userEdit_->text(), token_, client_, 0, settings_.hardcore);
    monWorker_->moveToThread(monThread_);

    connect(monThread_, &QThread::started, monWorker_, &MonitorWorker::start);
    connect(monWorker_, &MonitorWorker::log, this, &MainWindow::onWorkerLog);
    connect(monWorker_, &MonitorWorker::bramDump, this, &MainWindow::onWorkerRam);
    connect(monWorker_, &MonitorWorker::romIdentified, this, &MainWindow::onWorkerRomIdentified);
    connect(monWorker_, &MonitorWorker::palState, this, [this](bool pal){
        consolePal_ = pal ? 1 : 0; rebuildAcList();
    }, Qt::QueuedConnection);
    connect(monWorker_, &MonitorWorker::gameDetected, this, [this](int gid){
        if (game_ && game_->gameid == gid) return;
        try {
            auto patch = client_->ra_patch(gid, userEdit_->text().toStdString(), token_.toStdString());
            Game g = build_game_from_patch(gid, "", patch);
            if (!g.valid) {
                appendLog(QString(T("Auto-Laden: kein gueltiges Patch-Data fuer RA-ID %1.")).arg(gid));
                return;
            }
            if (g.addr_map.empty()) {
                appendLog(QString(T("Auto-Laden: RA-ID %1 hat %2 Achievement(s), aber keine davon hat eine von der Engine lesbare Adresse.")).arg(gid).arg(g.raw_core));
                return;
            }
            {
                try {
                    auto owned = client_->ra_unlocks(gid, userEdit_->text().toStdString(), token_.toStdString(), settings_.hardcore);
                    int nOwned = 0;
                    for (auto& a : g.achievements)
                        if (owned.count(a.id)) { a.owned = true; a.triggered = true; nOwned++; }
                    if (nOwned) appendLog(QString(T("%1 Achievement(s) bereits freigeschaltet.")).arg(nOwned));
                } catch (...) {}
                game_ = g;
                setHasSet_ = true;
                // Worker direkt updaten
                if (monWorker_) monWorker_->updateGame(g);
                int n = static_cast<int>(g.raw_core);
                int uns = static_cast<int>(g.n_unsupported);
                QString info = QString::fromUtf8("\u2713 ") + QString::fromStdString(g.name) + "\n";
                info += "[" + T("KOMPATIBEL") + "]\n";
                info += QString(T("Achievements gesamt: %1\n")).arg(n);
                info += QString(T("Von der Engine lesbar: %1/%2")).arg(n - uns).arg(n);
                statusLabel_->setText(info);
                statusLabel_->setStyleSheet("color: #00ff00; font-family: Courier; font-size: 9pt;");
                appendLog(QString(T("Set automatisch geladen: %1")).arg(QString::fromStdString(g.name)));
                rebuildAcList();
            }
        } catch (const std::exception& e) {
            appendLog(QString(T("Auto-Laden fehlgeschlagen: ")) + e.what());
        }
    }, Qt::QueuedConnection);
    connect(monWorker_, &MonitorWorker::unlocked, this, &MainWindow::onWorkerUnlocked);
    connect(monWorker_, &MonitorWorker::connectionLost, this, &MainWindow::onWorkerConnectionLost);
    connect(monWorker_, &MonitorWorker::coreMissing, this, &MainWindow::onWorkerCoreMissing);
    connect(monWorker_, &MonitorWorker::finished, this, &MainWindow::onWorkerFinished);
    connect(monWorker_, &MonitorWorker::finished, monWorker_, &QObject::deleteLater);

    monThread_->start();
    monitoring_ = true;
    monitorBtn_->setText(T("Monitor stoppen"));
    monitorHint_->setText(QString::fromUtf8("\u2713 ") + T("Verbunden ") + port);
    monitorHint_->setStyleSheet("color: #00ff00;");
    appendLog(T("Verbinde mit ") + port + " ...");
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
void MainWindow::onWorkerRomIdentified(const QString& fingerprint, const QString& title) {
    // Der Header kommt direkt aus dem Cartridge-PSRAM und sagt, welche ROM
    // die Konsole tatsaechlich faehrt. Die Zuordnung zum RA-Set laeuft
    // weiterhin ueber die gewaehlte Datei.
    appendLog(QString(T("Konsole faehrt: %1  [%2]")).arg(title).arg(fingerprint));
}

void MainWindow::onWorkerRam(const QByteArray& rawBytes) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastByteChangeMs_.size() < rawBytes.size())
        lastByteChangeMs_.resize(rawBytes.size(), 0);

    QString html = "<div style=\"font-family:'Consolas','Courier New',monospace; font-size:11pt; "
                   "letter-spacing:3px;\">";
    for (int i = 0; i < rawBytes.size(); ++i) {
        uint8_t b = static_cast<uint8_t>(rawBytes[i]);
        QString hex = QString("%1").arg(b, 2, 16, QChar('0')).toUpper();
        bool changed = (i >= lastRamBytes_.size()) ||
                       (static_cast<uint8_t>(lastRamBytes_[i]) != b);
        if (changed && !lastRamBytes_.isEmpty()) lastByteChangeMs_[i] = now;

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
    lastRamBytes_ = rawBytes;
    ramLabel_->setTextFormat(Qt::RichText);
    ramLabel_->setText(html);
}

void MainWindow::onWorkerUnlocked(const QString& title, int points, qlonglong achId) {
    appendLog(QString(T("*** FREIGESCHALTET: %1 (%2 Punkte, ID %3) ***")).arg(title).arg(points).arg(achId));
    playUnlockSound();
    QString desc;
    QString badgeName;
    if (game_) {
        for (auto& a : game_->achievements)
            if (a.id == achId) {
                a.triggered = true;
                desc = QString::fromStdString(a.desc);
                badgeName = QString::fromStdString(a.badge);
                break;
            }
        rebuildAcList();
    }

    // Freischalt-Popup unten rechts. Wird beim ersten Mal erzeugt und danach
    // wiederverwendet; zeige() setzt die Animation jedes Mal neu auf.
    if (!achPopup_) achPopup_ = new AchievementPopup();
    QPixmap badge;
    if (!badgeName.isEmpty()) badge = badges_.get(badgeName, 128);
    achPopup_->zeige(title, desc, points, badge, g_lang == "en");

    // Diagnose: meldet, ob das Fenster wirklich erzeugt, positioniert und
    // sichtbar ist. Ein Achievement wurde am 23.08. gebucht, ohne dass das
    // Popup zu sehen war - ohne diese Werte laesst sich nicht sagen, ob es
    // gar nicht kam, hinter etwas liegt oder ausserhalb des Bildschirms sitzt.
    {
        const QRect g = achPopup_->geometry();
        appendLog(QString("Popup: sichtbar=%1 pos=%2,%3 groesse=%4x%5 badge=%6")
                  .arg(achPopup_->isVisible() ? 1 : 0)
                  .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height())
                  .arg(badge.isNull() ? "leer" : "geladen"));
    }
}

void MainWindow::onWorkerCoreMissing() {
    ramLabel_->setTextFormat(Qt::PlainText);
    ramLabel_->setText(T("RAM: [RA-Mapper nicht geladen]"));
    ramLabel_->setStyleSheet(
        "background-color: #1a0a0a; border: 1px solid #6a2a2a; "
        "border-radius: 4px; padding: 6px 10px; color: #ff4444; font-weight: bold;");
    QMessageBox::warning(this, T("RA-Mapper nicht geladen"),
        T("Auf der Konsole laeuft nicht unser Custom-Mapper, sondern der "
          "Standard-Core. Ohne ihn gibt es keinen Speicherspiegel und keine "
          "Achievements.\n\n"
          "Das Spiel so starten:\n\n"
          "ueber USB:\n"
          "  edlink run --file SPIEL.md --fpga mega-core.x25\n\n"
          "ueber SD-Karte:\n"
          "  mega-core.x25 in denselben Ordner wie das ROM legen"));
}

void MainWindow::onWorkerConnectionLost(const QString& reason) {
    ramLabel_->setTextFormat(Qt::PlainText);
    ramLabel_->setText(T("RAM: [Verbindung verloren - ") + reason + "]");
    ramLabel_->setStyleSheet(
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
        ramLabel_->setTextFormat(Qt::PlainText);
        ramLabel_->setText(T("RAM: -"));
        ramLabel_->setStyleSheet(
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
