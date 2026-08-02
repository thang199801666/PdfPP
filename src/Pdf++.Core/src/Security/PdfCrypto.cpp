#include "Internal/Security/PdfCrypto.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace CPPPdf::Internal {
namespace {

constexpr std::array<std::uint32_t, 64> K{
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};

std::uint8_t xtime(std::uint8_t x) { return static_cast<std::uint8_t>((x << 1U) ^ ((x & 0x80U) ? 0x1bU : 0U)); }
std::uint8_t mul(std::uint8_t a, std::uint8_t b) { std::uint8_t r{}; while (b) { if (b & 1U) r ^= a; a = xtime(a); b >>= 1U; } return r; }
std::uint8_t gfPow(std::uint8_t a, unsigned exponent) { std::uint8_t r = 1U; while (exponent) { if (exponent & 1U) r = mul(r, a); a = mul(a, a); exponent >>= 1U; } return r; }
std::uint8_t sbox(std::uint8_t x) { const auto inv = static_cast<std::uint8_t>(x == 0 ? 0U : gfPow(x, 254U)); const auto rot = [](std::uint8_t v, unsigned n) { return static_cast<std::uint8_t>((v << n) | (v >> (8U - n))); }; return static_cast<std::uint8_t>(inv ^ rot(inv, 1) ^ rot(inv, 2) ^ rot(inv, 3) ^ rot(inv, 4) ^ 0x63U); }
std::uint8_t invSbox(std::uint8_t x) { for (unsigned i = 0; i < 256U; ++i) if (sbox(static_cast<std::uint8_t>(i)) == x) return static_cast<std::uint8_t>(i); return 0; }
std::array<std::uint8_t, 240> expand(std::span<const std::uint8_t, 32> key) {
    std::array<std::uint8_t, 240> out{}; std::copy(key.begin(), key.end(), out.begin()); std::uint8_t rcon = 1U;
    for (std::size_t n = 32; n < out.size(); n += 4) { std::array<std::uint8_t, 4> t{}; std::copy_n(out.begin() + n - 4, 4, t.begin());
        if (n % 32 == 0) { const auto q = t[0]; t[0] = sbox(t[1]) ^ rcon; t[1] = sbox(t[2]); t[2] = sbox(t[3]); t[3] = sbox(q); rcon = xtime(rcon); }
        else if (n % 32 == 16) for (auto& v : t) v = sbox(v);
        for (auto v : t) { out[n] = out[n - 32] ^ v; ++n; }
        n -= 4;
    } return out;
}
void addKey(std::array<std::uint8_t,16>& s, const std::array<std::uint8_t,240>& w, int round) { for (int i=0;i<16;++i) s[i] ^= w[round*16+i]; }
void shiftRows(std::array<std::uint8_t,16>& s) { const auto t=s; s[1]=t[5];s[5]=t[9];s[9]=t[13];s[13]=t[1];s[2]=t[10];s[6]=t[14];s[10]=t[2];s[14]=t[6];s[3]=t[15];s[7]=t[3];s[11]=t[7];s[15]=t[11]; }
void invShiftRows(std::array<std::uint8_t,16>& s) { const auto t=s; s[1]=t[13];s[5]=t[1];s[9]=t[5];s[13]=t[9];s[2]=t[10];s[6]=t[14];s[10]=t[2];s[14]=t[6];s[3]=t[7];s[7]=t[11];s[11]=t[15];s[15]=t[3]; }
void mix(std::array<std::uint8_t,16>& s) { for(int c=0;c<4;++c){const int i=4*c;const auto a=s[i],b=s[i+1],c2=s[i+2],d=s[i+3];s[i]=mul(a,2)^mul(b,3)^c2^d;s[i+1]=a^mul(b,2)^mul(c2,3)^d;s[i+2]=a^b^mul(c2,2)^mul(d,3);s[i+3]=mul(a,3)^b^c2^mul(d,2);} }
void invMix(std::array<std::uint8_t,16>& s) { for(int c=0;c<4;++c){const int i=4*c;const auto a=s[i],b=s[i+1],c2=s[i+2],d=s[i+3];s[i]=mul(a,14)^mul(b,11)^mul(c2,13)^mul(d,9);s[i+1]=mul(a,9)^mul(b,14)^mul(c2,11)^mul(d,13);s[i+2]=mul(a,13)^mul(b,9)^mul(c2,14)^mul(d,11);s[i+3]=mul(a,11)^mul(b,13)^mul(c2,9)^mul(d,14);} }
}

std::array<std::uint8_t,32> Sha256(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> data(input.begin(), input.end()); const auto bits=static_cast<std::uint64_t>(data.size())*8U; data.push_back(0x80U); while(data.size()%64U!=56U)data.push_back(0); for(int i=7;i>=0;--i)data.push_back(static_cast<std::uint8_t>(bits>>(i*8)));
    std::array<std::uint32_t,8> h{0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    for(std::size_t off=0;off<data.size();off+=64){std::array<std::uint32_t,64>w{};for(int i=0;i<16;++i)for(int j=0;j<4;++j)w[i]|=static_cast<std::uint32_t>(data[off+i*4+j])<<(24-j*8);for(int i=16;i<64;++i){const auto s0=std::rotr(w[i-15],7)^std::rotr(w[i-15],18)^(w[i-15]>>3);const auto s1=std::rotr(w[i-2],17)^std::rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],x=h[7];for(int i=0;i<64;++i){const auto S1=std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);const auto ch=(e&f)^(~e&g);const auto t1=x+S1+ch+K[i]+w[i];const auto S0=std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);const auto maj=(a&b)^(a&c)^(b&c);const auto t2=S0+maj;x=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=x;}
    std::array<std::uint8_t,32> out{};for(int i=0;i<8;++i)for(int j=0;j<4;++j)out[i*4+j]=static_cast<std::uint8_t>(h[i]>>(24-j*8));return out;
}

std::array<std::uint8_t,16> Aes256EncryptBlock(std::span<const std::uint8_t,32> key, std::span<const std::uint8_t,16> block) { auto s=std::array<std::uint8_t,16>{};std::copy(block.begin(),block.end(),s.begin());const auto w=expand(key);addKey(s,w,0);for(int r=1;r<14;++r){for(auto&v:s)v=sbox(v);shiftRows(s);mix(s);addKey(s,w,r);}for(auto&v:s)v=sbox(v);shiftRows(s);addKey(s,w,14);return s; }
std::array<std::uint8_t,16> Aes256DecryptBlock(std::span<const std::uint8_t,32> key, std::span<const std::uint8_t,16> block) { auto s=std::array<std::uint8_t,16>{};std::copy(block.begin(),block.end(),s.begin());const auto w=expand(key);addKey(s,w,14);for(int r=13;r>0;--r){invShiftRows(s);for(auto&v:s)v=invSbox(v);addKey(s,w,r);invMix(s);}invShiftRows(s);for(auto&v:s)v=invSbox(v);addKey(s,w,0);return s; }
} // namespace CPPPdf::Internal
