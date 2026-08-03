add_executable(PdfPP.UnitTests
    Unit/CoreTests.cpp
    Unit/ObjectAndRenderingTests.cpp
    Unit/CffAndFilterTests.cpp
    Unit/ContentTextFontTests.cpp
    Unit/PdfAValidationTests.cpp
    Unit/ReaderIntegrationTests.cpp
    Unit/WriterIntegrationTests.cpp
    Unit/ApiCoverageTests.cpp
    Unit/FeatureUnitTests.cpp
    Unit/SecurityTests.cpp
)
target_link_libraries(PdfPP.UnitTests PRIVATE CPPPdf::Core)
target_compile_features(PdfPP.UnitTests PRIVATE cxx_std_20)
target_include_directories(PdfPP.UnitTests PRIVATE ${PROJECT_SOURCE_DIR}/src/Pdf++.Core/src)
add_test(NAME PdfPP.UnitTests COMMAND PdfPP.UnitTests)
