#include "badge_loader.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>

QPixmap BadgeLoader::get(const QString& badge) {
    if (badge.isEmpty()) return QPixmap();
    if (mem_.contains(badge)) return mem_.value(badge);

    QString cacheDir = QDir(QCoreApplication::applicationDirPath()).filePath("badge_cache");
    QDir().mkpath(cacheDir);
    QString local = QDir(cacheDir).filePath(badge + ".png");

    if (!QFile::exists(local)) {
        QNetworkAccessManager nam;
        QNetworkRequest req(QUrl("https://media.retroachievements.org/Badge/" + badge + ".png"));
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (MEGA-RAW Achievement Badge Loader)");
        QNetworkReply* rep = nam.get(req);
        QEventLoop loop;
        QTimer::singleShot(8000, &loop, &QEventLoop::quit);    // timeout=8
        QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (rep->error() == QNetworkReply::NoError) {
            QByteArray data = rep->readAll();
            QFile f(local);
            if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); }
        }
        rep->deleteLater();
    }

    QPixmap pm;
    if (pm.load(local)) {
        if (pm.width() > 32)
            pm = pm.scaledToWidth(32, Qt::SmoothTransformation);
        mem_.insert(badge, pm);
        return pm;
    }
    return QPixmap();
}
