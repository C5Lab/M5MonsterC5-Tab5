cmake_minimum_required(VERSION 3.20)
project(tab5_ui_emulator LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configure with emcmake cmake, or use build.ps1/build.sh")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# This artifact is downloaded over the network, so size is a product concern:
# optimize the whole build, LVGL included, for size rather than speed.
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG" CACHE STRING "" FORCE)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/generated/lv_conf.h" CACHE FILEPATH "Browser LVGL configuration")
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}/managed_components/lvgl__lvgl" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")

file(GLOB COMPONENT_INCLUDES "${REPO_ROOT}/components/*/include")
# The local PCAP analysis screens run the real components, not a re-implementation.
# Extraction uses the production component with a vendored portable SHA-256
# compatibility subset, so the build needs no host ESP-IDF or TLS library.
file(GLOB PCAP_COMPONENT_SOURCES "${REPO_ROOT}/components/pcap_*/*.c")
file(GLOB SUBGHZ_SCREEN_SOURCES "${REPO_ROOT}/main/screens/subghz_*.c")
file(GLOB NFC_SCREEN_SOURCES "${REPO_ROOT}/main/screens/nfc_*.c")
add_executable(emulator
    "${REPO_ROOT}/main/app_keyboard.c"
    "${REPO_ROOT}/main/app_keyboard_navigation.c"
    "${REPO_ROOT}/main/app_keyboard_rotation.c"
    "${REPO_ROOT}/main/scan_filter.c"
    "${REPO_ROOT}/main/transfer_speed_config.c"
    "${REPO_ROOT}/main/sta_mac_setup_model.c"
    "${REPO_ROOT}/main/sta_mac_setup_response.c"
    "${REPO_ROOT}/main/pixie_dust_session.c"
    "${REPO_ROOT}/main/sniffer_clients_model.c"
    "${REPO_ROOT}/main/ota_rf.c"
    "${REPO_ROOT}/main/tile_order.c"
    "${REPO_ROOT}/main/screens/tool_order_screen.c"
    "${REPO_ROOT}/main/wifi_analyzer_model.c"
    "${REPO_ROOT}/main/screens/wifi_analyzer_screen.c"
    generated/browser.c
    runtime/runtime.c
    vendor/sha256.c
    "${REPO_ROOT}/main/screens/cgw_parser.c"
    "${REPO_ROOT}/main/screens/psram_dynarr.c"
    ${SUBGHZ_SCREEN_SOURCES}
    ${NFC_SCREEN_SOURCES}
    ${PCAP_COMPONENT_SOURCES}
    "${REPO_ROOT}/main/lv_font_lato_regular_ttf.c")
set_source_files_properties("${REPO_ROOT}/main/screens/wifi_analyzer_screen.c"
    PROPERTIES COMPILE_DEFINITIONS EMU_NATIVE_ANALYZER_SCREEN)
target_include_directories(emulator PRIVATE
    generated generated/shims runtime
    "${REPO_ROOT}/main"
    "${REPO_ROOT}/main/screens" ${COMPONENT_INCLUDES})
set_source_files_properties(${SUBGHZ_SCREEN_SOURCES} "${REPO_ROOT}/main/screens/psram_dynarr.c"
    PROPERTIES COMPILE_DEFINITIONS EMULATOR_NATIVE_SUBGHZ=1)
set_source_files_properties(${NFC_SCREEN_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS EMULATOR_NATIVE_NFC=1)
target_compile_options(emulator PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(emulator PRIVATE lvgl m)
# Browser entry points carry EMSCRIPTEN_KEEPALIVE in runtime.c. No pthreads or
# SharedArrayBuffer are needed: the browser advances the cooperative loop.
target_link_options(emulator PRIVATE
    --no-entry
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createEmulator"
    "-sENVIRONMENT=web"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=67108864"
    "-sMAXIMUM_MEMORY=268435456"
    "-sSTACK_SIZE=1048576"
    "-sEXIT_RUNTIME=0"
    "-sASSERTIONS=1"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPU8','HEAPU16','FS']")
set_target_properties(emulator PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
# An always-run target also copies web-only changes on incremental builds.
add_custom_target(emulator_web ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/web" "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/model" "${CMAKE_CURRENT_SOURCE_DIR}/dist/model"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${REPO_ROOT}/docs/ui-emulator/neon-district.seed.json" "${CMAKE_CURRENT_SOURCE_DIR}/dist/scenario.json"
    DEPENDS emulator
    COMMENT "Copying emulator web assets")
