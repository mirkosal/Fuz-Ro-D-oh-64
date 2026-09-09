cmake_minimum_required(VERSION 3.20)

set(NAME "FuzRoDoh")
set(VERSION 2.5.0)

project(${NAME} VERSION ${VERSION} LANGUAGES CXX)

message(STATUS "Building ${NAME} ${VERSION} for the Skyrim 1.7.104 test port")

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed")
endif()

# CommonLibSSE-NG 7.4.0 is the dependency baseline used by this port.
include(FetchContent)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIB_ENABLE_IPO OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    CommonLibSSE_NG
    GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
    GIT_TAG 9b17b42fc9db23aea2b60f92690e778784e612b1
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(CommonLibSSE_NG)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
)

include(cmake/headerlist.cmake)
include(cmake/sourcelist.cmake)

add_library(
    ${PROJECT_NAME}
    SHARED
    ${headers}
    ${sources}
    ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        CommonLibSSE::CommonLibSSE
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# CommonLibSSE-NG 7.4 is built with the dynamic MSVC runtime (/MD).
# Keep this target on the same CRT to avoid LNK2038/LNK1169 runtime-library conflicts.
set_target_properties(${PROJECT_NAME} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

if(MSVC)
    target_compile_options(
        ${PROJECT_NAME}
        PRIVATE
            /sdl
            /utf-8
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /W4
            /wd4100
            /wd4189
            /wd4505
    )
endif()
