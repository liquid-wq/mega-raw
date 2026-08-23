#include "md_romindex.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace mdromid {

int Snapshot::validCount() const {
    int n = 0;
    for (bool b : valid) if (b) ++n;
    return n;
}

Snapshot parseSnapshot(const QByteArray& vec, const QByteArray& vld) {
    Snapshot s;
    s.word.resize(kWordCount);
    s.valid.resize(kWordCount);
    for (int i = 0; i < kWordCount; ++i) {
        const bool ok = (i < vld.size()) && (static_cast<uint8_t>(vld[i]) & 1);
        s.valid[i] = ok;
        if (ok && 2*i + 1 < vec.size())
            s.word[i] = static_cast<uint16_t>(
                (static_cast<uint8_t>(vec[2*i]) << 8) | static_cast<uint8_t>(vec[2*i + 1]));
        else
            s.word[i] = 0;
    }
    return s;
}

QStringList matchRoms(const QString& root, const Snapshot& snap, int maxHits) {
    QStringList hits;
    if (snap.validCount() == 0) return hits;

    static const QStringList ext = {"*.md", "*.bin", "*.gen", "*.smd", "*.68k", "*.sgd"};

    QDirIterator it(root, ext, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && hits.size() < maxHits) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray head = f.read(2 * kWordCount);
        f.close();
        if (head.size() < 8) continue;

        bool ok = true;
        for (int i = 0; i < kWordCount && ok; ++i) {
            if (!snap.valid[i]) continue;
            if (2*i + 1 >= head.size()) { ok = false; break; }
            const uint16_t w = static_cast<uint16_t>(
                (static_cast<uint8_t>(head[2*i]) << 8) | static_cast<uint8_t>(head[2*i + 1]));
            if (w != snap.word[i]) ok = false;
        }
        if (ok) hits << path;
    }
    return hits;
}

} // namespace mdromid
