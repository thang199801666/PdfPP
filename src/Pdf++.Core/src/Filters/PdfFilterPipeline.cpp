#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <regex>
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
        else throw PdfException(PdfErrorCode::UnsupportedFeature,"Unsupported PDF filter: "+f.name);
        Check(data.size(),maxDecodedSize_);
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
} // namespace CPPPdf
