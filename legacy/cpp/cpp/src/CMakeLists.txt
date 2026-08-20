cmake_minimum_required(VERSION 3.16)
project(mega_raw_gui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network SerialPort)

add_executable(mega_raw_gui
    main.cpp
    mainwindow.cpp
    ra_network.cpp
    ed_serial_qt.cpp
    monitor_worker.cpp
    stub.cpp
    md5.cpp
    json.cpp
    ra_cache.cpp
    ra_client.cpp
    throttle.cpp
    ra_engine.cpp
    rom_patch.cpp
    ed_protocol.cpp
    sd_export_worker.cpp
    batch_patch_worker.cpp
    archive_extract.cpp
    i18n.cpp
    compat.cpp
    badge_loader.cpp
    miniz/miniz.c
    miniz/miniz_zip.c
    miniz/miniz_tinfl.c
    miniz/miniz_tdef.c
    settings.cpp
    intro_window.cpp
    app.rc
    resources.qrc
)
target_link_libraries(mega_raw_gui PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::SerialPort winmm)

# assets/ (z.B. Pixelart) und mega_raw_intro.html automatisch neben die .exe
# kopieren, damit das nach jedem Build nicht mehr manuell noetig ist.
add_custom_command(TARGET mega_raw_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:mega_raw_gui>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/mega_raw_intro.html"
        "$<TARGET_FILE_DIR:mega_raw_gui>/mega_raw_intro.html"
)
