add_executable(pdfpp_security_smoke security_smoke.cpp)
target_link_libraries(pdfpp_security_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_security_smoke PRIVATE cxx_std_20)
add_test(NAME pdfpp.security.smoke COMMAND pdfpp_security_smoke)
set_tests_properties(pdfpp.security.smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

find_program(PDFPP_PDFINFO_EXECUTABLE pdfinfo)
if(PDFPP_PDFINFO_EXECUTABLE)
    add_test(NAME pdfpp.security.poppler
        COMMAND ${PDFPP_PDFINFO_EXECUTABLE} -upw user-password aes256_r6_incremental_smoke.pdf)
    set_tests_properties(pdfpp.security.poppler PROPERTIES
        DEPENDS pdfpp.security.smoke
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

add_executable(pdfpp_incremental_smoke incremental_smoke.cpp)
target_link_libraries(pdfpp_incremental_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_incremental_smoke PRIVATE cxx_std_20)
add_test(NAME pdfpp.incremental.smoke COMMAND pdfpp_incremental_smoke)

add_executable(pdfpp_cms_smoke cms_smoke.cpp)
target_link_libraries(pdfpp_cms_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_cms_smoke PRIVATE cxx_std_20)
target_compile_definitions(pdfpp_cms_smoke PRIVATE
    PDFPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME pdfpp.cms.smoke COMMAND pdfpp_cms_smoke)
set_tests_properties(pdfpp.cms.smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

find_program(PDFPP_OPENSSL_EXECUTABLE openssl)
if(PDFPP_OPENSSL_EXECUTABLE)
    add_test(NAME pdfpp.cms.openssl
        COMMAND ${PDFPP_OPENSSL_EXECUTABLE} cms -verify -binary -inform DER
            -in cms_smoke.der -content cms_smoke_content.bin -noverify
            -out cms_smoke_verified.bin)
    set_tests_properties(pdfpp.cms.openssl PROPERTIES
        DEPENDS pdfpp.cms.smoke
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

add_executable(pdfpp_pdfa_smoke pdfa_smoke.cpp)
target_link_libraries(pdfpp_pdfa_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_pdfa_smoke PRIVATE cxx_std_20)
add_test(NAME pdfpp.pdfa.smoke COMMAND pdfpp_pdfa_smoke)

add_executable(pdfpp_tagged_smoke tagged_smoke.cpp)
target_link_libraries(pdfpp_tagged_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_tagged_smoke PRIVATE cxx_std_20)
add_test(NAME pdfpp.tagged.smoke COMMAND pdfpp_tagged_smoke)

add_executable(pdfpp_forms_annotations_smoke forms_annotations_smoke.cpp)
target_link_libraries(pdfpp_forms_annotations_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_forms_annotations_smoke PRIVATE cxx_std_20)
add_test(NAME pdfpp.forms_annotations.smoke COMMAND pdfpp_forms_annotations_smoke)

add_executable(pdfpp_image_layout_smoke image_layout_smoke.cpp)
target_link_libraries(pdfpp_image_layout_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_image_layout_smoke PRIVATE cxx_std_20)
target_compile_definitions(pdfpp_image_layout_smoke PRIVATE
    PDFPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME pdfpp.image_layout.smoke COMMAND pdfpp_image_layout_smoke)
set_tests_properties(pdfpp.image_layout.smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

find_program(PDFPP_IDENTIFY_EXECUTABLE identify)
if(PDFPP_IDENTIFY_EXECUTABLE)
    add_test(NAME pdfpp.jpeg.identify
        COMMAND ${PDFPP_IDENTIFY_EXECUTABLE} image_smoke.jpg)
    set_tests_properties(pdfpp.jpeg.identify PROPERTIES
        DEPENDS pdfpp.image_layout.smoke
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

find_program(PDFPP_PDFTOPPM_EXECUTABLE pdftoppm)
if(PDFPP_PDFTOPPM_EXECUTABLE)
    add_test(NAME pdfpp.jpeg.poppler_render
        COMMAND ${PDFPP_PDFTOPPM_EXECUTABLE} -f 1 -singlefile -png jpeg_smoke.pdf jpeg_smoke_rendered)
    set_tests_properties(pdfpp.jpeg.poppler_render PROPERTIES
        DEPENDS pdfpp.image_layout.smoke
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

add_executable(pdfpp_graphics_advanced_smoke graphics_advanced_smoke.cpp)
target_link_libraries(pdfpp_graphics_advanced_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_graphics_advanced_smoke PRIVATE cxx_std_20)
add_test(NAME pdfpp.graphics_advanced.smoke COMMAND pdfpp_graphics_advanced_smoke)
set_tests_properties(pdfpp.graphics_advanced.smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

if(PDFPP_PDFTOPPM_EXECUTABLE)
    add_test(NAME pdfpp.graphics_advanced.poppler_render
        COMMAND ${PDFPP_PDFTOPPM_EXECUTABLE} -f 1 -singlefile -png
            graphics_advanced_smoke.pdf graphics_advanced_smoke_rendered)
    set_tests_properties(pdfpp.graphics_advanced.poppler_render PROPERTIES
        DEPENDS pdfpp.graphics_advanced.smoke
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

add_executable(pdfpp_font_mesh_ua_smoke font_mesh_ua_smoke.cpp)
target_link_libraries(pdfpp_font_mesh_ua_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_font_mesh_ua_smoke PRIVATE cxx_std_20)
add_test(NAME pdfpp.font_mesh_ua.smoke COMMAND pdfpp_font_mesh_ua_smoke)
set_tests_properties(pdfpp.font_mesh_ua.smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

if(PDFPP_PDFTOPPM_EXECUTABLE)
    add_test(NAME pdfpp.font_mesh_ua.poppler_render
        COMMAND ${PDFPP_PDFTOPPM_EXECUTABLE} -f 1 -singlefile -png
            font_mesh_ua_smoke.pdf font_mesh_ua_smoke_rendered)
    set_tests_properties(pdfpp.font_mesh_ua.poppler_render PROPERTIES
        DEPENDS pdfpp.font_mesh_ua.smoke
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

add_executable(pdfpp_patch_mesh_pattern_smoke patch_mesh_pattern_smoke.cpp)
target_link_libraries(pdfpp_patch_mesh_pattern_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_patch_mesh_pattern_smoke PRIVATE cxx_std_20)
add_test(NAME pdfpp.patch_mesh_pattern.smoke COMMAND pdfpp_patch_mesh_pattern_smoke)
set_tests_properties(pdfpp.patch_mesh_pattern.smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

if(PDFPP_PDFTOPPM_EXECUTABLE)
    add_test(NAME pdfpp.patch_mesh_pattern.poppler_render
        COMMAND ${PDFPP_PDFTOPPM_EXECUTABLE} -f 1 -singlefile -png
            patch_mesh_pattern_smoke.pdf patch_mesh_pattern_smoke_rendered)
    set_tests_properties(pdfpp.patch_mesh_pattern.poppler_render PROPERTIES
        DEPENDS pdfpp.patch_mesh_pattern.smoke
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

add_executable(pdfpp_pades_ltv_smoke pades_ltv_smoke.cpp)
target_link_libraries(pdfpp_pades_ltv_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_pades_ltv_smoke PRIVATE cxx_std_20)
add_test(NAME pdfpp.pades_ltv.smoke COMMAND pdfpp_pades_ltv_smoke)
set_tests_properties(pdfpp.pades_ltv.smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(pdfpp_pdfa_ua_conformance_smoke pdfa_ua_conformance_smoke.cpp)
target_link_libraries(pdfpp_pdfa_ua_conformance_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_pdfa_ua_conformance_smoke PRIVATE cxx_std_20)
target_compile_definitions(pdfpp_pdfa_ua_conformance_smoke PRIVATE
    PDFPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME pdfpp.pdfa_ua_conformance.smoke COMMAND pdfpp_pdfa_ua_conformance_smoke)
set_tests_properties(pdfpp.pdfa_ua_conformance.smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

find_program(PDFPP_VERAPDF_EXECUTABLE verapdf)
if(PDFPP_VERAPDF_EXECUTABLE)
    foreach(PDFPP_CONFORMANCE_FLAVOUR IN ITEMS 4f ua2 2a ua1)
        if(PDFPP_CONFORMANCE_FLAVOUR STREQUAL "4f" OR PDFPP_CONFORMANCE_FLAVOUR STREQUAL "ua2")
            set(PDFPP_CONFORMANCE_FILE pdfa4f_ua2_smoke.pdf)
        else()
            set(PDFPP_CONFORMANCE_FILE pdfa2a_ua1_smoke.pdf)
        endif()
        add_test(NAME pdfpp.verapdf.${PDFPP_CONFORMANCE_FLAVOUR}
            COMMAND ${PDFPP_VERAPDF_EXECUTABLE} --format text
                --flavour ${PDFPP_CONFORMANCE_FLAVOUR} ${PDFPP_CONFORMANCE_FILE})
        set_tests_properties(pdfpp.verapdf.${PDFPP_CONFORMANCE_FLAVOUR} PROPERTIES
            DEPENDS pdfpp.pdfa_ua_conformance.smoke
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endforeach()
endif()

add_executable(pdfpp_conformance_hardening_smoke conformance_hardening_smoke.cpp)
target_link_libraries(pdfpp_conformance_hardening_smoke PRIVATE PdfPP::Core)
target_compile_features(pdfpp_conformance_hardening_smoke PRIVATE cxx_std_20)
target_compile_definitions(pdfpp_conformance_hardening_smoke PRIVATE
    PDFPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME pdfpp.conformance_hardening.smoke COMMAND pdfpp_conformance_hardening_smoke)
set_tests_properties(pdfpp.conformance_hardening.smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

if(PDFPP_BUILD_UNIT_TESTS)
    add_executable(pdfpp_unit_tests
        Unit/CoreTests.cpp
        Unit/ObjectAndRenderingTests.cpp
        Unit/CffAndFilterTests.cpp
        Unit/ContentTextFontTests.cpp
        Unit/PdfAValidationTests.cpp
        Unit/ReaderIntegrationTests.cpp
        Unit/WriterIntegrationTests.cpp
        Unit/ApiCoverageTests.cpp
        Unit/FeatureUnitTests.cpp
        Unit/SecurityTests.cpp)
    target_link_libraries(pdfpp_unit_tests PRIVATE PdfPP::Core)
    target_compile_features(pdfpp_unit_tests PRIVATE cxx_std_20)
    target_include_directories(pdfpp_unit_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/Pdf++.Core/src
        ${PROJECT_SOURCE_DIR}/third_party/zlib)
    if(MSVC)
        target_compile_options(pdfpp_unit_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /FS)
    else()
        target_compile_options(pdfpp_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME pdfpp.unit.all COMMAND pdfpp_unit_tests)
    set_tests_properties(pdfpp.unit.all PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
