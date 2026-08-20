#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include "ra_engine.h"
#include "ra_client.h"

// SD-Export als QThread-Worker. Portiert die Kernlogik von _sd_export_t():
// ROMs rekursiv finden -> ins Staging kopieren -> optional patchen (mit
// Throttle-Logik) -> optional in Buchstaben-Ordner (<=800/Ordner) sortieren.
// Archiv-Entpacken (.zip/.rar) bewusst NICHT enthalten (braucht externe Lib);
// solche Dateien werden uebersprungen und gezaehlt.
class SdExportWorker : public QObject {
    Q_OBJECT
public:
    SdExportWorker(QString src, QString dst, bool doIps, bool doBucket,
                   QString user, QString token, std::shared_ptr<RaClient> client,
                   bool delArc = false);

public slots:
    void start();
    void stop();

signals:
    void progress(const QString& line);
    void progressPercent(int current, int total);
    void finishedSummary(const QString& summary);
    void finishedDetail(const QString& detail); // Aufschlüsselung

private:
    QString src_, dst_, user_, token_;
    bool doIps_, doBucket_, delArc_ = false;
    std::shared_ptr<RaClient> client_;
    std::atomic<bool> running_{true};
};
