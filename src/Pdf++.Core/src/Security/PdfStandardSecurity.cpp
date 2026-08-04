#include "Internal/Security/PdfStandardSecurity.hpp"

#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cstring>
#include <random>
#include <regex>
#include <optional>
#include <stdexcept>

namespace CPPPdf::Internal {
namespace {

constexpr std::array<std::uint8_t, 32> PasswordPadding{
    0x28,0xBF,0x4E,0x5E,0x4E,0x75,0x8A,0x41,0x64,0x00,0x4E,0x56,0xFF,0xFA,0x01,0x08,
    0x2E,0x2E,0x00,0xB6,0xD0,0x68,0x3E,0x80,0x2F,0x0C,0xA9,0xFE,0x64,0x53,0x69,0x7A};

std::array<std::uint8_t, 32> padPassword(const std::string_view password) {
    std::array<std::uint8_t, 32> result{};
    const auto count = std::min<std::size_t>(password.size(), result.size());
    std::memcpy(result.data(), password.data(), count);
    std::copy_n(PasswordPadding.begin(), result.size() - count, result.begin() + count);
    return result;
}

std::array<std::uint8_t, 16> md5(const std::span<const std::uint8_t> input) {
    static constexpr std::array<std::uint32_t, 64> K{
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static constexpr std::array<unsigned, 64> S{
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    std::vector<std::uint8_t> data(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8U;
    data.push_back(0x80U);
    while ((data.size() % 64U) != 56U) data.push_back(0U);
    for (unsigned i = 0; i < 8; ++i) data.push_back(static_cast<std::uint8_t>(bitLength >> (8U * i)));

    std::uint32_t a0=0x67452301U,b0=0xefcdab89U,c0=0x98badcfeU,d0=0x10325476U;
    for (std::size_t offset=0; offset<data.size(); offset+=64U) {
        std::array<std::uint32_t,16> m{};
        for (std::size_t i=0;i<16;++i) for(unsigned j=0;j<4;++j)
            m[i]|=static_cast<std::uint32_t>(data[offset+i*4U+j])<<(8U*j);
        std::uint32_t a=a0,b=b0,c=c0,d=d0;
        for(std::uint32_t i=0;i<64;++i){std::uint32_t f{},g{};
            if(i<16){f=(b&c)|(~b&d);g=i;}else if(i<32){f=(d&b)|(~d&c);g=(5*i+1)%16;}
            else if(i<48){f=b^c^d;g=(3*i+5)%16;}else{f=c^(b|~d);g=(7*i)%16;}
            const auto oldD=d;d=c;c=b;b+=std::rotl(a+f+K[i]+m[g],static_cast<int>(S[i]));a=oldD;
        } a0+=a;b0+=b;c0+=c;d0+=d;
    }
    std::array<std::uint8_t,16> result{}; const std::array words{a0,b0,c0,d0};
    for(std::size_t i=0;i<4;++i)for(unsigned j=0;j<4;++j)result[i*4U+j]=static_cast<std::uint8_t>(words[i]>>(8U*j));
    return result;
}

std::vector<std::uint8_t> rc4(const std::span<const std::uint8_t> input,
                              const std::span<const std::uint8_t> key) {
    std::array<std::uint8_t,256> s{}; for(std::size_t i=0;i<s.size();++i)s[i]=static_cast<std::uint8_t>(i);
    std::uint8_t j{}; for(std::size_t i=0;i<s.size();++i){j=static_cast<std::uint8_t>(j+s[i]+key[i%key.size()]);std::swap(s[i],s[j]);}
    std::vector<std::uint8_t> out(input.begin(),input.end()); std::uint8_t i{};j=0;
    for(auto& value:out){i=static_cast<std::uint8_t>(i+1);j=static_cast<std::uint8_t>(j+s[i]);std::swap(s[i],s[j]);value^=s[static_cast<std::uint8_t>(s[i]+s[j])];}
    return out;
}

constexpr std::array<std::uint8_t,256> Sbox{
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
constexpr std::array<std::uint8_t,256> InvSbox{
0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d};

std::uint8_t mul(std::uint8_t a,std::uint8_t b){std::uint8_t r{};while(b){if(b&1U)r^=a;a=static_cast<std::uint8_t>((a<<1U)^((a&0x80U)?0x1bU:0U));b>>=1U;}return r;}
std::array<std::uint8_t,176> expandKey(std::span<const std::uint8_t,16> key){std::array<std::uint8_t,176>w{};std::copy(key.begin(),key.end(),w.begin());std::uint8_t rcon=1;std::size_t n=16;while(n<176){std::array<std::uint8_t,4>t{};std::copy_n(w.begin()+n-4,4,t.begin());if(n%16==0){const auto q=t[0];t[0]=Sbox[t[1]]^rcon;t[1]=Sbox[t[2]];t[2]=Sbox[t[3]];t[3]=Sbox[q];rcon=mul(rcon,2);}for(auto v:t){w[n]=w[n-16]^v;++n;}}return w;}
void addKey(std::array<std::uint8_t,16>&s,const std::array<std::uint8_t,176>&w,int round){for(int i=0;i<16;++i)s[i]^=w[round*16+i];}
void shiftRows(std::array<std::uint8_t,16>&s){const auto t=s;s[1]=t[5];s[5]=t[9];s[9]=t[13];s[13]=t[1];s[2]=t[10];s[6]=t[14];s[10]=t[2];s[14]=t[6];s[3]=t[15];s[7]=t[3];s[11]=t[7];s[15]=t[11];}
void invShiftRows(std::array<std::uint8_t,16>&s){const auto t=s;s[1]=t[13];s[5]=t[1];s[9]=t[5];s[13]=t[9];s[2]=t[10];s[6]=t[14];s[10]=t[2];s[14]=t[6];s[3]=t[7];s[7]=t[11];s[11]=t[15];s[15]=t[3];}
void mixColumns(std::array<std::uint8_t,16>&s){for(int c=0;c<4;++c){const int i=4*c;const auto a=s[i],b=s[i+1],d=s[i+2],e=s[i+3];s[i]=mul(a,2)^mul(b,3)^d^e;s[i+1]=a^mul(b,2)^mul(d,3)^e;s[i+2]=a^b^mul(d,2)^mul(e,3);s[i+3]=mul(a,3)^b^d^mul(e,2);}}
void invMixColumns(std::array<std::uint8_t,16>&s){for(int c=0;c<4;++c){const int i=4*c;const auto a=s[i],b=s[i+1],d=s[i+2],e=s[i+3];s[i]=mul(a,14)^mul(b,11)^mul(d,13)^mul(e,9);s[i+1]=mul(a,9)^mul(b,14)^mul(d,11)^mul(e,13);s[i+2]=mul(a,13)^mul(b,9)^mul(d,14)^mul(e,11);s[i+3]=mul(a,11)^mul(b,13)^mul(d,9)^mul(e,14);}}
void aesEncryptBlock(std::array<std::uint8_t,16>&s,const std::array<std::uint8_t,176>&w){addKey(s,w,0);for(int r=1;r<10;++r){for(auto&v:s)v=Sbox[v];shiftRows(s);mixColumns(s);addKey(s,w,r);}for(auto&v:s)v=Sbox[v];shiftRows(s);addKey(s,w,10);}
void aesDecryptBlock(std::array<std::uint8_t,16>&s,const std::array<std::uint8_t,176>&w){addKey(s,w,10);for(int r=9;r>0;--r){invShiftRows(s);for(auto&v:s)v=InvSbox[v];addKey(s,w,r);invMixColumns(s);}invShiftRows(s);for(auto&v:s)v=InvSbox[v];addKey(s,w,0);}

std::vector<std::uint8_t> aesCbc(const std::span<const std::uint8_t> input,
                                 const std::span<const std::uint8_t,16> key,bool encrypt){
    const auto schedule=expandKey(key);
    if(encrypt){auto iv=GeneratePdfFileId();std::vector<std::uint8_t> data(input.begin(),input.end());const auto pad=static_cast<std::uint8_t>(16U-data.size()%16U);data.insert(data.end(),pad,pad);std::vector<std::uint8_t> out(iv.begin(),iv.end());std::array<std::uint8_t,16>prev=iv;
        for(std::size_t p=0;p<data.size();p+=16){std::array<std::uint8_t,16>b{};std::copy_n(data.begin()+p,16,b.begin());for(int i=0;i<16;++i)b[i]^=prev[i];aesEncryptBlock(b,schedule);out.insert(out.end(),b.begin(),b.end());prev=b;}return out;}
    if(input.size()<32U||(input.size()%16U)!=0U)throw PdfException(PdfErrorCode::MalformedObject,"Invalid AES encrypted object length.");
    std::array<std::uint8_t,16>prev{};std::copy_n(input.begin(),16,prev.begin());std::vector<std::uint8_t>out;out.reserve(input.size()-16U);
    for(std::size_t p=16;p<input.size();p+=16){std::array<std::uint8_t,16>b{},cipher{};std::copy_n(input.begin()+p,16,b.begin());cipher=b;aesDecryptBlock(b,schedule);for(int i=0;i<16;++i)b[i]^=prev[i];out.insert(out.end(),b.begin(),b.end());prev=cipher;}
    const auto pad=out.back();if(pad==0||pad>16||pad>out.size()||!std::all_of(out.end()-pad,out.end(),[pad](auto v){return v==pad;}))throw PdfException(PdfErrorCode::MalformedObject,"Invalid AES padding.");out.resize(out.size()-pad);return out;
}

std::array<std::uint8_t,16> ownerKey(std::string_view password, std::size_t keyLength){auto digest=md5(padPassword(password));if(keyLength==16U)for(int i=0;i<50;++i)digest=md5(digest);return digest;}
std::array<std::uint8_t,16> fileKeyFromPadded(std::span<const std::uint8_t,32> padded,
    std::span<const std::uint8_t,32> owner,std::int32_t permissions,std::span<const std::uint8_t> id,bool encryptMetadata,std::size_t keyLength){
    std::vector<std::uint8_t>d;d.reserve(32+32+4+id.size()+4);d.insert(d.end(),padded.begin(),padded.end());d.insert(d.end(),owner.begin(),owner.end());const auto p=static_cast<std::uint32_t>(permissions);for(unsigned i=0;i<4;++i)d.push_back(static_cast<std::uint8_t>(p>>(8U*i)));d.insert(d.end(),id.begin(),id.end());if(!encryptMetadata&&keyLength==16U)d.insert(d.end(),4,0xffU);auto digest=md5(d);if(keyLength==16U)for(int i=0;i<50;++i)digest=md5(digest);return digest;
}
std::array<std::uint8_t,32> computeUser(std::span<const std::uint8_t,16> key,std::span<const std::uint8_t> id,std::size_t keyLength){std::vector<std::uint8_t>d(PasswordPadding.begin(),PasswordPadding.end());d.insert(d.end(),id.begin(),id.end());auto encrypted=rc4(md5(d),std::span<const std::uint8_t>(key).first(keyLength));if(keyLength==16U){std::array<std::uint8_t,16>pass{};for(std::uint8_t i=1;i<=19;++i){for(std::size_t j=0;j<16;++j)pass[j]=key[j]^i;encrypted=rc4(encrypted,pass);}}std::array<std::uint8_t,32>u{};std::copy(encrypted.begin(),encrypted.end(),u.begin());return u;}
bool matchesUser(std::span<const std::uint8_t,16> key,std::span<const std::uint8_t,32> expected,std::span<const std::uint8_t> id,std::size_t keyLength){const auto u=computeUser(key,id,keyLength);return std::equal(u.begin(),u.begin()+16,expected.begin());}

std::string regexValue(std::string_view source,const char* pattern){const std::string value(source);std::smatch m;if(!std::regex_search(value,m,std::regex(pattern)))throw PdfException(PdfErrorCode::UnsupportedFeature,"Malformed PDF encryption dictionary.");return m[1].str();}
std::vector<std::uint8_t> decodeLiteral(std::string_view s,std::size_t&pos){std::vector<std::uint8_t>out;++pos;int depth=1;while(pos<s.size()&&depth){char c=s[pos++];if(c=='\\'&&pos<s.size()){char e=s[pos++];if(e>='0'&&e<='7'){int v=e-'0';for(int n=0;n<2&&pos<s.size()&&s[pos]>='0'&&s[pos]<='7';++n)v=v*8+(s[pos++]-'0');out.push_back(static_cast<std::uint8_t>(v));}else if(e=='n')out.push_back('\n');else if(e=='r')out.push_back('\r');else if(e=='t')out.push_back('\t');else if(e=='b')out.push_back('\b');else if(e=='f')out.push_back('\f');else if(e=='\r'){if(pos<s.size()&&s[pos]=='\n')++pos;}else if(e!='\n')out.push_back(static_cast<std::uint8_t>(e));}
        else if(c=='('){++depth;out.push_back('(');}else if(c==')'){if(--depth)out.push_back(')');}else out.push_back(static_cast<std::uint8_t>(c));}if(depth)throw PdfException(PdfErrorCode::MalformedObject,"Unterminated string in encrypted object.");return out;}
std::vector<std::uint8_t> decodeHexAt(std::string_view s,std::size_t&pos){const auto begin=++pos;const auto end=s.find('>',begin);if(end==std::string_view::npos)throw PdfException(PdfErrorCode::MalformedObject,"Unterminated hex string in encrypted object.");auto bytes=ParsePdfHex(s.substr(begin,end-begin));pos=end+1;return bytes;}

void replaceStreamLength(std::string& dictionary, const std::size_t length) {
    const auto key = dictionary.find("/Length");
    if (key == std::string::npos) return;
    std::size_t begin = key + 7U;
    while (begin < dictionary.size() && std::isspace(static_cast<unsigned char>(dictionary[begin]))) ++begin;
    std::size_t end = begin;
    while (end < dictionary.size() && std::isdigit(static_cast<unsigned char>(dictionary[end]))) ++end;
    if (end == begin) return;

    // Normalize both direct lengths and the common indirect "n g R" form.
    std::size_t candidate = end;
    while (candidate < dictionary.size() && std::isspace(static_cast<unsigned char>(dictionary[candidate]))) ++candidate;
    const std::size_t generationBegin = candidate;
    while (candidate < dictionary.size() && std::isdigit(static_cast<unsigned char>(dictionary[candidate]))) ++candidate;
    if (candidate > generationBegin) {
        while (candidate < dictionary.size() && std::isspace(static_cast<unsigned char>(dictionary[candidate]))) ++candidate;
        if (candidate < dictionary.size() && dictionary[candidate] == 'R') end = candidate + 1U;
    }
    dictionary.replace(begin, end - begin, std::to_string(length));
}

std::optional<std::size_t> directStreamLength(const std::string_view dictionary) {
    const auto key = dictionary.find("/Length");
    if (key == std::string_view::npos) return std::nullopt;
    std::size_t begin = key + 7U;
    while (begin < dictionary.size() && std::isspace(static_cast<unsigned char>(dictionary[begin]))) ++begin;
    std::size_t end = begin;
    while (end < dictionary.size() && std::isdigit(static_cast<unsigned char>(dictionary[end]))) ++end;
    if (end == begin) return std::nullopt;
    std::size_t candidate = end;
    while (candidate < dictionary.size() && std::isspace(static_cast<unsigned char>(dictionary[candidate]))) ++candidate;
    if (candidate < dictionary.size() && std::isdigit(static_cast<unsigned char>(dictionary[candidate]))) {
        return std::nullopt; // indirect /Length object
    }
    std::size_t value{};
    const auto parsed = std::from_chars(dictionary.data() + begin, dictionary.data() + end, value);
    return parsed.ec == std::errc{} ? std::optional<std::size_t>(value) : std::nullopt;
}

} // namespace

std::array<std::uint8_t,16> GeneratePdfFileId(){std::array<std::uint8_t,16>id{};thread_local std::random_device random;for(auto&v:id)v=static_cast<std::uint8_t>(random());return id;}
std::string PdfHex(std::span<const std::uint8_t>bytes){static constexpr char hex[]="0123456789ABCDEF";std::string out;out.reserve(bytes.size()*2U);for(auto b:bytes){out.push_back(hex[b>>4U]);out.push_back(hex[b&15U]);}return out;}
std::vector<std::uint8_t> ParsePdfHex(std::string_view hex){std::vector<std::uint8_t>out;int high=-1;for(char c:hex){int v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(v<0)continue;if(high<0)high=v;else{out.push_back(static_cast<std::uint8_t>((high<<4)|v));high=-1;}}if(high>=0)out.push_back(static_cast<std::uint8_t>(high<<4));return out;}
std::int32_t BuildPdfPermissionBits(const PdfPermissions&p){std::uint32_t value=0xfffff0c0U;if(p.print)value|=4U;if(p.modify)value|=8U;if(p.copy)value|=16U;if(p.annotate)value|=32U;if(p.fillForms)value|=256U;if(p.accessibility)value|=512U;if(p.assemble)value|=1024U;if(p.highQualityPrint)value|=2048U;return static_cast<std::int32_t>(value);}

PdfStandardSecurity PdfStandardSecurity::Create(const PdfEncryptionOptions&o,std::span<const std::uint8_t,16>id){PdfStandardSecurity s;s.algorithm_=o.algorithm;s.permissions_=BuildPdfPermissionBits(o.permissions);s.encryptMetadata_=o.encryptMetadata;std::copy(id.begin(),id.end(),s.fileId_.begin());const std::size_t keyLength=o.algorithm==PdfEncryptionAlgorithm::Rc4_40?5U:16U;const auto ownerPassword=o.ownerPassword.empty()?o.userPassword:o.ownerPassword;const auto key=ownerKey(ownerPassword,keyLength);auto owner=rc4(padPassword(o.userPassword),std::span<const std::uint8_t>(key).first(keyLength));if(keyLength==16U){std::array<std::uint8_t,16>pass{};for(std::uint8_t i=1;i<=19;++i){for(std::size_t j=0;j<16;++j)pass[j]=key[j]^i;owner=rc4(owner,pass);}}std::copy(owner.begin(),owner.end(),s.ownerEntry_.begin());const auto padded=padPassword(o.userPassword);s.fileKey_=fileKeyFromPadded(padded,s.ownerEntry_,s.permissions_,s.fileId_,s.encryptMetadata_,keyLength);s.userEntry_=computeUser(s.fileKey_,s.fileId_,keyLength);s.authenticated_=true;s.ownerAuthenticated_=true;return s;}

PdfStandardSecurity PdfStandardSecurity::Parse(std::string_view d,std::span<const std::uint8_t>id){PdfStandardSecurity s;const auto filter=regexValue(d,R"(/Filter\s*/([^\s/<>()\[\]]+))");if(filter!="Standard")throw PdfException(PdfErrorCode::UnsupportedFeature,"Only the PDF Standard Security Handler is supported.");const int r=std::stoi(regexValue(d,R"(/R\s+(-?\d+))"));const int v=std::stoi(regexValue(d,R"(/V\s+(-?\d+))"));if(r==4&&v==4){if(d.find("/CFM /AESV2")==std::string_view::npos)throw PdfException(PdfErrorCode::UnsupportedEncryption,"Only AESV2 crypt filters are supported for revision 4 encryption.");s.algorithm_=PdfEncryptionAlgorithm::Aes128;}else if(r==3&&(v==2||v==3))s.algorithm_=PdfEncryptionAlgorithm::Rc4_128;else if(r==2&&v==1)s.algorithm_=PdfEncryptionAlgorithm::Rc4_40;else throw PdfException(PdfErrorCode::UnsupportedEncryption,"Unsupported PDF encryption revision. Supported: AES-128 (R4), RC4-128 (R3), and RC4-40 (R2).");s.permissions_=static_cast<std::int32_t>(std::stoll(regexValue(d,R"(/P\s+(-?\d+))")));s.encryptMetadata_=d.find("/EncryptMetadata false")==std::string_view::npos;auto o=ParsePdfHex(regexValue(d,R"(/O\s*<([0-9A-Fa-f\s]+)>)"));auto u=ParsePdfHex(regexValue(d,R"(/U\s*<([0-9A-Fa-f\s]+)>)"));if(o.size()!=32||u.size()!=32||id.size()<16)throw PdfException(PdfErrorCode::UnsupportedEncryption,"Malformed encryption owner/user key or file ID.");std::copy_n(o.begin(),32,s.ownerEntry_.begin());std::copy_n(u.begin(),32,s.userEntry_.begin());std::copy_n(id.begin(),16,s.fileId_.begin());return s;}

bool PdfStandardSecurity::Authenticate(std::string_view password){const std::size_t keyLength=algorithm_==PdfEncryptionAlgorithm::Rc4_40?5U:16U;const auto padded=padPassword(password);auto candidate=fileKeyFromPadded(padded,ownerEntry_,permissions_,fileId_,encryptMetadata_,keyLength);if(matchesUser(candidate,userEntry_,fileId_,keyLength)){fileKey_=candidate;authenticated_=true;ownerAuthenticated_=false;return true;}const auto key=ownerKey(password,keyLength);std::vector<std::uint8_t>recovered(ownerEntry_.begin(),ownerEntry_.end());if(keyLength==16U){std::array<std::uint8_t,16>pass{};for(int i=19;i>=0;--i){for(std::size_t j=0;j<16;++j)pass[j]=key[j]^static_cast<std::uint8_t>(i);recovered=rc4(recovered,pass);}}else recovered=rc4(recovered,std::span<const std::uint8_t>(key).first(keyLength));std::array<std::uint8_t,32>ownerRecovered{};std::copy_n(recovered.begin(),32,ownerRecovered.begin());candidate=fileKeyFromPadded(ownerRecovered,ownerEntry_,permissions_,fileId_,encryptMetadata_,keyLength);if(matchesUser(candidate,userEntry_,fileId_,keyLength)){fileKey_=candidate;authenticated_=true;ownerAuthenticated_=true;return true;}return false;}

std::string PdfStandardSecurity::EncryptionDictionary()const{std::string d="<< /Filter /Standard ";if(algorithm_==PdfEncryptionAlgorithm::Aes128)d+="/V 4 /R 4 /Length 128 /CF << /StdCF << /CFM /AESV2 /AuthEvent /DocOpen /Length 16 >> >> /StmF /StdCF /StrF /StdCF ";else if(algorithm_==PdfEncryptionAlgorithm::Rc4_128)d+="/V 2 /R 3 /Length 128 ";else d+="/V 1 /R 2 /Length 40 ";d+="/O <"+PdfHex(ownerEntry_)+"> /U <"+PdfHex(userEntry_)+"> /P "+std::to_string(permissions_);if(!encryptMetadata_&&algorithm_!=PdfEncryptionAlgorithm::Rc4_40)d+=" /EncryptMetadata false";d+=" >>";return d;}

std::vector<std::uint8_t> PdfStandardSecurity::Crypt(std::span<const std::uint8_t>input,std::uint32_t objectNumber,std::uint16_t generation,bool encrypt)const{if(!authenticated_)throw PdfException(PdfErrorCode::PasswordRequired,"A valid password is required to access encrypted PDF objects.");const std::size_t keyLength=algorithm_==PdfEncryptionAlgorithm::Rc4_40?5U:16U;std::vector<std::uint8_t>material(fileKey_.begin(),fileKey_.begin()+static_cast<std::ptrdiff_t>(keyLength));material.push_back(static_cast<std::uint8_t>(objectNumber));material.push_back(static_cast<std::uint8_t>(objectNumber>>8U));material.push_back(static_cast<std::uint8_t>(objectNumber>>16U));material.push_back(static_cast<std::uint8_t>(generation));material.push_back(static_cast<std::uint8_t>(generation>>8U));if(algorithm_==PdfEncryptionAlgorithm::Aes128){material.insert(material.end(),{'s','A','l','T'});}const auto digest=md5(material);if(algorithm_!=PdfEncryptionAlgorithm::Aes128)return rc4(input,std::span<const std::uint8_t>(digest).first(std::min<std::size_t>(16U,keyLength+5U)));return aesCbc(input,digest,encrypt);}

std::string PdfStandardSecurity::TransformObject(std::string_view object,std::uint32_t number,std::uint16_t generation,bool encrypt)const{
    auto transformText=[&](std::string_view text){std::string out;out.reserve(text.size()+32);std::size_t p=0;while(p<text.size()){if(text[p]=='%'){do{out.push_back(text[p++]);}while(p<text.size()&&text[p]!='\r'&&text[p]!='\n');continue;}if(text[p]=='('){auto bytes=decodeLiteral(text,p);auto crypt=Crypt(bytes,number,generation,encrypt);out+='<'+PdfHex(crypt)+'>';continue;}if(text[p]=='<'&&p+1<text.size()&&text[p+1]!='<'&&(p==0||text[p-1]!='<')){auto bytes=decodeHexAt(text,p);auto crypt=Crypt(bytes,number,generation,encrypt);out+='<'+PdfHex(crypt)+'>';continue;}out.push_back(text[p++]);}return out;};
    std::size_t keyword=std::string_view::npos,dataStart{};for(std::size_t p=0;(p=object.find("stream",p))!=std::string_view::npos;++p){const auto after=p+6U;const bool left=p==0||object[p-1]=='\n'||object[p-1]=='\r'||object[p-1]==' '||object[p-1]=='>';if(left&&after<object.size()&&(object[after]=='\r'||object[after]=='\n')){keyword=p;dataStart=after;if(object[dataStart]=='\r')++dataStart;if(dataStart<object.size()&&object[dataStart]=='\n')++dataStart;break;}}
    if(keyword==std::string_view::npos)return transformText(object);
    const auto end=object.rfind("endstream");if(end==std::string_view::npos||end<dataStart)throw PdfException(PdfErrorCode::MalformedObject,"Encrypted stream has no endstream marker.");std::size_t dataEnd=end;if(const auto length=directStreamLength(object.substr(0,keyword));length&&*length<=end-dataStart)dataEnd=dataStart+*length;else while(dataEnd>dataStart&&(object[dataEnd-1]=='\n'||object[dataEnd-1]=='\r'))--dataEnd;
    std::string prefix=transformText(object.substr(0,dataStart));std::vector<std::uint8_t>stream(object.begin()+static_cast<std::ptrdiff_t>(dataStart),object.begin()+static_cast<std::ptrdiff_t>(dataEnd));if(!( !encryptMetadata_ && object.substr(0,keyword).find("/Type /Metadata")!=std::string_view::npos))stream=Crypt(stream,number,generation,encrypt);
    replaceStreamLength(prefix,stream.size());std::string out;out.reserve(prefix.size()+stream.size()+object.size()-dataEnd);out=std::move(prefix);out.append(reinterpret_cast<const char*>(stream.data()),stream.size());out.append(object.substr(dataEnd));return out;
}

std::string PdfStandardSecurity::EncryptObject(std::string_view o,std::uint32_t n,std::uint16_t g)const{return TransformObject(o,n,g,true);}std::string PdfStandardSecurity::DecryptObject(std::string_view o,std::uint32_t n,std::uint16_t g)const{return TransformObject(o,n,g,false);}

} // namespace CPPPdf::Internal
