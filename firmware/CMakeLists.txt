cmake_minimum_required(VERSION 3.22)

# Your Wi-Fi and relay secrets go in sdkconfig.defaults.local (git-ignored), see README.
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deskling)
