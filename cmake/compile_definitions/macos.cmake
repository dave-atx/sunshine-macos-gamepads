# macos specific compile definitions

add_compile_definitions(SUNSHINE_PLATFORM="macos")

if (SUNSHINE_BUILD_HOMEBREW)
    set(SUNSHINE_ASSETS_DIR "${CMAKE_INSTALL_PREFIX}/${SUNSHINE_ASSETS_DIR}")
else()
    # Bundle layout for macOS app builds
    set(SUNSHINE_ASSETS_DIR "${CMAKE_PROJECT_NAME}.app/Contents/Resources/assets")
    set(SUNSHINE_ASSETS_DIR_DEF "../Resources/assets")
endif()

set(MACOS_LINK_DIRECTORIES
        /opt/homebrew/lib
        /opt/local/lib
        /usr/local/lib)

foreach(dir ${MACOS_LINK_DIRECTORIES})
    if(EXISTS ${dir})
        link_directories(${dir})
    endif()
endforeach()

if(NOT BOOST_USE_STATIC AND NOT CPM_BOOST_USED)
    ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)
endif()

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        ${APP_KIT_LIBRARY}
        ${APP_SERVICES_LIBRARY}
        ${AUDIO_TOOLBOX_LIBRARY}
        ${AUDIO_UNIT_LIBRARY}
        ${AV_FOUNDATION_LIBRARY}
        ${CORE_AUDIO_LIBRARY}
        ${CORE_MEDIA_LIBRARY}
        ${CORE_VIDEO_LIBRARY}
        ${FOUNDATION_LIBRARY}
        ${IOKIT_LIBRARY}
        ${USER_NOTIFICATIONS_LIBRARY}
        ${SCREEN_CAPTURE_KIT_LIBRARY}
        ${VIDEO_TOOLBOX_LIBRARY})

set(APPLE_PLIST_TEMPLATE "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/build/Info.plist.in")
set(APPLE_PLIST_FILE "${CMAKE_BINARY_DIR}/Info.plist")
set(APPLE_ENTITLEMENTS_FILE "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/entitlements.plist")
configure_file("${APPLE_PLIST_TEMPLATE}" "${APPLE_PLIST_FILE}" @ONLY)

set(PLATFORM_TARGET_FILES
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_audio.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_audio.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_img_t.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_video.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/av_video.m"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/display.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/display_helpers.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/display_helpers.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/input.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/microphone.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/misc.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/misc.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/publish.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/sc_capture.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/sc_capture.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/sc_capture_loop.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/sc_capture_loop.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/vt_encoder.h"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/vt_encoder.mm"
        "${CMAKE_SOURCE_DIR}/third-party/TPCircularBuffer/TPCircularBuffer.c"
        "${CMAKE_SOURCE_DIR}/third-party/TPCircularBuffer/TPCircularBuffer.h"
        ${APPLE_ENTITLEMENTS_FILE}
        ${APPLE_PLIST_FILE})

# OpenJoystickDriver virtual gamepad backend.
#
# Apple restricts com.apple.developer.hid.virtual.device to approved teams, so a
# self-built Sunshine cannot create HID devices itself. This component forwards
# gamepad input to a separately entitled local broker over its documented
# endpoint instead. It is inert unless ojd_socket_path and ojd_token are set.
set(NEXUS_OJD_DIR "${CMAKE_SOURCE_DIR}/third-party/nexus-ojd")
if(EXISTS "${NEXUS_OJD_DIR}/CMakeLists.txt")
    add_subdirectory("${NEXUS_OJD_DIR}" "${CMAKE_BINARY_DIR}/nexus-ojd")

    list(APPEND PLATFORM_TARGET_FILES
            "${NEXUS_OJD_DIR}/src/sunshine_adapter.cpp")
    list(APPEND SUNSHINE_EXTERNAL_LIBRARIES ojd_client)
    include_directories(SYSTEM "${NEXUS_OJD_DIR}/include")
else()
    message(WARNING "nexus-ojd component not found at ${NEXUS_OJD_DIR}; OJD gamepad backend disabled")
endif()
