# An explicit installed library supports reproducible offline builds as well.
set(VKM_OPUS_LIBRARY "" CACHE FILEPATH "Optional installed libopus 1.x library")
set(VKM_OPUS_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/licenses/opus/COPYING" CACHE FILEPATH "License shipped with the installed libopus")
if(VKM_OPUS_LIBRARY)
    if(NOT EXISTS "${VKM_OPUS_LIBRARY}" OR NOT EXISTS "${VKM_OPUS_LICENSE}")
        message(FATAL_ERROR "Supply an existing libopus library and its license")
    endif()
    add_library(opus UNKNOWN IMPORTED)
    set_target_properties(opus PROPERTIES IMPORTED_LOCATION "${VKM_OPUS_LIBRARY}")
    set(VKM_OPUS_COPYING "${VKM_OPUS_LICENSE}")
else()
    include(FetchContent)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "Use application codec tests" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "Build library only" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "Do not install dependencies" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "Do not install dependencies" FORCE)
    FetchContent_Declare(opus
        URL https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz
        URL_HASH SHA256=6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(opus)
    set(VKM_OPUS_COPYING "${opus_SOURCE_DIR}/COPYING")
endif()
