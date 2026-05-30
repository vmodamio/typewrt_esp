# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3" CACHE STRING "ESP-IDF target")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(typewrt)

# ESP-IDF v6.0.1 leaves an unused static helper in the ESP32-S3 OPI flash path
# when MXIC OPI probing is disabled. Keep that external warning out of app builds.
idf_component_get_property(spi_flash_lib spi_flash COMPONENT_LIB)
target_compile_options(${spi_flash_lib} PRIVATE -Wno-unused-function)
