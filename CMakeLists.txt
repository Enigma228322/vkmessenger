cmake_minimum_required(VERSION 3.21)
project(VKNative VERSION 0.2.0 LANGUAGES CXX)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Network Concurrent Sql Multimedia)
find_package(ZLIB REQUIRED)
include(cmake/Rlottie.cmake)
include(cmake/Opus.cmake)
qt_standard_project_setup(REQUIRES 6.8)
option(VKM_ENABLE_API_INSPECTOR "Include the developer API inspector in the interface" OFF)
set(VKM_VK_APP_ID "" CACHE STRING "Registered VK application ID with Messages API access")
set(VKM_VK_REDIRECT_URI "http://127.0.0.1:43821/callback" CACHE STRING "Registered VK redirect URI")
set(VKM_VK_SCOPE "" CACHE STRING "Scopes granted to the registered VK application")
configure_file(cmake/BuildConfig.h.in generated/BuildConfig.h @ONLY)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    qt_add_plugin(vkm-webp STATIC CLASS_NAME VkmWebPPlugin src/platform/WebPPlugin.mm)
    target_link_libraries(vkm-webp PRIVATE Qt6::Gui "-framework ImageIO" "-framework CoreGraphics")
    add_library(vkm-platform STATIC src/platform/Preferences.mm)
    target_link_libraries(vkm-platform PRIVATE "-framework AppKit")
else()
    add_library(vkm-platform STATIC src/platform/Preferences.cpp)
endif()
target_include_directories(vkm-platform PUBLIC src)
if(APPLE)
    target_link_libraries(vkm-platform PUBLIC vkm-webp)
endif()

qt_add_executable(VK MACOSX_BUNDLE
    src/main.cpp src/Messenger.cpp src/Messenger.h
    src/OAuth.cpp src/OAuth.h src/Keychain.cpp src/Keychain.h
    src/LongPoll.cpp src/LongPoll.h src/LongPollEvent.h src/Form.h
    src/Methods.cpp src/Methods.h src/Transfers.cpp src/Transfers.h
    src/storage/LocalStore.cpp src/storage/LocalStore.h src/api/VkApiClient.cpp src/api/VkApiClient.h src/messaging/ActionController.cpp src/messaging/ActionController.h src/messaging/FolderController.cpp src/messaging/FolderController.h src/media/MediaCache.cpp src/media/MediaCache.h src/media/VoiceRecorder.cpp src/media/VoiceRecorder.h src/media/OggOpusWriter.cpp src/media/OggOpusWriter.h
    src/LottieSticker.cpp src/LottieSticker.h)
qt_add_qml_module(VK
    URI VK VERSION 1.0
    QML_FILES qml/PinnedMessageBar.qml qml/ConversationProfile.qml qml/ActionIcon.qml qml/Theme.qml qml/MessageActionPopup.qml qml/Main.qml qml/ReactionImage.qml qml/ReadIndicator.qml qml/FolderSheet.qml qml/MediaViewer.qml qml/ActionSheet.qml qml/MethodBrowser.qml qml/ResultView.qml qml/AttachmentView.qml qml/BotKeyboard.qml)
target_compile_definitions(VK PRIVATE VKM_ENABLE_API_INSPECTOR=$<BOOL:${VKM_ENABLE_API_INSPECTOR}>)
target_include_directories(VK PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(VK PRIVATE vkm-platform Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Concurrent Qt6::Sql Qt6::Multimedia opus rlottie::rlottie ZLIB::ZLIB)
if(APPLE AND VKM_OPUS_LIBRARY MATCHES "\\.dylib$")
    add_custom_command(TARGET VK POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:VK>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VKM_OPUS_LIBRARY}" "$<TARGET_BUNDLE_DIR:VK>/Contents/Frameworks/")
endif()
if(APPLE)
    add_custom_command(TARGET VK POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:VK>/Contents/Resources/licenses/opus"
        COMMAND ${CMAKE_COMMAND} -E copy "${VKM_OPUS_COPYING}" "$<TARGET_BUNDLE_DIR:VK>/Contents/Resources/licenses/opus/COPYING"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${rlottie_SOURCE_DIR}/licenses" "$<TARGET_BUNDLE_DIR:VK>/Contents/Resources/licenses/rlottie"
        COMMAND ${CMAKE_COMMAND} -E copy "${rlottie_SOURCE_DIR}/COPYING" "$<TARGET_BUNDLE_DIR:VK>/Contents/Resources/licenses/rlottie/COPYING"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_BUNDLE_DIR:VK>/Contents/Resources/licenses/THIRD_PARTY_NOTICES.md")
endif()
if(APPLE)
    target_link_libraries(VK PRIVATE "-framework Security" "-framework CoreFoundation")
    qt_import_plugins(VK INCLUDE Qt6::QDarwinMicrophonePermissionPlugin)
endif()
set_target_properties(VK PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "VK"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.reforged.messenger"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(voice-codec-tests tests/VoiceCodecTest.cpp src/media/OggOpusWriter.cpp)
    target_include_directories(voice-codec-tests PRIVATE src)
    target_link_libraries(voice-codec-tests PRIVATE Qt6::Test opus)
    add_test(NAME voice-codec-tests COMMAND voice-codec-tests)
    qt_add_executable(storage-tests tests/StorageTest.cpp src/storage/LocalStore.cpp src/storage/LocalStore.h)
    target_include_directories(storage-tests PRIVATE src)
    target_link_libraries(storage-tests PRIVATE Qt6::Test Qt6::Sql)
    add_test(NAME storage-tests COMMAND storage-tests)
    qt_add_executable(media-cache-tests tests/MediaCacheTest.cpp src/media/MediaCache.cpp src/media/MediaCache.h)
    target_include_directories(media-cache-tests PRIVATE src)
    target_link_libraries(media-cache-tests PRIVATE Qt6::Test Qt6::Network Qt6::Concurrent)
    add_test(NAME media-cache-tests COMMAND media-cache-tests)
    qt_add_executable(protocol-tests tests/ProtocolTest.cpp src/OAuth.cpp src/OAuth.h)
    target_include_directories(protocol-tests PRIVATE src)
    target_link_libraries(protocol-tests PRIVATE Qt6::Test Qt6::Network Qt6::Gui)
    add_test(NAME protocol-tests COMMAND protocol-tests)

    qt_add_executable(messenger-tests tests/MessengerTest.cpp
        src/Messenger.cpp src/Messenger.h src/OAuth.cpp src/OAuth.h src/LongPoll.cpp src/LongPoll.h
        src/storage/LocalStore.cpp src/storage/LocalStore.h src/api/VkApiClient.cpp src/api/VkApiClient.h src/messaging/ActionController.cpp src/messaging/ActionController.h src/messaging/FolderController.cpp src/messaging/FolderController.h src/media/MediaCache.cpp src/media/MediaCache.h src/media/VoiceRecorder.cpp src/media/VoiceRecorder.h src/media/OggOpusWriter.cpp src/media/OggOpusWriter.h
        src/Methods.cpp src/Methods.h src/Transfers.cpp src/Transfers.h src/LottieSticker.cpp src/LottieSticker.h)
    target_include_directories(messenger-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(messenger-tests PRIVATE NATIVE_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
    target_compile_definitions(messenger-tests PRIVATE VKM_ENABLE_API_INSPECTOR=1)
    target_link_libraries(messenger-tests PRIVATE vkm-platform Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Concurrent Qt6::Sql Qt6::Multimedia opus rlottie::rlottie ZLIB::ZLIB)
    add_test(NAME messenger-tests COMMAND messenger-tests)

    qt_add_executable(longpoll-tests tests/LongPollTest.cpp src/LongPoll.cpp src/LongPoll.h)
    target_include_directories(longpoll-tests PRIVATE src)
    target_link_libraries(longpoll-tests PRIVATE Qt6::Test Qt6::Network)
    add_test(NAME longpoll-tests COMMAND longpoll-tests)

    qt_add_executable(methods-tests tests/MethodsTest.cpp src/Methods.cpp src/Methods.h)
    target_include_directories(methods-tests PRIVATE src)
    target_link_libraries(methods-tests PRIVATE Qt6::Test)
    add_test(NAME methods-tests COMMAND methods-tests)

    qt_add_executable(transfers-tests tests/TransfersTest.cpp src/Transfers.cpp src/Transfers.h)
    target_include_directories(transfers-tests PRIVATE src)
    target_link_libraries(transfers-tests PRIVATE Qt6::Test Qt6::Network Qt6::Concurrent)
    add_test(NAME transfers-tests COMMAND transfers-tests)

    qt_add_executable(sticker-tests tests/StickerTest.cpp src/LottieSticker.cpp src/LottieSticker.h)
    target_include_directories(sticker-tests PRIVATE src)
    target_compile_definitions(sticker-tests PRIVATE STICKER_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sticker.json")
    target_link_libraries(sticker-tests PRIVATE Qt6::Test Qt6::Quick Qt6::Network Qt6::Concurrent rlottie::rlottie ZLIB::ZLIB)
    add_test(NAME sticker-tests COMMAND sticker-tests)
    set_tests_properties(voice-codec-tests media-cache-tests storage-tests protocol-tests messenger-tests longpoll-tests methods-tests transfers-tests sticker-tests PROPERTIES TIMEOUT 90)
endif()
