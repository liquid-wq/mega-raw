#pragma once
#include <QString>
#include <QStringList>
#include <vector>
#include <cstdint>

// achCount/achUnsupported optional (=-1 -> Punkt 4 wird uebersprungen).
struct CompatResult { bool ok; QStringList lines; };
CompatResult check_compat(const std::vector<uint8_t>& rom,
                          int achCount = -1, int achUnsupported = -1);
