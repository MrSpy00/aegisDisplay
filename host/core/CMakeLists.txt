cmake_minimum_required(VERSION 3.24)
project(aegisDisplayCore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /EHsc /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

add_library(aegis_protocol STATIC
    src/adsp_protocol.cpp
    src/input_math.cpp
)
target_include_directories(aegis_protocol PUBLIC src third_party)

add_executable(aegisDisplayCore
    src/main.cpp
    src/sudovda_manager.cpp
    src/dxgi_capture.cpp
    src/video_encoder.cpp
    src/input_injector.cpp
    src/network_server.cpp
    src/ipc_server.cpp
    src/aegisdisplay_core.rc
)

target_include_directories(aegisDisplayCore PRIVATE src third_party third_party/ffnvcodec)
target_link_libraries(aegisDisplayCore PRIVATE
    aegis_protocol
    d3d11
    dxgi
    dxguid
    mfplat
    mfuuid
    mfreadwrite
    ws2_32
    iphlpapi
    setupapi
    newdev
    crypt32
    qwave
    avrt
    user32
    advapi32
    bcrypt
)

add_custom_command(TARGET aegisDisplayCore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../drivers/sudovda"
        "$<TARGET_FILE_DIR:aegisDisplayCore>/drivers/sudovda"
    COMMENT "Copying SudoVDA driver package next to aegisDisplayCore"
)

enable_testing()
add_executable(aegis_protocol_tests tests/adsp_protocol_tests.cpp)
target_link_libraries(aegis_protocol_tests PRIVATE aegis_protocol)
add_test(NAME adsp_protocol_tests COMMAND aegis_protocol_tests)
