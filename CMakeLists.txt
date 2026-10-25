cmake_minimum_required(VERSION 3.16)
project(bananatube C)

# BananaTube -- a native dual-screen YouTube client for BananaOS on the Anbernic RG DS / RG DS Plus.
#
# Three targets:
#   bananatube_core  the device-independent half (draw lists, key mapping, config, the YouTube backends).
#                    Builds, and its tests run, on any Linux -- no DRM, no GPU, no network.
#   bananatube       the binary: DRM/KMS, GBM/EGL, evdev, FreeType, and (Phase 3) libmpv.
#   tests            the host tests against bananatube_core.
#
# The two handhelds are the only target; docs/ARCHITECTURE.md says what runs where.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(BANANATUBE_KMS   "Build the DRM/KMS display half (libdrm, EGL, GBM, FreeType)" ON)
option(BANANATUBE_MPV   "Build libmpv playback (Phase 3; off when libmpv is not there)" ON)
option(BANANATUBE_TESTS "Build the host tests" ON)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
# glibc, like the rest of BananaOS (Buildroot's external toolchain): POSIX and GNU extensions are what the
# DRM/evdev/EGL headers expect.
add_compile_definitions(_GNU_SOURCE)
find_package(PkgConfig REQUIRED)

# ---- bananatube_core: no device dependencies, so the tests run anywhere --------------------------------
add_library(bananatube_core STATIC
    src/util.c
    src/conf.c
    src/display/drawlist.c
    src/display/render.c
    src/font.c
    src/input/keys.c
    src/input/keyboard.c
    src/net/http.c
    src/net/invidious.c
    src/net/innertube.c
    src/net/resolver.c
    src/net/thumbnail.c
    src/net/searchjob.c
    src/net/thumbworker.c
    src/history.c
    src/ui/theme.c
    src/ui/widget.c
    src/ui/results.c
    src/ui/view.c
)
target_include_directories(bananatube_core PUBLIC src)
pkg_check_modules(CURL  REQUIRED libcurl)
pkg_check_modules(CJSON REQUIRED libcjson)
pkg_check_modules(JPEG  REQUIRED libjpeg)
pkg_check_modules(FREETYPE REQUIRED freetype2)
target_include_directories(bananatube_core PRIVATE
    ${CURL_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS} ${JPEG_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(bananatube_core PUBLIC
    ${CURL_LIBRARIES} ${CJSON_LIBRARIES} ${JPEG_LIBRARIES} ${FREETYPE_LIBRARIES} pthread m)

# ---- bananatube: the binary ---------------------------------------------------------------------------
if(BANANATUBE_KMS)
    pkg_check_modules(DRM    REQUIRED libdrm)
    pkg_check_modules(EGL    REQUIRED egl)
    pkg_check_modules(GLESV2 REQUIRED glesv2)
    pkg_check_modules(GBM    REQUIRED gbm)

    # libmpv is Phase 3's playback. On by default when the library is there (a desktop checkout can build and
    # test it); the Buildroot package turns it on explicitly.
    set(HAVE_MPV OFF)
    if(BANANATUBE_MPV)
        pkg_check_modules(MPV mpv)
        if(MPV_FOUND)
            set(HAVE_MPV ON)
            add_compile_definitions(BANANATUBE_MPV=1)
        endif()
    endif()

    add_executable(bananatube
        src/main.c
        src/display/drm.c
        src/display/gbm_egl.c
        src/display/bottom.c
        src/input/evdev.c
    )
    if(HAVE_MPV)
        add_library(bananatube_mpv STATIC
            src/player/player.c
            src/player/osd.c
            src/display/gles_blit.c
        )
        target_include_directories(bananatube_mpv PUBLIC src PRIVATE ${MPV_INCLUDE_DIRS})
        target_link_libraries(bananatube_mpv PUBLIC bananatube_core ${MPV_LIBRARIES})
        target_link_libraries(bananatube PRIVATE bananatube_mpv)
    endif()
    target_include_directories(bananatube PRIVATE
        ${DRM_INCLUDE_DIRS} ${EGL_INCLUDE_DIRS} ${GLESV2_INCLUDE_DIRS} ${GBM_INCLUDE_DIRS}
        ${MPV_INCLUDE_DIRS})
    target_link_libraries(bananatube PRIVATE bananatube_core
        ${DRM_LIBRARIES} ${EGL_LIBRARIES} ${GLESV2_LIBRARIES} ${GBM_LIBRARIES}
        ${MPV_LIBRARIES})
    install(TARGETS bananatube RUNTIME DESTINATION bin)
endif()

# ---- tests --------------------------------------------------------------------------------------------
if(BANANATUBE_TESTS)
    enable_testing()
    foreach(t drawlist keys conf parse select lru ui worker render history)
        add_executable(test_${t} tests/test_${t}.c)
        target_link_libraries(test_${t} PRIVATE bananatube_core)
        target_compile_definitions(test_${t} PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        add_test(NAME ${t} COMMAND test_${t})
    endforeach()

    # the player's own test needs libmpv, so it is only built when it is there
    if(HAVE_MPV)
        add_executable(test_player tests/test_player.c)
        target_link_libraries(test_player PRIVATE bananatube_mpv)
        target_compile_definitions(test_player PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        add_test(NAME player COMMAND test_player)
    endif()
endif()
