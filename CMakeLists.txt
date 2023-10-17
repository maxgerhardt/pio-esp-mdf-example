cmake_minimum_required(VERSION 3.16.0)
# Make sure to use our ESP-MDF
set(ENV{MDF_PATH} ${CMAKE_CURRENT_LIST_DIR}/esp-mdf)
include($ENV{MDF_PATH}/project.cmake)
project(esp32_mdf_new)
