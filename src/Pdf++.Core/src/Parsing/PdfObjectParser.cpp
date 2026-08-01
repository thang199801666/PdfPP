#include "Internal/Parsing/PdfObjectParser.hpp"
#include <CPPPdf/PdfError.hpp>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace CPPPdf::Internal {
namespace {
class Parser {
public:
    Parser(std::string_view s,std::size_t maxDepth):s_(s),maxDepth_(maxDepth){}
    PdfObject ParseValue(std::size_t depth=0){
        if(depth>maxDepth_) throw PdfException(PdfErrorCode::MalformedObject,"PDF object recursion limit exceeded.");
        Skip(); if(End()) return {};
        if(Match("null")) return {};
        if(Match("true")) return PdfObject(true);
        if(Match("false")) return PdfObject(false);
        if(Peek('/') ) return PdfObject(ParseName());
        if(Peek('(')) return PdfObject(ParseLiteralString());
        if(Starts("<<")) return PdfObject(ParseDictionary(depth+1));
        if(Peek('[')) return PdfObject(ParseArray(depth+1));
        if(Peek('<')) return PdfObject(ParseHexString());
        if(Peek('+')||Peek('-')||Peek('.')||std::isdigit(static_cast<unsigned char>(Current()))) return ParseNumberOrReference();
        throw PdfException(PdfErrorCode::MalformedObject,"Unsupported token in typed PDF object parser.");
    }
private:
    void Skip(){ while(!End()){ unsigned char c=static_cast<unsigned char>(s_[p_]); if(std::isspace(c)||c==0){++p_;continue;} if(c=='%'){while(!End()&&s_[p_]!='\n'&&s_[p_]!='\r')++p_;continue;} break; } }
    bool End()const{return p_>=s_.size();} char Current()const{return End()?'\0':s_[p_];} bool Peek(char c)const{return Current()==c;}
    bool Starts(std::string_view t)const{return p_+t.size()<=s_.size()&&s_.substr(p_,t.size())==t;}
    bool Match(std::string_view t){Skip(); if(!Starts(t))return false; p_+=t.size(); return true;}
    static bool Delim(char c){return std::isspace(static_cast<unsigned char>(c))||c=='/'||c=='<'||c=='>'||c=='['||c==']'||c=='('||c==')'||c=='%';}
    PdfName ParseName(){ ++p_; std::string v; while(!End()&&!Delim(Current())){ if(Current()=='#'&&p_+2<s_.size()){ auto hex=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}; int a=hex(s_[p_+1]),b=hex(s_[p_+2]); if(a>=0&&b>=0){v.push_back(static_cast<char>((a<<4)|b));p_+=3;continue;}} v.push_back(Current());++p_;} return PdfName(std::move(v)); }
    std::string ParseLiteralString(){ ++p_; std::string out; int depth=1; bool esc=false; while(!End()&&depth>0){char c=s_[p_++]; if(esc){esc=false; switch(c){case'n':out+='\n';break;case'r':out+='\r';break;case't':out+='\t';break;case'b':out+='\b';break;case'f':out+='\f';break;case'\n':break;case'\r':if(!End()&&Current()=='\n')++p_;break;default:out+=c;} continue;} if(c=='\\'){esc=true;continue;} if(c=='('){++depth;out+=c;} else if(c==')'){if(--depth>0)out+=c;} else out+=c;} if(depth)throw PdfException(PdfErrorCode::MalformedObject,"Unterminated PDF string."); return out; }
    std::string ParseHexString(){ ++p_; std::string hex; while(!End()&&Current()!='>'){if(!std::isspace(static_cast<unsigned char>(Current())))hex+=Current();++p_;} if(End())throw PdfException(PdfErrorCode::MalformedObject,"Unterminated hex string."); ++p_; if(hex.size()%2)hex+='0'; auto h=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}; std::string out; for(std::size_t i=0;i<hex.size();i+=2){int a=h(hex[i]),b=h(hex[i+1]);if(a<0||b<0)throw PdfException(PdfErrorCode::MalformedObject,"Invalid hex string.");out.push_back(static_cast<char>((a<<4)|b));}return out; }
    PdfArray ParseArray(std::size_t depth){++p_; PdfArray a; a.reserve(8U); for(;;){Skip(); if(End())throw PdfException(PdfErrorCode::MalformedObject,"Unterminated array."); if(Peek(']')){++p_;break;} a.push_back(ParseValue(depth));} return a;}
    PdfDictionary ParseDictionary(std::size_t depth){p_+=2; PdfDictionary d; d.reserve(8U); for(;;){Skip(); if(Starts(">>")){p_+=2;break;} if(!Peek('/'))throw PdfException(PdfErrorCode::MalformedObject,"Dictionary key is not a name."); auto key=ParseName(); auto value=ParseValue(depth); d.Put(std::move(key),std::move(value));} return d;}
    PdfObject ParseNumberOrReference(){ Skip(); auto start=p_; while(!End()&&!Delim(Current()))++p_; auto token=s_.substr(start,p_-start); bool real=token.find_first_of(".")!=std::string_view::npos; if(real){
            double value{};
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value,
                                                std::chars_format::general);
            if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()) {
                return PdfObject(value);
            }
            // Some older standard libraries have incomplete floating-point
            // from_chars support. Keep a compatible fallback outside the hot path.
            std::string temporary(token);
            char* end{};
            value = std::strtod(temporary.c_str(), &end);
            if (end != temporary.c_str() + temporary.size()) {
                throw PdfException(PdfErrorCode::MalformedObject, "Invalid real number.");
            }
            return PdfObject(value);
        } std::int64_t first{}; auto r=std::from_chars(token.data(),token.data()+token.size(),first); if(r.ec!=std::errc{})throw PdfException(PdfErrorCode::MalformedObject,"Invalid number."); auto save=p_; Skip(); auto secondStart=p_; while(!End()&&!Delim(Current()))++p_; auto secondToken=s_.substr(secondStart,p_-secondStart); std::int64_t second{}; auto r2=std::from_chars(secondToken.data(),secondToken.data()+secondToken.size(),second); if(r2.ec==std::errc{}){Skip(); if(Peek('R')){++p_;return PdfObject::IndirectReference(static_cast<std::uint32_t>(first),static_cast<std::uint16_t>(second));}} p_=save; return PdfObject(first); }
    std::string_view s_; std::size_t p_{}; std::size_t maxDepth_;
};
}
PdfObject PdfObjectParser::Parse(std::string_view source,std::size_t maxDepth){
    auto objPos=source.find("obj");
    if(objPos!=std::string_view::npos) source.remove_prefix(objPos+3);
    auto endObject=source.rfind("endobj");
    if(endObject!=std::string_view::npos) source=source.substr(0,endObject);

    PdfObject parsed = Parser(source,maxDepth).ParseValue();
    const auto* dictionary = parsed.AsDictionary();
    if(dictionary == nullptr) return parsed;

    const auto streamToken = source.find("stream");
    if(streamToken == std::string_view::npos) return parsed;
    const auto endStream = source.rfind("endstream");
    if(endStream == std::string_view::npos || endStream <= streamToken + 6U) {
        throw PdfException(PdfErrorCode::MalformedObject,"Unterminated PDF stream object.");
    }

    std::size_t dataStart = streamToken + 6U;
    if(dataStart < source.size() && source[dataStart] == '\r') ++dataStart;
    if(dataStart < source.size() && source[dataStart] == '\n') ++dataStart;
    std::size_t dataEnd = endStream;

    if(const auto* lengthObject = dictionary->Find(PdfName("Length"))) {
        if(const auto length = lengthObject->AsInteger(); length.has_value() && *length >= 0) {
            const auto requested = static_cast<std::size_t>(*length);
            if(requested <= source.size() - dataStart) dataEnd = dataStart + requested;
        }
    } else {
        while(dataEnd > dataStart && (source[dataEnd-1] == '\r' || source[dataEnd-1] == '\n')) --dataEnd;
    }

    std::vector<std::byte> bytes(dataEnd - dataStart);
    if (!bytes.empty()) {
        std::memcpy(bytes.data(), source.data() + dataStart, bytes.size());
    }
    return PdfObject(PdfStream(*dictionary,std::move(bytes)));
}
} // namespace CPPPdf::Internal
