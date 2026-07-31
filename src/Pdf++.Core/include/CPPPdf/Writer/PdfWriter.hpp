#pragma once
#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Graphics/PdfCanvas.hpp>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>
#include <optional>
#include <string>
#include <ostream>

namespace CPPPdf {
namespace Internal { struct PdfWriterState; }

enum class PdfSaveMode { Rewrite, Incremental };

struct PdfSaveOptions final {
    PdfSaveMode mode{PdfSaveMode::Rewrite};
    bool subsetTrueTypeFonts{true};
};

using PdfStampPoint = PdfPoint;

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
    void RemovePage(std::size_t index);
    void MovePage(std::size_t from, std::size_t to);
    [[nodiscard]] std::size_t GetPageCount() const noexcept;
    [[nodiscard]] PdfRectangle GetPageMediaBox(std::size_t pageIndex) const;
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

    void SetViewerPreferences(const PdfViewerPreferences& preferences);
    [[nodiscard]] const PdfViewerPreferences& GetViewerPreferences() const noexcept;
    void SetOpenAction(const PdfDestinationOptions& destination);
    void ClearOpenAction() noexcept;
    [[nodiscard]] bool HasOpenAction() const noexcept;
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

    void AddTextStamp(std::size_t pageIndex, const PdfTextStampOptions& options);
    void AddTextStampToAllPages(const PdfTextStampOptions& options);
    void AddImageStamp(std::size_t pageIndex, const PdfImage& image, const PdfImageStampOptions& options);
    void AddImageStampToAllPages(const PdfImage& image, const PdfImageStampOptions& options);
    void AddWatermark(std::size_t pageIndex, const PdfWatermarkOptions& options);
    void AddWatermarkToAllPages(const PdfWatermarkOptions& options);

    void Save(const std::filesystem::path& path, PdfSaveMode mode = PdfSaveMode::Rewrite) const;
    void Save(const std::filesystem::path& path, const PdfSaveOptions& options) const;
    void Save(std::ostream& output, PdfSaveMode mode = PdfSaveMode::Rewrite) const;
    void Save(std::ostream& output, const PdfSaveOptions& options) const;
private:
    std::shared_ptr<Internal::PdfWriterState> state_;
};

} // namespace CPPPdf
