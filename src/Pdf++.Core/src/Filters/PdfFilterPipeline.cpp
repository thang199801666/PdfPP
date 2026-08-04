#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <regex>
#include <unordered_map>
#include <zlib.h>

namespace CPPPdf {
namespace {
int Hex(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
void Check(std::size_t n,std::size_t max){ if(n>max) throw PdfException(PdfErrorCode::UnsupportedFeature,"Decoded stream exceeds configured limit."); }
void CheckAppend(std::size_t current, std::size_t additional, std::size_t max) {
    if (current > max || additional > max - current) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Decoded stream exceeds configured limit.");
    }
}

int Parameter(const std::string& dictionary, const char* key, int fallback) {
    const std::regex expression(std::string("/") + key + R"(\s+([+-]?\d+))");
    std::smatch match;
    if (!std::regex_search(dictionary, match, expression)) return fallback;
    return std::stoi(match[1].str());
}

unsigned char Paeth(unsigned char left, unsigned char above, unsigned char upperLeft) {
    const int p = static_cast<int>(left) + static_cast<int>(above) - static_cast<int>(upperLeft);
    const int pa = std::abs(p - static_cast<int>(left));
    const int pb = std::abs(p - static_cast<int>(above));
    const int pc = std::abs(p - static_cast<int>(upperLeft));
    if (pa <= pb && pa <= pc) return left;
    return pb <= pc ? above : upperLeft;
}

std::vector<std::byte> ApplyPredictor(
    std::vector<std::byte> decoded,
    const std::string& parameters,
    std::size_t maxDecodedSize) {
    const int predictor = Parameter(parameters, "Predictor", 1);
    if (predictor <= 1) return decoded;
    const int colors = Parameter(parameters, "Colors", 1);
    const int bits = Parameter(parameters, "BitsPerComponent", 8);
    const int columns = Parameter(parameters, "Columns", 1);
    if (colors <= 0 || bits <= 0 || columns <= 0) {
        throw PdfException(PdfErrorCode::MalformedObject, "Invalid predictor parameters.");
    }
    const std::size_t rowBytes =
        (static_cast<std::size_t>(colors) * static_cast<std::size_t>(columns) *
         static_cast<std::size_t>(bits) + 7U) / 8U;
    const std::size_t bytesPerPixel = std::max<std::size_t>(
        1U, (static_cast<std::size_t>(colors) * static_cast<std::size_t>(bits) + 7U) / 8U);
    if (rowBytes == 0U) throw PdfException(PdfErrorCode::MalformedObject, "Predictor row size is zero.");

    if (predictor == 2) {
        if (decoded.size() % rowBytes != 0U) {
            throw PdfException(PdfErrorCode::MalformedObject, "TIFF predictor row length is invalid.");
        }
        for (std::size_t row = 0; row < decoded.size(); row += rowBytes) {
            for (std::size_t i = bytesPerPixel; i < rowBytes; ++i) {
                decoded[row + i] = static_cast<std::byte>(
                    std::to_integer<unsigned char>(decoded[row + i]) +
                    std::to_integer<unsigned char>(decoded[row + i - bytesPerPixel]));
            }
        }
        return decoded;
    }
    if (predictor < 10 || predictor > 15) {
        throw PdfException(PdfErrorCode::UnsupportedFeature, "Unsupported predictor value.");
    }
    const std::size_t encodedRowBytes = rowBytes + 1U;
    if (decoded.size() % encodedRowBytes != 0U) {
        throw PdfException(PdfErrorCode::MalformedObject, "PNG predictor row length is invalid.");
    }
    const std::size_t rowCount = decoded.size() / encodedRowBytes;
    Check(rowCount * rowBytes, maxDecodedSize);
    std::vector<std::byte> output(rowCount * rowBytes);
    for (std::size_t row = 0; row < rowCount; ++row) {
        const auto filter = std::to_integer<unsigned char>(decoded[row * encodedRowBytes]);
        const auto sourceOffset = row * encodedRowBytes + 1U;
        const auto targetOffset = row * rowBytes;
        for (std::size_t i = 0; i < rowBytes; ++i) {
            const unsigned char source = std::to_integer<unsigned char>(decoded[sourceOffset + i]);
            const unsigned char left = i >= bytesPerPixel
                ? std::to_integer<unsigned char>(output[targetOffset + i - bytesPerPixel]) : 0U;
            const unsigned char above = row > 0
                ? std::to_integer<unsigned char>(output[targetOffset - rowBytes + i]) : 0U;
            const unsigned char upperLeft = row > 0 && i >= bytesPerPixel
                ? std::to_integer<unsigned char>(output[targetOffset - rowBytes + i - bytesPerPixel]) : 0U;
            unsigned char value{};
            switch (filter) {
            case 0: value = source; break;
            case 1: value = static_cast<unsigned char>(source + left); break;
            case 2: value = static_cast<unsigned char>(source + above); break;
            case 3: value = static_cast<unsigned char>(source + (static_cast<unsigned>(left) + above) / 2U); break;
            case 4: value = static_cast<unsigned char>(source + Paeth(left, above, upperLeft)); break;
            default: throw PdfException(PdfErrorCode::MalformedObject, "Unsupported PNG predictor row filter.");
            }
            output[targetOffset + i] = static_cast<std::byte>(value);
        }
    }
    return output;
}

std::vector<std::byte> EncodePredictor(
    std::span<const std::byte> input,
    const std::string& parameters) {
    const int predictor = Parameter(parameters, "Predictor", 1);
    if (predictor <= 1) return std::vector<std::byte>(input.begin(), input.end());
    const int colors = Parameter(parameters, "Colors", 1);
    const int bits = Parameter(parameters, "BitsPerComponent", 8);
    const int columns = Parameter(parameters, "Columns", 1);
    if (colors <= 0 || bits <= 0 || columns <= 0) {
        throw PdfException(PdfErrorCode::MalformedObject, "Invalid predictor parameters.");
    }
    if (bits % 8 != 0) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Predictor encoding requires byte-aligned samples.");
    }
    const std::size_t rowBytes = static_cast<std::size_t>(colors) *
        static_cast<std::size_t>(columns) * static_cast<std::size_t>(bits / 8);
    const std::size_t bytesPerPixel = static_cast<std::size_t>(colors) *
        static_cast<std::size_t>(bits / 8);
    if (rowBytes == 0U || input.size() % rowBytes != 0U) {
        throw PdfException(PdfErrorCode::MalformedObject, "Predictor input row length is invalid.");
    }
    if (predictor == 2) {
        std::vector<std::byte> output(input.begin(), input.end());
        for (std::size_t row = 0; row < output.size(); row += rowBytes) {
            for (std::size_t i = rowBytes; i-- > bytesPerPixel;) {
                output[row + i] = static_cast<std::byte>(
                    std::to_integer<unsigned char>(output[row + i]) -
                    std::to_integer<unsigned char>(output[row + i - bytesPerPixel]));
            }
        }
        return output;
    }
    if (predictor < 10 || predictor > 15) {
        throw PdfException(PdfErrorCode::UnsupportedFeature, "Unsupported predictor value.");
    }
    std::vector<std::byte> output;
    output.reserve(input.size() + input.size() / rowBytes);
    for (std::size_t row = 0; row < input.size(); row += rowBytes) {
        // Filter type 0 is valid for every PNG predictor mode and keeps the
        // encoder deterministic without needing to optimize each scanline.
        output.push_back(std::byte{0});
        output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(row),
                      input.begin() + static_cast<std::ptrdiff_t>(row + rowBytes));
    }
    return output;
}
}

std::vector<std::byte> PdfFilterPipeline::Decode(std::span<const std::byte> input,const std::vector<PdfFilterSpec>& filters) const {
    Check(input.size(), maxDecodedSize_);
    std::vector<std::byte> data(input.begin(),input.end());
    for(const auto& f:filters){
        if(f.name=="FlateDecode"||f.name=="Fl") {
            data=DecodeFlate(data,maxDecodedSize_);
            data=ApplyPredictor(std::move(data), f.decodeParameters, maxDecodedSize_);
        }
        else if(f.name=="ASCIIHexDecode"||f.name=="AHx") data=DecodeAsciiHex(data,maxDecodedSize_);
        else if(f.name=="ASCII85Decode"||f.name=="A85") data=DecodeAscii85(data,maxDecodedSize_);
        else if(f.name=="RunLengthDecode"||f.name=="RL") data=DecodeRunLength(data,maxDecodedSize_);
        else if(f.name=="LZWDecode"||f.name=="LZW") {
            const bool earlyChange = Parameter(f.decodeParameters, "EarlyChange", 1) != 0;
            data=DecodeLzw(data, earlyChange, maxDecodedSize_);
            data=ApplyPredictor(std::move(data), f.decodeParameters, maxDecodedSize_);
        }
        else throw PdfException(PdfErrorCode::UnsupportedFeature,"Unsupported PDF filter: "+f.name);
        Check(data.size(),maxDecodedSize_);
    }
    return data;
}

std::vector<std::byte> PdfFilterPipeline::Encode(
    std::span<const std::byte> input, const std::vector<PdfFilterSpec>& filters) const {
    std::vector<std::byte> data(input.begin(), input.end());
    for (const auto& f : filters) {
        if (f.name == "FlateDecode" || f.name == "Fl") {
            data = EncodeFlate(EncodePredictor(data, f.decodeParameters));
        }
        else if (f.name == "ASCIIHexDecode" || f.name == "AHx") data = EncodeAsciiHex(data);
        else if (f.name == "ASCII85Decode" || f.name == "A85") data = EncodeAscii85(data);
        else if (f.name == "RunLengthDecode" || f.name == "RL") data = EncodeRunLength(data);
        else if (f.name == "LZWDecode" || f.name == "LZW") {
            const bool earlyChange = Parameter(f.decodeParameters, "EarlyChange", 1) != 0;
            data = EncodeLzw(EncodePredictor(data, f.decodeParameters), earlyChange);
        }
        else throw PdfException(PdfErrorCode::UnsupportedFeature, "Unsupported PDF filter for encoding: " + f.name);
        Check(data.size(), maxDecodedSize_);
    }
    return data;
}
std::vector<std::byte> PdfFilterPipeline::DecodeFlate(std::span<const std::byte> input,std::size_t maxSize){
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "FlateDecode input exceeds the zlib input limit.");
    }
    z_stream s{}; s.next_in=reinterpret_cast<Bytef*>(const_cast<std::byte*>(input.data())); s.avail_in=static_cast<uInt>(input.size());
    if(inflateInit(&s)!=Z_OK) throw PdfException(PdfErrorCode::MalformedObject,"Cannot initialize FlateDecode.");
    std::vector<std::byte> out; std::byte chunk[16384]; int code=Z_OK;
    while(code==Z_OK){ s.next_out=reinterpret_cast<Bytef*>(chunk); s.avail_out=sizeof(chunk); code=inflate(&s,Z_NO_FLUSH); const auto n=sizeof(chunk)-s.avail_out; out.insert(out.end(),chunk,chunk+n); Check(out.size(),maxSize); }
    inflateEnd(&s); if(code!=Z_STREAM_END) throw PdfException(PdfErrorCode::MalformedObject,"Invalid FlateDecode stream."); return out;
}
std::vector<std::byte> PdfFilterPipeline::DecodeAsciiHex(
    std::span<const std::byte> input, std::size_t maxDecodedSize) {
    std::vector<std::byte> out;
    int high = -1;
    for (auto b : input) {
        const char c = static_cast<char>(b);
        if (c == '>') break;
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        const int h = Hex(c);
        if (h < 0) throw PdfException(PdfErrorCode::MalformedObject, "Invalid ASCIIHex data.");
        if (high < 0) {
            high = h;
        } else {
            CheckAppend(out.size(), 1U, maxDecodedSize);
            out.push_back(static_cast<std::byte>((high << 4) | h));
            high = -1;
        }
    }
    if (high >= 0) {
        CheckAppend(out.size(), 1U, maxDecodedSize);
        out.push_back(static_cast<std::byte>(high << 4));
    }
    return out;
}
std::vector<std::byte> PdfFilterPipeline::DecodeAscii85(
    std::span<const std::byte> input, std::size_t maxDecodedSize) {
    std::vector<std::byte> out;
    std::uint32_t tuple = 0;
    int count = 0;
    for (auto b : input) {
        const char c = static_cast<char>(b);
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '~') break;
        if (c == 'z' && count == 0) {
            CheckAppend(out.size(), 4U, maxDecodedSize);
            out.insert(out.end(), 4U, std::byte{0});
            continue;
        }
        if (c < '!' || c > 'u') {
            throw PdfException(PdfErrorCode::MalformedObject, "Invalid ASCII85 data.");
        }
        tuple = tuple * 85U + static_cast<unsigned>(c - '!');
        if (++count == 5) {
            CheckAppend(out.size(), 4U, maxDecodedSize);
            for (int i = 3; i >= 0; --i) {
                out.push_back(static_cast<std::byte>((tuple >> (i * 8)) & 0xFFU));
            }
            tuple = 0;
            count = 0;
        }
    }
    if (count > 1) {
        for (int i = count; i < 5; ++i) tuple = tuple * 85U + 84U;
        const std::size_t produced = static_cast<std::size_t>(count - 1);
        CheckAppend(out.size(), produced, maxDecodedSize);
        for (int i = 3; i >= 4 - (count - 1); --i) {
            out.push_back(static_cast<std::byte>((tuple >> (i * 8)) & 0xFFU));
        }
    }
    return out;
}
std::vector<std::byte> PdfFilterPipeline::DecodeRunLength(
    std::span<const std::byte> input, std::size_t maxDecodedSize) {
    std::vector<std::byte> out;
    std::size_t i = 0;
    while (i < input.size()) {
        const unsigned n = std::to_integer<unsigned>(input[i++]);
        if (n == 128U) break;
        if (n <= 127U) {
            const std::size_t count = n + 1U;
            if (i + count > input.size()) {
                throw PdfException(PdfErrorCode::MalformedObject, "Truncated RunLength stream.");
            }
            CheckAppend(out.size(), count, maxDecodedSize);
            out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(i),
                       input.begin() + static_cast<std::ptrdiff_t>(i + count));
            i += count;
        } else {
            if (i >= input.size()) {
                throw PdfException(PdfErrorCode::MalformedObject, "Truncated RunLength stream.");
            }
            CheckAppend(out.size(), 257U - n, maxDecodedSize);
            out.insert(out.end(), 257U - n, input[i++]);
        }
    }
    return out;
}

namespace {
// LZW codes are packed into the byte stream with the least significant bit of
// each code first (PDF "EarlyChange" variant, table growth starting at 511).
class LzwBitReader final {
public:
    explicit LzwBitReader(std::span<const std::byte> data) : data_(data) {}

    // Returns 0xFFFFFFFF when fewer than `width` bits remain (stream end).
    std::uint32_t Read(int width) {
        if (width < 0 || width > 12 || bitPosition_ + static_cast<std::size_t>(width) > data_.size() * 8U) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        std::uint32_t value = 0;
        for (int i = 0; i < width; ++i) {
            const std::size_t byteIndex = bitPosition_ >> 3;
            const std::size_t bitIndex = bitPosition_ & 7U;
            const std::uint32_t bit =
                (std::to_integer<std::uint32_t>(data_[byteIndex]) >> bitIndex) & 1U;
            value |= bit << i;
            ++bitPosition_;
        }
        return value;
    }

private:
    std::span<const std::byte> data_;
    std::size_t bitPosition_{};
};
} // namespace

std::vector<std::byte> PdfFilterPipeline::DecodeLzw(
    std::span<const std::byte> input, bool earlyChange, std::size_t maxDecodedSize) {
    constexpr std::uint32_t kClear = 256U;
    constexpr std::uint32_t kEndOfData = 257U;
    constexpr std::size_t kTableSize = 4096U;
    constexpr int kMaxCodeWidth = 12;

    std::vector<std::string> table(kTableSize);
    for (std::uint32_t i = 0U; i < 256U; ++i) {
        table[i] = std::string(1, static_cast<char>(i));
    }

    LzwBitReader reader(input);
    std::vector<std::byte> output;
    std::string previous;
    std::uint32_t nextCode = 258U;
    int codeWidth = 9;

    for (;;) {
        const std::uint32_t code = reader.Read(codeWidth);
        if (code == std::numeric_limits<std::uint32_t>::max()) break;
        if (code == kClear) {
            nextCode = 258U;
            codeWidth = 9;
            previous.clear();
            continue;
        }
        if (code == kEndOfData) break;

        std::string entry;
        if (code == nextCode) {
            if (previous.empty() || nextCode >= kTableSize) {
                throw PdfException(PdfErrorCode::MalformedObject, "Invalid LZW sequence.");
            }
            entry = previous + previous[0];
            table[nextCode] = entry;
            ++nextCode;
        } else {
            if (code > nextCode) {
                throw PdfException(PdfErrorCode::MalformedObject, "Invalid LZW code.");
            }
            entry = table[code];
            if (!previous.empty() && nextCode < kTableSize) {
                table[nextCode] = previous + entry[0];
                ++nextCode;
            }
        }

        CheckAppend(output.size(), entry.size(), maxDecodedSize);
        for (const char ch : entry) {
            output.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
        previous = std::move(entry);

        if (codeWidth < kMaxCodeWidth) {
            const std::uint32_t threshold =
                earlyChange ? (1U << codeWidth) - 1U : (1U << codeWidth);
            if (nextCode == threshold) ++codeWidth;
        }
    }
    return output;
}

std::vector<std::byte> PdfFilterPipeline::EncodeFlate(std::span<const std::byte> input) {
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "FlateDecode input exceeds the zlib input limit.");
    }
    uLongf bound = compressBound(static_cast<uLong>(input.size()));
    std::vector<std::byte> out(bound);
    const int rc = compress2(reinterpret_cast<Bytef*>(out.data()), &bound,
        reinterpret_cast<const Bytef*>(input.data()), static_cast<uLong>(input.size()),
        Z_BEST_COMPRESSION);
    if (rc != Z_OK) throw PdfException(PdfErrorCode::UnsupportedFeature, "FlateDecode compression failed.");
    out.resize(bound);
    return out;
}

std::vector<std::byte> PdfFilterPipeline::EncodeAsciiHex(std::span<const std::byte> input) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::vector<std::byte> out;
    out.reserve(input.size() * 2U + 2U);
    for (const auto b : input) {
        const auto value = std::to_integer<unsigned char>(b);
        out.push_back(static_cast<std::byte>(kHex[value >> 4U]));
        out.push_back(static_cast<std::byte>(kHex[value & 0x0FU]));
    }
    out.push_back(std::byte{'>'});
    return out;
}

std::vector<std::byte> PdfFilterPipeline::EncodeAscii85(std::span<const std::byte> input) {
    static constexpr char kBase[] = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "[\\]^_`abcdefghijklmnopqrstu";
    std::vector<std::byte> out;
    out.reserve(input.size() + input.size() / 4U + 8U);
    std::size_t i = 0;
    while (i + 4U <= input.size()) {
        std::uint32_t tuple = 0;
        for (int j = 0; j < 4; ++j) tuple = (tuple << 8U) | std::to_integer<unsigned>(input[i++]);
        if (tuple == 0U) {
            out.push_back(std::byte{'z'});
            continue;
        }
        std::byte digits[5];
        for (int j = 4; j >= 0; --j) { digits[j] = static_cast<std::byte>(kBase[tuple % 85U]); tuple /= 85U; }
        out.insert(out.end(), digits, digits + 5);
    }
    const std::size_t remaining = input.size() - i;
    if (remaining > 0U) {
        std::uint32_t tuple = 0;
        for (std::size_t j = 0; j < remaining; ++j) tuple = (tuple << 8U) | std::to_integer<unsigned>(input[i++]);
        for (std::size_t j = remaining; j < 4U; ++j) tuple = (tuple << 8U) | 0U;
        std::byte digits[5];
        for (int j = 4; j >= 0; --j) { digits[j] = static_cast<std::byte>(kBase[tuple % 85U]); tuple /= 85U; }
        out.insert(out.end(), digits, digits + remaining + 1U);
    }
    out.push_back(std::byte{'~'});
    out.push_back(std::byte{'>'});
    return out;
}

std::vector<std::byte> PdfFilterPipeline::EncodeRunLength(std::span<const std::byte> input) {
    std::vector<std::byte> out;
    out.reserve(input.size());
    std::size_t i = 0;
    while (i < input.size()) {
        std::size_t run = 1U;
        while (i + run < input.size() && run < 128U &&
               std::to_integer<unsigned char>(input[i]) == std::to_integer<unsigned char>(input[i + run])) {
            ++run;
        }
        if (run >= 2U) {
            out.push_back(static_cast<std::byte>(257U - run));
            out.push_back(input[i]);
            i += run;
            continue;
        }
        std::size_t literalEnd = i;
        while (literalEnd + 1U < input.size() && literalEnd - i < 127U &&
               std::to_integer<unsigned char>(input[literalEnd]) != std::to_integer<unsigned char>(input[literalEnd + 1U])) {
            ++literalEnd;
        }
        if (literalEnd - i + 1U >= 128U) literalEnd = i + 127U;
        const std::size_t literalCount = literalEnd - i + 1U;
        out.push_back(static_cast<std::byte>(literalCount - 1U));
        out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(i),
                   input.begin() + static_cast<std::ptrdiff_t>(i + literalCount));
        i += literalCount;
    }
    out.push_back(std::byte{128});
    return out;
}

std::vector<std::byte> PdfFilterPipeline::EncodeLzw(std::span<const std::byte> input, bool earlyChange) {
    constexpr std::uint32_t kClear = 256U;
    constexpr std::uint32_t kEndOfData = 257U;
    constexpr std::size_t kTableSize = 4096U;
    constexpr int kMaxCodeWidth = 12;

    std::unordered_map<std::string, std::uint32_t> table;
    for (std::uint32_t i = 0U; i < 256U; ++i) {
        table.emplace(std::string(1, static_cast<char>(static_cast<unsigned char>(i))), i);
    }
    std::uint32_t nextCode = 258U;
    int codeWidth = 9;

    std::vector<std::uint32_t> codes;
    codes.push_back(kClear);
    std::string current;
    for (const auto b : input) {
        const std::string next = current + static_cast<char>(std::to_integer<unsigned char>(b));
        const auto it = table.find(next);
        if (it != table.end()) {
            current = std::move(next);
            continue;
        }
        codes.push_back(table.at(current));
        if (nextCode < kTableSize) {
            table.emplace(next, nextCode++);
        }
        if (codeWidth < kMaxCodeWidth) {
            const std::uint32_t threshold = earlyChange ? (1U << codeWidth) - 1U : (1U << codeWidth);
            if (nextCode == threshold) ++codeWidth;
        }
        current.assign(1, static_cast<char>(std::to_integer<unsigned char>(b)));
    }
    if (!current.empty()) codes.push_back(table.at(current));
    codes.push_back(kEndOfData);

    std::vector<std::byte> packed;
    packed.reserve(codes.size());
    std::uint64_t bitBuffer = 0;
    std::size_t bitCount = 0;
    std::uint32_t tableSize = 258U;
    int width = 9;
    for (std::size_t ci = 0; ci < codes.size(); ++ci) {
        const std::uint32_t code = codes[ci];
        if (code == kClear) {
            tableSize = 258U;
            width = 9;
        }
        bitBuffer |= static_cast<std::uint64_t>(code) << bitCount;
        bitCount += static_cast<std::size_t>(width);
        while (bitCount >= 8U) {
            packed.push_back(static_cast<std::byte>(bitBuffer & 0xFFU));
            bitBuffer >>= 8U;
            bitCount -= 8U;
        }
        if (code != kClear) {
            const std::uint32_t threshold = earlyChange ? (1U << width) - 1U : (1U << width);
            if (tableSize == threshold && width < kMaxCodeWidth) {
                if (ci + 1U < codes.size() && codes[ci + 1U] == kClear) {
                    // EOD/CLEAR resets the width; do not grow.
                } else {
                    ++width;
                }
                ++tableSize;
            } else {
                ++tableSize;
            }
        }
    }
    if (bitCount > 0U) {
        packed.push_back(static_cast<std::byte>(bitBuffer & 0xFFU));
    }
    return packed;
}
} // namespace CPPPdf
