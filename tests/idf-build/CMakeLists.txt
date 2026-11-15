cmake_minimum_required(VERSION 3.16)

get_filename_component(DRAGON_CORE_ROOT "../.." ABSOLUTE
                       BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(EXTRA_COMPONENT_DIRS "${DRAGON_CORE_ROOT}/components")

# Each build directory keeps its own sdkconfig, so builds with different
# ESP-IDF versions do not share one file. A -DSDKCONFIG=... value has priority.
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The committed dependencies.lock is for ESP-IDF 5 (the CI minimum, 5.3).
# Other major versions solve their own lock, which is not committed.
if(NOT IDF_VERSION_MAJOR EQUAL 5)
    idf_build_set_property(DEPENDENCIES_LOCK
        "${CMAKE_CURRENT_LIST_DIR}/dependencies.idf${IDF_VERSION_MAJOR}.lock")
endif()

project(dragon_core_compile_test)
