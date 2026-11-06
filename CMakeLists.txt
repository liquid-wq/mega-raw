cmake_minimum_required(VERSION 3.20)
project(mega_raw CXX C RC)

# ---------------------------------------------------------------------------
# Ziel: EINE Datei. Keine Begleit-DLLs ausser denen, die zu Windows gehoeren.
#
# Dear ImGui statt Qt, Win32 + OpenGL2 fuer Fenster und Zeichnung, MinGW-
# Laufzeit statisch gebunden. Mapper-Kern, Portrait, Intro-Bilder und
# Programmsymbol stecken als Windows-Ressourcen in der Exe (src/app.rc.in).
#
# src/core ist unveraendert aus der Qt-Fassung uebernommen (Bedingungs-
# auswertung, RA-Client, Kopierprogramm, Protokoll). Ersetzt sind nur
# Oberflaeche, serieller Anschluss und HTTP.
#
# Fremdquellen liegen ABSICHTLICH ausserhalb des Projektordners, damit hier
# ausschliesslich eigener Code liegt.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Version und Build-Nummer stehen wie in der Qt-Fassung in src/version.h.
file(STRINGS "${CMAKE_SOURCE_DIR}/src/version.h" BUILD_ZEILE REGEX "^#define MEGA_RAW_CPP_BUILD ")
string(REGEX REPLACE "^#define MEGA_RAW_CPP_BUILD ([0-9]+).*" "\\1" BUILD_NR "${BUILD_ZEILE}")

set(DEPS_DIR "${CMAKE_SOURCE_DIR}/../ra_nes_deps" CACHE PATH "Ordner mit miniz/")
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/../ra_saturn_deps/imgui" CACHE PATH "Ordner mit imgui.cpp")
set(MINIZ_DIR "${DEPS_DIR}/miniz")

foreach(d "${IMGUI_DIR}/imgui.cpp" "${MINIZ_DIR}/miniz.c")
    if(NOT EXISTS "${d}")
        message(FATAL_ERROR "Fehlt: ${d}")
    endif()
endforeach()

set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
configure_file(src/app.rc.in "${CMAKE_BINARY_DIR}/erzeugt/app.rc" @ONLY)

set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    # OpenGL2 und nicht OpenGL3: Windows ohne Grafiktreiber liefert nur
    # "OpenGL 1.1.0 / GDI Generic", dort stuerzt die OpenGL3-Anbindung ab.
    "${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp"
)

set(CORE_SOURCES
    src/core/ra_engine.cpp
    src/core/ra_client.cpp
    src/core/ra_cache.cpp
    src/core/json.cpp
    src/core/md5.cpp
    src/core/hook_copy.cpp
    src/core/ed_protocol.cpp
)

add_executable(mega_raw WIN32
    src/main.cpp
    "${CMAKE_BINARY_DIR}/erzeugt/app.rc"
    src/app.cpp
    src/intro.cpp
    src/popup.cpp
    src/bilder.cpp
    src/schrift.cpp
    src/i18n.cpp
    src/worker.cpp
    src/md_snoop.cpp
    src/md_romread.cpp
    src/ed_serial.cpp
    src/ra_http.cpp
    src/einstellungen.cpp
    src/archiv.cpp
    src/plattform.cpp
    ${CORE_SOURCES}
    "${MINIZ_DIR}/miniz.c"
    ${IMGUI_SOURCES}
)

target_include_directories(mega_raw PRIVATE
    src
    src/core
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    "${MINIZ_DIR}"
)

target_compile_definitions(mega_raw PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(mega_raw PRIVATE
    opengl32 gdi32 user32 shell32 comdlg32 dwmapi
    setupapi        # Auflisten der seriellen Anschluesse samt USB-Kennung
    winhttp         # HTTPS zu RetroAchievements
    gdiplus ole32   # PNG dekodieren, Freischalt-Einblendung
    winmm           # Freischalt-Ton
)

if(MINGW)
    # Keine libstdc++/libgcc/libwinpthread daneben. -municode, weil der
    # Einstiegspunkt wWinMain heisst. -s: Symbole entfernen.
    target_link_options(mega_raw PRIVATE -municode -static -static-libgcc -static-libstdc++ -s)
endif()

set(AUSGABE_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Ablage der fertigen Programme")
set_target_properties(mega_raw PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${AUSGABE_DIR}"
    # Build-Nummer im Dateinamen: ein neuer Build kollidiert nie mit einer
    # alten Exe, die noch laeuft oder gesperrt ist.
    OUTPUT_NAME "mega_raw_b${BUILD_NR}")
