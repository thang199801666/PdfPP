#pragma once
#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Writer/PdfWriter.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>
#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>
#include <CPPPdf/Fonts/PdfType1Font.hpp>
#include <CPPPdf/Fonts/PdfCff.hpp>
#include <CPPPdf/Fonts/PdfType3Font.hpp>
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <cstddef>
#include <set>
#include <unordered_map>
#include <CPPPdf/Rendering/PdfBitmap.hpp>

namespace CPPPdf::Internal {
struct PdfWriterImage {
    PdfImage image;
    std::string resourceName;
};
enum class PdfWriterColorSpaceKind { IccBased, Separation, DeviceN };
struct PdfWriterColorSpace {
    PdfWriterColorSpaceKind kind{PdfWriterColorSpaceKind::IccBased};
    std::string resourceName;
    PdfDeviceColorSpace alternate{PdfDeviceColorSpace::Rgb};
    std::uint8_t components{3U};
    std::vector<std::byte> profileBytes;
    std::vector<std::string> colorantNames;
    std::vector<double> c0;
    std::vector<double> c1;
    double exponent{1.0};
    std::string tintTransformProgram;
};
enum class PdfWriterMeshShadingKind { FreeForm, Lattice, CoonsPatch, TensorProductPatch };
struct PdfWriterPatchMesh {
    std::vector<PdfPoint> controlPoints;
    std::array<std::vector<double>, 4> cornerColors;
};
struct PdfWriterMeshShading {
    PdfWriterMeshShadingKind kind{PdfWriterMeshShadingKind::FreeForm};
    std::string resourceName;
    PdfDeviceColorSpace colorSpace{PdfDeviceColorSpace::Rgb};
    std::vector<PdfMeshVertex> vertices;
    std::vector<PdfWriterPatchMesh> patches;
    std::size_t verticesPerRow{};
    bool antiAlias{true};
};
struct PdfWriterExtGState {
    double strokeOpacity{1.0};
    double fillOpacity{1.0};
    PdfBlendMode blendMode{PdfBlendMode::SourceOver};
    std::string resourceName;
};
struct PdfWriterEmbeddedFont {
    PdfTrueTypeFont font;
    std::string resourceName;
    std::vector<std::pair<std::uint32_t, std::uint16_t>> usedMappings;
    bool vertical{};
};
struct PdfWriterType1Font {
    PdfType1Font font;
    std::string resourceName;
};
struct PdfWriterCffFont {
    PdfCffFont font;
    std::string resourceName;
};
struct PdfWriterType3Font {
    PdfType3Font font;
    std::string resourceName;
};
struct PdfWriterBookmark {
    std::string title;
    std::size_t pageIndex{};
    std::optional<std::size_t> parentIndex{};
    PdfBookmarkDestinationType destinationType{PdfBookmarkDestinationType::FitPage};
    std::optional<double> left{};
    std::optional<double> top{};
    std::optional<double> zoom{};
    bool open{true};
    bool bold{false};
    bool italic{false};
    std::optional<PdfColor> color{};
};
struct PdfWriterNamedDestination {
    std::string name;
    std::size_t pageIndex{};
    PdfDestinationType destinationType{PdfDestinationType::FitPage};
    std::optional<double> left{};
    std::optional<double> top{};
    std::optional<double> zoom{};
};
enum class PdfWriterLinkKind { NamedDestination, Uri, Remote, Launch };
struct PdfWriterLink {
    PdfWriterLinkKind kind{PdfWriterLinkKind::NamedDestination};
    std::string target;
    PdfLinkOptions options{};
    std::string destination;
};

struct PdfWriterEmbeddedFile {
    std::string name;
    std::vector<std::byte> bytes;
    PdfEmbeddedFileOptions options{};
};
struct PdfWriterFileAttachment {
    std::string embeddedFileName;
    PdfFileAttachmentOptions options{};
};

struct PdfWriterPageLabel {
    std::size_t pageIndex{};
    PdfPageLabelOptions options{};
};
struct PdfWriterOcg {
    std::string name;
    bool visible{true};
};
struct PdfWriterMarkedContent {
    std::uint32_t mcid{};
    std::string role{"Span"};
    std::string alternativeText;
    std::string actualText;
    std::string language;
    std::string title;
    std::string expandedText;
    std::string identifier;
    PdfStructureAttributes attributes;
    std::optional<std::size_t> parentIndex;
    std::vector<std::size_t> childIndices;
};
struct PdfWriterCanvasGraphicsState {
    double strokeOpacity{1.0};
    double fillOpacity{1.0};
    PdfBlendMode blendMode{PdfBlendMode::SourceOver};
};
struct PdfWriterPage {
    PdfRectangle mediaBox{0,0,595,842};
    std::optional<PdfRectangle> cropBox;
    int rotation{0};
    std::string content;
    std::string fontName{"Helvetica"};
    double currentFontSize{0.0};
    bool verticalWriting{false};
    std::vector<std::size_t> imageIndices;
    std::vector<std::size_t> extGStateIndices;
    std::vector<std::size_t> patternIndices;
    std::vector<std::size_t> colorSpaceIndices;
    std::vector<std::size_t> shadingIndices;
    std::vector<std::size_t> embeddedFontIndices;
    std::vector<std::size_t> type1FontIndices;
    std::vector<std::size_t> cffFontIndices;
    std::vector<std::size_t> type3FontIndices;
    std::optional<std::size_t> activeEmbeddedFontIndex;
    std::optional<std::size_t> activeType1FontIndex;
    std::optional<std::size_t> activeCffFontIndex;
    std::optional<std::size_t> activeType3FontIndex;
    std::vector<PdfWriterLink> links;
    std::vector<PdfWriterFileAttachment> fileAttachments;
    std::set<std::string> ocgResources;
    std::vector<PdfWriterMarkedContent> markedContents;
    std::vector<std::optional<std::size_t>> markedContentStack;
    std::size_t openMarkedContentDepth{};
    PdfWriterCanvasGraphicsState graphicsState{};
    std::vector<PdfWriterCanvasGraphicsState> graphicsStateStack;
};
struct PdfWriterState {
    PdfDocumentInfo documentInfo{};
    PdfViewerPreferences viewerPreferences{};
    std::optional<PdfDestinationOptions> openAction{};
    bool tagged{false};
    std::vector<std::pair<std::string, std::string>> taggedRoleMap;
    std::string taggedAltText;
    bool writeXmpMetadata{false};
    std::optional<PdfConformanceProfile> pdfAProfile;
    std::optional<PdfConformanceProfile> pdfUaProfile;
    bool enforceConformance{true};
    std::vector<std::byte> pdfAOutputIntentIcc;
    std::string pdfAOutputConditionIdentifier{"sRGB IEC61966-2.1"};
    std::string language;
    std::vector<PdfWriterPageLabel> pageLabels;
    std::vector<PdfWriterPage> pages;
    std::vector<PdfWriterOcg> ocgs;
    std::vector<PdfWriterBookmark> bookmarks;
    std::vector<PdfWriterNamedDestination> namedDestinations;
    std::vector<PdfWriterEmbeddedFile> embeddedFiles;
    std::optional<PdfPortfolioOptions> portfolio;
    std::vector<PdfWriterImage> images;
    // Content hash buckets used by PdfCanvas::DrawImage so identical images
    // referenced on multiple pages share one XObject and one encoded stream.
    std::unordered_map<std::size_t, std::vector<std::size_t>> imageCache;
    std::vector<PdfWriterExtGState> extGStates;
    std::vector<PdfWriterColorSpace> colorSpaces;
    std::vector<PdfWriterMeshShading> meshShadings;
    std::vector<PdfWriterEmbeddedFont> embeddedFonts;
    std::vector<PdfWriterType1Font> type1Fonts;
    std::vector<PdfWriterCffFont> cffFonts;
    std::vector<PdfWriterType3Font> type3Fonts;
    struct PdfWriterTilingPattern {
        PdfTilingPatternOptions options;
    };
    std::vector<PdfWriterTilingPattern> tilingPatterns;
    std::optional<PdfEncryptionOptions> encryption;
};
}
