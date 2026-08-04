#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/IO/PdfReader.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace CPPPdf {

enum class PdfAnnotationType {
    Highlight,
    Underline,
    StrikeOut,
    TextNote,
    Link,
    Line,
    FileAttachment,
    FreeText,
    Ink,
    Polygon,
    Polyline,
    Square,
    Circle,
    Stamp,
    Popup
};

enum class PdfLineEndStyle {
    None,
    Square,
    Circle,
    Diamond,
    OpenArrow,
    ClosedArrow,
    Butt,
    ROpenArrow,
    RClosedArrow,
    Slash
};

// Reply relationship between annotations (PDF 32000-1 §12.5.6.6).
enum class PdfAnnotationReplyType {
    None,
    R, // The annotation is a reply to /IRT.
    Group, // The annotation groups replies to /IRT.
    Reply // Default; the annotation replies to the previous thread.
};

struct PdfAnnotationColor final {
    double red{1.0};
    double green{1.0};
    double blue{0.0};
};

struct PdfAnnotation final {
    std::size_t pageIndex{};
    PdfAnnotationType type{PdfAnnotationType::Highlight};
    PdfRectangle rectangle{};
    std::vector<PdfRectangle> quadrilaterals;
    // Ink: one stroke per entry, each a list of (x, y) points in PDF space.
    std::vector<std::vector<PdfPoint>> inkPaths;
    // Polygon / Polyline: explicit vertices (fallback to rectangle corners).
    std::vector<PdfPoint> vertices;
    PdfAnnotationColor color{};
    PdfAnnotationColor interiorColor{0.0, 0.0, 0.0};
    double opacity{1.0};
    std::string contents;
    std::string title;
    std::string uri;
    bool open{false};
    double borderWidth{0.0};
    PdfLineEndStyle lineStart{PdfLineEndStyle::None};
    PdfLineEndStyle lineEnd{PdfLineEndStyle::None};
    double rotationDegrees{0.0};
    // FreeText annotation-specific default appearance text alignment.
    int textAlignment{0};
    // Stamp name (e.g. /Approved, /Draft, /Confidential).
    std::string stampName;
    // Existing embedded-file name used by FileAttachment annotations.
    std::string attachmentName;
    // Reply thread: set inReplyTo to the 1-based annotation index on the same
    // page to make this annotation a reply to that annotation.
    std::size_t inReplyTo{0U};
    PdfAnnotationReplyType replyType{PdfAnnotationReplyType::None};
    // When true, a linked /Popup annotation is added automatically (TextNote,
    // FreeText, and reply annotations).
    bool hasPopup{false};
};

struct PdfAnnotationEditResult final {
    std::filesystem::path outputPath;
    std::size_t annotationCount{};
    std::size_t modifiedPageCount{};
};

struct PdfAnnotationRemovalResult final {
    std::filesystem::path outputPath;
    std::size_t removedCount{};
    std::size_t modifiedPageCount{};
};

struct PdfAnnotationAppearanceResult final {
    std::filesystem::path outputPath;
    std::size_t appearanceCount{};
    std::size_t modifiedPageCount{};
};

struct PdfAnnotationFlattenResult final {
    std::filesystem::path outputPath;
    std::size_t flattenedCount{};
    std::size_t removedCount{};
    std::size_t modifiedPageCount{};
};

class PdfAnnotationEditor final {
public:
    [[nodiscard]] static PdfAnnotationEditResult AddAnnotations(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<PdfAnnotation>& annotations,
        const PdfReaderOptions& readerOptions = {});

    // Generates a deterministic /AP appearance stream for every annotation on
    // the page that can be drawn natively (FreeText, Square, Circle, Polygon,
    // Polyline, Ink, Stamp, Highlight, Underline, StrikeOut, Text, Link).
    // The appearance uses PDF graphics operators so viewers can render the
    // annotation without interactive logic.
    [[nodiscard]] static PdfAnnotationAppearanceResult GenerateAppearances(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t pageIndex,
        const PdfReaderOptions& readerOptions = {});

    // Flattens annotations into the page content stream and removes them from
    // /Annots. Every annotation that has a generated /AP /N appearance (or that
    // can be drawn natively) is burned into the page, so it renders identically
    // in viewers without interactive annotation support. `typeFilter` limits
    // flattening to the listed subtypes (e.g. "/Square"); empty flattens all.
    [[nodiscard]] static PdfAnnotationFlattenResult FlattenAnnotations(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t pageIndex,
        const std::vector<std::string>& typeFilter = {},
        const PdfReaderOptions& readerOptions = {});

    // Removes annotations from a page. `typeFilter` (when non-empty) limits
    // removal to matching subtypes (e.g. "/Highlight", "/FreeText"); an empty
    // filter removes every annotation on the page.
    [[nodiscard]] static PdfAnnotationRemovalResult RemoveAnnotations(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t pageIndex,
        const std::vector<std::string>& typeFilter = {},
        const PdfReaderOptions& readerOptions = {});

    // Updates the /Contents (and /T title) of every annotation on a page whose
    // subtype matches `annotationType`. Returns the number of updated fields.
    [[nodiscard]] static std::size_t UpdateAnnotationContents(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t pageIndex,
        PdfAnnotationType annotationType,
        const std::string& newContents,
        const std::string& newTitle = {},
        const PdfReaderOptions& readerOptions = {});
};

} // namespace CPPPdf
