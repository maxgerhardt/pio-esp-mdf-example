# This is the project CMakeLists.txt file for the test subproject 
cmake_minimum_required(VERSION 3.5)

# Include the components directory of the main application:
#
set(EXTRA_COMPONENT_DIRS "../../../components")

# Set the components to include the tests for.
# This can be overriden from CMake cache:
# - when invoking CMake directly: cmake -D TEST_COMPONENTS="xxxxx" ..
# - when using idf.py: idf.py build -T xxxxx
#
set(TEST_COMPONENTS "aliyun_sdk" CACHE STRING "List of components to test")

if(NOT DEFINED $ENV{MDF_PATH})
    set(ENV{MDF_PATH} ${CMAKE_CURRENT_LIST_DIR}/../../..)
endif()
include($ENV{MDF_PATH}/project.cmake)
project(aliyun_unit_test)
