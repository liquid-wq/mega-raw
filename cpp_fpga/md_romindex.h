#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>

// Spielerkennung ueber den ROM-Mitschnitt des Mappers.
//
// Der Mapper schneidet mit, was der 68000 aus $000000-$0001FF liest -
// Vektortabelle und ROM-Header. Gelesen wird nur, was das Spiel auch
// wirklich anfasst; welche Woerter gueltig sind, sagt die Gueltigkeitstabelle.
// Der PC vergleicht diese Woerter gegen den Anfang jeder ROM-Datei.
//
// Ueber USB darf das ROM NICHT gelesen werden: Konsole und PI teilen sich
// das PSRAM, ein Zugriff bei laufendem Spiel stuerzt den 68000 ab.

namespace mdromid {

constexpr uint32_t kVecOffset = 0x5000;   // 512 Byte, Big-Endian
constexpr uint32_t kVldOffset = 0x6000;   // 256 Byte, ein Bit je Wort
constexpr int kWordCount = 256;

struct Snapshot {
    QVector<uint16_t> word;   // kWordCount Eintraege
    QVector<bool>     valid;
    int validCount() const;
};

Snapshot parseSnapshot(const QByteArray& vec, const QByteArray& vld);

// Kandidaten aus einem Verzeichnisbaum. Vergleicht nur die gueltigen Woerter.
QStringList matchRoms(const QString& root, const Snapshot& snap, int maxHits = 32);

} // namespace mdromid
