cmake_minimum_required(VERSION 4.3)
project(Projekt_Ask)

set(CMAKE_CXX_STANDARD 20)

# Aplikacja z interfejsem graficznym (domyślny target uruchamiany z CLion).
add_executable(Projekt_Ask WIN32
        GuiMain.cpp
        main.cpp
        Microprocessor.cpp
        Microprocessor.h
        ProgramConfig.h)

# Wersja konsolowa (zachowana dla kompatybilności).
add_executable(Projekt_Ask_CLI
        main.cpp
        Microprocessor.cpp
        Microprocessor.h
        ProgramConfig.h)

target_compile_definitions(Projekt_Ask PRIVATE ASK_GUI_BUILD)
target_link_libraries(Projekt_Ask PRIVATE comctl32 comdlg32)
target_link_libraries(Projekt_Ask_CLI PRIVATE comctl32)
