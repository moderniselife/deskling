cmake_minimum_required(VERSION 3.22)

# Default build: ESP32-P4 silicon v1.x + LCD V1 (Guition boards from 2025), plus your
# own settings in sdkconfig.defaults.local if it exists (git-ignored).
# tools/release.sh passes SDKCONFIG_DEFAULTS itself to build every variant.
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;config/p4r1.defaults;config/lcd1.defaults")
    if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local)
        list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.local")
    endif()
endif()

if(DEFINED ENV{DESKLING_VERSION})
    set(PROJECT_VER $ENV{DESKLING_VERSION})
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deskling)
