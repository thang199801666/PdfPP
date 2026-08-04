cmake_minimum_required(VERSION 3.24)

project(PdfPP VERSION 0.129.0 DESCRIPTION "Modern C++20 PDF library" LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(PDFPP_BUILD_TESTS "Build Pdf++ tests" ${BUILD_TESTING})
option(PDFPP_BUILD_FUZZERS "Build Pdf++ libFuzzer targets (Clang only)" OFF)
option(PDFPP_BUILD_VALIDATION_TOOLS "Build Pdf++ cross-engine validation tools" OFF)
option(PDFPP_INSTALL "Generate install and package-export rules" ON)
option(PDFPP_WARNINGS_AS_ERRORS "Treat Pdf++ warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/zlib EXCLUDE_FROM_ALL)

set(PDFPP_PUBLIC_HEADERS
    src/Pdf++.Core/include/CPPPdf/CPPPdf.h
    src/Pdf++.Core/include/CPPPdf/Api.hpp
    src/Pdf++.Core/include/CPPPdf/Version.hpp
src/Pdf++.Core/include/CPPPdf/pdfpp_c.h
    src/Pdf++.Core/include/CPPPdf/Types.hpp
    src/Pdf++.Core/include/CPPPdf/Reader.hpp
    src/Pdf++.Core/include/CPPPdf/Document.hpp
    src/Pdf++.Core/include/CPPPdf/Text.hpp
    src/Pdf++.Core/include/CPPPdf/Graphics.hpp
    src/Pdf++.Core/include/CPPPdf/Writer.hpp
    src/Pdf++.Core/include/CPPPdf/Rendering.hpp
    src/Pdf++.Core/include/CPPPdf/Annotations.hpp
    src/Pdf++.Core/include/CPPPdf/Forms.hpp
    src/Pdf++.Core/include/CPPPdf/PdfDocument.hpp
    src/Pdf++.Core/include/CPPPdf/PdfError.hpp
    src/Pdf++.Core/include/CPPPdf/PdfPage.hpp
    src/Pdf++.Core/include/CPPPdf/PdfReader.hpp
    src/Pdf++.Core/include/CPPPdf/Core/PdfTypes.hpp
    src/Pdf++.Core/include/CPPPdf/Document/PdfDocument.hpp
    src/Pdf++.Core/include/CPPPdf/Document/PdfPage.hpp
    src/Pdf++.Core/include/CPPPdf/Document/PdfPageEditor.hpp
    src/Pdf++.Core/include/CPPPdf/Filters/PdfFilterPipeline.hpp
    src/Pdf++.Core/include/CPPPdf/IO/PdfReader.hpp
    src/Pdf++.Core/include/CPPPdf/Objects/PdfObject.hpp
    src/Pdf++.Core/include/CPPPdf/Fonts/PdfFont.hpp
    src/Pdf++.Core/include/CPPPdf/Fonts/PdfFontResource.hpp
    src/Pdf++.Core/include/CPPPdf/Fonts/PdfTrueTypeFont.hpp
src/Pdf++.Core/include/CPPPdf/Fonts/PdfType1Font.hpp
    src/Pdf++.Core/include/CPPPdf/Fonts/PdfCff.hpp
    src/Pdf++.Core/include/CPPPdf/Content/PdfContentProcessor.hpp
    src/Pdf++.Core/include/CPPPdf/Writer/PdfWriter.hpp
src/Pdf++.Core/include/CPPPdf/Security/PdfSecurity.hpp
src/Pdf++.Core/include/CPPPdf/Security/PdfSignature.hpp
src/Pdf++.Core/include/CPPPdf/Security/PdfCms.hpp
src/Pdf++.Core/include/CPPPdf/Security/PdfPades.hpp
    src/Pdf++.Core/include/CPPPdf/Graphics/PdfCanvas.hpp
    src/Pdf++.Core/include/CPPPdf/Graphics/PdfImage.hpp
    src/Pdf++.Core/include/CPPPdf/Graphics/PdfFunction.hpp
    src/Pdf++.Core/include/CPPPdf/Rendering/PdfBitmap.hpp
    src/Pdf++.Core/include/CPPPdf/Rendering/PdfDisplayList.hpp
    src/Pdf++.Core/include/CPPPdf/Rendering/PdfShading.hpp
    src/Pdf++.Core/include/CPPPdf/Rendering/PdfTransparencyGroup.h
    src/Pdf++.Core/include/CPPPdf/Rendering/PdfPageRenderer.hpp
src/Pdf++.Core/include/CPPPdf/Text/PdfTextExtractor.hpp
src/Pdf++.Core/include/CPPPdf/Text/PdfTextSearch.hpp
src/Pdf++.Core/include/CPPPdf/Text/PdfTextLayout.hpp
    src/Pdf++.Core/include/CPPPdf/Text/PdfTextDocumentIndex.hpp
    src/Pdf++.Core/include/CPPPdf/Annotations/PdfKeywordHighlighter.hpp
    src/Pdf++.Core/include/CPPPdf/Annotations/PdfAnnotationEditor.hpp
src/Pdf++.Core/include/CPPPdf/Annotations/PdfXfdf.hpp
    src/Pdf++.Core/include/CPPPdf/Document/PdfPageOrganizer.hpp
    src/Pdf++.Core/include/CPPPdf/Document/PdfPageImporter.hpp
src/Pdf++.Core/include/CPPPdf/Document/PdfDocumentLayout.hpp
src/Pdf++.Core/include/CPPPdf/Document/PdfRedactor.hpp
    src/Pdf++.Core/include/CPPPdf/Forms/PdfAcroForm.hpp
    src/Pdf++.Core/include/CPPPdf/Validation/PdfConformance.hpp)

set(PDFPP_CORE_SOURCES
    src/Pdf++.Core/src/Document/PdfDocument.cpp
    src/Pdf++.Core/src/Document/PdfObjectResolver.cpp
    src/Pdf++.Core/src/Document/PdfPageEditor.cpp
    src/Pdf++.Core/src/Document/PdfPageOrganizer.cpp
    src/Pdf++.Core/src/Document/PdfPageImporter.cpp
src/Pdf++.Core/src/Document/PdfDocumentLayout.cpp
src/Pdf++.Core/src/Document/PdfRedactor.cpp
    src/Pdf++.Core/src/Internal/Writer/PdfObjectSerializer.cpp
    src/Pdf++.Core/src/Internal/Writer/PdfObjectSerializer.hpp
src/Pdf++.Core/src/Internal/Graphics/PdfJpegEncoder.hpp
    src/Pdf++.Core/src/Internal/Writer/PdfIncrementalWriter.hpp
    src/Pdf++.Core/src/Writer/PdfIncrementalWriter.cpp
    src/Pdf++.Core/src/Filters/PdfFilterPipeline.cpp
    src/Pdf++.Core/src/IO/PdfInputSource.cpp
    src/Pdf++.Core/src/Objects/PdfObject.cpp
    src/Pdf++.Core/src/Parsing/PdfObjectParser.cpp
    src/Pdf++.Core/src/Internal/Parsing/PdfObjectParser.hpp
    src/Pdf++.Core/src/Internal/Document/PdfObjectResolver.hpp
    src/Pdf++.Core/src/Fonts/PdfFont.cpp
    src/Pdf++.Core/src/Fonts/PdfFontResource.cpp
    src/Pdf++.Core/src/Fonts/PdfTrueTypeFont.cpp
src/Pdf++.Core/src/Fonts/PdfType1Font.cpp
src/Pdf++.Core/src/Internal/Graphics/PdfJpegEncoder.cpp
    src/Pdf++.Core/src/Fonts/PdfCff.cpp
    src/Pdf++.Core/src/Content/PdfContentProcessor.cpp
    src/Pdf++.Core/src/Writer/PdfWriter.cpp
    src/Pdf++.Core/src/Writer/PdfWriterState.hpp
    src/Pdf++.Core/src/Security/PdfStandardSecurity.cpp
    src/Pdf++.Core/src/Security/PdfPasswordManager.cpp
    src/Pdf++.Core/src/Security/PdfSignature.cpp
src/Pdf++.Core/src/Security/PdfCms.cpp
src/Pdf++.Core/src/Security/PdfPades.cpp
src/Pdf++.Core/src/Api/pdfpp_c.cpp
    src/Pdf++.Core/src/Internal/Security/PdfStandardSecurity.hpp
    src/Pdf++.Core/src/Graphics/PdfCanvas.cpp
    src/Pdf++.Core/src/Graphics/PdfImage.cpp
    src/Pdf++.Core/src/Graphics/PdfFunction.cpp
    src/Pdf++.Core/src/Rendering/PdfBitmap.cpp
    src/Pdf++.Core/src/Rendering/PdfShading.cpp
    src/Pdf++.Core/src/Rendering/PdfPageRenderer.cpp
src/Pdf++.Core/src/Text/PdfTextExtractor.cpp
src/Pdf++.Core/src/Text/PdfTextSearch.cpp
src/Pdf++.Core/src/Text/PdfTextLayout.cpp
    src/Pdf++.Core/src/Text/PdfTextDocumentIndex.cpp
    src/Pdf++.Core/src/Annotations/PdfKeywordHighlighter.cpp
    src/Pdf++.Core/src/Annotations/PdfAnnotationEditor.cpp
src/Pdf++.Core/src/Annotations/PdfXfdf.cpp
    src/Pdf++.Core/src/Forms/PdfAcroForm.cpp
    src/Pdf++.Core/src/Validation/PdfConformance.cpp)

add_library(PdfPP.Core STATIC ${PDFPP_PUBLIC_HEADERS} ${PDFPP_CORE_SOURCES})
add_library(CPPPdf::Core ALIAS PdfPP.Core)
set_target_properties(PdfPP.Core PROPERTIES
    OUTPUT_NAME "Pdf++.Core"
    EXPORT_NAME Core
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    DEBUG_POSTFIX d)

target_compile_features(PdfPP.Core PUBLIC cxx_std_20)
target_include_directories(PdfPP.Core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/Pdf++.Core/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Pdf++.Core/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
target_link_libraries(PdfPP.Core PRIVATE zlibstatic)
if(WIN32)
    target_link_libraries(PdfPP.Core PRIVATE bcrypt)
endif()

option(PDFPP_BUILD_NATIVE_BRIDGE "Build the C ABI bridge for desktop UI clients" OFF)
if(PDFPP_BUILD_NATIVE_BRIDGE)
    add_library(PdfPP.Native SHARED src/PdfPP.Native/PdfPP.Native.cpp)
    target_link_libraries(PdfPP.Native PRIVATE CPPPdf::Core)
    if(WIN32)
        target_link_libraries(PdfPP.Native PRIVATE windowsapp gdiplus)
    endif()
    target_compile_features(PdfPP.Native PRIVATE cxx_std_20)
    set_target_properties(PdfPP.Native PROPERTIES OUTPUT_NAME "PdfPP.Native")
    if(MSVC)
        target_compile_options(PdfPP.Native PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
endif()

target_compile_definitions(PdfPP.Core PRIVATE CPPPDF_BUILDING_CORE=1)
if(MSVC)
    target_compile_options(PdfPP.Core PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    if(PDFPP_WARNINGS_AS_ERRORS)
        target_compile_options(PdfPP.Core PRIVATE /WX)
    endif()
else()
    target_compile_options(PdfPP.Core PRIVATE -Wall -Wextra -Wpedantic)
    if(PDFPP_WARNINGS_AS_ERRORS)
        target_compile_options(PdfPP.Core PRIVATE -Werror)
    endif()
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src/Pdf++.Core/include" PREFIX "Public API" FILES ${PDFPP_PUBLIC_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src/Pdf++.Core/src" PREFIX "Implementation" FILES ${PDFPP_CORE_SOURCES})

if(PDFPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PDFPP_BUILD_FUZZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(PDFPP_FUZZ_TARGETS
        PdfPP.FuzzReader
        PdfPP.FuzzContent
        PdfPP.FuzzFilter
        PdfPP.FuzzCffFont
        PdfPP.FuzzTrueTypeFont)
    add_executable(PdfPP.FuzzReader tools/fuzz/PdfReaderFuzz.cpp)
    add_executable(PdfPP.FuzzContent tools/fuzz/PdfContentFuzz.cpp)
    add_executable(PdfPP.FuzzFilter tools/fuzz/PdfFilterFuzz.cpp)
    add_executable(PdfPP.FuzzCffFont tools/fuzz/PdfCffFontFuzz.cpp)
    add_executable(PdfPP.FuzzTrueTypeFont tools/fuzz/PdfTrueTypeFontFuzz.cpp)
    foreach(target ${PDFPP_FUZZ_TARGETS})
        target_link_libraries(${target} PRIVATE CPPPdf::Core)
        target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_compile_features(${target} PRIVATE cxx_std_20)
    endforeach()
endif()

if(PDFPP_BUILD_VALIDATION_TOOLS)
    add_executable(PdfPP.Inspect tools/validation/PdfPPInspect.cpp)
    target_link_libraries(PdfPP.Inspect PRIVATE CPPPdf::Core)
    target_compile_features(PdfPP.Inspect PRIVATE cxx_std_20)
    add_executable(PdfPP.WriteValidation tools/validation/PdfPPWriteValidation.cpp)
    target_link_libraries(PdfPP.WriteValidation PRIVATE CPPPdf::Core)
    target_compile_features(PdfPP.WriteValidation PRIVATE cxx_std_20)
    add_executable(PdfPP.GenCorpus tools/validation/PdfPPGenCorpus.cpp)
    target_link_libraries(PdfPP.GenCorpus PRIVATE CPPPdf::Core)
    target_compile_features(PdfPP.GenCorpus PRIVATE cxx_std_20)
endif()

if(PDFPP_INSTALL)
    install(TARGETS PdfPP.Core
        EXPORT PdfPPTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY src/Pdf++.Core/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT PdfPPTargets
        FILE PdfPPTargets.cmake
        NAMESPACE CPPPdf::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PdfPP)

    configure_package_config_file(
        cmake/PdfPPConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/PdfPPConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PdfPP)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/PdfPPConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/PdfPPConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/PdfPPConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PdfPP)
endif()
