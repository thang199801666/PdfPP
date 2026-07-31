#include "Internal/Writer/PdfObjectSerializer.hpp"

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/PdfError.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <ostream>

namespace CPPPdf::Internal {
namespace {

constexpr char HexDigits[] = "0123456789ABCDEF";

void writeArray(
    std::ostream& output,
    const PdfArray& array,
    const PdfObjectSerializer::ReferenceMapper& mapper) {
    output.put('[');
    bool first = true;
    for (const auto& value : array.values()) {
        if (!first) output.put(' ');
        first = false;
        PdfObjectSerializer::WriteObject(output, value, mapper);
    }
    output.put(']');
}

template <typename Number>
void writeNumber(std::ostream& output, const Number value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Failed to serialize a PDF number.");
    }
    output.write(buffer.data(), static_cast<std::streamsize>(result.ptr - buffer.data()));
}

void writeReal(std::ostream& output, const double value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value,
        std::chars_format::general, 12);
    if (result.ec != std::errc{}) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Failed to serialize a PDF real number.");
    }
    output.write(buffer.data(), static_cast<std::streamsize>(result.ptr - buffer.data()));
}

} // namespace

std::string PdfObjectSerializer::EscapeName(const std::string_view value) {
    std::string output;
    output.reserve(value.size() + 1U);
    output.push_back('/');
    for (const unsigned char ch : value) {
        const bool regular = ch >= 33U && ch <= 126U && ch != '#' && ch != '/' && ch != '%' &&
            ch != '(' && ch != ')' && ch != '<' && ch != '>' && ch != '[' && ch != ']' &&
            ch != '{' && ch != '}';
        if (regular) {
            output.push_back(static_cast<char>(ch));
        } else {
            output.push_back('#');
            output.push_back(HexDigits[(ch >> 4U) & 0x0FU]);
            output.push_back(HexDigits[ch & 0x0FU]);
        }
    }
    return output;
}

std::string PdfObjectSerializer::EscapeLiteral(const std::string_view value) {
    std::string output;
    output.reserve(value.size() + 8U);
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': case '(': case ')':
            output.push_back('\\');
            output.push_back(static_cast<char>(ch));
            break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        default:
            if (ch < 32U || ch >= 127U) {
                output.push_back('\\');
                output.push_back(static_cast<char>('0' + ((ch >> 6U) & 0x07U)));
                output.push_back(static_cast<char>('0' + ((ch >> 3U) & 0x07U)));
                output.push_back(static_cast<char>('0' + (ch & 0x07U)));
            } else {
                output.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return output;
}

void PdfObjectSerializer::WriteDictionary(
    std::ostream& output,
    const PdfDictionary& dictionary,
    const ReferenceMapper& mapper) {
    output.write("<<", 2);
    for (const auto& [key, value] : dictionary.values()) {
        output.put('\n');
        const std::string escapedName = EscapeName(key.value());
        output.write(escapedName.data(), static_cast<std::streamsize>(escapedName.size()));
        output.put(' ');
        WriteObject(output, value, mapper);
    }
    output.write("\n>>", 3);
}

void PdfObjectSerializer::WriteObject(
    std::ostream& output,
    const PdfObject& object,
    const ReferenceMapper& mapper) {
    switch (object.type()) {
    case PdfObjectType::Null: output.write("null", 4); break;
    case PdfObjectType::Boolean:
        if (*object.AsBoolean()) output.write("true", 4);
        else output.write("false", 5);
        break;
    case PdfObjectType::Integer: writeNumber(output, *object.AsInteger()); break;
    case PdfObjectType::Real: writeReal(output, *object.AsReal()); break;
    case PdfObjectType::Name: {
        const std::string escaped = EscapeName(object.AsName()->value());
        output.write(escaped.data(), static_cast<std::streamsize>(escaped.size()));
        break;
    }
    case PdfObjectType::String: {
        const std::string escaped = EscapeLiteral(*object.AsString());
        output.put('(');
        output.write(escaped.data(), static_cast<std::streamsize>(escaped.size()));
        output.put(')');
        break;
    }
    case PdfObjectType::Array: writeArray(output, *object.AsArray(), mapper); break;
    case PdfObjectType::Dictionary: WriteDictionary(output, *object.AsDictionary(), mapper); break;
    case PdfObjectType::IndirectReference: {
        const auto pair = *object.AsReference();
        PdfReference reference{pair.first, pair.second};
        if (mapper) reference = mapper(reference);
        writeNumber(output, reference.objectNumber);
        output.put(' ');
        writeNumber(output, reference.generation);
        output.write(" R", 2);
        break;
    }
    case PdfObjectType::Stream: {
        const PdfStream& stream = *object.AsStream();
        PdfDictionary dictionary = stream.dictionary();
        dictionary.Put(PdfName("Length"), PdfObject(static_cast<std::int64_t>(stream.bytes().size())));
        WriteDictionary(output, dictionary, mapper);
        output.write("\nstream\n", 8);
        const auto bytes = stream.bytes();
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.write("\nendstream", 10);
        break;
    }
    }
}

} // namespace CPPPdf::Internal
