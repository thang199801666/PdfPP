#include "CPPPdf/PdfDocument.hpp"
#include "CPPPdf/PdfError.hpp"
#include "CPPPdf/PdfPage.hpp"
#include "Internal/Document/PdfObjectResolver.hpp"
#include "CPPPdf/Filters/PdfFilterPipeline.hpp"
#include "CPPPdf/Fonts/PdfFontResource.hpp"
#include "CPPPdf/Graphics/PdfImage.hpp"
#include "CPPPdf/Content/PdfContentProcessor.hpp"

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

[[nodiscard]] bool startsWithAt(const std::vector<char>& bytes,
                                std::size_t offset,
                                std::string_view text) {
    return offset <= bytes.size() && text.size() <= bytes.size() - offset &&
           std::equal(text.begin(), text.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] std::size_t skipWhitespace(const std::vector<char>& bytes, std::size_t pos) {
    while (pos < bytes.size()) {
        const unsigned char ch = static_cast<unsigned char>(bytes[pos]);
        if (!std::isspace(ch) && ch != 0) {
            break;
        }
        ++pos;
    }
    return pos;
}

[[nodiscard]] std::string readLine(const std::vector<char>& bytes, std::size_t& pos) {
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

[[nodiscard]] std::size_t findDictionaryEnd(const std::vector<char>& bytes, std::size_t begin) {
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
        case 'r': result.push_back('\r'); break;
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

[[nodiscard]] std::string inflateZlib(std::string_view input) {
    if (input.empty()) return {};

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
        output.append(buffer.data(), produced);
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END) {
        throw PdfException(PdfErrorCode::MalformedObject, "Invalid or unsupported FlateDecode stream.");
    }
    return output;
}


[[nodiscard]] int parseOptionalIntegerAfterKey(const std::string& dictionary,
                                                const std::string& key,
                                                int defaultValue) {
    const std::regex expression("/" + key + R"(\s+(\d+))");
    std::smatch match;
    if (!std::regex_search(dictionary, match, expression)) return defaultValue;
    return std::stoi(match[1].str());
}

[[nodiscard]] unsigned char paethPredictor(unsigned char left,
                                           unsigned char above,
                                           unsigned char upperLeft) {
    const int p = static_cast<int>(left) + static_cast<int>(above) - static_cast<int>(upperLeft);
    const int pa = std::abs(p - static_cast<int>(left));
    const int pb = std::abs(p - static_cast<int>(above));
    const int pc = std::abs(p - static_cast<int>(upperLeft));
    if (pa <= pb && pa <= pc) return left;
    if (pb <= pc) return above;
    return upperLeft;
}

[[nodiscard]] std::string applyFlateDecodeParms(const std::string& dictionary,
                                                std::string decoded) {
    const int predictor = parseOptionalIntegerAfterKey(dictionary, "Predictor", 1);
    if (predictor <= 1) return decoded;

    const int colors = parseOptionalIntegerAfterKey(dictionary, "Colors", 1);
    const int bitsPerComponent = parseOptionalIntegerAfterKey(dictionary, "BitsPerComponent", 8);
    const int columns = parseOptionalIntegerAfterKey(dictionary, "Columns", 1);
    if (colors <= 0 || columns <= 0 || bitsPerComponent <= 0) {
        throw PdfException(PdfErrorCode::MalformedXref, "Invalid FlateDecode predictor parameters.");
    }

    const std::size_t rowBytes =
        (static_cast<std::size_t>(colors) * static_cast<std::size_t>(columns) *
             static_cast<std::size_t>(bitsPerComponent) + 7U) / 8U;
    const std::size_t bytesPerPixel = std::max<std::size_t>(
        1U, (static_cast<std::size_t>(colors) * static_cast<std::size_t>(bitsPerComponent) + 7U) / 8U);

    if (predictor == 2) {
        if (rowBytes == 0U || decoded.size() % rowBytes != 0U) {
            throw PdfException(PdfErrorCode::MalformedXref, "TIFF predictor data has an invalid row length.");
        }
        for (std::size_t row = 0; row < decoded.size(); row += rowBytes) {
            for (std::size_t i = bytesPerPixel; i < rowBytes; ++i) {
                decoded[row + i] = static_cast<char>(
                    static_cast<unsigned char>(decoded[row + i]) +
                    static_cast<unsigned char>(decoded[row + i - bytesPerPixel]));
            }
        }
        return decoded;
    }

    if (predictor < 10 || predictor > 15) {
        throw PdfException(PdfErrorCode::MalformedXref, "Unsupported FlateDecode predictor.");
    }

    const std::size_t encodedRowBytes = rowBytes + 1U;
    if (rowBytes == 0U || decoded.size() % encodedRowBytes != 0U) {
        throw PdfException(PdfErrorCode::MalformedXref, "PNG predictor data has an invalid row length.");
    }

    const std::size_t rowCount = decoded.size() / encodedRowBytes;
    std::string output(rowCount * rowBytes, '\0');
    for (std::size_t row = 0; row < rowCount; ++row) {
        const auto filter = static_cast<unsigned char>(decoded[row * encodedRowBytes]);
        const auto* source = reinterpret_cast<const unsigned char*>(
            decoded.data() + row * encodedRowBytes + 1U);
        auto* target = reinterpret_cast<unsigned char*>(output.data() + row * rowBytes);
        const auto* previous = row == 0U ? nullptr :
            reinterpret_cast<const unsigned char*>(output.data() + (row - 1U) * rowBytes);

        for (std::size_t i = 0; i < rowBytes; ++i) {
            const unsigned char left = i >= bytesPerPixel ? target[i - bytesPerPixel] : 0U;
            const unsigned char above = previous ? previous[i] : 0U;
            const unsigned char upperLeft =
                previous && i >= bytesPerPixel ? previous[i - bytesPerPixel] : 0U;
            switch (filter) {
            case 0: target[i] = source[i]; break;
            case 1: target[i] = static_cast<unsigned char>(source[i] + left); break;
            case 2: target[i] = static_cast<unsigned char>(source[i] + above); break;
            case 3:
                target[i] = static_cast<unsigned char>(
                    source[i] + (static_cast<unsigned int>(left) +
                                 static_cast<unsigned int>(above)) / 2U);
                break;
            case 4:
                target[i] = static_cast<unsigned char>(
                    source[i] + paethPredictor(left, above, upperLeft));
                break;
            default:
                throw PdfException(PdfErrorCode::MalformedXref,
                                   "Unsupported PNG predictor row filter.");
            }
        }
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
            fonts->insert_or_assign(
                resourceName.value(),
                PdfFontResource::Create(*dictionary, resolver));
        } catch (const std::exception&) {
            // Keep extraction alive when one font resource is malformed.
        }
    }
    return fonts;
}


void attachPageFontResolver(
    PdfTextExtractionRequest& request,
    const std::shared_ptr<PageFontMap>& fonts) {
    request.fontResolver = [fonts](const std::string_view resourceName)
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
            output.push_back(makeExtractedImage(
                document, *stream, reference, event.text,
                event.textState.currentTransformationMatrix,
                options, document.readerOptions().limits.maxDecodedStreamSize));
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


PdfDocument::~PdfDocument() = default;
PdfDocument::PdfDocument(PdfDocument&&) noexcept = default;
PdfDocument& PdfDocument::operator=(PdfDocument&&) noexcept = default;

PdfDocument PdfDocument::Open(const std::filesystem::path& path) {
    return Open(path, PdfReaderOptions{});
}

PdfDocument PdfDocument::Open(const std::filesystem::path& path, const PdfReaderOptions& options) {
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
    document.bytes_ = source->ReadAll();
    document.parse();
    if (document.xref_.size() > options.limits.maxObjectCount) {
        throw PdfException(PdfErrorCode::MalformedXref, "PDF object count exceeds configured limit.");
    }
    return document;
}

void PdfDocument::parse() {
    parseHeader();
    parseXrefSection(findStartXref());
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
        return;
    }

    std::size_t pos = skipWhitespace(bytes_, static_cast<std::size_t>(offset));
    if (startsWithAt(bytes_, pos, "xref")) {
        parseClassicXref(offset);
    } else {
        parseXrefStream(offset);
    }
}

std::vector<std::size_t> PdfDocument::parseIntegerArrayAfterKey(
    const std::string& dictionary, const std::string& key) {
    const std::regex expression("/" + key + R"(\s*\[([^\]]*)\])");
    std::smatch match;
    if (!std::regex_search(dictionary, match, expression)) return {};
    std::vector<std::size_t> values;
    std::istringstream stream(match[1].str());
    std::size_t value{};
    while (stream >> value) values.push_back(value);
    return values;
}

std::string PdfDocument::extractStreamData(const std::string& streamObject) {
    const std::size_t streamKeyword = streamObject.find("stream");
    if (streamKeyword == std::string::npos) return {};
    std::size_t dataBegin = streamKeyword + 6U;
    if (dataBegin < streamObject.size() && streamObject[dataBegin] == '\r') ++dataBegin;
    if (dataBegin < streamObject.size() && streamObject[dataBegin] == '\n') ++dataBegin;

    std::size_t dataEnd = std::string::npos;
    try {
        const std::size_t length = parseIntegerAfterKey(streamObject, "Length");
        if (dataBegin + length <= streamObject.size()) dataEnd = dataBegin + length;
    } catch (...) {
    }
    if (dataEnd == std::string::npos) dataEnd = streamObject.rfind("endstream");
    if (dataEnd == std::string::npos || dataEnd < dataBegin) {
        throw PdfException(PdfErrorCode::MalformedObject, "Stream has no valid endstream marker.");
    }
    return streamObject.substr(dataBegin, dataEnd - dataBegin);
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
    if (parseNameAfterKey(object, "Type") != "XRef") {
        throw PdfException(PdfErrorCode::UnsupportedXrefStream,
                           "startxref does not point to a classic xref table or /Type /XRef stream.");
    }

    const auto widths = parseIntegerArrayAfterKey(object, "W");
    if (widths.size() != 3U) {
        throw PdfException(PdfErrorCode::MalformedXref, "XRef stream /W must contain three integers.");
    }

    std::vector<std::size_t> index = parseIntegerArrayAfterKey(object, "Index");
    if (index.empty()) index = {0U, parseIntegerAfterKey(object, "Size")};
    if ((index.size() % 2U) != 0U) {
        throw PdfException(PdfErrorCode::MalformedXref, "XRef stream /Index must contain pairs.");
    }

    std::string decoded = extractStreamData(object);
    if (object.find("/FlateDecode") != std::string::npos) {
        decoded = applyFlateDecodeParms(object, inflateZlib(decoded));
    }
    else if (object.find("/Filter") != std::string::npos) {
        throw PdfException(PdfErrorCode::MalformedXref, "Unsupported filter in xref stream.");
    }

    const std::size_t entryWidth = widths[0] + widths[1] + widths[2];
    if (entryWidth == 0U) throw PdfException(PdfErrorCode::MalformedXref, "XRef stream entry width is zero.");

    auto readBigEndian = [&](std::size_t& pos, std::size_t width) -> std::uint64_t {
        std::uint64_t value = 0;
        if (pos + width > decoded.size()) {
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
                entry.generation = static_cast<std::uint16_t>(field3);
                entry.inUse = false;
            } else if (typeValue == 1U) {
                entry.type = PdfXrefEntry::Type::Uncompressed;
                entry.offset = field2;
                entry.generation = static_cast<std::uint16_t>(field3);
                entry.inUse = true;
            } else if (typeValue == 2U) {
                entry.type = PdfXrefEntry::Type::Compressed;
                entry.objectStream = static_cast<std::uint32_t>(field2);
                entry.objectIndex = static_cast<std::uint32_t>(field3);
                entry.inUse = true;
            } else {
                continue;
            }
            xref_.try_emplace(first + i, entry);
        }
    }

    const std::size_t dictBegin = object.find("<<");
    const std::size_t dictEnd = object.find("stream", dictBegin);
    if (trailerDictionary_.empty()) {
        trailerDictionary_ = object.substr(dictBegin, dictEnd - dictBegin);
    }

    const std::regex prevExpression(R"(/Prev\s+(\d+))");
    std::smatch prevMatch;
    if (std::regex_search(object, prevMatch, prevExpression)) {
        parseXrefSection(static_cast<std::uint64_t>(std::stoull(prevMatch[1].str())));
    }
}

void PdfDocument::parseClassicXref(std::uint64_t offset64) {
    std::size_t pos = static_cast<std::size_t>(offset64);
    pos = skipWhitespace(bytes_, pos);
    if (!startsWithAt(bytes_, pos, "xref")) {
        throw PdfException(PdfErrorCode::UnsupportedXrefStream,
                           "This basic version supports classic xref tables only; xref streams are not supported yet.");
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
        if (!subsectionStream || count == 0) {
            throw PdfException(PdfErrorCode::MalformedXref,
                               "Malformed xref subsection header: " + subsection);
        }

        for (std::uint32_t i = 0; i < count; ++i) {
            const std::string line = trim(readLine(bytes_, pos));
            std::istringstream entryStream(line);
            std::uint64_t entryOffset{};
            std::uint32_t generation{};
            char state{};
            entryStream >> entryOffset >> generation >> state;
            if (!entryStream || generation > std::numeric_limits<std::uint16_t>::max() ||
                (state != 'n' && state != 'f')) {
                throw PdfException(PdfErrorCode::MalformedXref,
                                   "Malformed xref entry: " + line);
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
    const std::string currentTrailer(
        bytes_.begin() + static_cast<std::ptrdiff_t>(pos),
        bytes_.begin() + static_cast<std::ptrdiff_t>(end));

    if (trailerDictionary_.empty()) {
        trailerDictionary_ = currentTrailer;
    }

    // Hybrid-reference PDFs store compressed-object entries in an xref stream
    // referenced by /XRefStm from the classic trailer. Parse it before /Prev so
    // entries from the newest revision always win over older revisions.
    const std::regex xrefStmExpression(R"(/XRefStm\s+(\d+))");
    std::smatch xrefStmMatch;
    if (std::regex_search(currentTrailer, xrefStmMatch, xrefStmExpression)) {
        parseXrefSection(static_cast<std::uint64_t>(std::stoull(xrefStmMatch[1].str())));
    }

    const std::regex prevExpression(R"(/Prev\s+(\d+))");
    std::smatch prevMatch;
    if (std::regex_search(currentTrailer, prevMatch, prevExpression)) {
        parseXrefSection(static_cast<std::uint64_t>(std::stoull(prevMatch[1].str())));
    }
}

std::string PdfDocument::readIndirectObject(std::uint32_t objectNumber) const {
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
    return std::string(remaining.substr(0, endRelative + 6));
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
        pos = skipWhitespace(bytes_, pos);
        if (!startsWithAt(bytes_, pos, "obj")) {
            searchPos += number.size();
            continue;
        }

        const std::string_view remaining(bytes_.data() + static_cast<std::ptrdiff_t>(searchPos),
                                         bytes_.size() - searchPos);
        const std::size_t endRelative = remaining.find("endobj");
        if (endRelative != std::string_view::npos) {
            return std::string(remaining.substr(0, endRelative + 6U));
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
            objectStream = std::string(remaining.substr(0, endRelative + 6U));
        } catch (...) {
            continue;
        }

        if (parseNameAfterKey(objectStream, "Type") != "ObjStm") continue;

        try {
            const std::size_t count = parseIntegerAfterKey(objectStream, "N");
            const std::size_t first = parseIntegerAfterKey(objectStream, "First");
            std::string decoded = extractStreamData(objectStream);
            if (objectStream.find("/FlateDecode") != std::string::npos) {
                decoded = inflateZlib(decoded);
            } else if (objectStream.find("/Filter") != std::string::npos) {
                continue;
            }
            if (first > decoded.size()) continue;

            std::istringstream header(decoded.substr(0, first));
            std::vector<std::pair<std::uint32_t, std::size_t>> objects;
            objects.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
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
                const std::size_t bodyBegin = first + objects[i].second;
                const std::size_t bodyEnd = i + 1U < objects.size()
                    ? first + objects[i + 1U].second : decoded.size();
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
    const std::string objectStream = readIndirectObject(entry.objectStream);
    if (parseNameAfterKey(objectStream, "Type") != "ObjStm") {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Compressed object references a stream that is not /Type /ObjStm.");
    }
    const std::size_t count = parseIntegerAfterKey(objectStream, "N");
    const std::size_t first = parseIntegerAfterKey(objectStream, "First");
    std::string decoded = extractStreamData(objectStream);
    if (objectStream.find("/FlateDecode") != std::string::npos) decoded = inflateZlib(decoded);
    else if (objectStream.find("/Filter") != std::string::npos) {
        throw PdfException(PdfErrorCode::MalformedObject, "Unsupported filter in object stream.");
    }
    if (first > decoded.size()) {
        throw PdfException(PdfErrorCode::MalformedObject, "/First points outside object stream.");
    }

    std::istringstream header(decoded.substr(0, first));
    std::vector<std::pair<std::uint32_t, std::size_t>> objects;
    objects.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t number{};
        std::size_t relative{};
        if (!(header >> number >> relative)) {
            throw PdfException(PdfErrorCode::MalformedObject, "Malformed /ObjStm object index.");
        }
        objects.emplace_back(number, relative);
    }

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

    const std::size_t bodyBegin = first + objects[selected].second;
    const std::size_t bodyEnd = selected + 1U < objects.size()
        ? first + objects[selected + 1U].second : decoded.size();
    if (bodyBegin > bodyEnd || bodyEnd > decoded.size()) {
        throw PdfException(PdfErrorCode::MalformedObject, "Invalid object boundaries in /ObjStm.");
    }
    return std::to_string(objectNumber) + " 0 obj\n" +
           decoded.substr(bodyBegin, bodyEnd - bodyBegin) + "\nendobj";
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

PdfReference PdfDocument::parseReferenceAfterKey(const std::string& dictionary,
                                                 const std::string& key) {
    const std::regex expression("/" + key + R"(\s+(\d+)\s+(\d+)\s+R)");
    std::smatch match;
    if (!std::regex_search(dictionary, match, expression)) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Reference /" + key + " was not found.");
    }
    return PdfReference{
        static_cast<std::uint32_t>(std::stoul(match[1].str())),
        static_cast<std::uint16_t>(std::stoul(match[2].str()))};
}

std::vector<PdfReference> PdfDocument::parseReferenceArrayAfterKey(const std::string& dictionary,
                                                                   const std::string& key) {
    const std::regex arrayExpression("/" + key + R"(\s*\[([^\]]*)\])");
    std::smatch arrayMatch;
    if (!std::regex_search(dictionary, arrayMatch, arrayExpression)) {
        return {};
    }

    std::vector<PdfReference> references;
    const std::string contents = arrayMatch[1].str();
    const std::regex referenceExpression(R"((\d+)\s+(\d+)\s+R)");
    for (std::sregex_iterator it(contents.begin(), contents.end(), referenceExpression), end;
         it != end; ++it) {
        references.push_back(PdfReference{
            static_cast<std::uint32_t>(std::stoul((*it)[1].str())),
            static_cast<std::uint16_t>(std::stoul((*it)[2].str()))});
    }
    return references;
}

std::string PdfDocument::parseNameAfterKey(const std::string& dictionary, const std::string& key) {
    const std::regex expression("/" + key + R"(\s*/([^\s<>{}\[\]()/%]+))");
    std::smatch match;
    if (!std::regex_search(dictionary, match, expression)) {
        return {};
    }
    return match[1].str();
}


std::string PdfDocument::parseStringAfterKey(const std::string& dictionary,
                                             const std::string& key) {
    const std::string marker = "/" + key;
    std::size_t pos = dictionary.find(marker);
    if (pos == std::string::npos) return {};
    pos += marker.size();
    while (pos < dictionary.size() && std::isspace(static_cast<unsigned char>(dictionary[pos]))) ++pos;
    if (pos >= dictionary.size()) return {};

    if (dictionary[pos] == '(') {
        const std::size_t begin = ++pos;
        int depth = 1;
        bool escaped = false;
        for (; pos < dictionary.size(); ++pos) {
            const char ch = dictionary[pos];
            if (escaped) { escaped = false; continue; }
            if (ch == '\\') { escaped = true; continue; }
            if (ch == '(') ++depth;
            else if (ch == ')' && --depth == 0) {
                return decodePdfLiteralString(std::string_view(dictionary).substr(begin, pos - begin));
            }
        }
        return {};
    }

    if (dictionary[pos] == '<' && (pos + 1 >= dictionary.size() || dictionary[pos + 1] != '<')) {
        const std::size_t end = dictionary.find('>', pos + 1);
        if (end == std::string::npos) return {};
        return decodePdfHexString(std::string_view(dictionary).substr(pos + 1, end - pos - 1));
    }
    return {};
}

std::vector<double> PdfDocument::parseNumberArrayAfterKey(const std::string& dictionary,
                                                           const std::string& key) {
    const std::regex expression("/" + key + R"(\s*\[([^\]]*)\])");
    std::smatch match;
    if (!std::regex_search(dictionary, match, expression)) return {};

    std::vector<double> values;
    const std::string body = match[1].str();
    const std::regex numberExpression(R"([+-]?(?:\d+(?:\.\d*)?|\.\d+))");
    for (std::sregex_iterator it(body.begin(), body.end(), numberExpression), end; it != end; ++it) {
        values.push_back(std::stod((*it).str()));
    }
    return values;
}

std::string PdfDocument::findInheritedPageValue(std::string pageObject,
                                                 const std::string& key) const {
    const std::regex valueExpression("/" + key + R"(\s*(\[[^\]]*\]|[+-]?\d+))");
    std::unordered_set<std::uint32_t> visited;
    for (;;) {
        std::smatch match;
        if (std::regex_search(pageObject, match, valueExpression)) return match[1].str();

        const std::regex parentExpression(R"(/Parent\s+(\d+)\s+(\d+)\s+R)");
        if (!std::regex_search(pageObject, match, parentExpression)) return {};
        const auto objectNumber = static_cast<std::uint32_t>(std::stoul(match[1].str()));
        if (!visited.insert(objectNumber).second) {
            throw PdfException(PdfErrorCode::InvalidPageTree,
                               "Cycle detected while resolving inherited page attributes.");
        }
        pageObject = readIndirectObject(objectNumber);
    }
}

std::size_t PdfDocument::parseIntegerAfterKey(const std::string& dictionary,
                                              const std::string& key) {
    const std::regex expression("/" + key + R"(\s+(\d+))");
    std::smatch match;
    if (!std::regex_search(dictionary, match, expression)) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Integer /" + key + " was not found.");
    }
    return static_cast<std::size_t>(std::stoull(match[1].str()));
}

PdfReference PdfDocument::findRootReference() const {
    return parseReferenceAfterKey(trailerDictionary_, "Root");
}

PdfReference PdfDocument::GetCatalogReference() const {
    return findRootReference();
}

std::optional<PdfReference> PdfDocument::GetTrailerReference(const PdfName& key) const {
    const std::string escapedKey = key.value();
    const std::regex expression("/" + escapedKey + R"(\s+(\d+)\s+(\d+)\s+R)");
    std::smatch match;
    if (!std::regex_search(trailerDictionary_, match, expression)) return std::nullopt;
    return PdfReference{
        static_cast<std::uint32_t>(std::stoul(match[1].str())),
        static_cast<std::uint16_t>(std::stoul(match[2].str()))};
}

PdfReference PdfDocument::findPagesReference(const std::string& catalogObject) const {
    return parseReferenceAfterKey(catalogObject, "Pages");
}

std::size_t PdfDocument::countPagesFromNode(const PdfReference& reference,
                                            std::unordered_map<std::uint32_t, bool>& visiting) const {
    if (visiting[reference.objectNumber]) {
        throw PdfException(PdfErrorCode::InvalidPageTree, "Cycle detected in PDF page tree.");
    }
    visiting[reference.objectNumber] = true;

    const std::string object = readIndirectObject(reference.objectNumber);
    const std::string type = parseNameAfterKey(object, "Type");
    if (type == "Page") {
        visiting[reference.objectNumber] = false;
        return 1;
    }
    if (type != "Pages") {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page tree object has unsupported /Type /" + type + ".");
    }

    const auto kids = parseReferenceArrayAfterKey(object, "Kids");
    if (kids.empty()) {
        const auto count = parseIntegerAfterKey(object, "Count");
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
    const std::regex infoExpression(R"(/Info\s+(\d+)\s+(\d+)\s+R)");
    std::smatch match;
    if (!std::regex_search(trailerDictionary_, match, infoExpression)) return info;

    const auto objectNumber = static_cast<std::uint32_t>(std::stoul(match[1].str()));
    const std::string object = readIndirectObject(objectNumber);
    info.title = parseStringAfterKey(object, "Title");
    info.author = parseStringAfterKey(object, "Author");
    info.subject = parseStringAfterKey(object, "Subject");
    info.keywords = parseStringAfterKey(object, "Keywords");
    info.creator = parseStringAfterKey(object, "Creator");
    info.producer = parseStringAfterKey(object, "Producer");
    info.creationDate = parseStringAfterKey(object, "CreationDate");
    info.modificationDate = parseStringAfterKey(object, "ModDate");
    return info;
}

PdfPageInfo PdfDocument::pageInfo(const std::size_t pageIndex) const {
    const auto pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree, "Page index is out of range.");
    }

    const PdfReference reference = pages[pageIndex];
    const std::string pageObject = readIndirectObject(reference.objectNumber);
    auto parseBox = [&](const std::string& key, const PdfRectangle& fallback) {
        const std::string inherited = findInheritedPageValue(pageObject, key);
        if (inherited.empty()) return fallback;
        const auto values = parseNumberArrayAfterKey("/" + key + " " + inherited, key);
        if (values.size() < 4U) return fallback;
        return PdfRectangle{values[0], values[1], values[2], values[3]};
    };

    const PdfRectangle defaultLetter{0.0, 0.0, 612.0, 792.0};
    const PdfRectangle mediaBox = parseBox("MediaBox", defaultLetter);
    const PdfRectangle cropBox = parseBox("CropBox", mediaBox);

    int rotation = 0;
    const std::string inheritedRotation = findInheritedPageValue(pageObject, "Rotate");
    if (!inheritedRotation.empty()) {
        try { rotation = std::stoi(inheritedRotation); } catch (...) { rotation = 0; }
        rotation %= 360;
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
    const std::string type = parseNameAfterKey(object, "Type");
    if (type == "Page") {
        pages.push_back(reference);
        visiting[reference.objectNumber] = false;
        return;
    }
    if (type != "Pages") {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page tree object has unsupported /Type /" + type + ".");
    }
    for (const auto& kid : parseReferenceArrayAfterKey(object, "Kids")) {
        collectPageReferences(kid, visiting, pages);
    }
    visiting[reference.objectNumber] = false;
}

std::vector<PdfReference> PdfDocument::pageReferences() const {
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
    auto references = parseReferenceArrayAfterKey(pageObject, "Contents");
    if (!references.empty()) return references;

    const std::regex expression(R"(/Contents\s+(\d+)\s+(\d+)\s+R)");
    std::smatch match;
    if (std::regex_search(pageObject, match, expression)) {
        references.push_back(PdfReference{
            static_cast<std::uint32_t>(std::stoul(match[1].str())),
            static_cast<std::uint16_t>(std::stoul(match[2].str()))});
    }
    return references;
}

std::string PdfDocument::decodeContentStream(const std::string& streamObject) const {
    const std::string encoded = extractStreamData(streamObject);
    std::vector<PdfFilterSpec> filters;
    const auto addFilter = [&](const char* full, const char* shortName) {
        if (streamObject.find(std::string("/") + full) != std::string::npos ||
            streamObject.find(std::string("/") + shortName) != std::string::npos) {
            filters.push_back(PdfFilterSpec{full, {}});
        }
    };
    addFilter("ASCIIHexDecode", "AHx");
    addFilter("ASCII85Decode", "A85");
    addFilter("FlateDecode", "Fl");
    addFilter("RunLengthDecode", "RL");
    if (filters.empty()) {
        if (streamObject.find("/Filter") != std::string::npos) {
            throw PdfException(PdfErrorCode::UnsupportedFeature,
                               "The content stream uses an unsupported filter.");
        }
        return encoded;
    }
    const auto* begin = reinterpret_cast<const std::byte*>(encoded.data());
    const auto decoded = PdfFilterPipeline(readerOptions_.limits.maxDecodedStreamSize)
        .Decode(std::span<const std::byte>(begin, encoded.size()), filters);
    return std::string(reinterpret_cast<const char*>(decoded.data()), decoded.size());
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
        return decodeContentStream(readIndirectObject(reference.objectNumber));
    };
    for (const auto& contentRef : contentReferences(pageObject)) {
        request.sourceObjectNumber = contentRef.objectNumber;
        const auto decoded = decoder(contentRef);
        extractContentRecursively(
            *this, decoded, resources, request, decoder, activeForms, 0U, chunks);
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

void PdfDocument::ClearObjectCache() const noexcept {
    if (objectResolver_) {
        objectResolver_->Clear();
    }
}

PdfPage PdfDocument::GetPage(const std::size_t pageIndex) const {
    const auto refs = pageReferences();
    if (pageIndex >= refs.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree, "Page index is out of range.");
    }
    const std::string pageObject = readIndirectObject(refs[pageIndex].objectNumber);
    const std::string resources = findInheritedPageValue(pageObject, "Resources");
    std::vector<std::string> streams;
    for (const auto& ref : contentReferences(pageObject)) {
        streams.push_back(decodeContentStream(readIndirectObject(ref.objectNumber)));
    }
    return PdfPage(pageInfo(pageIndex), resources, std::move(streams));
}

PdfReference PdfDocument::GetPageReference(const std::size_t pageIndex) const {
    const auto refs = pageReferences();
    if (pageIndex >= refs.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree, "Page index is out of range.");
    }
    return refs[pageIndex];
}

std::string PdfDocument::extractPageText(std::size_t pageIndex) const {
    const auto pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page index " + std::to_string(pageIndex) + " is outside the document.");
    }
    return extractPageTextFromReference(pages[pageIndex], {});
}

PdfTextPage PdfDocument::ExtractTextPage(
    std::size_t pageIndex,
    const PdfTextExtractionOptions& options) const {
    const auto pages = pageReferences();
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
    const auto pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page index " + std::to_string(pageIndex) + " is outside the document.");
    }
    const std::string pageObject = readIndirectObject(pages[pageIndex].objectNumber);
    const auto* resources = inheritedPageResources(*this, pages[pageIndex]);
    std::vector<PdfTextChunk> chunks;
    std::unordered_set<std::uint64_t> activeForms;
    const StreamDecoder decoder = [this](const PdfReference& reference) {
        return decodeContentStream(readIndirectObject(reference.objectNumber));
    };
    for (const auto& contentRef : contentReferences(pageObject)) {
        request.sourceObjectNumber = contentRef.objectNumber;
        const auto decoded = decoder(contentRef);
        extractContentRecursively(
            *this, decoded, resources, request, decoder, activeForms, 0U, chunks);
    }
    return chunks;
}

std::string PdfDocument::ExtractText(
    const std::size_t pageIndex,
    const PdfTextExtractionRequest& request) const {
    return PdfTextExtractor::BuildText(ExtractTextChunks(pageIndex, request), request);
}

std::vector<PdfExtractedImage> PdfDocument::ExtractImages(
    const std::size_t pageIndex,
    const PdfImageExtractionOptions& options) const {
    const auto pages = pageReferences();
    if (pageIndex >= pages.size()) {
        throw PdfException(PdfErrorCode::InvalidPageTree,
                           "Page index " + std::to_string(pageIndex) + " is outside the document.");
    }
    const std::string pageObject = readIndirectObject(pages[pageIndex].objectNumber);
    const auto* resources = inheritedPageResources(*this, pages[pageIndex]);
    std::vector<PdfExtractedImage> images;
    std::unordered_set<std::uint64_t> activeForms;
    const StreamDecoder decoder = [this](const PdfReference& reference) {
        return decodeContentStream(readIndirectObject(reference.objectNumber));
    };
    const std::array<double, 6> identity{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    for (const auto& contentRef : contentReferences(pageObject)) {
        extractImagesRecursively(*this, decoder(contentRef), resources, identity,
            options, decoder, activeForms, 0U, images);
    }
    return images;
}

std::vector<std::string> PdfDocument::extractAllPageText() const {
    const auto pages = pageReferences();
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

} // namespace CPPPdf
