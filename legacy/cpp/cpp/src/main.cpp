#include <QApplication>
#include <cstdio>
#include <QTime>
#include <fstream>
#include <QIcon>
#include <QDir>
#include <QPropertyAnimation>
#include "mainwindow.h"
#include "intro_window.h"

int main(int argc, char** argv) {
    // Wie report_callback_exception/_thread_excepthook: Fehler sichtbar in error_log.txt.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg){
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
            std::ofstream f("error_log.txt", std::ios::app);
            if (f) f << QTime::currentTime().toString("HH:mm:ss").toStdString()
                     << "  " << msg.toStdString() << "\n";
        }
        fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    });
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/app_icon"));

    QString introPath = QDir(QApplication::applicationDirPath()).filePath("mega_raw_intro.html");
    bool hadIntro = IntroWindow::showBlocking(introPath);

    MainWindow w;
    if (hadIntro) {
        // Hauptfenster sanft aus dem Nichts einblenden (nahtlos nach Intro-Fade).
        w.setWindowOpacity(0.0);
        w.show();
        auto* in = new QPropertyAnimation(&w, "windowOpacity");
        in->setDuration(450);
        in->setStartValue(0.0);
        in->setEndValue(1.0);
        in->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        w.show();
    }
    return app.exec();
}
