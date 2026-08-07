#include <CPPPdf/Security/PdfCms.hpp>

#include "Internal/Security/PdfCrypto.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <string_view>

#if defined(_WIN32)
// bcrypt.h depends on the fundamental Win32 types declared by windows.h.
// Keep the include order explicit; including bcrypt.h directly causes MSVC to
// parse members such as ULONG/PUCHAR/NTSTATUS as invalid declarations.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace CPPPdf {
namespace {

// Converts a civil (Gregorian) date/time to Unix seconds (Howard Hinnant's
// days_from_civil). Returns 0 for invalid input.
std::uint64_t CivilToUnix(const std::uint64_t year, const std::uint64_t month,
                          const std::uint64_t day, const std::uint64_t hour,
                          const std::uint64_t minute, const std::uint64_t second) {
    if (month < 1U || month > 12U || day < 1U || day > 31U ||
        hour > 23U || minute > 59U || second > 60U) {
        return 0U;
    }
    const std::int64_t y = static_cast<std::int64_t>(year) - (static_cast<std::int64_t>(month) <= 2U ? 1 : 0);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t mp = (static_cast<std::int64_t>(month) + 9U) % 12U;
    const std::int64_t doy = (153 * mp + 2) / 5 + static_cast<std::int64_t>(day) - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = era * 146097 + doe - 719468;
    return static_cast<std::uint64_t>(days) * 86400U +
           hour * 3600U + minute * 60U + second;
}

// Minimal big-endian bignum for RSA modular exponentiation. Numbers are stored
// little-endian limbs (base 2^32) for the multiply/divide core and converted
// to/from big-endian byte vectors at the boundary.
using Limb = std::uint32_t;
using DblLimb = std::uint64_t;

class BigNum {
public:
    std::vector<Limb> v;

    explicit BigNum(std::uint64_t value = 0) {
        while (value != 0U) { v.push_back(static_cast<Limb>(value & 0xFFFFFFFFU)); value >>= 32U; }
        if (v.empty()) v.push_back(0);
    }

    static BigNum FromBytes(const std::vector<std::uint8_t>& bytes) {
        BigNum result;
        result.v.clear();
        // Bytes are big-endian (most significant first); limbs are
        // little-endian. Accumulate 4 bytes at a time from the END.
        std::size_t i = bytes.size();
        while (i > 0U) {
            std::uint32_t limb = 0;
            const std::size_t n = std::min<std::size_t>(4U, i);
            i -= n;
            for (std::size_t j = 0; j < n; ++j) {
                limb = (limb << 8U) | bytes[i + j];
            }
            result.v.push_back(limb);
        }
        result.Normalize();
        if (result.v.empty()) result.v.push_back(0);
        return result;
    }

    [[nodiscard]] std::vector<std::uint8_t> ToBytes(const std::size_t fixedSize = 0U) const {
        // little-endian limbs -> big-endian bytes.
        std::vector<std::uint8_t> bytes;
        for (auto iterator = v.rbegin(); iterator != v.rend(); ++iterator) {
            const Limb limb = *iterator;
            bytes.push_back(static_cast<std::uint8_t>((limb >> 24U) & 0xFFU));
            bytes.push_back(static_cast<std::uint8_t>((limb >> 16U) & 0xFFU));
            bytes.push_back(static_cast<std::uint8_t>((limb >> 8U) & 0xFFU));
            bytes.push_back(static_cast<std::uint8_t>(limb & 0xFFU));
        }
        // Strip leading zero bytes.
        auto first = bytes.begin();
        while (first != bytes.end() && *first == 0U) ++first;
        if (first != bytes.begin()) bytes.erase(bytes.begin(), first);
        if (bytes.empty()) bytes.push_back(0);
        if (fixedSize > bytes.size()) {
            std::vector<std::uint8_t> padded(fixedSize - bytes.size(), std::uint8_t{0});
            padded.insert(padded.end(), bytes.begin(), bytes.end());
            return padded;
        }
        return bytes;
    }

    void Normalize() {
        while (v.size() > 1U && v.back() == 0U) v.pop_back();
    }

    [[nodiscard]] bool IsZero() const { return v.size() == 1U && v[0] == 0U; }

    friend bool operator<(const BigNum& a, const BigNum& b) {
        if (a.v.size() != b.v.size()) return a.v.size() < b.v.size();
        for (std::size_t i = a.v.size(); i-- > 0U;) {
            if (a.v[i] != b.v[i]) return a.v[i] < b.v[i];
        }
        return false;
    }
};

BigNum BigSub(const BigNum& a, const BigNum& b) { // assumes a >= b
    BigNum result;
    DblLimb borrow = 0;
    result.v.resize(a.v.size());
    for (std::size_t i = 0; i < a.v.size(); ++i) {
        const DblLimb x = a.v[i];
        const DblLimb y = (i < b.v.size() ? b.v[i] : 0U) + borrow;
        DblLimb diff;
        if (x >= y) { diff = x - y; borrow = 0; }
        else { diff = (static_cast<DblLimb>(1) << 32U) + x - y; borrow = 1; }
        result.v[i] = static_cast<Limb>(diff & 0xFFFFFFFFU);
    }
    result.Normalize();
    return result;
}

BigNum BigMul(const BigNum& a, const BigNum& b) {
    if (a.IsZero() || b.IsZero()) return BigNum(0);
    BigNum result;
    result.v.assign(a.v.size() + b.v.size(), 0U);
    for (std::size_t i = 0; i < a.v.size(); ++i) {
        DblLimb carry = 0;
        for (std::size_t j = 0; j < b.v.size(); ++j) {
            const DblLimb cur = static_cast<DblLimb>(a.v[i]) * b.v[j] +
                                result.v[i + j] + carry;
            result.v[i + j] = static_cast<Limb>(cur & 0xFFFFFFFFU);
            carry = cur >> 32U;
        }
        // Propagate any remaining carry through the higher limbs (may span more
        // than one limb).
        std::size_t k = i + b.v.size();
        while (carry != 0U && k < result.v.size()) {
            const DblLimb cur = static_cast<DblLimb>(result.v[k]) + carry;
            result.v[k] = static_cast<Limb>(cur & 0xFFFFFFFFU);
            carry = cur >> 32U;
            ++k;
        }
    }
    result.Normalize();
    return result;
}

void BigShiftLeftOne(BigNum& a) {
    Limb carry = 0;
    for (auto& limb : a.v) {
        const DblLimb shifted = (static_cast<DblLimb>(limb) << 1U) | carry;
        limb = static_cast<Limb>(shifted & 0xFFFFFFFFU);
        carry = static_cast<Limb>(shifted >> 32U);
    }
    if (carry != 0U) a.v.push_back(carry);
}

void BigShiftRightOne(BigNum& a) {
    Limb carry = 0;
    for (std::size_t i = a.v.size(); i-- > 0U;) {
        const Limb nextCarry = a.v[i] & 1U;
        a.v[i] = (a.v[i] >> 1U) | (carry << 31U);
        carry = nextCarry;
    }
    a.Normalize();
}

bool BigIsOdd(const BigNum& a) { return !a.v.empty() && (a.v[0] & 1U) != 0U; }

// Returns the index of the highest set bit (0-based), or -1 for zero.
[[nodiscard]] int BigBitLength(const BigNum& a) {
    for (std::size_t i = a.v.size(); i-- > 0U;) {
        if (a.v[i] != 0U) {
            int bits = 0;
            std::uint32_t value = a.v[i];
            while (value != 0U) { ++bits; value >>= 1U; }
            return static_cast<int>(i * 32U) + bits;
        }
    }
    return 0;
}

BigNum BigMod(const BigNum& a, const BigNum& m) {
    if (a < m) return a;
    if (m.IsZero()) return BigNum(0);
    // Binary long division: r = a; for each bit, shift r left, set the current
    // bit of a, subtract m when r >= m. O(bits * limbs) but with a single
    // working vector to keep allocations low.
    BigNum remainder;
    remainder.v.reserve(a.v.size() + 1U);
    remainder.v.assign(a.v.begin(), a.v.end());
    const int aBits = BigBitLength(a);
    for (int bit = aBits - 1; bit >= 0; --bit) {
        BigShiftLeftOne(remainder);
        const std::size_t limbIndex = static_cast<std::size_t>(bit / 32);
        if (limbIndex < a.v.size()) {
            if ((a.v[limbIndex] & (std::uint32_t{1} << (bit % 32))) != 0U) {
                remainder.v[0] |= 1U;
            }
        }
        if (!(remainder < m)) remainder = BigSub(remainder, m);
    }
    remainder.Normalize();
    return remainder;
}

BigNum BigModMul(const BigNum& a, const BigNum& b, const BigNum& m) {
    return BigMod(BigMul(a, b), m);
}

// Barrett reduction context: precomputes mu = floor(2^(2k) / m) once so every
// reduction is two multiplications and a couple of subtractions. Used by
// BigModExp for large (2048-bit) moduli.
struct BarrettContext {
    BigNum m;
    BigNum mu;
    int k{};

    explicit BarrettContext(const BigNum& modulus) : m(modulus) {
        k = BigBitLength(m);
        // mu = floor(2^(2k) / m), computed with a single long division.
        BigNum numerator{1U};
        for (int i = 0; i < 2 * k; ++i) BigShiftLeftOne(numerator);
        BigNum remainder = numerator;
        const int shift = BigBitLength(numerator) - BigBitLength(m);
        BigNum mShift = m;
        for (int s = 0; s < shift; ++s) BigShiftLeftOne(mShift);
        BigNum quotient;
        quotient.v.assign(static_cast<std::size_t>(shift / 32) + 1U, 0U);
        for (int s = shift; s >= 0; --s) {
            if (!(remainder < mShift)) {
                remainder = BigSub(remainder, mShift);
                const std::size_t limb = static_cast<std::size_t>(s / 32);
                const int bit = s % 32;
                if (limb < quotient.v.size()) quotient.v[limb] |= std::uint32_t{1} << bit;
            }
            BigShiftRightOne(mShift);
        }
        quotient.Normalize();
        mu = quotient;
    }

    // Returns a mod m using precomputed mu. Requires a < m^2.
    [[nodiscard]] BigNum Reduce(const BigNum& a) const {
        if (a < m) return a;
        // q = floor(a * mu / 2^(2k)); drop the low 2k bits at once.
        BigNum q = BigMul(a, mu);
        const std::size_t dropLimbs = static_cast<std::size_t>((2 * k) / 32);
        const int dropBits = (2 * k) % 32;
        if (dropLimbs < q.v.size()) q.v.erase(q.v.begin(), q.v.begin() + dropLimbs);
        else q.v.assign(1U, 0U);
        if (dropBits > 0 && q.v.size() > 1U) {
            for (std::size_t i = 0; i < q.v.size() - 1U; ++i) {
                q.v[i] = (q.v[i] >> dropBits) | (q.v[i + 1U] << (32 - dropBits));
            }
            q.v.back() >>= dropBits;
        }
        q.Normalize();
        BigNum r = BigSub(a, BigMul(q, m));
        while (!(r < m)) r = BigSub(r, m);
        return r;
    }
};

BigNum BigModExp(const BigNum& base, const BigNum& exp, const BigNum& m) {
    if (m.IsZero()) return BigNum(0);
    if (BigBitLength(m) > 256) {
        // Large modulus: Barrett reduction.
        BarrettContext ctx(m);
        BigNum result(1);
        BigNum b = ctx.Reduce(base);
        BigNum e = exp;
        while (!e.IsZero()) {
            if (BigIsOdd(e)) result = ctx.Reduce(BigMul(result, b));
            BigShiftRightOne(e);
            if (!e.IsZero()) b = ctx.Reduce(BigMul(b, b));
        }
        return result;
    }
    BigNum result(1);
    BigNum b = BigMod(base, m);
    BigNum e = exp;
    while (!e.IsZero()) {
        if (BigIsOdd(e)) result = BigModMul(result, b, m);
        BigShiftRightOne(e);
        if (!e.IsZero()) b = BigModMul(b, b, m);
    }
    return result;
}

// ---- Minimal DER parser ----

class DerReader {
public:
    explicit DerReader(std::span<const std::uint8_t> data) : data_(data) {}

    bool ReadLength(std::size_t& length) {
        if (pos_ >= data_.size()) return false;
        const auto first = data_[pos_++];
        if ((first & 0x80U) == 0U) { length = first; return true; }
        const std::size_t count = first & 0x7FU;
        if (count == 0U || count > 4U || pos_ + count > data_.size()) return false;
        std::size_t value = 0;
        for (std::size_t i = 0; i < count; ++i) value = (value << 8U) | data_[pos_++];
        length = value;
        return true;
    }

    // Reads a TLV header and returns the value slice (tag, value). Advances past it.
    bool ReadElement(std::uint8_t& tag, std::span<const std::uint8_t>& value) {
        if (pos_ >= data_.size()) return false;
        tag = data_[pos_++];
        std::size_t length = 0;
        if (!ReadLength(length)) return false;
        if (pos_ + length > data_.size()) return false;
        value = data_.subspan(pos_, length);
        pos_ += length;
        return true;
    }

    [[nodiscard]] bool AtEnd() const { return pos_ == data_.size(); }
    [[nodiscard]] std::size_t Position() const { return pos_; }
    void Seek(const std::size_t position) { pos_ = position; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_{};
};

// Locates a child element by tag within a constructed value, returning its slice.
[[nodiscard]] bool FindChild(const std::span<const std::uint8_t> container,
                             const std::uint8_t tag, std::span<const std::uint8_t>& out) {
    DerReader reader(container);
    while (!reader.AtEnd()) {
        std::uint8_t currentTag = 0;
        std::span<const std::uint8_t> value;
        if (!reader.ReadElement(currentTag, value)) return false;
        if (currentTag == tag) { out = value; return true; }
    }
    return false;
}

// Parses a PEM-encoded RSA key body (base64 between BEGIN/END markers).
[[nodiscard]] std::vector<std::uint8_t> PemBody(std::string_view pem) {
    const auto beginMarker = pem.find("-----BEGIN");
    const auto endMarker = pem.rfind("-----END");
    std::string_view body = pem;
    if (beginMarker != std::string_view::npos && endMarker != std::string_view::npos && endMarker > beginMarker) {
        // Start after the BEGIN header line.
        const auto newline = pem.find('\n', beginMarker);
        const auto bodyStart = (newline != std::string_view::npos && newline < endMarker) ? newline + 1 : beginMarker;
        body = pem.substr(bodyStart, endMarker - bodyStart);
    }
    std::string compact;
    for (const char ch : body) {
        const auto c = static_cast<unsigned char>(ch);
        const bool b64 = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
        if (b64) compact.push_back(ch);
    }
    // Decode base64.
    const auto b64value = [](const char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<std::uint8_t> result;
    int buffer = 0;
    int bits = 0;
    for (const char c : compact) {
        const int value = b64value(c);
        if (value < 0) continue;
        buffer = (buffer << 6) | value;
        bits += 6;
        while (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return result;
}

// Reads an RSA INTEGER from a DER sequence by tag 0x02, returning its bytes
// (big-endian) or empty when missing.
[[nodiscard]] std::vector<std::uint8_t> ReadInteger(const std::span<const std::uint8_t> seq,
                                                    const std::size_t index) {
    DerReader reader(seq);
    std::size_t current = 0;
    while (!reader.AtEnd()) {
        std::uint8_t tag = 0;
        std::span<const std::uint8_t> value;
        if (!reader.ReadElement(tag, value)) return {};
        if (tag == 0x02U) {
            if (current == index) {
                // Strip a leading sign byte (0x00) for positive integers.
                if (!value.empty() && value[0] == 0x00U) value = value.subspan(1);
                return std::vector<std::uint8_t>(value.begin(), value.end());
            }
            ++current;
        }
    }
    return {};
}

} // namespace

// Appends a DER length field (short or long form) to a byte vector.
void AppendDerLength(std::vector<std::uint8_t>& out, const std::size_t length) {
    if (length < 128U) {
        out.push_back(static_cast<std::uint8_t>(length));
        return;
    }
    std::vector<std::uint8_t> bytes;
    std::size_t value = length;
    while (value != 0U) {
        bytes.insert(bytes.begin(), static_cast<std::uint8_t>(value & 0xFFU));
        value >>= 8U;
    }
    out.push_back(static_cast<std::uint8_t>(0x80U | bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
}

namespace {
using DerBytes = std::vector<std::uint8_t>;

DerBytes DerElement(const std::uint8_t tag, const std::span<const std::uint8_t> value) {
    DerBytes result{tag};
    AppendDerLength(result, value.size());
    result.insert(result.end(), value.begin(), value.end());
    return result;
}

void AppendBytes(DerBytes& destination, const std::span<const std::uint8_t> source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

DerBytes DerOid(const std::span<const std::uint8_t> value) {
    return DerElement(0x06U, value);
}

DerBytes DerSequence(const std::span<const std::uint8_t> value) {
    return DerElement(0x30U, value);
}

DerBytes DerSet(const std::span<const std::uint8_t> value) {
    return DerElement(0x31U, value);
}

DerBytes DerAlgorithmIdentifier(const std::span<const std::uint8_t> oid, const bool includeNull = true) {
    DerBytes body = DerOid(oid);
    if (includeNull) {
        body.push_back(std::uint8_t{0x05});
        body.push_back(std::uint8_t{0x00});
    }
    return DerSequence(body);
}

DerBytes DerAttribute(const std::span<const std::uint8_t> oid,
                      const std::span<const std::uint8_t> encodedValue) {
    DerBytes body = DerOid(oid);
    const DerBytes values = DerSet(encodedValue);
    AppendBytes(body, values);
    return DerSequence(body);
}

bool OidEquals(const std::span<const std::uint8_t> value,
               const std::span<const std::uint8_t> expected) {
    return value.size() == expected.size() && std::equal(value.begin(), value.end(), expected.begin());
}

bool ExtractIssuerAndSerial(const std::span<const std::uint8_t> certificateDer,
                            DerBytes& issuerDer,
                            DerBytes& serialDer) {
    DerReader certificateReader(certificateDer);
    std::uint8_t tag = 0U;
    std::span<const std::uint8_t> certificateValue;
    if (!certificateReader.ReadElement(tag, certificateValue) || tag != 0x30U) return false;

    DerReader certificate(certificateValue);
    std::span<const std::uint8_t> tbsValue;
    if (!certificate.ReadElement(tag, tbsValue) || tag != 0x30U) return false;

    DerReader tbs(tbsValue);
    std::span<const std::uint8_t> value;
    if (!tbs.ReadElement(tag, value)) return false;
    if (tag == 0xA0U && !tbs.ReadElement(tag, value)) return false;
    if (tag != 0x02U) return false;
    serialDer = DerElement(0x02U, value);

    // signature AlgorithmIdentifier
    if (!tbs.ReadElement(tag, value) || tag != 0x30U) return false;
    // issuer Name
    if (!tbs.ReadElement(tag, value) || tag != 0x30U) return false;
    issuerDer = DerElement(0x30U, value);
    return true;
}

DerBytes EncodeSigningTime(const std::uint64_t unixSeconds) {
    const std::time_t raw = static_cast<std::time_t>(unixSeconds != 0U
        ? unixSeconds
        : static_cast<std::uint64_t>(std::time(nullptr)));
    std::tm utc{};
#if defined(_MSC_VER)
    if (gmtime_s(&utc, &raw) != 0) return {};
#else
    if (gmtime_r(&raw, &utc) == nullptr) return {};
#endif
    char buffer[20]{};
    const int year = utc.tm_year + 1900;
    const char* format = year >= 1950 && year <= 2049 ? "%y%m%d%H%M%SZ" : "%Y%m%d%H%M%SZ";
    if (std::strftime(buffer, sizeof(buffer), format, &utc) == 0U) return {};
    const auto length = std::char_traits<char>::length(buffer);
    const auto bytes = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(buffer), length);
    return DerElement(year >= 1950 && year <= 2049 ? 0x17U : 0x18U, bytes);
}

std::uint64_t ParseCmsTime(const std::span<const std::uint8_t> value, const bool generalized) {
    const std::size_t minimum = generalized ? 15U : 13U;
    if (value.size() < minimum || value.back() != static_cast<std::uint8_t>('Z')) return 0U;
    const auto digits = [&](const std::size_t offset, const std::size_t count) -> std::uint64_t {
        if (offset + count > value.size()) return 0U;
        std::uint64_t result = 0U;
        for (std::size_t i = 0U; i < count; ++i) {
            const auto digit = value[offset + i];
            if (digit < static_cast<std::uint8_t>('0') || digit > static_cast<std::uint8_t>('9')) return 0U;
            result = result * 10U + static_cast<std::uint64_t>(digit - static_cast<std::uint8_t>('0'));
        }
        return result;
    };
    const std::uint64_t year = generalized
        ? digits(0U, 4U)
        : ((digits(0U, 2U) >= 50U ? 1900U : 2000U) + digits(0U, 2U));
    const std::size_t offset = generalized ? 4U : 2U;
    return CivilToUnix(year, digits(offset, 2U), digits(offset + 2U, 2U),
                       digits(offset + 4U, 2U), digits(offset + 6U, 2U),
                       digits(offset + 8U, 2U));
}

constexpr std::array<std::uint8_t, 9> kCmsSignedDataOid{
    0x2aU, 0x86U, 0x48U, 0x86U, 0xf7U, 0x0dU, 0x01U, 0x07U, 0x02U};
constexpr std::array<std::uint8_t, 9> kCmsDataOid{
    0x2aU, 0x86U, 0x48U, 0x86U, 0xf7U, 0x0dU, 0x01U, 0x07U, 0x01U};
constexpr std::array<std::uint8_t, 9> kSha256Oid{
    0x60U, 0x86U, 0x48U, 0x01U, 0x65U, 0x03U, 0x04U, 0x02U, 0x01U};
constexpr std::array<std::uint8_t, 9> kRsaEncryptionOid{
    0x2aU, 0x86U, 0x48U, 0x86U, 0xf7U, 0x0dU, 0x01U, 0x01U, 0x01U};
constexpr std::array<std::uint8_t, 9> kContentTypeAttributeOid{
    0x2aU, 0x86U, 0x48U, 0x86U, 0xf7U, 0x0dU, 0x01U, 0x09U, 0x03U};
constexpr std::array<std::uint8_t, 9> kMessageDigestAttributeOid{
    0x2aU, 0x86U, 0x48U, 0x86U, 0xf7U, 0x0dU, 0x01U, 0x09U, 0x04U};
constexpr std::array<std::uint8_t, 9> kSigningTimeAttributeOid{
    0x2aU, 0x86U, 0x48U, 0x86U, 0xf7U, 0x0dU, 0x01U, 0x09U, 0x05U};

} // namespace

PdfCms::RsaPublicKey PdfCms::ParsePublicKeyPem(const std::string_view pem) {
    const auto der = PemBody(pem);
    // SubjectPublicKeyInfo ::= SEQUENCE { algorithm SEQUENCE, subjectPublicKey BIT STRING }
    DerReader outer(der);
    std::uint8_t tag = 0;
    std::span<const std::uint8_t> spkiValue;
    if (!outer.ReadElement(tag, spkiValue) || tag != 0x30U) return {};
    std::span<const std::uint8_t> bitString;
    if (!FindChild(spkiValue, 0x03U, bitString)) return {};
    if (bitString.empty() || bitString[0] != 0U) return {};
    const auto rsaKey = bitString.subspan(1);
    std::span<const std::uint8_t> rsaSeq;
    if (!FindChild(rsaKey, 0x30U, rsaSeq)) return {};
    RsaPublicKey key;
    key.modulus = ReadInteger(rsaSeq, 0);
    key.exponent = ReadInteger(rsaSeq, 1);
    return key;
}

bool PdfCms::ParsePublicKeyFromCertificate(
    const std::span<const std::uint8_t> certificateDer,
    RsaPublicKey& outKey) {
    // X.509 Certificate ::= SEQUENCE { tbsCertificate SEQUENCE, ... }
    // The SubjectPublicKeyInfo is a SEQUENCE inside tbsCertificate that holds
    // a BIT STRING whose content is the RSA key SEQUENCE. We recursively scan
    // for the first BIT STRING that decodes as an RSA public key.
    DerReader outer(certificateDer);
    std::uint8_t tag = 0;
    std::span<const std::uint8_t> certificateValue;
    if (!outer.ReadElement(tag, certificateValue) || tag != 0x30U) return false;
    std::span<const std::uint8_t> tbs;
    if (!FindChild(certificateValue, 0x30U, tbs)) return false;

    // Recursively search constructed elements for a usable RSA BIT STRING.
    struct Search {
        static bool Scan(const std::span<const std::uint8_t>& value, RsaPublicKey& key) {
            DerReader reader(value);
            while (!reader.AtEnd()) {
                std::uint8_t elementTag = 0;
                std::span<const std::uint8_t> element;
                if (!reader.ReadElement(elementTag, element)) return false;
                if (elementTag == 0x03U && !element.empty() && element[0] == 0U) {
                    const auto spki = element.subspan(1);
                    std::span<const std::uint8_t> rsaSeq;
                    if (FindChild(spki, 0x30U, rsaSeq)) {
                        key.modulus = ReadInteger(rsaSeq, 0);
                        key.exponent = ReadInteger(rsaSeq, 1);
                        if (!key.modulus.empty() && !key.exponent.empty()) return true;
                    }
                } else if ((elementTag & 0x20U) != 0U) {
                    // Constructed element: search inside.
                    if (Scan(element, key)) return true;
                }
            }
            return false;
        }
    };
    return Search::Scan(tbs, outKey);
}
PdfCms::RsaPrivateKey PdfCms::ParsePrivateKeyPem(const std::string_view pem) {
    const auto der = PemBody(pem);
    DerReader outer(der);
    std::uint8_t tag = 0;
    std::span<const std::uint8_t> pkcs1;
    if (!outer.ReadElement(tag, pkcs1) || tag != 0x30U) return {};
    RsaPrivateKey key;
    key.modulus = ReadInteger(pkcs1, 1);
    key.publicExponent = ReadInteger(pkcs1, 2);
    key.privateExponent = ReadInteger(pkcs1, 3);
    // CRT components (positions 4..8 in RSAPrivateKey).
    if (pkcs1.size() > 1U) {
        key.prime1 = ReadInteger(pkcs1, 4);
        key.prime2 = ReadInteger(pkcs1, 5);
        key.exponent1 = ReadInteger(pkcs1, 6);
        key.exponent2 = ReadInteger(pkcs1, 7);
        key.coefficient = ReadInteger(pkcs1, 8);
    }
    return key;
}

std::vector<std::uint8_t> PdfCms::RsaOperation(
    const std::vector<std::uint8_t>& input,
    const std::vector<std::uint8_t>& exponent,
    const std::vector<std::uint8_t>& modulus) {
    const BigNum base = BigNum::FromBytes(input);
    const BigNum exp = BigNum::FromBytes(exponent);
    const BigNum mod = BigNum::FromBytes(modulus);
    const BigNum result = BigModExp(base, exp, mod);
    return result.ToBytes(modulus.size());
}

std::vector<std::uint8_t> PdfCms::RsaSha256SignFallback(
    const RsaPrivateKey& key,
    const std::span<const std::uint8_t, 32> digest) {
    // DigestInfo ::= SEQUENCE { algorithm SHA-256, OCTET STRING digest }
    // SHA-256 OID 2.16.840.1.101.3.4.2.1 encoded as 06 09 60 86 48 01 65 03 04 02 01.
    const std::vector<std::uint8_t> digestInfo = {
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
        0x05, 0x00, 0x04, 0x20};
    std::vector<std::uint8_t> tbs = digestInfo;
    tbs.insert(tbs.end(), digest.begin(), digest.end());

    // PKCS#1 v1.5 block type 1: 0x00 0x01 0xFF... 0x00 || tbs.
    const std::size_t modulusSize = key.modulus.size();
    if (tbs.size() + 11U > modulusSize) return {};
    std::vector<std::uint8_t> em(modulusSize, 0xFF);
    em[0] = 0x00; em[1] = 0x01;
    const std::size_t padEnd = modulusSize - tbs.size() - 1U;
    em[padEnd] = 0x00;
    std::copy(tbs.begin(), tbs.end(), em.begin() + padEnd + 1U);
    return RsaOperation(em, key.privateExponent, key.modulus);
}

bool PdfCms::RsaSha256VerifyFallback(
    const RsaPublicKey& key,
    const std::span<const std::uint8_t, 32> digest,
    const std::span<const std::uint8_t> signature) {
    const auto em = RsaOperation(std::vector<std::uint8_t>(signature.begin(), signature.end()),
                                 key.exponent, key.modulus);
    if (em.size() != key.modulus.size()) return false;
    // Check PKCS#1 v1.5 padding: 00 01 FF..FF 00 || DigestInfo(digest).
    if (em[0] != 0x00U || em[1] != 0x01U) return false;
    std::size_t pos = 2U;
    while (pos < em.size() && em[pos] == 0xFFU) ++pos;
    if (pos >= em.size() || em[pos] != 0x00U) return false;
    ++pos;
    // DigestInfo prefix for SHA-256.
    const std::vector<std::uint8_t> prefix = {
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
        0x05, 0x00, 0x04, 0x20};
    if (em.size() - pos < prefix.size() + 32U) return false;
    if (!std::equal(prefix.begin(), prefix.end(), em.begin() + pos)) return false;
    pos += prefix.size();
    return std::equal(digest.begin(), digest.end(), em.begin() + pos);
}

std::vector<std::uint8_t> PdfCms::BuildSignedData(
    const std::span<const std::uint8_t, 32> digest,
    const RsaPublicKey& publicKey,
    const RsaPrivateKey& privateKey,
    const std::span<const std::uint8_t> certificateDer,
    const SignedDataOptions& options) {
    if (certificateDer.empty() || privateKey.modulus.empty() || privateKey.privateExponent.empty()) return {};

    std::vector<DerBytes> attributes;
    const DerBytes contentTypeValue = DerOid(kCmsDataOid);
    attributes.push_back(DerAttribute(kContentTypeAttributeOid, contentTypeValue));
    const DerBytes messageDigestValue = DerElement(0x04U, digest);
    attributes.push_back(DerAttribute(kMessageDigestAttributeOid, messageDigestValue));
    if (options.includeSigningTime) {
        const DerBytes signingTime = EncodeSigningTime(options.signingTimeSeconds);
        if (!signingTime.empty()) attributes.push_back(DerAttribute(kSigningTimeAttributeOid, signingTime));
    }
    std::sort(attributes.begin(), attributes.end());

    DerBytes signedAttributesContent;
    for (const auto& attribute : attributes) AppendBytes(signedAttributesContent, attribute);
    const DerBytes signedAttributesDer = DerSet(signedAttributesContent);
    const auto signedAttributesDigest = Internal::Sha256(signedAttributesDer);
    const DerBytes signature = RsaSha256Sign(privateKey, signedAttributesDigest);
    if (signature.empty()) return {};

    DerBytes issuerDer;
    DerBytes serialDer;
    const bool hasIssuerAndSerial = ExtractIssuerAndSerial(certificateDer, issuerDer, serialDer);

    DerBytes signerInfoBody;
    if (hasIssuerAndSerial) {
        AppendBytes(signerInfoBody, DerElement(0x02U, std::array<std::uint8_t, 1>{0x01U}));
        DerBytes sidBody = issuerDer;
        AppendBytes(sidBody, serialDer);
        AppendBytes(signerInfoBody, DerSequence(sidBody));
    } else {
        AppendBytes(signerInfoBody, DerElement(0x02U, std::array<std::uint8_t, 1>{0x03U}));
        const auto keyHash = Internal::Sha256(publicKey.modulus);
        AppendBytes(signerInfoBody, DerElement(0x80U,
            std::span<const std::uint8_t>(keyHash.data(), 20U)));
    }
    AppendBytes(signerInfoBody, DerAlgorithmIdentifier(kSha256Oid));
    // signedAttrs is [0] IMPLICIT SignedAttributes: retain only the SET content.
    AppendBytes(signerInfoBody, DerElement(0xA0U, signedAttributesContent));
    AppendBytes(signerInfoBody, DerAlgorithmIdentifier(kRsaEncryptionOid));
    AppendBytes(signerInfoBody, DerElement(0x04U, signature));
    const DerBytes signerInfo = DerSequence(signerInfoBody);

    const DerBytes digestAlgorithms = DerSet(DerAlgorithmIdentifier(kSha256Oid));
    const DerBytes encapContentInfo = DerSequence(DerOid(kCmsDataOid));

    DerBytes certificateSetContent(certificateDer.begin(), certificateDer.end());
    for (const auto& certificate : options.certificateChain) {
        if (certificate.empty() ||
            (certificate.size() == certificateDer.size() &&
             std::equal(certificate.begin(), certificate.end(), certificateDer.begin()))) {
            continue;
        }
        AppendBytes(certificateSetContent, certificate);
    }

    DerBytes signedDataBody;
    const std::array<std::uint8_t, 1> signedDataVersion{
        static_cast<std::uint8_t>(hasIssuerAndSerial ? 0x01U : 0x03U)};
    AppendBytes(signedDataBody, DerElement(0x02U, signedDataVersion));
    AppendBytes(signedDataBody, digestAlgorithms);
    AppendBytes(signedDataBody, encapContentInfo);
    AppendBytes(signedDataBody, DerElement(0xA0U, certificateSetContent));
    AppendBytes(signedDataBody, DerSet(signerInfo));
    const DerBytes signedDataSequence = DerSequence(signedDataBody);

    DerBytes contentInfoBody = DerOid(kCmsSignedDataOid);
    AppendBytes(contentInfoBody, DerElement(0xA0U, signedDataSequence));
    (void)options.signerName;
    return DerSequence(contentInfoBody);
}

std::vector<std::uint8_t> PdfCms::BuildSignedData(
    const std::span<const std::uint8_t, 32> digest,
    const RsaPublicKey& publicKey,
    const RsaPrivateKey& privateKey,
    const std::span<const std::uint8_t> certificateDer,
    const std::string_view signerName) {
    SignedDataOptions options;
    options.signerName = std::string(signerName);
    return BuildSignedData(digest, publicKey, privateKey, certificateDer, options);
}

bool PdfCms::ParseSignedData(
    const std::span<const std::uint8_t> signedData,
    SignedDataInfo& outInfo) {
    outInfo = {};
    DerReader outer(signedData);
    std::uint8_t tag = 0U;
    std::span<const std::uint8_t> contentInfoValue;
    if (!outer.ReadElement(tag, contentInfoValue) || tag != 0x30U) return false;

    DerReader contentInfo(contentInfoValue);
    std::span<const std::uint8_t> oid;
    if (!contentInfo.ReadElement(tag, oid) || tag != 0x06U || !OidEquals(oid, kCmsSignedDataOid)) return false;
    std::span<const std::uint8_t> explicitSignedData;
    if (!contentInfo.ReadElement(tag, explicitSignedData) || tag != 0xA0U) return false;

    std::span<const std::uint8_t> signedDataBody = explicitSignedData;
    DerReader explicitReader(explicitSignedData);
    std::span<const std::uint8_t> sequenceValue;
    const std::size_t explicitStart = explicitReader.Position();
    if (explicitReader.ReadElement(tag, sequenceValue) && tag == 0x30U) {
        signedDataBody = sequenceValue;
    } else {
        explicitReader.Seek(explicitStart);
    }

    DerReader body(signedDataBody);
    std::span<const std::uint8_t> value;
    if (!body.ReadElement(tag, value) || tag != 0x02U) return false; // version
    if (!body.ReadElement(tag, value) || tag != 0x31U) return false; // digestAlgorithms
    if (!body.ReadElement(tag, value) || tag != 0x30U) return false; // encapContentInfo

    if (!body.ReadElement(tag, value)) return false;
    if (tag == 0xA0U) {
        DerReader certificates(value);
        while (!certificates.AtEnd()) {
            std::span<const std::uint8_t> certificateValue;
            if (!certificates.ReadElement(tag, certificateValue)) return false;
            if (tag == 0x30U) outInfo.certificates.push_back(DerElement(tag, certificateValue));
        }
        if (!body.ReadElement(tag, value)) return false;
    }
    if (tag == 0xA1U) { // optional CRLs
        if (!body.ReadElement(tag, value)) return false;
    }
    if (tag != 0x31U) return false;

    std::span<const std::uint8_t> signerInfoBody = value;
    DerReader signerInfos(value);
    std::span<const std::uint8_t> signerInfoValue;
    if (signerInfos.ReadElement(tag, signerInfoValue) && tag == 0x30U) {
        signerInfoBody = signerInfoValue;
    }

    DerReader signerInfo(signerInfoBody);
    if (!signerInfo.ReadElement(tag, value) || tag != 0x02U) return false; // version
    if (!signerInfo.ReadElement(tag, value)) return false; // sid
    if (!signerInfo.ReadElement(tag, value) || tag != 0x30U) return false; // digestAlgorithm
    if (!signerInfo.ReadElement(tag, value)) return false;

    if (tag == 0xA0U) {
        outInfo.hasSignedAttributes = true;
        outInfo.signedAttributesDer = DerSet(value);
        DerReader attributes(value);
        while (!attributes.AtEnd()) {
            std::span<const std::uint8_t> attributeValue;
            if (!attributes.ReadElement(tag, attributeValue) || tag != 0x30U) return false;
            DerReader attribute(attributeValue);
            std::span<const std::uint8_t> attributeOid;
            std::span<const std::uint8_t> values;
            if (!attribute.ReadElement(tag, attributeOid) || tag != 0x06U) return false;
            if (!attribute.ReadElement(tag, values) || tag != 0x31U) return false;
            DerReader attributeValues(values);
            std::span<const std::uint8_t> firstValue;
            if (!attributeValues.ReadElement(tag, firstValue)) return false;
            if (OidEquals(attributeOid, kContentTypeAttributeOid)) {
                outInfo.hasContentType = tag == 0x06U && OidEquals(firstValue, kCmsDataOid);
            } else if (OidEquals(attributeOid, kMessageDigestAttributeOid)) {
                if (tag == 0x04U && firstValue.size() == outInfo.messageDigest.size()) {
                    std::copy(firstValue.begin(), firstValue.end(), outInfo.messageDigest.begin());
                    outInfo.hasMessageDigest = true;
                }
            } else if (OidEquals(attributeOid, kSigningTimeAttributeOid)) {
                if (tag == 0x17U || tag == 0x18U) {
                    outInfo.signingTimeSeconds = ParseCmsTime(firstValue, tag == 0x18U);
                }
            }
        }
        if (!signerInfo.ReadElement(tag, value) || tag != 0x30U) return false; // signatureAlgorithm
    } else if (tag != 0x30U) {
        return false;
    }

    if (!signerInfo.ReadElement(tag, value) || tag != 0x04U) return false;
    outInfo.signature.assign(value.begin(), value.end());
    return !outInfo.signature.empty();
}

bool PdfCms::ParseSignedData(
    const std::span<const std::uint8_t> signedData,
    std::vector<std::uint8_t>& outCertificate,
    std::vector<std::uint8_t>& outSignature) {
    SignedDataInfo info;
    if (!ParseSignedData(signedData, info)) {
        outCertificate.clear();
        outSignature.clear();
        return false;
    }
    outCertificate = info.certificates.empty() ? std::vector<std::uint8_t>{} : info.certificates.front();
    outSignature = std::move(info.signature);
    return true;
}

#if defined(_WIN32)
namespace {
// Imports an RSA key blob into a BCrypt provider and returns a handle.
// `includePrivate` selects a private (for signing) vs public (for verifying)
// key blob. The caller closes the handle.
NTSTATUS ImportRsaKey(BCRYPT_ALG_HANDLE algorithm, const PdfCms::RsaPublicKey& publicKey,
                      const PdfCms::RsaPrivateKey* privateKey,
                      BCRYPT_KEY_HANDLE& outKey) {
    const std::size_t keySize = publicKey.modulus.size();
    const bool hasCrt = privateKey != nullptr && !privateKey->prime1.empty() &&
                        !privateKey->prime2.empty() && !privateKey->exponent1.empty() &&
                        !privateKey->exponent2.empty() && !privateKey->coefficient.empty();
    std::vector<std::uint8_t> blob;
    blob.resize(sizeof(BCRYPT_RSAKEY_BLOB));
    auto* header = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(blob.data());
    header->Magic = hasCrt ? BCRYPT_RSAFULLPRIVATE_MAGIC : BCRYPT_RSAPUBLIC_MAGIC;
    header->BitLength = static_cast<ULONG>(keySize * 8U);
    header->cbPublicExp = static_cast<ULONG>(publicKey.exponent.size());
    header->cbModulus = static_cast<ULONG>(keySize);
    header->cbPrime1 = hasCrt ? static_cast<ULONG>(keySize / 2U) : 0U;
    header->cbPrime2 = hasCrt ? static_cast<ULONG>(keySize / 2U) : 0U;
    auto append = [&](const std::vector<std::uint8_t>& data) {
        blob.insert(blob.end(), data.begin(), data.end());
    };
    auto appendPadded = [&](const std::vector<std::uint8_t>& data, const std::size_t size) {
        blob.insert(blob.end(), size - data.size(), std::uint8_t{0});
        blob.insert(blob.end(), data.begin(), data.end());
    };
    append(publicKey.exponent);
    append(publicKey.modulus);
    if (hasCrt) {
        appendPadded(privateKey->prime1, keySize / 2U);
        appendPadded(privateKey->prime2, keySize / 2U);
        appendPadded(privateKey->exponent1, keySize / 2U);
        appendPadded(privateKey->exponent2, keySize / 2U);
        appendPadded(privateKey->coefficient, keySize / 2U);
        appendPadded(privateKey->privateExponent, keySize);
    }
    return BCryptImportKeyPair(algorithm, nullptr,
                               hasCrt ? BCRYPT_RSAFULLPRIVATE_BLOB : BCRYPT_RSAPUBLIC_BLOB,
                               &outKey, blob.data(), static_cast<ULONG>(blob.size()), 0U);
}
} // namespace
#endif

std::vector<std::uint8_t> PdfCms::RsaSha256Sign(
    const RsaPrivateKey& key,
    const std::span<const std::uint8_t, 32> digest) {
#if defined(_WIN32)
    if (!key.prime1.empty() && !key.prime2.empty()) {
        // BCrypt needs the CRT components for a private key blob; use them when
        // available (fast + correct), otherwise fall back to the big-number core.
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0U) != 0) return {};
        BCRYPT_KEY_HANDLE keyHandle = nullptr;
        PdfCms::RsaPublicKey publicView;
        publicView.modulus = key.modulus;
        publicView.exponent = key.publicExponent;
        if (ImportRsaKey(algorithm, publicView, &key, keyHandle) != 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0U);
            return {};
        }
        std::vector<std::uint8_t> hash = {digest.begin(), digest.end()};
        BCRYPT_PKCS1_PADDING_INFO paddingInfo{};
        paddingInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;
        ULONG resultSize = 0;
        const NTSTATUS status = BCryptSignHash(keyHandle, &paddingInfo, hash.data(),
                                               static_cast<ULONG>(hash.size()), nullptr, 0,
                                               &resultSize, BCRYPT_PAD_PKCS1);
        std::vector<std::uint8_t> result(resultSize);
        if (status == 0U) {
            BCryptSignHash(keyHandle, &paddingInfo, hash.data(), static_cast<ULONG>(hash.size()),
                           result.data(), static_cast<ULONG>(result.size()), &resultSize,
                           BCRYPT_PAD_PKCS1);
        }
        BCryptDestroyKey(keyHandle);
        BCryptCloseAlgorithmProvider(algorithm, 0U);
        return result;
    }
#endif
    return RsaSha256SignFallback(key, digest);
}

bool PdfCms::RsaSha256Verify(
    const RsaPublicKey& key,
    const std::span<const std::uint8_t, 32> digest,
    const std::span<const std::uint8_t> signature) {
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0U) != 0) {
        return false;
    }
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    if (ImportRsaKey(algorithm, key, nullptr, keyHandle) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0U);
        return false;
    }
    std::vector<std::uint8_t> hash = {digest.begin(), digest.end()};
    std::vector<std::uint8_t> sig(signature.begin(), signature.end());
    BCRYPT_PKCS1_PADDING_INFO paddingInfo{};
    paddingInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;
    const NTSTATUS status = BCryptVerifySignature(keyHandle, &paddingInfo, hash.data(),
                                                  static_cast<ULONG>(hash.size()),
                                                  sig.data(), static_cast<ULONG>(sig.size()),
                                                  BCRYPT_PAD_PKCS1);
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(algorithm, 0U);
    return status == 0U;
#else
    return RsaSha256VerifyFallback(key, digest, signature);
#endif
}

#if defined(_WIN32)
namespace {
// Imports an ECDSA P-256 key blob into a BCrypt provider.
NTSTATUS ImportEcKey(BCRYPT_ALG_HANDLE algorithm, const std::vector<std::uint8_t>& point,
                     const std::vector<std::uint8_t>& scalar,
                     const bool includePrivate, BCRYPT_KEY_HANDLE& outKey) {
    // BCRYPT_ECCKEY_BLOB header (X, Y sizes) followed by X || Y (|| d when private).
    const std::size_t coordSize = 32U;
    std::vector<std::uint8_t> blob;
    blob.resize(sizeof(BCRYPT_ECCKEY_BLOB));
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = includePrivate ? BCRYPT_ECDSA_PRIVATE_P256_MAGIC : BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = static_cast<ULONG>(coordSize);
    if (point.size() < 1U + 2U * coordSize) return STATUS_INVALID_PARAMETER;
    std::vector<std::uint8_t> x(point.begin() + 1, point.begin() + 1 + coordSize);
    std::vector<std::uint8_t> y(point.begin() + 1 + coordSize, point.begin() + 1 + 2U * coordSize);
    blob.insert(blob.end(), x.begin(), x.end());
    blob.insert(blob.end(), y.begin(), y.end());
    if (includePrivate) {
        blob.insert(blob.end(), scalar.begin(), scalar.end());
    }
    return BCryptImportKeyPair(algorithm, nullptr,
                               includePrivate ? BCRYPT_ECCPRIVATE_BLOB : BCRYPT_ECCPUBLIC_BLOB,
                               &outKey, blob.data(), static_cast<ULONG>(blob.size()), 0U);
}
} // namespace
#endif

std::vector<std::uint8_t> PdfCms::EcDsaSign(
    const EcPrivateKey& key,
    const std::span<const std::uint8_t, 32> digest) {
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0U) != 0) return {};
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    if (ImportEcKey(algorithm, key.publicKey.point, key.scalar, true, keyHandle) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0U);
        return {};
    }
    std::vector<std::uint8_t> hash = {digest.begin(), digest.end()};
    ULONG resultSize = 0;
    const NTSTATUS status = BCryptSignHash(keyHandle, nullptr, hash.data(),
                                           static_cast<ULONG>(hash.size()), nullptr, 0,
                                           &resultSize, 0U);
    std::vector<std::uint8_t> result(resultSize);
    if (status == 0U) {
        BCryptSignHash(keyHandle, nullptr, hash.data(), static_cast<ULONG>(hash.size()),
                       result.data(), static_cast<ULONG>(result.size()), &resultSize, 0U);
    }
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(algorithm, 0U);
    if (status != 0U) return {};
    result.resize(resultSize);
    return result; // Windows CNG returns the fixed-width raw r || s form.
#else
    (void)key;
    (void)digest;
    return {}; // ECDSA requires CNG on this platform.
#endif
}

bool PdfCms::EcDsaVerify(
    const EcPublicKey& key,
    const std::span<const std::uint8_t, 32> digest,
    const std::span<const std::uint8_t> signature) {
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0U) != 0) return false;
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    if (ImportEcKey(algorithm, key.point, {}, false, keyHandle) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0U);
        return false;
    }
    std::vector<std::uint8_t> hash = {digest.begin(), digest.end()};
    std::vector<std::uint8_t> sig(signature.begin(), signature.end());
    const NTSTATUS status = BCryptVerifySignature(keyHandle, nullptr, hash.data(),
                                                  static_cast<ULONG>(hash.size()),
                                                  sig.data(), static_cast<ULONG>(sig.size()),
                                                  0U);
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(algorithm, 0U);
    return status == 0U;
#else
    (void)key; (void)digest; (void)signature;
    return false; // ECDSA requires CNG on this platform.
#endif
}

PdfCms::CertificateInfo PdfCms::CertificateInfoOf(
    const std::span<const std::uint8_t> certificateDer) {
    CertificateInfo info;
    DerReader outer(certificateDer);
    std::uint8_t tag = 0;
    std::span<const std::uint8_t> certificateValue;
    if (!outer.ReadElement(tag, certificateValue) || tag != 0x30U) return info;
    std::span<const std::uint8_t> tbs;
    if (!FindChild(certificateValue, 0x30U, tbs)) return info;

    // Parse the UTCTime/GeneralizedTime from a validity SEQUENCE, and the
    // first printable name CN from subject/issuer.
    const auto readTime = [](std::span<const std::uint8_t> value) -> std::uint64_t {
        if (value.size() < 13U) return 0U;
        const auto digits = [&](const std::size_t offset, const std::size_t count) -> std::uint64_t {
            std::uint64_t out = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (value[offset + i] < '0' || value[offset + i] > '9') return 0U;
                out = out * 10U + static_cast<std::uint64_t>(value[offset + i] - '0');
            }
            return out;
        };
        if (value[0] == '2' && value.size() >= 15U) { // GeneralizedTime YYYYMMDD
            const std::uint64_t year = digits(0, 4);
            const std::uint64_t month = digits(4, 2);
            const std::uint64_t day = digits(6, 2);
            const std::uint64_t hour = digits(8, 2);
            const std::uint64_t minute = digits(10, 2);
            const std::uint64_t second = digits(12, 2);
            return CivilToUnix(year, month, day, hour, minute, second);
        }
        const std::uint64_t year = 2000U + digits(0, 2); // UTCTime YYMMDD
        const std::uint64_t month = digits(2, 2);
        const std::uint64_t day = digits(4, 2);
        const std::uint64_t hour = digits(6, 2);
        const std::uint64_t minute = digits(8, 2);
        const std::uint64_t second = digits(10, 2);
        return CivilToUnix(year, month, day, hour, minute, second);
    };
    const auto findName = [](std::span<const std::uint8_t> name, std::string& out) {
        // Name ::= SEQUENCE OF RelativeDistinguishedName (SET OF AttributeTypeAndValue)
        DerReader nameReader(name);
        std::uint8_t nTag = 0;
        std::span<const std::uint8_t> rdn;
        while (nameReader.ReadElement(nTag, rdn)) {
            DerReader rdnReader(rdn);
            std::span<const std::uint8_t> attr;
            if (rdnReader.ReadElement(nTag, attr)) {
                DerReader attrReader(attr);
                std::span<const std::uint8_t> oid;
                std::span<const std::uint8_t> value;
                if (attrReader.ReadElement(nTag, oid) && attrReader.ReadElement(nTag, value)) {
                    // CommonName OID 2.5.4.3 = 55 04 03.
                    if (oid.size() >= 3U && oid[oid.size() - 3U] == 0x55U &&
                        oid[oid.size() - 2U] == 0x04U && oid[oid.size() - 1U] == 0x03U) {
                        out.assign(reinterpret_cast<const char*>(value.data()), value.size());
                        return true;
                    }
                }
            }
        }
        return false;
    };

    // tbsCertificate: version [0] (optional), serial INTEGER, signature SEQUENCE,
    // issuer SEQUENCE, validity SEQUENCE, subject SEQUENCE, spki SEQUENCE.
    DerReader tbsReader(tbs);
    std::uint8_t tTag = 0;
    std::span<const std::uint8_t> element;
    // First element: version [0] (optional) or serial.
    if (!tbsReader.ReadElement(tTag, element)) return info;
    if (tTag == 0xA0U) {
        // version [0]: skip it, next is serial.
        if (!tbsReader.ReadElement(tTag, element)) return info;
    }
    // serial INTEGER
    // signature SEQUENCE
    if (!tbsReader.ReadElement(tTag, element)) return info;
    // issuer SEQUENCE
    if (!tbsReader.ReadElement(tTag, element)) return info;
    findName(element, info.issuer);
    // validity SEQUENCE
    if (!tbsReader.ReadElement(tTag, element)) return info;
    {
        DerReader validity(element);
        std::span<const std::uint8_t> notBefore;
        std::span<const std::uint8_t> notAfter;
        if (validity.ReadElement(tTag, notBefore)) info.notBefore = readTime(notBefore);
        if (validity.ReadElement(tTag, notAfter)) info.notAfter = readTime(notAfter);
        info.hasValidity = info.notBefore != 0U && info.notAfter != 0U;
    }
    // subject SEQUENCE
    if (!tbsReader.ReadElement(tTag, element)) return info;
    findName(element, info.subject);
    info.selfSigned = info.subject == info.issuer && !info.subject.empty();
    return info;
}

PdfCms::CertificateStatus PdfCms::ValidateCertificate(
    const std::span<const std::uint8_t> certificateDer,
    const std::span<const std::span<const std::uint8_t>> chain,
    const std::uint64_t nowSeconds) {
    const auto now = nowSeconds != 0U ? nowSeconds :
        static_cast<std::uint64_t>(std::time(nullptr));
    const auto info = CertificateInfoOf(certificateDer);
    if (!info.hasValidity) return CertificateStatus::Malformed;
    if (info.notAfter != 0U && now > info.notAfter) return CertificateStatus::Expired;
    if (info.notBefore != 0U && now < info.notBefore) return CertificateStatus::NotYetValid;
    // Self-signed leaf with no issuer chain: valid, but a CA is not established.
    if (info.selfSigned && chain.empty()) return CertificateStatus::SelfSigned;
    // Verify that each chain issuer's subject matches the previous cert's issuer
    // (a basic chain walk; full signature verification is future work).
    std::string expectedIssuer = info.issuer;
    for (const auto& issuerCert : chain) {
        const auto issuerInfo = CertificateInfoOf(issuerCert);
        if (!expectedIssuer.empty() && issuerInfo.subject != expectedIssuer) {
            return CertificateStatus::Malformed;
        }
        if (issuerInfo.notAfter != 0U && now > issuerInfo.notAfter) return CertificateStatus::Expired;
        expectedIssuer = issuerInfo.issuer;
    }
    return CertificateStatus::Valid;
}

} // namespace CPPPdf
