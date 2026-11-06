#include "md_romid.h"

namespace mdrom {

static QString field(const QByteArray& raw, int off, int len) {
    if (raw.size() < off + len) return QString();
    return QString::fromLatin1(raw.mid(off, len)).trimmed();
}

QString Header::fingerprint() const {
    return QString("%1|%2").arg(serial).arg(checksum, 4, 16, QChar('0')).toUpper();
}

Header parseHeader(const QByteArray& raw) {
    Header h;
    if (raw.size() < 0xF0) return h;

    h.system             = field(raw, 0x00, 16);
    h.copyright          = field(raw, 0x10, 16);
    h.titleDomestic      = field(raw, 0x20, 48);
    h.titleInternational = field(raw, 0x50, 48);
    h.serial             = field(raw, 0x80, 14);
    h.checksum           = static_cast<uint16_t>(
                             (static_cast<uint8_t>(raw[0x8E]) << 8) |
                              static_cast<uint8_t>(raw[0x8F]));
    h.region             = field(raw, 0xF0, 16);

    // Ein gueltiger Header traegt die Systemkennung an erster Stelle.
    h.valid = h.system.startsWith("SEGA") || h.system.contains("MEGA DRIVE")
              || h.system.contains("GENESIS");
    return h;
}

Header readHeader(EdSerial& ed, uint32_t base) {
    try {
        QByteArray raw = ed.memrd(base + kHeaderStart, kHeaderLen);
        return parseHeader(raw);
    } catch (...) {
        return Header();
    }
}

} // namespace mdrom
