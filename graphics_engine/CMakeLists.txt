cmake_minimum_required(VERSION 3.17)
project(BorlandEmulator)

set(CMAKE_CXX_STANDARD 17)

# Find SDL2 - Standard lookup
find_package(SDL2 REQUIRED)

# Include directories
include_directories(${SDL2_INCLUDE_DIRS} .)

# Source files
set(SOURCES
    main.cpp
    BorlandEngine.cpp
    Utils.cpp
    WindowSystem.cpp
    UI.cpp
)

# Create Executable
add_executable(BorlandIDE ${SOURCES})

# Link Libraries
target_link_libraries(BorlandIDE ${SDL2_LIBRARIES})

# Windows helper: Copies SDL2.dll to build folder if on Windows
if(WIN32)
    file(COPY ${SDL2_LIBDIR}/SDL2.dll DESTINATION ${CMAKE_BINARY_DIR})
endif()