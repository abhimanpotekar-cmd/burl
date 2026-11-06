cmake_minimum_required(VERSION 3.24)
project(burl VERSION 0.1.0 LANGUAGES CXX)

# Enforce Modern C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable FetchContent for automated dependency resolution
include(FetchContent)

FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.8
)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)

    add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )

    target_include_directories(imgui PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
    )

    target_link_libraries(imgui PUBLIC
            d3d11
            dxgi
    )
endif()


set(SOURCES
        src/main.cpp
        src/config_parser.cpp
        src/config_parser.h
        src/router.cpp
        src/router.h
        src/win32_integration.cpp
        src/win32_integration.h
        ui/settings_window.cpp
        ui/settings_window.h
)

# WIN32 flag configures the linker for the Windows Subsystem (wWinMain entry point)
add_executable(burl WIN32 ${SOURCES})

# Target Include Directories
target_include_directories(burl PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
)


target_compile_definitions(burl PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

if(MSVC)
    target_compile_options(burl PRIVATE
            /W4
            /utf-8
            /permissive-
    )
else()
    target_compile_options(burl PRIVATE
            -Wall
            -Wextra
    )
endif()


target_link_libraries(burl PRIVATE
        nlohmann_json::nlohmann_json
        imgui
        advapi32
        shell32
        ole32
        d3d11
        dxgi
)


add_custom_command(TARGET burl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:burl>/config"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/default_rules.json"
        "$<TARGET_FILE_DIR:burl>/config/default_rules.json"
        COMMENT "Syncing config/default_rules.json to output directory"
)