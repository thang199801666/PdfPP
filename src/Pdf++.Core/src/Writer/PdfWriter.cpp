#include <CPPPdf/Writer/PdfWriter.hpp>
#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "PdfWriterState.hpp"
#include "Internal/Security/PdfStandardSecurity.hpp"
#include "Internal/Writer/PdfObjectCollectionWriter.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace CPPPdf {
namespace {

[[nodiscard]] std::string compressBytes(const std::span<const std::byte> input) {
    uLongf outputSize = compressBound(static_cast<uLong>(input.size()));
    std::string output(outputSize, '\0');
    const int status = compress2(
        reinterpret_cast<Bytef*>(output.data()), &outputSize,
        reinterpret_cast<const Bytef*>(input.data()), static_cast<uLong>(input.size()),
        Z_BEST_SPEED);
    if (status != Z_OK) throw std::runtime_error("Cannot compress image stream.");
    output.resize(outputSize);
    return output;
}

void collectReferencesFromObject(const PdfObject& object, std::vector<PdfReference>& references) {
    switch (object.type()) {
    case PdfObjectType::IndirectReference: {
        const auto pair = *object.AsReference();
        references.push_back(PdfReference{pair.first, pair.second});
        break;
    }
    case PdfObjectType::Array:
        for (const auto& value : object.AsArray()->values()) {
            collectReferencesFromObject(value, references);
        }
        break;
    case PdfObjectType::Dictionary:
        for (const auto& [key, value] : object.AsDictionary()->values()) {
            (void)key;
            collectReferencesFromObject(value, references);
        }
        break;
    case PdfObjectType::Stream:
        for (const auto& [key, value] : object.AsStream()->dictionary().values()) {
            (void)key;
            collectReferencesFromObject(value, references);
        }
        break;
    default:
        break;
    }
}


[[nodiscard]] std::string escapePdfString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '(': escaped += "\\("; break;
        case ')': escaped += "\\)"; break;
        case '\r': escaped += "\\r"; break;
        case '\n': escaped += "\\n"; break;
        case '\t': escaped += "\\t"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

void appendInfoEntry(std::ostringstream& dictionary, const char* key, const std::string& value) {
    if (!value.empty()) dictionary << " /" << key << " (" << escapePdfString(value) << ')';
}

[[nodiscard]] std::string destinationArray(const Internal::PdfWriterNamedDestination& destination,
                                           const std::vector<int>& pageIds) {
    const auto numberOrNull = [](const std::optional<double>& value) {
        if (!value) return std::string("null");
        std::ostringstream output;
        output << *value;
        return output.str();
    };
    std::ostringstream output;
    output << '[' << pageIds.at(destination.pageIndex) << " 0 R ";
    switch (destination.destinationType) {
    case PdfDestinationType::FitPage:
        output << "/Fit";
        break;
    case PdfDestinationType::FitWidth:
        output << "/FitH " << numberOrNull(destination.top);
        break;
    case PdfDestinationType::XYZ:
        output << "/XYZ " << numberOrNull(destination.left) << ' '
               << numberOrNull(destination.top) << ' ' << numberOrNull(destination.zoom);
        break;
    }
    output << ']';
    return output.str();
}


[[nodiscard]] const char* pageLayoutName(const PdfPageLayout value) {
    switch (value) {
    case PdfPageLayout::SinglePage: return "/SinglePage";
    case PdfPageLayout::OneColumn: return "/OneColumn";
    case PdfPageLayout::TwoColumnLeft: return "/TwoColumnLeft";
    case PdfPageLayout::TwoColumnRight: return "/TwoColumnRight";
    case PdfPageLayout::TwoPageLeft: return "/TwoPageLeft";
    case PdfPageLayout::TwoPageRight: return "/TwoPageRight";
    case PdfPageLayout::Default: return nullptr;
    }
    return nullptr;
}

[[nodiscard]] const char* pageModeName(const PdfPageMode value) {
    switch (value) {
    case PdfPageMode::UseNone: return "/UseNone";
    case PdfPageMode::UseOutlines: return "/UseOutlines";
    case PdfPageMode::UseThumbs: return "/UseThumbs";
    case PdfPageMode::FullScreen: return "/FullScreen";
    case PdfPageMode::UseOptionalContent: return "/UseOC";
    case PdfPageMode::UseAttachments: return "/UseAttachments";
    case PdfPageMode::Default: return nullptr;
    }
    return nullptr;
}

[[nodiscard]] const char* pageLabelStyleName(const PdfPageLabelStyle value) {
    switch (value) {
    case PdfPageLabelStyle::Decimal: return "/D";
    case PdfPageLabelStyle::UpperRoman: return "/R";
    case PdfPageLabelStyle::LowerRoman: return "/r";
    case PdfPageLabelStyle::UpperLetters: return "/A";
    case PdfPageLabelStyle::LowerLetters: return "/a";
    }
    return "/D";
}


[[nodiscard]] const char* associatedFileRelationshipName(const PdfAssociatedFileRelationship value) {
    switch (value) {
    case PdfAssociatedFileRelationship::Source: return "/Source";
    case PdfAssociatedFileRelationship::Data: return "/Data";
    case PdfAssociatedFileRelationship::Alternative: return "/Alternative";
    case PdfAssociatedFileRelationship::Supplement: return "/Supplement";
    case PdfAssociatedFileRelationship::EncryptedPayload: return "/EncryptedPayload";
    case PdfAssociatedFileRelationship::FormData: return "/FormData";
    case PdfAssociatedFileRelationship::Schema: return "/Schema";
    case PdfAssociatedFileRelationship::Unspecified: return "/Unspecified";
    }
    return "/Unspecified";
}

[[nodiscard]] const char* fileAttachmentIconName(const PdfFileAttachmentIcon value) {
    switch (value) {
    case PdfFileAttachmentIcon::Graph: return "/Graph";
    case PdfFileAttachmentIcon::Paperclip: return "/Paperclip";
    case PdfFileAttachmentIcon::PushPin: return "/PushPin";
    case PdfFileAttachmentIcon::Tag: return "/Tag";
    }
    return "/PushPin";
}

[[nodiscard]] std::string encodePdfName(std::string value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char ch : value) {
        const bool regular = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
        if (regular) {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('#');
            encoded.push_back(hex[(ch >> 4U) & 0x0FU]);
            encoded.push_back(hex[ch & 0x0FU]);
        }
    }
    return encoded;
}

[[nodiscard]] const char* colorSpaceName(const PdfImageColorSpace colorSpace) {
    switch (colorSpace) {
    case PdfImageColorSpace::DeviceGray: return "/DeviceGray";
    case PdfImageColorSpace::DeviceRGB: return "/DeviceRGB";
    case PdfImageColorSpace::DeviceCMYK: return "/DeviceCMYK";
    default: throw std::runtime_error("PdfWriter supports DeviceGray, DeviceRGB, and DeviceCMYK images only.");
    }
}

} // namespace

PdfWriter::PdfWriter():state_(std::make_shared<Internal::PdfWriterState>()){} PdfWriter::~PdfWriter()=default;
PdfWriter::PdfWriter(PdfWriter&&) noexcept=default; PdfWriter& PdfWriter::operator=(PdfWriter&&) noexcept=default;
std::size_t PdfWriter::AddPage(PdfRectangle box) {
    Internal::PdfWriterPage page;
    page.mediaBox = box;
    state_->pages.push_back(std::move(page));
    return state_->pages.size() - 1;
}

std::size_t PdfWriter::InsertPage(std::size_t index, PdfRectangle box) {
    if (index > state_->pages.size()) throw std::out_of_range("Page index");
    Internal::PdfWriterPage page;
    page.mediaBox = box;
    state_->pages.insert(state_->pages.begin() + static_cast<std::ptrdiff_t>(index), std::move(page));
    for (auto& bookmark : state_->bookmarks) {
        if (bookmark.pageIndex >= index) ++bookmark.pageIndex;
    }
    for (auto& destination : state_->namedDestinations) {
        if (destination.pageIndex >= index) ++destination.pageIndex;
    }
    for (auto& label : state_->pageLabels) {
        if (label.pageIndex >= index) ++label.pageIndex;
    }
    if (state_->openAction && state_->openAction->pageIndex >= index) ++state_->openAction->pageIndex;
    return index;
}

void PdfWriter::RemovePage(std::size_t index) {
    if (index >= state_->pages.size()) throw std::out_of_range("Page index");
    state_->pages.erase(state_->pages.begin() + static_cast<std::ptrdiff_t>(index));

    std::vector<bool> remove(state_->bookmarks.size(), false);
    for (std::size_t i = 0; i < state_->bookmarks.size(); ++i) {
        if (state_->bookmarks[i].pageIndex == index) remove[i] = true;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < state_->bookmarks.size(); ++i) {
            const auto parent = state_->bookmarks[i].parentIndex;
            if (!remove[i] && parent && remove[*parent]) {
                remove[i] = true;
                changed = true;
            }
        }
    }

    std::vector<std::optional<std::size_t>> remap(state_->bookmarks.size());
    std::vector<Internal::PdfWriterBookmark> retained;
    retained.reserve(state_->bookmarks.size());
    for (std::size_t i = 0; i < state_->bookmarks.size(); ++i) {
        if (!remove[i]) {
            remap[i] = retained.size();
            retained.push_back(std::move(state_->bookmarks[i]));
        }
    }
    for (auto& bookmark : retained) {
        if (bookmark.pageIndex > index) --bookmark.pageIndex;
        if (bookmark.parentIndex) bookmark.parentIndex = remap[*bookmark.parentIndex];
    }
    state_->bookmarks = std::move(retained);
    state_->namedDestinations.erase(
        std::remove_if(state_->namedDestinations.begin(), state_->namedDestinations.end(),
            [index](const auto& destination) { return destination.pageIndex == index; }),
        state_->namedDestinations.end());
    for (auto& destination : state_->namedDestinations) {
        if (destination.pageIndex > index) --destination.pageIndex;
    }
    state_->pageLabels.erase(std::remove_if(state_->pageLabels.begin(), state_->pageLabels.end(),
        [index](const auto& label) { return label.pageIndex == index; }), state_->pageLabels.end());
    for (auto& label : state_->pageLabels) {
        if (label.pageIndex > index) --label.pageIndex;
    }
    if (state_->openAction) {
        if (state_->openAction->pageIndex == index) state_->openAction.reset();
        else if (state_->openAction->pageIndex > index) --state_->openAction->pageIndex;
    }
}

void PdfWriter::MovePage(std::size_t from, std::size_t to) {
    if (from >= state_->pages.size() || to >= state_->pages.size()) throw std::out_of_range("Page index");
    if (from == to) return;
    auto page = std::move(state_->pages[from]);
    state_->pages.erase(state_->pages.begin() + static_cast<std::ptrdiff_t>(from));
    state_->pages.insert(state_->pages.begin() + static_cast<std::ptrdiff_t>(to), std::move(page));
    const auto remapPageIndex = [from, to](std::size_t& pageIndex) {
        if (pageIndex == from) pageIndex = to;
        else if (from < to && pageIndex > from && pageIndex <= to) --pageIndex;
        else if (to < from && pageIndex >= to && pageIndex < from) ++pageIndex;
    };
    for (auto& bookmark : state_->bookmarks) remapPageIndex(bookmark.pageIndex);
    for (auto& destination : state_->namedDestinations) remapPageIndex(destination.pageIndex);
    for (auto& label : state_->pageLabels) remapPageIndex(label.pageIndex);
    if (state_->openAction) remapPageIndex(state_->openAction->pageIndex);
    std::sort(state_->pageLabels.begin(), state_->pageLabels.end(), [](const auto& a, const auto& b) { return a.pageIndex < b.pageIndex; });
}
std::size_t PdfWriter::GetPageCount() const noexcept{return state_->pages.size();}
PdfRectangle PdfWriter::GetPageMediaBox(std::size_t i) const { if(i>=state_->pages.size())throw std::out_of_range("Page index"); return state_->pages[i].mediaBox; }
PdfCanvas PdfWriter::GetCanvas(std::size_t i){if(i>=state_->pages.size())throw std::out_of_range("Page index");return PdfCanvas(state_,i);}

void PdfWriter::SetDocumentInfo(const PdfDocumentInfo& info) { state_->documentInfo = info; }
const PdfDocumentInfo& PdfWriter::GetDocumentInfo() const noexcept { return state_->documentInfo; }
void PdfWriter::SetTitle(std::string value) { state_->documentInfo.title = std::move(value); }
void PdfWriter::SetAuthor(std::string value) { state_->documentInfo.author = std::move(value); }
void PdfWriter::SetSubject(std::string value) { state_->documentInfo.subject = std::move(value); }
void PdfWriter::SetKeywords(std::string value) { state_->documentInfo.keywords = std::move(value); }
void PdfWriter::SetCreator(std::string value) { state_->documentInfo.creator = std::move(value); }
void PdfWriter::SetProducer(std::string value) { state_->documentInfo.producer = std::move(value); }
void PdfWriter::SetCreationDate(std::string value) { state_->documentInfo.creationDate = std::move(value); }
void PdfWriter::SetModificationDate(std::string value) { state_->documentInfo.modificationDate = std::move(value); }

void PdfWriter::SetViewerPreferences(const PdfViewerPreferences& preferences) {
    if (preferences.numberOfCopies == 0U) throw std::invalid_argument("Number of copies must be positive");
    state_->viewerPreferences = preferences;
}
const PdfViewerPreferences& PdfWriter::GetViewerPreferences() const noexcept { return state_->viewerPreferences; }
void PdfWriter::SetOpenAction(const PdfDestinationOptions& destination) {
    if (destination.pageIndex >= state_->pages.size()) throw std::out_of_range("Open action page index");
    if (destination.zoom && *destination.zoom <= 0.0) throw std::invalid_argument("Open action zoom must be positive");
    state_->openAction = destination;
}
void PdfWriter::ClearOpenAction() noexcept { state_->openAction.reset(); }
bool PdfWriter::HasOpenAction() const noexcept { return state_->openAction.has_value(); }

void PdfWriter::SetTaggedPdf(const bool tagged) { state_->tagged = tagged; }
bool PdfWriter::IsTaggedPdf() const noexcept { return state_->tagged; }
void PdfWriter::SetLanguage(std::string langCode) { state_->language = std::move(langCode); }
const std::string& PdfWriter::GetLanguage() const noexcept { return state_->language; }
void PdfWriter::AddPageLabel(std::size_t pageIndex, const PdfPageLabelOptions& options) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page label index");
    if (options.startNumber == 0U) throw std::invalid_argument("Page label start number must be positive");
    const auto duplicate = std::find_if(state_->pageLabels.begin(), state_->pageLabels.end(),
        [pageIndex](const auto& label) { return label.pageIndex == pageIndex; });
    if (duplicate != state_->pageLabels.end()) throw std::invalid_argument("Page label already exists at this page");
    state_->pageLabels.push_back({pageIndex, options});
    std::sort(state_->pageLabels.begin(), state_->pageLabels.end(), [](const auto& a, const auto& b) { return a.pageIndex < b.pageIndex; });
}
void PdfWriter::RemovePageLabel(std::size_t pageIndex) {
    const auto oldSize = state_->pageLabels.size();
    state_->pageLabels.erase(std::remove_if(state_->pageLabels.begin(), state_->pageLabels.end(),
        [pageIndex](const auto& label) { return label.pageIndex == pageIndex; }), state_->pageLabels.end());
    if (state_->pageLabels.size() == oldSize) throw std::out_of_range("Page label not found");
}
void PdfWriter::ClearPageLabels() noexcept { state_->pageLabels.clear(); }
std::size_t PdfWriter::GetPageLabelCount() const noexcept { return state_->pageLabels.size(); }

std::size_t PdfWriter::AddBookmark(const PdfBookmarkOptions& options) {
    if (options.title.empty()) throw std::invalid_argument("Bookmark title cannot be empty");
    if (options.pageIndex >= state_->pages.size()) throw std::out_of_range("Bookmark page index");
    if (options.parentIndex && *options.parentIndex >= state_->bookmarks.size())
        throw std::out_of_range("Bookmark parent index");
    if (options.zoom && *options.zoom <= 0.0) throw std::invalid_argument("Bookmark zoom must be positive");
    if (options.color && (options.color->r < 0.0 || options.color->r > 1.0 ||
                          options.color->g < 0.0 || options.color->g > 1.0 ||
                          options.color->b < 0.0 || options.color->b > 1.0))
        throw std::invalid_argument("Bookmark color components must be between zero and one");

    Internal::PdfWriterBookmark bookmark;
    bookmark.title = options.title;
    bookmark.pageIndex = options.pageIndex;
    bookmark.parentIndex = options.parentIndex;
    bookmark.destinationType = options.destinationType;
    bookmark.left = options.left;
    bookmark.top = options.top;
    bookmark.zoom = options.zoom;
    bookmark.open = options.open;
    bookmark.bold = options.bold;
    bookmark.italic = options.italic;
    bookmark.color = options.color;
    state_->bookmarks.push_back(std::move(bookmark));
    return state_->bookmarks.size() - 1;
}

void PdfWriter::ClearBookmarks() noexcept { state_->bookmarks.clear(); }
std::size_t PdfWriter::GetBookmarkCount() const noexcept { return state_->bookmarks.size(); }

void PdfWriter::AddNamedDestination(std::string name, const PdfDestinationOptions& destination) {
    if (name.empty()) throw std::invalid_argument("Named destination cannot be empty");
    if (destination.pageIndex >= state_->pages.size()) throw std::out_of_range("Destination page index");
    if (destination.zoom && *destination.zoom <= 0.0) throw std::invalid_argument("Destination zoom must be positive");
    const auto duplicate = std::find_if(state_->namedDestinations.begin(), state_->namedDestinations.end(),
        [&](const auto& item) { return item.name == name; });
    if (duplicate != state_->namedDestinations.end()) throw std::invalid_argument("Named destination already exists");
    Internal::PdfWriterNamedDestination item;
    item.name = std::move(name);
    item.pageIndex = destination.pageIndex;
    item.destinationType = destination.destinationType;
    item.left = destination.left;
    item.top = destination.top;
    item.zoom = destination.zoom;
    state_->namedDestinations.push_back(std::move(item));
}

void PdfWriter::RemoveNamedDestination(const std::string& name) {
    const auto oldSize = state_->namedDestinations.size();
    state_->namedDestinations.erase(
        std::remove_if(state_->namedDestinations.begin(), state_->namedDestinations.end(),
            [&](const auto& item) { return item.name == name; }),
        state_->namedDestinations.end());
    if (state_->namedDestinations.size() == oldSize) throw std::out_of_range("Named destination not found");
}

void PdfWriter::ClearNamedDestinations() noexcept { state_->namedDestinations.clear(); }
std::size_t PdfWriter::GetNamedDestinationCount() const noexcept { return state_->namedDestinations.size(); }

void PdfWriter::AddNamedDestinationLink(std::size_t pageIndex, std::string destinationName,
                                        const PdfLinkOptions& options) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    if (destinationName.empty()) throw std::invalid_argument("Destination name cannot be empty");
    if (options.borderWidth < 0.0) throw std::invalid_argument("Link border width cannot be negative");
    state_->pages[pageIndex].links.push_back(
        {Internal::PdfWriterLinkKind::NamedDestination, std::move(destinationName), options});
}

void PdfWriter::AddUriLink(std::size_t pageIndex, std::string uri, const PdfLinkOptions& options) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    if (uri.empty()) throw std::invalid_argument("URI cannot be empty");
    if (options.borderWidth < 0.0) throw std::invalid_argument("Link border width cannot be negative");
    state_->pages[pageIndex].links.push_back({Internal::PdfWriterLinkKind::Uri, std::move(uri), options});
}

void PdfWriter::ClearLinks(std::size_t pageIndex) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    state_->pages[pageIndex].links.clear();
}

std::size_t PdfWriter::GetLinkCount(std::size_t pageIndex) const {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    return state_->pages[pageIndex].links.size();
}


void PdfWriter::AddEmbeddedFile(std::string name, std::span<const std::byte> bytes,
                                const PdfEmbeddedFileOptions& options) {
    if (name.empty()) throw std::invalid_argument("Embedded file name cannot be empty");
    const auto duplicate = std::find_if(state_->embeddedFiles.begin(), state_->embeddedFiles.end(),
        [&](const auto& file) { return file.name == name; });
    if (duplicate != state_->embeddedFiles.end()) throw std::invalid_argument("Embedded file already exists");
    Internal::PdfWriterEmbeddedFile file;
    file.name = std::move(name);
    file.bytes.assign(bytes.begin(), bytes.end());
    file.options = options;
    state_->embeddedFiles.push_back(std::move(file));
}

void PdfWriter::AddEmbeddedFile(const std::filesystem::path& path, const PdfEmbeddedFileOptions& options) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open embedded file");
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0) throw std::runtime_error("Cannot determine embedded file size");
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("Cannot read embedded file");
    AddEmbeddedFile(path.filename().string(), bytes, options);
}

void PdfWriter::RemoveEmbeddedFile(const std::string& name) {
    for (const auto& page : state_->pages) {
        const auto referenced = std::any_of(page.fileAttachments.begin(), page.fileAttachments.end(),
            [&](const auto& attachment) { return attachment.embeddedFileName == name; });
        if (referenced) throw std::runtime_error("Embedded file is still referenced by a page attachment");
    }
    const auto oldSize = state_->embeddedFiles.size();
    state_->embeddedFiles.erase(std::remove_if(state_->embeddedFiles.begin(), state_->embeddedFiles.end(),
        [&](const auto& file) { return file.name == name; }), state_->embeddedFiles.end());
    if (state_->embeddedFiles.size() == oldSize) throw std::out_of_range("Embedded file not found");
}

void PdfWriter::ClearEmbeddedFiles() noexcept {
    state_->embeddedFiles.clear();
    for (auto& page : state_->pages) page.fileAttachments.clear();
}

std::size_t PdfWriter::GetEmbeddedFileCount() const noexcept { return state_->embeddedFiles.size(); }

std::size_t PdfWriter::AddOptionalContentGroup(const PdfOcgOptions& options) {
    if (options.name.empty()) throw std::invalid_argument("Optional content group name cannot be empty.");
    const auto existing = std::find_if(state_->ocgs.begin(), state_->ocgs.end(),
        [&](const auto& item) { return item.name == options.name; });
    if (existing != state_->ocgs.end()) {
        existing->visible = options.visible;
        return static_cast<std::size_t>(std::distance(state_->ocgs.begin(), existing));
    }
    state_->ocgs.push_back(Internal::PdfWriterOcg{options.name, options.visible});
    return state_->ocgs.size() - 1U;
}

void PdfWriter::ClearOptionalContentGroups() noexcept { state_->ocgs.clear(); }

std::size_t PdfWriter::GetOptionalContentGroupCount() const noexcept { return state_->ocgs.size(); }

void PdfWriter::AddFileAttachment(std::size_t pageIndex, std::string embeddedFileName,
                                  const PdfFileAttachmentOptions& options) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    const auto file = std::find_if(state_->embeddedFiles.begin(), state_->embeddedFiles.end(),
        [&](const auto& item) { return item.name == embeddedFileName; });
    if (file == state_->embeddedFiles.end()) throw std::out_of_range("Embedded file not found");
    state_->pages[pageIndex].fileAttachments.push_back({std::move(embeddedFileName), options});
}

void PdfWriter::ClearFileAttachments(std::size_t pageIndex) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    state_->pages[pageIndex].fileAttachments.clear();
}

std::size_t PdfWriter::GetFileAttachmentCount(std::size_t pageIndex) const {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    return state_->pages[pageIndex].fileAttachments.size();
}

void PdfWriter::SetPortfolio(const PdfPortfolioOptions& options) {
    state_->portfolio = options;
}

void PdfWriter::ClearPortfolio() noexcept { state_->portfolio.reset(); }

bool PdfWriter::HasPortfolio() const noexcept { return state_->portfolio.has_value(); }

void PdfWriter::AddTextStamp(std::size_t pageIndex, const PdfTextStampOptions& options) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    if (options.text.empty()) return;
    const double angle = options.rotationDegrees * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double estimatedWidth = static_cast<double>(options.text.size()) * options.fontSize * 0.5;
    const double estimatedHeight = options.fontSize;
    auto& pageContent = state_->pages[pageIndex].content;
    const std::size_t originalSize = pageContent.size();
    auto canvas = GetCanvas(pageIndex);
    canvas.SaveState().SetOpacity(options.opacity);
    if (options.drawBackground || options.drawBorder) {
        const double width = estimatedWidth + options.padding * 2.0;
        const double height = estimatedHeight + options.padding * 2.0;
        canvas.SaveState()
              .ConcatenateMatrix(cosine, sine, -sine, cosine, options.position.x, options.position.y)
              .Rectangle(-options.padding, -options.padding, width, height);
        if (options.drawBackground && options.drawBorder) {
            canvas.SetFillColor(options.backgroundColor).SetStrokeColor(options.borderColor)
                  .SetLineWidth(options.borderWidth).FillStroke();
        } else if (options.drawBackground) {
            canvas.SetFillColor(options.backgroundColor).Fill();
        } else {
            canvas.SetStrokeColor(options.borderColor).SetLineWidth(options.borderWidth).Stroke();
        }
        canvas.RestoreState();
    }
    canvas.SetFillColor(options.textColor)
          .BeginText().SetFontAndSize(options.fontName, options.fontSize)
          .SetTextMatrix(cosine, sine, -sine, cosine, options.position.x, options.position.y)
          .ShowText(options.text).EndText().RestoreState();
    if (options.layer == PdfStampLayer::Background) {
        const std::string appended = pageContent.substr(originalSize);
        pageContent.erase(originalSize);
        pageContent.insert(0, appended);
    }
}

void PdfWriter::AddTextStampToAllPages(const PdfTextStampOptions& options) {
    for (std::size_t i=0;i<state_->pages.size();++i) AddTextStamp(i, options);
}

void PdfWriter::AddImageStamp(std::size_t pageIndex, const PdfImage& image, const PdfImageStampOptions& options) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    auto& pageContent = state_->pages[pageIndex].content;
    const std::size_t originalSize = pageContent.size();
    auto canvas = GetCanvas(pageIndex);
    canvas.SaveState().SetOpacity(options.opacity).DrawImage(image, options.rectangle);
    if (options.drawBorder) {
        canvas.SetStrokeColor(options.borderColor).SetLineWidth(options.borderWidth)
              .Rectangle(options.rectangle.left, options.rectangle.bottom,
                         options.rectangle.right-options.rectangle.left,
                         options.rectangle.top-options.rectangle.bottom).Stroke();
    }
    canvas.RestoreState();
    if (options.layer == PdfStampLayer::Background) {
        const std::string appended = pageContent.substr(originalSize);
        pageContent.erase(originalSize);
        pageContent.insert(0, appended);
    }
}

void PdfWriter::AddImageStampToAllPages(const PdfImage& image, const PdfImageStampOptions& options) {
    for (std::size_t i=0;i<state_->pages.size();++i) AddImageStamp(i, image, options);
}

void PdfWriter::AddWatermark(std::size_t pageIndex, const PdfWatermarkOptions& options) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    const auto box = state_->pages[pageIndex].mediaBox;
    const double estimatedWidth = static_cast<double>(options.text.size()) * options.fontSize * 0.5;
    double x = box.left;
    double y = box.bottom;
    switch (options.horizontalAlignment) {
    case PdfStampHorizontalAlignment::Left: x = box.left; break;
    case PdfStampHorizontalAlignment::Center: x = (box.left + box.right - estimatedWidth) * 0.5; break;
    case PdfStampHorizontalAlignment::Right: x = box.right - estimatedWidth; break;
    }
    switch (options.verticalAlignment) {
    case PdfStampVerticalAlignment::Bottom: y = box.bottom; break;
    case PdfStampVerticalAlignment::Middle: y = (box.bottom + box.top - options.fontSize) * 0.5; break;
    case PdfStampVerticalAlignment::Top: y = box.top - options.fontSize; break;
    }
    PdfTextStampOptions stamp;
    stamp.text = options.text;
    stamp.position = {x + options.offset.x, y + options.offset.y};
    stamp.fontName = options.fontName;
    stamp.fontSize = options.fontSize;
    stamp.textColor = options.color;
    stamp.opacity = options.opacity;
    stamp.rotationDegrees = options.rotationDegrees;
    stamp.layer = options.layer;
    AddTextStamp(pageIndex, stamp);
}

void PdfWriter::AddWatermarkToAllPages(const PdfWatermarkOptions& options) {
    for (std::size_t i=0;i<state_->pages.size();++i) AddWatermark(i, options);
}

void PdfWriter::SetEncryption(const PdfEncryptionOptions& options) {
    if (options.userPassword.size() > 32U || options.ownerPassword.size() > 32U) {
        throw std::invalid_argument("PDF AES-128/RC4-128 passwords are limited to 32 bytes.");
    }
    state_->encryption = options;
}

void PdfWriter::ClearEncryption() noexcept { state_->encryption.reset(); }
bool PdfWriter::HasEncryption() const noexcept { return state_->encryption.has_value(); }
const PdfEncryptionOptions* PdfWriter::GetEncryptionOptions() const noexcept {
    return state_->encryption ? &*state_->encryption : nullptr;
}

void PdfWriter::Save(const std::filesystem::path& path, PdfSaveMode mode) const {
    PdfSaveOptions options; options.mode = mode; Save(path, options);
}

void PdfWriter::Save(const std::filesystem::path& path, const PdfSaveOptions& options) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot create PDF output file");
    Save(output, options);
}

void PdfWriter::Save(std::ostream& output, const PdfSaveMode mode) const {
    PdfSaveOptions options;
    options.mode = mode;
    Save(output, options);
}

void PdfWriter::Save(std::ostream& out, const PdfSaveOptions& options) const {
    if(options.mode==PdfSaveMode::Incremental) throw std::runtime_error("Incremental save is reserved for the next writer milestone");
    for (const auto& page : state_->pages) {
        for (const auto& link : page.links) {
            if (link.kind != Internal::PdfWriterLinkKind::NamedDestination) continue;
            const auto destination = std::find_if(
                state_->namedDestinations.begin(), state_->namedDestinations.end(),
                [&](const auto& item) { return item.name == link.target; });
            if (destination == state_->namedDestinations.end()) {
                throw std::runtime_error("Named destination link target does not exist: " + link.target);
            }
        }
    }

    std::vector<std::string> objects(1);
    auto allocate=[&](){ objects.emplace_back(); return static_cast<int>(objects.size()-1); };
    const int catalog=allocate(), pages=allocate(), base14Font=allocate();
    const int encryptionObject = state_->encryption ? allocate() : 0;
    const auto fileId = Internal::GeneratePdfFileId();
    const auto security = state_->encryption
        ? std::optional<Internal::PdfStandardSecurity>(
            Internal::PdfStandardSecurity::Create(*state_->encryption, fileId))
        : std::nullopt;
    const bool hasDocumentInfo = !state_->documentInfo.title.empty() || !state_->documentInfo.author.empty() ||
        !state_->documentInfo.subject.empty() || !state_->documentInfo.keywords.empty() ||
        !state_->documentInfo.creator.empty() || !state_->documentInfo.producer.empty() ||
        !state_->documentInfo.creationDate.empty() || !state_->documentInfo.modificationDate.empty();
    const int infoObject = hasDocumentInfo ? allocate() : 0;
    const int outlinesObject = state_->bookmarks.empty() ? 0 : allocate();
    const int destinationsObject = state_->namedDestinations.empty() ? 0 : allocate();
    const int embeddedFilesObject = state_->embeddedFiles.empty() ? 0 : allocate();
    const int pageLabelsObject = state_->pageLabels.empty() ? 0 : allocate();
    std::vector<int> bookmarkIds(state_->bookmarks.size());
    for (auto& id : bookmarkIds) id = allocate();
    struct EmbeddedFileIds { int stream{}, fileSpec{}; };
    std::vector<EmbeddedFileIds> embeddedFileIds(state_->embeddedFiles.size());
    for (auto& ids : embeddedFileIds) { ids.stream = allocate(); ids.fileSpec = allocate(); }
    std::vector<int> imageIds(state_->images.size()); for(auto& id:imageIds) id=allocate();
    std::vector<int> extGStateIds(state_->extGStates.size()); for(auto& id:extGStateIds) id=allocate();
    std::vector<int> ocgIds(state_->ocgs.size()); for(auto& id:ocgIds) id=allocate();
    const int ocPropertiesObject = state_->ocgs.empty() ? 0 : allocate();
    const int structTreeObject = state_->tagged ? allocate() : 0;
    struct EmbeddedIds { int file{}, descriptor{}, cid{}, toUnicode{}, type0{}; };
    std::vector<EmbeddedIds> fontIds(state_->embeddedFonts.size());
    for(auto& ids:fontIds){ ids.file=allocate(); ids.descriptor=allocate(); ids.cid=allocate(); ids.toUnicode=allocate(); ids.type0=allocate(); }
    std::vector<int> type1Ids(state_->type1Fonts.size());
    std::vector<int> type1FileIds(state_->type1Fonts.size());
    std::vector<int> type1DescIds(state_->type1Fonts.size());
    for (auto& id : type1Ids) id = allocate();
    for (auto& id : type1FileIds) id = allocate();
    for (auto& id : type1DescIds) id = allocate();
    std::vector<int> pageIds,contentIds; for(std::size_t i=0;i<state_->pages.size();++i){pageIds.push_back(allocate());contentIds.push_back(allocate());}
    std::vector<std::vector<int>> linkIds(state_->pages.size());
    std::vector<std::vector<int>> attachmentIds(state_->pages.size());
    for (std::size_t i = 0; i < state_->pages.size(); ++i) {
        for (std::size_t j = 0; j < state_->pages[i].links.size(); ++j) linkIds[i].push_back(allocate());
        for (std::size_t j = 0; j < state_->pages[i].fileAttachments.size(); ++j) attachmentIds[i].push_back(allocate());
    }
    objects[catalog] = "<< /Type /Catalog /Pages " + std::to_string(pages) + " 0 R";
    if (outlinesObject != 0) objects[catalog] += " /Outlines " + std::to_string(outlinesObject) + " 0 R";
    if (destinationsObject != 0 || embeddedFilesObject != 0) {
        objects[catalog] += " /Names <<";
        if (destinationsObject != 0) objects[catalog] += " /Dests " + std::to_string(destinationsObject) + " 0 R";
        if (embeddedFilesObject != 0) objects[catalog] += " /EmbeddedFiles " + std::to_string(embeddedFilesObject) + " 0 R";
        objects[catalog] += " >>";
    }
    if (std::any_of(state_->embeddedFiles.begin(), state_->embeddedFiles.end(),
                    [](const auto& file) { return file.options.associateWithDocument; })) {
        objects[catalog] += " /AF [";
        for (std::size_t i = 0; i < state_->embeddedFiles.size(); ++i) {
            if (state_->embeddedFiles[i].options.associateWithDocument) {
                objects[catalog] += std::to_string(embeddedFileIds[i].fileSpec) + " 0 R ";
            }
        }
        objects[catalog] += "]";
    }
    if (pageLabelsObject != 0) objects[catalog] += " /PageLabels " + std::to_string(pageLabelsObject) + " 0 R";
    if (ocPropertiesObject != 0) objects[catalog] += " /OCProperties " + std::to_string(ocPropertiesObject) + " 0 R";
    if (state_->portfolio) {
        objects[catalog] += " /Collection << /Type /Collection /View /" +
            (state_->portfolio->view.empty() ? std::string("D") : state_->portfolio->view);
        if (!state_->portfolio->title.empty()) {
            objects[catalog] += " /Title (" + escapePdfString(state_->portfolio->title) + ")";
        }
        objects[catalog] += " >>";
    }
    if (state_->tagged) {
        objects[catalog] += " /MarkInfo << /Marked true >>";
        if (!state_->language.empty()) {
            objects[catalog] += " /Lang (" + escapePdfString(state_->language) + ")";
        }
        objects[catalog] += " /StructTreeRoot " + std::to_string(structTreeObject) + " 0 R";
    }
    if (state_->openAction) {
        Internal::PdfWriterNamedDestination action;
        action.pageIndex = state_->openAction->pageIndex;
        action.destinationType = state_->openAction->destinationType;
        action.left = state_->openAction->left;
        action.top = state_->openAction->top;
        action.zoom = state_->openAction->zoom;
        objects[catalog] += " /OpenAction " + destinationArray(action, pageIds);
    }
    if (const auto* layout = pageLayoutName(state_->viewerPreferences.pageLayout)) objects[catalog] += std::string(" /PageLayout ") + layout;
    const auto effectiveMode = state_->viewerPreferences.pageMode == PdfPageMode::Default && outlinesObject != 0
        ? PdfPageMode::UseOutlines : state_->viewerPreferences.pageMode;
    if (const auto* modeName = pageModeName(effectiveMode)) objects[catalog] += std::string(" /PageMode ") + modeName;
    const auto& vp = state_->viewerPreferences;
    if (vp.hideToolbar || vp.hideMenuBar || vp.hideWindowUi || vp.fitWindow || vp.centerWindow ||
        vp.displayDocumentTitle || vp.readingDirection == PdfReadingDirection::RightToLeft ||
        vp.nonFullScreenPageMode != PdfPageMode::UseNone || vp.printScaling == PdfPrintScaling::None ||
        vp.duplex != PdfDuplexMode::Default || vp.pickTrayByPdfSize || vp.numberOfCopies != 1U) {
        objects[catalog] += " /ViewerPreferences <<";
        if (vp.hideToolbar) objects[catalog] += " /HideToolbar true";
        if (vp.hideMenuBar) objects[catalog] += " /HideMenubar true";
        if (vp.hideWindowUi) objects[catalog] += " /HideWindowUI true";
        if (vp.fitWindow) objects[catalog] += " /FitWindow true";
        if (vp.centerWindow) objects[catalog] += " /CenterWindow true";
        if (vp.displayDocumentTitle) objects[catalog] += " /DisplayDocTitle true";
        if (vp.readingDirection == PdfReadingDirection::RightToLeft) objects[catalog] += " /Direction /R2L";
        if (const auto* nonFullScreen = pageModeName(vp.nonFullScreenPageMode))
            objects[catalog] += std::string(" /NonFullScreenPageMode ") + nonFullScreen;
        if (vp.printScaling == PdfPrintScaling::None) objects[catalog] += " /PrintScaling /None";
        switch (vp.duplex) {
        case PdfDuplexMode::Simplex: objects[catalog] += " /Duplex /Simplex"; break;
        case PdfDuplexMode::DuplexFlipShortEdge: objects[catalog] += " /Duplex /DuplexFlipShortEdge"; break;
        case PdfDuplexMode::DuplexFlipLongEdge: objects[catalog] += " /Duplex /DuplexFlipLongEdge"; break;
        case PdfDuplexMode::Default: break;
        }
        if (vp.pickTrayByPdfSize) objects[catalog] += " /PickTrayByPDFSize true";
        if (vp.numberOfCopies != 1U) objects[catalog] += " /NumCopies " + std::to_string(vp.numberOfCopies);
        objects[catalog] += " >>";
    }
    objects[catalog] += " >>";
    if (hasDocumentInfo) {
        std::ostringstream infoDictionary;
        infoDictionary << "<<";
        appendInfoEntry(infoDictionary, "Title", state_->documentInfo.title);
        appendInfoEntry(infoDictionary, "Author", state_->documentInfo.author);
        appendInfoEntry(infoDictionary, "Subject", state_->documentInfo.subject);
        appendInfoEntry(infoDictionary, "Keywords", state_->documentInfo.keywords);
        appendInfoEntry(infoDictionary, "Creator", state_->documentInfo.creator);
        appendInfoEntry(infoDictionary, "Producer", state_->documentInfo.producer);
        appendInfoEntry(infoDictionary, "CreationDate", state_->documentInfo.creationDate);
        appendInfoEntry(infoDictionary, "ModDate", state_->documentInfo.modificationDate);
        infoDictionary << " >>";
        objects[infoObject] = infoDictionary.str();
    }
    std::ostringstream kids; for(int id:pageIds)kids<<id<<" 0 R "; objects[pages]="<< /Type /Pages /Count "+std::to_string(pageIds.size())+" /Kids [ "+kids.str()+"] >>";
    if (outlinesObject != 0) {
        const std::size_t rootIndex = state_->bookmarks.size();
        std::vector<std::vector<std::size_t>> children(state_->bookmarks.size() + 1U);
        for (std::size_t i = 0; i < state_->bookmarks.size(); ++i) {
            const auto parent = state_->bookmarks[i].parentIndex.value_or(rootIndex);
            children[parent].push_back(i);
        }

        std::function<std::size_t(std::size_t)> descendantCount = [&](const std::size_t index) {
            std::size_t count = 0;
            for (const auto child : children[index]) count += 1U + descendantCount(child);
            return count;
        };

        const auto& roots = children[rootIndex];
        std::ostringstream rootDictionary;
        rootDictionary << "<< /Type /Outlines /Count " << descendantCount(rootIndex);
        if (!roots.empty()) {
            rootDictionary << " /First " << bookmarkIds[roots.front()] << " 0 R"
                           << " /Last " << bookmarkIds[roots.back()] << " 0 R";
        }
        rootDictionary << " >>";
        objects[outlinesObject] = rootDictionary.str();

        const auto numberOrNull = [](const std::optional<double>& value) {
            if (!value) return std::string("null");
            std::ostringstream output;
            output << *value;
            return output.str();
        };

        for (std::size_t i = 0; i < state_->bookmarks.size(); ++i) {
            const auto& bookmark = state_->bookmarks[i];
            const std::size_t parent = bookmark.parentIndex.value_or(rootIndex);
            const auto& siblings = children[parent];
            const auto position = std::find(siblings.begin(), siblings.end(), i);
            const auto& ownChildren = children[i];

            std::ostringstream dictionary;
            dictionary << "<< /Title (" << escapePdfString(bookmark.title) << ")"
                       << " /Parent " << (parent == rootIndex ? outlinesObject : bookmarkIds[parent]) << " 0 R";
            if (position != siblings.begin()) dictionary << " /Prev " << bookmarkIds[*(position - 1)] << " 0 R";
            if (position + 1 != siblings.end()) dictionary << " /Next " << bookmarkIds[*(position + 1)] << " 0 R";
            if (!ownChildren.empty()) {
                const auto count = descendantCount(i);
                dictionary << " /First " << bookmarkIds[ownChildren.front()] << " 0 R"
                           << " /Last " << bookmarkIds[ownChildren.back()] << " 0 R"
                           << " /Count " << (bookmark.open ? static_cast<long long>(count) : -static_cast<long long>(count));
            }

            dictionary << " /Dest [" << pageIds[bookmark.pageIndex] << " 0 R ";
            switch (bookmark.destinationType) {
            case PdfBookmarkDestinationType::FitPage:
                dictionary << "/Fit";
                break;
            case PdfBookmarkDestinationType::FitWidth:
                dictionary << "/FitH " << numberOrNull(bookmark.top);
                break;
            case PdfBookmarkDestinationType::XYZ:
                dictionary << "/XYZ " << numberOrNull(bookmark.left) << ' '
                           << numberOrNull(bookmark.top) << ' ' << numberOrNull(bookmark.zoom);
                break;
            }
            dictionary << ']';
            if (bookmark.color) {
                dictionary << " /C [" << bookmark.color->r << ' ' << bookmark.color->g << ' ' << bookmark.color->b << ']';
            }
            const int flags = (bookmark.italic ? 1 : 0) | (bookmark.bold ? 2 : 0);
            if (flags != 0) dictionary << " /F " << flags;
            dictionary << " >>";
            objects[bookmarkIds[i]] = dictionary.str();
        }
    }
    if (destinationsObject != 0) {
        std::vector<std::reference_wrapper<const Internal::PdfWriterNamedDestination>> sorted;
        sorted.reserve(state_->namedDestinations.size());
        for (const auto& destination : state_->namedDestinations) sorted.emplace_back(destination);
        std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.get().name < rhs.get().name;
        });
        std::ostringstream names;
        names << "<< /Names [";
        for (const auto& destination : sorted) {
            names << '(' << escapePdfString(destination.get().name) << ") "
                  << destinationArray(destination.get(), pageIds) << ' ';
        }
        names << "] >>";
        objects[destinationsObject] = names.str();
    }
    if (embeddedFilesObject != 0) {
        std::vector<std::size_t> order(state_->embeddedFiles.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](const auto lhs, const auto rhs) {
            return state_->embeddedFiles[lhs].name < state_->embeddedFiles[rhs].name;
        });
        std::ostringstream names;
        names << "<< /Names [";
        for (const auto index : order) {
            names << '(' << escapePdfString(state_->embeddedFiles[index].name) << ") "
                  << embeddedFileIds[index].fileSpec << " 0 R ";
        }
        names << "] >>";
        objects[embeddedFilesObject] = names.str();
        for (std::size_t i = 0; i < state_->embeddedFiles.size(); ++i) {
            const auto& file = state_->embeddedFiles[i];
            std::string payload(reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size());
            std::string filter;
            if (file.options.compress && !file.bytes.empty()) {
                payload = compressBytes(file.bytes);
                filter = " /Filter /FlateDecode";
            }
            std::ostringstream stream;
            stream << "<< /Type /EmbeddedFile /Subtype /" << encodePdfName(file.options.mimeType)
                   << " /Params << /Size " << file.bytes.size();
            if (!file.options.creationDate.empty()) stream << " /CreationDate (" << escapePdfString(file.options.creationDate) << ')';
            if (!file.options.modificationDate.empty()) stream << " /ModDate (" << escapePdfString(file.options.modificationDate) << ')';
            stream << " >>" << filter << " /Length " << payload.size() << " >>\nstream\n";
            objects[embeddedFileIds[i].stream] = stream.str() + payload + "\nendstream";
            std::ostringstream spec;
            spec << "<< /Type /Filespec /F (" << escapePdfString(file.name) << ") /UF ("
                 << escapePdfString(file.name) << ')';
            if (!file.options.description.empty()) spec << " /Desc (" << escapePdfString(file.options.description) << ')';
            spec << " /EF << /F " << embeddedFileIds[i].stream << " 0 R /UF "
                 << embeddedFileIds[i].stream << " 0 R >> /AFRelationship "
                 << associatedFileRelationshipName(file.options.relationship) << " >>";
            objects[embeddedFileIds[i].fileSpec] = spec.str();
        }
    }
    if (pageLabelsObject != 0) {
        std::ostringstream labels;
        labels << "<< /Nums [";
        for (const auto& label : state_->pageLabels) {
            labels << label.pageIndex << " << /S " << pageLabelStyleName(label.options.style);
            if (!label.options.prefix.empty()) labels << " /P (" << escapePdfString(label.options.prefix) << ')';
            if (label.options.startNumber != 1U) labels << " /St " << label.options.startNumber;
            labels << " >> ";
        }
        labels << "] >>";
        objects[pageLabelsObject] = labels.str();
    }
    objects[base14Font]="<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    for(std::size_t i=0;i<state_->extGStates.size();++i){ const auto& gs=state_->extGStates[i]; const auto bm=gs.blendMode==PdfBlendMode::Multiply?"/Multiply":gs.blendMode==PdfBlendMode::Screen?"/Screen":gs.blendMode==PdfBlendMode::Darken?"/Darken":gs.blendMode==PdfBlendMode::Lighten?"/Lighten":gs.blendMode==PdfBlendMode::Overlay?"/Overlay":gs.blendMode==PdfBlendMode::Difference?"/Difference":gs.blendMode==PdfBlendMode::Exclusion?"/Exclusion":"/Normal"; std::ostringstream d; d<<"<< /Type /ExtGState /CA "<<gs.strokeOpacity<<" /ca "<<gs.fillOpacity<<" /BM "<<bm<<" >>"; objects[extGStateIds[i]]=d.str(); }
    for(std::size_t i=0;i<state_->images.size();++i){
        const auto& image=state_->images[i].image; std::string bytes; const char* filterName=nullptr;
        const auto encoding=image.GetEncoding();
        if(encoding==PdfImageEncoding::Dct){const auto span=image.GetBytes();bytes.assign(reinterpret_cast<const char*>(span.data()),span.size());filterName="/DCTDecode";}
        else if(encoding==PdfImageEncoding::Jpx){const auto span=image.GetBytes();bytes.assign(reinterpret_cast<const char*>(span.data()),span.size());filterName="/JPXDecode";}
        else if(encoding==PdfImageEncoding::CcittFax){const auto span=image.GetBytes();bytes.assign(reinterpret_cast<const char*>(span.data()),span.size());filterName="/CCITTFaxDecode";}
        else{bytes=compressBytes(image.GetBytes());filterName="/FlateDecode";}
        std::ostringstream d; d<<"<< /Type /XObject /Subtype /Image /Width "<<image.GetWidth()<<" /Height "<<image.GetHeight()<<" /ColorSpace "<<colorSpaceName(image.GetColorSpace())<<" /BitsPerComponent "<<image.GetBitsPerComponent()<<" /Filter "<<filterName<<" /Length "<<bytes.size()<<" >>\nstream\n"; objects[imageIds[i]]=d.str()+bytes+"\nendstream";
    }
    auto utf16Hex=[](std::uint32_t cp){ static constexpr char h[]="0123456789ABCDEF"; auto unit=[&](std::uint16_t v){std::string x(4,'0');for(int j=3;j>=0;--j){x[j]=h[v&15];v>>=4;}return x;}; if(cp<=0xFFFF)return unit(static_cast<std::uint16_t>(cp)); cp-=0x10000; return unit(static_cast<std::uint16_t>(0xD800+(cp>>10)))+unit(static_cast<std::uint16_t>(0xDC00+(cp&0x3FF))); };
    for(std::size_t i=0;i<state_->embeddedFonts.size();++i){
        const auto& ef=state_->embeddedFonts[i]; const auto& ids=fontIds[i];
        std::vector<std::uint16_t> requestedGlyphs; requestedGlyphs.reserve(ef.usedMappings.size() + 1U); requestedGlyphs.push_back(0);
        for (const auto& mapping : ef.usedMappings) requestedGlyphs.push_back(mapping.second);
        const auto subset = options.subsetTrueTypeFonts ? ef.font.BuildSubset(requestedGlyphs) : PdfTrueTypeSubset{ef.font.GetBytes(), requestedGlyphs, ef.font.GetBytes().size(), false};
        const auto& fontBytes = subset.bytes; std::string raw(reinterpret_cast<const char*>(fontBytes.data()),fontBytes.size());
        objects[ids.file]="<< /Length "+std::to_string(raw.size())+" /Length1 "+std::to_string(raw.size())+" >>\nstream\n"+raw+"\nendstream";
        std::string base=(subset.subsetApplied ? "PPABCD+" : "")+std::string("PdfPPEmbedded")+std::to_string(i+1); {
            const auto& metrics = ef.font.GetMetrics();
            const auto scale1000 = [&](std::int32_t value) { return static_cast<int>(std::lround(double(value) * 1000.0 / metrics.unitsPerEm)); };
            std::ostringstream widths;
            std::vector<std::uint16_t> glyphs; glyphs.reserve(ef.usedMappings.size());
            for (const auto& mapping : ef.usedMappings) glyphs.push_back(mapping.second);
            std::sort(glyphs.begin(), glyphs.end()); glyphs.erase(std::unique(glyphs.begin(), glyphs.end()), glyphs.end());
            for (const auto glyph : glyphs) widths << glyph << " [" << scale1000(ef.font.GetAdvanceWidth(glyph)) << "] ";
            objects[ids.descriptor]="<< /Type /FontDescriptor /FontName /"+base+" /Flags 32 /FontBBox [-1000 -1000 2000 2000] /ItalicAngle 0 /Ascent "+std::to_string(scale1000(metrics.ascent))+" /Descent "+std::to_string(scale1000(metrics.descent))+" /CapHeight "+std::to_string(scale1000(metrics.ascent))+" /StemV 80 /FontFile2 "+std::to_string(ids.file)+" 0 R >>";
            objects[ids.cid]="<< /Type /Font /Subtype /CIDFontType2 /BaseFont /"+base+" /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor "+std::to_string(ids.descriptor)+" 0 R /DW 1000 /W ["+widths.str()+"] /CIDToGIDMap /Identity >>";
        }
        std::vector<std::pair<std::uint32_t,std::uint16_t>> mappings=ef.usedMappings; std::sort(mappings.begin(),mappings.end(),[](auto a,auto b){return a.second<b.second;}); mappings.erase(std::unique(mappings.begin(),mappings.end()),mappings.end());
        std::ostringstream cmap; cmap<<"/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n/CMapName /PdfPPToUnicode def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        for(std::size_t pos=0;pos<mappings.size();pos+=100){const auto count=std::min<std::size_t>(100,mappings.size()-pos);cmap<<count<<" beginbfchar\n";for(std::size_t j=0;j<count;++j){const auto [cp,gid]=mappings[pos+j];cmap<<'<';cmap<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<gid<<"> <"<<utf16Hex(cp)<<">\n"<<std::dec;}cmap<<"endbfchar\n";}
        cmap<<"endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n"; const auto cm=cmap.str(); objects[ids.toUnicode]="<< /Length "+std::to_string(cm.size())+" >>\nstream\n"+cm+"endstream";
        objects[ids.type0]="<< /Type /Font /Subtype /Type0 /BaseFont /"+base+" /Encoding /Identity-H /DescendantFonts ["+std::to_string(ids.cid)+" 0 R] /ToUnicode "+std::to_string(ids.toUnicode)+" 0 R >>";
    }
    for (std::size_t i = 0; i < state_->type1Fonts.size(); ++i) {
        const auto& t1 = state_->type1Fonts[i].font;
        const std::string base = t1.GetFontName().empty() ? "PdfPPType1" : t1.GetFontName();
        const auto& raw = t1.GetBytes();
        std::string fontFile(reinterpret_cast<const char*>(raw.data()), raw.size());
        objects[type1FileIds[i]] = "<< /Length " + std::to_string(fontFile.size()) + " /Length1 "
            + std::to_string(fontFile.size()) + " >>\nstream\n" + fontFile + "\nendstream";
        objects[type1DescIds[i]] = "<< /Type /FontDescriptor /FontName /" + base
            + " /Flags 32 /FontBBox [-123 -251 1000 1000] /ItalicAngle 0 /Ascent 800 /Descent -200 "
            + "/CapHeight 700 /StemV 80 /FontFile " + std::to_string(type1FileIds[i]) + " 0 R >>";
        objects[type1Ids[i]] = "<< /Type /Font /Subtype /Type1 /BaseFont /" + base
            + " /FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding /FontDescriptor "
            + std::to_string(type1DescIds[i]) + " 0 R /Widths [";
        for (int c = 32; c <= 255; ++c) {
            objects[type1Ids[i]] += std::to_string(t1.GetGlyphWidth(static_cast<std::uint8_t>(c))) + " ";
        }
        objects[type1Ids[i]] += "] >>";
    }
    if (structTreeObject != 0) {
        // Minimal structure tree: a /StructTreeRoot with an empty /K array and
        // the default document role map.
        objects[structTreeObject] = "<< /Type /StructTreeRoot /K [] "
            "/ParentTree << /Nums [0 []] >> /RoleMap << /Document /Document >> >>";
    }
    for (std::size_t i = 0; i < state_->ocgs.size(); ++i) {
        objects[ocgIds[i]] = "<< /Type /OCG /Name (" + escapePdfString(state_->ocgs[i].name) + ") /Usage << >> >>";
    }
    if (ocPropertiesObject != 0) {
        std::ostringstream properties;
        properties << "<< /OCGs [";
        for (const auto id : ocgIds) properties << id << " 0 R ";
        properties << "] /D << /Order [] /ListMode /AllPages ";
        std::ostringstream defaultState;
        defaultState << "<< /ON [";
        for (std::size_t i = 0; i < state_->ocgs.size(); ++i) {
            if (state_->ocgs[i].visible) defaultState << ocgIds[i] << " 0 R ";
        }
        defaultState << "] >>";
        properties << "/Default " << defaultState.str() << " >> >>";
        objects[ocPropertiesObject] = properties.str();
    }
    for(std::size_t i=0;i<state_->pages.size();++i){
        const auto&p=state_->pages[i];std::ostringstream box;box<<p.mediaBox.left<<' '<<p.mediaBox.bottom<<' '<<p.mediaBox.right<<' '<<p.mediaBox.top;
        std::ostringstream fonts; fonts<<"/F1 "<<base14Font<<" 0 R "; for(const auto fi:p.embeddedFontIndices)fonts<<'/'<<state_->embeddedFonts.at(fi).resourceName<<' '<<fontIds.at(fi).type0<<" 0 R "; for(const auto ti:p.type1FontIndices)fonts<<'/'<<state_->type1Fonts.at(ti).resourceName<<' '<<type1Ids.at(ti)<<" 0 R ";
        std::ostringstream xObjects;for(const auto ii:p.imageIndices)xObjects<<'/'<<state_->images.at(ii).resourceName<<' '<<imageIds.at(ii)<<" 0 R ";
        std::string resources="<< /Font << "+fonts.str()+">>";if(!p.imageIndices.empty())resources+=" /XObject << "+xObjects.str()+">>";if(!p.extGStateIndices.empty()){std::ostringstream gs;for(const auto gi:p.extGStateIndices)gs<<'/'<<state_->extGStates.at(gi).resourceName<<' '<<extGStateIds.at(gi)<<" 0 R ";resources+=" /ExtGState << "+gs.str()+">>";}if(!p.ocgResources.empty()){std::ostringstream props;props<<" /Properties << ";for(const auto& name:p.ocgResources){const std::size_t index=name.size()>2?static_cast<std::size_t>(std::stoul(name.substr(2)))-1U:0U;if(index<state_->ocgs.size())props<<'/'<<name<<' '<<ocgIds[index]<<" 0 R ";}props<<">>";resources+=props.str();}resources+=" >>";
        std::string annotations;
        if (!linkIds[i].empty() || !attachmentIds[i].empty()) {
            annotations = " /Annots [";
            for (const auto id : linkIds[i]) annotations += std::to_string(id) + " 0 R ";
            for (const auto id : attachmentIds[i]) annotations += std::to_string(id) + " 0 R ";
            annotations += ']';
        }
        objects[pageIds[i]]="<< /Type /Page /Parent "+std::to_string(pages)+" 0 R /MediaBox ["+box.str()+"] /Resources "+resources+" /Contents "+std::to_string(contentIds[i])+" 0 R"+annotations+" >>";objects[contentIds[i]]="<< /Length "+std::to_string(p.content.size())+" >>\nstream\n"+p.content+"endstream";
        for (std::size_t j = 0; j < p.links.size(); ++j) {
            const auto& link = p.links[j];
            const auto& rectangle = link.options.rectangle;
            std::ostringstream annotation;
            annotation << "<< /Type /Annot /Subtype /Link /Rect ["
                       << rectangle.left << ' ' << rectangle.bottom << ' '
                       << rectangle.right << ' ' << rectangle.top << "] /P "
                       << pageIds[i] << " 0 R";
            if (link.options.drawBorder) {
                annotation << " /Border [0 0 " << link.options.borderWidth << "] /C ["
                           << link.options.borderColor.r << ' ' << link.options.borderColor.g << ' '
                           << link.options.borderColor.b << ']';
            } else {
                annotation << " /Border [0 0 0]";
            }
            if (link.kind == Internal::PdfWriterLinkKind::NamedDestination) {
                annotation << " /Dest (" << escapePdfString(link.target) << ')';
            } else {
                annotation << " /A << /S /URI /URI (" << escapePdfString(link.target) << ") >>";
            }
            annotation << " >>";
            objects[linkIds[i][j]] = annotation.str();
        }
        for (std::size_t j = 0; j < p.fileAttachments.size(); ++j) {
            const auto& attachment = p.fileAttachments[j];
            const auto file = std::find_if(state_->embeddedFiles.begin(), state_->embeddedFiles.end(),
                [&](const auto& item) { return item.name == attachment.embeddedFileName; });
            if (file == state_->embeddedFiles.end()) throw std::runtime_error("File attachment target does not exist");
            const auto fileIndex = static_cast<std::size_t>(std::distance(state_->embeddedFiles.begin(), file));
            const auto& rectangle = attachment.options.rectangle;
            std::ostringstream annotation;
            annotation << "<< /Type /Annot /Subtype /FileAttachment /Rect ["
                       << rectangle.left << ' ' << rectangle.bottom << ' '
                       << rectangle.right << ' ' << rectangle.top << "] /P " << pageIds[i] << " 0 R"
                       << " /FS " << embeddedFileIds[fileIndex].fileSpec << " 0 R"
                       << " /Name " << fileAttachmentIconName(attachment.options.icon);
            if (!attachment.options.contents.empty()) {
                annotation << " /Contents (" << escapePdfString(attachment.options.contents) << ')';
            }
            annotation << " >>";
            objects[attachmentIds[i][j]] = annotation.str();
        }
    }
    if (security) objects[encryptionObject] = security->EncryptionDictionary();

    Internal::PdfObjectCollectionWriterOptions collectionOptions;
    collectionOptions.writeXrefStream = options.writeXrefStream;
    collectionOptions.writeObjectStreams = options.writeObjectStreams;
    Internal::PdfObjectCollectionWriter::Write(
        out, collectionOptions, objects,
        static_cast<std::size_t>(catalog),
        hasDocumentInfo ? static_cast<std::size_t>(infoObject) : 0U,
        static_cast<std::size_t>(encryptionObject),
        security ? &*security : nullptr,
        fileId);
}

void PdfWriter::Resave(const PdfDocument& document,
                       const std::filesystem::path& outputPath,
                       const PdfSaveOptions& options) {
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot create PDF output file");
    if (options.mode == PdfSaveMode::Incremental) {
        throw std::runtime_error("Incremental resave is not supported; use the incremental editors.");
    }

    const PdfReference root = document.GetCatalogReference();

    // Collect every object reachable from the catalog (and the document /Info
    // dictionary, which the trailer references directly). Missing objects are
    // skipped so damaged sources still resave.
    std::vector<std::string> objects(1);
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    std::vector<PdfReference> queue{root};
    const auto infoReference = document.GetTrailerReference(PdfName("Info"));
    if (infoReference) queue.push_back(*infoReference);
    std::size_t pending = 0U;
    std::size_t catalogObject = 0U;
    std::size_t infoObject = 0U;
    while (pending < queue.size()) {
        const PdfReference reference = queue[pending++];
        if (remap.contains(reference.objectNumber)) continue;
        const PdfObject* parsed = nullptr;
        try {
            parsed = &document.GetObject(reference);
        } catch (const PdfException&) {
            continue;
        }
        if (parsed == nullptr) continue;
        const std::uint32_t newNumber = static_cast<std::uint32_t>(objects.size());
        remap[reference.objectNumber] = newNumber;
        objects.emplace_back();
        if (reference == root) catalogObject = newNumber;
        if (infoReference && reference == *infoReference) infoObject = newNumber;
        collectReferencesFromObject(*parsed, queue);
    }
    if (catalogObject == 0U) {
        throw std::runtime_error("Resave: the document catalog could not be read.");
    }

    // Serialize every reachable object with remapped reference numbers. This
    // first pass uses the provisional numbers so identical bodies can be
    // recognized before the final numbering is assigned.
    std::vector<std::string> provisional(objects.size());
    for (const auto& [oldNumber, newNumber] : remap) {
        (void)oldNumber;
        std::ostringstream body;
        const PdfObject& object = document.GetObject(PdfReference{oldNumber, 0U});
        Internal::PdfObjectSerializer::WriteObject(body, object, [&remap](const PdfReference& reference) {
            const auto mapped = remap.find(reference.objectNumber);
            if (mapped == remap.end()) return reference;
            return PdfReference{mapped->second, 0U};
        });
        provisional[newNumber] = body.str();
    }

    // Deduplicate byte-identical objects. Only streams (fonts, images, content
    // streams) are merged: sharing a stream by reference is always safe, while
    // two identical dictionaries can still be distinct entities (e.g. two
    // empty page objects) that must not collapse into one.
    std::vector<std::uint32_t> canonical(objects.size(), 0U);
    if (options.deduplicateObjects) {
        std::unordered_map<std::string_view, std::uint32_t> canonicalByBody;
        for (std::uint32_t i = 1U; i < objects.size(); ++i) {
            const std::string& body = provisional[i];
            if (body.find("endstream") == std::string::npos) {
                canonical[i] = i;
                continue;
            }
            const auto found = canonicalByBody.find(body);
            if (found == canonicalByBody.end()) {
                canonicalByBody.emplace(body, i);
                canonical[i] = i;
            } else {
                canonical[i] = found->second;
            }
        }
    } else {
        for (std::uint32_t i = 1U; i < objects.size(); ++i) canonical[i] = i;
    }

    // Compact the numbering: every canonical object keeps one final number.
    std::vector<std::uint32_t> finalNumber(objects.size(), 0U);
    std::uint32_t nextFinal = 1U;
    for (std::uint32_t i = 1U; i < objects.size(); ++i) {
        if (canonical[i] == i) finalNumber[i] = nextFinal++;
    }

    // Re-serialize with the final mapper so every reference (including the
    // dropped duplicates) points at the surviving canonical object.
    std::vector<std::string> deduped(nextFinal);
    std::size_t catalogFinal = 0U;
    std::size_t infoFinal = 0U;
    for (const auto& [oldNumber, newNumber] : remap) {
        const std::uint32_t final = finalNumber[canonical[newNumber]];
        if (final == 0U) continue;
        std::ostringstream body;
        const PdfObject& object = document.GetObject(PdfReference{oldNumber, 0U});
        Internal::PdfObjectSerializer::WriteObject(body, object,
            [&remap, &canonical, &finalNumber](const PdfReference& reference) {
                const auto mapped = remap.find(reference.objectNumber);
                if (mapped == remap.end()) return reference;
                return PdfReference{finalNumber[canonical[mapped->second]], 0U};
            });
        deduped[final] = body.str();
        if (newNumber == catalogObject) catalogFinal = final;
        if (infoObject != 0U && newNumber == infoObject) infoFinal = final;
    }
    objects = std::move(deduped);
    catalogObject = catalogFinal;
    infoObject = infoFinal;

    const Internal::PdfStandardSecurity* security = nullptr;
    std::array<std::uint8_t, 16> fileId{};
    std::size_t encryptionObject = 0U;
    if (document.IsEncrypted()) {
        // Preserve the source encryption: reuse its security handler and file
        // ID so the same passwords continue to unlock the resaved file.
        security = document.encryption_.get();
        fileId = document.encryption_->FileId();
        encryptionObject = objects.size();
        objects.emplace_back();
        objects[encryptionObject] = security->EncryptionDictionary();
    } else {
        fileId = Internal::GeneratePdfFileId();
    }

    Internal::PdfObjectCollectionWriterOptions collectionOptions;
    collectionOptions.writeXrefStream = options.writeXrefStream;
    collectionOptions.writeObjectStreams = options.writeObjectStreams;
    Internal::PdfObjectCollectionWriter::Write(
        output, collectionOptions, objects, catalogObject, infoObject,
        encryptionObject, security, fileId);
}

void PdfWriter::Resave(const std::filesystem::path& inputPath,
                       const std::filesystem::path& outputPath,
                       const PdfSaveOptions& options) {
    Resave(PdfDocument::Open(inputPath), outputPath, options);
}

void PdfWriter::Resave(const std::filesystem::path& inputPath,
                       const std::filesystem::path& outputPath,
                       const PdfReaderOptions& readerOptions,
                       const PdfSaveOptions& options) {
    Resave(PdfDocument::Open(inputPath, readerOptions), outputPath, options);
}
} // namespace CPPPdf
