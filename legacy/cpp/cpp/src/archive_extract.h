#pragma once
#include <QString>
#include <optional>

// aus ZIP (miniz, eingebettet) oder RAR (WinRAR UnRAR.exe, nur falls
// installiert -- identisch zum Python-Original) nach destDir entpacken.
// Rueckgabe: Pfad der ROM; leer wenn keine ROM/kein Entpacker.
// Ist der Pfad selbst schon eine ROM, wird er unveraendert zurueckgegeben.
std::optional<QString> extract_archive_rom(const QString& arc, const QString& destDir = QString());
