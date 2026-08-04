#pragma once
#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Graphics/PdfCanvas.hpp>
#include <CPPPdf/Security/PdfSecurity.hpp>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>
#include <optional>
#include <string>
#include <ostream>

namespace CPPPdf {
namespace Internal { struct PdfWriterState; }
struct PdfReaderOptions;
class PdfDocument;

enum class PdfSaveMode { Rewrite, Incremental };

struct PdfSaveOptions final {
    PdfSaveMode mode{PdfSaveMode::Rewrite};
    bool subsetTrueTypeFonts{true};
    // When true, the file cross-reference table is written as an /XRef
    // stream instead of a classic `xref` table. Modern readers prefer this
    // form and it is required before object streams can be emitted.
    bool writeXrefStream{true};
    // When true, small non-stream objects are packed into an /ObjStm and
    // referenced from the /XRef stream with compressed (type 2) entries.
    bool writeObjectStreams{false};
    // When true, Resave merges byte-identical stream objects (fonts, images,
    // content streams) into a single shared object. Safe because the merged
    // objects carry no positional identity.
    bool deduplicateObjects{false};
    // When true, JPEG (DCT) sources keep their /DCTDecode encoding on write
    // instead of being re-compressed with Flate. Already-DCT/JPX/CCITT images
    // always keep their native encoding.
    bool preserveImageEncodings{true};
};

using PdfStampPoint = PdfPoint;

enum class PdfLayerVisibility { Visible, Hidden, VisibleIfScreen, HiddenIfScreen };

// Optional content group (PDF layer): a named group whose visibility can be
// toggled by viewers. Content streams mark drawing operations with the group
// via the /OC property in the page resources' /Properties dictionary.
struct PdfOcgOptions final {
    std::string name;
    // Initial visibility when no viewer preference is set (defaults to on).
    bool visible{true};
};

struct PdfLayerOptions final {
    // The layer (by name) this drawing belongs to. When non-empty the writer
    // wraps the drawing in /OC /PropertiesList marking and attaches the OCG to
    // the page resources.
    std::string layerName;
    // Visibility policy for the layer (PDF 32000-1 §8.11.3.2).
    PdfLayerVisibility visibility{PdfLayerVisibility::Visible};
    // When true the drawing is always emitted outside any /OC marking even if
    // a layer name is present (used for content that must never be hidden).
    bool alwaysVisible{false};
};

enum class PdfStampLayer { Background, Foreground };

enum class PdfStampHorizontalAlignment { Left, Center, Right };
enum class PdfStampVerticalAlignment { Bottom, Middle, Top };

struct PdfTextStampOptions final {
    std::string text;
    PdfStampPoint position{72,72};
    std::string fontName{"Helvetica"};
    double fontSize{12.0};
    PdfColor textColor{PdfColor::Black()};
    double opacity{1.0};
    double rotationDegrees{0.0};
    double padding{6.0};
    bool drawBackground{false};
    PdfColor backgroundColor{PdfColor::White()};
    bool drawBorder{false};
    PdfColor borderColor{PdfColor::Black()};
    double borderWidth{1.0};
    PdfStampLayer layer{PdfStampLayer::Foreground};
};

struct PdfWatermarkOptions final {
    std::string text{"CONFIDENTIAL"};
    std::string fontName{"Helvetica"};
    double fontSize{48.0};
    PdfColor color{PdfColor::Gray(0.5)};
    double opacity{0.25};
    double rotationDegrees{45.0};
    PdfStampHorizontalAlignment horizontalAlignment{PdfStampHorizontalAlignment::Center};
    PdfStampVerticalAlignment verticalAlignment{PdfStampVerticalAlignment::Middle};
    PdfStampPoint offset{};
    PdfStampLayer layer{PdfStampLayer::Foreground};
};



enum class PdfPageLayout { Default, SinglePage, OneColumn, TwoColumnLeft, TwoColumnRight, TwoPageLeft, TwoPageRight };
enum class PdfPageMode { Default, UseNone, UseOutlines, UseThumbs, FullScreen, UseOptionalContent, UseAttachments };
enum class PdfReadingDirection { LeftToRight, RightToLeft };
enum class PdfPrintScaling { AppDefault, None };
enum class PdfDuplexMode { Default, Simplex, DuplexFlipShortEdge, DuplexFlipLongEdge };

struct PdfViewerPreferences final {
    PdfPageLayout pageLayout{PdfPageLayout::Default};
    PdfPageMode pageMode{PdfPageMode::Default};
    PdfReadingDirection readingDirection{PdfReadingDirection::LeftToRight};
    bool hideToolbar{false};
    bool hideMenuBar{false};
    bool hideWindowUi{false};
    bool fitWindow{false};
    bool centerWindow{false};
    bool displayDocumentTitle{false};
    PdfPageMode nonFullScreenPageMode{PdfPageMode::UseNone};
    PdfPrintScaling printScaling{PdfPrintScaling::AppDefault};
    PdfDuplexMode duplex{PdfDuplexMode::Default};
    bool pickTrayByPdfSize{false};
    unsigned int numberOfCopies{1};
};

// A tiling pattern: a small content stream painted in a tiled grid across the
// fill (or stroke) area. Used for hatch patterns, grids, and textured fills.
struct PdfTilingPatternOptions final {
    std::string name;      // resource name, e.g. "P1" (used without the '/').
    std::string content;   // content stream operators (e.g. "0 0 4 4 re f").
    PdfRectangle bbox{};   // bounding box of one tile.
    double xStep{0.0};     // horizontal tile spacing (<=0 means bbox width).
    double yStep{0.0};     // vertical tile spacing (<=0 means bbox height).
    bool paintTypeColor{true}; // 1 = colored, 0 = uncolored.
    bool horizontal{false};
    bool vertical{false};
};

enum class PdfPageLabelStyle { Decimal, UpperRoman, LowerRoman, UpperLetters, LowerLetters };

struct PdfPageLabelOptions final {
    PdfPageLabelStyle style{PdfPageLabelStyle::Decimal};
    std::string prefix;
    unsigned int startNumber{1};
};

enum class PdfBookmarkDestinationType { FitPage, FitWidth, XYZ };

using PdfDestinationType = PdfBookmarkDestinationType;

struct PdfDestinationOptions final {
    std::size_t pageIndex{};
    PdfDestinationType destinationType{PdfDestinationType::FitPage};
    std::optional<double> left{};
    std::optional<double> top{};
    std::optional<double> zoom{};
};

struct PdfLinkOptions final {
    PdfRectangle rectangle{};
    bool drawBorder{false};
    PdfColor borderColor{PdfColor::Blue()};
    double borderWidth{1.0};
};

struct PdfBookmarkOptions final {
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


enum class PdfAssociatedFileRelationship { Unspecified, Source, Data, Alternative, Supplement, EncryptedPayload, FormData, Schema };
enum class PdfFileAttachmentIcon { Graph, Paperclip, PushPin, Tag };

// A PDF portfolio (collection) groups embedded files into a browsable shell.
struct PdfPortfolioOptions final {
    std::string title;
    // Initial view mode: /D (detail), /T (tiles), /H (hidden), /L (left).
    std::string view{"D"};
};

struct PdfEmbeddedFileOptions final {
    std::string description;
    std::string mimeType{"application/octet-stream"};
    std::string creationDate;
    std::string modificationDate;
    PdfAssociatedFileRelationship relationship{PdfAssociatedFileRelationship::Unspecified};
    bool associateWithDocument{true};
    bool compress{true};
};

struct PdfFileAttachmentOptions final {
    PdfRectangle rectangle{};
    PdfFileAttachmentIcon icon{PdfFileAttachmentIcon::PushPin};
    std::string contents;
};

struct PdfImageStampOptions final {
    PdfRectangle rectangle{};
    double opacity{1.0};
    bool drawBorder{false};
    PdfColor borderColor{PdfColor::Black()};
    double borderWidth{1.0};
    PdfStampLayer layer{PdfStampLayer::Foreground};
};

class PdfWriter final {
public:
    PdfWriter();
    ~PdfWriter();
    PdfWriter(PdfWriter&&) noexcept;
    PdfWriter& operator=(PdfWriter&&) noexcept;
    PdfWriter(const PdfWriter&) = delete;
    PdfWriter& operator=(const PdfWriter&) = delete;

    [[nodiscard]] std::size_t AddPage(PdfRectangle mediaBox = {0, 0, 595, 842});
    [[nodiscard]] std::size_t InsertPage(std::size_t index, PdfRectangle mediaBox = {0, 0, 595, 842});
    // Registers a tiling pattern usable via PdfCanvas::SetPattern on any page.
    [[nodiscard]] std::size_t AddTilingPattern(const PdfTilingPatternOptions& options);
    void RemovePage(std::size_t index);
    void MovePage(std::size_t from, std::size_t to);
    [[nodiscard]] std::size_t GetPageCount() const noexcept;
    [[nodiscard]] PdfRectangle GetPageMediaBox(std::size_t pageIndex) const;
    // Changes an existing page's media box (page geometry) after AddPage.
    void SetPageSize(std::size_t pageIndex, const PdfRectangle& mediaBox);
    [[nodiscard]] PdfCanvas GetCanvas(std::size_t pageIndex);

    void SetDocumentInfo(const PdfDocumentInfo& info);
    [[nodiscard]] const PdfDocumentInfo& GetDocumentInfo() const noexcept;
    void SetTitle(std::string title);
    void SetAuthor(std::string author);
    void SetSubject(std::string subject);
    void SetKeywords(std::string keywords);
    void SetCreator(std::string creator);
    void SetProducer(std::string producer);
    void SetCreationDate(std::string creationDate);
    void SetModificationDate(std::string modificationDate);

    // Embeds an XMP metadata packet (built from the document info) as the
    // catalog /Metadata stream. PDF/A compliance requires one.
    void SetXmpMetadata(bool enabled = true);
    [[nodiscard]] bool GetXmpMetadataEnabled() const noexcept;

    void SetViewerPreferences(const PdfViewerPreferences& preferences);
    [[nodiscard]] const PdfViewerPreferences& GetViewerPreferences() const noexcept;
    void SetOpenAction(const PdfDestinationOptions& destination);
    void ClearOpenAction() noexcept;
    [[nodiscard]] bool HasOpenAction() const noexcept;

    // Marks the document as tagged (PDF/UA-ready): adds /MarkInfo /Marked true,
    // /Lang, and a minimal /StructTreeRoot to the catalog.
    void SetTaggedPdf(bool tagged = true);
    [[nodiscard]] bool IsTaggedPdf() const noexcept;
    void SetLanguage(std::string langCode);
    [[nodiscard]] const std::string& GetLanguage() const noexcept;
    void AddPageLabel(std::size_t pageIndex, const PdfPageLabelOptions& options);
    void RemovePageLabel(std::size_t pageIndex);
    void ClearPageLabels() noexcept;
    [[nodiscard]] std::size_t GetPageLabelCount() const noexcept;

    [[nodiscard]] std::size_t AddBookmark(const PdfBookmarkOptions& options);
    void ClearBookmarks() noexcept;
    [[nodiscard]] std::size_t GetBookmarkCount() const noexcept;

    void AddNamedDestination(std::string name, const PdfDestinationOptions& destination);
    void RemoveNamedDestination(const std::string& name);
    void ClearNamedDestinations() noexcept;
    [[nodiscard]] std::size_t GetNamedDestinationCount() const noexcept;
    void AddNamedDestinationLink(std::size_t pageIndex, std::string destinationName,
                                 const PdfLinkOptions& options);
    void AddUriLink(std::size_t pageIndex, std::string uri, const PdfLinkOptions& options);
    void ClearLinks(std::size_t pageIndex);
    [[nodiscard]] std::size_t GetLinkCount(std::size_t pageIndex) const;

    void AddEmbeddedFile(std::string name, std::span<const std::byte> bytes,
                         const PdfEmbeddedFileOptions& options = {});
    void AddEmbeddedFile(const std::filesystem::path& path,
                         const PdfEmbeddedFileOptions& options = {});
    void RemoveEmbeddedFile(const std::string& name);
    void ClearEmbeddedFiles() noexcept;
    [[nodiscard]] std::size_t GetEmbeddedFileCount() const noexcept;
    void AddFileAttachment(std::size_t pageIndex, std::string embeddedFileName,
                           const PdfFileAttachmentOptions& options);
    void ClearFileAttachments(std::size_t pageIndex);
    [[nodiscard]] std::size_t GetFileAttachmentCount(std::size_t pageIndex) const;

    // Marks the document as a portfolio with a /Collection entry in the catalog.
    void SetPortfolio(const PdfPortfolioOptions& options = {});
    void ClearPortfolio() noexcept;
    [[nodiscard]] bool HasPortfolio() const noexcept;

    // Optional content (layers): register a named layer and set layer defaults.
    [[nodiscard]] std::size_t AddOptionalContentGroup(const PdfOcgOptions& options);
    void ClearOptionalContentGroups() noexcept;
    [[nodiscard]] std::size_t GetOptionalContentGroupCount() const noexcept;

    void AddTextStamp(std::size_t pageIndex, const PdfTextStampOptions& options);
    void AddTextStampToAllPages(const PdfTextStampOptions& options);
    void AddImageStamp(std::size_t pageIndex, const PdfImage& image, const PdfImageStampOptions& options);
    void AddImageStampToAllPages(const PdfImage& image, const PdfImageStampOptions& options);
    void AddWatermark(std::size_t pageIndex, const PdfWatermarkOptions& options);
    void AddWatermarkToAllPages(const PdfWatermarkOptions& options);

    void SetEncryption(const PdfEncryptionOptions& options);
    void ClearEncryption() noexcept;
    [[nodiscard]] bool HasEncryption() const noexcept;
    [[nodiscard]] const PdfEncryptionOptions* GetEncryptionOptions() const noexcept;

    void Save(const std::filesystem::path& path, PdfSaveMode mode = PdfSaveMode::Rewrite) const;
    void Save(const std::filesystem::path& path, const PdfSaveOptions& options) const;
    void Save(std::ostream& output, PdfSaveMode mode = PdfSaveMode::Rewrite) const;
    void Save(std::ostream& output, const PdfSaveOptions& options) const;

    // Rewrites an existing PDF through the writer pipeline: every reachable
    // object is parsed, re-serialized cleanly, and emitted as a fresh file
    // (xref stream by default, optional object streams). This sanitizes the
    // output, removes incremental revisions, and drops orphan objects.
    static void Resave(const PdfDocument& document,
                       const std::filesystem::path& outputPath,
                       const PdfSaveOptions& options = {});
    static void Resave(const std::filesystem::path& inputPath,
                       const std::filesystem::path& outputPath,
                       const PdfSaveOptions& options = {});
    static void Resave(const std::filesystem::path& inputPath,
                       const std::filesystem::path& outputPath,
                       const PdfReaderOptions& readerOptions,
                       const PdfSaveOptions& options = {});
private:
    std::shared_ptr<Internal::PdfWriterState> state_;
};

} // namespace CPPPdf
