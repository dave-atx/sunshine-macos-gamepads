# macos specific target definitions

if (SUNSHINE_BUILD_HOMEBREW)
    target_link_options(sunshine PRIVATE LINKER:-sectcreate,__TEXT,__info_plist,${APPLE_PLIST_FILE})
else()
    # .app build
    set_target_properties(sunshine PROPERTIES
            OUTPUT_NAME "${CMAKE_PROJECT_NAME}"
            MACOSX_BUNDLE_BUNDLE_NAME "${CMAKE_PROJECT_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${SUNSHINE_MACOS_BUNDLE_ID}"
            MACOSX_BUNDLE_INFO_PLIST "${APPLE_PLIST_FILE}"
            MACOSX_BUNDLE_ICON_FILE "sunshine.icns"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

    # Populate bundle resources in the build tree for local runs.
    set(_bundle_resources_dir "$<TARGET_FILE_DIR:sunshine>/../Resources")
    add_custom_command(TARGET sunshine POST_BUILD
            COMMENT "Copying bundle resources to build tree"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_bundle_resources_dir}"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_BINARY_DIR}/assets" "${_bundle_resources_dir}/assets"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${PROJECT_SOURCE_DIR}/src_assets/macos/build/sunshine.icns"
                    "${_bundle_resources_dir}/sunshine.icns"
            VERBATIM)

    # Re-sign the bundle with a stable identity when one is available.
    #
    # CMake leaves the app ad-hoc signed, whose designated requirement is
    # `cdhash H"..."`. That hash changes on every rebuild, so macOS cannot match
    # an existing privacy grant to the running binary: System Settings shows
    # screen recording as enabled while CGPreflightScreenCaptureAccess keeps
    # returning false, and only `tccutil reset ScreenCapture` clears it.
    #
    # Signing with any real certificate produces an identifier-based requirement
    # that survives rebuilds, so grants stick. This is still a local-development
    # signature, not Developer ID: distributing the app still needs notarization.
    set(SUNSHINE_MACOS_SIGN_IDENTITY "" CACHE STRING
        "Optional codesign identity for local builds, e.g. 'Apple Development: Name (TEAMID)'. Empty leaves CMake's ad-hoc signature.")
    if(SUNSHINE_MACOS_SIGN_IDENTITY)
        add_custom_command(TARGET sunshine POST_BUILD
                COMMENT "Signing bundle with ${SUNSHINE_MACOS_SIGN_IDENTITY}"
                COMMAND /usr/bin/codesign --force --deep
                        --sign "${SUNSHINE_MACOS_SIGN_IDENTITY}"
                        --identifier "${SUNSHINE_MACOS_BUNDLE_ID}"
                        "$<TARGET_BUNDLE_DIR:sunshine>"
                VERBATIM)
        message(STATUS "macOS bundle will be signed with: ${SUNSHINE_MACOS_SIGN_IDENTITY}")
    else()
        message(STATUS "macOS bundle stays ad-hoc signed; screen-recording grants will not survive a rebuild (see README)")
    endif()
endif()

# Tell linker to dynamically load these symbols at runtime, in case they're unavailable:
target_link_options(sunshine PRIVATE -Wl,-U,_CGPreflightScreenCaptureAccess -Wl,-U,_CGRequestScreenCaptureAccess)
