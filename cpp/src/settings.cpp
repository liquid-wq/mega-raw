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
        if (auto* v = j.find("av_safe"))   if (v->type()==Json::Type::Bool) s.av_safe = v->as_bool();
        if (auto* v = j.find("tv_flash"))  if (v->type()==Json::Type::Bool) s.tv_flash = v->as_bool();
        if (auto* v = j.find("bucket"))    if (v->type()==Json::Type::String) s.bucket = QString::fromStdString(v->as_string());
        if (auto* v = j.find("language"))  if (v->type()==Json::Type::String) s.language = QString::fromStdString(v->as_string());
    } catch (...) {}
    s.hardcore = false; // Softcore-Zwang
    return s;
}

void Settings::save(const QString& path) const {
    Json j = Json::object();
    j.set("hardcore", Json::boolean(false)); // gesperrt
    j.set("av_safe", Json::boolean(av_safe));
    j.set("tv_flash", Json::boolean(tv_flash));
    j.set("bucket", Json::string(bucket.toStdString()));
    j.set("language", Json::string(language.toStdString()));
    std::ofstream o(path.toStdString(), std::ios::binary);
    o << j.dump(1);
}
