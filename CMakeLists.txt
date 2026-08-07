cmake_minimum_required(VERSION 3.24)

project(PdfPP VERSION 1.0.0 LANGUAGES CXX C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(BUILD_SHARED_LIBS "Build Pdf++ as a shared library" OFF)
option(PDFPP_BUILD_TESTS "Build Pdf++ tests" ${BUILD_TESTING})
option(PDFPP_BUILD_UNIT_TESTS "Build the comprehensive PdfPP.UnitTests target" ON)
option(PDFPP_STRICT "Treat compiler warnings as errors" OFF)
option(PDFPP_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PDFPP_BUILD_WIN32_VIEWER "Build the native Win32 PDF reader" ON)

find_package(ZLIB REQUIRED)

set(PDFPP_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Pdf++.Core")
file(GLOB_RECURSE PDFPP_SOURCES CONFIGURE_DEPENDS
    "${PDFPP_CORE_DIR}/src/*.cpp")

add_library(PdfPPCore ${PDFPP_SOURCES})
add_library(PdfPP::Core ALIAS PdfPPCore)
set_target_properties(PdfPPCore PROPERTIES
    OUTPUT_NAME "PdfPP.Core"
    EXPORT_NAME Core
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_compile_features(PdfPPCore PUBLIC cxx_std_20)
target_compile_definitions(PdfPPCore PRIVATE CPPPDF_BUILDING_CORE=1)
target_include_directories(PdfPPCore
    PUBLIC
        $<BUILD_INTERFACE:${PDFPP_CORE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${PDFPP_CORE_DIR}/src)
target_link_libraries(PdfPPCore PRIVATE ZLIB::ZLIB)

if(WIN32)
    target_link_libraries(PdfPPCore PRIVATE bcrypt)
endif()

if(MSVC)
    target_compile_options(PdfPPCore PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /FS)
    if(PDFPP_STRICT)
        target_compile_options(PdfPPCore PRIVATE /WX)
    endif()
else()
    target_compile_options(PdfPPCore PRIVATE -Wall -Wextra -Wpedantic)
    if(PDFPP_STRICT)
        target_compile_options(PdfPPCore PRIVATE -Werror)
    endif()
endif()

if(PDFPP_ENABLE_ASAN)
    if(MSVC)
        target_compile_options(PdfPPCore PUBLIC /fsanitize=address)
        target_link_options(PdfPPCore PUBLIC /fsanitize=address)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(PdfPPCore PUBLIC
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(PdfPPCore PUBLIC -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "PDFPP_ENABLE_ASAN is unsupported by this compiler")
    endif()
endif()

install(TARGETS PdfPPCore
    EXPORT PdfPPTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY "${PDFPP_CORE_DIR}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT PdfPPTargets
    FILE PdfPPTargets.cmake
    NAMESPACE PdfPP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PdfPP)

configure_package_config_file(
    cmake/PdfPPConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/PdfPPConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PdfPP)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PdfPPConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PdfPPConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PdfPPConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PdfPP)

if(WIN32 AND PDFPP_BUILD_WIN32_VIEWER)
    set(PDFPP_MODERN_WIN32_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/PdfPP.ModernWin32")
    add_library(PdfPPModernWin32 STATIC
        "${PDFPP_MODERN_WIN32_DIR}/src/ModernWin32.cpp")
    target_compile_features(PdfPPModernWin32 PUBLIC cxx_std_20)
    target_compile_definitions(PdfPPModernWin32 PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(PdfPPModernWin32 PUBLIC
        "${PDFPP_MODERN_WIN32_DIR}/include")
    target_link_libraries(PdfPPModernWin32 PUBLIC comctl32 dwmapi uxtheme)

    set(PDFPP_WIN32_READER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/apps/PdfPP.Win32")
    add_executable(PdfPP.Win32 WIN32
        "${PDFPP_WIN32_READER_DIR}/src/Main.cpp"
        "${PDFPP_WIN32_READER_DIR}/src/PdfViewerWindow.cpp"
        "${PDFPP_WIN32_READER_DIR}/resources/PdfPP.Win32.rc")
    target_compile_features(PdfPP.Win32 PRIVATE cxx_std_20)
    target_compile_definitions(PdfPP.Win32 PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(PdfPP.Win32 PRIVATE
        "${PDFPP_WIN32_READER_DIR}/include")
    target_link_libraries(PdfPP.Win32 PRIVATE
        PdfPPCore PdfPPModernWin32 ZLIB::ZLIB
        bcrypt comctl32 comdlg32 dwmapi uxtheme shell32 shlwapi)
    set_target_properties(PdfPP.Win32 PROPERTIES OUTPUT_NAME "PdfPP.Win32")

    if(MSVC)
        target_compile_options(PdfPPModernWin32 PRIVATE /W4 /permissive- /utf-8 /FS)
        target_compile_options(PdfPP.Win32 PRIVATE /W4 /permissive- /utf-8 /FS)
    endif()
endif()

if(PDFPP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
