cmake_minimum_required(VERSION 3.28)
project(algorithmvisualizercpp CXX)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- 1. Setup SDL3 ---
# We download the main SDL3 library.
FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.0
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(SDL3)

# --- 2. Define Executable ---
add_executable(algorithmvisualizercpp main.cpp)


# Connect SDL3 to executable
target_link_libraries(algorithmvisualizercpp PRIVATE SDL3::SDL3)

# --- 4. Windows Setup (DLL Copying & Static Linking) ---
if (WIN32)
    # This automatically copies "SDL3.dll" next to your .exe every time you build.
    add_custom_command(
            TARGET algorithmvisualizercpp POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:algorithmvisualizercpp>"
            VERBATIM
    )

    set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")
endif()