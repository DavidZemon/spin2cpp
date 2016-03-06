cmake_minimum_required(VERSION 3.0)

if (CROSS STREQUAL win32)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/CMakeModules/Toolchain-mingw64.cmake)
elseif (CROSS STREQUAL rpi)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/CMakeModules/Toolchain-arm-linux.cmake)
endif ()

project(spin2cpp
    VERSION 1.96.0)

find_package(BISON REQUIRED)

# Compilation flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
include_directories(${CMAKE_CURRENT_LIST_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Set source files
set(SPIN_SOURCES spin2cpp functions pasm outcpp outdat spin.tab.c)
set(LEX_SOURCES lexer symbol ast expr flexbuf preprocess)
add_custom_command(OUTPUT spin.tab.c spin.tab.h
    COMMAND ${BISON_EXECUTABLE} -t -b spin -d "${CMAKE_CURRENT_LIST_DIR}/spin.y"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/spin.y")

# Make sure spin.tab.h is generated prior to compiling any other sources
add_custom_target(spin_tab
    DEPENDS spin.tab.h spin.tab.c)
add_library(lex ${LEX_SOURCES})
add_dependencies(lex spin_tab)

add_executable(spin2cpp ${SPIN_SOURCES})
target_link_libraries(spin2cpp lex m)

# Installation rules
install(TARGETS spin2cpp
    DESTINATION bin)
install(FILES
        README.md
        COPYING
        Changelog.txt
    DESTINATION share/doc/${PROJECT_NAME})

# Add unit tests
enable_testing()
add_subdirectory(Test)

# Packaging rules
include(CMakeCPack.cmake)