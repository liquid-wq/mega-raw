// ROM aus ZIP/RAR holen -- wie archive_extract.h/.cpp der Qt-Fassung.
// ZIP ueber miniz, RAR ueber ein installiertes WinRAR (UnRAR.exe).
// Entpackt neben das Archiv (oder nach zielOrdner). Pfade in UTF-8.
#pragma once

#include <optional>
#include <string>

std::optional<std::string> extract_archive_rom(const std::string& arc, const std::string& zielOrdner = std::string());
