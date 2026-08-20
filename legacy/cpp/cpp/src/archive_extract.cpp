#include "archive_extract.h"
#include "miniz/miniz.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QProcess>

namespace {
const QStringList ROM_EXTS = {"md","bin","gen","smd"};
bool isRomName(const QString& n) {
    return ROM_EXTS.contains(QFileInfo(n).suffix().toLower());
}
QString unrarPath() {
    for (const char* p : { "C:/Program Files/WinRAR/UnRAR.exe",
                           "C:/Program Files (x86)/WinRAR/UnRAR.exe" })
        if (QFile::exists(p)) return p;
    return QString();
}
}

std::optional<QString> extract_archive_rom(const QString& arc, const QString& destDir) {
    const QString ext = QFileInfo(arc).suffix().toLower();
    const QString dest = destDir.isEmpty() ? QFileInfo(arc).absolutePath() : destDir;

    if (ext != "zip" && ext != "rar")
        return arc; // ist schon eine ROM

    if (ext == "zip") {
        mz_zip_archive za; memset(&za, 0, sizeof(za));
        if (!mz_zip_reader_init_file(&za, arc.toLocal8Bit().constData(), 0))
            return std::nullopt;
        int found = -1; QString name;
        const int n = int(mz_zip_reader_get_num_files(&za));
        for (int i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&za, i, &st)) continue;
            QString fn = QString::fromUtf8(st.m_filename);
            if (isRomName(fn)) { found = i; name = fn; break; }
        }
        if (found < 0) { mz_zip_reader_end(&za); return std::nullopt; }
        const QString out = QDir(dest).filePath(QFileInfo(name).fileName());
        bool ok = mz_zip_reader_extract_to_file(&za, found, out.toLocal8Bit().constData(), 0);
        mz_zip_reader_end(&za);
        return ok ? std::optional<QString>(out) : std::nullopt;
    }

    // RAR:
    const QString tool = unrarPath();
    if (tool.isEmpty()) return std::nullopt;
    QProcess lp;
    lp.start(tool, {"lb", arc});                 // Namen listen
    if (!lp.waitForFinished(30000)) return std::nullopt;
    QString name;
    for (const QString& l : QString::fromLocal8Bit(lp.readAllStandardOutput()).split('\n')) {
        QString t = l.trimmed();
        if (!t.isEmpty() && isRomName(t)) { name = t; break; }
    }
    if (name.isEmpty()) return std::nullopt;
    QProcess ep;                                  // "e" = ohne Pfade nach dest
    ep.start(tool, {"e", "-inul", "-o+", arc, name, QDir::toNativeSeparators(dest) + "\\"});
    if (!ep.waitForFinished(120000) || ep.exitCode() != 0) return std::nullopt;
    const QString out = QDir(dest).filePath(QFileInfo(name).fileName());
    return QFile::exists(out) ? std::optional<QString>(out) : std::nullopt;
}
