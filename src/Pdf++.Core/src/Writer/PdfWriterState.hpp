#pragma once
#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Writer/PdfWriter.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>
#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <cstddef>
#include <set>
#include <CPPPdf/Rendering/PdfBitmap.hpp>

namespace CPPPdf::Internal {
struct PdfWriterImage {
    PdfImage image;
    std::string resourceName;
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
enum class PdfWriterLinkKind { NamedDestination, Uri };
struct PdfWriterLink {
    PdfWriterLinkKind kind{PdfWriterLinkKind::NamedDestination};
    std::string target;
    PdfLinkOptions options{};
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
struct PdfWriterPage {
    PdfRectangle mediaBox{0,0,595,842};
    std::string content;
    std::string fontName{"Helvetica"};
    double currentFontSize{0.0};
    bool verticalWriting{false};
    std::vector<std::size_t> imageIndices;
    std::vector<std::size_t> extGStateIndices;
    std::vector<std::size_t> embeddedFontIndices;
    std::optional<std::size_t> activeEmbeddedFontIndex;
    std::vector<PdfWriterLink> links;
    std::vector<PdfWriterFileAttachment> fileAttachments;
    std::set<std::string> ocgResources;
};
struct PdfWriterState {
    PdfDocumentInfo documentInfo{};
    PdfViewerPreferences viewerPreferences{};
    std::optional<PdfDestinationOptions> openAction{};
    std::vector<PdfWriterPageLabel> pageLabels;
    std::vector<PdfWriterPage> pages;
    std::vector<PdfWriterOcg> ocgs;
    std::vector<PdfWriterBookmark> bookmarks;
    std::vector<PdfWriterNamedDestination> namedDestinations;
    std::vector<PdfWriterEmbeddedFile> embeddedFiles;
    std::optional<PdfPortfolioOptions> portfolio;
    std::vector<PdfWriterImage> images;
    std::vector<PdfWriterExtGState> extGStates;
    std::vector<PdfWriterEmbeddedFont> embeddedFonts;
    std::optional<PdfEncryptionOptions> encryption;
};
}
