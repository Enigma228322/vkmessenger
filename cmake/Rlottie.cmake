include(FetchContent)
# Pin the renderer and verify the archive; no moving branch is used in builds.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)
set(BUILD_SHARED_LIBS OFF)
set(LOTTIE_MODULE OFF CACHE BOOL "Build the decoder into rlottie" FORCE)
set(LOTTIE_TEST OFF CACHE BOOL "Use the application's renderer tests" FORCE)
FetchContent_Declare(rlottie
    URL https://codeload.github.com/TelegramMessenger/rlottie/tar.gz/53c9679069eb6b6513ccc3c892ef4f4a760658da
    URL_HASH SHA256=edc469495d49856a83304e249177987306ffd1c0ad4fb0847666c3a9214a85b3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(rlottie)
set_target_properties(rlottie PROPERTIES CXX_STANDARD 14 POSITION_INDEPENDENT_CODE ON)
target_include_directories(rlottie INTERFACE "$<BUILD_INTERFACE:${rlottie_SOURCE_DIR}/inc>")
# Upstream treats compiler-version-specific diagnostics as errors and has a
# malformed legacy macOS linker flag. Neither changes the renderer itself.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(rlottie PRIVATE -Wno-error)
endif()
if(APPLE)
    # Upstream's NEON helpers call 32-bit ARM assembly, unavailable on macOS
    # arm64. Select the portable implementation and omit those helper objects.
    target_compile_definitions(rlottie PRIVATE LOTTIE_DISABLE_ARM_NEON)
    get_target_property(rlottie_sources rlottie SOURCES)
    list(FILTER rlottie_sources EXCLUDE REGEX "vdrawhelper_neon\\.cpp$")
    set_property(TARGET rlottie PROPERTY SOURCES "${rlottie_sources}")
    get_target_property(rlottie_links rlottie INTERFACE_LINK_LIBRARIES)
    if(rlottie_links)
        list(FILTER rlottie_links EXCLUDE REGEX "undefined error")
        set_property(TARGET rlottie PROPERTY INTERFACE_LINK_LIBRARIES "${rlottie_links}")
    endif()
    get_target_property(rlottie_private_links rlottie LINK_LIBRARIES)
    if(rlottie_private_links)
        list(FILTER rlottie_private_links EXCLUDE REGEX "undefined error")
        set_property(TARGET rlottie PROPERTY LINK_LIBRARIES "${rlottie_private_links}")
    endif()
endif()
