#pragma once
#include <QSerialPort>
#include <QString>
#include <QByteArray>
#include <QMutex>
#include <memory>
#include <optional>
#include <utility>

// Struktur/Timing bewusst identisch zur Python-Vorlage gehalten, damit das
// Verhalten (Handshake, Recovery, Timeouts) vergleichbar bleibt.
// WICHTIG: blockiert den aufrufenden Thread (wie pyserial synchron) --
// in der GUI daher aus einem Worker-Thread aufrufen, nie aus dem UI-Thread.
class EdSerial {
public:
    explicit EdSerial(QString port = "COM5");

    // Wirft std::runtime_error bei Fehlschlag.
    bool open();
    void close();

    QByteArray memrd(uint32_t addr, uint32_t length);
    void memwr(uint32_t addr, const QByteArray& data);

    bool waitBramFrame(int timeoutMs = 200);
    QByteArray readBram(int length = 200);

    // goldenes Aufpulsen ueber MBX-Register.
    void tvFlash();

    QString portName() const { return portname_; }

private:
    QString portname_;
    std::unique_ptr<QSerialPort> ser_;
    QMutex lock_;

    QByteArray memrdRaw(uint32_t addr, uint32_t length);
    void memwrRaw(uint32_t addr, const QByteArray& data);
    bool recover();

    // Liest bis zu maxLen Bytes, bis Timeout abgelaufen ist (wie pyserial
    // read(n) mit gesetztem .timeout -- liefert ggf. weniger als maxLen).
    QByteArray readUpTo(int maxLen, int timeoutMs);
};

// anderen verfuegbaren. Liefert (EdSerial, Portname) oder (nullptr, "").
std::pair<std::unique_ptr<EdSerial>, QString> find_everdrive(const QString& preferred = QString());
