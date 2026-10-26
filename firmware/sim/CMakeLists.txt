# Host build of the Deskling UI: renders the real screens to PNGs without the hardware.
#   cmake -S firmware/sim -B build-sim -DLVGL_DIR=... -DCJSON_DIR=... -DSTB_DIR=...
#   cmake --build build-sim && ./build-sim/deskling_sim out/
# tools/run_sim.sh fetches the dependencies and does all of that.
cmake_minimum_required(VERSION 3.16)
project(deskling_sim C)
set(CMAKE_C_STANDARD 11)

set(LVGL_DIR "" CACHE PATH "LVGL source tree (v9.6)")
set(CJSON_DIR "" CACHE PATH "cJSON source tree")
set(STB_DIR "" CACHE PATH "Folder with stb_image_write.h")

set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(${LVGL_DIR} lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(MAIN ${CMAKE_CURRENT_SOURCE_DIR}/../main)
file(GLOB UI_SRC ${MAIN}/ui/*.c ${MAIN}/ui/fonts/*.c)
add_executable(deskling_sim
  sim_main.c
  ${UI_SRC}
  ${MAIN}/app/model.c
  ${CJSON_DIR}/cJSON.c
)
target_include_directories(deskling_sim PRIVATE ${MAIN}/ui ${MAIN}/app ${CJSON_DIR} ${STB_DIR})
target_compile_options(deskling_sim PRIVATE -Wall -Wno-unused-function)
target_link_libraries(deskling_sim PRIVATE lvgl m)
