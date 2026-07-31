#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace CPPPdf {
namespace {
struct Table { std::size_t offset{}; std::size_t length{}; };
std::uint16_t Read16(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p + 2 > b.size()) throw std::runtime_error("Invalid TrueType font data.");
    return static_cast<std::uint16_t>((std::uint16_t(b[p]) << 8) | b[p + 1]);
}
std::int16_t ReadS16(const std::vector<std::uint8_t>& b, std::size_t p) { return static_cast<std::int16_t>(Read16(b,p)); }
std::uint32_t Read32(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p + 4 > b.size()) throw std::runtime_error("Invalid TrueType font data.");
    return (std::uint32_t(b[p]) << 24) | (std::uint32_t(b[p + 1]) << 16) | (std::uint32_t(b[p + 2]) << 8) | b[p + 3];
}

void Write16(std::vector<std::uint8_t>& b, std::size_t p, std::uint16_t v) {
    if (p + 2 > b.size()) throw std::runtime_error("Invalid TrueType write range.");
    b[p] = static_cast<std::uint8_t>(v >> 8); b[p + 1] = static_cast<std::uint8_t>(v);
}
void Write32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) {
    if (p + 4 > b.size()) throw std::runtime_error("Invalid TrueType write range.");
    b[p] = static_cast<std::uint8_t>(v >> 24); b[p + 1] = static_cast<std::uint8_t>(v >> 16);
    b[p + 2] = static_cast<std::uint8_t>(v >> 8); b[p + 3] = static_cast<std::uint8_t>(v);
}
std::uint32_t Checksum(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t sum = 0; for (std::size_t i = 0; i < bytes.size(); i += 4) {
        std::uint32_t word = 0; for (std::size_t j = 0; j < 4; ++j) { word <<= 8; if (i + j < bytes.size()) word |= bytes[i + j]; }
        sum += word;
    } return sum;
}
std::size_t Align4(std::size_t n) { return (n + 3U) & ~std::size_t(3U); }
struct TableRecord { std::string tag; std::vector<std::uint8_t> data; };

std::unordered_map<std::string,Table> ParseTables(const std::vector<std::uint8_t>& b) {
    if (b.size() < 12) {
        throw std::runtime_error("TrueType file is too small.");
    }

    const auto count = Read16(b, 4);
    std::unordered_map<std::string, Table> out;
    for (std::size_t i = 0; i < count; ++i) {
        const auto p = 12U + i * 16U;
        if (p + 16U > b.size()) {
            throw std::runtime_error("Invalid TrueType table directory.");
        }

        std::string tag(reinterpret_cast<const char*>(b.data() + p), 4);
        const auto offset = Read32(b, p + 8U);
        const auto length = Read32(b, p + 12U);
        if (std::size_t(offset) > b.size() || std::size_t(length) > b.size() - offset) {
            throw std::runtime_error("Invalid TrueType table range.");
        }
        out.emplace(std::move(tag), Table{offset, length});
    }
    return out;
}
std::vector<std::uint32_t> DecodeUtf8(std::string_view text) {
    std::vector<std::uint32_t> result;
    for (std::size_t i = 0; i < text.size();) {
        const auto firstByte = static_cast<unsigned char>(text[i]);
        std::uint32_t codePoint = 0;
        std::size_t sequenceLength = 0;

        if (firstByte < 0x80U) {
            codePoint = firstByte;
            sequenceLength = 1;
        } else if ((firstByte & 0xE0U) == 0xC0U) {
            codePoint = firstByte & 0x1FU;
            sequenceLength = 2;
        } else if ((firstByte & 0xF0U) == 0xE0U) {
            codePoint = firstByte & 0x0FU;
            sequenceLength = 3;
        } else if ((firstByte & 0xF8U) == 0xF0U) {
            codePoint = firstByte & 0x07U;
            sequenceLength = 4;
        } else {
            throw std::invalid_argument("Invalid UTF-8 sequence.");
        }

        if (i + sequenceLength > text.size()) {
            throw std::invalid_argument("Truncated UTF-8 sequence.");
        }
        for (std::size_t j = 1; j < sequenceLength; ++j) {
            const auto continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation & 0xC0U) != 0x80U) {
                throw std::invalid_argument("Invalid UTF-8 continuation byte.");
            }
            codePoint = (codePoint << 6U) | (continuation & 0x3FU);
        }

        const bool overlong =
            (sequenceLength == 2 && codePoint < 0x80U) ||
            (sequenceLength == 3 && codePoint < 0x800U) ||
            (sequenceLength == 4 && codePoint < 0x10000U);
        const bool invalidScalar =
            codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU);
        if (overlong || invalidScalar) {
            throw std::invalid_argument("Invalid UTF-8 code point.");
        }

        result.push_back(codePoint);
        i += sequenceLength;
    }
    return result;
}
std::unordered_map<std::uint32_t,std::uint16_t> ParseCMap(const std::vector<std::uint8_t>& b,const Table& cmap){
    const auto base=cmap.offset; const auto count=Read16(b,base+2); std::size_t f12=0,f4=0;
    for(std::size_t i=0;i<count;++i){auto p=base+4+i*8;if(p+8>base+cmap.length)break;auto sub=base+Read32(b,p+4);if(sub+2>b.size())continue;auto f=Read16(b,sub);if(f==12)f12=sub;else if(f==4&&!f4)f4=sub;}
    std::unordered_map<std::uint32_t,std::uint16_t> r;
    if(f12){auto groups=Read32(b,f12+12);for(std::uint32_t i=0;i<groups;++i){auto p=f12+16+std::size_t(i)*12;auto first=Read32(b,p),last=Read32(b,p+4),g=Read32(b,p+8);if(last<first||last-first>200000)continue;for(auto cp=first;cp<=last;++cp){auto gid=g+cp-first;if(gid&&gid<=65535)r.emplace(cp,static_cast<std::uint16_t>(gid));if(cp==0xFFFFFFFF)break;}}}
    else if(f4){const std::size_t seg = Read16(b, f4 + 6) / 2U;auto end=f4+14,start=end+seg*2+2,delta=start+seg*2,range=delta+seg*2;for(std::size_t i=0;i<seg;++i){auto first=Read16(b,start+i*2),last=Read16(b,end+i*2),d=Read16(b,delta+i*2),ro=Read16(b,range+i*2);for(std::uint32_t cp=first;cp<=last&&cp!=0xFFFF;++cp){std::uint16_t gid=0;if(!ro)gid=static_cast<std::uint16_t>((cp+d)&0xFFFF);else{auto loc=range+i*2+ro+(cp-first)*2;if(loc+2<=b.size()){gid=Read16(b,loc);if(gid)gid=static_cast<std::uint16_t>((gid+d)&0xFFFF);}}if(gid)r.emplace(cp,gid);}}}
    if (r.empty()) {
        throw std::runtime_error("Unsupported or empty TrueType cmap table.");
    }
    return r;
}
}
PdfTrueTypeFont PdfTrueTypeFont::Load(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot open TrueType font: "+path.string());return Parse(std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),{}),path.filename().string());}
PdfTrueTypeFont PdfTrueTypeFont::Parse(std::vector<std::uint8_t> bytes,std::string sourceName){
    const auto tables=ParseTables(bytes); auto require=[&](const char* tag)->const Table&{auto it=tables.find(tag);if(it==tables.end())throw std::runtime_error(std::string("TrueType font is missing ")+tag+" table.");return it->second;};
    PdfTrueTypeFont f;f.sourceName_=std::move(sourceName);f.unicodeToGlyph_=ParseCMap(bytes,require("cmap"));
    const auto& head=require("head");f.metrics_.unitsPerEm=Read16(bytes,head.offset+18);if(!f.metrics_.unitsPerEm)throw std::runtime_error("TrueType unitsPerEm is zero.");
    const auto& maxp=require("maxp");f.metrics_.glyphCount=Read16(bytes,maxp.offset+4);
    const auto& hhea=require("hhea");f.metrics_.ascent=ReadS16(bytes,hhea.offset+4);f.metrics_.descent=ReadS16(bytes,hhea.offset+6);f.metrics_.lineGap=ReadS16(bytes,hhea.offset+8);const auto longCount=Read16(bytes,hhea.offset+34);
    const auto& hmtx=require("hmtx"); if(!longCount||longCount>f.metrics_.glyphCount)throw std::runtime_error("Invalid TrueType horizontal metrics count.");
    f.advanceWidths_.resize(f.metrics_.glyphCount);std::uint16_t last=0;for(std::uint16_t i=0;i<longCount;++i){last=Read16(bytes,hmtx.offset+std::size_t(i)*4);f.advanceWidths_[i]=last;}for(std::size_t i=longCount;i<f.advanceWidths_.size();++i)f.advanceWidths_[i]=last;
    f.bytes_=std::move(bytes);return f;
}
const std::string& PdfTrueTypeFont::GetSourceName()const noexcept{return sourceName_;} const std::vector<std::uint8_t>& PdfTrueTypeFont::GetBytes()const noexcept{return bytes_;}
const PdfTrueTypeMetrics& PdfTrueTypeFont::GetMetrics()const noexcept{return metrics_;} std::size_t PdfTrueTypeFont::GetGlyphMappingCount()const noexcept{return unicodeToGlyph_.size();}
bool PdfTrueTypeFont::Supports(std::uint32_t cp)const noexcept{return unicodeToGlyph_.contains(cp);} std::optional<std::uint16_t> PdfTrueTypeFont::GetGlyphId(std::uint32_t cp)const noexcept{auto it=unicodeToGlyph_.find(cp);return it==unicodeToGlyph_.end()?std::nullopt:std::optional<std::uint16_t>(it->second);}
std::uint16_t PdfTrueTypeFont::GetAdvanceWidth(std::uint16_t gid)const noexcept{return gid<advanceWidths_.size()?advanceWidths_[gid]:0;}
double PdfTrueTypeFont::GetAdvanceWidth(std::uint16_t gid,double size)const{if(size<=0||!std::isfinite(size))throw std::invalid_argument("Font size must be positive and finite.");return double(GetAdvanceWidth(gid))*size/metrics_.unitsPerEm;}
double PdfTrueTypeFont::MeasureTextUtf8(std::string_view text,double size)const{if(size<=0||!std::isfinite(size))throw std::invalid_argument("Font size must be positive and finite.");double w=0;for(auto cp:DecodeUtf8(text)){auto gid=GetGlyphId(cp);if(!gid)throw std::invalid_argument("The TrueType font does not contain a requested Unicode code point.");w+=GetAdvanceWidth(*gid,size);}return w;}
double PdfTrueTypeFont::GetLineHeight(double size,double spacing)const{if(size<=0||!std::isfinite(size)||spacing<=0||!std::isfinite(spacing))throw std::invalid_argument("Font size and line spacing must be positive and finite.");return double(metrics_.ascent-metrics_.descent+metrics_.lineGap)*size/metrics_.unitsPerEm*spacing;}

PdfTrueTypeSubset PdfTrueTypeFont::BuildSubset(std::span<const std::uint16_t> glyphIds) const {
    PdfTrueTypeSubset result; result.originalByteSize = bytes_.size();
    const auto tables = ParseTables(bytes_);
    const auto glyfIt = tables.find("glyf"), locaIt = tables.find("loca"), headIt = tables.find("head");
    if (glyfIt == tables.end() || locaIt == tables.end() || headIt == tables.end()) {
        result.bytes = bytes_; result.subsetApplied = false; return result;
    }
    const auto glyphCount = metrics_.glyphCount;
    std::vector<bool> keep(glyphCount, false); if (glyphCount) keep[0] = true;
    for (const auto gid : glyphIds) { if (gid >= glyphCount) throw std::out_of_range("TrueType glyph ID is out of range."); keep[gid] = true; }
    const auto locaFormat = ReadS16(bytes_, headIt->second.offset + 50);
    std::vector<std::uint32_t> offsets(std::size_t(glyphCount) + 1U);
    for (std::size_t i = 0; i < offsets.size(); ++i) offsets[i] = locaFormat == 0 ? std::uint32_t(Read16(bytes_, locaIt->second.offset + i * 2U)) * 2U : Read32(bytes_, locaIt->second.offset + i * 4U);
    auto addCompositeDependencies = [&](auto&& self, std::uint16_t gid) -> void {
        if (gid >= glyphCount || offsets[gid + 1U] <= offsets[gid]) return;
        const std::size_t start = glyfIt->second.offset + offsets[gid];
        if (ReadS16(bytes_, start) >= 0) return;
        std::size_t p = start + 10U; bool more = true;
        while (more) {
            const auto flags = Read16(bytes_, p); const auto component = Read16(bytes_, p + 2U); p += 4U;
            if (component < glyphCount && !keep[component]) { keep[component] = true; self(self, component); }
            p += (flags & 0x0001U) ? 4U : 2U;
            if (flags & 0x0008U) p += 2U; else if (flags & 0x0040U) p += 4U; else if (flags & 0x0080U) p += 8U;
            more = (flags & 0x0020U) != 0U;
            if (p > glyfIt->second.offset + glyfIt->second.length) throw std::runtime_error("Invalid composite glyph data.");
        }
    };
    for (std::uint16_t gid = 0; gid < glyphCount; ++gid) if (keep[gid]) addCompositeDependencies(addCompositeDependencies, gid);
    std::vector<std::uint8_t> newGlyf; std::vector<std::uint32_t> newOffsets(std::size_t(glyphCount) + 1U);
    for (std::uint16_t gid = 0; gid < glyphCount; ++gid) {
        newOffsets[gid] = static_cast<std::uint32_t>(newGlyf.size());
        if (keep[gid] && offsets[gid + 1U] > offsets[gid]) {
            const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(glyfIt->second.offset + offsets[gid]);
            const auto end = bytes_.begin() + static_cast<std::ptrdiff_t>(glyfIt->second.offset + offsets[gid + 1U]);
            newGlyf.insert(newGlyf.end(), begin, end);
            const auto alignment = locaFormat == 0 ? 2U : 4U; while (newGlyf.size() % alignment) newGlyf.push_back(0);
        }
    }
    newOffsets[glyphCount] = static_cast<std::uint32_t>(newGlyf.size());
    std::vector<std::uint8_t> newLoca((std::size_t(glyphCount) + 1U) * (locaFormat == 0 ? 2U : 4U));
    for (std::size_t i = 0; i < newOffsets.size(); ++i) {
        if (locaFormat == 0) { if (newOffsets[i] / 2U > 0xFFFFU) { result.bytes = bytes_; return result; } Write16(newLoca, i * 2U, static_cast<std::uint16_t>(newOffsets[i] / 2U)); }
        else Write32(newLoca, i * 4U, newOffsets[i]);
    }
    std::vector<TableRecord> records; const auto tableCount = Read16(bytes_, 4);
    for (std::size_t i = 0; i < tableCount; ++i) {
        const auto p = 12U + i * 16U; std::string tag(reinterpret_cast<const char*>(bytes_.data() + p), 4);
        if (tag == "DSIG") {
            continue;
        }
        const auto& table = tables.at(tag);
        std::vector<std::uint8_t> data;
        if (tag == "glyf") {
            data = newGlyf;
        } else if (tag == "loca") {
            data = newLoca;
        } else {
            data.assign(
                bytes_.begin() + static_cast<std::ptrdiff_t>(table.offset),
                bytes_.begin() + static_cast<std::ptrdiff_t>(table.offset + table.length));
        }
        if (tag == "head" && data.size() >= 12U) {
            Write32(data, 8U, 0U);
        }
        records.push_back({tag, std::move(data)});
    }
    const auto n = static_cast<std::uint16_t>(records.size()); std::uint16_t maxPow2 = 1, entrySelector = 0; while (std::uint16_t(maxPow2 * 2U) <= n) { maxPow2 *= 2U; ++entrySelector; }
    std::vector<std::uint8_t> out(12U + std::size_t(n) * 16U); std::copy_n(bytes_.begin(), 4, out.begin()); Write16(out,4,n); Write16(out,6,std::uint16_t(maxPow2*16U)); Write16(out,8,entrySelector); Write16(out,10,std::uint16_t(n*16U-maxPow2*16U));
    std::size_t cursor = out.size(), headOffset = 0;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto dir = 12U + i * 16U; std::copy(records[i].tag.begin(), records[i].tag.end(), out.begin() + static_cast<std::ptrdiff_t>(dir));
        Write32(out, dir + 4U, Checksum(records[i].data)); Write32(out, dir + 8U, static_cast<std::uint32_t>(cursor)); Write32(out, dir + 12U, static_cast<std::uint32_t>(records[i].data.size()));
        if (records[i].tag == "head") {
            headOffset = cursor;
        }
        out.insert(out.end(), records[i].data.begin(), records[i].data.end());
        while (out.size() < Align4(out.size())) {
            out.push_back(0);
        }
        cursor = out.size();
    }
    if (headOffset) Write32(out, headOffset + 8U, 0xB1B0AFBAU - Checksum(out));
    for (std::uint16_t gid = 0; gid < glyphCount; ++gid) if (keep[gid]) result.glyphIds.push_back(gid);
    result.bytes = std::move(out); result.subsetApplied = result.bytes.size() < bytes_.size(); if (!result.subsetApplied) result.bytes = bytes_;
    return result;
}

} // namespace CPPPdf
