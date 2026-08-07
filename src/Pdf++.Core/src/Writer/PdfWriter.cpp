#include <CPPPdf/Writer/PdfWriter.hpp>
#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "PdfWriterState.hpp"
#include "Internal/Security/PdfStandardSecurity.hpp"
#include "Internal/Security/PdfCrypto.hpp"
#include "Internal/Writer/PdfObjectCollectionWriter.hpp"
#include "Internal/Writer/PdfIncrementalWriter.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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


class MeshBitWriter final {
public:
    void Write(std::uint32_t value, const unsigned int bitCount) {
        for (unsigned int bit = bitCount; bit > 0U; --bit) {
            current_ = static_cast<std::uint8_t>((current_ << 1U) | ((value >> (bit - 1U)) & 1U));
            ++usedBits_;
            if (usedBits_ == 8U) {
                bytes_.push_back(static_cast<std::byte>(current_));
                current_ = 0U;
                usedBits_ = 0U;
            }
        }
    }

    [[nodiscard]] std::vector<std::byte> Finish() {
        if (usedBits_ != 0U) {
            current_ = static_cast<std::uint8_t>(current_ << (8U - usedBits_));
            bytes_.push_back(static_cast<std::byte>(current_));
            current_ = 0U;
            usedBits_ = 0U;
        }
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
    std::uint8_t current_{};
    unsigned int usedBits_{};
};

struct MeshBounds final {
    double minX{};
    double maxX{};
    double minY{};
    double maxY{};
};

void includeMeshPoint(MeshBounds& bounds, const PdfPoint point, const bool first) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        throw std::invalid_argument("Mesh shading coordinates must be finite.");
    }
    if (first) {
        bounds = {point.x, point.x, point.y, point.y};
        return;
    }
    bounds.minX = std::min(bounds.minX, point.x);
    bounds.maxX = std::max(bounds.maxX, point.x);
    bounds.minY = std::min(bounds.minY, point.y);
    bounds.maxY = std::max(bounds.maxY, point.y);
}

[[nodiscard]] MeshBounds meshBounds(const Internal::PdfWriterMeshShading& shading) {
    MeshBounds bounds{};
    bool first = true;
    for (const auto& vertex : shading.vertices) {
        includeMeshPoint(bounds, vertex.position, first);
        first = false;
    }
    for (const auto& patch : shading.patches) {
        for (const auto point : patch.controlPoints) {
            includeMeshPoint(bounds, point, first);
            first = false;
        }
    }
    if (first || bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        throw std::invalid_argument("Mesh shading coordinates must span a non-zero area.");
    }
    return bounds;
}

[[nodiscard]] std::uint32_t quantizeMeshValue(const double value, const double minimum,
                                               const double maximum, const std::uint32_t levels) {
    const double normalized = std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0);
    return static_cast<std::uint32_t>(std::lround(normalized * static_cast<double>(levels)));
}

void writeMeshPoint(MeshBitWriter& writer, const PdfPoint point, const MeshBounds& bounds) {
    writer.Write(quantizeMeshValue(point.x, bounds.minX, bounds.maxX, 65535U), 16U);
    writer.Write(quantizeMeshValue(point.y, bounds.minY, bounds.maxY, 65535U), 16U);
}

void writeMeshColor(MeshBitWriter& writer, const std::span<const double> components) {
    for (const double component : components) {
        writer.Write(static_cast<std::uint32_t>(std::lround(component * 255.0)), 8U);
    }
}

[[nodiscard]] std::vector<std::byte> encodeMeshShading(
    const Internal::PdfWriterMeshShading& shading, const MeshBounds& bounds) {
    MeshBitWriter writer;
    if (shading.kind == Internal::PdfWriterMeshShadingKind::FreeForm ||
        shading.kind == Internal::PdfWriterMeshShadingKind::Lattice) {
        for (const auto& vertex : shading.vertices) {
            if (shading.kind == Internal::PdfWriterMeshShadingKind::FreeForm) {
                // Independent triangles are represented by groups of three flag-0 vertices.
                writer.Write(0U, 2U);
            }
            writeMeshPoint(writer, vertex.position, bounds);
            writeMeshColor(writer, vertex.colorComponents);
        }
        return writer.Finish();
    }

    // Patch meshes are intentionally emitted as independent flag-0 records.
    // This is larger than edge-reused records but avoids hidden state and is
    // accepted by conforming Type 6/7 decoders.
    for (const auto& patch : shading.patches) {
        writer.Write(0U, 2U);
        for (const auto point : patch.controlPoints) writeMeshPoint(writer, point, bounds);
        for (const auto& color : patch.cornerColors) writeMeshColor(writer, color);
    }
    return writer.Finish();
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

[[nodiscard]] const char* structurePlacementName(const PdfStructurePlacement value) {
    switch (value) {
    case PdfStructurePlacement::Block: return "/Block";
    case PdfStructurePlacement::Inline: return "/Inline";
    case PdfStructurePlacement::Before: return "/Before";
    case PdfStructurePlacement::Start: return "/Start";
    case PdfStructurePlacement::End: return "/End";
    }
    return "/Block";
}

[[nodiscard]] const char* structureTextAlignmentName(const PdfStructureTextAlignment value) {
    switch (value) {
    case PdfStructureTextAlignment::Start: return "/Start";
    case PdfStructureTextAlignment::Center: return "/Center";
    case PdfStructureTextAlignment::End: return "/End";
    case PdfStructureTextAlignment::Justify: return "/Justify";
    }
    return "/Start";
}

[[nodiscard]] const char* tableScopeName(const PdfTableScope value) {
    switch (value) {
    case PdfTableScope::Row: return "/Row";
    case PdfTableScope::Column: return "/Column";
    case PdfTableScope::Both: return "/Both";
    case PdfTableScope::None: return nullptr;
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

[[nodiscard]] bool isValidResourceName(const std::string_view name) noexcept {
    if (name.empty() || name.front() == '/') return false;
    constexpr std::string_view delimiters{"()<>[]{}/%#"};
    for (const unsigned char ch : name) {
        if (ch <= 0x20U || ch >= 0x7FU ||
            delimiters.find(static_cast<char>(ch)) != std::string_view::npos) return false;
    }
    return true;
}

[[nodiscard]] const char* deviceColorSpaceName(const PdfDeviceColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case PdfDeviceColorSpace::Gray: return "/DeviceGray";
    case PdfDeviceColorSpace::Rgb: return "/DeviceRGB";
    case PdfDeviceColorSpace::Cmyk: return "/DeviceCMYK";
    }
    return "/DeviceRGB";
}

[[nodiscard]] std::size_t deviceColorSpaceComponents(const PdfDeviceColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case PdfDeviceColorSpace::Gray: return 1U;
    case PdfDeviceColorSpace::Rgb: return 3U;
    case PdfDeviceColorSpace::Cmyk: return 4U;
    }
    return 3U;
}

void validateUnitComponents(const std::span<const double> values, const char* label) {
    for (const double value : values) {
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
            throw std::invalid_argument(std::string(label) + " components must be finite values in [0, 1].");
        }
    }
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

[[nodiscard]] const char* blendModeName(const PdfBlendMode mode) noexcept {
    switch (mode) {
    case PdfBlendMode::Multiply: return "/Multiply";
    case PdfBlendMode::Screen: return "/Screen";
    case PdfBlendMode::Overlay: return "/Overlay";
    case PdfBlendMode::Darken: return "/Darken";
    case PdfBlendMode::Lighten: return "/Lighten";
    case PdfBlendMode::ColorDodge: return "/ColorDodge";
    case PdfBlendMode::ColorBurn: return "/ColorBurn";
    case PdfBlendMode::HardLight: return "/HardLight";
    case PdfBlendMode::SoftLight: return "/SoftLight";
    case PdfBlendMode::Difference: return "/Difference";
    case PdfBlendMode::Exclusion: return "/Exclusion";
    case PdfBlendMode::Hue: return "/Hue";
    case PdfBlendMode::Saturation: return "/Saturation";
    case PdfBlendMode::Color: return "/Color";
    case PdfBlendMode::Luminosity: return "/Luminosity";
    case PdfBlendMode::SourceOver: return "/Normal";
    }
    return "/Normal";
}

// Encodes a byte string as a hex `<...>` literal (PDF string form).
std::string hexLookup(const std::string& bytes) {
    static constexpr char h[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2U);
    for (const unsigned char c : bytes) {
        out.push_back(h[c >> 4U]);
        out.push_back(h[c & 0x0FU]);
    }
    return out;
}

[[nodiscard]] std::string currentPdfDateUtc() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &now) != 0) return {};
#else
    if (gmtime_r(&now, &utc) == nullptr) return {};
#endif
    std::ostringstream value;
    value << "D:" << std::put_time(&utc, "%Y%m%d%H%M%SZ");
    return value.str();
}


struct PdfAWriterRequirements {
    int part{};
    char conformance{};
    bool tagged{};
    bool forbidsTransparency{};
    bool permitsArbitraryEmbeddedFiles{};
    bool engineeringVariant{};
    std::string pdfVersion;
};

struct PdfUaWriterRequirements {
    int part{};
    int revision{};
    std::string pdfVersion;
};

[[nodiscard]] std::optional<PdfAWriterRequirements> pdfAWriterRequirements(
    const PdfConformanceProfile profile) {
    switch (profile) {
    case PdfConformanceProfile::PdfA1A: return PdfAWriterRequirements{1, 'A', true, true, false, false, "1.4"};
    case PdfConformanceProfile::PdfA1B: return PdfAWriterRequirements{1, 'B', false, true, false, false, "1.4"};
    case PdfConformanceProfile::PdfA2A: return PdfAWriterRequirements{2, 'A', true, false, false, false, "1.7"};
    case PdfConformanceProfile::PdfA2B: return PdfAWriterRequirements{2, 'B', false, false, false, false, "1.7"};
    case PdfConformanceProfile::PdfA2U: return PdfAWriterRequirements{2, 'U', false, false, false, false, "1.7"};
    case PdfConformanceProfile::PdfA3A: return PdfAWriterRequirements{3, 'A', true, false, true, false, "1.7"};
    case PdfConformanceProfile::PdfA3B: return PdfAWriterRequirements{3, 'B', false, false, true, false, "1.7"};
    case PdfConformanceProfile::PdfA3U: return PdfAWriterRequirements{3, 'U', false, false, true, false, "1.7"};
    case PdfConformanceProfile::PdfA4: return PdfAWriterRequirements{4, '\0', false, false, false, false, "2.0"};
    case PdfConformanceProfile::PdfA4E: return PdfAWriterRequirements{4, 'E', false, false, true, true, "2.0"};
    case PdfConformanceProfile::PdfA4F: return PdfAWriterRequirements{4, 'F', false, false, true, false, "2.0"};
    default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<PdfUaWriterRequirements> pdfUaWriterRequirements(
    const PdfConformanceProfile profile) {
    switch (profile) {
    case PdfConformanceProfile::PdfUA1: return PdfUaWriterRequirements{1, 0, "1.7"};
    case PdfConformanceProfile::PdfUA2: return PdfUaWriterRequirements{2, 2024, "2.0"};
    default: return std::nullopt;
    }
}

[[nodiscard]] bool compatibleConformanceProfiles(
    const std::optional<PdfConformanceProfile> pdfAProfile,
    const std::optional<PdfConformanceProfile> pdfUaProfile) {
    if (!pdfAProfile || !pdfUaProfile) return true;
    const auto pdfA = pdfAWriterRequirements(*pdfAProfile);
    const auto pdfUa = pdfUaWriterRequirements(*pdfUaProfile);
    if (!pdfA || !pdfUa) return false;
    return pdfUa->part == 1 ? (pdfA->part == 2 || pdfA->part == 3) : pdfA->part == 4;
}

[[nodiscard]] std::string xmlEscape(const std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&apos;"; break;
        default: output.push_back(character); break;
        }
    }
    return output;
}


[[nodiscard]] bool isValidLanguageTag(const std::string_view value) {
    if (value.empty() || value.front() == '-' || value.back() == '-') return false;
    std::size_t segmentLength = 0U;
    for (const unsigned char character : value) {
        if (character == '-') {
            if (segmentLength == 0U || segmentLength > 8U) return false;
            segmentLength = 0U;
            continue;
        }
        if (!std::isalnum(character)) return false;
        ++segmentLength;
        if (segmentLength > 8U) return false;
    }
    return segmentLength != 0U;
}

[[nodiscard]] bool isKnownWriterRole(const std::string_view role, const int uaPart) {
    static const std::unordered_set<std::string> common = {
        "Document", "Part", "Art", "Sect", "Div", "BlockQuote", "Caption", "TOC", "TOCI",
        "Index", "NonStruct", "Private", "H", "H1", "H2", "H3", "H4", "H5", "H6", "P",
        "L", "LI", "Lbl", "LBody", "Table", "TR", "TH", "TD", "THead", "TBody", "TFoot",
        "Span", "Quote", "Note", "Reference", "BibEntry", "Code", "Link", "Annot", "Ruby",
        "RB", "RT", "RP", "Warichu", "WT", "WP", "Figure", "Formula", "Form"
    };
    if (common.contains(std::string(role))) return true;
    if (uaPart == 2) {
        static const std::unordered_set<std::string> pdf20 = {
            "DocumentFragment", "Aside", "Title", "FENote", "Sub", "Em", "Strong", "Artifact"
        };
        return pdf20.contains(std::string(role));
    }
    return false;
}

[[nodiscard]] std::string resolveWriterRole(
    const Internal::PdfWriterState& state, std::string role, const int uaPart) {
    std::unordered_set<std::string> visited;
    for (std::size_t depth = 0U; depth < 32U; ++depth) {
        if (isKnownWriterRole(role, uaPart)) return role;
        if (!visited.insert(role).second) {
            throw std::runtime_error("PDF/UA RoleMap contains a cycle involving /" + role + '.');
        }
        const auto mapping = std::find_if(
            state.taggedRoleMap.begin(), state.taggedRoleMap.end(),
            [&](const auto& item) { return item.first == role; });
        if (mapping == state.taggedRoleMap.end()) {
            throw std::runtime_error("PDF/UA structure role /" + role + " is neither standard nor role-mapped.");
        }
        role = mapping->second;
    }
    throw std::runtime_error("PDF/UA RoleMap chain is too deep.");
}

void validateWriterSemanticStructure(const Internal::PdfWriterState& state, const int uaPart) {
    if (!isValidLanguageTag(state.language)) {
        throw std::runtime_error("PDF/UA document language must be a valid BCP 47-style language tag.");
    }
    for (const auto& [customRole, standardRole] : state.taggedRoleMap) {
        if (customRole.empty() || standardRole.empty()) {
            throw std::runtime_error("PDF/UA RoleMap entries require non-empty role names.");
        }
        (void)resolveWriterRole(state, customRole, uaPart);
    }

    std::unordered_set<std::string> identifiers;
    std::unordered_set<std::string> tableHeaderIdentifiers;
    for (const auto& page : state.pages) {
        for (const auto& item : page.markedContents) {
            const std::string role = resolveWriterRole(state, item.role, uaPart);
            if (!item.language.empty() && !isValidLanguageTag(item.language)) {
                throw std::runtime_error("PDF/UA structure element /Lang is not a valid BCP 47-style language tag.");
            }
            if (!item.identifier.empty() && !identifiers.insert(item.identifier).second) {
                throw std::runtime_error("PDF/UA structure element IDs must be unique.");
            }
            if (item.attributes.rowSpan == 0U || item.attributes.columnSpan == 0U) {
                throw std::runtime_error("PDF/UA table RowSpan and ColSpan values must be positive.");
            }
            if (role == "TH" && !item.identifier.empty()) tableHeaderIdentifiers.insert(item.identifier);
        }
    }

    const auto allowedChild = [](const std::string_view parent, const std::string_view child) {
        if (parent == "L") return child == "LI";
        if (parent == "LI") return child == "Lbl" || child == "LBody";
        if (parent == "Table") {
            return child == "Caption" || child == "TR" || child == "THead" ||
                   child == "TBody" || child == "TFoot";
        }
        if (parent == "THead" || parent == "TBody" || parent == "TFoot") return child == "TR";
        if (parent == "TR") return child == "TH" || child == "TD";
        if (parent == "TOC") return child == "TOCI";
        if (parent == "Ruby") return child == "RB" || child == "RT" || child == "RP";
        if (parent == "Warichu") return child == "WT" || child == "WP";
        return true;
    };

    for (const auto& page : state.pages) {
        for (std::size_t index = 0U; index < page.markedContents.size(); ++index) {
            const auto& item = page.markedContents[index];
            const std::string role = resolveWriterRole(state, item.role, uaPart);
            std::string parentRole = "Document";
            if (item.parentIndex) {
                if (*item.parentIndex >= page.markedContents.size()) {
                    throw std::runtime_error("PDF/UA structure parent index is invalid.");
                }
                parentRole = resolveWriterRole(state, page.markedContents[*item.parentIndex].role, uaPart);
            }

            if ((role == "LI" && parentRole != "L") ||
                ((role == "Lbl" || role == "LBody") && parentRole != "LI") ||
                ((role == "THead" || role == "TBody" || role == "TFoot") && parentRole != "Table") ||
                (role == "TR" && parentRole != "Table" && parentRole != "THead" &&
                 parentRole != "TBody" && parentRole != "TFoot") ||
                ((role == "TH" || role == "TD") && parentRole != "TR") ||
                (role == "TOCI" && parentRole != "TOC") ||
                ((role == "RB" || role == "RT" || role == "RP") && parentRole != "Ruby") ||
                ((role == "WT" || role == "WP") && parentRole != "Warichu")) {
                throw std::runtime_error("PDF/UA structure role /" + role +
                                         " is not permitted beneath /" + parentRole + '.');
            }

            std::unordered_map<std::string, std::size_t> childCounts;
            for (const auto childIndex : item.childIndices) {
                if (childIndex >= page.markedContents.size()) {
                    throw std::runtime_error("PDF/UA structure child index is invalid.");
                }
                const std::string childRole = resolveWriterRole(state, page.markedContents[childIndex].role, uaPart);
                if (!allowedChild(role, childRole)) {
                    throw std::runtime_error("PDF/UA structure role /" + childRole +
                                             " is not permitted beneath /" + role + '.');
                }
                ++childCounts[childRole];
            }
            if (role == "L" && childCounts["LI"] == 0U) {
                throw std::runtime_error("PDF/UA list elements require at least one /LI child.");
            }
            if (role == "LI" && (childCounts["LBody"] != 1U || childCounts["Lbl"] > 1U)) {
                throw std::runtime_error("PDF/UA /LI requires exactly one /LBody and no more than one /Lbl.");
            }
            if (role == "Table" && item.childIndices.empty()) {
                throw std::runtime_error("PDF/UA table elements require rows or table row groups.");
            }
            if (role == "TR" && childCounts["TH"] + childCounts["TD"] == 0U) {
                throw std::runtime_error("PDF/UA table rows require at least one /TH or /TD child.");
            }
            if (role == "Ruby" && (childCounts["RB"] == 0U || childCounts["RT"] == 0U)) {
                throw std::runtime_error("PDF/UA ruby elements require /RB and /RT children.");
            }
            if (role == "Warichu" && (childCounts["WT"] == 0U || childCounts["WP"] == 0U)) {
                throw std::runtime_error("PDF/UA warichu elements require /WT and /WP children.");
            }
            for (const auto& header : item.attributes.headers) {
                if (!tableHeaderIdentifiers.contains(header)) {
                    throw std::runtime_error("PDF/UA table /Headers entry does not reference a /TH structure ID: " + header);
                }
            }
        }
    }
}

[[nodiscard]] bool isPaintingContentEvent(const PdfContentEvent& event) {
    switch (event.type) {
    case PdfContentEventType::RenderText:
    case PdfContentEventType::InvokeXObject:
    case PdfContentEventType::RenderInlineImage:
    case PdfContentEventType::PaintShading:
        return true;
    case PdfContentEventType::RenderPath:
        return event.operation == "S" || event.operation == "s" || event.operation == "f" ||
               event.operation == "F" || event.operation == "f*" || event.operation == "B" ||
               event.operation == "B*" || event.operation == "b" || event.operation == "b*";
    default: return false;
    }
}

void validateWriterTaggedContent(const Internal::PdfWriterState& state) {
    const int uaPart = state.pdfUaProfile && *state.pdfUaProfile == PdfConformanceProfile::PdfUA2 ? 2 : 1;
    validateWriterSemanticStructure(state, uaPart);
    for (std::size_t pageIndex = 0; pageIndex < state.pages.size(); ++pageIndex) {
        const auto& page = state.pages[pageIndex];
        for (const auto& item : page.markedContents) {
            if ((item.role == "Figure" || item.role == "Formula") &&
                item.alternativeText.empty() && item.actualText.empty()) {
                throw std::runtime_error("PDF/UA Figure and Formula elements require alternative or actual text.");
            }
            if (item.role == "TH" && item.attributes.scope == PdfTableScope::None &&
                item.attributes.headers.empty()) {
                throw std::runtime_error("PDF/UA table header cells require /Scope or /Headers.");
            }
        }
        struct MarkedState { bool semantic{}; bool artifact{}; };
        std::vector<MarkedState> stack;
        bool untaggedPainting = false;
        PdfContentProcessor processor;
        processor.SetHandler([&](const PdfContentEvent& event) {
            if (event.type == PdfContentEventType::BeginMarkedContent) {
                stack.push_back({event.markedContentProperty.find("/MCID") != std::string::npos,
                                 event.text == "Artifact"});
                return;
            }
            if (event.type == PdfContentEventType::EndMarkedContent) {
                if (!stack.empty()) stack.pop_back();
                return;
            }
            if (!isPaintingContentEvent(event)) return;
            const bool covered = std::any_of(stack.begin(), stack.end(), [](const auto& entry) {
                return entry.semantic || entry.artifact;
            });
            if (!covered) untaggedPainting = true;
        });
        processor.Process(page.content);
        if (untaggedPainting) {
            throw std::runtime_error("PDF/UA real page content must be tagged or marked as an Artifact.");
        }
        for (const auto& link : page.links) {
            if (link.options.accessibleDescription.empty()) {
                throw std::runtime_error("PDF/UA link annotations require an accessible description.");
            }
            if (link.options.structureRole.empty()) {
                throw std::runtime_error("PDF/UA link annotations require a structure role.");
            }
        }
        for (const auto& attachment : page.fileAttachments) {
            if (attachment.options.contents.empty() && attachment.options.alternativeText.empty()) {
                throw std::runtime_error("PDF/UA file attachments require accessible text.");
            }
        }
    }
}

[[nodiscard]] bool contentUsesBase14Font(const std::string_view content) {
    std::size_t position = 0U;
    while ((position = content.find("/F1", position)) != std::string_view::npos) {
        const std::size_t after = position + 3U;
        if (after == content.size() || std::isspace(static_cast<unsigned char>(content[after]))) {
            return true;
        }
        position = after;
    }
    return false;
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

std::size_t PdfWriter::AddTilingPattern(const PdfTilingPatternOptions& options) {
    if (!isValidResourceName(options.name)) {
        throw std::invalid_argument("Tiling pattern name must be a simple PDF resource name.");
    }
    if (options.content.empty()) throw std::invalid_argument("Tiling pattern content must not be empty.");
    if (options.bbox.empty()) throw std::invalid_argument("Tiling pattern bbox must be non-empty.");
    if ((options.xStep != 0.0 && (!std::isfinite(options.xStep) || options.xStep <= 0.0)) ||
        (options.yStep != 0.0 && (!std::isfinite(options.yStep) || options.yStep <= 0.0))) {
        throw std::invalid_argument("Tiling pattern steps must be finite positive values or zero for automatic sizing.");
    }
    for (const double value : options.matrix) {
        if (!std::isfinite(value)) throw std::invalid_argument("Tiling pattern matrix values must be finite.");
    }
    const auto tilingType = static_cast<int>(options.tilingType);
    if (tilingType < 1 || tilingType > 3) {
        throw std::invalid_argument("Tiling pattern type must be 1, 2, or 3.");
    }
    for (const auto& existing : state_->tilingPatterns) {
        if (existing.options.name == options.name) {
            throw std::invalid_argument("A tiling pattern with this name already exists: " + options.name);
        }
    }
    state_->tilingPatterns.push_back({options});
    return state_->tilingPatterns.size() - 1U;
}

namespace {
void ensureUniqueWriterColorSpace(const Internal::PdfWriterState& state,
                                  const std::string& name) {
    if (!isValidResourceName(name)) {
        throw std::invalid_argument("Color-space resource name must be a simple PDF name without a leading slash.");
    }
    const auto duplicate = std::find_if(state.colorSpaces.begin(), state.colorSpaces.end(),
        [&](const auto& item) { return item.resourceName == name; });
    if (duplicate != state.colorSpaces.end()) {
        throw std::invalid_argument("A color space with this resource name already exists: " + name);
    }
}
}

std::size_t PdfWriter::AddIccColorSpace(const PdfIccColorSpaceOptions& options) {
    ensureUniqueWriterColorSpace(*state_, options.name);
    if (options.profileBytes.empty()) throw std::invalid_argument("ICC profile bytes must not be empty.");
    if (options.components == 0U || options.components > 4U) {
        throw std::invalid_argument("ICC color spaces must have between one and four components.");
    }
    if (options.components != deviceColorSpaceComponents(options.alternate)) {
        throw std::invalid_argument("ICC component count must match the selected alternate device color space.");
    }
    Internal::PdfWriterColorSpace colorSpace;
    colorSpace.kind = Internal::PdfWriterColorSpaceKind::IccBased;
    colorSpace.resourceName = options.name;
    colorSpace.alternate = options.alternate;
    colorSpace.components = options.components;
    colorSpace.profileBytes = options.profileBytes;
    state_->colorSpaces.push_back(std::move(colorSpace));
    return state_->colorSpaces.size() - 1U;
}

std::size_t PdfWriter::AddSeparationColorSpace(const PdfSeparationColorSpaceOptions& options) {
    ensureUniqueWriterColorSpace(*state_, options.name);
    if (!isValidResourceName(options.colorantName)) {
        throw std::invalid_argument("Separation colorant name must be a simple PDF name.");
    }
    const auto components = deviceColorSpaceComponents(options.alternate);
    if (options.c0.size() != components || options.c1.size() != components) {
        throw std::invalid_argument("Separation C0/C1 component counts must match the alternate color space.");
    }
    validateUnitComponents(options.c0, "Separation C0");
    validateUnitComponents(options.c1, "Separation C1");
    if (!std::isfinite(options.exponent) || options.exponent <= 0.0) {
        throw std::invalid_argument("Separation exponent must be finite and positive.");
    }
    Internal::PdfWriterColorSpace colorSpace;
    colorSpace.kind = Internal::PdfWriterColorSpaceKind::Separation;
    colorSpace.resourceName = options.name;
    colorSpace.alternate = options.alternate;
    colorSpace.components = 1U;
    colorSpace.colorantNames = {options.colorantName};
    colorSpace.c0 = options.c0;
    colorSpace.c1 = options.c1;
    colorSpace.exponent = options.exponent;
    state_->colorSpaces.push_back(std::move(colorSpace));
    return state_->colorSpaces.size() - 1U;
}

std::size_t PdfWriter::AddDeviceNColorSpace(const PdfDeviceNColorSpaceOptions& options) {
    ensureUniqueWriterColorSpace(*state_, options.name);
    if (options.colorantNames.empty()) throw std::invalid_argument("DeviceN requires at least one colorant.");
    if (options.colorantNames.size() > 32U) throw std::invalid_argument("DeviceN colorant count exceeds the supported limit.");
    for (const auto& colorant : options.colorantNames) {
        if (!isValidResourceName(colorant)) throw std::invalid_argument("DeviceN colorant names must be simple PDF names.");
    }
    if (options.tintTransformProgram.empty()) {
        throw std::invalid_argument("DeviceN requires a calculator tint-transform program.");
    }
    Internal::PdfWriterColorSpace colorSpace;
    colorSpace.kind = Internal::PdfWriterColorSpaceKind::DeviceN;
    colorSpace.resourceName = options.name;
    colorSpace.alternate = options.alternate;
    colorSpace.components = static_cast<std::uint8_t>(options.colorantNames.size());
    colorSpace.colorantNames = options.colorantNames;
    colorSpace.tintTransformProgram = options.tintTransformProgram;
    state_->colorSpaces.push_back(std::move(colorSpace));
    return state_->colorSpaces.size() - 1U;
}


namespace {
void validateMeshVertex(const PdfMeshVertex& vertex, const std::size_t componentCount) {
    if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y)) {
        throw std::invalid_argument("Mesh vertex coordinates must be finite.");
    }
    if (vertex.colorComponents.size() != componentCount) {
        throw std::invalid_argument("Mesh vertex color component count does not match the color space.");
    }
    validateUnitComponents(vertex.colorComponents, "Mesh vertex color");
}

void validatePatchPoint(const PdfPoint point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        throw std::invalid_argument("Patch mesh control-point coordinates must be finite.");
    }
}

void validatePatchColors(const PdfPatchCornerColors& colors, const std::size_t componentCount) {
    for (const auto& color : colors) {
        if (color.size() != componentCount) {
            throw std::invalid_argument("Patch mesh corner-color component count does not match the color space.");
        }
        validateUnitComponents(color, "Patch mesh corner color");
    }
}

void ensureUniqueMeshShading(const Internal::PdfWriterState& state, const std::string& name) {
    if (!isValidResourceName(name)) {
        throw std::invalid_argument("Mesh shading resource name must be a simple PDF name.");
    }
    if (std::any_of(state.meshShadings.begin(), state.meshShadings.end(), [&](const auto& shading) {
            return shading.resourceName == name;
        })) {
        throw std::invalid_argument("A mesh shading with this resource name already exists: " + name);
    }
}
}

std::size_t PdfWriter::AddFreeFormMeshShading(const PdfFreeFormMeshShadingOptions& options) {
    ensureUniqueMeshShading(*state_, options.name);
    if (options.triangles.empty()) throw std::invalid_argument("Free-form mesh requires at least one triangle.");
    const auto componentCount = deviceColorSpaceComponents(options.colorSpace);
    Internal::PdfWriterMeshShading shading;
    shading.kind = Internal::PdfWriterMeshShadingKind::FreeForm;
    shading.resourceName = options.name;
    shading.colorSpace = options.colorSpace;
    shading.antiAlias = options.antiAlias;
    shading.vertices.reserve(options.triangles.size() * 3U);
    for (const auto& triangle : options.triangles) {
        for (const auto& vertex : triangle) {
            validateMeshVertex(vertex, componentCount);
            shading.vertices.push_back(vertex);
        }
    }
    state_->meshShadings.push_back(std::move(shading));
    return state_->meshShadings.size() - 1U;
}

std::size_t PdfWriter::AddLatticeMeshShading(const PdfLatticeMeshShadingOptions& options) {
    ensureUniqueMeshShading(*state_, options.name);
    if (options.verticesPerRow < 2U) throw std::invalid_argument("Lattice mesh requires at least two vertices per row.");
    if (options.vertices.size() < options.verticesPerRow * 2U ||
        options.vertices.size() % options.verticesPerRow != 0U) {
        throw std::invalid_argument("Lattice mesh vertices must contain at least two complete rows.");
    }
    const auto componentCount = deviceColorSpaceComponents(options.colorSpace);
    for (const auto& vertex : options.vertices) validateMeshVertex(vertex, componentCount);
    Internal::PdfWriterMeshShading shading;
    shading.kind = Internal::PdfWriterMeshShadingKind::Lattice;
    shading.resourceName = options.name;
    shading.colorSpace = options.colorSpace;
    shading.vertices = options.vertices;
    shading.verticesPerRow = options.verticesPerRow;
    shading.antiAlias = options.antiAlias;
    state_->meshShadings.push_back(std::move(shading));
    return state_->meshShadings.size() - 1U;
}

std::size_t PdfWriter::AddCoonsPatchMeshShading(
    const PdfCoonsPatchMeshShadingOptions& options) {
    ensureUniqueMeshShading(*state_, options.name);
    if (options.patches.empty()) throw std::invalid_argument("Coons patch mesh requires at least one patch.");
    const auto componentCount = deviceColorSpaceComponents(options.colorSpace);
    Internal::PdfWriterMeshShading shading;
    shading.kind = Internal::PdfWriterMeshShadingKind::CoonsPatch;
    shading.resourceName = options.name;
    shading.colorSpace = options.colorSpace;
    shading.antiAlias = options.antiAlias;
    shading.patches.reserve(options.patches.size());
    for (const auto& source : options.patches) {
        Internal::PdfWriterPatchMesh patch;
        patch.controlPoints.assign(source.controlPoints.begin(), source.controlPoints.end());
        for (const auto point : patch.controlPoints) validatePatchPoint(point);
        validatePatchColors(source.cornerColors, componentCount);
        patch.cornerColors = source.cornerColors;
        shading.patches.push_back(std::move(patch));
    }
    state_->meshShadings.push_back(std::move(shading));
    return state_->meshShadings.size() - 1U;
}

std::size_t PdfWriter::AddTensorProductPatchMeshShading(
    const PdfTensorProductPatchMeshShadingOptions& options) {
    ensureUniqueMeshShading(*state_, options.name);
    if (options.patches.empty()) {
        throw std::invalid_argument("Tensor-product patch mesh requires at least one patch.");
    }
    const auto componentCount = deviceColorSpaceComponents(options.colorSpace);
    Internal::PdfWriterMeshShading shading;
    shading.kind = Internal::PdfWriterMeshShadingKind::TensorProductPatch;
    shading.resourceName = options.name;
    shading.colorSpace = options.colorSpace;
    shading.antiAlias = options.antiAlias;
    shading.patches.reserve(options.patches.size());
    for (const auto& source : options.patches) {
        Internal::PdfWriterPatchMesh patch;
        patch.controlPoints.assign(source.controlPoints.begin(), source.controlPoints.end());
        for (const auto point : patch.controlPoints) validatePatchPoint(point);
        validatePatchColors(source.cornerColors, componentCount);
        patch.cornerColors = source.cornerColors;
        shading.patches.push_back(std::move(patch));
    }
    state_->meshShadings.push_back(std::move(shading));
    return state_->meshShadings.size() - 1U;
}

void PdfWriter::ClearColorSpaces() {
    const bool inUse = std::any_of(state_->pages.begin(), state_->pages.end(),
        [](const auto& page) { return !page.colorSpaceIndices.empty(); });
    if (inUse) {
        throw std::logic_error("Registered color spaces cannot be cleared after page content references them.");
    }
    state_->colorSpaces.clear();
}
std::size_t PdfWriter::GetColorSpaceCount() const noexcept { return state_->colorSpaces.size(); }
std::size_t PdfWriter::GetMeshShadingCount() const noexcept { return state_->meshShadings.size(); }

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
void PdfWriter::SetPageSize(const std::size_t pageIndex, const PdfRectangle& mediaBox) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    if (mediaBox.empty() || !std::isfinite(mediaBox.left) || !std::isfinite(mediaBox.top) ||
        !std::isfinite(mediaBox.right) || !std::isfinite(mediaBox.bottom)) {
        throw std::invalid_argument("Media box must be finite and non-empty.");
    }
    state_->pages[pageIndex].mediaBox = mediaBox;
}
void PdfWriter::SetPageCropBox(const std::size_t pageIndex, const PdfRectangle& cropBox) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    if (cropBox.empty()) {
        state_->pages[pageIndex].cropBox.reset();
        return;
    }
    if (!std::isfinite(cropBox.left) || !std::isfinite(cropBox.top) ||
        !std::isfinite(cropBox.right) || !std::isfinite(cropBox.bottom)) {
        throw std::invalid_argument("Crop box must be finite.");
    }
    state_->pages[pageIndex].cropBox = cropBox;
}
PdfRectangle PdfWriter::GetPageCropBox(const std::size_t pageIndex) const {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    return state_->pages[pageIndex].cropBox.value_or(PdfRectangle{});
}
void PdfWriter::SetPageRotation(const std::size_t pageIndex, const int rotation) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    if (rotation % 90 != 0) throw std::invalid_argument("Rotation must be a multiple of 90 degrees.");
    state_->pages[pageIndex].rotation = (rotation % 360 + 360) % 360;
}
int PdfWriter::GetPageRotation(const std::size_t pageIndex) const {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    return state_->pages[pageIndex].rotation;
}
PdfCanvas PdfWriter::GetCanvas(std::size_t i){if(i>=state_->pages.size())throw std::out_of_range("Page index");return PdfCanvas(state_,i);}

void PdfWriter::SetDocumentInfo(const PdfDocumentInfo& info) { state_->documentInfo = info; }
const PdfDocumentInfo& PdfWriter::GetDocumentInfo() const noexcept { return state_->documentInfo; }
void PdfWriter::SetTitle(std::string value) { state_->documentInfo.title = std::move(value); }
void PdfWriter::SetXmpMetadata(const bool enabled) { state_->writeXmpMetadata = enabled; }
bool PdfWriter::GetXmpMetadataEnabled() const noexcept { return state_->writeXmpMetadata; }

void PdfWriter::ConfigureForPdfA(const PdfConformanceProfile profile,
                                 const std::span<const std::byte> iccProfile,
                                 std::string outputConditionIdentifier) {
    const auto requirements = pdfAWriterRequirements(profile);
    if (!requirements) {
        throw std::invalid_argument("ConfigureForPdfA requires a PDF/A conformance profile.");
    }
    if (iccProfile.empty()) {
        throw std::invalid_argument("PDF/A creation requires a non-empty ICC output profile.");
    }
    if (state_->encryption) {
        throw std::invalid_argument("PDF/A forbids encryption; clear encryption before configuration.");
    }
    if (!compatibleConformanceProfiles(profile, state_->pdfUaProfile)) {
        throw std::invalid_argument("Configured PDF/A and PDF/UA profiles use incompatible PDF versions.");
    }
    state_->pdfAProfile = profile;
    state_->pdfAOutputIntentIcc.assign(iccProfile.begin(), iccProfile.end());
    state_->pdfAOutputConditionIdentifier = outputConditionIdentifier.empty()
        ? "Custom" : std::move(outputConditionIdentifier);
    state_->writeXmpMetadata = true;
    if (requirements->tagged) state_->tagged = true;
}

void PdfWriter::ClearPdfAConfiguration() noexcept {
    state_->pdfAProfile.reset();
    state_->pdfAOutputIntentIcc.clear();
    state_->pdfAOutputConditionIdentifier = "sRGB IEC61966-2.1";
}

std::optional<PdfConformanceProfile> PdfWriter::GetPdfAProfile() const noexcept {
    return state_->pdfAProfile;
}

void PdfWriter::ConfigureForPdfUa(const PdfConformanceProfile profile,
                                  std::string language,
                                  std::string title) {
    const auto requirements = pdfUaWriterRequirements(profile);
    if (!requirements) throw std::invalid_argument("ConfigureForPdfUa requires PDF/UA-1 or PDF/UA-2.");
    if (language.empty()) throw std::invalid_argument("PDF/UA requires a document language.");
    if (title.empty()) throw std::invalid_argument("PDF/UA requires a document title.");
    if (!compatibleConformanceProfiles(state_->pdfAProfile, profile)) {
        throw std::invalid_argument("Configured PDF/A and PDF/UA profiles use incompatible PDF versions.");
    }
    state_->pdfUaProfile = profile;
    state_->tagged = true;
    state_->language = std::move(language);
    state_->documentInfo.title = std::move(title);
    state_->writeXmpMetadata = true;
    state_->viewerPreferences.displayDocumentTitle = true;
}

void PdfWriter::ClearPdfUaConfiguration() noexcept { state_->pdfUaProfile.reset(); }
std::optional<PdfConformanceProfile> PdfWriter::GetPdfUaProfile() const noexcept {
    return state_->pdfUaProfile;
}
void PdfWriter::SetConformanceEnforcement(const bool enabled) noexcept {
    state_->enforceConformance = enabled;
}
bool PdfWriter::GetConformanceEnforcement() const noexcept { return state_->enforceConformance; }
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
void PdfWriter::SetTaggedRoleMap(std::string customRole, std::string standardRole) {
    if (customRole.empty() || standardRole.empty()) {
        throw std::invalid_argument("Tagged role map entries cannot be empty.");
    }
    if (customRole.front() == '/') customRole.erase(0U, 1U);
    if (standardRole.front() == '/') standardRole.erase(0U, 1U);
    const auto found = std::find_if(state_->taggedRoleMap.begin(), state_->taggedRoleMap.end(),
        [&](const auto& entry) { return entry.first == customRole; });
    if (found == state_->taggedRoleMap.end()) {
        state_->taggedRoleMap.emplace_back(std::move(customRole), std::move(standardRole));
    } else {
        found->second = std::move(standardRole);
    }
}
void PdfWriter::ClearTaggedRoleMap() noexcept { state_->taggedRoleMap.clear(); }
void PdfWriter::SetTaggedDocumentAlternativeText(std::string altText) {
    state_->taggedAltText = std::move(altText);
}
const std::string& PdfWriter::GetTaggedDocumentAlternativeText() const noexcept {
    return state_->taggedAltText;
}
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
    Internal::PdfWriterLink link;
    link.kind = Internal::PdfWriterLinkKind::NamedDestination;
    link.target = std::move(destinationName);
    link.options = options;
    state_->pages[pageIndex].links.push_back(std::move(link));
}

void PdfWriter::AddUriLink(std::size_t pageIndex, std::string uri, const PdfLinkOptions& options) {
    if (pageIndex >= state_->pages.size()) throw std::out_of_range("Page index");
    if (uri.empty()) throw std::invalid_argument("URI cannot be empty");
    if (options.borderWidth < 0.0) throw std::invalid_argument("Link border width cannot be negative");
    Internal::PdfWriterLink link;
    link.kind = Internal::PdfWriterLinkKind::Uri;
    link.target = std::move(uri);
    link.options = options;
    state_->pages[pageIndex].links.push_back(std::move(link));
}

void PdfWriter::AddRemoteLink(std::size_t pageIndex, std::string fileName,
                              std::string destination, const PdfLinkOptions& options) {
    if (pageIndex >= state_->pages.size() || fileName.empty() || destination.empty()) {
        throw std::invalid_argument("Remote link requires a page, file name, and destination.");
    }
    Internal::PdfWriterLink link;
    link.kind = Internal::PdfWriterLinkKind::Remote;
    link.target = std::move(fileName);
    link.destination = std::move(destination);
    link.options = options;
    state_->pages[pageIndex].links.push_back(std::move(link));
}

void PdfWriter::AddLaunchLink(std::size_t pageIndex, std::string fileName,
                              const PdfLinkOptions& options) {
    if (pageIndex >= state_->pages.size() || fileName.empty()) {
        throw std::invalid_argument("Launch link requires a page and file name.");
    }
    Internal::PdfWriterLink link;
    link.kind = Internal::PdfWriterLinkKind::Launch;
    link.target = std::move(fileName);
    link.options = options;
    state_->pages[pageIndex].links.push_back(std::move(link));
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
    const std::size_t maximumPasswordBytes =
        options.algorithm == PdfEncryptionAlgorithm::Aes256 ? 127U : 32U;
    if (options.userPassword.size() > maximumPasswordBytes ||
        options.ownerPassword.size() > maximumPasswordBytes) {
        throw std::invalid_argument(
            options.algorithm == PdfEncryptionAlgorithm::Aes256
                ? "AES-256 revision 6 passwords are limited to 127 UTF-8 bytes."
                : "Legacy PDF Standard Security passwords are limited to 32 bytes.");
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
    if (options.mode == PdfSaveMode::Incremental) {
        throw std::invalid_argument(
            "Incremental Save(std::ostream&) has no source revision. Use PdfWriter::Resave "
            "with PdfSaveMode::Incremental or the incremental editor APIs.");
    }
    const auto pdfA = state_->pdfAProfile
        ? pdfAWriterRequirements(*state_->pdfAProfile) : std::nullopt;
    const auto pdfUa = state_->pdfUaProfile
        ? pdfUaWriterRequirements(*state_->pdfUaProfile) : std::nullopt;
    if ((pdfA || pdfUa) && state_->enforceConformance) {
        if (state_->pages.empty()) throw std::runtime_error("Conforming PDF output requires at least one page.");
        if (!compatibleConformanceProfiles(state_->pdfAProfile, state_->pdfUaProfile)) {
            throw std::runtime_error("Configured PDF/A and PDF/UA profiles are incompatible.");
        }
    }
    if (pdfA && state_->enforceConformance) {
        if (state_->encryption) throw std::runtime_error("PDF/A output cannot be encrypted.");
        if (state_->pdfAOutputIntentIcc.empty()) {
            throw std::runtime_error("PDF/A output requires an embedded ICC output intent.");
        }
        if (pdfA->tagged && state_->language.empty()) {
            throw std::runtime_error("PDF/A level A requires a document language.");
        }
        if (!pdfA->permitsArbitraryEmbeddedFiles && !state_->embeddedFiles.empty()) {
            throw std::runtime_error("This PDF/A profile does not permit arbitrary embedded files; use PDF/A-3, PDF/A-4e, or PDF/A-4f.");
        }
        if (pdfA->part == 4 && pdfA->conformance == 'F' && state_->embeddedFiles.empty()) {
            throw std::runtime_error("PDF/A-4F requires at least one embedded file in the EmbeddedFiles name tree.");
        }
        if (pdfA->permitsArbitraryEmbeddedFiles) {
            for (const auto& file : state_->embeddedFiles) {
                if (file.options.relationship == PdfAssociatedFileRelationship::Unspecified) {
                    throw std::runtime_error("PDF/A associated files require a meaningful AFRelationship.");
                }
            }
        }
        if (state_->portfolio) throw std::runtime_error("PDF/A does not permit portfolio collection mode.");
        for (const auto& page : state_->pages) {
            if (contentUsesBase14Font(page.content)) {
                throw std::runtime_error("PDF/A requires embedded fonts; page content uses the Base-14 /F1 font.");
            }
            for (const auto& link : page.links) {
                if (link.kind == Internal::PdfWriterLinkKind::Launch) {
                    throw std::runtime_error("PDF/A forbids Launch actions.");
                }
            }
            if (pdfA->part == 1 && !page.fileAttachments.empty()) {
                throw std::runtime_error("PDF/A-1 forbids FileAttachment annotations.");
            }
        }
        if (pdfA->forbidsTransparency) {
            for (const auto& graphicsState : state_->extGStates) {
                if (graphicsState.strokeOpacity != 1.0 || graphicsState.fillOpacity != 1.0 ||
                    graphicsState.blendMode != PdfBlendMode::SourceOver) {
                    throw std::runtime_error("PDF/A-1 forbids transparency and non-Normal blend modes.");
                }
            }
            for (const auto& image : state_->images) {
                if (image.image.HasSoftMask()) throw std::runtime_error("PDF/A-1 forbids image soft masks.");
            }
        }
    }
    if (pdfUa && state_->enforceConformance) {
        if (!state_->tagged) throw std::runtime_error("PDF/UA output must be tagged.");
        if (state_->language.empty()) throw std::runtime_error("PDF/UA requires a document language.");
        if (state_->documentInfo.title.empty()) throw std::runtime_error("PDF/UA requires a document title.");
        if (!state_->writeXmpMetadata) throw std::runtime_error("PDF/UA requires XMP metadata.");
        if (!state_->viewerPreferences.displayDocumentTitle) {
            throw std::runtime_error("PDF/UA requires /DisplayDocTitle true.");
        }
        for (const auto& page : state_->pages) {
            if (contentUsesBase14Font(page.content)) {
                throw std::runtime_error("PDF/UA requires Unicode-mapped embedded fonts; Base-14 /F1 is not permitted.");
            }
        }
        validateWriterTaggedContent(*state_);
    }
    for (const auto& page : state_->pages) {
        if (page.openMarkedContentDepth != 0U) {
            throw std::runtime_error("Tagged marked-content sequences must be closed before saving.");
        }
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
    const bool usesBase14Font = std::any_of(
        state_->pages.begin(), state_->pages.end(),
        [](const auto& page) { return contentUsesBase14Font(page.content); });
    const int catalog = allocate();
    const int pages = allocate();
    const int base14Font = usesBase14Font ? allocate() : 0;
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
    std::vector<int> imageSoftMaskIds(state_->images.size(), 0);
    for (std::size_t index = 0; index < state_->images.size(); ++index) {
        if (state_->images[index].image.HasSoftMask()) imageSoftMaskIds[index] = allocate();
    }
    std::vector<int> extGStateIds(state_->extGStates.size()); for(auto& id:extGStateIds) id=allocate();
    std::vector<int> ocgIds(state_->ocgs.size()); for(auto& id:ocgIds) id=allocate();
    const int ocPropertiesObject = state_->ocgs.empty() ? 0 : allocate();
    const int structTreeObject = state_->tagged ? allocate() : 0;
    const int parentTreeObject = state_->tagged ? allocate() : 0;
    const int documentStructElementObject = state_->tagged ? allocate() : 0;
    const int standardStructureNamespaceObject =
        state_->tagged && pdfUa && pdfUa->part == 2 ? allocate() : 0;
    const int metadataObject = state_->writeXmpMetadata ? allocate() : 0;
    const int outputProfileObject = pdfA ? allocate() : 0;
    const int outputIntentObject = pdfA ? allocate() : 0;
    struct EmbeddedIds { int file{}, descriptor{}, cid{}, toUnicode{}, type0{}; };
    std::vector<EmbeddedIds> fontIds(state_->embeddedFonts.size());
    for(auto& ids:fontIds){ ids.file=allocate(); ids.descriptor=allocate(); ids.cid=allocate(); ids.toUnicode=allocate(); ids.type0=allocate(); }
    std::vector<int> type1Ids(state_->type1Fonts.size());
    std::vector<int> type1FileIds(state_->type1Fonts.size());
    std::vector<int> type1DescIds(state_->type1Fonts.size());
    for (auto& id : type1Ids) id = allocate();
    for (auto& id : type1FileIds) id = allocate();
    for (auto& id : type1DescIds) id = allocate();
    std::vector<int> cffIds(state_->cffFonts.size());
    std::vector<int> cffFileIds(state_->cffFonts.size());
    std::vector<int> cffDescIds(state_->cffFonts.size());
    for (auto& id : cffIds) id = allocate();
    for (auto& id : cffFileIds) id = allocate();
    for (auto& id : cffDescIds) id = allocate();
    struct Type3Ids { int font{}; int toUnicode{}; std::vector<int> charProcs; };
    std::vector<Type3Ids> type3Ids(state_->type3Fonts.size());
    for (std::size_t index = 0; index < state_->type3Fonts.size(); ++index) {
        type3Ids[index].font = allocate();
        const auto& glyphs = state_->type3Fonts[index].font.GetGlyphs();
        type3Ids[index].charProcs.resize(glyphs.size());
        for (auto& id : type3Ids[index].charProcs) id = allocate();
        if (std::any_of(glyphs.begin(), glyphs.end(), [](const auto& glyph) {
                return glyph.unicodeCodePoint.has_value();
            })) {
            type3Ids[index].toUnicode = allocate();
        }
    }
    std::vector<int> patternIds(state_->tilingPatterns.size());
    for (auto& id : patternIds) id = allocate();
    std::vector<int> shadingIds(state_->meshShadings.size());
    for (auto& id : shadingIds) id = allocate();
    std::vector<int> colorSpaceAuxIds(state_->colorSpaces.size());
    for (auto& id : colorSpaceAuxIds) id = allocate();
    std::vector<int> pageIds,contentIds; for(std::size_t i=0;i<state_->pages.size();++i){pageIds.push_back(allocate());contentIds.push_back(allocate());}
    std::vector<std::vector<int>> structElementIds(state_->pages.size());
    if (state_->tagged) {
        for (std::size_t pageIndex = 0; pageIndex < state_->pages.size(); ++pageIndex) {
            for (std::size_t item = 0; item < state_->pages[pageIndex].markedContents.size(); ++item) {
                structElementIds[pageIndex].push_back(allocate());
            }
        }
    }
    std::vector<std::vector<int>> linkIds(state_->pages.size());
    std::vector<std::vector<int>> attachmentIds(state_->pages.size());
    std::vector<std::vector<int>> linkStructElementIds(state_->pages.size());
    std::vector<std::vector<int>> attachmentStructElementIds(state_->pages.size());
    std::vector<std::vector<std::size_t>> linkStructParentKeys(state_->pages.size());
    std::vector<std::vector<std::size_t>> attachmentStructParentKeys(state_->pages.size());
    std::size_t nextStructParentKey = state_->pages.size();
    for (std::size_t i = 0; i < state_->pages.size(); ++i) {
        for (std::size_t j = 0; j < state_->pages[i].links.size(); ++j) {
            linkIds[i].push_back(allocate());
            if (state_->tagged) {
                linkStructElementIds[i].push_back(allocate());
                linkStructParentKeys[i].push_back(nextStructParentKey++);
            }
        }
        for (std::size_t j = 0; j < state_->pages[i].fileAttachments.size(); ++j) {
            attachmentIds[i].push_back(allocate());
            if (state_->tagged) {
                attachmentStructElementIds[i].push_back(allocate());
                attachmentStructParentKeys[i].push_back(nextStructParentKey++);
            }
        }
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
    if (metadataObject != 0) objects[catalog] += " /Metadata " + std::to_string(metadataObject) + " 0 R";
    if (outputIntentObject != 0) objects[catalog] += " /OutputIntents [" + std::to_string(outputIntentObject) + " 0 R]";
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
    if (metadataObject != 0) {
        std::ostringstream xmp;
        const auto& info = state_->documentInfo;
        xmp << "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
            << "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Pdf++ Core\">\n"
            << "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
            << "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
            << "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" "
            << "xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\" "
            << "xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" "
            << "xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\" "
            << "xmlns:pdfaExtension=\"http://www.aiim.org/pdfa/ns/extension/\" "
            << "xmlns:pdfaSchema=\"http://www.aiim.org/pdfa/ns/schema#\" "
            << "xmlns:pdfaProperty=\"http://www.aiim.org/pdfa/ns/property#\">\n";
        if (!info.title.empty()) xmp << "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" << xmlEscape(info.title) << "</rdf:li></rdf:Alt></dc:title>\n";
        if (!state_->language.empty()) {
            xmp << "<dc:language><rdf:Bag><rdf:li>" << xmlEscape(state_->language)
                << "</rdf:li></rdf:Bag></dc:language>\n";
        }
        if (!info.author.empty()) xmp << "<dc:creator><rdf:Seq><rdf:li>" << xmlEscape(info.author) << "</rdf:li></rdf:Seq></dc:creator>\n";
        if (!info.subject.empty()) xmp << "<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">" << xmlEscape(info.subject) << "</rdf:li></rdf:Alt></dc:description>\n";
        if (!info.keywords.empty()) xmp << "<dc:subject><rdf:Bag><rdf:li>" << xmlEscape(info.keywords) << "</rdf:li></rdf:Bag></dc:subject>\n";
        if (!info.creator.empty()) xmp << "<xmp:CreatorTool>" << xmlEscape(info.creator) << "</xmp:CreatorTool>\n";
        if (!info.producer.empty()) xmp << "<pdf:Producer>" << xmlEscape(info.producer) << "</pdf:Producer>\n";
        if (!info.creationDate.empty()) xmp << "<xmp:CreateDate>" << xmlEscape(info.creationDate) << "</xmp:CreateDate>\n";
        if (!info.modificationDate.empty()) xmp << "<xmp:ModifyDate>" << xmlEscape(info.modificationDate) << "</xmp:ModifyDate>\n";
        if (pdfA) {
            xmp << "<pdfaid:part>" << pdfA->part << "</pdfaid:part>\n";
            if (pdfA->conformance != '\0') xmp << "<pdfaid:conformance>" << pdfA->conformance << "</pdfaid:conformance>\n";
        }
        if (pdfUa) {
            xmp << "<pdfuaid:part>" << pdfUa->part << "</pdfuaid:part>\n";
            if (pdfUa->revision != 0) xmp << "<pdfuaid:rev>" << pdfUa->revision << "</pdfuaid:rev>\n";
        }
        if (pdfA && pdfUa) {
            xmp << "<pdfaExtension:schemas><rdf:Bag>\n"
                << "<rdf:li rdf:parseType=\"Resource\">\n"
                << "<pdfaSchema:schema>PDF/UA identification schema</pdfaSchema:schema>\n"
                << "<pdfaSchema:namespaceURI>http://www.aiim.org/pdfua/ns/id/</pdfaSchema:namespaceURI>\n"
                << "<pdfaSchema:prefix>pdfuaid</pdfaSchema:prefix>\n"
                << "<pdfaSchema:property><rdf:Seq>\n"
                << "<rdf:li rdf:parseType=\"Resource\"><pdfaProperty:name>part</pdfaProperty:name>"
                << "<pdfaProperty:valueType>Integer</pdfaProperty:valueType>"
                << "<pdfaProperty:category>internal</pdfaProperty:category>"
                << "<pdfaProperty:description>PDF/UA part identifier</pdfaProperty:description></rdf:li>\n";
            if (pdfUa->revision != 0) {
                xmp << "<rdf:li rdf:parseType=\"Resource\"><pdfaProperty:name>rev</pdfaProperty:name>"
                    << "<pdfaProperty:valueType>Integer</pdfaProperty:valueType>"
                    << "<pdfaProperty:category>internal</pdfaProperty:category>"
                    << "<pdfaProperty:description>PDF/UA revision year</pdfaProperty:description></rdf:li>\n";
            }
            xmp << "</rdf:Seq></pdfaSchema:property>\n"
                << "</rdf:li></rdf:Bag></pdfaExtension:schemas>\n";
        }
        xmp << "</rdf:Description>\n</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";
        const std::string packet = xmp.str();
        objects[metadataObject] = "<< /Type /Metadata /Subtype /XML /Length " +
            std::to_string(packet.size()) + " >>\nstream\n" + packet + "\nendstream";
    }
    if (pdfA) {
        const auto& profile = state_->pdfAOutputIntentIcc;
        std::string profileBytes(reinterpret_cast<const char*>(profile.data()), profile.size());
        objects[outputProfileObject] = "<< /N 3 /Length " + std::to_string(profileBytes.size()) +
            " >>\nstream\n" + profileBytes + "\nendstream";
        const std::string identifier = escapePdfString(state_->pdfAOutputConditionIdentifier);
        objects[outputIntentObject] = "<< /Type /OutputIntent /S /GTS_PDFA1 "
            "/OutputConditionIdentifier (" + identifier + ") /Info (" + identifier + ") "
            "/DestOutputProfile " + std::to_string(outputProfileObject) + " 0 R >>";
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
            const auto digest = Internal::Md5(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(file.bytes.data()), file.bytes.size()));
            const std::string digestBytes(reinterpret_cast<const char*>(digest.data()), digest.size());
            std::ostringstream stream;
            stream << "<< /Type /EmbeddedFile /Subtype /" << encodePdfName(file.options.mimeType)
                   << " /Params << /Size " << file.bytes.size()
                   << " /CheckSum <" << hexLookup(digestBytes) << '>';
            if (!file.options.creationDate.empty()) stream << " /CreationDate (" << escapePdfString(file.options.creationDate) << ')';
            const std::string modificationDate = file.options.modificationDate.empty()
                ? currentPdfDateUtc() : file.options.modificationDate;
            if (!modificationDate.empty()) stream << " /ModDate (" << escapePdfString(modificationDate) << ')';
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
    if (base14Font != 0) {
        objects[base14Font] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    }
    for (std::size_t i = 0; i < state_->extGStates.size(); ++i) {
        const auto& gs = state_->extGStates[i];
        std::ostringstream dictionary;
        dictionary << "<< /Type /ExtGState /CA " << gs.strokeOpacity
                   << " /ca " << gs.fillOpacity << " /BM "
                   << blendModeName(gs.blendMode) << " >>";
        objects[extGStateIds[i]] = dictionary.str();
    }
    for (std::size_t i = 0; i < state_->images.size(); ++i) {
        const auto& image = state_->images[i].image;
        std::string softMaskEntry;
        if (imageSoftMaskIds[i] != 0) {
            const auto alpha = compressBytes(image.GetSoftMaskBytes());
            std::ostringstream mask;
            mask << "<< /Type /XObject /Subtype /Image /Width " << image.GetWidth()
                 << " /Height " << image.GetHeight()
                 << " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode";
            if (!image.GetMatte().empty()) {
                mask << " /Matte [";
                for (const double component : image.GetMatte()) mask << component << ' ';
                mask << ']';
            }
            mask << " /Length " << alpha.size() << " >>\nstream\n" << alpha << "\nendstream";
            objects[imageSoftMaskIds[i]] = mask.str();
            softMaskEntry = " /SMask " + std::to_string(imageSoftMaskIds[i]) + " 0 R";
        }

        std::string bytes;
        const char* filterName = nullptr;
        const auto encoding = image.GetEncoding();
        if (encoding == PdfImageEncoding::Dct) {
            const auto span = image.GetBytes();
            bytes.assign(reinterpret_cast<const char*>(span.data()), span.size());
            filterName = "/DCTDecode";
        } else if (encoding == PdfImageEncoding::Jpx) {
            const auto span = image.GetBytes();
            bytes.assign(reinterpret_cast<const char*>(span.data()), span.size());
            filterName = "/JPXDecode";
        } else if (encoding == PdfImageEncoding::CcittFax) {
            const auto span = image.GetBytes();
            bytes.assign(reinterpret_cast<const char*>(span.data()), span.size());
            filterName = "/CCITTFaxDecode";
        } else if (encoding == PdfImageEncoding::Raw &&
                   image.GetColorSpace() == PdfImageColorSpace::DeviceRGB &&
                   image.GetBitsPerComponent() == 8U) {
            // Indexed palette optimization: convert RGB images with <=32 unique
            // colors to an /Indexed color space with a compressed index plane.
            const auto raw = image.GetBytes();
            const std::uint32_t width = image.GetWidth();
            const std::uint32_t height = image.GetHeight();
            if (width > 0U && height > 0U &&
                raw.size() >= static_cast<std::size_t>(width) * height * 3U) {
                std::map<std::array<std::uint8_t, 3>, std::uint8_t> palette;
                std::vector<std::uint8_t> indices;
                indices.reserve(static_cast<std::size_t>(width) * height);
                bool tooMany = false;
                for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
                    const std::array<std::uint8_t, 3> rgb{
                        std::to_integer<std::uint8_t>(raw[pixel * 3U]),
                        std::to_integer<std::uint8_t>(raw[pixel * 3U + 1U]),
                        std::to_integer<std::uint8_t>(raw[pixel * 3U + 2U])};
                    auto iterator = palette.find(rgb);
                    if (iterator == palette.end()) {
                        if (palette.size() >= 32U) { tooMany = true; break; }
                        palette[rgb] = static_cast<std::uint8_t>(palette.size());
                    }
                    indices.push_back(palette[rgb]);
                }
                if (!tooMany && !palette.empty()) {
                    std::string lookup;
                    for (const auto& [rgb, unused] : palette) {
                        (void)unused;
                        lookup.push_back(static_cast<char>(rgb[0]));
                        lookup.push_back(static_cast<char>(rgb[1]));
                        lookup.push_back(static_cast<char>(rgb[2]));
                    }
                    bytes = compressBytes(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(indices.data()), indices.size()));
                    std::ostringstream dictionary;
                    dictionary << "<< /Type /XObject /Subtype /Image /Width " << width
                               << " /Height " << height
                               << " /ColorSpace [/Indexed /DeviceRGB " << palette.size() - 1U
                               << " <" << hexLookup(lookup) << ">] /BitsPerComponent 8"
                               << " /Filter /FlateDecode" << softMaskEntry
                               << " /Length " << bytes.size() << " >>\nstream\n"
                               << bytes << "\nendstream";
                    objects[imageIds[i]] = dictionary.str();
                    continue;
                }
            }
            bytes = compressBytes(image.GetBytes());
            filterName = "/FlateDecode";
        } else {
            bytes = compressBytes(image.GetBytes());
            filterName = "/FlateDecode";
        }
        std::ostringstream dictionary;
        dictionary << "<< /Type /XObject /Subtype /Image /Width " << image.GetWidth()
                   << " /Height " << image.GetHeight()
                   << " /ColorSpace " << colorSpaceName(image.GetColorSpace())
                   << " /BitsPerComponent " << image.GetBitsPerComponent()
                   << " /Filter " << filterName << softMaskEntry
                   << " /Length " << bytes.size() << " >>\nstream\n";
        objects[imageIds[i]] = dictionary.str() + bytes + "\nendstream";
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
            std::string verticalMetrics;
            if (ef.vertical) {
                std::ostringstream w2;
                for (const auto glyph : glyphs) {
                    const auto width = scale1000(ef.font.GetAdvanceWidth(glyph));
                    w2 << glyph << " [" << -1000 << ' ' << width / 2 << " 880] ";
                }
                verticalMetrics = " /DW2 [880 -1000] /W2 [" + w2.str() + "]";
            }
            objects[ids.cid]="<< /Type /Font /Subtype /CIDFontType2 /BaseFont /"+base+" /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor "+std::to_string(ids.descriptor)+" 0 R /DW 1000 /W ["+widths.str()+"]"+verticalMetrics+" /CIDToGIDMap /Identity >>";
        }
        std::vector<std::pair<std::uint32_t,std::uint16_t>> mappings=ef.usedMappings; std::sort(mappings.begin(),mappings.end(),[](auto a,auto b){return a.second<b.second;}); mappings.erase(std::unique(mappings.begin(),mappings.end()),mappings.end());
        std::ostringstream cmap; cmap<<"/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n/CMapName /PdfPPToUnicode def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        for(std::size_t pos=0;pos<mappings.size();pos+=100){const auto count=std::min<std::size_t>(100,mappings.size()-pos);cmap<<count<<" beginbfchar\n";for(std::size_t j=0;j<count;++j){const auto [cp,gid]=mappings[pos+j];cmap<<'<';cmap<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<gid<<"> <"<<utf16Hex(cp)<<">\n"<<std::dec;}cmap<<"endbfchar\n";}
        cmap<<"endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n"; const auto cm=cmap.str(); objects[ids.toUnicode]="<< /Length "+std::to_string(cm.size())+" >>\nstream\n"+cm+"endstream";
        objects[ids.type0]="<< /Type /Font /Subtype /Type0 /BaseFont /"+base+" /Encoding "+std::string(ef.vertical ? "/Identity-V" : "/Identity-H")+" /DescendantFonts ["+std::to_string(ids.cid)+" 0 R] /ToUnicode "+std::to_string(ids.toUnicode)+" 0 R >>";
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

    for (std::size_t fontIndex = 0; fontIndex < state_->type3Fonts.size(); ++fontIndex) {
        const auto& writerFont = state_->type3Fonts[fontIndex];
        const auto& font = writerFont.font;
        const auto& glyphs = font.GetGlyphs();
        const auto& ids = type3Ids[fontIndex];
        for (std::size_t glyphIndex = 0; glyphIndex < glyphs.size(); ++glyphIndex) {
            const auto& glyph = glyphs[glyphIndex];
            std::ostringstream body;
            body << glyph.advanceWidth << " 0 "
                 << glyph.boundingBox.left << ' ' << glyph.boundingBox.bottom << ' '
                 << glyph.boundingBox.right << ' ' << glyph.boundingBox.top << " d1\n"
                 << glyph.content;
            if (glyph.content.empty() || glyph.content.back() != '\n') body << '\n';
            const auto stream = body.str();
            objects[ids.charProcs[glyphIndex]] = "<< /Length " + std::to_string(stream.size()) +
                " >>\nstream\n" + stream + "endstream";
        }

        const auto firstCode = static_cast<unsigned int>(glyphs.front().code);
        const auto lastCode = static_cast<unsigned int>(glyphs.back().code);
        std::ostringstream dictionary;
        dictionary << "<< /Type /Font /Subtype /Type3 /Name /" << font.GetFontName()
                   << " /FontBBox [" << font.GetFontBoundingBox().left << ' '
                   << font.GetFontBoundingBox().bottom << ' ' << font.GetFontBoundingBox().right
                   << ' ' << font.GetFontBoundingBox().top << "] /FontMatrix [";
        for (const auto value : font.GetFontMatrix()) dictionary << value << ' ';
        dictionary << "] /CharProcs <<";
        for (std::size_t glyphIndex = 0; glyphIndex < glyphs.size(); ++glyphIndex) {
            dictionary << " /" << glyphs[glyphIndex].name << ' '
                       << ids.charProcs[glyphIndex] << " 0 R";
        }
        dictionary << " >> /Encoding << /Type /Encoding /Differences [";
        for (const auto& glyph : glyphs) {
            dictionary << static_cast<unsigned int>(glyph.code) << " /" << glyph.name << ' ';
        }
        dictionary << "] >> /FirstChar " << firstCode << " /LastChar " << lastCode << " /Widths [";
        for (unsigned int code = firstCode; code <= lastCode; ++code) {
            const auto* glyph = font.FindGlyphByCode(static_cast<std::uint8_t>(code));
            dictionary << (glyph ? glyph->advanceWidth : 0.0) << ' ';
        }
        dictionary << "] /Resources << >>";
        if (ids.toUnicode != 0) dictionary << " /ToUnicode " << ids.toUnicode << " 0 R";
        dictionary << " >>";
        objects[ids.font] = dictionary.str();

        if (ids.toUnicode != 0) {
            std::vector<const PdfType3Glyph*> unicodeGlyphs;
            for (const auto& glyph : glyphs) {
                if (glyph.unicodeCodePoint) unicodeGlyphs.push_back(&glyph);
            }
            std::ostringstream cmap;
            cmap << "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
                 << "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                 << "/CMapName /PdfPPType3ToUnicode def\n/CMapType 2 def\n"
                 << "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n";
            for (std::size_t position = 0; position < unicodeGlyphs.size(); position += 100U) {
                const auto count = std::min<std::size_t>(100U, unicodeGlyphs.size() - position);
                cmap << count << " beginbfchar\n";
                for (std::size_t offset = 0; offset < count; ++offset) {
                    const auto* glyph = unicodeGlyphs[position + offset];
                    cmap << '<' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                         << static_cast<unsigned int>(glyph->code) << "> <"
                         << utf16Hex(*glyph->unicodeCodePoint) << ">\n" << std::dec;
                }
                cmap << "endbfchar\n";
            }
            cmap << "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
            const auto data = cmap.str();
            objects[ids.toUnicode] = "<< /Length " + std::to_string(data.size()) +
                " >>\nstream\n" + data + "endstream";
        }
    }

    if (structTreeObject != 0) {
        if (standardStructureNamespaceObject != 0) {
            objects[standardStructureNamespaceObject] =
                "<< /Type /Namespace /NS (http://iso.org/pdf2/ssn) >>";
        }
        std::ostringstream structure;
        structure << "<< /Type /StructTreeRoot /K [" << documentStructElementObject
                  << " 0 R] /ParentTree " << parentTreeObject << " 0 R"
                  << " /ParentTreeNextKey " << nextStructParentKey;
        if (standardStructureNamespaceObject != 0) {
            structure << " /Namespaces [" << standardStructureNamespaceObject << " 0 R]";
        }
        structure << " /RoleMap <<";
        for (const auto& [customRole, standardRole] : state_->taggedRoleMap) {
            structure << " /" << customRole << " /" << standardRole;
        }
        structure << " >> >>";
        objects[structTreeObject] = structure.str();

        std::ostringstream documentElement;
        documentElement << "<< /Type /StructElem /S /Document /P " << structTreeObject << " 0 R";
        if (standardStructureNamespaceObject != 0) {
            documentElement << " /NS " << standardStructureNamespaceObject << " 0 R";
        }
        documentElement << " /K [";
        for (std::size_t pageIndex = 0; pageIndex < state_->pages.size(); ++pageIndex) {
            const auto& marked = state_->pages[pageIndex].markedContents;
            for (std::size_t itemIndex = 0; itemIndex < marked.size(); ++itemIndex) {
                if (!marked[itemIndex].parentIndex) {
                    documentElement << structElementIds[pageIndex][itemIndex] << " 0 R ";
                }
            }
            for (const auto id : linkStructElementIds[pageIndex]) documentElement << id << " 0 R ";
            for (const auto id : attachmentStructElementIds[pageIndex]) documentElement << id << " 0 R ";
        }
        documentElement << ']';
        if (!state_->taggedAltText.empty()) {
            documentElement << " /Alt (" << escapePdfString(state_->taggedAltText) << ')';
        }
        documentElement << " >>";
        objects[documentStructElementObject] = documentElement.str();

        std::ostringstream parentTree;
        parentTree << "<< /Nums [";
        for (std::size_t pageIndex = 0; pageIndex < state_->pages.size(); ++pageIndex) {
            parentTree << pageIndex << " [";
            for (const auto elementId : structElementIds[pageIndex]) parentTree << elementId << " 0 R ";
            parentTree << "] ";
            for (std::size_t index = 0; index < linkStructElementIds[pageIndex].size(); ++index) {
                parentTree << linkStructParentKeys[pageIndex][index] << ' '
                           << linkStructElementIds[pageIndex][index] << " 0 R ";
            }
            for (std::size_t index = 0; index < attachmentStructElementIds[pageIndex].size(); ++index) {
                parentTree << attachmentStructParentKeys[pageIndex][index] << ' '
                           << attachmentStructElementIds[pageIndex][index] << " 0 R ";
            }
        }
        parentTree << "] >>";
        objects[parentTreeObject] = parentTree.str();

        for (std::size_t pageIndex = 0; pageIndex < state_->pages.size(); ++pageIndex) {
            const auto& marked = state_->pages[pageIndex].markedContents;
            for (std::size_t itemIndex = 0; itemIndex < marked.size(); ++itemIndex) {
                const auto& item = marked[itemIndex];
                std::ostringstream element;
                element << "<< /Type /StructElem /S /" << item.role;
                if (standardStructureNamespaceObject != 0) {
                    element << " /NS " << standardStructureNamespaceObject << " 0 R";
                }
                element << " /P ";
                if (item.parentIndex) element << structElementIds[pageIndex][*item.parentIndex] << " 0 R";
                else element << documentStructElementObject << " 0 R";
                element << " /Pg " << pageIds[pageIndex] << " 0 R /K ";
                if (item.childIndices.empty()) element << item.mcid;
                else {
                    element << '[' << item.mcid << ' ';
                    for (const auto childIndex : item.childIndices) {
                        element << structElementIds[pageIndex][childIndex] << " 0 R ";
                    }
                    element << ']';
                }
                if (!item.alternativeText.empty()) element << " /Alt (" << escapePdfString(item.alternativeText) << ')';
                if (!item.actualText.empty()) element << " /ActualText (" << escapePdfString(item.actualText) << ')';
                if (!item.language.empty()) element << " /Lang (" << escapePdfString(item.language) << ')';
                if (!item.title.empty()) element << " /T (" << escapePdfString(item.title) << ')';
                if (!item.expandedText.empty()) element << " /E (" << escapePdfString(item.expandedText) << ')';
                if (!item.identifier.empty()) element << " /ID (" << escapePdfString(item.identifier) << ')';
                const auto& attributes = item.attributes;
                const bool hasLayoutAttributes = attributes.placement || attributes.textAlignment ||
                    attributes.boundingBox || attributes.width || attributes.height;
                const bool hasTableAttributes = attributes.rowSpan != 1U || attributes.columnSpan != 1U ||
                    attributes.scope != PdfTableScope::None || !attributes.headers.empty();
                if (hasLayoutAttributes || hasTableAttributes) {
                    element << " /A [";
                    if (hasLayoutAttributes) {
                        element << "<< /O /Layout";
                        if (attributes.placement) element << " /Placement " << structurePlacementName(*attributes.placement);
                        if (attributes.textAlignment) element << " /TextAlign " << structureTextAlignmentName(*attributes.textAlignment);
                        if (attributes.boundingBox) {
                            element << " /BBox [" << attributes.boundingBox->left << ' '
                                    << attributes.boundingBox->bottom << ' '
                                    << attributes.boundingBox->right << ' '
                                    << attributes.boundingBox->top << ']';
                        }
                        if (attributes.width) element << " /Width " << *attributes.width;
                        if (attributes.height) element << " /Height " << *attributes.height;
                        element << " >>";
                    }
                    if (hasTableAttributes) {
                        element << " << /O /Table";
                        if (attributes.rowSpan != 1U) element << " /RowSpan " << attributes.rowSpan;
                        if (attributes.columnSpan != 1U) element << " /ColSpan " << attributes.columnSpan;
                        if (const auto* scope = tableScopeName(attributes.scope)) element << " /Scope " << scope;
                        if (!attributes.headers.empty()) {
                            element << " /Headers [";
                            for (const auto& header : attributes.headers) element << '(' << escapePdfString(header) << ") ";
                            element << ']';
                        }
                        element << " >>";
                    }
                    element << ']';
                }
                element << " >>";
                objects[structElementIds[pageIndex][itemIndex]] = element.str();
            }

            for (std::size_t index = 0; index < state_->pages[pageIndex].links.size(); ++index) {
                const auto& link = state_->pages[pageIndex].links[index];
                std::ostringstream element;
                element << "<< /Type /StructElem /S /" << link.options.structureRole;
                if (standardStructureNamespaceObject != 0) element << " /NS " << standardStructureNamespaceObject << " 0 R";
                element << " /P " << documentStructElementObject << " 0 R /Pg " << pageIds[pageIndex]
                        << " 0 R /K << /Type /OBJR /Obj " << linkIds[pageIndex][index]
                        << " 0 R /Pg " << pageIds[pageIndex] << " 0 R >> /Alt ("
                        << escapePdfString(link.options.accessibleDescription) << ") >>";
                objects[linkStructElementIds[pageIndex][index]] = element.str();
            }
            for (std::size_t index = 0; index < state_->pages[pageIndex].fileAttachments.size(); ++index) {
                const auto& attachment = state_->pages[pageIndex].fileAttachments[index];
                const std::string description = attachment.options.alternativeText.empty()
                    ? attachment.options.contents : attachment.options.alternativeText;
                std::ostringstream element;
                element << "<< /Type /StructElem /S /" << attachment.options.structureRole;
                if (standardStructureNamespaceObject != 0) element << " /NS " << standardStructureNamespaceObject << " 0 R";
                element << " /P " << documentStructElementObject << " 0 R /Pg " << pageIds[pageIndex]
                        << " 0 R /K << /Type /OBJR /Obj " << attachmentIds[pageIndex][index]
                        << " 0 R /Pg " << pageIds[pageIndex] << " 0 R >> /Alt ("
                        << escapePdfString(description) << ") >>";
                objects[attachmentStructElementIds[pageIndex][index]] = element.str();
            }
        }
    }
    for (std::size_t i = 0; i < state_->cffFonts.size(); ++i) {
        const auto& cff = state_->cffFonts[i].font;
        const std::string base = cff.name.empty() ? "PdfPPCFF" : cff.name;
        const auto& raw = cff.data;
        std::string cffFile(reinterpret_cast<const char*>(raw.data()), raw.size());
        objects[cffFileIds[i]] = "<< /Length " + std::to_string(cffFile.size())
            + " /Subtype /Type1C >>\nstream\n" + cffFile + "\nendstream";
        objects[cffDescIds[i]] = "<< /Type /FontDescriptor /FontName /" + base
            + " /Flags 32 /FontBBox [-250 -250 1000 1000] /ItalicAngle 0 /Ascent 800 /Descent -200 "
            + "/CapHeight 700 /StemV 80 /FontFile3 " + std::to_string(cffFileIds[i]) + " 0 R >>";
        objects[cffIds[i]] = "<< /Type /Font /Subtype /Type1 /BaseFont /" + base
            + " /FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding /FontDescriptor "
            + std::to_string(cffDescIds[i]) + " 0 R /Widths [";
        // Use the parsed CFF advance widths for 32..255 where the font has glyphs.
        std::ostringstream cffWidths;
        for (std::uint32_t code = 32U; code <= 255U; ++code) {
            const std::uint32_t glyphId = code - 32U;
            const double advance = glyphId < cff.glyphCount
                ? PdfCffParser::GetAdvanceWidth(cff, glyphId)
                : cff.privateDict.defaultWidthX;
            cffWidths << std::lround(advance) << (code == 255U ? "" : " ");
        }
        objects[cffIds[i]] += cffWidths.str() + " ] >>";
    }
    for (std::size_t i = 0; i < state_->tilingPatterns.size(); ++i) {
        const auto& pattern = state_->tilingPatterns[i].options;
        const double xStep = pattern.xStep > 0.0 ? pattern.xStep : (pattern.bbox.right - pattern.bbox.left);
        const double yStep = pattern.yStep > 0.0 ? pattern.yStep : (pattern.bbox.top - pattern.bbox.bottom);
        std::ostringstream p;
        p << "<< /Type /Pattern /PatternType 1 /PaintType " << (pattern.paintTypeColor ? 1 : 2)
          << " /TilingType " << static_cast<int>(pattern.tilingType)
          << " /BBox [" << pattern.bbox.left << ' ' << pattern.bbox.bottom << ' '
          << pattern.bbox.right << ' ' << pattern.bbox.top << "] /XStep " << xStep
          << " /YStep " << yStep << " /Matrix [";
        for (const double value : pattern.matrix) p << value << ' ';
        p << "] /Resources << >> /Length " << pattern.content.size()
          << " >>\nstream\n" << pattern.content << "\nendstream";
        objects[patternIds[i]] = p.str();
    }


    for (std::size_t i = 0; i < state_->meshShadings.size(); ++i) {
        const auto& shading = state_->meshShadings[i];
        const auto bounds = meshBounds(shading);
        const auto raw = encodeMeshShading(shading, bounds);
        const auto compressed = compressBytes(raw);
        std::ostringstream dictionary;
        int shadingType = 4;
        switch (shading.kind) {
        case Internal::PdfWriterMeshShadingKind::FreeForm: shadingType = 4; break;
        case Internal::PdfWriterMeshShadingKind::Lattice: shadingType = 5; break;
        case Internal::PdfWriterMeshShadingKind::CoonsPatch: shadingType = 6; break;
        case Internal::PdfWriterMeshShadingKind::TensorProductPatch: shadingType = 7; break;
        }
        dictionary << "<< /ShadingType " << shadingType
                   << " /ColorSpace " << deviceColorSpaceName(shading.colorSpace)
                   << " /BitsPerCoordinate 16 /BitsPerComponent 8";
        if (shading.kind == Internal::PdfWriterMeshShadingKind::Lattice) {
            dictionary << " /VerticesPerRow " << shading.verticesPerRow;
        } else {
            dictionary << " /BitsPerFlag 2";
        }
        dictionary << " /Decode [" << bounds.minX << ' ' << bounds.maxX << ' '
                   << bounds.minY << ' ' << bounds.maxY;
        const auto componentCount = deviceColorSpaceComponents(shading.colorSpace);
        for (std::size_t component = 0; component < componentCount; ++component) {
            dictionary << " 0 1";
        }
        dictionary << "] /AntiAlias " << (shading.antiAlias ? "true" : "false")
                   << " /Filter /FlateDecode /Length " << compressed.size()
                   << " >>\nstream\n";
        objects[shadingIds[i]] = dictionary.str() + compressed + "\nendstream";
    }

    for (std::size_t i = 0; i < state_->colorSpaces.size(); ++i) {
        const auto& colorSpace = state_->colorSpaces[i];
        if (colorSpace.kind == Internal::PdfWriterColorSpaceKind::IccBased) {
            const std::string profile(reinterpret_cast<const char*>(colorSpace.profileBytes.data()),
                                      colorSpace.profileBytes.size());
            std::ostringstream stream;
            stream << "<< /N " << static_cast<unsigned>(colorSpace.components)
                   << " /Alternate " << deviceColorSpaceName(colorSpace.alternate)
                   << " /Length " << profile.size() << " >>\nstream\n"
                   << profile << "\nendstream";
            objects[colorSpaceAuxIds[i]] = stream.str();
        } else if (colorSpace.kind == Internal::PdfWriterColorSpaceKind::Separation) {
            std::ostringstream function;
            function << "<< /FunctionType 2 /Domain [0 1] /C0 [";
            for (const double component : colorSpace.c0) function << component << ' ';
            function << "] /C1 [";
            for (const double component : colorSpace.c1) function << component << ' ';
            function << "] /N " << colorSpace.exponent << " >>";
            objects[colorSpaceAuxIds[i]] = function.str();
        } else {
            std::ostringstream domain;
            for (std::size_t component = 0; component < colorSpace.colorantNames.size(); ++component) {
                domain << "0 1 ";
            }
            std::ostringstream range;
            for (std::size_t component = 0; component < deviceColorSpaceComponents(colorSpace.alternate); ++component) {
                range << "0 1 ";
            }
            const std::string program = "{ " + colorSpace.tintTransformProgram + " }";
            std::ostringstream function;
            function << "<< /FunctionType 4 /Domain [" << domain.str() << "] /Range ["
                     << range.str() << "] /Length " << program.size() << " >>\nstream\n"
                     << program << "\nendstream";
            objects[colorSpaceAuxIds[i]] = function.str();
        }
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
        std::ostringstream fonts;
        if (base14Font != 0 && contentUsesBase14Font(p.content)) fonts << "/F1 " << base14Font << " 0 R ";
        for (const auto fontIndex : p.embeddedFontIndices) {
            fonts << '/' << state_->embeddedFonts.at(fontIndex).resourceName << ' '
                  << fontIds.at(fontIndex).type0 << " 0 R ";
        }
        for (const auto fontIndex : p.type1FontIndices) {
            fonts << '/' << state_->type1Fonts.at(fontIndex).resourceName << ' '
                  << type1Ids.at(fontIndex) << " 0 R ";
        }
        for (const auto fontIndex : p.cffFontIndices) {
            fonts << '/' << state_->cffFonts.at(fontIndex).resourceName << ' '
                  << cffIds.at(fontIndex) << " 0 R ";
        }
        for (const auto fontIndex : p.type3FontIndices) {
            fonts << '/' << state_->type3Fonts.at(fontIndex).resourceName << ' '
                  << type3Ids.at(fontIndex).font << " 0 R ";
        }
        std::ostringstream xObjects;for(const auto ii:p.imageIndices)xObjects<<'/'<<state_->images.at(ii).resourceName<<' '<<imageIds.at(ii)<<" 0 R ";
        std::string resources="<< /Font << "+fonts.str()+">>";if(!p.imageIndices.empty())resources+=" /XObject << "+xObjects.str()+">>";if(!p.extGStateIndices.empty()){std::ostringstream gs;for(const auto gi:p.extGStateIndices)gs<<'/'<<state_->extGStates.at(gi).resourceName<<' '<<extGStateIds.at(gi)<<" 0 R ";resources+=" /ExtGState << "+gs.str()+">>";}if(!p.patternIndices.empty()){std::ostringstream pat;for(const auto pi:p.patternIndices){const auto& opts=state_->tilingPatterns.at(pi).options;pat<<'/'<<opts.name<<' '<<patternIds.at(pi)<<" 0 R ";}resources+=" /Pattern << "+pat.str()+">>";}if(!p.shadingIndices.empty()){std::ostringstream sh;for(const auto si:p.shadingIndices){const auto& shading=state_->meshShadings.at(si);sh<<'/'<<shading.resourceName<<' '<<shadingIds.at(si)<<" 0 R ";}resources+=" /Shading << "+sh.str()+">>";}
        if (!p.colorSpaceIndices.empty()) {
            std::ostringstream colorSpaces;
            for (const auto colorSpaceIndex : p.colorSpaceIndices) {
                const auto& colorSpace = state_->colorSpaces.at(colorSpaceIndex);
                colorSpaces << '/' << colorSpace.resourceName << ' ';
                if (colorSpace.kind == Internal::PdfWriterColorSpaceKind::IccBased) {
                    colorSpaces << "[/ICCBased " << colorSpaceAuxIds.at(colorSpaceIndex) << " 0 R] ";
                } else if (colorSpace.kind == Internal::PdfWriterColorSpaceKind::Separation) {
                    colorSpaces << "[/Separation /" << colorSpace.colorantNames.front() << ' '
                                << deviceColorSpaceName(colorSpace.alternate) << ' '
                                << colorSpaceAuxIds.at(colorSpaceIndex) << " 0 R] ";
                } else {
                    colorSpaces << "[/DeviceN [";
                    for (const auto& colorant : colorSpace.colorantNames) colorSpaces << '/' << colorant << ' ';
                    colorSpaces << "] " << deviceColorSpaceName(colorSpace.alternate) << ' '
                                << colorSpaceAuxIds.at(colorSpaceIndex) << " 0 R] ";
                }
            }
            resources += " /ColorSpace << " + colorSpaces.str() + ">>";
        }
        if(!p.ocgResources.empty()){std::ostringstream props;props<<" /Properties << ";for(const auto& name:p.ocgResources){const std::size_t index=name.size()>2?static_cast<std::size_t>(std::stoul(name.substr(2)))-1U:0U;if(index<state_->ocgs.size())props<<'/'<<name<<' '<<ocgIds[index]<<" 0 R ";}props<<">>";resources+=props.str();}resources+=" >>";
        std::string annotations;
        if (!linkIds[i].empty() || !attachmentIds[i].empty()) {
            annotations = " /Annots [";
            for (const auto id : linkIds[i]) annotations += std::to_string(id) + " 0 R ";
            for (const auto id : attachmentIds[i]) annotations += std::to_string(id) + " 0 R ";
            annotations += ']';
        }
        const std::string structParents = state_->tagged
            ? " /StructParents " + std::to_string(i) + " /Tabs /S" : std::string{};
        objects[pageIds[i]]="<< /Type /Page /Parent "+std::to_string(pages)+" 0 R /MediaBox ["+box.str()+"]"+std::string(p.cropBox?(" /CropBox ["+std::to_string(p.cropBox->left)+" "+std::to_string(p.cropBox->bottom)+" "+std::to_string(p.cropBox->right)+" "+std::to_string(p.cropBox->top)+"]"):"")+std::string(p.rotation!=0?" /Rotate "+std::to_string(p.rotation):"")+structParents+" /Resources "+resources+" /Contents "+std::to_string(contentIds[i])+" 0 R"+annotations+" >>";objects[contentIds[i]]="<< /Length "+std::to_string(p.content.size())+" >>\nstream\n"+p.content+"endstream";
        for (std::size_t j = 0; j < p.links.size(); ++j) {
            const auto& link = p.links[j];
            const auto& rectangle = link.options.rectangle;
            std::ostringstream annotation;
            annotation << "<< /Type /Annot /Subtype /Link /Rect ["
                       << rectangle.left << ' ' << rectangle.bottom << ' '
                       << rectangle.right << ' ' << rectangle.top << "] /P "
                       << pageIds[i] << " 0 R /F 4";
            if (state_->tagged) {
                annotation << " /StructParent " << linkStructParentKeys[i][j]
                           << " /Contents (" << escapePdfString(link.options.accessibleDescription) << ')';
            } else if (!link.options.accessibleDescription.empty()) {
                annotation << " /Contents (" << escapePdfString(link.options.accessibleDescription) << ')';
            }
            if (link.options.drawBorder) {
                annotation << " /Border [0 0 " << link.options.borderWidth << "] /C ["
                           << link.options.borderColor.r << ' ' << link.options.borderColor.g << ' '
                           << link.options.borderColor.b << ']';
            } else {
                annotation << " /Border [0 0 0]";
            }
            if (link.kind == Internal::PdfWriterLinkKind::NamedDestination) {
                annotation << " /Dest (" << escapePdfString(link.target) << ')';
            } else if (link.kind == Internal::PdfWriterLinkKind::Uri) {
                annotation << " /A << /S /URI /URI (" << escapePdfString(link.target) << ") >>";
            } else if (link.kind == Internal::PdfWriterLinkKind::Remote) {
                annotation << " /A << /S /GoToR /F (" << escapePdfString(link.target)
                           << ") /D (" << escapePdfString(link.destination) << ") >>";
            } else {
                annotation << " /A << /S /Launch /F (" << escapePdfString(link.target) << ") >>";
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
                       << rectangle.right << ' ' << rectangle.top << "] /P " << pageIds[i] << " 0 R /F 4"
                       << " /FS " << embeddedFileIds[fileIndex].fileSpec << " 0 R"
                       << " /Name " << fileAttachmentIconName(attachment.options.icon);
            const std::string accessibleText = attachment.options.alternativeText.empty()
                ? attachment.options.contents : attachment.options.alternativeText;
            if (!accessibleText.empty()) annotation << " /Contents (" << escapePdfString(accessibleText) << ')';
            if (state_->tagged) annotation << " /StructParent " << attachmentStructParentKeys[i][j];
            annotation << " >>";
            objects[attachmentIds[i][j]] = annotation.str();
        }
    }
    if (security) objects[encryptionObject] = security->EncryptionDictionary();

    Internal::PdfObjectCollectionWriterOptions collectionOptions;
    collectionOptions.pdfVersion = pdfA ? pdfA->pdfVersion : (pdfUa ? pdfUa->pdfVersion : "1.7");
    collectionOptions.writeXrefStream = pdfA && pdfA->part == 1 ? false : options.writeXrefStream;
    collectionOptions.writeObjectStreams = pdfA && pdfA->part == 1 ? false : options.writeObjectStreams;
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
    if (options.mode == PdfSaveMode::Incremental) {
        if (document.GetPath().empty()) {
            throw std::invalid_argument(
                "Incremental resave requires a document opened from a file path.");
        }
        Internal::PdfIncrementalWriterOptions incrementalOptions;
        incrementalOptions.writeXrefStream = options.writeXrefStream;
        incrementalOptions.writeObjectStreams = options.writeObjectStreams;
        Internal::PdfIncrementalWriter writer(
            document.GetPath(), outputPath, document, incrementalOptions);

        const auto encryptionReference = document.GetTrailerReference(PdfName("Encrypt"));
        for (const std::uint32_t objectNumber : document.objectNumbers()) {
            if (encryptionReference && encryptionReference->objectNumber == objectNumber) {
                // The encryption dictionary is inherited through /Prev and must never itself
                // be encrypted or duplicated in the new revision.
                continue;
            }
            const auto xref = document.GetXrefEntry(objectNumber);
            if (!xref || !xref->inUse) continue;
            const PdfReference reference{objectNumber, xref->generation};
            try {
                writer.WriteObject(reference, document.GetObject(reference));
            } catch (const PdfException&) {
                // Preserve damaged-but-readable files: objects that cannot be materialized
                // remain available from the previous revision through /Prev.
            }
        }
        writer.Finish(Internal::PdfIncrementalWriter::NextObjectNumber(document));
        return;
    }

    std::ofstream output(outputPath, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot create PDF output file");
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
