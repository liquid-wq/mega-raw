#include "schrift.h"

#include <map>

namespace schrift {
namespace {
std::map<const ImFont*, float>& Tabelle() {
    static std::map<const ImFont*, float> t;
    return t;
}
}  // namespace

void Faktor(ImFont* f, float faktor) { if (f) Tabelle()[f] = faktor; }

float Px(ImFont* f, float em) {
    auto it = Tabelle().find(f);
    return em * (it == Tabelle().end() ? 1.0f : it->second);
}

}  // namespace schrift
