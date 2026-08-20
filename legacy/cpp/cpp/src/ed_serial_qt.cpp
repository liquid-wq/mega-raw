#include "ed_serial_qt.h"
#include "ed_protocol.h"
#include <QSerialPortInfo>
#include <QElapsedTimer>
#include <QThread>
#include <QMutexLocker>
#include <stdexcept>

EdSerial::EdSerial(QString port) : portname_(std::move(port)) {}

QByteArray EdSerial::readUpTo(int maxLen, int timeoutMs) {
    QByteArray out;
    QElapsedTimer t; t.start();
    while (out.size() < maxLen) {
        int remaining = timeoutMs - static_cast<int>(t.elapsed());
        if (remaining <= 0) break;
        if (!ser_->waitForReadyRead(remaining)) break;
        out += ser_->readAll();
    }
    return out;
}

bool EdSerial::open() {
    ser_ = std::make_unique<QSerialPort>();
    ser_->setPortName(portname_);
    ser_->setBaudRate(921600);
    if (!ser_->open(QIODevice::ReadWrite)) {
        throw std::runtime_error(("Port " + portname_ + " konnte nicht geoeffnet werden: "
                                   + ser_->errorString()).toStdString());
    }

    // Wie edlink: 66 Null-Bytes senden, Eingang leeren
    ser_->write(QByteArray(66, '\0'));
    ser_->waitForBytesWritten(500);
    ser_->flush();

    // Eventuellen Reststrom aktiv leersaugen bis 0.3s Funkstille
    double quietMs = 0.0;
    QElapsedTimer t0; t0.start();
    while (quietMs < 300.0 && t0.elapsed() < 3000) {
        QByteArray chunk = readUpTo(4096, 50);
        if (!chunk.isEmpty()) quietMs = 0.0;
        else quietMs += 50.0;
    }
    ser_->clear(QSerialPort::Input);

    // Status-Handshake
    auto cmdVec = ed_cmd(CMD_STATUS);
    ser_->write(QByteArray::fromRawData(
        reinterpret_cast<const char*>(cmdVec.data()), static_cast<int>(cmdVec.size())));
    ser_->waitForBytesWritten(500);
    ser_->flush();
    QThread::msleep(50);
    QByteArray resp = readUpTo(4, 500);

    bool has5A = resp.contains(static_cast<char>(0x5A));
    bool hasA5 = resp.contains(static_cast<char>(0xA5));
    if (resp.isEmpty() || (!has5A && !hasA5)) {
        QString hex = resp.isEmpty() ? "leer" : QString(resp.toHex());
        close();
        throw std::runtime_error(("EverDrive antwortet nicht (Status: " + hex).toStdString());
    }
    return true;
}

void EdSerial::close() {
    if (ser_) {
        if (ser_->isOpen()) ser_->close();
        ser_.reset();
    }
}

bool EdSerial::recover() {
    close();
    QThread::msleep(800);
    try {
        open();
        return true;
    } catch (...) {}

    for (const auto& info : QSerialPortInfo::availablePorts()) {
        try {
            portname_ = info.portName();
            open();
            return true;
        } catch (...) {
            close();
        }
    }
    return false;
}

QByteArray EdSerial::memrd(uint32_t addr, uint32_t length) {
    try {
        return memrdRaw(addr, length);
    } catch (...) {
        if (recover()) return memrdRaw(addr, length);
        throw;
    }
}

void EdSerial::memwr(uint32_t addr, const QByteArray& data) {
    try {
        memwrRaw(addr, data);
    } catch (...) {
        if (recover()) { memwrRaw(addr, data); return; }
        throw;
    }
}

QByteArray EdSerial::memrdRaw(uint32_t addr, uint32_t length) {
    QMutexLocker lock(&lock_);
    ser_->clear(QSerialPort::Input);
    auto pkt = ed_build_memrd_packet(addr, length);
    ser_->write(QByteArray::fromRawData(reinterpret_cast<const char*>(pkt.data()),
                                         static_cast<int>(pkt.size())));
    ser_->flush();
    QByteArray data = readUpTo(static_cast<int>(length), 500);
    if (static_cast<uint32_t>(data.size()) != length) {
        throw std::runtime_error("memrd: " + std::to_string(data.size()) + "/" +
                                  std::to_string(length) + " Bytes erhalten");
    }
    return data;
}

void EdSerial::memwrRaw(uint32_t addr, const QByteArray& data) {
    QMutexLocker lock(&lock_);
    std::vector<uint8_t> dvec(data.begin(), data.end());
    auto pkt = ed_build_memwr_packet(addr, dvec);
    ser_->write(QByteArray::fromRawData(reinterpret_cast<const char*>(pkt.data()),
                                         static_cast<int>(pkt.size())));
    ser_->flush();
}

bool EdSerial::waitBramFrame(int timeoutMs) {
    memwr(ADDR_BRAM + 198, QByteArray("\x00\xAA", 2));
    QElapsedTimer t; t.start();
    while (t.elapsed() < timeoutMs) {
        QByteArray v = memrd(ADDR_BRAM + 198, 2);
        if (v.size() == 2 && static_cast<uint8_t>(v[1]) == 0x00) return true;
    }
    return false;
}

QByteArray EdSerial::readBram(int length) {
    return memrd(ADDR_BRAM, static_cast<uint32_t>(length));
}

void EdSerial::tvFlash() {
    static const int up[]   = {0x002, 0x024, 0x048, 0x06A, 0x08C, 0x0AE};
    static const int down[] = {0x0AE, 0x08C, 0x06A, 0x048, 0x024, 0x002, 0x000};
    std::vector<int> seq(up, up + 6);
    for (int i = 0; i < 10; ++i) seq.push_back(0x0AE);
    seq.insert(seq.end(), down, down + 7);
    for (int col : seq) {
        uint16_t v = 0xA000 | static_cast<uint16_t>(col);
        QByteArray b(2, 0);
        b[0] = static_cast<char>((v >> 8) & 0xFF);
        b[1] = static_cast<char>(v & 0xFF);
        memwr(ADDR_MBX, b);
        QThread::msleep(45);
    }
    memwr(ADDR_MBX, QByteArray(2, '\0'));
}

std::pair<std::unique_ptr<EdSerial>, QString> find_everdrive(const QString& preferred) {
    QStringList candidates;
    if (!preferred.isEmpty()) candidates << preferred;
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        if (!candidates.contains(info.portName())) candidates << info.portName();
    }
    for (const auto& port : candidates) {
        auto ed = std::make_unique<EdSerial>(port);
        try {
            ed->open();
            return {std::move(ed), port};
        } catch (...) {
            ed->close();
        }
    }
    return {nullptr, QString()};
}
