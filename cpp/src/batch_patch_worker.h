#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include "ra_client.h"

// ORDNER PATCHEN als QThread-Worker. Portiert _batch_patch_t():
// ROMs im Ordner (nicht rekursiv) in-place patchen (.ips daneben),
// bereits gepatchte ueberspringen, Reports schreiben (batch_log.txt,
// verify_report.txt, patched_games.txt, boot_risk.txt neben der exe).
// Abweichung zum Python-Original: .zip/.rar werden NICHT entpackt
// (kein Entpacker eingebettet) sondern gezaehlt uebersprungen --
// identisch zur Behandlung im SD-Export.
class BatchPatchWorker : public QObject {
    Q_OBJECT
public:
    BatchPatchWorker(QString folder, QString user, QString token,
                     bool avSafe, std::shared_ptr<RaClient> client);

public slots:
    void start();
    void stop();

signals:
    void progress(const QString& line);
    void progressPercent(int current, int total);
    void finishedSummary(const QString& summary);

private:
    QString folder_, user_, token_;
    bool avSafe_;
    std::shared_ptr<RaClient> client_;
    std::atomic<bool> running_{true};
};
