#include "einstellungen.h"

#include "json.h"

#include <filesystem>
#include <fstream>
#include <sstream>

Settings Settings::load(const std::string& path) {
    Settings s;
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return s;
    std::stringstream ss; ss << f.rdbuf();
    try {
        Json j = Json::parse(ss.str());
        if (j.type() != Json::Type::Object) return s;
        if (auto* v = j.find("hardcore"))   if (v->type() == Json::Type::Bool) s.hardcore = v->as_bool();
        if (auto* v = j.find("language"))   if (v->type() == Json::Type::String) s.language = v->as_string();
        if (auto* v = j.find("rom_root"))   if (v->type() == Json::Type::String) s.rom_root = v->as_string();
        if (auto* v = j.find("save_login")) if (v->type() == Json::Type::Bool) s.save_login = v->as_bool();
        if (auto* v = j.find("user"))       if (v->type() == Json::Type::String) s.user = v->as_string();
        if (auto* v = j.find("token"))      if (v->type() == Json::Type::String) s.token = v->as_string();
        if (auto* v = j.find("message_read")) if (v->type() == Json::Type::Int) s.message_read = v->as_int();
        if (auto* v = j.find("popup_in_game")) if (v->type() == Json::Type::Bool) s.popup_in_game = v->as_bool();
        if (auto* v = j.find("md5_cache"))
            if (v->type() == Json::Type::Object)
                for (const auto& e : v->entries())
                    if (e.second.type() == Json::Type::String) s.md5_cache[e.first] = e.second.as_string();
    } catch (...) {}
    return s;
}

void Settings::save(const std::string& path) const {
    Json j = Json::object();
    j.set("hardcore", Json::boolean(hardcore));
    j.set("language", Json::string(language));
    j.set("rom_root", Json::string(rom_root));
    j.set("save_login", Json::boolean(save_login));
    j.set("user", Json::string(user));
    j.set("token", Json::string(token));
    j.set("message_read", Json::integer(message_read));
    j.set("popup_in_game", Json::boolean(popup_in_game));
    Json c = Json::object();
    for (const auto& e : md5_cache) c.set(e.first, Json::string(e.second));
    j.set("md5_cache", c);
    std::ofstream o(std::filesystem::u8path(path), std::ios::binary);
    o << j.dump(1);
}
