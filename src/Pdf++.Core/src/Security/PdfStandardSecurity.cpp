#include "Internal/Security/PdfStandardSecurity.hpp"
#include "Internal/Security/PdfCrypto.hpp"

#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cstring>
#include <limits>
#include <random>

#if defined(_WIN32)
// bcrypt.h is not self-contained: it expects Win32 scalar and pointer types
// (ULONG, PUCHAR, NTSTATUS, and related declarations) from windows.h.
// Including it directly produces a cascade of misleading MSVC errors such as
// "unknown override specifier" for cbSize/cbData/etc.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#elif defined(__linux__)
#  include <cerrno>
#  include <sys/random.h>
#elif defined(__APPLE__)
#  include <cstdlib>
#endif
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
    data.push_back(std::uint8_t{0x80});
    while ((data.size() % 64U) != 56U) data.push_back(std::uint8_t{0});
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


template <std::size_t N>
std::array<std::uint8_t, N> randomBytes() {
    static_assert(N <= static_cast<std::size_t>(std::numeric_limits<unsigned long>::max()));
    std::array<std::uint8_t, N> output{};
#if defined(_WIN32)
    const NTSTATUS status = BCryptGenRandom(
        nullptr, reinterpret_cast<PUCHAR>(output.data()), static_cast<ULONG>(N),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Windows CSPRNG failed while generating PDF security material.");
    }
#elif defined(__linux__)
    std::size_t offset = 0U;
    while (offset < output.size()) {
        const ssize_t count = ::getrandom(output.data() + offset, output.size() - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Linux CSPRNG failed while generating PDF security material.");
    }
#elif defined(__APPLE__)
    arc4random_buf(output.data(), output.size());
#else
    // std::random_device is the portable fallback. Supported release targets
    // use their operating-system CSPRNG branches above.
    std::random_device source;
    for (auto& value : output) value = static_cast<std::uint8_t>(source());
#endif
    return output;
}

std::vector<std::uint8_t> normalizedRevision6Password(const std::string_view password) {
    // ISO 32000-2 consumes the UTF-8 password byte sequence and limits it to
    // 127 bytes. The public API treats std::string passwords as UTF-8.
    const std::size_t count = std::min<std::size_t>(127U, password.size());
    return {reinterpret_cast<const std::uint8_t*>(password.data()),
            reinterpret_cast<const std::uint8_t*>(password.data()) + count};
}

bool constantTimeEqual(const std::span<const std::uint8_t> left,
                       const std::span<const std::uint8_t> right) {
    if (left.size() != right.size()) return false;
    std::uint8_t difference = 0U;
    for (std::size_t i = 0; i < left.size(); ++i) difference |= left[i] ^ right[i];
    return difference == 0U;
}

std::vector<std::uint8_t> aes128CbcNoPadding(
    const std::span<const std::uint8_t> input,
    const std::span<const std::uint8_t, 16> key,
    const std::span<const std::uint8_t, 16> iv,
    const bool encrypt) {
    if (input.size() % 16U != 0U) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "AES-128-CBC input is not block aligned.");
    }
    const auto schedule = expandKey(key);
    std::array<std::uint8_t, 16> previous{};
    std::copy(iv.begin(), iv.end(), previous.begin());
    std::vector<std::uint8_t> output;
    output.reserve(input.size());
    for (std::size_t offset = 0; offset < input.size(); offset += 16U) {
        std::array<std::uint8_t, 16> block{};
        std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset), 16U, block.begin());
        if (encrypt) {
            for (std::size_t i = 0; i < 16U; ++i) block[i] ^= previous[i];
            aesEncryptBlock(block, schedule);
            previous = block;
            output.insert(output.end(), block.begin(), block.end());
        } else {
            const auto ciphertext = block;
            aesDecryptBlock(block, schedule);
            for (std::size_t i = 0; i < 16U; ++i) block[i] ^= previous[i];
            previous = ciphertext;
            output.insert(output.end(), block.begin(), block.end());
        }
    }
    return output;
}

std::vector<std::uint8_t> aes256CbcNoPadding(
    const std::span<const std::uint8_t> input,
    const std::span<const std::uint8_t, 32> key,
    const std::span<const std::uint8_t, 16> iv,
    const bool encrypt) {
    return encrypt ? Aes256CbcEncrypt(key, iv, input)
                   : Aes256CbcDecrypt(key, iv, input);
}

std::vector<std::uint8_t> aes256ObjectCrypt(
    const std::span<const std::uint8_t> input,
    const std::span<const std::uint8_t, 32> key,
    const bool encrypt) {
    if (encrypt) {
        const auto iv = randomBytes<16>();
        std::vector<std::uint8_t> padded(input.begin(), input.end());
        const auto padding = static_cast<std::uint8_t>(16U - (padded.size() % 16U));
        padded.insert(padded.end(), padding, padding);
        auto encrypted = Aes256CbcEncrypt(key, iv, padded);
        std::vector<std::uint8_t> output(iv.begin(), iv.end());
        output.insert(output.end(), encrypted.begin(), encrypted.end());
        return output;
    }
    if (input.size() < 32U || input.size() % 16U != 0U) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Invalid AES-256 encrypted object length.");
    }
    std::array<std::uint8_t, 16> iv{};
    std::copy_n(input.begin(), 16U, iv.begin());
    const auto ciphertext = input.subspan(16U);
    auto output = Aes256CbcDecrypt(key, iv, ciphertext);
    if (output.empty()) {
        throw PdfException(PdfErrorCode::MalformedObject, "Missing AES-256 padding.");
    }
    const auto padding = output.back();
    if (padding == 0U || padding > 16U || padding > output.size() ||
        !std::all_of(output.end() - padding, output.end(),
                     [padding](const auto value) { return value == padding; })) {
        throw PdfException(PdfErrorCode::MalformedObject, "Invalid AES-256 padding.");
    }
    output.resize(output.size() - padding);
    return output;
}

int firstSixteenBytesMod3(const std::span<const std::uint8_t> bytes) {
    std::uint32_t remainder = 0U;
    for (std::size_t i = 0; i < 16U; ++i) {
        remainder = (remainder * 256U + bytes[i]) % 3U;
    }
    return static_cast<int>(remainder);
}

std::array<std::uint8_t, 32> revision6Hash(
    const std::span<const std::uint8_t> password,
    const std::span<const std::uint8_t, 8> salt,
    const std::span<const std::uint8_t> userEntry) {
    std::vector<std::uint8_t> initial;
    initial.reserve(password.size() + salt.size() + userEntry.size());
    initial.insert(initial.end(), password.begin(), password.end());
    initial.insert(initial.end(), salt.begin(), salt.end());
    initial.insert(initial.end(), userEntry.begin(), userEntry.end());

    const auto initialDigest = Sha256(initial);
    std::vector<std::uint8_t> digest(initialDigest.begin(), initialDigest.end());
    std::vector<std::uint8_t> encrypted;
    std::size_t round = 0U;
    do {
        std::vector<std::uint8_t> block;
        const std::size_t unitSize = password.size() + digest.size() + userEntry.size();
        block.reserve(unitSize * 64U);
        for (std::size_t repeat = 0; repeat < 64U; ++repeat) {
            block.insert(block.end(), password.begin(), password.end());
            block.insert(block.end(), digest.begin(), digest.end());
            block.insert(block.end(), userEntry.begin(), userEntry.end());
        }
        std::array<std::uint8_t, 16> key{};
        std::array<std::uint8_t, 16> iv{};
        std::copy_n(digest.begin(), 16U, key.begin());
        std::copy_n(digest.begin() + 16, 16U, iv.begin());
        encrypted = aes128CbcNoPadding(block, key, iv, true);
        switch (firstSixteenBytesMod3(encrypted)) {
        case 0: {
            const auto value = Sha256(encrypted);
            digest.assign(value.begin(), value.end());
            break;
        }
        case 1: {
            const auto value = Sha384(encrypted);
            digest.assign(value.begin(), value.end());
            break;
        }
        default: {
            const auto value = Sha512(encrypted);
            digest.assign(value.begin(), value.end());
            break;
        }
        }
        ++round;
    } while (round < 64U || round - 32U < encrypted.back());

    std::array<std::uint8_t, 32> output{};
    std::copy_n(digest.begin(), output.size(), output.begin());
    return output;
}

std::uint32_t readLittleEndian32(const std::span<const std::uint8_t, 4> bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}


} // namespace

std::array<std::uint8_t, 16> GeneratePdfFileId() {
    return randomBytes<16>();
}

std::string PdfHex(const std::span<const std::uint8_t> bytes) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(bytes.size() * 2U);
    for (const auto value : bytes) {
        output.push_back(hex[value >> 4U]);
        output.push_back(hex[value & 15U]);
    }
    return output;
}

std::vector<std::uint8_t> ParsePdfHex(const std::string_view hex) {
    std::vector<std::uint8_t> output;
    int high = -1;
    for (const char character : hex) {
        const int value = character >= '0' && character <= '9' ? character - '0'
            : character >= 'a' && character <= 'f' ? character - 'a' + 10
            : character >= 'A' && character <= 'F' ? character - 'A' + 10 : -1;
        if (value < 0) continue;
        if (high < 0) high = value;
        else {
            output.push_back(static_cast<std::uint8_t>((high << 4) | value));
            high = -1;
        }
    }
    if (high >= 0) output.push_back(static_cast<std::uint8_t>(high << 4));
    return output;
}

std::int32_t BuildPdfPermissionBits(const PdfPermissions& permissions) {
    std::uint32_t value = 0xfffff0c0U;
    if (permissions.print) value |= 4U;
    if (permissions.modify) value |= 8U;
    if (permissions.copy) value |= 16U;
    if (permissions.annotate) value |= 32U;
    if (permissions.fillForms) value |= 256U;
    if (permissions.accessibility) value |= 512U;
    if (permissions.assemble) value |= 1024U;
    if (permissions.highQualityPrint) value |= 2048U;
    return static_cast<std::int32_t>(value);
}

PdfStandardSecurity PdfStandardSecurity::Create(
    const PdfEncryptionOptions& options,
    const std::span<const std::uint8_t, 16> fileId) {
    PdfStandardSecurity security;
    security.algorithm_ = options.algorithm;
    security.permissions_ = BuildPdfPermissionBits(options.permissions);
    security.encryptMetadata_ = options.encryptMetadata;
    std::copy(fileId.begin(), fileId.end(), security.fileId_.begin());

    if (options.algorithm == PdfEncryptionAlgorithm::Aes256) {
        security.fileKey_ = randomBytes<32>();
        const auto userPassword = normalizedRevision6Password(options.userPassword);
        const auto ownerPassword = normalizedRevision6Password(
            options.ownerPassword.empty() ? options.userPassword : options.ownerPassword);
        const auto userSalts = randomBytes<16>();
        const auto ownerSalts = randomBytes<16>();

        std::array<std::uint8_t, 8> userValidationSalt{};
        std::array<std::uint8_t, 8> userKeySalt{};
        std::array<std::uint8_t, 8> ownerValidationSalt{};
        std::array<std::uint8_t, 8> ownerKeySalt{};
        std::copy_n(userSalts.begin(), 8U, userValidationSalt.begin());
        std::copy_n(userSalts.begin() + 8, 8U, userKeySalt.begin());
        std::copy_n(ownerSalts.begin(), 8U, ownerValidationSalt.begin());
        std::copy_n(ownerSalts.begin() + 8, 8U, ownerKeySalt.begin());

        const auto userValidationHash = revision6Hash(userPassword, userValidationSalt, {});
        std::copy(userValidationHash.begin(), userValidationHash.end(), security.userEntry_.begin());
        std::copy(userValidationSalt.begin(), userValidationSalt.end(), security.userEntry_.begin() + 32);
        std::copy(userKeySalt.begin(), userKeySalt.end(), security.userEntry_.begin() + 40);

        const auto userKeyHash = revision6Hash(userPassword, userKeySalt, {});
        const std::array<std::uint8_t, 16> zeroIv{};
        const auto encryptedUserKey = aes256CbcNoPadding(
            security.fileKey_, userKeyHash, zeroIv, true);
        std::copy_n(encryptedUserKey.begin(), 32U, security.userEncryptedKey_.begin());

        const auto ownerValidationHash = revision6Hash(
            ownerPassword, ownerValidationSalt, security.userEntry_);
        std::copy(ownerValidationHash.begin(), ownerValidationHash.end(), security.ownerEntry_.begin());
        std::copy(ownerValidationSalt.begin(), ownerValidationSalt.end(), security.ownerEntry_.begin() + 32);
        std::copy(ownerKeySalt.begin(), ownerKeySalt.end(), security.ownerEntry_.begin() + 40);

        const auto ownerKeyHash = revision6Hash(ownerPassword, ownerKeySalt, security.userEntry_);
        const auto encryptedOwnerKey = aes256CbcNoPadding(
            security.fileKey_, ownerKeyHash, zeroIv, true);
        std::copy_n(encryptedOwnerKey.begin(), 32U, security.ownerEncryptedKey_.begin());

        std::array<std::uint8_t, 16> permissionsBlock{};
        const auto permissionBits = static_cast<std::uint32_t>(security.permissions_);
        permissionsBlock[0] = static_cast<std::uint8_t>(permissionBits);
        permissionsBlock[1] = static_cast<std::uint8_t>(permissionBits >> 8U);
        permissionsBlock[2] = static_cast<std::uint8_t>(permissionBits >> 16U);
        permissionsBlock[3] = static_cast<std::uint8_t>(permissionBits >> 24U);
        std::fill(permissionsBlock.begin() + 4, permissionsBlock.begin() + 8, 0xffU);
        permissionsBlock[8] = security.encryptMetadata_ ? 'T' : 'F';
        permissionsBlock[9] = 'a';
        permissionsBlock[10] = 'd';
        permissionsBlock[11] = 'b';
        const auto randomTail = randomBytes<4>();
        std::copy(randomTail.begin(), randomTail.end(), permissionsBlock.begin() + 12);
        security.encryptedPermissions_ = Aes256EncryptBlock(security.fileKey_, permissionsBlock);
        security.authenticated_ = true;
        security.ownerAuthenticated_ = true;
        return security;
    }

    const std::size_t keyLength = options.algorithm == PdfEncryptionAlgorithm::Rc4_40 ? 5U : 16U;
    const auto ownerPassword = options.ownerPassword.empty() ? options.userPassword : options.ownerPassword;
    const auto key = ownerKey(ownerPassword, keyLength);
    auto owner = rc4(padPassword(options.userPassword),
                     std::span<const std::uint8_t>(key).first(keyLength));
    if (keyLength == 16U) {
        std::array<std::uint8_t, 16> pass{};
        for (std::uint8_t i = 1U; i <= 19U; ++i) {
            for (std::size_t j = 0; j < 16U; ++j) pass[j] = key[j] ^ i;
            owner = rc4(owner, pass);
        }
    }
    std::copy_n(owner.begin(), 32U, security.ownerEntry_.begin());
    const auto padded = padPassword(options.userPassword);
    const auto legacyFileKey = fileKeyFromPadded(
        padded, std::span<const std::uint8_t, 32>(security.ownerEntry_.data(), 32U),
        security.permissions_, security.fileId_, security.encryptMetadata_, keyLength);
    std::copy(legacyFileKey.begin(), legacyFileKey.end(), security.fileKey_.begin());
    const auto user = computeUser(legacyFileKey, security.fileId_, keyLength);
    std::copy(user.begin(), user.end(), security.userEntry_.begin());
    security.authenticated_ = true;
    security.ownerAuthenticated_ = true;
    return security;
}

PdfStandardSecurity PdfStandardSecurity::Parse(
    const std::string_view dictionary,
    const std::span<const std::uint8_t> fileId) {
    PdfStandardSecurity security;
    const auto filter = regexValue(dictionary, R"(/Filter\s*/([^\s/<>()\[\]]+))");
    if (filter != "Standard") {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Only the PDF Standard Security Handler is supported.");
    }
    const int revision = std::stoi(regexValue(dictionary, R"(/R\s+(-?\d+))"));
    const int version = std::stoi(regexValue(dictionary, R"(/V\s+(-?\d+))"));
    if (revision == 6 && version == 5) {
        if (dictionary.find("/CFM /AESV3") == std::string_view::npos) {
            throw PdfException(PdfErrorCode::UnsupportedEncryption,
                               "Revision 6 requires an AESV3 crypt filter.");
        }
        security.algorithm_ = PdfEncryptionAlgorithm::Aes256;
    } else if (revision == 4 && version == 4) {
        if (dictionary.find("/CFM /AESV2") == std::string_view::npos) {
            throw PdfException(PdfErrorCode::UnsupportedEncryption,
                               "Only AESV2 crypt filters are supported for revision 4 encryption.");
        }
        security.algorithm_ = PdfEncryptionAlgorithm::Aes128;
    } else if (revision == 3 && (version == 2 || version == 3)) {
        security.algorithm_ = PdfEncryptionAlgorithm::Rc4_128;
    } else if (revision == 2 && version == 1) {
        security.algorithm_ = PdfEncryptionAlgorithm::Rc4_40;
    } else {
        throw PdfException(PdfErrorCode::UnsupportedEncryption,
            "Unsupported PDF encryption revision. Supported: AES-256 (R6), AES-128 (R4), "
            "RC4-128 (R3), and RC4-40 (R2).");
    }

    security.permissions_ = static_cast<std::int32_t>(
        std::stoll(regexValue(dictionary, R"(/P\s+(-?\d+))")));
    security.encryptMetadata_ = dictionary.find("/EncryptMetadata false") == std::string_view::npos;
    const auto owner = ParsePdfHex(regexValue(dictionary, R"(/O\s*<([0-9A-Fa-f\s]+)>)"));
    const auto user = ParsePdfHex(regexValue(dictionary, R"(/U\s*<([0-9A-Fa-f\s]+)>)"));
    const std::size_t expectedEntrySize = security.algorithm_ == PdfEncryptionAlgorithm::Aes256 ? 48U : 32U;
    if (owner.size() != expectedEntrySize || user.size() != expectedEntrySize) {
        throw PdfException(PdfErrorCode::UnsupportedEncryption,
                           "Malformed encryption owner or user entry.");
    }
    std::copy(owner.begin(), owner.end(), security.ownerEntry_.begin());
    std::copy(user.begin(), user.end(), security.userEntry_.begin());

    if (security.algorithm_ == PdfEncryptionAlgorithm::Aes256) {
        const auto ownerKey = ParsePdfHex(regexValue(dictionary, R"(/OE\s*<([0-9A-Fa-f\s]+)>)"));
        const auto userKey = ParsePdfHex(regexValue(dictionary, R"(/UE\s*<([0-9A-Fa-f\s]+)>)"));
        const auto permissions = ParsePdfHex(regexValue(dictionary, R"(/Perms\s*<([0-9A-Fa-f\s]+)>)"));
        if (ownerKey.size() != 32U || userKey.size() != 32U || permissions.size() != 16U) {
            throw PdfException(PdfErrorCode::UnsupportedEncryption,
                               "Malformed AES-256 /OE, /UE, or /Perms entry.");
        }
        std::copy(ownerKey.begin(), ownerKey.end(), security.ownerEncryptedKey_.begin());
        std::copy(userKey.begin(), userKey.end(), security.userEncryptedKey_.begin());
        std::copy(permissions.begin(), permissions.end(), security.encryptedPermissions_.begin());
    }
    if (fileId.size() >= 16U) std::copy_n(fileId.begin(), 16U, security.fileId_.begin());
    else if (security.algorithm_ != PdfEncryptionAlgorithm::Aes256) {
        throw PdfException(PdfErrorCode::UnsupportedEncryption,
                           "Encrypted PDF trailer does not contain a valid file ID.");
    }
    return security;
}

bool PdfStandardSecurity::AuthenticateAes256(const std::string_view password, const bool owner) {
    const auto normalized = normalizedRevision6Password(password);
    const auto& entry = owner ? ownerEntry_ : userEntry_;
    std::array<std::uint8_t, 8> validationSalt{};
    std::array<std::uint8_t, 8> keySalt{};
    std::copy_n(entry.begin() + 32, 8U, validationSalt.begin());
    std::copy_n(entry.begin() + 40, 8U, keySalt.begin());
    const auto userVector = owner
        ? std::span<const std::uint8_t>(userEntry_.data(), userEntry_.size())
        : std::span<const std::uint8_t>{};
    const auto validation = revision6Hash(normalized, validationSalt, userVector);
    if (!constantTimeEqual(validation,
            std::span<const std::uint8_t>(entry.data(), 32U))) {
        return false;
    }
    const auto keyHash = revision6Hash(normalized, keySalt, userVector);
    const std::array<std::uint8_t, 16> zeroIv{};
    const auto encryptedKey = owner
        ? std::span<const std::uint8_t>(ownerEncryptedKey_)
        : std::span<const std::uint8_t>(userEncryptedKey_);
    const auto fileKey = aes256CbcNoPadding(encryptedKey, keyHash, zeroIv, false);
    std::copy_n(fileKey.begin(), 32U, fileKey_.begin());
    if (!ValidateAes256Permissions()) {
        std::fill(fileKey_.begin(), fileKey_.end(), 0U);
        return false;
    }
    authenticated_ = true;
    ownerAuthenticated_ = owner;
    return true;
}

bool PdfStandardSecurity::ValidateAes256Permissions() const {
    const auto plain = Aes256DecryptBlock(fileKey_, encryptedPermissions_);
    if (plain[9] != 'a' || plain[10] != 'd' || plain[11] != 'b') return false;
    if (!std::all_of(plain.begin() + 4, plain.begin() + 8,
                     [](const auto value) { return value == 0xffU; })) return false;
    const auto permissionValue = readLittleEndian32(
        std::span<const std::uint8_t, 4>(plain.data(), 4U));
    if (permissionValue != static_cast<std::uint32_t>(permissions_)) return false;
    const bool permissionsEncryptMetadata = plain[8] == 'T';
    if (plain[8] != 'T' && plain[8] != 'F') return false;
    return permissionsEncryptMetadata == encryptMetadata_;
}

bool PdfStandardSecurity::Authenticate(const std::string_view password) {
    if (algorithm_ == PdfEncryptionAlgorithm::Aes256) {
        // Owner authentication is attempted first, matching common readers and
        // preserving full permissions when owner/user passwords are identical.
        if (!password.empty() && AuthenticateAes256(password, true)) return true;
        return AuthenticateAes256(password, false);
    }

    const std::size_t keyLength = algorithm_ == PdfEncryptionAlgorithm::Rc4_40 ? 5U : 16U;
    const auto padded = padPassword(password);
    const auto ownerSpan = std::span<const std::uint8_t, 32>(ownerEntry_.data(), 32U);
    const auto userSpan = std::span<const std::uint8_t, 32>(userEntry_.data(), 32U);
    auto candidate = fileKeyFromPadded(
        padded, ownerSpan, permissions_, fileId_, encryptMetadata_, keyLength);
    if (matchesUser(candidate, userSpan, fileId_, keyLength)) {
        std::copy(candidate.begin(), candidate.end(), fileKey_.begin());
        authenticated_ = true;
        ownerAuthenticated_ = false;
        return true;
    }
    const auto key = ownerKey(password, keyLength);
    std::vector<std::uint8_t> recovered(ownerSpan.begin(), ownerSpan.end());
    if (keyLength == 16U) {
        std::array<std::uint8_t, 16> pass{};
        for (int i = 19; i >= 0; --i) {
            for (std::size_t j = 0; j < 16U; ++j) {
                pass[j] = key[j] ^ static_cast<std::uint8_t>(i);
            }
            recovered = rc4(recovered, pass);
        }
    } else {
        recovered = rc4(recovered, std::span<const std::uint8_t>(key).first(keyLength));
    }
    std::array<std::uint8_t, 32> ownerRecovered{};
    std::copy_n(recovered.begin(), 32U, ownerRecovered.begin());
    candidate = fileKeyFromPadded(
        ownerRecovered, ownerSpan, permissions_, fileId_, encryptMetadata_, keyLength);
    if (matchesUser(candidate, userSpan, fileId_, keyLength)) {
        std::copy(candidate.begin(), candidate.end(), fileKey_.begin());
        authenticated_ = true;
        ownerAuthenticated_ = true;
        return true;
    }
    return false;
}

std::string PdfStandardSecurity::EncryptionDictionary() const {
    std::string dictionary = "<< /Filter /Standard ";
    if (algorithm_ == PdfEncryptionAlgorithm::Aes256) {
        dictionary += "/V 5 /R 6 /Length 256 "
            "/CF << /StdCF << /CFM /AESV3 /AuthEvent /DocOpen /Length 32 >> >> "
            "/StmF /StdCF /StrF /StdCF /EFF /StdCF ";
    } else if (algorithm_ == PdfEncryptionAlgorithm::Aes128) {
        dictionary += "/V 4 /R 4 /Length 128 "
            "/CF << /StdCF << /CFM /AESV2 /AuthEvent /DocOpen /Length 16 >> >> "
            "/StmF /StdCF /StrF /StdCF ";
    } else if (algorithm_ == PdfEncryptionAlgorithm::Rc4_128) {
        dictionary += "/V 2 /R 3 /Length 128 ";
    } else {
        dictionary += "/V 1 /R 2 /Length 40 ";
    }
    const std::size_t entrySize = algorithm_ == PdfEncryptionAlgorithm::Aes256 ? 48U : 32U;
    dictionary += "/O <" + PdfHex(std::span<const std::uint8_t>(ownerEntry_.data(), entrySize)) +
                  "> /U <" + PdfHex(std::span<const std::uint8_t>(userEntry_.data(), entrySize)) + "> ";
    if (algorithm_ == PdfEncryptionAlgorithm::Aes256) {
        dictionary += "/OE <" + PdfHex(ownerEncryptedKey_) + "> /UE <" +
                      PdfHex(userEncryptedKey_) + "> /Perms <" +
                      PdfHex(encryptedPermissions_) + "> ";
    }
    dictionary += "/P " + std::to_string(permissions_);
    if (!encryptMetadata_ && algorithm_ != PdfEncryptionAlgorithm::Rc4_40) {
        dictionary += " /EncryptMetadata false";
    }
    dictionary += " >>";
    return dictionary;
}

std::vector<std::uint8_t> PdfStandardSecurity::Crypt(
    const std::span<const std::uint8_t> input,
    const std::uint32_t objectNumber,
    const std::uint16_t generation,
    const bool encrypt) const {
    if (!authenticated_) {
        throw PdfException(PdfErrorCode::PasswordRequired,
                           "A valid password is required to access encrypted PDF objects.");
    }
    if (algorithm_ == PdfEncryptionAlgorithm::Aes256) {
        return aes256ObjectCrypt(input, fileKey_, encrypt);
    }

    const std::size_t keyLength = algorithm_ == PdfEncryptionAlgorithm::Rc4_40 ? 5U : 16U;
    std::vector<std::uint8_t> material(fileKey_.begin(),
        fileKey_.begin() + static_cast<std::ptrdiff_t>(keyLength));
    material.push_back(static_cast<std::uint8_t>(objectNumber));
    material.push_back(static_cast<std::uint8_t>(objectNumber >> 8U));
    material.push_back(static_cast<std::uint8_t>(objectNumber >> 16U));
    material.push_back(static_cast<std::uint8_t>(generation));
    material.push_back(static_cast<std::uint8_t>(generation >> 8U));
    if (algorithm_ == PdfEncryptionAlgorithm::Aes128) {
        material.insert(material.end(), {'s', 'A', 'l', 'T'});
    }
    const auto digest = md5(material);
    if (algorithm_ != PdfEncryptionAlgorithm::Aes128) {
        return rc4(input, std::span<const std::uint8_t>(digest).first(
            std::min<std::size_t>(16U, keyLength + 5U)));
    }
    return aesCbc(input, digest, encrypt);
}

std::string PdfStandardSecurity::TransformObject(
    const std::string_view object,
    const std::uint32_t number,
    const std::uint16_t generation,
    const bool encrypt) const {
    const auto transformText = [&](const std::string_view text) {
        std::string output;
        output.reserve(text.size() + 32U);
        std::size_t position = 0U;
        while (position < text.size()) {
            if (text[position] == '%') {
                do output.push_back(text[position++]);
                while (position < text.size() && text[position] != '\r' && text[position] != '\n');
                continue;
            }
            if (text[position] == '(') {
                auto bytes = decodeLiteral(text, position);
                const auto crypt = Crypt(bytes, number, generation, encrypt);
                output += '<' + PdfHex(crypt) + '>';
                continue;
            }
            if (text[position] == '<' && position + 1U < text.size() &&
                text[position + 1U] != '<' && (position == 0U || text[position - 1U] != '<')) {
                auto bytes = decodeHexAt(text, position);
                const auto crypt = Crypt(bytes, number, generation, encrypt);
                output += '<' + PdfHex(crypt) + '>';
                continue;
            }
            output.push_back(text[position++]);
        }
        return output;
    };

    std::size_t keyword = std::string_view::npos;
    std::size_t dataStart = 0U;
    for (std::size_t position = 0U;
         (position = object.find("stream", position)) != std::string_view::npos;
         ++position) {
        const auto after = position + 6U;
        const bool left = position == 0U || object[position - 1U] == '\n' ||
            object[position - 1U] == '\r' || object[position - 1U] == ' ' ||
            object[position - 1U] == '>';
        if (left && after < object.size() &&
            (object[after] == '\r' || object[after] == '\n')) {
            keyword = position;
            dataStart = after;
            if (object[dataStart] == '\r') ++dataStart;
            if (dataStart < object.size() && object[dataStart] == '\n') ++dataStart;
            break;
        }
    }
    if (keyword == std::string_view::npos) return transformText(object);

    const auto end = object.rfind("endstream");
    if (end == std::string_view::npos || end < dataStart) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Encrypted stream has no endstream marker.");
    }
    std::size_t dataEnd = end;
    if (const auto length = directStreamLength(object.substr(0U, keyword));
        length && *length <= end - dataStart) {
        dataEnd = dataStart + *length;
    } else {
        while (dataEnd > dataStart &&
               (object[dataEnd - 1U] == '\n' || object[dataEnd - 1U] == '\r')) --dataEnd;
    }
    std::string prefix = transformText(object.substr(0U, dataStart));
    std::vector<std::uint8_t> stream(
        object.begin() + static_cast<std::ptrdiff_t>(dataStart),
        object.begin() + static_cast<std::ptrdiff_t>(dataEnd));
    if (!(!encryptMetadata_ &&
          object.substr(0U, keyword).find("/Type /Metadata") != std::string_view::npos)) {
        stream = Crypt(stream, number, generation, encrypt);
    }
    replaceStreamLength(prefix, stream.size());
    std::string output;
    output.reserve(prefix.size() + stream.size() + object.size() - dataEnd);
    output = std::move(prefix);
    output.append(reinterpret_cast<const char*>(stream.data()), stream.size());
    output.append(object.substr(dataEnd));
    return output;
}

std::string PdfStandardSecurity::EncryptObject(
    const std::string_view object,
    const std::uint32_t number,
    const std::uint16_t generation) const {
    return TransformObject(object, number, generation, true);
}

std::string PdfStandardSecurity::DecryptObject(
    const std::string_view object,
    const std::uint32_t number,
    const std::uint16_t generation) const {
    return TransformObject(object, number, generation, false);
}

} // namespace CPPPdf::Internal
