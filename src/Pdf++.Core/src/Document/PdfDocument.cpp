#include "CPPPdf/PdfDocument.hpp"
#include "CPPPdf/PdfError.hpp"
#include "CPPPdf/PdfPage.hpp"
#include "Internal/Document/PdfObjectResolver.hpp"
#include "Internal/Security/PdfStandardSecurity.hpp"
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"
#include "CPPPdf/Filters/PdfFilterPipeline.hpp"
#include "CPPPdf/Fonts/PdfFontResource.hpp"
#include "CPPPdf/Graphics/PdfImage.hpp"
#include "CPPPdf/Content/PdfContentProcessor.hpp"
#include "CPPPdf/Rendering/PdfDisplayList.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <functional>
#include <future>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <zlib.h>

namespace CPPPdf {
namespace {

constexpr std::size_t kTailSearchSize = 64U * 1024U;

[[nodiscard]] std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

[[nodiscard]] bool startsWithAt(std::span<const char> bytes,
                                std::size_t offset,
                                std::string_view text) {
    return offset <= bytes.size() && text.size() <= bytes.size() - offset &&
           std::equal(text.begin(), text.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] std::size_t skipWhitespace(std::span<const char> bytes, std::size_t pos) {
    while (pos < bytes.size()) {
        const unsigned char ch = static_cast<unsigned char>(bytes[pos]);
        if (!std::isspace(ch) && ch != 0) {
            break;
        }
        ++pos;
    }
    return pos;
}

[[nodiscard]] std::string readLine(std::span<const char> bytes, std::size_t& pos) {
    if (pos >= bytes.size()) {
        return {};
    }

    const std::size_t begin = pos;
    while (pos < bytes.size() && bytes[pos] != '\r' && bytes[pos] != '\n') {
        ++pos;
    }
    std::string line(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                     bytes.begin() + static_cast<std::ptrdiff_t>(pos));

    if (pos < bytes.size() && bytes[pos] == '\r') {
        ++pos;
    }
    if (pos < bytes.size() && bytes[pos] == '\n') {
        ++pos;
    }
    return line;
}

[[nodiscard]] std::uint64_t parseUnsigned64(std::string_view text, const char* context) {
    std::uint64_t value{};
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw PdfException(PdfErrorCode::MalformedXref,
                           std::string("Invalid integer while parsing ") + context + ": " + std::string(text));
    }
    return value;
}

[[nodiscard]] std::size_t findDictionaryEnd(std::span<const char> bytes, std::size_t begin) {
    if (!startsWithAt(bytes, begin, "<<")) {
        throw PdfException(PdfErrorCode::MalformedObject, "Dictionary does not start with <<.");
    }

    int depth = 0;
    bool inLiteralString = false;
    bool escaped = false;
    for (std::size_t i = begin; i + 1 < bytes.size(); ++i) {
        const char ch = bytes[i];
        if (inLiteralString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == ')') {
                inLiteralString = false;
            }
            continue;
        }
        if (ch == '(') {
            inLiteralString = true;
            continue;
        }
        if (bytes[i] == '<' && bytes[i + 1] == '<') {
            ++depth;
            ++i;
            continue;
        }
        if (bytes[i] == '>' && bytes[i + 1] == '>') {
            --depth;
            i += 1;
            if (depth == 0) {
                return i + 1;
            }
        }
    }
    throw PdfException(PdfErrorCode::MalformedObject, "Unterminated PDF dictionary.");
}


[[nodiscard]] std::string decodePdfLiteralString(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        char ch = value[i];
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (++i >= value.size()) {
            break;
        }
        ch = value[i];
        switch (ch) {
        case 'n': result.push_back('\n'); break;
        case 'r':
            if (i + 1 < value.size() && value[i + 1] == '\n') ++i;
            break;
        case 't': result.push_back('\t'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case '(':
        case ')':
        case '\\': result.push_back(ch); break;
        case '\r':
            if (i + 1 < value.size() && value[i + 1] == '\n') ++i;
            break;
        case '\n': break;
        default:
            if (ch >= '0' && ch <= '7') {
                int octal = ch - '0';
                for (int count = 0; count < 2 && i + 1 < value.size() &&
                     value[i + 1] >= '0' && value[i + 1] <= '7'; ++count) {
                    octal = octal * 8 + (value[++i] - '0');
                }
                result.push_back(static_cast<char>(octal & 0xFF));
            } else {
                result.push_back(ch);
            }
            break;
        }
    }
    return result;
}

[[nodiscard]] int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

[[nodiscard]] std::string decodePdfHexString(std::string_view value) {
    std::string compact;
    compact.reserve(value.size());
    for (char ch : value) {
        if (!std::isspace(static_cast<unsigned char>(ch))) compact.push_back(ch);
    }
    if ((compact.size() % 2U) != 0U) compact.push_back('0');

    std::string result;
    result.reserve(compact.size() / 2U);
    for (std::size_t i = 0; i + 1 < compact.size(); i += 2) {
        const int high = hexValue(compact[i]);
        const int low = hexValue(compact[i + 1]);
        if (high >= 0 && low >= 0) result.push_back(static_cast<char>((high << 4) | low));
    }
    return result;
}

[[nodiscard]] std::string inflateZlib(std::string_view input, std::size_t maxSize) {
    if (input.empty()) return {};

    if (input.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "FlateDecode input exceeds the zlib input limit.");
    }

    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (inflateInit(&stream) != Z_OK) {
        throw PdfException(PdfErrorCode::MalformedObject, "Cannot initialize FlateDecode decompressor.");
    }

    std::string output;
    std::vector<char> buffer(32U * 1024U);
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = buffer.size() - stream.avail_out;
        if (output.size() > maxSize || produced > maxSize - output.size()) {
            inflateEnd(&stream);
            throw PdfException(PdfErrorCode::UnsupportedFeature,
                               "Decoded stream exceeds configured limit.");
        }
        output.append(buffer.data(), produced);
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END) {
        throw PdfException(PdfErrorCode::MalformedObject, "Invalid or unsupported FlateDecode stream.");
    }
    return output;
}


[[nodiscard]] std::size_t skipContentWhitespace(std::string_view content, std::size_t pos) {
    while (pos < content.size()) {
        const unsigned char ch = static_cast<unsigned char>(content[pos]);
        if (std::isspace(ch)) { ++pos; continue; }
        if (content[pos] == '%') {
            while (pos < content.size() && content[pos] != '\r' && content[pos] != '\n') ++pos;
            continue;
        }
        break;
    }
    return pos;
}

[[nodiscard]] std::string parseContentString(std::string_view content, std::size_t& pos) {
    if (pos >= content.size()) return {};
    if (content[pos] == '(') {
        ++pos;
        const std::size_t begin = pos;
        int depth = 1;
        bool escaped = false;
        while (pos < content.size() && depth > 0) {
            const char ch = content[pos];
            if (escaped) { escaped = false; ++pos; continue; }
            if (ch == '\\') { escaped = true; ++pos; continue; }
            if (ch == '(') ++depth;
            else if (ch == ')') --depth;
            ++pos;
        }
        const std::size_t end = depth == 0 ? pos - 1 : pos;
        return decodePdfLiteralString(content.substr(begin, end - begin));
    }
    if (content[pos] == '<' && (pos + 1 >= content.size() || content[pos + 1] != '<')) {
        ++pos;
        const std::size_t begin = pos;
        while (pos < content.size() && content[pos] != '>') ++pos;
        const auto result = decodePdfHexString(content.substr(begin, pos - begin));
        if (pos < content.size()) ++pos;
        return result;
    }
    return {};
}


[[nodiscard]] const PdfObject* resolveObject(
    const PdfDocument& document,
    const PdfObject* object) {
    if (object == nullptr) return nullptr;
    const auto reference = object->AsReference();
    if (!reference.has_value()) return object;
    return &document.GetObject(PdfReference{reference->first, reference->second});
}

[[nodiscard]] const PdfDictionary* objectDictionary(
    const PdfDocument& document,
    const PdfObject* object) {
    object = resolveObject(document, object);
    if (object == nullptr) return nullptr;
    if (const auto* dictionary = object->AsDictionary()) return dictionary;
    if (const auto* stream = object->AsStream()) return &stream->dictionary();
    return nullptr;
}

// Returns the dictionary of a parsed object whether it is a plain dictionary
// or a stream (whose dictionary wraps the stream data).
[[nodiscard]] const PdfDictionary* asDictionary(const PdfObject& object) {
    if (const auto* dictionary = object.AsDictionary()) return dictionary;
    if (const auto* stream = object.AsStream()) return &stream->dictionary();
    return nullptr;
}

[[nodiscard]] const PdfDictionary* inheritedPageResources(
    const PdfDocument& document,
    PdfReference pageReference) {
    std::unordered_set<std::uint64_t> visited;
    for (std::size_t depth = 0; depth < 256U; ++depth) {
        const std::uint64_t key = (static_cast<std::uint64_t>(pageReference.objectNumber) << 16U) |
                                  pageReference.generation;
        if (!visited.insert(key).second) return nullptr;
        const auto* pageDictionary = objectDictionary(document, &document.GetObject(pageReference));
        if (pageDictionary == nullptr) return nullptr;
        if (const auto* resources = objectDictionary(
                document, pageDictionary->Find(PdfName::Resources))) {
            return resources;
        }
        const auto* parent = pageDictionary->Find(PdfName("Parent"));
        const auto parentReference = parent ? parent->AsReference() : std::nullopt;
        if (!parentReference.has_value()) return nullptr;
        pageReference = PdfReference{parentReference->first, parentReference->second};
    }
    return nullptr;
}

using PageFontMap = std::unordered_map<std::string, PdfFontResource>;

[[nodiscard]] std::shared_ptr<PageFontMap> buildFontMap(
    const PdfDocument& document,
    const PdfDictionary* resources) {
    auto fonts = std::make_shared<PageFontMap>();
    if (resources == nullptr) return fonts;
    const auto* fontDictionary = objectDictionary(document, resources->Find(PdfName::Font));
    if (fontDictionary == nullptr) return fonts;

    const PdfFontResource::Resolver resolver = [&document](const PdfReference& reference)
        -> const PdfObject& {
        return document.GetObject(reference);
    };
    for (const auto& [resourceName, fontObject] : fontDictionary->values()) {
        const auto* dictionary = objectDictionary(document, &fontObject);
        if (dictionary == nullptr) continue;
        try {
            if (const auto indirect = fontObject.AsReference()) {
                const PdfReference reference{indirect->first, indirect->second};
                if (const auto cached = document.GetCachedFontResource(reference)) {
                    fonts->insert_or_assign(resourceName.value(), *cached);
                    continue;
                }
            }
            fonts->insert_or_assign(resourceName.value(), PdfFontResource::Create(*dictionary, resolver));
        } catch (const std::exception&) {
            // Keep extraction alive when one font resource is malformed.
        }
    }
    return fonts;
}


void attachPageFontResolver(
    PdfTextExtractionRequest& request,
    const std::shared_ptr<PageFontMap>& fonts) {
    request.fontResolver = [fonts](const std::uint32_t, const std::string_view resourceName)
        -> const PdfFontResource* {
        const auto it = fonts->find(std::string(resourceName));
        return it == fonts->end() ? nullptr : &it->second;
    };
}

[[nodiscard]] double objectNumberValue(const PdfObject& object, const double fallback) {
    if (const auto integer = object.AsInteger()) return static_cast<double>(*integer);
    if (const auto real = object.AsReal()) return *real;
    return fallback;
}

[[nodiscard]] std::array<double, 6> matrixFromDictionary(const PdfDictionary& dictionary) {
    std::array<double, 6> result{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    const auto* matrix = dictionary.GetAsArray(PdfName("Matrix"));
    if (matrix == nullptr || matrix->size() < 6U) return result;
    for (std::size_t index = 0; index < 6U; ++index) {
        result[index] = objectNumberValue(matrix->at(index), result[index]);
    }
    return result;
}

[[nodiscard]] std::array<double, 6> multiplyMatrices(
    const std::array<double, 6>& lhs,
    const std::array<double, 6>& rhs) noexcept {
    return {
        lhs[0] * rhs[0] + lhs[2] * rhs[1],
        lhs[1] * rhs[0] + lhs[3] * rhs[1],
        lhs[0] * rhs[2] + lhs[2] * rhs[3],
        lhs[1] * rhs[2] + lhs[3] * rhs[3],
        lhs[0] * rhs[4] + lhs[2] * rhs[5] + lhs[4],
        lhs[1] * rhs[4] + lhs[3] * rhs[5] + lhs[5]
    };
}

using StreamDecoder = std::function<std::string(const PdfReference&)>;

[[nodiscard]] std::string joinDecodedStreams(
    const std::vector<PdfReference>& references,
    const StreamDecoder& decoder) {
    if (references.empty()) return {};
    std::vector<std::string> decoded;
    decoded.reserve(references.size());
    std::size_t totalBytes = references.size() - 1U;
    for (const auto& reference : references) {
        decoded.push_back(decoder(reference));
        totalBytes += decoded.back().size();
    }
    std::string content;
    content.reserve(totalBytes);
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        if (index != 0U) content.push_back('\n');
        content.append(decoded[index]);
    }
    return content;
}

void extractContentRecursively(
    const PdfDocument& document,
    const std::string_view content,
    const PdfDictionary* resources,
    PdfTextExtractionRequest request,
    const StreamDecoder& decodeStream,
    std::unordered_set<std::uint64_t>& activeForms,
    const std::size_t depth,
    std::vector<PdfTextChunk>& output) {
    if (depth > 32U) return;

    const auto fonts = buildFontMap(document, resources);
    attachPageFontResolver(request, fonts);
    request.resourceObjectNumber = request.sourceObjectNumber;
    request.xObjectHandler = [&](const std::string_view resourceName,
                                 const std::array<double, 6>& invocationCtm,
                                 std::vector<PdfTextChunk>& destination) {
        if (resources == nullptr) return;
        const auto* xObjects = objectDictionary(
            document, resources->Find(PdfName("XObject")));
        if (xObjects == nullptr) return;
        const auto* xObjectEntry = xObjects->Find(PdfName(std::string(resourceName)));
        if (xObjectEntry == nullptr) return;

        PdfReference reference{};
        const PdfStream* stream{};
        if (const auto indirect = xObjectEntry->AsReference()) {
            reference = PdfReference{indirect->first, indirect->second};
            const std::uint64_t key =
                (static_cast<std::uint64_t>(reference.objectNumber) << 16U) |
                reference.generation;
            if (!activeForms.insert(key).second) return;
            stream = document.GetObject(reference).AsStream();
            if (stream == nullptr) {
                activeForms.erase(key);
                return;
            }
        } else {
            stream = xObjectEntry->AsStream();
            if (stream == nullptr) return;
        }

        const auto subtype = stream->dictionary().GetAsName(PdfName("Subtype"));
        if (!subtype.has_value() || subtype->value() != "Form") {
            if (reference.objectNumber != 0U) {
                const std::uint64_t key =
                    (static_cast<std::uint64_t>(reference.objectNumber) << 16U) |
                    reference.generation;
                activeForms.erase(key);
            }
            return;
        }

        const auto formMatrix = matrixFromDictionary(stream->dictionary());
        PdfTextExtractionRequest childRequest = request;
        childRequest.initialTransformationMatrix =
            multiplyMatrices(formMatrix, invocationCtm);
        childRequest.sourceObjectNumber = reference.objectNumber;

        const PdfDictionary* childResources = objectDictionary(
            document, stream->dictionary().Find(PdfName::Resources));
        if (childResources == nullptr) childResources = resources;

        std::string childContent;
        if (reference.objectNumber != 0U) {
            childContent = decodeStream(reference);
        } else {
            const auto bytes = stream->bytes();
            childContent.assign(
                reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }

        extractContentRecursively(
            document,
            childContent,
            childResources,
            std::move(childRequest),
            decodeStream,
            activeForms,
            depth + 1U,
            destination);

        if (reference.objectNumber != 0U) {
            const std::uint64_t key =
                (static_cast<std::uint64_t>(reference.objectNumber) << 16U) |
                reference.generation;
            activeForms.erase(key);
        }
    };

    auto chunks = PdfTextExtractor::ExtractChunks(content, request);
    output.insert(output.end(),
                  std::make_move_iterator(chunks.begin()),
                  std::make_move_iterator(chunks.end()));
}

std::optional<PdfTransparencyGroupProperties> resolveTransparencyGroup(
    const PdfDocument& document, const PdfDictionary& dictionary) {
    const auto* groupObject = dictionary.Find(PdfName("Group"));
    if (groupObject == nullptr) return std::nullopt;
    const auto* group = objectDictionary(document, groupObject);
    if (group == nullptr) return std::nullopt;
    const auto subtype = group->GetAsName(PdfName("S"));
    if (!subtype.has_value() || subtype->value() != "Transparency") return std::nullopt;
    PdfTransparencyGroupProperties properties;
    const auto readBoolean = [&](const char* key) {
        const auto* value = group->Find(PdfName(key));
        return value != nullptr && value->AsBoolean().value_or(false);
    };
    properties.isolated = readBoolean("I");
    properties.knockout = readBoolean("K");
    if (const auto blend = group->GetAsName(PdfName("BM"))) properties.blendMode = blend->value();
    if (const auto alpha = group->Find(PdfName("CA"))) {
        if (const auto real = alpha->AsReal()) properties.alpha = std::clamp(*real, 0.0, 1.0);
    }
    return properties;
}

void processPageContentRecursively(
    const PdfDocument& document,
    const std::string_view content,
    const PdfDictionary* resources,
    const PdfTextStateSnapshot& initialState,
    const StreamDecoder& decodeStream,
    std::unordered_set<std::uint64_t>& activeForms,
    const std::size_t depth,
    const std::uint32_t resourceObjectNumber,
    const PdfDocument::PdfContentEventHandler& handler) {
    if (depth > 32U || !handler) return;

    std::vector<bool> markedContentGroups;
    PdfContentProcessor processor;
    processor.SetHandler([&](const PdfContentEvent& event) {
        PdfContentEvent scopedEvent = event;
        scopedEvent.resourceScope = depth == 0U ? "Page" : "Form" + std::to_string(depth);
        scopedEvent.resourceObjectNumber = resourceObjectNumber;

        // Resolve name-referenced BDC property lists (e.g. "/P1 BDC") through
        // the current resource scope. Inline dictionaries were already turned
        // into BeginTransparencyGroup by the content processor.
        if (event.type == PdfContentEventType::BeginMarkedContent &&
            !event.markedContentProperty.empty() &&
            event.markedContentProperty.front() != '<') {
            const auto* properties = resources ? objectDictionary(
                document, resources->Find(PdfName("Properties"))) : nullptr;
            const auto* propertyObject = properties ? properties->Find(
                PdfName(event.markedContentProperty)) : nullptr;
            const auto* property = propertyObject ? objectDictionary(document, propertyObject) : nullptr;
            if (property) {
                const auto group = resolveTransparencyGroup(document, *property);
                if (group.has_value()) {
                    scopedEvent.type = PdfContentEventType::BeginTransparencyGroup;
                    scopedEvent.transparencyGroup = *group;
                    markedContentGroups.push_back(true);
                    handler(scopedEvent);
                    return;
                }
            }
            markedContentGroups.push_back(false);
            handler(scopedEvent);
            return;
        }
        if (event.type == PdfContentEventType::BeginTransparencyGroup) {
            markedContentGroups.push_back(true);
            handler(scopedEvent);
            return;
        }
        if (event.type == PdfContentEventType::EndTransparencyGroup) {
            if (!markedContentGroups.empty()) markedContentGroups.pop_back();
            handler(scopedEvent);
            return;
        }
        if (event.type == PdfContentEventType::EndMarkedContent) {
            const bool closesGroup = !markedContentGroups.empty() && markedContentGroups.back();
            if (!markedContentGroups.empty()) markedContentGroups.pop_back();
            if (closesGroup) {
                scopedEvent.type = PdfContentEventType::EndTransparencyGroup;
            }
            handler(scopedEvent);
            return;
        }
        handler(scopedEvent);
        if (event.type != PdfContentEventType::InvokeXObject || resources == nullptr) return;

        const auto* xObjects = objectDictionary(document, resources->Find(PdfName("XObject")));
        if (xObjects == nullptr) return;
        const auto* entry = xObjects->Find(PdfName(event.text));
        if (entry == nullptr) return;

        PdfReference reference{};
        const PdfStream* stream{};
        if (const auto indirect = entry->AsReference()) {
            reference = PdfReference{indirect->first, indirect->second};
            const std::uint64_t key =
                (static_cast<std::uint64_t>(reference.objectNumber) << 16U) |
                reference.generation;
            if (!activeForms.insert(key).second) return;
            stream = document.GetObject(reference).AsStream();
            if (stream == nullptr) {
                activeForms.erase(key);
                return;
            }
        } else {
            stream = entry->AsStream();
            if (stream == nullptr) return;
        }

        const auto subtype = stream->dictionary().GetAsName(PdfName("Subtype"));
        if (!subtype.has_value() || subtype->value() != "Form") {
            if (reference.objectNumber != 0U) {
                const std::uint64_t key =
                    (static_cast<std::uint64_t>(reference.objectNumber) << 16U) |
                    reference.generation;
                activeForms.erase(key);
            }
            return;
        }

        const auto formMatrix = matrixFromDictionary(stream->dictionary());
        PdfTextStateSnapshot childState = event.textState;
        childState.currentTransformationMatrix =
            multiplyMatrices(formMatrix, event.textState.currentTransformationMatrix);
        const PdfDictionary* childResources = objectDictionary(
            document, stream->dictionary().Find(PdfName("Resources")));
        if (childResources == nullptr) childResources = resources;

        std::string childContent;
        if (reference.objectNumber != 0U) {
            childContent = decodeStream(reference);
        } else {
            const auto bytes = stream->bytes();
            childContent.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }

        const auto group = resolveTransparencyGroup(
            document, stream->dictionary());
        if (group.has_value()) {
            PdfContentEvent begin;
            begin.type = PdfContentEventType::BeginTransparencyGroup;
            begin.text = event.text;
            begin.operation = event.operation;
            begin.textState = event.textState;
            begin.resourceScope = scopedEvent.resourceScope;
            begin.resourceObjectNumber = scopedEvent.resourceObjectNumber;
            begin.transparencyGroup = *group;
            handler(begin);
        }
        processPageContentRecursively(document, childContent, childResources, childState,
                                      decodeStream, activeForms, depth + 1U,
                                      reference.objectNumber, handler);
        if (group.has_value()) {
            PdfContentEvent end;
            end.type = PdfContentEventType::EndTransparencyGroup;
            end.text = event.text;
            end.operation = event.operation;
            end.textState = event.textState;
            end.resourceScope = scopedEvent.resourceScope;
            end.resourceObjectNumber = scopedEvent.resourceObjectNumber;
            handler(end);
        }

        if (reference.objectNumber != 0U) {
            const std::uint64_t key =
                (static_cast<std::uint64_t>(reference.objectNumber) << 16U) |
                reference.generation;
            activeForms.erase(key);
        }
    });
    processor.Process(content, initialState);
}


[[nodiscard]] std::uint32_t dictionaryUnsigned(
    const PdfDictionary& dictionary,
    const char* key,
    const std::uint32_t fallback = 0U) {
    const auto* object = dictionary.Find(PdfName(key));
    if (object == nullptr) return fallback;
    if (const auto integer = object->AsInteger(); integer.has_value() && *integer >= 0) {
        return static_cast<std::uint32_t>(*integer);
    }
    return fallback;
}

[[nodiscard]] bool dictionaryBoolean(
    const PdfDictionary& dictionary,
    const char* key,
    const bool fallback = false) {
    const auto* object = dictionary.Find(PdfName(key));
    if (object == nullptr) return fallback;
    return object->AsBoolean().value_or(fallback);
}

[[nodiscard]] PdfImageColorSpace imageColorSpace(const PdfDictionary& dictionary) {
    const auto* object = dictionary.Find(PdfName("ColorSpace"));
    if (object == nullptr) object = dictionary.Find(PdfName("CS"));
    if (object == nullptr) return PdfImageColorSpace::Unknown;
    const PdfName* name = object->AsName();
    if (name == nullptr) return object->AsArray() != nullptr
        ? PdfImageColorSpace::Indexed : PdfImageColorSpace::Unknown;
    const auto& value = name->value();
    if (value == "DeviceGray" || value == "G") return PdfImageColorSpace::DeviceGray;
    if (value == "DeviceRGB" || value == "RGB") return PdfImageColorSpace::DeviceRGB;
    if (value == "DeviceCMYK" || value == "CMYK") return PdfImageColorSpace::DeviceCMYK;
    if (value == "Indexed" || value == "I") return PdfImageColorSpace::Indexed;
    if (value == "ICCBased") return PdfImageColorSpace::ICCBased;
    if (value == "Separation") return PdfImageColorSpace::Separation;
    if (value == "DeviceN") return PdfImageColorSpace::DeviceN;
    if (value == "Pattern") return PdfImageColorSpace::Pattern;
    return PdfImageColorSpace::Unknown;
}

void imageColorSpaceMetadata(const PdfDocument& document, const PdfDictionary& dictionary, PdfImageInfo& info) {
    const auto* object = dictionary.Find(PdfName("ColorSpace"));
    if (object == nullptr) object = dictionary.Find(PdfName("CS"));
    const auto* array = object ? object->AsArray() : nullptr;
    if (array == nullptr || array->size() < 2U) return;
    const auto* name = array->at(0).AsName();
    if (name == nullptr) return;
    // A color space encoded as an array ([ /Separation ... ], [ /ICCBased ... ],
    // [ /Indexed ... ], [ /DeviceN ... ]) must update the resolved enum, not
    // default to Indexed.
    const auto& spaceName = name->value();
    if (spaceName == "Indexed" || spaceName == "I") {
        info.colorSpace = PdfImageColorSpace::Indexed;
        info.colorSpaceHighValue = static_cast<std::uint32_t>(array->at(2).AsInteger().value_or(255));
        if (const auto* lookup = array->at(3).AsString()) {
            const auto& bytes = *lookup;
            info.colorSpaceData.assign(reinterpret_cast<const std::byte*>(bytes.data()),
                                       reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()));
        }
    } else if (spaceName == "Separation" && array->size() >= 4U) {
        info.colorSpace = PdfImageColorSpace::Separation;
        info.colorSpaceComponents = 1U;
        if (const auto* alternate = array->at(2).AsName()) {
            info.hasSeparationAlternate = alternate->value() == "DeviceGray" ||
                alternate->value() == "DeviceRGB" || alternate->value() == "DeviceCMYK";
            info.separationAlternate[0] = alternate->value() == "DeviceGray" ? 1U :
                (alternate->value() == "DeviceCMYK" ? 4U : 3U);
        }
        const PdfObject* functionObject = &array->at(3);
        if (const auto reference = functionObject->AsReference()) {
            functionObject = &document.GetObject({reference->first, reference->second});
        }
        const auto* function = functionObject->AsDictionary();
        if (function != nullptr && function->Find(PdfName("FunctionType")) != nullptr &&
            function->Find(PdfName("FunctionType"))->AsInteger().value_or(0) == 2) {
            const auto* c0 = function->GetAsArray(PdfName("C0"));
            const auto* c1 = function->GetAsArray(PdfName("C1"));
            if (c0 != nullptr && c1 != nullptr && c0->size() == c1->size() && !c0->empty()) {
                for (std::size_t index = 0; index < c0->size(); ++index) {
                    info.separationC0.push_back(objectNumberValue(c0->at(index), 0.0));
                    info.separationC1.push_back(objectNumberValue(c1->at(index), 1.0));
                }
                if (const auto* exponent = function->Find(PdfName("N"))) {
                    info.separationExponent = objectNumberValue(*exponent, 1.0);
                    info.hasSeparationFunction = true;
                }
            }
        }
    } else if (spaceName == "ICCBased") {
        info.colorSpace = PdfImageColorSpace::ICCBased;
        const PdfObject* profile = &array->at(1);
        if (const auto reference = profile->AsReference()) profile = &document.GetObject({reference->first, reference->second});
        if (const auto* stream = profile->AsStream()) {
            info.hasIccProfile = true;
            info.colorSpaceComponents = static_cast<std::uint8_t>(stream->dictionary().Find(PdfName("N"))
                ? stream->dictionary().Find(PdfName("N"))->AsInteger().value_or(0) : 0);
            const auto bytes = stream->bytes();
            info.iccProfileBytes.assign(bytes.begin(), bytes.end());
        }
    } else if (spaceName == "DeviceN" && array->size() >= 3U) {
        info.colorSpace = PdfImageColorSpace::DeviceN;
        if (const auto* names = array->at(1).AsArray()) info.deviceNComponentCount = static_cast<std::uint32_t>(names->size());
        info.colorSpaceComponents = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, info.deviceNComponentCount));
        // Alternate space (index 2) and a tint transform function (index 3)
        // are shared with the Separation path so the renderer can fall back to
        // the alternate color when no DeviceN function array is present.
        if (const auto* alternate = array->at(2).AsName()) {
            info.hasSeparationAlternate = alternate->value() == "DeviceGray" ||
                alternate->value() == "DeviceRGB" || alternate->value() == "DeviceCMYK";
            info.separationAlternate[0] = alternate->value() == "DeviceGray" ? 1U :
                (alternate->value() == "DeviceCMYK" ? 4U : 3U);
        }
        if (array->size() >= 4U) {
            const PdfObject* functionObject = &array->at(3);
            if (const auto reference = functionObject->AsReference()) {
                functionObject = &document.GetObject({reference->first, reference->second});
            }
            // A single shared function applies to every component.
            const auto* function = functionObject->AsDictionary();
            if (function != nullptr && function->Find(PdfName("FunctionType")) != nullptr &&
                function->Find(PdfName("FunctionType"))->AsInteger().value_or(0) == 2) {
                const auto* c0 = function->GetAsArray(PdfName("C0"));
                const auto* c1 = function->GetAsArray(PdfName("C1"));
                if (c0 != nullptr && c1 != nullptr && c0->size() == c1->size() && !c0->empty()) {
                    info.separationC0.clear();
                    info.separationC1.clear();
                    for (std::size_t index = 0; index < c0->size(); ++index) {
                        info.separationC0.push_back(objectNumberValue(c0->at(index), 0.0));
                        info.separationC1.push_back(objectNumberValue(c1->at(index), 1.0));
                    }
                    if (const auto* exponent = function->Find(PdfName("N"))) {
                        info.separationExponent = objectNumberValue(*exponent, 1.0);
                        info.hasSeparationFunction = true;
                    }
                }
            }
        }
    }
}

[[nodiscard]] std::vector<std::string> imageFilterNames(const PdfDictionary& dictionary) {
    const auto* object = dictionary.Find(PdfName("Filter"));
    if (object == nullptr) object = dictionary.Find(PdfName("F"));
    std::vector<std::string> names;
    if (object == nullptr) return names;
    if (const auto* name = object->AsName()) {
        names.push_back(name->value());
    } else if (const auto* array = object->AsArray()) {
        for (const auto& item : array->values()) {
            if (const auto* arrayName = item.AsName()) names.push_back(arrayName->value());
        }
    }
    return names;
}

[[nodiscard]] PdfImageEncoding imageEncodingFromName(const std::string_view name) {
    if (name == "FlateDecode" || name == "Fl") return PdfImageEncoding::Flate;
    if (name == "ASCIIHexDecode" || name == "AHx") return PdfImageEncoding::AsciiHex;
    if (name == "ASCII85Decode" || name == "A85") return PdfImageEncoding::Ascii85;
    if (name == "RunLengthDecode" || name == "RL") return PdfImageEncoding::RunLength;
    if (name == "DCTDecode" || name == "DCT") return PdfImageEncoding::Dct;
    if (name == "JPXDecode") return PdfImageEncoding::Jpx;
    if (name == "CCITTFaxDecode" || name == "CCF") return PdfImageEncoding::CcittFax;
    if (name == "JBIG2Decode") return PdfImageEncoding::Jbig2;
    return PdfImageEncoding::Unsupported;
}

[[nodiscard]] PdfRectangle transformedUnitSquare(const std::array<double, 6>& matrix) {
    const auto transform = [&](const double x, const double y) {
        return PdfPoint{
            matrix[0] * x + matrix[2] * y + matrix[4],
            matrix[1] * x + matrix[3] * y + matrix[5]};
    };
    const std::array<PdfPoint, 4> points{
        transform(0.0, 0.0), transform(1.0, 0.0),
        transform(1.0, 1.0), transform(0.0, 1.0)};
    PdfRectangle box{points[0].x, points[0].y, points[0].x, points[0].y};
    for (const auto& point : points) {
        box.left = std::min(box.left, point.x);
        box.bottom = std::min(box.bottom, point.y);
        box.right = std::max(box.right, point.x);
        box.top = std::max(box.top, point.y);
    }
    return box;
}


[[nodiscard]] std::string predictorParameters(const PdfDictionary* dictionary) {
    if (dictionary == nullptr) return {};
    std::ostringstream output;
    output << "<<";
    const auto append = [&](const char* key, const int fallback) {
        const auto value = dictionaryUnsigned(*dictionary, key, static_cast<std::uint32_t>(fallback));
        output << " /" << key << ' ' << value;
    };
    append("Predictor", 1);
    append("Colors", 1);
    append("BitsPerComponent", 8);
    append("Columns", 1);
    append("EarlyChange", 1);
    output << " >>";
    return output.str();
}

[[nodiscard]] std::vector<std::string> imageDecodeParameters(
    const PdfDictionary& dictionary,
    const std::size_t filterCount) {
    std::vector<std::string> result(filterCount);
    const PdfObject* object = dictionary.Find(PdfName("DecodeParms"));
    if (object == nullptr) object = dictionary.Find(PdfName("DP"));
    if (object == nullptr) return result;
    if (const auto* parameters = object->AsDictionary()) {
        if (!result.empty()) result.front() = predictorParameters(parameters);
        return result;
    }
    if (const auto* array = object->AsArray()) {
        for (std::size_t i = 0; i < result.size() && i < array->size(); ++i) {
            result[i] = predictorParameters(array->at(i).AsDictionary());
        }
    }
    return result;
}

// Builds the ordered filter chain for a stream from its typed dictionary.
// Each filter receives the serialized /DecodeParms entry that applies to it so
// the shared decoding pipeline can configure predictors.
[[nodiscard]] std::vector<PdfFilterSpec> filterSpecsFromDictionary(
    const PdfDictionary& dictionary) {
    std::vector<PdfFilterSpec> specs;
    const PdfObject* filterObject = dictionary.Find(PdfName("Filter"));
    if (filterObject == nullptr) return specs;
    if (const PdfName* name = filterObject->AsName()) {
        specs.push_back(PdfFilterSpec{name->value(), {}});
    } else if (const PdfArray* array = filterObject->AsArray()) {
        specs.reserve(array->size());
        for (const PdfObject& item : array->values()) {
            if (const PdfName* filterName = item.AsName()) {
                specs.push_back(PdfFilterSpec{filterName->value(), {}});
            }
        }
    }

    const PdfObject* parameters = dictionary.Find(PdfName("DecodeParms"));
    if (parameters == nullptr) parameters = dictionary.Find(PdfName("DP"));
    if (parameters == nullptr) return specs;
    if (const auto* single = parameters->AsDictionary()) {
        if (!specs.empty()) specs.front().decodeParameters = predictorParameters(single);
        return specs;
    }
    if (const auto* array = parameters->AsArray()) {
        for (std::size_t i = 0; i < specs.size() && i < array->size(); ++i) {
            if (const auto* parms = array->at(i).AsDictionary()) {
                specs[i].decodeParameters = predictorParameters(parms);
            }
        }
    }
    return specs;
}

[[nodiscard]] std::string stripInlineName(std::string value) {
    if (!value.empty() && value.front() == '/') value.erase(value.begin());
    return value;
}

[[nodiscard]] PdfDictionary inlineImageDictionary(
    const std::vector<PdfInlineImageProperty>& properties) {
    PdfDictionary dictionary;
    for (const auto& property : properties) {
        std::string key = property.name;
        if (key == "W") key = "Width";
        else if (key == "H") key = "Height";
        else if (key == "BPC") key = "BitsPerComponent";
        else if (key == "CS") key = "ColorSpace";
        else if (key == "F") key = "Filter";
        else if (key == "DP") key = "DecodeParms";
        else if (key == "IM") key = "ImageMask";
        const std::string value = property.value;
        if (!value.empty() && value.front() == '/') {
            auto name = stripInlineName(value);
            if (name == "G") name = "DeviceGray";
            else if (name == "RGB") name = "DeviceRGB";
            else if (name == "CMYK") name = "DeviceCMYK";
            dictionary.Put(PdfName(key), PdfObject(PdfName(std::move(name))));
        } else if (value == "true" || value == "false") {
            dictionary.Put(PdfName(key), PdfObject(value == "true"));
        } else {
            char* end{};
            const long long number = std::strtoll(value.c_str(), &end, 10);
            if (end == value.c_str() + value.size()) {
                dictionary.Put(PdfName(key), PdfObject(static_cast<std::int64_t>(number)));
            }
        }
    }
    return dictionary;
}

[[nodiscard]] PdfExtractedImage makeExtractedImage(
    const PdfDocument& document,
    const PdfStream& stream,
    const PdfReference reference,
    std::string resourceName,
    const std::array<double, 6>& ctm,
    const PdfImageExtractionOptions& options,
    const std::size_t maxDecodedSize) {
    PdfExtractedImage image;
    image.info.resourceName = std::move(resourceName);
    image.info.reference = reference;
    image.info.sourceObjectNumber = reference.objectNumber;
    image.info.width = dictionaryUnsigned(stream.dictionary(), "Width",
        dictionaryUnsigned(stream.dictionary(), "W"));
    image.info.height = dictionaryUnsigned(stream.dictionary(), "Height",
        dictionaryUnsigned(stream.dictionary(), "H"));
    image.info.bitsPerComponent = static_cast<std::uint16_t>(dictionaryUnsigned(
        stream.dictionary(), "BitsPerComponent", dictionaryUnsigned(stream.dictionary(), "BPC", 8U)));
    image.info.colorSpace = imageColorSpace(stream.dictionary());
    imageColorSpaceMetadata(document, stream.dictionary(), image.info);
    image.info.imageMask = dictionaryBoolean(stream.dictionary(), "ImageMask",
        dictionaryBoolean(stream.dictionary(), "IM"));
    image.info.boundingBox = transformedUnitSquare(ctm);

    const auto bytes = stream.bytes();
    if (options.keepEncodedBytes) image.encodedBytes.assign(bytes.begin(), bytes.end());
    const auto filterNames = imageFilterNames(stream.dictionary());
    image.info.encoding = filterNames.empty()
        ? PdfImageEncoding::Raw : imageEncodingFromName(filterNames.back());

    bool canDecode = options.decodeSupportedFilters;
    std::vector<PdfFilterSpec> filters;
    const auto decodeParameters = imageDecodeParameters(stream.dictionary(), filterNames.size());
    std::size_t filterIndex{};
    for (const auto& filterName : filterNames) {
        const auto encoding = imageEncodingFromName(filterName);
        if (encoding == PdfImageEncoding::Dct || encoding == PdfImageEncoding::Jpx ||
            encoding == PdfImageEncoding::CcittFax || encoding == PdfImageEncoding::Jbig2 ||
            encoding == PdfImageEncoding::Unsupported) {
            canDecode = false;
            break;
        }
        filters.push_back(PdfFilterSpec{filterName,
            filterIndex < decodeParameters.size() ? decodeParameters[filterIndex] : std::string{}});
        ++filterIndex;
    }
    if (canDecode) {
        if (filters.empty()) image.decodedBytes.assign(bytes.begin(), bytes.end());
        else image.decodedBytes = PdfFilterPipeline(maxDecodedSize).Decode(bytes, filters);
        image.info.decoded = true;
    }

    auto attachMask = [&](const char* key, const bool softMask) {
        if (!options.extractImageMasks) return;
        const PdfObject* maskObject = stream.dictionary().Find(PdfName(key));
        if (maskObject == nullptr) return;
        PdfReference maskReference{};
        const PdfStream* maskStream{};
        if (const auto indirect = maskObject->AsReference()) {
            maskReference = PdfReference{indirect->first, indirect->second};
            maskStream = document.GetObject(maskReference).AsStream();
        } else {
            maskStream = maskObject->AsStream();
        }
        if (maskStream == nullptr) return;
        const auto maskSubtype = maskStream->dictionary().GetAsName(PdfName("Subtype"));
        if (maskSubtype.has_value() && maskSubtype->value() != "Image") return;
        PdfImageExtractionOptions maskOptions = options;
        maskOptions.keepEncodedBytes = false;
        maskOptions.extractImageMasks = false;
        auto mask = makeExtractedImage(document, *maskStream, maskReference, {}, ctm,
                                       maskOptions, maxDecodedSize);
        if (!mask.info.decoded) return;
        image.alphaBytes = std::move(mask.decodedBytes);
        if (softMask) {
            image.info.hasSoftMask = true;
            image.info.softMaskReference = maskReference;
            image.info.softMaskWidth = mask.info.width;
            image.info.softMaskHeight = mask.info.height;
        } else {
            image.info.hasExplicitMask = true;
            image.info.explicitMaskReference = maskReference;
        }
    };

    attachMask("SMask", true);
    attachMask("Mask", false);
    return image;
}

void extractImagesRecursively(
    const PdfDocument& document,
    const std::string_view content,
    const PdfDictionary* resources,
    const std::array<double, 6>& initialCtm,
    const PdfImageExtractionOptions& options,
    const StreamDecoder& decodeStream,
    std::unordered_set<std::uint64_t>& activeForms,
    const std::size_t depth,
    std::vector<PdfExtractedImage>& output) {
    if (depth > options.maxRecursionDepth) return;
    PdfContentProcessor processor;
    processor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::RenderInlineImage) {
            if (!options.includeInlineImages) return;
            auto dictionary = inlineImageDictionary(event.inlineImageDictionary);
            PdfStream stream(std::move(dictionary), event.bytes);
            auto image = makeExtractedImage(
                document, stream, {}, {}, event.textState.currentTransformationMatrix,
                options, document.readerOptions().limits.maxDecodedStreamSize);
            image.info.inlineImage = true;
            image.info.fillAlpha = event.textState.fillAlpha;
            image.info.strokeAlpha = event.textState.strokeAlpha;
            output.push_back(std::move(image));
            return;
        }
        if (event.type != PdfContentEventType::InvokeXObject || resources == nullptr) return;
        const auto* xObjects = objectDictionary(document, resources->Find(PdfName("XObject")));
        if (xObjects == nullptr) return;
        const auto* entry = xObjects->Find(PdfName(event.text));
        if (entry == nullptr) return;
        PdfReference reference{};
        const PdfStream* stream{};
        if (const auto indirect = entry->AsReference()) {
            reference = PdfReference{indirect->first, indirect->second};
            stream = document.GetObject(reference).AsStream();
        } else {
            stream = entry->AsStream();
        }
        if (stream == nullptr) return;
        const auto subtype = stream->dictionary().GetAsName(PdfName("Subtype"));
        if (!subtype.has_value()) return;
        if (subtype->value() == "Image") {
            auto image = makeExtractedImage(
                document, *stream, reference, event.text,
                event.textState.currentTransformationMatrix,
                options, document.readerOptions().limits.maxDecodedStreamSize);
            image.info.fillAlpha = event.textState.fillAlpha;
            image.info.strokeAlpha = event.textState.strokeAlpha;
            output.push_back(std::move(image));
            return;
        }
        if (subtype->value() != "Form" || !options.includeFormXObjects) return;
        const std::uint64_t key =
            (static_cast<std::uint64_t>(reference.objectNumber) << 16U) | reference.generation;
        if (reference.objectNumber != 0U && !activeForms.insert(key).second) return;
        const auto formMatrix = matrixFromDictionary(stream->dictionary());
        const auto childCtm = multiplyMatrices(
            formMatrix, event.textState.currentTransformationMatrix);
        const PdfDictionary* childResources = objectDictionary(
            document, stream->dictionary().Find(PdfName::Resources));
        if (childResources == nullptr) childResources = resources;
        std::string childContent;
        if (reference.objectNumber != 0U) childContent = decodeStream(reference);
        else {
            const auto streamBytes = stream->bytes();
            childContent.assign(reinterpret_cast<const char*>(streamBytes.data()), streamBytes.size());
        }
        extractImagesRecursively(document, childContent, childResources, childCtm,
            options, decodeStream, activeForms, depth + 1U, output);
        if (reference.objectNumber != 0U) activeForms.erase(key);
    });
    PdfTextStateSnapshot initialState;
    initialState.currentTransformationMatrix = initialCtm;
    processor.Process(content, initialState);
}

} // namespace


PdfDocument::PdfDocument() = default;
PdfDocument::~PdfDocument() = default;
PdfDocument::PdfDocument(PdfDocument&&) noexcept = default;
PdfDocument& PdfDocument::operator=(PdfDocument&&) noexcept = default;

PdfDocument PdfDocument::Open(const std::filesystem::path& path) {
    return Open(path, PdfReaderOptions{});
}

PdfDocument PdfDocument::Open(const std::filesystem::path& path, const PdfReaderOptions& options) {
    if (options.limits.memoryMapThresholdBytes != 0U) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (!error && static_cast<std::uint64_t>(size) >= options.limits.memoryMapThresholdBytes) {
            return OpenMapped(path, options);
        }
    }
    auto source = std::make_unique<PdfFileInputSource>(path);
    PdfDocument document = Open(std::move(source), options);
    document.path_ = path;
    return document;
}

PdfDocument PdfDocument::OpenMapped(const std::filesystem::path& path, const PdfReaderOptions& options) {
    auto source = std::make_unique<PdfMappedFileInputSource>(path);
    PdfDocument document = Open(std::move(source), options);
    document.path_ = path;
    return document;
}

PdfDocument PdfDocument::Open(std::span<const std::byte> bytes, const PdfReaderOptions& options) {
    return Open(std::make_unique<PdfMemoryInputSource>(bytes), options);
}

PdfDocument PdfDocument::Open(std::istream& stream, const PdfReaderOptions& options) {
    return Open(std::make_unique<PdfStreamInputSource>(stream), options);
}

PdfDocument PdfDocument::Open(std::unique_ptr<PdfInputSource> source, const PdfReaderOptions& options) {
    if (!source) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "The PDF input source is null.");
    }
    PdfDocument document;
    document.readerOptions_ = options;
    document.source_ = std::move(source);
    document.bytes_ = document.source_->View();
    if (document.bytes_.empty() && document.source_->Size() != 0U) {
        document.ownedBytes_ = document.source_->ReadAll();
        document.bytes_ = document.ownedBytes_;
    }
    document.parse();
    if (document.xref_.size() > options.limits.maxObjectCount) {
        throw PdfException(PdfErrorCode::MalformedXref, "PDF object count exceeds configured limit.");
    }
    return document;
}

void PdfDocument::parse() {
    parseHeader();
    try {
        parseXrefSection(findStartXref());
    } catch (...) {
        if (readerOptions_.strictParsing || !readerOptions_.repairDamagedXref) throw;
        bool recovered = false;
        for (const std::uint64_t candidate : findRecoveredXrefOffsets()) {
            xref_.clear();
            parsedXrefOffsets_.clear();
            trailerDictionary_.clear();
            try {
                parseXrefSection(candidate);
                if (!xref_.empty() && !trailerDictionary_.empty()) {
                    recovered = true;
                    break;
                }
            } catch (...) {
                // Try the next xref marker; damaged files often contain stale
                // fragments before the last usable table or stream.
            }
        }
        if (!recovered) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "Unable to recover a usable xref table or stream.");
        }
    }
    initializeEncryption();
}

std::vector<std::uint64_t> PdfDocument::findRecoveredXrefOffsets() const {
    std::vector<std::uint64_t> offsets;
    const std::string_view input(bytes_.data(), bytes_.size());
    std::size_t position = 0U;
    while ((position = input.find("xref", position)) != std::string_view::npos) {
        const bool leftBoundary = position == 0U ||
            std::isspace(static_cast<unsigned char>(input[position - 1U]));
        const std::size_t after = position + 4U;
        const bool rightBoundary = after >= input.size() ||
            std::isspace(static_cast<unsigned char>(input[after]));
        if (leftBoundary && rightBoundary) offsets.push_back(static_cast<std::uint64_t>(position));
        position = after;
    }

    position = 0U;
    while ((position = input.find("/Type /XRef", position)) != std::string_view::npos) {
        const std::size_t objectMarker = input.rfind("obj", position);
        if (objectMarker != std::string_view::npos && objectMarker < position) {
            const std::size_t lineBegin = input.rfind('\n', objectMarker);
            const std::size_t candidate = lineBegin == std::string_view::npos
                ? 0U : lineBegin + 1U;
            if (candidate < objectMarker) offsets.push_back(static_cast<std::uint64_t>(candidate));
        }
        position += 10U;
    }
    std::sort(offsets.begin(), offsets.end(), std::greater<>());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    return offsets;
}

void PdfDocument::initializeEncryption() {
    if (trailerDictionary_.find("/Encrypt") == std::string::npos) return;

    const PdfObject trailerObject = Internal::PdfObjectParser::Parse(
        trailerDictionary_, readerOptions_.limits.maxRecursionDepth);
    const PdfDictionary* trailer = trailerObject.AsDictionary();
    const PdfObject* encryptObject = trailer ? trailer->Find(PdfName("Encrypt")) : nullptr;
    const auto encryptReference = encryptObject ? encryptObject->AsReference() : std::nullopt;
    if (!encryptReference.has_value()) {
        throw PdfException(PdfErrorCode::UnsupportedEncryption,
                           "Encrypted PDF trailer does not contain a valid /Encrypt reference.");
    }
    encryptionReference_ = PdfReference{encryptReference->first, encryptReference->second};
    const std::string dictionary = readRawIndirectObject(encryptionReference_->objectNumber);
    const PdfObject* idObject = trailer ? trailer->Find(PdfName("ID")) : nullptr;
    const PdfArray* ids = idObject ? idObject->AsArray() : nullptr;
    const std::string* firstId = ids && !ids->empty() ? ids->values()[0].AsString() : nullptr;
    if (!firstId || firstId->size() < 16U) {
        throw PdfException(PdfErrorCode::UnsupportedEncryption,
                           "Encrypted PDF trailer does not contain a valid file ID.");
    }
    const std::vector<std::uint8_t> id(firstId->begin(), firstId->end());
    encryption_ = std::make_unique<Internal::PdfStandardSecurity>(
        Internal::PdfStandardSecurity::Parse(dictionary, id));

    if (encryption_->Authenticate(readerOptions_.password)) return;
    if (!readerOptions_.password.empty()) {
        throw PdfException(PdfErrorCode::InvalidPassword, "The PDF password is invalid.");
    }
}

bool PdfDocument::IsPasswordRequired() const noexcept {
    return encryption_ && !encryption_->IsAuthenticated();
}

bool PdfDocument::AuthenticatePassword(const std::string_view password) {
    if (!encryption_) return true;
    if (!encryption_->Authenticate(password)) return false;
    ClearObjectCache();
    pageReferencesCached_ = false;
    pageReferencesCache_.clear();
    readerOptions_.password.assign(password);
    return true;
}

bool PdfDocument::IsOwnerPasswordAuthenticated() const noexcept {
    return encryption_ && encryption_->IsOwnerAuthenticated();
}

std::string PdfDocument::EncryptObjectForIncrementalWrite(
    const std::string_view object, const PdfReference& reference) const {
    if (!encryption_) return std::string(object);
    if (!encryption_->IsAuthenticated()) {
        throw PdfException(PdfErrorCode::PasswordRequired,
                           "A valid password is required to update this encrypted PDF.");
    }
    return encryption_->EncryptObject(object, reference.objectNumber, reference.generation);
}

std::int32_t PdfDocument::GetPermissionBits() const noexcept {
    return encryption_ ? encryption_->PermissionBits() : -1;
}

void PdfDocument::parseHeader() {
    constexpr std::string_view marker = "%PDF-";
    const std::size_t inspectLength = std::min<std::size_t>(bytes_.size(), 1024U);
    const std::string_view prefix(bytes_.data(), inspectLength);
    const std::size_t position = prefix.find(marker);
    if (position == std::string_view::npos) {
        throw PdfException(PdfErrorCode::InvalidHeader, "PDF header %PDF-x.y was not found.");
    }

    const std::size_t versionBegin = position + marker.size();
    std::size_t versionEnd = versionBegin;
    while (versionEnd < prefix.size() &&
           (std::isdigit(static_cast<unsigned char>(prefix[versionEnd])) || prefix[versionEnd] == '.')) {
        ++versionEnd;
    }
    version_ = std::string(prefix.substr(versionBegin, versionEnd - versionBegin));
    if (version_.empty()) {
        throw PdfException(PdfErrorCode::InvalidHeader, "PDF version is missing from header.");
    }
}

std::uint64_t PdfDocument::findStartXref() const {
    constexpr std::string_view marker = "startxref";
    const std::size_t begin = bytes_.size() > kTailSearchSize ? bytes_.size() - kTailSearchSize : 0U;
    const std::string_view tail(bytes_.data() + static_cast<std::ptrdiff_t>(begin), bytes_.size() - begin);
    const std::size_t markerPos = tail.rfind(marker);
    if (markerPos == std::string_view::npos) {
        throw PdfException(PdfErrorCode::StartXrefNotFound, "startxref was not found near end of file.");
    }

    std::size_t pos = begin + markerPos + marker.size();
    pos = skipWhitespace(bytes_, pos);
    const std::size_t numberBegin = pos;
    while (pos < bytes_.size() && std::isdigit(static_cast<unsigned char>(bytes_[pos]))) {
        ++pos;
    }
    if (numberBegin == pos) {
        throw PdfException(PdfErrorCode::StartXrefNotFound, "startxref does not contain an offset.");
    }

    const std::string_view number(bytes_.data() + static_cast<std::ptrdiff_t>(numberBegin), pos - numberBegin);
    const auto offset = parseUnsigned64(number, "startxref");
    if (offset >= bytes_.size()) {
        throw PdfException(PdfErrorCode::MalformedXref, "startxref points outside the file.");
    }
    return offset;
}


void PdfDocument::parseXrefSection(std::uint64_t offset) {
    if (!parsedXrefOffsets_.insert(offset).second) {
        if (readerOptions_.strictParsing) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "Cyclic xref revision chain detected.");
        }
        return;
    }

    std::size_t pos = skipWhitespace(bytes_, static_cast<std::size_t>(offset));
    if (startsWithAt(bytes_, pos, "xref")) {
        parseClassicXref(offset);
    } else {
        parseXrefStream(offset);
    }
}

std::string_view PdfDocument::extractStreamDataView(const std::string& streamObject) const {
    // Locate the stream keyword after the dictionary, not inside a string or a
    // name that happens to contain the token.
    std::size_t streamKeyword = std::string::npos;
    const std::size_t dictBegin = streamObject.find("<<");
    if (dictBegin != std::string::npos) {
        try {
            const std::size_t dictEnd = findDictionaryEnd(
                std::span<const char>(streamObject.data(), streamObject.size()), dictBegin);
            streamKeyword = streamObject.find("stream", dictEnd);
        } catch (const PdfException&) {
            return {};
        }
    } else {
        streamKeyword = streamObject.find("stream");
    }
    if (streamKeyword == std::string::npos) return {};
    std::size_t dataBegin = streamKeyword + 6U;
    if (dataBegin < streamObject.size() && streamObject[dataBegin] == '\r') ++dataBegin;
    if (dataBegin < streamObject.size() && streamObject[dataBegin] == '\n') ++dataBegin;

    std::size_t dataEnd = std::string::npos;
    std::size_t length{};
    const PdfObject parsed = Internal::PdfObjectParser::Parse(
        streamObject, readerOptions_.limits.maxRecursionDepth);
    const PdfDictionary* dict = asDictionary(parsed);
    const PdfObject* lengthObject = dict ? dict->Find(PdfName("Length")) : nullptr;
    if (lengthObject != nullptr) {
        if (const auto direct = lengthObject->AsInteger()) {
            if (*direct < 0 || static_cast<std::uint64_t>(*direct) > streamObject.size()) {
                throw PdfException(PdfErrorCode::MalformedObject,
                                   "Stream length is invalid.");
            }
            length = static_cast<std::size_t>(*direct);
        } else if (const auto reference = lengthObject->AsReference()) {
            const PdfObject lengthParsed = Internal::PdfObjectParser::Parse(
                readIndirectObject(reference->first), readerOptions_.limits.maxRecursionDepth);
            const auto indirect = lengthParsed.AsInteger();
            if (!indirect.has_value() || *indirect < 0 ||
                static_cast<std::uint64_t>(*indirect) > streamObject.size()) {
                throw PdfException(PdfErrorCode::MalformedObject,
                                   "Indirect stream length is invalid.");
            }
            length = static_cast<std::size_t>(*indirect);
        } else {
            throw PdfException(PdfErrorCode::MalformedObject,
                               "Stream length is not an integer or reference.");
        }
    }
    if (length <= streamObject.size() - dataBegin) {
        dataEnd = dataBegin + length;
    }
    if (dataEnd == std::string::npos) dataEnd = streamObject.rfind("endstream");
    if (dataEnd == std::string::npos || dataEnd < dataBegin) {
        throw PdfException(PdfErrorCode::MalformedObject, "Stream has no valid endstream marker.");
    }
    return std::string_view(streamObject).substr(dataBegin, dataEnd - dataBegin);
}

std::string PdfDocument::extractStreamData(const std::string& streamObject) const {
    const auto view = extractStreamDataView(streamObject);
    return std::string(view);
}

void PdfDocument::parseXrefStream(std::uint64_t offset64) {
    const std::size_t offset = static_cast<std::size_t>(offset64);
    if (offset >= bytes_.size()) {
        throw PdfException(PdfErrorCode::MalformedXref, "XRef stream offset points outside PDF file.");
    }

    const std::string_view remaining(bytes_.data() + static_cast<std::ptrdiff_t>(offset), bytes_.size() - offset);
    const std::size_t endRelative = remaining.find("endobj");
    if (endRelative == std::string_view::npos) {
        throw PdfException(PdfErrorCode::MalformedXref, "XRef stream object has no endobj marker.");
    }
    const std::string object(remaining.substr(0, endRelative + 6U));

    const PdfObject parsed = Internal::PdfObjectParser::Parse(
        object, readerOptions_.limits.maxRecursionDepth);
    const PdfDictionary* dictionary = asDictionary(parsed);
    if (dictionary == nullptr) {
        throw PdfException(PdfErrorCode::UnsupportedXrefStream,
                           "startxref does not point to a classic xref table or /Type /XRef stream.");
    }
    const auto type = dictionary->GetAsName(PdfName("Type"));
    if (!type.has_value() || type->value() != "XRef") {
        throw PdfException(PdfErrorCode::UnsupportedXrefStream,
                           "startxref does not point to a classic xref table or /Type /XRef stream.");
    }

    const PdfArray* widthArray = dictionary->GetAsArray(PdfName("W"));
    if (widthArray == nullptr || widthArray->size() != 3U) {
        throw PdfException(PdfErrorCode::MalformedXref, "XRef stream /W must contain three integers.");
    }
    std::array<std::size_t, 3> widths{0U, 0U, 0U};
    for (std::size_t i = 0U; i < 3U; ++i) {
        const auto width = widthArray->at(i).AsInteger();
        if (!width.has_value() || *width < 0 || *width > 8) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "XRef stream /W contains an invalid field width.");
        }
        widths[i] = static_cast<std::size_t>(*width);
    }

    std::vector<std::size_t> index;
    if (const PdfArray* indexArray = dictionary->GetAsArray(PdfName("Index"))) {
        for (const PdfObject& item : indexArray->values()) {
            const auto value = item.AsInteger();
            if (!value.has_value() || *value < 0 ||
                static_cast<std::uint64_t>(*value) > std::numeric_limits<std::uint32_t>::max()) {
                throw PdfException(PdfErrorCode::MalformedXref,
                                   "XRef stream /Index contains an invalid value.");
            }
            index.push_back(static_cast<std::size_t>(*value));
        }
    }
    if (index.empty()) {
        const PdfObject* sizeObject = dictionary->Find(PdfName("Size"));
        const auto size = sizeObject ? sizeObject->AsInteger() : std::nullopt;
        if (!size.has_value() || *size < 0) {
            throw PdfException(PdfErrorCode::MalformedXref, "XRef stream /Size is missing or invalid.");
        }
        if (static_cast<std::uint64_t>(*size) > readerOptions_.limits.maxObjectCount ||
            static_cast<std::uint64_t>(*size) > std::numeric_limits<std::uint32_t>::max()) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "XRef stream /Size exceeds configured limits.");
        }
        index = {0U, static_cast<std::size_t>(*size)};
    }
    if ((index.size() % 2U) != 0U) {
        throw PdfException(PdfErrorCode::MalformedXref, "XRef stream /Index must contain pairs.");
    }
    for (std::size_t pair = 0U; pair < index.size(); pair += 2U) {
        if (index[pair + 1U] > readerOptions_.limits.maxObjectCount ||
            static_cast<std::uint64_t>(index[pair]) + index[pair + 1U] >
                static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1ULL) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "XRef stream /Index exceeds configured limits.");
        }
    }

    std::string decoded = extractStreamData(object);
    const auto filters = filterSpecsFromDictionary(*dictionary);
    if (!filters.empty()) {
        const auto* begin = reinterpret_cast<const std::byte*>(decoded.data());
        const auto result = PdfFilterPipeline(readerOptions_.limits.maxDecodedStreamSize)
            .Decode(std::span<const std::byte>(begin, decoded.size()), filters);
        decoded.assign(reinterpret_cast<const char*>(result.data()), result.size());
    }

    const std::size_t entryWidth = widths[0] + widths[1] + widths[2];
    if (entryWidth == 0U) throw PdfException(PdfErrorCode::MalformedXref, "XRef stream entry width is zero.");

    auto readBigEndian = [&](std::size_t& pos, std::size_t width) -> std::uint64_t {
        std::uint64_t value = 0;
        if (pos > decoded.size() || width > decoded.size() - pos) {
            throw PdfException(PdfErrorCode::MalformedXref, "XRef stream ended before all entries were read.");
        }
        for (std::size_t i = 0; i < width; ++i) {
            value = (value << 8U) | static_cast<unsigned char>(decoded[pos++]);
        }
        return value;
    };

    std::size_t pos = 0;
    for (std::size_t pair = 0; pair < index.size(); pair += 2U) {
        const std::uint32_t first = static_cast<std::uint32_t>(index[pair]);
        const std::uint32_t count = static_cast<std::uint32_t>(index[pair + 1U]);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint64_t typeValue = widths[0] == 0U ? 1U : readBigEndian(pos, widths[0]);
            const std::uint64_t field2 = readBigEndian(pos, widths[1]);
            const std::uint64_t field3 = readBigEndian(pos, widths[2]);
            PdfXrefEntry entry{};
            if (typeValue == 0U) {
                entry.type = PdfXrefEntry::Type::Free;
                if (field3 > std::numeric_limits<std::uint16_t>::max()) continue;
                entry.generation = static_cast<std::uint16_t>(field3);
                entry.inUse = false;
            } else if (typeValue == 1U) {
                entry.type = PdfXrefEntry::Type::Uncompressed;
                if (field2 >= bytes_.size()) continue;
                if (field3 > std::numeric_limits<std::uint16_t>::max()) continue;
                entry.offset = field2;
                entry.generation = static_cast<std::uint16_t>(field3);
                entry.inUse = true;
            } else if (typeValue == 2U) {
                entry.type = PdfXrefEntry::Type::Compressed;
                if (field2 > std::numeric_limits<std::uint32_t>::max() ||
                    field3 > std::numeric_limits<std::uint32_t>::max()) continue;
                entry.objectStream = static_cast<std::uint32_t>(field2);
                entry.objectIndex = static_cast<std::uint32_t>(field3);
                entry.inUse = true;
            } else {
                continue;
            }
            xref_.try_emplace(first + i, entry);
        }
    }

    if (trailerDictionary_.empty()) {
        const std::size_t dictBegin = object.find("<<");
        if (dictBegin != std::string::npos) {
            const std::size_t dictEnd = findDictionaryEnd(
                std::span<const char>(object.data(), object.size()), dictBegin);
            trailerDictionary_ = object.substr(dictBegin, dictEnd - dictBegin);
        }
    }

    if (const PdfObject* prev = dictionary->Find(PdfName("Prev"))) {
        if (const auto previous = prev->AsInteger(); previous.has_value() && *previous >= 0) {
            const auto previousOffset = static_cast<std::uint64_t>(*previous);
            if (previousOffset >= bytes_.size()) {
                throw PdfException(PdfErrorCode::MalformedXref,
                                   "XRef stream /Prev points outside the file.");
            }
            parseXrefSection(previousOffset);
        } else if (prev->type() != PdfObjectType::Null) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "XRef stream /Prev is invalid.");
        }
    }
}

void PdfDocument::parseClassicXref(std::uint64_t offset64) {
    std::size_t pos = static_cast<std::size_t>(offset64);
    pos = skipWhitespace(bytes_, pos);
    if (!startsWithAt(bytes_, pos, "xref")) {
        throw PdfException(PdfErrorCode::UnsupportedXrefStream,
                           "startxref does not point to a classic xref table or /Type /XRef stream.");
    }
    pos += 4;

    while (true) {
        pos = skipWhitespace(bytes_, pos);
        if (startsWithAt(bytes_, pos, "trailer")) {
            pos += 7;
            break;
        }

        const std::string subsection = trim(readLine(bytes_, pos));
        if (subsection.empty()) {
            continue;
        }

        std::istringstream subsectionStream(subsection);
        std::uint32_t firstObject{};
        std::uint32_t count{};
        subsectionStream >> firstObject >> count;
        // Empty subsections (for example `0 0`) are legal and occur in
        // PDFs produced by several desktop applications.
        if (!subsectionStream) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "Malformed xref subsection header: " + subsection);
        }
        std::string subsectionExtra;
        if (subsectionStream >> subsectionExtra) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "Malformed xref subsection header: " + subsection);
        }
        if (count > readerOptions_.limits.maxObjectCount ||
            static_cast<std::uint64_t>(firstObject) + count >
                static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1ULL) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "Xref subsection exceeds configured limits.");
        }

        for (std::uint32_t i = 0; i < count; ++i) {
            const std::string line = trim(readLine(bytes_, pos));
            std::istringstream entryStream(line);
            std::uint64_t entryOffset{};
            std::uint32_t generation{};
            char state{};
            entryStream >> entryOffset >> generation >> state;
            if (!entryStream || (state != 'n' && state != 'f')) {
                throw PdfException(PdfErrorCode::MalformedXref,
                                   "Malformed xref entry: " + line);
            }
            std::string entryExtra;
            if (entryStream >> entryExtra) {
                throw PdfException(PdfErrorCode::MalformedXref,
                                   "Malformed xref entry: " + line);
            }
            if (entryOffset >= bytes_.size() && state == 'n') {
                if (readerOptions_.strictParsing || !readerOptions_.repairDamagedXref) {
                    throw PdfException(PdfErrorCode::MalformedXref,
                                       "Xref entry points outside the file: " + line);
                }
                continue;
            }

            // The PDF generation field is formally limited to uint16. In
            // practice, damaged producers occasionally write 65536 (or a
            // larger value) while keeping the object offset and state valid.
            // MuPDF-style recovery keeps the usable entry instead of
            // rejecting the entire document; strict mode still reports it.
            if (generation > std::numeric_limits<std::uint16_t>::max()) {
                if (readerOptions_.strictParsing || !readerOptions_.repairDamagedXref) {
                    throw PdfException(PdfErrorCode::MalformedXref,
                                       "Malformed xref entry: " + line);
                }
                generation = std::numeric_limits<std::uint16_t>::max();
            }

            PdfXrefEntry entry{};
            entry.type = state == 'n' ? PdfXrefEntry::Type::Uncompressed : PdfXrefEntry::Type::Free;
            entry.offset = entryOffset;
            entry.generation = static_cast<std::uint16_t>(generation);
            entry.inUse = state == 'n';
            xref_.try_emplace(firstObject + i, entry);
        }
    }

    pos = skipWhitespace(bytes_, pos);
    if (!startsWithAt(bytes_, pos, "<<")) {
        throw PdfException(PdfErrorCode::TrailerNotFound, "Trailer dictionary was not found after xref table.");
    }
    const std::size_t end = findDictionaryEnd(bytes_, pos);
    const std::string currentTrailer(bytes_.data() + pos, end - pos);

    if (trailerDictionary_.empty()) {
        trailerDictionary_ = currentTrailer;
    }

    // Hybrid-reference PDFs store compressed-object entries in an xref stream
    // referenced by /XRefStm from the classic trailer. Parse it before /Prev so
    // entries from the newest revision always win over older revisions.
    const PdfObject parsedTrailer = Internal::PdfObjectParser::Parse(
        currentTrailer, readerOptions_.limits.maxRecursionDepth);
    const PdfDictionary* trailer = asDictionary(parsedTrailer);
    if (trailer != nullptr) {
        if (const PdfObject* xrefStm = trailer->Find(PdfName("XRefStm"))) {
            if (const auto offset = xrefStm->AsInteger(); offset.has_value() && *offset >= 0) {
                parseXrefSection(static_cast<std::uint64_t>(*offset));
            }
        }
        if (const PdfObject* prev = trailer->Find(PdfName("Prev"))) {
            if (const auto previous = prev->AsInteger(); previous.has_value() && *previous >= 0) {
                const auto previousOffset = static_cast<std::uint64_t>(*previous);
                if (previousOffset >= bytes_.size()) {
                    throw PdfException(PdfErrorCode::MalformedXref,
                                       "XRef trailer /Prev points outside the file.");
                }
                parseXrefSection(previousOffset);
            } else if (prev->type() != PdfObjectType::Null) {
                throw PdfException(PdfErrorCode::MalformedXref,
                                   "XRef trailer /Prev is invalid.");
            }
        }
    }
}

std::string PdfDocument::readRawIndirectObject(std::uint32_t objectNumber) const {
    const auto iterator = xref_.find(objectNumber);
    if (iterator == xref_.end() || !iterator->second.inUse) {
        return recoverIndirectObject(objectNumber);
    }

    if (iterator->second.type == PdfXrefEntry::Type::Compressed) {
        return readCompressedObject(objectNumber, iterator->second);
    }

    std::size_t pos = static_cast<std::size_t>(iterator->second.offset);
    if (pos >= bytes_.size()) {
        throw PdfException(PdfErrorCode::MalformedObject, "Object offset points outside PDF file.");
    }

    const std::string header = std::to_string(objectNumber) + " " +
                               std::to_string(iterator->second.generation) + " obj";
    if (!startsWithAt(bytes_, pos, header)) {
        const std::size_t searchEnd = std::min(bytes_.size(), pos + 64U);
        const std::string_view nearby(bytes_.data() + static_cast<std::ptrdiff_t>(pos), searchEnd - pos);
        const auto relative = nearby.find(header);
        if (relative == std::string_view::npos) {
            throw PdfException(PdfErrorCode::MalformedObject,
                               "Object header does not match xref entry for object " +
                                   std::to_string(objectNumber) + ".");
        }
        pos += relative;
    }

    const std::string_view remaining(bytes_.data() + static_cast<std::ptrdiff_t>(pos), bytes_.size() - pos);
    const std::size_t endRelative = remaining.find("endobj");
    if (endRelative == std::string_view::npos) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "endobj was not found for object " + std::to_string(objectNumber) + ".");
    }
    const std::size_t objectBytes = endRelative + 6U;
    if (readerOptions_.limits.maxIndirectObjectBytes != 0U &&
        objectBytes > readerOptions_.limits.maxIndirectObjectBytes) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Indirect object exceeds the configured size limit.");
    }
    return std::string(remaining.substr(0, objectBytes));
}

std::string PdfDocument::readIndirectObject(const std::uint32_t objectNumber) const {
    const auto entry = xref_.find(objectNumber);
    const bool compressed = entry != xref_.end() &&
        entry->second.type == PdfXrefEntry::Type::Compressed;
    std::string object = readRawIndirectObject(objectNumber);
    if (!encryption_ || compressed ||
        (encryptionReference_ && encryptionReference_->objectNumber == objectNumber)) {
        return object;
    }
    if (!encryption_->IsAuthenticated()) {
        throw PdfException(PdfErrorCode::PasswordRequired,
                           "A valid password is required to read this encrypted PDF.");
    }
    const std::uint16_t generation = entry == xref_.end() ? 0U : entry->second.generation;
    return encryption_->DecryptObject(object, objectNumber, generation);
}


std::string PdfDocument::recoverIndirectObject(std::uint32_t objectNumber) const {
    // Recovery path for damaged, incomplete, or non-conforming xref data.
    // Search only for a syntactically valid indirect-object header, not for a
    // raw number occurrence inside a stream.
    const std::string number = std::to_string(objectNumber);
    std::size_t searchPos = 0;

    while ((searchPos = std::string_view(bytes_.data(), bytes_.size()).find(number, searchPos))
           != std::string_view::npos) {
        const bool validLeft = searchPos == 0U ||
            std::isspace(static_cast<unsigned char>(bytes_[searchPos - 1U]));
        if (!validLeft) {
            searchPos += number.size();
            continue;
        }

        std::size_t pos = searchPos + number.size();
        if (pos >= bytes_.size() || !std::isspace(static_cast<unsigned char>(bytes_[pos]))) {
            searchPos += number.size();
            continue;
        }
        pos = skipWhitespace(bytes_, pos);

        const std::size_t generationBegin = pos;
        while (pos < bytes_.size() && std::isdigit(static_cast<unsigned char>(bytes_[pos]))) {
            ++pos;
        }
        if (generationBegin == pos) {
            searchPos += number.size();
            continue;
        }
        const std::size_t generationEnd = pos;
        pos = skipWhitespace(bytes_, pos);
        if (!startsWithAt(bytes_, pos, "obj")) {
            searchPos += number.size();
            continue;
        }

        const std::string_view generationText(
            bytes_.data() + static_cast<std::ptrdiff_t>(generationBegin),
            generationEnd - generationBegin);
        const std::uint64_t generation = parseUnsigned64(generationText, "recovered object generation");
        if (generation > std::numeric_limits<std::uint16_t>::max()) {
            if (readerOptions_.strictParsing || !readerOptions_.repairDamagedXref) {
                throw PdfException(PdfErrorCode::MalformedObject,
                                   "Recovered object generation exceeds the PDF limit.");
            }
            searchPos += number.size();
            continue;
        }

        const std::string_view remaining(bytes_.data() + static_cast<std::ptrdiff_t>(searchPos),
                                         bytes_.size() - searchPos);
        const std::size_t endRelative = remaining.find("endobj");
        if (endRelative != std::string_view::npos) {
            const std::size_t objectBytes = endRelative + 6U;
            if (readerOptions_.limits.maxIndirectObjectBytes != 0U &&
                objectBytes > readerOptions_.limits.maxIndirectObjectBytes) {
                throw PdfException(PdfErrorCode::MalformedObject,
                                   "Recovered indirect object exceeds the configured size limit.");
            }
            return std::string(remaining.substr(0, objectBytes));
        }
        break;
    }

    const std::string compressed = recoverFromObjectStreams(objectNumber);
    if (!compressed.empty()) {
        return compressed;
    }

    throw PdfException(PdfErrorCode::ObjectNotFound,
                       "PDF object " + std::to_string(objectNumber) +
                       " was not found in xref data or by object recovery scan.");
}

std::string PdfDocument::recoverFromObjectStreams(std::uint32_t objectNumber) const {
    for (const auto& [streamNumber, streamEntry] : xref_) {
        if (!streamEntry.inUse || streamEntry.type != PdfXrefEntry::Type::Uncompressed) {
            continue;
        }

        std::string objectStream;
        try {
            std::size_t pos = static_cast<std::size_t>(streamEntry.offset);
            if (pos >= bytes_.size()) continue;
            const std::string header = std::to_string(streamNumber) + " " +
                                       std::to_string(streamEntry.generation) + " obj";
            if (!startsWithAt(bytes_, pos, header)) continue;
            const std::string_view remaining(bytes_.data() + static_cast<std::ptrdiff_t>(pos),
                                             bytes_.size() - pos);
            const std::size_t endRelative = remaining.find("endobj");
            if (endRelative == std::string_view::npos) continue;
            const std::size_t objectBytes = endRelative + 6U;
            if (readerOptions_.limits.maxIndirectObjectBytes != 0U &&
                objectBytes > readerOptions_.limits.maxIndirectObjectBytes) {
                continue;
            }
            objectStream = std::string(remaining.substr(0, objectBytes));
        } catch (...) {
            continue;
        }

        const PdfObject streamParsed = Internal::PdfObjectParser::Parse(
            objectStream, readerOptions_.limits.maxRecursionDepth);
        const PdfDictionary* streamDictionary = asDictionary(streamParsed);
        if (streamDictionary == nullptr) continue;
        const auto streamType = streamDictionary->GetAsName(PdfName("Type"));
        if (!streamType.has_value() || streamType->value() != "ObjStm") continue;

        try {
            const PdfObject* countObject = streamDictionary->Find(PdfName("N"));
            const PdfObject* firstObject = streamDictionary->Find(PdfName("First"));
            if (countObject == nullptr || firstObject == nullptr) continue;
            const auto count = countObject->AsInteger();
            const auto first = firstObject->AsInteger();
            if (!count.has_value() || !first.has_value() || *count < 0 || *first < 0) continue;
            if (static_cast<std::size_t>(*count) > readerOptions_.limits.maxObjectStreamObjects) continue;
            const std::size_t countValue = static_cast<std::size_t>(*count);
            const std::size_t firstValue = static_cast<std::size_t>(*first);
            std::string decoded = extractStreamData(objectStream);
            const auto filters = filterSpecsFromDictionary(*streamDictionary);
            if (!filters.empty()) {
                const auto* begin = reinterpret_cast<const std::byte*>(decoded.data());
                const auto result = PdfFilterPipeline(readerOptions_.limits.maxDecodedStreamSize)
                    .Decode(std::span<const std::byte>(begin, decoded.size()), filters);
                decoded.assign(reinterpret_cast<const char*>(result.data()), result.size());
            }
            if (firstValue > decoded.size()) continue;

            std::istringstream header(decoded.substr(0, firstValue));
            std::vector<std::pair<std::uint32_t, std::size_t>> objects;
            objects.reserve(countValue);
            for (std::size_t i = 0; i < countValue; ++i) {
                std::uint32_t numberInStream{};
                std::size_t relative{};
                if (!(header >> numberInStream >> relative)) {
                    objects.clear();
                    break;
                }
                objects.emplace_back(numberInStream, relative);
            }

            for (std::size_t i = 0; i < objects.size(); ++i) {
                if (objects[i].first != objectNumber) continue;
                const std::size_t bodyBegin = firstValue + objects[i].second;
                const std::size_t bodyEnd = i + 1U < objects.size()
                    ? firstValue + objects[i + 1U].second : decoded.size();
                if (bodyBegin > bodyEnd || bodyEnd > decoded.size()) break;
                return std::to_string(objectNumber) + " 0 obj\n" +
                       decoded.substr(bodyBegin, bodyEnd - bodyBegin) + "\nendobj";
            }
        } catch (...) {
            continue;
        }
    }
    return {};
}



std::string PdfDocument::readCompressedObject(std::uint32_t objectNumber,
                                              const PdfXrefEntry& entry) const {
    const auto objectStream = loadObjectStream(entry.objectStream);
    const auto& objects = objectStream->objects;

    std::size_t selected = objects.size();
    if (entry.objectIndex < objects.size() && objects[entry.objectIndex].first == objectNumber) {
        selected = entry.objectIndex;
    } else {
        for (std::size_t i = 0; i < objects.size(); ++i) {
            if (objects[i].first == objectNumber) { selected = i; break; }
        }
    }
    if (selected == objects.size()) {
        throw PdfException(PdfErrorCode::ObjectNotFound, "Object was not found inside /ObjStm.");
    }

    const std::size_t bodyBegin = objectStream->first + objects[selected].second;
    const std::size_t bodyEnd = selected + 1U < objects.size()
        ? objectStream->first + objects[selected + 1U].second : objectStream->decoded.size();
    if (bodyBegin > bodyEnd || bodyEnd > objectStream->decoded.size()) {
        throw PdfException(PdfErrorCode::MalformedObject, "Invalid object boundaries in /ObjStm.");
    }
    return std::to_string(objectNumber) + " 0 obj\n" +
           objectStream->decoded.substr(bodyBegin, bodyEnd - bodyBegin) + "\nendobj";
}

std::shared_ptr<const PdfDocument::DecodedObjectStream> PdfDocument::loadObjectStream(
    const std::uint32_t objectStreamNumber) const {
    if (const auto cached = objectStreamCache_.find(objectStreamNumber);
        cached != objectStreamCache_.end()) {
        ++objectStreamCacheHits_;
        objectStreamRecency_.splice(
            objectStreamRecency_.begin(), objectStreamRecency_, cached->second.recency);
        return cached->second.stream;
    }
    ++objectStreamCacheMisses_;

    const std::string objectStream = readIndirectObject(objectStreamNumber);
    const PdfObject streamParsed = Internal::PdfObjectParser::Parse(
        objectStream, readerOptions_.limits.maxRecursionDepth);
    const PdfDictionary* streamDictionary = asDictionary(streamParsed);
    const auto streamType = streamDictionary
        ? streamDictionary->GetAsName(PdfName("Type")) : std::nullopt;
    if (!streamType.has_value() || streamType->value() != "ObjStm") {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Compressed object references a stream that is not /Type /ObjStm.");
    }
    const PdfObject* countObject = streamDictionary->Find(PdfName("N"));
    const PdfObject* firstObject = streamDictionary->Find(PdfName("First"));
    if (countObject == nullptr || firstObject == nullptr) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Object stream /N or /First is missing.");
    }
    const auto count = countObject->AsInteger();
    const auto first = firstObject->AsInteger();
    if (!count.has_value() || !first.has_value() || *count < 0 || *first < 0) {
        throw PdfException(PdfErrorCode::MalformedObject, "Object stream /N or /First is invalid.");
    }
    if (static_cast<std::size_t>(*count) > readerOptions_.limits.maxObjectStreamObjects) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Object stream exceeds configured object count limit.");
    }
    const std::size_t countValue = static_cast<std::size_t>(*count);
    const std::size_t firstValue = static_cast<std::size_t>(*first);
    std::string decoded = extractStreamData(objectStream);
    const auto filters = filterSpecsFromDictionary(*streamDictionary);
    if (!filters.empty()) {
        const auto* begin = reinterpret_cast<const std::byte*>(decoded.data());
        const auto result = PdfFilterPipeline(readerOptions_.limits.maxDecodedStreamSize)
            .Decode(std::span<const std::byte>(begin, decoded.size()), filters);
        decoded.assign(reinterpret_cast<const char*>(result.data()), result.size());
    }
    if (firstValue > decoded.size()) {
        throw PdfException(PdfErrorCode::MalformedObject, "/First points outside object stream.");
    }

    auto result = std::make_shared<DecodedObjectStream>();
    result->decoded = std::move(decoded);
    result->first = firstValue;
    result->objects.reserve(countValue);
    const std::string_view header(result->decoded.data(), firstValue);
    std::size_t position = 0U;
    auto parseUnsigned = [&header, &position](auto& value) {
        while (position < header.size() &&
               std::isspace(static_cast<unsigned char>(header[position]))) {
            ++position;
        }
        const char* begin = header.data() + static_cast<std::ptrdiff_t>(position);
        const char* end = header.data() + static_cast<std::ptrdiff_t>(header.size());
        const auto parsed = std::from_chars(begin, end, value);
        if (parsed.ec != std::errc{} || parsed.ptr == begin) return false;
        position = static_cast<std::size_t>(parsed.ptr - header.data());
        return true;
    };
    for (std::size_t i = 0; i < countValue; ++i) {
        std::uint32_t number{};
        std::size_t relative{};
        if (!parseUnsigned(number) || !parseUnsigned(relative)) {
            throw PdfException(PdfErrorCode::MalformedObject, "Malformed /ObjStm object index.");
        }
        if ((!result->objects.empty() && relative < result->objects.back().second) ||
            relative > result->decoded.size() - firstValue) {
            throw PdfException(PdfErrorCode::MalformedObject,
                               "Invalid or non-monotonic offsets in /ObjStm index.");
        }
        result->objects.emplace_back(number, relative);
    }

    const auto& limits = readerOptions_.limits;
    const std::size_t decodedBytes = result->decoded.size();
    if (limits.maxCachedObjectStreams == 0U ||
        limits.maxCachedObjectStreamBytes == 0U ||
        decodedBytes > limits.maxCachedObjectStreamBytes) {
        return result;
    }

    while (!objectStreamRecency_.empty() &&
           (objectStreamCache_.size() >= limits.maxCachedObjectStreams ||
            cachedObjectStreamBytes_ > limits.maxCachedObjectStreamBytes - decodedBytes)) {
        const std::uint32_t victim = objectStreamRecency_.back();
        const auto victimEntry = objectStreamCache_.find(victim);
        if (victimEntry != objectStreamCache_.end()) {
            cachedObjectStreamBytes_ -= victimEntry->second.stream->decoded.size();
            objectStreamCache_.erase(victimEntry);
        }
        objectStreamRecency_.pop_back();
    }

    objectStreamRecency_.push_front(objectStreamNumber);
    objectStreamCache_.emplace(
        objectStreamNumber, ObjectStreamCacheEntry{result, objectStreamRecency_.begin()});
    cachedObjectStreamBytes_ += decodedBytes;
    return result;
}

std::vector<std::uint32_t> PdfDocument::objectNumbers() const {
    std::vector<std::uint32_t> result;
    result.reserve(xref_.size());
    for (const auto& [objectNumber, entry] : xref_) {
        if (entry.inUse) {
            result.push_back(objectNumber);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<PdfXrefEntry> PdfDocument::GetXrefEntry(const std::uint32_t objectNumber) const {
    const auto found = xref_.find(objectNumber);
    if (found == xref_.end()) return std::nullopt;
    return found->second;
}

PdfReference PdfDocument::parseReferenceAfterKey(const std::string& dictionary,
                                                 const std::string& key,
                                                 std::size_t maxDepth) {
    const PdfObject parsed = Internal::PdfObjectParser::Parse(dictionary, maxDepth);
    const PdfDictionary* dict = asDictionary(parsed);
    if (dict == nullptr) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Reference /" + key + " was not found.");
    }
    const PdfObject* value = dict->Find(PdfName(key));
    const auto reference = value ? value->AsReference() : std::nullopt;
    if (!reference.has_value()) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Reference /" + key + " was not found.");
    }
    return PdfReference{reference->first, reference->second};
}

std::vector<PdfReference> PdfDocument::parseReferenceArrayAfterKey(const std::string& dictionary,
                                                                   const std::string& key,
                                                                   std::size_t maxDepth) {
    const PdfObject parsed = Internal::PdfObjectParser::Parse(dictionary, maxDepth);
    const PdfDictionary* dict = asDictionary(parsed);
    std::vector<PdfReference> references;
    if (dict == nullptr) return references;

    const PdfObject* value = dict->Find(PdfName(key));
    if (value == nullptr) return references;
    if (const PdfArray* array = value->AsArray()) {
        references.reserve(array->size());
        for (const PdfObject& item : array->values()) {
            if (const auto reference = item.AsReference()) {
                references.push_back(PdfReference{reference->first, reference->second});
            }
        }
        return references;
    }
    // Accept a single reference in place of an array (common for /Contents).
    if (const auto single = value->AsReference()) {
        references.push_back(PdfReference{single->first, single->second});
    }
    return references;
}

std::string PdfDocument::parseNameAfterKey(const std::string& dictionary,
                                           const std::string& key,
                                           std::size_t maxDepth) {
    const PdfObject parsed = Internal::PdfObjectParser::Parse(dictionary, maxDepth);
    const PdfDictionary* dict = asDictionary(parsed);
    if (dict == nullptr) return {};
    const auto name = dict->GetAsName(PdfName(key));
    return name ? name->value() : std::string{};
}

std::string PdfDocument::parseStringAfterKey(const std::string& dictionary,
                                             const std::string& key,
                                             std::size_t maxDepth) {
    const PdfObject parsed = Internal::PdfObjectParser::Parse(dictionary, maxDepth);
    const PdfDictionary* dict = asDictionary(parsed);
    if (dict == nullptr) return {};
    const PdfObject* value = dict->Find(PdfName(key));
    const std::string* text = value ? value->AsString() : nullptr;
    return text ? *text : std::string{};
}

PdfObject PdfDocument::findInheritedPageValue(const std::string& pageObject,
                                              const std::string& key,
                                              std::size_t maxDepth) const {
    std::unordered_set<std::uint32_t> visited;
    std::string current = pageObject;
    for (;;) {
        const PdfObject parsed = Internal::PdfObjectParser::Parse(current, maxDepth);
        const PdfDictionary* dict = asDictionary(parsed);
        if (dict == nullptr) return {};
        if (const PdfObject* value = dict->Find(PdfName(key))) {
            return *value;
        }
        const PdfObject* parent = dict->Find(PdfName("Parent"));
        const auto parentReference = parent ? parent->AsReference() : std::nullopt;
        if (!parentReference.has_value()) return {};
        if (!visited.insert(parentReference->first).second) {
            throw PdfException(PdfErrorCode::InvalidPageTree,
                               "Cycle detected while resolving inherited page attributes.");
        }
        current = readIndirectObject(parentReference->first);
    }
}

std::size_t PdfDocument::parseIntegerAfterKey(const std::string& dictionary,
                                              const std::string& key,
                                              std::size_t maxDepth) {
    const PdfObject parsed = Internal::PdfObjectParser::Parse(dictionary, maxDepth);
    const PdfDictionary* dict = asDictionary(parsed);
    if (dict == nullptr) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Integer /" + key + " was not found.");
    }
    const PdfObject* value = dict->Find(PdfName(key));
    const auto integer = value ? value->AsInteger() : std::nullopt;
    if (!integer.has_value()) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Integer /" + key + " was not found.");
    }
    return static_cast<std::size_t>(*integer);
}

PdfReference PdfDocument::findRootReference() const {
    return parseReferenceAfterKey(
        trailerDictionary_, "Root", readerOptions_.limits.maxRecursionDepth);
}

PdfReference PdfDocument::GetCatalogReference() const {
    return findRootReference();
}

std::optional<PdfReference> PdfDocument::GetTrailerReference(const PdfName& key) const {
    const PdfObject parsed = Internal::PdfObjectParser::Parse(
        trailerDictionary_, readerOptions_.limits.maxRecursionDepth);
    const PdfDictionary* dict = asDictionary(parsed);
    const PdfObject* value = dict ? dict->Find(key) : nullptr;
    const auto reference = value ? value->AsReference() : std::nullopt;
    if (!reference.has_value()) return std::nullopt;
    return PdfReference{reference->first, reference->second};
}

PdfReference PdfDocument::findPagesReference(const std::string& catalogObject) const {
    return parseReferenceAfterKey(
        catalogObject, "Pages", readerOptions_.limits.maxRecursionDepth);
}

std::size_t PdfDocument::countPagesFromNode(const PdfReference& reference,
                                            std::unordered_map<std::uint32_t, bool>& visiting) const {
    if (visiting[reference.objectNumber]) {
        throw PdfException(PdfErrorCode::InvalidPageTree, "Cycle detected in PDF page tree.");
    }
    visiting[reference.objectNumber] = true;

    const std::string object = readIndirectObject(reference.objectNumber);
    const std::string type = parseNameAfterKey(
        object, "Type", readerOptions_.limits.maxRecursionDepth);
    if (type == "Page") {
        visiting[reference.objectNumber] = false;
        return 1;
    }
    if (type != "Pages") {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page tree object has unsupported /Type /" + type + ".");
    }

    const auto kids = parseReferenceArrayAfterKey(
        object, "Kids", readerOptions_.limits.maxRecursionDepth);
    if (kids.empty()) {
        const auto count = parseIntegerAfterKey(
            object, "Count", readerOptions_.limits.maxRecursionDepth);
        visiting[reference.objectNumber] = false;
        return count;
    }

    std::size_t total = 0;
    for (const auto& kid : kids) {
        total += countPagesFromNode(kid, visiting);
    }
    visiting[reference.objectNumber] = false;
    return total;
}


PdfDocumentInfo PdfDocument::documentInfo() const {
    PdfDocumentInfo info;
    const PdfObject trailerObject = Internal::PdfObjectParser::Parse(
        trailerDictionary_, readerOptions_.limits.maxRecursionDepth);
    const PdfDictionary* trailer = asDictionary(trailerObject);
    if (trailer == nullptr) return info;
    const PdfObject* infoObject = trailer->Find(PdfName("Info"));
    const auto infoReference = infoObject ? infoObject->AsReference() : std::nullopt;
    if (!infoReference.has_value()) return info;

    const auto objectNumber = infoReference->first;
    const std::string object = readIndirectObject(objectNumber);
    info.title = parseStringAfterKey(
        object, "Title", readerOptions_.limits.maxRecursionDepth);
    info.author = parseStringAfterKey(
        object, "Author", readerOptions_.limits.maxRecursionDepth);
    info.subject = parseStringAfterKey(
        object, "Subject", readerOptions_.limits.maxRecursionDepth);
    info.keywords = parseStringAfterKey(
        object, "Keywords", readerOptions_.limits.maxRecursionDepth);
    info.creator = parseStringAfterKey(
        object, "Creator", readerOptions_.limits.maxRecursionDepth);
    info.producer = parseStringAfterKey(
        object, "Producer", readerOptions_.limits.maxRecursionDepth);
    info.creationDate = parseStringAfterKey(
        object, "CreationDate", readerOptions_.limits.maxRecursionDepth);
    info.modificationDate = parseStringAfterKey(
        object, "ModDate", readerOptions_.limits.maxRecursionDepth);
    return info;
}

PdfPageInfo PdfDocument::pageInfo(const std::size_t pageIndex) const {
    const auto& pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree, "Page index is out of range.");
    }

    const PdfReference reference = pages[pageIndex];
    const std::string pageObject = readIndirectObject(reference.objectNumber);
    auto parseBox = [&](const std::string& key, const PdfRectangle& fallback) {
        const PdfObject inherited = findInheritedPageValue(
            pageObject, key, readerOptions_.limits.maxRecursionDepth);
        if (inherited.IsNull()) return fallback;
        const PdfArray* array = inherited.AsArray();
        if (array == nullptr) return fallback;
        std::vector<double> values;
        values.reserve(array->size());
        for (const PdfObject& item : array->values()) {
            if (const auto integer = item.AsInteger()) values.push_back(static_cast<double>(*integer));
            else if (const auto real = item.AsReal()) values.push_back(*real);
        }
        if (values.size() < 4U) return fallback;
        return PdfRectangle{values[0], values[1], values[2], values[3]};
    };

    const PdfRectangle defaultLetter{0.0, 0.0, 612.0, 792.0};
    const PdfRectangle mediaBox = parseBox("MediaBox", defaultLetter);
    const PdfRectangle cropBox = parseBox("CropBox", mediaBox);

    int rotation = 0;
    const PdfObject inheritedRotation = findInheritedPageValue(
        pageObject, "Rotate", readerOptions_.limits.maxRecursionDepth);
    const auto rotationValue = inheritedRotation.AsInteger();
    if (rotationValue.has_value()) {
        rotation = static_cast<int>(*rotationValue % 360);
        if (rotation < 0) rotation += 360;
    }

    return PdfPageInfo{pageIndex, reference.objectNumber, mediaBox, cropBox, rotation};
}

std::size_t PdfDocument::pageCount() const {
    return pageReferences().size();
}


void PdfDocument::collectPageReferences(const PdfReference& reference,
                                        std::unordered_map<std::uint32_t, bool>& visiting,
                                        std::vector<PdfReference>& pages) const {
    if (visiting[reference.objectNumber]) {
        throw PdfException(PdfErrorCode::InvalidPageTree, "Cycle detected in PDF page tree.");
    }
    visiting[reference.objectNumber] = true;
    const std::string object = readIndirectObject(reference.objectNumber);
    const std::string type = parseNameAfterKey(
        object, "Type", readerOptions_.limits.maxRecursionDepth);
    if (type == "Page") {
        if (pages.size() >= readerOptions_.limits.maxPageCount) {
            throw PdfException(PdfErrorCode::InvalidPageTree,
                               "PDF page count exceeds configured limit.");
        }
        pages.push_back(reference);
        visiting[reference.objectNumber] = false;
        return;
    }
    if (type != "Pages") {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page tree object has unsupported /Type /" + type + ".");
    }
    for (const auto& kid : parseReferenceArrayAfterKey(
             object, "Kids", readerOptions_.limits.maxRecursionDepth)) {
        collectPageReferences(kid, visiting, pages);
    }
    visiting[reference.objectNumber] = false;
}

const std::vector<PdfReference>& PdfDocument::pageReferences() const {
    if (!pageReferencesCached_) {
        const auto root = findRootReference();
        const auto catalog = readIndirectObject(root.objectNumber);
        const auto pagesRoot = findPagesReference(catalog);
        std::unordered_map<std::uint32_t, bool> visiting;
        pageReferencesCache_.clear();
        collectPageReferences(pagesRoot, visiting, pageReferencesCache_);
        pageReferencesCached_ = true;
    }
    return pageReferencesCache_;
}

std::vector<PdfReference> PdfDocument::contentReferences(const std::string& pageObject) const {
    return parseReferenceArrayAfterKey(
        pageObject, "Contents", readerOptions_.limits.maxRecursionDepth);
}

std::string PdfDocument::decodeContentStream(const std::string& streamObject) const {
    const std::string_view encoded = extractStreamDataView(streamObject);
    const PdfObject parsed = Internal::PdfObjectParser::Parse(
        streamObject, readerOptions_.limits.maxRecursionDepth);
    const PdfDictionary* dictionary = asDictionary(parsed);
    const auto filters = dictionary ? filterSpecsFromDictionary(*dictionary)
                                    : std::vector<PdfFilterSpec>{};
    if (filters.empty()) {
        if (dictionary != nullptr && dictionary->Contains(PdfName("Filter"))) {
            throw PdfException(PdfErrorCode::UnsupportedFeature,
                               "The content stream uses an unsupported filter.");
        }
        return std::string(encoded);
    }
    const auto* begin = reinterpret_cast<const std::byte*>(encoded.data());
    const auto decoded = PdfFilterPipeline(readerOptions_.limits.maxDecodedStreamSize)
        .Decode(std::span<const std::byte>(begin, encoded.size()), filters);
    return std::string(reinterpret_cast<const char*>(decoded.data()), decoded.size());
}

std::string PdfDocument::decodeContentStreamReference(const PdfReference& reference) const {
    const std::uint64_t key = (static_cast<std::uint64_t>(reference.objectNumber) << 16U) |
                              reference.generation;
    if (const auto cached = contentStreamCache_.find(key); cached != contentStreamCache_.end()) {
        ++contentStreamCacheHits_;
        const auto position = std::find(contentStreamRecency_.begin(), contentStreamRecency_.end(), key);
        if (position != contentStreamRecency_.end()) {
            contentStreamRecency_.splice(contentStreamRecency_.begin(), contentStreamRecency_, position);
        }
        return *cached->second;
    }
    ++contentStreamCacheMisses_;
    const std::string decoded = decodeContentStream(readIndirectObject(reference.objectNumber));
    const auto& limits = readerOptions_.limits;
    if (limits.maxCachedContentStreams == 0U || limits.maxCachedContentStreamBytes == 0U ||
        decoded.size() > limits.maxCachedContentStreamBytes) {
        return decoded;
    }
    while (!contentStreamRecency_.empty() &&
           (contentStreamCache_.size() >= limits.maxCachedContentStreams ||
            cachedContentStreamBytes_ > limits.maxCachedContentStreamBytes - decoded.size())) {
        const std::uint64_t victim = contentStreamRecency_.back();
        contentStreamRecency_.pop_back();
        const auto victimEntry = contentStreamCache_.find(victim);
        if (victimEntry != contentStreamCache_.end()) {
            cachedContentStreamBytes_ -= victimEntry->second->size();
            contentStreamCache_.erase(victimEntry);
        }
    }
    auto stored = std::make_shared<const std::string>(decoded);
    contentStreamRecency_.push_front(key);
    contentStreamCache_.insert_or_assign(key, stored);
    cachedContentStreamBytes_ += stored->size();
    return *stored;
}

std::string PdfDocument::extractTextOperators(
    const std::string& contentText,
    const PdfTextExtractionOptions& options) {
    const std::string_view content(contentText);
    std::vector<std::string> operands;
    std::string output;
    std::size_t pos = 0;

    double textX = 0.0;
    double textY = 0.0;
    double lineStartX = 0.0;
    double lineStartY = 0.0;
    double leading = 0.0;
    double fontSize = 12.0;
    bool inTextObject = false;
    bool hasOutputPosition = false;
    double outputX = 0.0;
    double outputY = 0.0;

    auto parseNumber = [](const std::string& value, double fallback = 0.0) {
        try {
            std::size_t consumed = 0;
            const double number = std::stod(value, &consumed);
            return consumed == value.size() ? number : fallback;
        } catch (...) {
            return fallback;
        }
    };

    auto appendNewline = [&]() {
        while (!output.empty() && output.back() == ' ') output.pop_back();
        if (!output.empty() && output.back() != '\n') output.push_back('\n');
        hasOutputPosition = false;
    };

    auto appendUtf8 = [](std::string& target, std::uint32_t codePoint) {
        if (codePoint <= 0x7FU) {
            target.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FFU) {
            target.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
            target.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        } else {
            target.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
            target.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            target.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
    };

    auto normalizeSimpleFontText = [&](const std::string& raw) {
        static constexpr std::uint16_t cp1252[32] = {
            0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
            0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
        };
        std::string normalized;
        normalized.reserve(raw.size() + 8U);
        for (const unsigned char value : raw) {
            if (value == 0x1EU) {
                normalized += "fi";
            } else if (value == 0x1FU) {
                normalized += "fl";
            } else if (value < 0x80U) {
                if (value == '\t' || value == '\n' || value == '\r' || value >= 0x20U) {
                    normalized.push_back(static_cast<char>(value));
                }
            } else if (value < 0xA0U) {
                appendUtf8(normalized, cp1252[value - 0x80U]);
            } else {
                appendUtf8(normalized, value);
            }
        }
        return normalized;
    };

    auto appendTextAtCurrentPosition = [&](const std::string& rawText) {
        const std::string text = normalizeSimpleFontText(rawText);
        if (text.empty()) return;

        if (options.preserveLayout && hasOutputPosition) {
            const double deltaY = std::abs(textY - outputY);
            if (deltaY > options.lineTolerance) {
                appendNewline();
            } else if (options.insertSpaces) {
                const double gap = textX - outputX;
                const double threshold = std::max(options.wordGapThreshold, fontSize * 0.20);
                if (gap > threshold && !output.empty() && output.back() != ' ' && output.back() != '\n') {
                    output.push_back(' ');
                }
            }
        }

        output += text;
        const double averageAdvance = std::max(1.0, fontSize * 0.50);
        textX += averageAdvance * static_cast<double>(text.size());
        outputX = textX;
        outputY = textY;
        hasOutputPosition = true;
    };

    while ((pos = skipContentWhitespace(content, pos)) < content.size()) {
        if (content[pos] == '%') {
            while (pos < content.size() && content[pos] != '\r' && content[pos] != '\n') ++pos;
            continue;
        }

        if (content[pos] == '(' || (content[pos] == '<' &&
            (pos + 1 >= content.size() || content[pos + 1] != '<'))) {
            operands.push_back(parseContentString(content, pos));
            continue;
        }

        if (content[pos] == '[') {
            ++pos;
            std::string arrayText;
            while ((pos = skipContentWhitespace(content, pos)) < content.size() && content[pos] != ']') {
                if (content[pos] == '(' || (content[pos] == '<' &&
                    (pos + 1 >= content.size() || content[pos + 1] != '<'))) {
                    arrayText += parseContentString(content, pos);
                } else {
                    const std::size_t numberBegin = pos;
                    while (pos < content.size() &&
                           !std::isspace(static_cast<unsigned char>(content[pos])) &&
                           content[pos] != ']' && content[pos] != '(' && content[pos] != '<') {
                        ++pos;
                    }
                    if (options.insertSpaces && pos > numberBegin) {
                        const std::string numberToken(content.substr(numberBegin, pos - numberBegin));
                        const double adjustment = parseNumber(numberToken, 0.0);
                        // A sufficiently negative TJ adjustment usually denotes a word gap.
                        if (adjustment < -120.0 && !arrayText.empty() && arrayText.back() != ' ') {
                            arrayText.push_back(' ');
                        }
                    }
                }
            }
            if (pos < content.size() && content[pos] == ']') ++pos;
            operands.push_back(std::move(arrayText));
            continue;
        }

        const std::size_t begin = pos;
        while (pos < content.size() && !std::isspace(static_cast<unsigned char>(content[pos])) &&
               content[pos] != '(' && content[pos] != '[' && content[pos] != '<') {
            ++pos;
        }

        if (pos == begin) {
            if (content[pos] == '<' && pos + 1 < content.size() && content[pos + 1] == '<') {
                pos += 2;
            } else if (content[pos] == '>' && pos + 1 < content.size() && content[pos + 1] == '>') {
                pos += 2;
            } else {
                ++pos;
            }
            operands.clear();
            continue;
        }

        const std::string token(content.substr(begin, pos - begin));

        if (token == "BT") {
            inTextObject = true;
            textX = textY = lineStartX = lineStartY = 0.0;
            operands.clear();
        } else if (token == "ET") {
            inTextObject = false;
            operands.clear();
        } else if (token == "Tf") {
            if (!operands.empty()) fontSize = std::max(1.0, parseNumber(operands.back(), fontSize));
            operands.clear();
        } else if (token == "Tm") {
            if (operands.size() >= 6) {
                textX = parseNumber(operands[operands.size() - 2]);
                textY = parseNumber(operands[operands.size() - 1]);
                lineStartX = textX;
                lineStartY = textY;
            }
            operands.clear();
        } else if (token == "Td" || token == "TD") {
            if (operands.size() >= 2) {
                const double tx = parseNumber(operands[operands.size() - 2]);
                const double ty = parseNumber(operands[operands.size() - 1]);
                lineStartX += tx;
                lineStartY += ty;
                textX = lineStartX;
                textY = lineStartY;
                if (token == "TD") leading = -ty;
            }
            operands.clear();
        } else if (token == "TL") {
            if (!operands.empty()) leading = parseNumber(operands.back(), leading);
            operands.clear();
        } else if (token == "T*") {
            lineStartY -= leading;
            textX = lineStartX;
            textY = lineStartY;
            if (!options.preserveLayout) appendNewline();
            operands.clear();
        } else if (token == "Tj" || token == "TJ") {
            if (inTextObject && !operands.empty()) appendTextAtCurrentPosition(operands.back());
            operands.clear();
        } else if (token == "'") {
            lineStartY -= leading;
            textX = lineStartX;
            textY = lineStartY;
            if (!operands.empty()) appendTextAtCurrentPosition(operands.back());
            operands.clear();
        } else if (token == "\"") {
            lineStartY -= leading;
            textX = lineStartX;
            textY = lineStartY;
            if (!operands.empty()) appendTextAtCurrentPosition(operands.back());
            operands.clear();
        } else if (!token.empty()) {
            operands.push_back(token);
            if (operands.size() > 24U) operands.erase(operands.begin(), operands.end() - 24);
        }
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' ')) {
        output.pop_back();
    }
    return output;
}

std::string PdfDocument::extractPageTextFromReference(
    const PdfReference& pageReference,
    const PdfTextExtractionOptions& options) const {
    const std::string pageObject = readIndirectObject(pageReference.objectNumber);
    std::vector<PdfTextChunk> chunks;
    PdfTextExtractionRequest request;
    request.strategy = options.preserveLayout
        ? PdfTextExtractionStrategy::Location
        : PdfTextExtractionStrategy::Simple;
    request.options = options;
    const auto* resources = inheritedPageResources(*this, pageReference);
    std::unordered_set<std::uint64_t> activeForms;
    const StreamDecoder decoder = [this](const PdfReference& reference) {
        return decodeContentStreamReference(reference);
    };
    const std::string content = joinDecodedStreams(contentReferences(pageObject), decoder);
    if (!content.empty()) {
        extractContentRecursively(
            *this, content, resources, request, decoder, activeForms, 0U, chunks);
    }
    return PdfTextExtractor::BuildText(chunks, request);
}

const PdfObject& PdfDocument::GetObject(const PdfReference& reference) const {
    if (!objectResolver_) {
        objectResolver_ = std::make_unique<Internal::PdfObjectResolver>(readerOptions_.limits);
    }
    return objectResolver_->Resolve(
        reference,
        [this](const std::uint32_t objectNumber) {
            return readIndirectObject(objectNumber);
        });
}

std::size_t PdfDocument::GetCachedObjectCount() const noexcept {
    return objectResolver_ ? objectResolver_->CachedObjectCount() : 0U;
}

std::size_t PdfDocument::GetObjectCacheCapacity() const noexcept {
    return readerOptions_.limits.maxCachedObjects;
}

std::size_t PdfDocument::GetCachedObjectStreamCount() const noexcept {
    return objectStreamCache_.size();
}

std::size_t PdfDocument::GetCachedObjectStreamBytes() const noexcept {
    return cachedObjectStreamBytes_;
}

std::size_t PdfDocument::GetObjectStreamCacheHits() const noexcept {
    return objectStreamCacheHits_;
}

std::size_t PdfDocument::GetObjectStreamCacheMisses() const noexcept {
    return objectStreamCacheMisses_;
}

std::shared_ptr<const PdfFontResource> PdfDocument::GetCachedFontResource(
    const PdfReference reference) const {
    const std::uint64_t key = (static_cast<std::uint64_t>(reference.objectNumber) << 16U) |
                              reference.generation;
    if (const auto cached = fontResourceCache_.find(key); cached != fontResourceCache_.end()) {
        ++fontResourceCacheHits_;
        const auto position = std::find(fontResourceRecency_.begin(), fontResourceRecency_.end(), key);
        if (position != fontResourceRecency_.end()) {
            fontResourceRecency_.splice(fontResourceRecency_.begin(), fontResourceRecency_, position);
        }
        return cached->second;
    }
    ++fontResourceCacheMisses_;
    const auto* dictionary = GetObject(reference).AsDictionary();
    if (dictionary == nullptr) return {};
    const PdfFontResource::Resolver resolver = [this](const PdfReference& child) -> const PdfObject& {
        return GetObject(child);
    };
    auto resource = std::make_shared<PdfFontResource>(PdfFontResource::Create(*dictionary, resolver));
    const std::size_t capacity = readerOptions_.limits.maxCachedFontResources;
    if (capacity == 0U) return resource;
    while (fontResourceCache_.size() >= capacity && !fontResourceRecency_.empty()) {
        const std::uint64_t victim = fontResourceRecency_.back();
        fontResourceRecency_.pop_back();
        fontResourceCache_.erase(victim);
    }
    fontResourceRecency_.push_front(key);
    fontResourceCache_.insert_or_assign(key, std::move(resource));
    return fontResourceCache_.at(key);
}

std::size_t PdfDocument::GetCachedFontResourceCount() const noexcept {
    return fontResourceCache_.size();
}

std::size_t PdfDocument::GetFontResourceCacheHits() const noexcept {
    return fontResourceCacheHits_;
}

std::size_t PdfDocument::GetFontResourceCacheMisses() const noexcept {
    return fontResourceCacheMisses_;
}

std::size_t PdfDocument::GetCachedContentStreamCount() const noexcept {
    return contentStreamCache_.size();
}

std::size_t PdfDocument::GetCachedContentStreamBytes() const noexcept {
    return cachedContentStreamBytes_;
}

std::size_t PdfDocument::GetContentStreamCacheHits() const noexcept {
    return contentStreamCacheHits_;
}

std::size_t PdfDocument::GetContentStreamCacheMisses() const noexcept {
    return contentStreamCacheMisses_;
}

void PdfDocument::ClearObjectCache() const noexcept {
    if (objectResolver_) {
        objectResolver_->Clear();
    }
    objectStreamCache_.clear();
    objectStreamRecency_.clear();
    cachedObjectStreamBytes_ = 0U;
    fontResourceCache_.clear();
    fontResourceRecency_.clear();
    contentStreamCache_.clear();
    contentStreamRecency_.clear();
    cachedContentStreamBytes_ = 0U;
}

PdfPage PdfDocument::GetPage(const std::size_t pageIndex) const {
    const auto& refs = pageReferences();
    if (pageIndex >= refs.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree, "Page index is out of range.");
    }
    const std::string pageObject = readIndirectObject(refs[pageIndex].objectNumber);
    const PdfObject resources = findInheritedPageValue(
        pageObject, "Resources", readerOptions_.limits.maxRecursionDepth);
    std::string resourcesDictionary;
    if (!resources.IsNull()) {
        std::ostringstream output;
        Internal::PdfObjectSerializer::WriteObject(output, resources);
        resourcesDictionary = output.str();
    }
    std::vector<std::string> streams;
    for (const auto& ref : contentReferences(pageObject)) {
        try {
            streams.push_back(decodeContentStreamReference(ref));
        } catch (const PdfException&) {
            // A damaged optional content stream must not make the complete
            // page unrenderable. Keep the stream slot empty so valid streams
            // on the same page can still be painted.
            streams.emplace_back();
        }
    }
    return PdfPage(pageInfo(pageIndex), std::move(resourcesDictionary), std::move(streams));
}

PdfReference PdfDocument::GetPageReference(const std::size_t pageIndex) const {
    const auto& refs = pageReferences();
    if (pageIndex >= refs.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree, "Page index is out of range.");
    }
    return refs[pageIndex];
}

std::string PdfDocument::extractPageText(std::size_t pageIndex) const {
    const auto& pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page index " + std::to_string(pageIndex) + " is outside the document.");
    }
    return extractPageTextFromReference(pages[pageIndex], {});
}

PdfTextPage PdfDocument::ExtractTextPage(
    std::size_t pageIndex,
    const PdfTextExtractionOptions& options) const {
    const auto& pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page index " + std::to_string(pageIndex) + " is outside the document.");
    }
    return PdfTextPage{pageIndex, extractPageTextFromReference(pages[pageIndex], options)};
}


std::vector<PdfTextChunk> PdfDocument::ExtractTextChunks(
    const std::size_t pageIndex,
    const PdfTextExtractionRequest& inputRequest) const {
    auto request = inputRequest;
    request.pageIndex = pageIndex;
    const auto& pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page index " + std::to_string(pageIndex) + " is outside the document.");
    }
    const std::string pageObject = readIndirectObject(pages[pageIndex].objectNumber);
    const auto* resources = inheritedPageResources(*this, pages[pageIndex]);
    std::unordered_map<std::string, std::shared_ptr<const PdfFontResource>> resolvedFonts;
    if (!request.fontResolver) {
        request.fontResolver = [this, pageIndex, &resolvedFonts](const std::uint32_t resourceObjectNumber,
                                                                  std::string_view name) -> const PdfFontResource* {
            const std::string key = std::to_string(resourceObjectNumber) + ":" + std::string(name);
            const auto found = resolvedFonts.find(key);
            if (found != resolvedFonts.end()) return found->second.get();
            auto font = ResolveFont(pageIndex, resourceObjectNumber, name);
            const auto* result = font.get();
            resolvedFonts.emplace(key, std::move(font));
            return result;
        };
    }
    if (!request.extGStateResolver) {
        request.extGStateResolver = [this, pageIndex](const std::uint32_t resourceObjectNumber,
                                                       std::string_view name) {
            return ResolveExtGStateAlpha(pageIndex, resourceObjectNumber, name);
        };
    }
    std::vector<PdfTextChunk> chunks;
    std::unordered_set<std::uint64_t> activeForms;
    const StreamDecoder decoder = [this](const PdfReference& reference) {
        return decodeContentStreamReference(reference);
    };
    const std::string content = joinDecodedStreams(contentReferences(pageObject), decoder);
    if (!content.empty()) {
        extractContentRecursively(
            *this, content, resources, request, decoder, activeForms, 0U, chunks);
    }
    return chunks;
}

std::shared_ptr<const PdfFontResource> PdfDocument::ResolvePageFont(
    const std::size_t pageIndex, const std::string_view resourceName) const {
    const auto& pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page index " + std::to_string(pageIndex) + " is outside the document.");
    }
    const auto* resources = inheritedPageResources(*this, pages[pageIndex]);
    if (!resources) return {};
    const PdfObject* fontsObject = resources->Find(PdfName("Font"));
    if (fontsObject && fontsObject->AsReference()) {
        const auto reference = fontsObject->AsReference();
        fontsObject = &GetObject({reference->first, reference->second});
    }
    const auto* fonts = fontsObject ? fontsObject->AsDictionary() : nullptr;
    if (!fonts) return {};
    const PdfObject* fontObject = fonts->Find(PdfName(std::string(resourceName)));
    if (fontObject && fontObject->AsReference()) {
        const auto reference = fontObject->AsReference();
        fontObject = &GetObject({reference->first, reference->second});
    }
    const auto* dictionary = fontObject ? fontObject->AsDictionary() : nullptr;
    if (!dictionary) return {};
    const PdfFontResource::Resolver resolver = [this](const PdfReference& reference) -> const PdfObject& {
        return GetObject(reference);
    };
    return std::make_shared<const PdfFontResource>(PdfFontResource::Create(*dictionary, resolver));
}

std::shared_ptr<const PdfFontResource> PdfDocument::ResolveFont(
    const std::size_t pageIndex, const std::uint32_t resourceObjectNumber,
    const std::string_view resourceName) const {
    if (resourceObjectNumber == 0U) return ResolvePageFont(pageIndex, resourceName);
    const auto* form = objectDictionary(*this, &GetObject({resourceObjectNumber, 0U}));
    const auto* resources = form ? objectDictionary(*this, form->Find(PdfName("Resources"))) : nullptr;
    const auto* fonts = resources ? objectDictionary(*this, resources->Find(PdfName("Font"))) : nullptr;
    const auto* fontObject = fonts ? fonts->Find(PdfName(std::string(resourceName))) : nullptr;
    const auto* dictionary = fontObject ? objectDictionary(*this, fontObject) : nullptr;
    if (!dictionary) return ResolvePageFont(pageIndex, resourceName);
    const PdfFontResource::Resolver resolver = [this](const PdfReference& reference) -> const PdfObject& {
        return GetObject(reference);
    };
    return std::make_shared<const PdfFontResource>(PdfFontResource::Create(*dictionary, resolver));
}

std::array<double, 2> PdfDocument::ResolvePageExtGStateAlpha(
    const std::size_t pageIndex, const std::string_view resourceName) const {
    const auto& pages = pageReferences();
    if (pageIndex >= pages.size()) return {1.0, 1.0};
    const auto* resources = inheritedPageResources(*this, pages[pageIndex]);
    const auto* states = resources ? objectDictionary(*this, resources->Find(PdfName("ExtGState"))) : nullptr;
    const auto* entry = states ? states->Find(PdfName(std::string(resourceName))) : nullptr;
    const auto* state = entry ? objectDictionary(*this, entry) : nullptr;
    if (!state) return {1.0, 1.0};
    const auto read = [&](const char* key) {
        const auto* value = state->Find(PdfName(key));
        if (!value) return 1.0;
        if (const auto real = value->AsReal()) return std::clamp(*real, 0.0, 1.0);
        if (const auto integer = value->AsInteger()) return std::clamp(static_cast<double>(*integer), 0.0, 1.0);
        return 1.0;
    };
    return {read("CA"), read("ca")};
}

std::array<double, 2> PdfDocument::ResolveExtGStateAlpha(
    const std::size_t pageIndex, const std::uint32_t resourceObjectNumber,
    const std::string_view resourceName) const {
    if (resourceObjectNumber == 0U) return ResolvePageExtGStateAlpha(pageIndex, resourceName);
    const auto* form = objectDictionary(*this, &GetObject({resourceObjectNumber, 0U}));
    if (form == nullptr) return ResolvePageExtGStateAlpha(pageIndex, resourceName);
    const auto* resources = objectDictionary(*this, form->Find(PdfName("Resources")));
    if (resources == nullptr) return ResolvePageExtGStateAlpha(pageIndex, resourceName);
    const auto* states = objectDictionary(*this, resources->Find(PdfName("ExtGState")));
    const auto* entry = states ? states->Find(PdfName(std::string(resourceName))) : nullptr;
    const auto* state = entry ? objectDictionary(*this, entry) : nullptr;
    if (state == nullptr) return ResolvePageExtGStateAlpha(pageIndex, resourceName);
    const auto read = [&](const char* key) {
        const auto* value = state->Find(PdfName(key));
        return value && value->AsReal().has_value()
            ? std::clamp(*value->AsReal(), 0.0, 1.0) : 1.0;
    };
    return {read("CA"), read("ca")};
}

std::pair<std::array<double, 2>, PdfBlendMode> PdfDocument::ResolveExtGState(
    const std::size_t pageIndex, const std::uint32_t resourceObjectNumber,
    const std::string_view resourceName) const {
    const auto alpha = ResolveExtGStateAlpha(pageIndex, resourceObjectNumber, resourceName);
    const auto* form = resourceObjectNumber == 0U ? nullptr : objectDictionary(*this, &GetObject({resourceObjectNumber, 0U}));
    const auto* resources = form ? objectDictionary(*this, form->Find(PdfName("Resources"))) : nullptr;
    const auto* states = resources ? objectDictionary(*this, resources->Find(PdfName("ExtGState"))) : nullptr;
    const auto& page = GetPage(pageIndex);
    PdfObject parsed;
    if (resourceObjectNumber == 0U && !page.GetResourcesDictionary().empty())
        parsed = Internal::PdfObjectParser::Parse(page.GetResourcesDictionary(), 256U);
    const auto* pageResources = parsed.AsDictionary();
    if (!resources) resources = pageResources;
    if (!states) states = resources ? objectDictionary(*this, resources->Find(PdfName("ExtGState"))) : nullptr;
    const auto* entry = states ? states->Find(PdfName(std::string(resourceName))) : nullptr;
    const auto* state = entry ? objectDictionary(*this, entry) : nullptr;
    const auto blend = state ? state->GetAsName(PdfName("BM")) : std::nullopt;
    if (!blend) return {alpha, PdfBlendMode::SourceOver};
    if (blend->value() == "Multiply") return {alpha, PdfBlendMode::Multiply};
    if (blend->value() == "Screen") return {alpha, PdfBlendMode::Screen};
    if (blend->value() == "Darken") return {alpha, PdfBlendMode::Darken};
    if (blend->value() == "Lighten") return {alpha, PdfBlendMode::Lighten};
    if (blend->value() == "Overlay") return {alpha, PdfBlendMode::Overlay};
    if (blend->value() == "Difference") return {alpha, PdfBlendMode::Difference};
    if (blend->value() == "Exclusion") return {alpha, PdfBlendMode::Exclusion};
    return {alpha, PdfBlendMode::SourceOver};
}

std::pair<bool, bool> PdfDocument::ResolveTransparencyFlags(
    const std::size_t pageIndex, const std::uint32_t resourceObjectNumber,
    const std::string_view resourceName) const {
    const auto* form = resourceObjectNumber == 0U ? nullptr : objectDictionary(*this, &GetObject({resourceObjectNumber, 0U}));
    const auto* resources = form ? objectDictionary(*this, form->Find(PdfName("Resources"))) : nullptr;
    const auto page = GetPage(pageIndex);
    PdfObject parsed;
    if (!resources && !page.GetResourcesDictionary().empty()) parsed = Internal::PdfObjectParser::Parse(page.GetResourcesDictionary(), 256U);
    if (!resources) resources = parsed.AsDictionary();
    const auto* states = resources ? objectDictionary(*this, resources->Find(PdfName("ExtGState"))) : nullptr;
    const auto* entry = states ? states->Find(PdfName(std::string(resourceName))) : nullptr;
    const auto* state = entry ? objectDictionary(*this, entry) : nullptr;
    if (!state) return {false, false};
    const auto read = [&](const char* key) {
        const auto* value = state->Find(PdfName(key));
        return value && value->AsBoolean().value_or(false);
    };
    return {read("I"), read("K")};
}

std::optional<PdfDictionary> PdfDocument::ResolveShading(
    const std::size_t pageIndex, const std::uint32_t resourceObjectNumber,
    const std::string_view resourceName) const {
    const auto* form = resourceObjectNumber == 0U ? nullptr :
        objectDictionary(*this, &GetObject({resourceObjectNumber, 0U}));
    const auto* resources = form ? objectDictionary(*this, form->Find(PdfName("Resources"))) : nullptr;
    if (resources == nullptr) resources = inheritedPageResources(*this, pageReferences().at(pageIndex));
    const auto* shadings = resources ? objectDictionary(*this, resources->Find(PdfName("Shading"))) : nullptr;
    const auto* entry = shadings ? shadings->Find(PdfName(std::string(resourceName))) : nullptr;
    if (entry == nullptr) return std::nullopt;
    if (const auto reference = entry->AsReference()) {
        const auto& resolved = GetObject({reference->first, reference->second});
        if (const auto* stream = resolved.AsStream()) return stream->dictionary();
        if (const auto* dictionary = resolved.AsDictionary()) return *dictionary;
        return std::nullopt;
    }
    if (const auto* dictionary = entry->AsDictionary()) return *dictionary;
    return std::nullopt;
}

std::optional<PdfResolvedShading> PdfDocument::ResolveAxialShading(
    const std::size_t pageIndex, const std::uint32_t resourceObjectNumber,
    const std::string_view resourceName) const {
    const auto dictionary = ResolveShading(pageIndex, resourceObjectNumber, resourceName);
    if (!dictionary) return std::nullopt;
    const auto type = dictionary->Find(PdfName("ShadingType"));
    if (type == nullptr || type->AsInteger().value_or(0) != 2) return std::nullopt;
    const auto* coords = dictionary->GetAsArray(PdfName("Coords"));
    const auto* functionObject = dictionary->Find(PdfName("Function"));
    if (coords == nullptr || coords->size() < 4U || functionObject == nullptr) return std::nullopt;
    const PdfObject* functionValue = functionObject;
    if (const auto reference = functionValue->AsReference()) {
        functionValue = &GetObject({reference->first, reference->second});
    }
    const auto* function = functionValue->AsDictionary();
    if (function == nullptr || function->Find(PdfName("FunctionType")) == nullptr ||
        function->Find(PdfName("FunctionType"))->AsInteger().value_or(0) != 2) return std::nullopt;
    const auto* c0 = function->GetAsArray(PdfName("C0"));
    const auto* c1 = function->GetAsArray(PdfName("C1"));
    const auto* exponent = function->Find(PdfName("N"));
    if (c0 == nullptr || c1 == nullptr || c0->size() != c1->size() || c0->empty() || exponent == nullptr) return std::nullopt;
    std::array<double, 4> coordinates{};
    for (std::size_t index = 0; index < coordinates.size(); ++index) coordinates[index] = objectNumberValue(coords->at(index), 0.0);
    std::vector<double> c0Values, c1Values;
    for (std::size_t index = 0; index < c0->size(); ++index) {
        c0Values.push_back(objectNumberValue(c0->at(index), 0.0));
        c1Values.push_back(objectNumberValue(c1->at(index), 1.0));
    }
    PdfResolvedShading result;
    result.type = 2U;
    result.axial.coordinates = coordinates;
    result.axial.function.emplace(std::move(c0Values), std::move(c1Values), objectNumberValue(*exponent, 1.0));
    if (const auto* domain = dictionary->GetAsArray(PdfName("Domain")); domain && domain->size() >= 2U) {
        result.axial.domain = {objectNumberValue(domain->at(0), 0.0), objectNumberValue(domain->at(1), 1.0)};
    }
    if (const auto* extend = dictionary->GetAsArray(PdfName("Extend")); extend && extend->size() >= 2U) {
        result.axial.extendStart = extend->at(0).AsBoolean().value_or(false);
        result.axial.extendEnd = extend->at(1).AsBoolean().value_or(false);
    }
    return result;
}

std::optional<PdfResolvedShading> PdfDocument::ResolveRadialShading(
    const std::size_t pageIndex, const std::uint32_t resourceObjectNumber,
    const std::string_view resourceName) const {
    const auto dictionary = ResolveShading(pageIndex, resourceObjectNumber, resourceName);
    if (!dictionary) return std::nullopt;
    const auto type = dictionary->Find(PdfName("ShadingType"));
    const auto* coordinates = dictionary->GetAsArray(PdfName("Coords"));
    const auto* functionObject = dictionary->Find(PdfName("Function"));
    if (type == nullptr || type->AsInteger().value_or(0) != 3 || coordinates == nullptr ||
        coordinates->size() < 6U || functionObject == nullptr) return std::nullopt;
    const PdfObject* functionValue = functionObject;
    if (const auto reference = functionValue->AsReference()) functionValue = &GetObject({reference->first, reference->second});
    const auto* function = functionValue->AsDictionary();
    if (function == nullptr || function->Find(PdfName("FunctionType")) == nullptr ||
        function->Find(PdfName("FunctionType"))->AsInteger().value_or(0) != 2) return std::nullopt;
    const auto* c0 = function->GetAsArray(PdfName("C0"));
    const auto* c1 = function->GetAsArray(PdfName("C1"));
    const auto* exponent = function->Find(PdfName("N"));
    if (c0 == nullptr || c1 == nullptr || c0->size() != c1->size() || c0->empty() || exponent == nullptr) return std::nullopt;
    std::vector<double> c0Values, c1Values;
    for (std::size_t index = 0; index < c0->size(); ++index) {
        c0Values.push_back(objectNumberValue(c0->at(index), 0.0));
        c1Values.push_back(objectNumberValue(c1->at(index), 1.0));
    }
    PdfResolvedShading result;
    result.type = 3U;
    for (std::size_t index = 0; index < 6U; ++index) result.radial.coordinates[index] = objectNumberValue(coordinates->at(index), 0.0);
    result.radial.function.emplace(std::move(c0Values), std::move(c1Values), objectNumberValue(*exponent, 1.0));
    if (const auto* domain = dictionary->GetAsArray(PdfName("Domain")); domain && domain->size() >= 2U)
        result.radial.domain = {objectNumberValue(domain->at(0), 0.0), objectNumberValue(domain->at(1), 1.0)};
    if (const auto* extend = dictionary->GetAsArray(PdfName("Extend")); extend && extend->size() >= 2U) {
        result.radial.extendStart = extend->at(0).AsBoolean().value_or(false);
        result.radial.extendEnd = extend->at(1).AsBoolean().value_or(false);
    }
    return result;
}

std::optional<PdfResolvedPattern> PdfDocument::ResolveTilingPattern(
    const std::size_t pageIndex, const std::uint32_t resourceObjectNumber,
    const std::string_view resourceName) const {
    const auto* form = resourceObjectNumber == 0U ? nullptr :
        objectDictionary(*this, &GetObject({resourceObjectNumber, 0U}));
    const auto* resources = form ? objectDictionary(*this, form->Find(PdfName("Resources"))) : nullptr;
    if (resources == nullptr) resources = inheritedPageResources(*this, pageReferences().at(pageIndex));
    const auto* patterns = resources ? objectDictionary(*this, resources->Find(PdfName("Pattern"))) : nullptr;
    const auto* entry = patterns ? patterns->Find(PdfName(std::string(resourceName))) : nullptr;
    if (entry == nullptr) return std::nullopt;
    const auto* patternDictionary = [&]() -> const PdfDictionary* {
        if (const auto reference = entry->AsReference()) {
            return objectDictionary(*this, &GetObject({reference->first, reference->second}));
        }
        return entry->AsDictionary();
    }();
    if (patternDictionary == nullptr) return std::nullopt;
    const auto patternType = patternDictionary->Find(PdfName("PatternType")) ?
        patternDictionary->Find(PdfName("PatternType"))->AsInteger().value_or(0) : 0U;
    if (patternType != 1U) return std::nullopt; // Coloring patterns unsupported.
    PdfResolvedPattern result;
    result.patternType = 1U;
    const auto* bbox = patternDictionary->GetAsArray(PdfName("BBox"));
    if (!bbox || bbox->size() < 4U) return std::nullopt;
    result.tiling.boundingBox = PdfRectangle{
        objectNumberValue(bbox->at(0U), 0.0), objectNumberValue(bbox->at(1U), 0.0),
        objectNumberValue(bbox->at(2U), 0.0), objectNumberValue(bbox->at(3U), 0.0)};
    result.tiling.xStep = objectNumberValue(patternDictionary->Find(PdfName("XStep"))
        ? *patternDictionary->Find(PdfName("XStep")) : PdfObject(1.0), 1.0);
    result.tiling.yStep = objectNumberValue(patternDictionary->Find(PdfName("YStep"))
        ? *patternDictionary->Find(PdfName("YStep")) : PdfObject(1.0), 1.0);
    result.tiling.paintType = patternDictionary->Find(PdfName("PaintType")) ?
        patternDictionary->Find(PdfName("PaintType"))->AsInteger().value_or(1) : 1U;
    result.tiling.tilingType = patternDictionary->Find(PdfName("TilingType")) ?
        patternDictionary->Find(PdfName("TilingType"))->AsInteger().value_or(1) : 1U;
    if (const auto* matrix = patternDictionary->GetAsArray(PdfName("Matrix")); matrix && matrix->size() >= 6U) {
        for (std::size_t index = 0U; index < 6U; ++index) {
            result.tiling.matrix[index] = objectNumberValue(matrix->at(index), index == 0U || index == 3U ? 1.0 : 0.0);
        }
    }
    if (const PdfObject* resourcesObject = patternDictionary->Find(PdfName("Resources"))) {
        if (const auto reference = resourcesObject->AsReference()) {
            result.tiling.resourceObjectNumber = reference->first;
            const PdfObject& resolved = GetObject({reference->first, reference->second});
            if (const PdfStream* stream = resolved.AsStream()) {
                result.tiling.resourcesDictionary.assign(
                    reinterpret_cast<const char*>(stream->bytes().data()), stream->bytes().size());
            } else if (const PdfDictionary* dictionary = resolved.AsDictionary()) {
                std::ostringstream buffer;
                Internal::PdfObjectSerializer::WriteDictionary(buffer, *dictionary);
                result.tiling.resourcesDictionary = buffer.str();
            }
        } else if (const PdfDictionary* dictionary = resourcesObject->AsDictionary()) {
            std::ostringstream buffer;
            Internal::PdfObjectSerializer::WriteDictionary(buffer, *dictionary);
            result.tiling.resourcesDictionary = buffer.str();
        }
    }
    const PdfStream* patternStream = [&]() -> const PdfStream* {
        if (const auto reference = entry->AsReference()) {
            return GetObject({reference->first, reference->second}).AsStream();
        }
        return entry->AsStream();
    }();
    if (patternStream == nullptr) return std::nullopt;
    const auto& bytes = patternStream->bytes();
    result.tiling.content.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return result;
}

void PdfDocument::ForEachPageContentEvent(
    const std::size_t pageIndex,
    const PdfContentEventHandler& handler) const {
    const auto& pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page index " + std::to_string(pageIndex) + " is outside the document.");
    }

    const std::string pageObject = readIndirectObject(pages[pageIndex].objectNumber);
    const auto* resources = inheritedPageResources(*this, pages[pageIndex]);
    const StreamDecoder decoder = [this](const PdfReference& reference) {
        return decodeContentStreamReference(reference);
    };
    const std::string content = joinDecodedStreams(contentReferences(pageObject), decoder);
    std::unordered_set<std::uint64_t> activeForms;
    processPageContentRecursively(*this, content, resources, {}, decoder,
                                  activeForms, 0U, 0U, handler);
}

PdfDisplayList PdfDocument::BuildPageDisplayList(const std::size_t pageIndex) const {
    PdfDisplayList list;
    ForEachPageContentEvent(pageIndex, [&](const PdfContentEvent& event) { list.Add(event); });
    list.SetImageResolver([this, pageIndex](const std::uint32_t resourceObjectNumber,
                                            const std::string_view resourceName,
                                            const PdfContentEvent& event)
        -> std::optional<PdfExtractedImage> {
        if (event.type == PdfContentEventType::RenderInlineImage) {
            PdfDictionary dictionary;
            for (const auto& property : event.inlineImageDictionary) {
                dictionary.Put(PdfName(property.name), PdfObject(property.value));
            }
            PdfStream stream(std::move(dictionary), event.bytes);
            PdfImageExtractionOptions options;
            options.keepEncodedBytes = false;
            options.decodeSupportedFilters = true;
            auto image = makeExtractedImage(*this, stream, {}, {},
                                            event.textState.currentTransformationMatrix,
                                            options, readerOptions_.limits.maxDecodedStreamSize);
            image.info.inlineImage = true;
            image.info.fillAlpha = event.textState.fillAlpha;
            image.info.strokeAlpha = event.textState.strokeAlpha;
            return image;
        }
        const auto page = GetPage(pageIndex);
        const auto resourcesText = page.GetResourcesDictionary();
        if (resourcesText.empty()) return std::nullopt;
        const auto resourcesObject = Internal::PdfObjectParser::Parse(resourcesText, 256U);
        const auto* resources = resourcesObject.AsDictionary();
        if (!resources) return std::nullopt;
        const auto* xObjects = objectDictionary(*this, resources->Find(PdfName("XObject")));
        if (!xObjects) return std::nullopt;
        const auto* entry = xObjects->Find(PdfName(std::string(resourceName)));
        if (!entry) return std::nullopt;
        PdfReference reference{};
        const PdfStream* stream{};
        if (const auto indirect = entry->AsReference()) {
            reference = {indirect->first, indirect->second};
            stream = GetObject(reference).AsStream();
        } else stream = entry->AsStream();
        if (!stream || !stream->dictionary().GetAsName(PdfName("Subtype")) ||
            stream->dictionary().GetAsName(PdfName("Subtype"))->value() != "Image") return std::nullopt;
        PdfImageExtractionOptions options;
        options.keepEncodedBytes = false;
        options.decodeSupportedFilters = true;
        auto image = makeExtractedImage(*this, *stream, reference, std::string(resourceName),
                                        event.textState.currentTransformationMatrix,
                                        options, readerOptions_.limits.maxDecodedStreamSize);
        image.info.fillAlpha = event.textState.fillAlpha;
        image.info.strokeAlpha = event.textState.strokeAlpha;
        image.info.sourceObjectNumber = resourceObjectNumber;
        return image;
    });
    return list;
}

std::string PdfDocument::ExtractText(
    const std::size_t pageIndex,
    const PdfTextExtractionRequest& request) const {
    return PdfTextExtractor::BuildText(ExtractTextChunks(pageIndex, request), request);
}

std::vector<PdfExtractedImage> PdfDocument::ExtractImages(
    const std::size_t pageIndex,
    const PdfImageExtractionOptions& options) const {
    const auto& pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page index " + std::to_string(pageIndex) + " is outside the document.");
    }
    const std::string pageObject = readIndirectObject(pages[pageIndex].objectNumber);
    const auto* resources = inheritedPageResources(*this, pages[pageIndex]);
    std::vector<PdfExtractedImage> images;
    std::unordered_set<std::uint64_t> activeForms;
    const StreamDecoder decoder = [this](const PdfReference& reference) {
        return decodeContentStreamReference(reference);
    };
    const std::array<double, 6> identity{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    const std::string content = joinDecodedStreams(contentReferences(pageObject), decoder);
    if (!content.empty()) {
        extractImagesRecursively(*this, content, resources, identity,
            options, decoder, activeForms, 0U, images);
    }
    return images;
}

std::vector<std::string> PdfDocument::extractAllPageText() const {
    const auto& pages = pageReferences();
    std::vector<std::string> result;
    result.reserve(pages.size());
    for (const auto& page : pages) {
        result.push_back(extractPageTextFromReference(page, {}));
    }
    return result;
}

std::vector<std::string> PdfDocument::ExtractAllPageTextParallel(
    std::size_t maxConcurrency) const {
    const std::size_t count = GetPageCount();
    if (count == 0U) return {};

    // Independent PdfDocument instances avoid sharing mutable resolver/cache
    // state across workers. Memory/stream-backed documents have no reopenable
    // path, so preserve correctness with the sequential implementation.
    if (path_.empty() || count == 1U) return extractAllPageText();

    if (maxConcurrency == 0U) {
        maxConcurrency = static_cast<std::size_t>(std::thread::hardware_concurrency());
        if (maxConcurrency == 0U) maxConcurrency = 2U;
    }
    const std::size_t workerCount = std::max<std::size_t>(
        1U, std::min(maxConcurrency, count));

    std::vector<std::string> result(count);
    std::atomic_size_t nextPage{0U};
    std::atomic_bool failed{false};
    std::exception_ptr firstFailure;
    std::mutex failureMutex;
    std::vector<std::future<void>> workers;
    workers.reserve(workerCount);

    for (std::size_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back(std::async(std::launch::async, [&, options = readerOptions_]() {
            try {
                PdfDocument local = PdfDocument::Open(path_, options);
                while (!failed.load(std::memory_order_relaxed)) {
                    const std::size_t page = nextPage.fetch_add(1U, std::memory_order_relaxed);
                    if (page >= count) break;
                    result[page] = local.extractPageText(page);
                }
            } catch (...) {
                failed.store(true, std::memory_order_relaxed);
                std::lock_guard lock(failureMutex);
                if (!firstFailure) firstFailure = std::current_exception();
            }
        }));
    }
    for (auto& worker : workers) worker.get();
    if (firstFailure) std::rethrow_exception(firstFailure);
    return result;
}

std::vector<PdfOutlineEntry> PdfDocument::GetOutlines() const {
    std::vector<PdfOutlineEntry> result;
    const auto catalogRef = GetTrailerReference(PdfName("Root"));
    if (!catalogRef) return result;
    const auto& catalog = GetObject(*catalogRef);
    const auto* catalogDict = catalog.AsDictionary();
    if (!catalogDict) return result;
    const auto outlinesValue = catalogDict->Find(PdfName("Outlines"));
    const auto outlinesRef = outlinesValue ? outlinesValue->AsReference() : std::nullopt;
    if (!outlinesRef) return result;
    const auto& outlines = GetObject(PdfReference{outlinesRef->first, outlinesRef->second});
    const auto* outlinesDict = outlines.AsDictionary();
    if (!outlinesDict) return result;
    const auto* firstValue = outlinesDict->Find(PdfName("First"));
    const auto firstRef = firstValue ? firstValue->AsReference() : std::nullopt;
    if (!firstRef) return result;
    // DFS through First/Next with a visited set and depth map to support nesting
    // while guarding against cyclic trees.
    std::unordered_map<std::uint32_t, std::size_t> depthOf;
    std::unordered_set<std::uint32_t> visited;
    std::function<void(const PdfReference&, std::size_t)> walk =
        [&](const PdfReference& item, const std::size_t depth) {
        if (!visited.insert(item.objectNumber).second) return;
        depthOf[item.objectNumber] = depth;
        const auto& obj = GetObject(item);
        const auto* dict = obj.AsDictionary();
        if (!dict) return;
        PdfOutlineEntry entry;
        if (const auto* title = dict->Find(PdfName("Title"))->AsString()) entry.title = *title;
        entry.objectNumber = item.objectNumber;
        entry.depth = depth;
        entry.isOpen = true;
        if (const auto* dest = dict->Find(PdfName("Dest"))) {
            if (const auto target = ResolveDestination(*dest)) entry.destinationPageIndex = target;
        }
        if (const auto* action = dict->Find(PdfName("A"))) {
            if (const auto* actionDict = action->AsDictionary()) {
                if (const auto* dest = actionDict->Find(PdfName("D"))) {
                    if (const auto target = ResolveDestination(*dest)) entry.destinationPageIndex = target;
                }
            }
        }
        result.push_back(std::move(entry));
        const auto* firstChildValue = dict->Find(PdfName("First"));
        const auto firstChildRef = firstChildValue ? firstChildValue->AsReference() : std::nullopt;
        if (firstChildRef) {
            walk(PdfReference{firstChildRef->first, firstChildRef->second}, depth + 1U);
        }
        const auto* nextValue = dict->Find(PdfName("Next"));
        const auto nextRef = nextValue ? nextValue->AsReference() : std::nullopt;
        if (nextRef) {
            const auto found = depthOf.find(nextRef->first);
            if (found == depthOf.end() || found->second == depth) {
                walk(PdfReference{nextRef->first, nextRef->second}, depth);
            }
        }
    };
    walk(PdfReference{firstRef->first, firstRef->second}, 0U);
    return result;
}

std::optional<std::size_t> PdfDocument::ResolveDestination(const PdfObject& destination) const {
    // Dest is either an array [page /Fit ...] or a name reference.
    const auto resolvePageIndex = [&](const PdfReference& page) {
        const auto& pages = pageReferences();
        for (std::size_t i = 0; i < pages.size(); ++i) {
            if (pages[i].objectNumber == page.objectNumber) return std::optional<std::size_t>(i);
        }
        return std::optional<std::size_t>();
    };
    if (const auto* array = destination.AsArray()) {
        if (!array->empty()) {
            const auto ref = array->at(0U).AsReference();
            if (ref) return resolvePageIndex(PdfReference{ref->first, ref->second});
        }
    }
    const auto ref = destination.AsReference();
    if (ref) {
        return resolvePageIndex(PdfReference{ref->first, ref->second});
    }
    return std::nullopt;
}

} // namespace CPPPdf
