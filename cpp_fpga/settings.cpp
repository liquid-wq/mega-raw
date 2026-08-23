#include "settings.h"
#include "json.h"
#include <fstream>
#include <sstream>

Settings Settings::load(const QString& path) {
    Settings s;
    std::ifstream f(path.toStdString(), std::ios::binary);
    if (!f) return s;
    std::stringstream ss; ss << f.rdbuf();
    try {
        Json j = Json::parse(ss.str());
        if (j.type() != Json::Type::Object) return s;
        if (auto* v = j.find("hardcore"))  if (v->type()==Json::Type::Bool) s.hardcore = v->as_bool();
        if (auto* v = j.find("language"))  if (v->type()==Json::Type::String) s.language = QString::fromStdString(v->as_string());
        if (auto* v = j.find("rom_root"))  if (v->type()==Json::Type::String) s.rom_root = QString::fromStdString(v->as_string());
        if (auto* v = j.find("save_login")) if (v->type()==Json::Type::Bool) s.save_login = v->as_bool();
        if (auto* v = j.find("user"))      if (v->type()==Json::Type::String) s.user = QString::fromStdString(v->as_string());
        if (auto* v = j.find("token"))     if (v->type()==Json::Type::String) s.token = QString::fromStdString(v->as_string());
    } catch (...) {}
    return s;
}

void Settings::save(const QString& path) const {
    Json j = Json::object();
    j.set("hardcore", Json::boolean(hardcore));
    j.set("language", Json::string(language.toStdString()));
    j.set("rom_root", Json::string(rom_root.toStdString()));
    j.set("save_login", Json::boolean(save_login));
    j.set("user", Json::string(user.toStdString()));
    j.set("token", Json::string(token.toStdString()));
    std::ofstream o(path.toStdString(), std::ios::binary);
    o << j.dump(1);
}
