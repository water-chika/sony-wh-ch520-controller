cmake_minimum_required(VERSION 3.31 FATAL_ERROR)
project(sony-wh-ch520-controller VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "sony-wh-ch520-controller currently supports Linux only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MDR_BUILD_CLIENT OFF CACHE BOOL "" FORCE)
set(MDR_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(MDR_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
set(MDR_BUILD_BT ON CACHE BOOL "" FORCE)
set(MDR_BT_BLE_SUPPORT OFF CACHE BOOL "" FORCE)
set(MDR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(SonyHeadphonesClient EXCLUDE_FROM_ALL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)

add_library(whch520_core
    src/bluez.cpp
    src/controller.cpp
    src/controls.cpp
    src/device.cpp
    src/probe_adapter.cpp
)
add_library(whch520::core ALIAS whch520_core)
target_include_directories(whch520_core PUBLIC include)
target_link_libraries(whch520_core
    PUBLIC mdr mdr::bt
    PRIVATE PkgConfig::DBUS
)
target_compile_options(whch520_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(whch520ctl src/main.cpp)
target_link_libraries(whch520ctl PRIVATE whch520::core)
target_compile_options(whch520ctl PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(whch520_controls_test tests/controls_test.cpp)
    target_link_libraries(whch520_controls_test PRIVATE whch520::core)
    add_test(NAME whch520_controls_test COMMAND whch520_controls_test)
endif()

install(TARGETS whch520ctl RUNTIME DESTINATION bin)
