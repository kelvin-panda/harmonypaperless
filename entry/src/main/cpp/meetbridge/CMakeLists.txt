cmake_minimum_required(VERSION 3.5)
project(meetbridge C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prebuilt libmeetcore.so/libcodec.so supplied with HarmonyDemo.
set(PREBUILT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../libs/arm64-v8a" CACHE PATH "prebuilt SDK libs")

add_library(meet SHARED
    ohos_meet_bridge.cpp
    ohos_meet_napi.cpp
)

target_include_directories(meet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(meet PRIVATE
    OHOS=1
    MEET_NAPI_MODNAME="meet"
)

target_compile_options(meet PRIVATE
    -fPIC -O1 -g
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-int-conversion -Wno-incompatible-pointer-types
    -Wno-deprecated-declarations -Wno-writable-strings
)

target_link_libraries(meet PRIVATE
    "${PREBUILT_DIR}/libmeetcore.so"
    "${PREBUILT_DIR}/libcodec.so"
    libace_napi.z.so
    libhilog_ndk.z.so
)

set_target_properties(meet PROPERTIES
    OUTPUT_NAME meet
    POSITION_INDEPENDENT_CODE ON
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
    LINK_FLAGS "-Wl,--allow-shlib-undefined -Wl,--unresolved-symbols=ignore-all"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
)
