#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>
#include <algorithm>
#include <cmath>
#include <bit>
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
    const auto& maxp=require("maxp");f.metrics_.glyphCount=Read16(bytes,maxp.offset+4); f.outlineCache_.reserve(f.metrics_.glyphCount);
    const auto& hhea=require("hhea");f.metrics_.ascent=ReadS16(bytes,hhea.offset+4);f.metrics_.descent=ReadS16(bytes,hhea.offset+6);f.metrics_.lineGap=ReadS16(bytes,hhea.offset+8);const auto longCount=Read16(bytes,hhea.offset+34);
    const auto& hmtx=require("hmtx"); if(!longCount||longCount>f.metrics_.glyphCount)throw std::runtime_error("Invalid TrueType horizontal metrics count.");
    f.advanceWidths_.resize(f.metrics_.glyphCount);std::uint16_t last=0;for(std::uint16_t i=0;i<longCount;++i){last=Read16(bytes,hmtx.offset+std::size_t(i)*4);f.advanceWidths_[i]=last;}for(std::size_t i=longCount;i<f.advanceWidths_.size();++i)f.advanceWidths_[i]=last;
    if(const auto kern=tables.find("kern");kern!=tables.end()&&kern->second.length>=4U){
        // Apple kern table: version (uint16), nTables (uint16), then subtables.
        const std::size_t tableBase=kern->second.offset;
        const std::uint16_t version=Read16(bytes,tableBase);
        const std::uint16_t nTables=Read16(bytes,tableBase+2);
        if(version==0U){
            std::size_t cursor=tableBase+4;
            for(std::uint16_t t=0;t<nTables&&cursor+6<=kern->second.offset+kern->second.length;++t){
                const std::uint16_t subVersion=Read16(bytes,cursor);
                const std::uint16_t length=Read16(bytes,cursor+2);
                const std::uint16_t coverage=Read16(bytes,cursor+4);
                // Format 0, horizontal, not override flags: (format&0xFF)==0, coverage&0x1.
                const bool horizontal=(coverage&0x1U)!=0U;
                const std::uint8_t format=static_cast<std::uint8_t>(coverage>>8U);
                if(subVersion==0U&&format==0U&&horizontal&&length>=14U){
                    const std::uint16_t nPairs=Read16(bytes,cursor+6);
                    const std::size_t pairsOffset=cursor+14;
                    for(std::uint16_t p=0;p<nPairs;++p){
                        const std::size_t at=pairsOffset+std::size_t(p)*6U;
                        if(at+6>kern->second.offset+kern->second.length)break;
                        const std::uint16_t left=Read16(bytes,at);
                        const std::uint16_t right=Read16(bytes,at+2);
                        const std::int16_t value=ReadS16(bytes,at+4);
                        if(value!=0)f.kerning_.emplace((std::uint64_t(left)<<16U)|right,value);
                    }
                }
                cursor+=length;
            }
        }
    }
    if(const auto gsub=tables.find("GSUB");gsub!=tables.end()&&gsub->second.length>=10U){
        // GSUB header: version, scriptListOffset, featureListOffset, lookupListOffset.
        const std::size_t gb=gsub->second.offset;
        const std::uint32_t lookupListOff=Read32(bytes,gb+8U);
        if(lookupListOff+6U<=gsub->second.length){
            const std::size_t ll=gb+lookupListOff;
            const std::uint16_t lookupCount=Read16(bytes,ll);
            for(std::uint16_t l=0;l<lookupCount;++l){
                const std::uint16_t lookupOff=Read16(bytes,ll+2U+std::size_t(l)*2U);
                if(lookupOff==0U||ll+lookupOff+8U>gb+gsub->second.length)continue;
                const std::size_t lookup=ll+lookupOff;
                const std::uint16_t lookupType=Read16(bytes,lookup);
                if(lookupType!=4U)continue; // LigatureSubst
                const std::uint16_t subCount=Read16(bytes,lookup+4U);
                for(std::uint16_t s=0;s<subCount;++s){
                    const std::uint16_t subOff=Read16(bytes,lookup+6U+std::size_t(s)*2U);
                    if(subOff==0U||lookup+subOff+8U>gb+gsub->second.length)continue;
                    const std::size_t sub=lookup+subOff;
                    const std::uint16_t firstGlyph=Read16(bytes,sub);
                    const std::uint16_t ligCount=Read16(bytes,sub+2U);
                    for(std::uint16_t lig=0;lig<ligCount;++lig){
                        const std::size_t at=sub+4U+std::size_t(lig)*2U;
                        if(at+8U>gb+gsub->second.length)break;
                        const std::uint16_t ligOff=Read16(bytes,at);
                        if(ligOff==0U||sub+ligOff+6U>gb+gsub->second.length)continue;
                        const std::size_t ligEntry=sub+ligOff;
                        const std::uint16_t ligGlyph=Read16(bytes,ligEntry);
                        const std::uint16_t compCount=Read16(bytes,ligEntry+2U);
                        if(compCount<2U||compCount>16U)continue;
                        PdfTrueTypeFont::LigatureEntry entry;
                        entry.ligatureGlyph=ligGlyph;
                        entry.components.push_back(firstGlyph);
                        for(std::uint16_t c=0;c+1U<compCount;++c){
                            entry.components.push_back(Read16(bytes,ligEntry+4U+std::size_t(c)*2U));
                        }
                        f.ligatures_.push_back(std::move(entry));
                    }
                }
            }
        }
    }
    if(const auto gpos=tables.find("GPOS");gpos!=tables.end()&&gpos->second.length>=10U){
        // GPOS header: version, scriptListOffset, featureListOffset, lookupListOffset.
        const std::size_t gb=gpos->second.offset;
        const std::uint32_t lookupListOff=Read32(bytes,gb+8U);
        if(lookupListOff+6U<=gpos->second.length){
            const std::size_t ll=gb+lookupListOff;
            const std::uint16_t lookupCount=Read16(bytes,ll);
            for(std::uint16_t l=0;l<lookupCount;++l){
                const std::uint16_t lookupOff=Read16(bytes,ll+2U+std::size_t(l)*2U);
                if(lookupOff==0U||ll+lookupOff+10U>gb+gpos->second.length)continue;
                const std::size_t lookup=ll+lookupOff;
                const std::uint16_t lookupType=Read16(bytes,lookup);
                const std::uint16_t subCount=Read16(bytes,lookup+4U);
                // PairPos (lookup type 2) supplies horizontal kerning for fonts
                // that place pairs in GPOS rather than the legacy kern table.
                for(std::uint16_t s=0;s<subCount&&lookupType==2U;++s){
                    const std::uint16_t subOff=Read16(bytes,lookup+6U+std::size_t(s)*2U);
                    if(subOff==0U||lookup+subOff+6U>gb+gpos->second.length)continue;
                    const std::size_t sub=lookup+subOff;
                    const std::uint16_t format=Read16(bytes,sub);
                    if(format!=1U)continue; // PairPosFormat1
                    const std::uint16_t coverageOff=Read16(bytes,sub+2U);
                    const std::uint16_t valueFormat1=Read16(bytes,sub+4U);
                    const std::uint16_t valueFormat2=Read16(bytes,sub+6U);
                    const std::uint16_t pairSetCount=Read16(bytes,sub+8U);
                    // We only consume the xAdvance field (bit 2) of value1.
                    const bool hasXAdvance1=(valueFormat1&0x0004U)!=0U;
                    if(!hasXAdvance1||coverageOff==0U||coverageOff+4U>gpos->second.length)continue;
                    // Fields appear in order of set bits; each is 2 bytes.
                    const std::size_t value1Size=std::popcount(valueFormat1)*2U;
                    const std::size_t value2Size=std::popcount(valueFormat2)*2U;
                    // Offset of xAdvance within value1 (bits 0..1 come first).
                    const std::size_t xAdvanceOffset=std::popcount(valueFormat1&0x0003U)*2U;
                    const std::size_t cov=sub+coverageOff;
                    const std::uint16_t covFormat=Read16(bytes,cov);
                    const std::uint16_t covCount=Read16(bytes,cov+2U);
                    for(std::uint16_t p=0;p<pairSetCount;++p){
                        const std::size_t pairOffAt=sub+10U+std::size_t(p)*2U;
                        if(pairOffAt+2U>gb+gpos->second.length)break;
                        const std::uint16_t pairOff=Read16(bytes,pairOffAt);
                        if(pairOff==0U||sub+pairOff+2U>gb+gpos->second.length)continue;
                        const std::size_t pairSet=sub+pairOff;
                        const std::uint16_t pairValueCount=Read16(bytes,pairSet);
                        // First glyph from coverage (format 1) or array (format 2).
                        std::uint16_t firstGlyph=0;
                        if(covFormat==1U&&p<covCount){
                            firstGlyph=Read16(bytes,cov+2U+std::size_t(p)*2U);
                        } else if(covFormat==2U){
                            // Format 2: sorted [start,end) ranges; find the range holding p.
                            const std::uint16_t rangeCount=Read16(bytes,cov+2U);
                            for(std::uint16_t r=0;r<rangeCount;++r){
                                const std::size_t at=cov+4U+std::size_t(r)*6U;
                                if(at+6U>gb+gpos->second.length)break;
                                const std::uint16_t start=Read16(bytes,at);
                                const std::uint16_t end=Read16(bytes,at+2U);
                                if(p>=start&&p<end){firstGlyph=Read16(bytes,at+4U)+(p-start);break;}
                            }
                        }
                        for(std::uint16_t v=0;v<pairValueCount;++v){
                            // Record layout: secondGlyph (2) + value1 + value2.
                            const std::size_t rec=pairSet+2U+std::size_t(v)*(2U+value1Size+value2Size);
                            if(rec+2U+value1Size+value2Size>gb+gpos->second.length)break;
                            const std::uint16_t secondGlyph=Read16(bytes,rec);
                            const std::int16_t xAdvance=ReadS16(bytes,rec+2U+xAdvanceOffset);
                            if(xAdvance!=0)f.kerning_.emplace((std::uint64_t(firstGlyph)<<16U)|secondGlyph,xAdvance);
                        }
                    }
                }
            }
        }
    }
    f.bytes_=std::move(bytes);return f;
}
const std::string& PdfTrueTypeFont::GetSourceName()const noexcept{return sourceName_;} const std::vector<std::uint8_t>& PdfTrueTypeFont::GetBytes()const noexcept{return bytes_;}
const PdfTrueTypeMetrics& PdfTrueTypeFont::GetMetrics()const noexcept{return metrics_;} std::size_t PdfTrueTypeFont::GetGlyphMappingCount()const noexcept{return unicodeToGlyph_.size();}
bool PdfTrueTypeFont::Supports(std::uint32_t cp)const noexcept{return unicodeToGlyph_.contains(cp);} std::optional<std::uint16_t> PdfTrueTypeFont::GetGlyphId(std::uint32_t cp)const noexcept{auto it=unicodeToGlyph_.find(cp);return it==unicodeToGlyph_.end()?std::nullopt:std::optional<std::uint16_t>(it->second);}
std::uint16_t PdfTrueTypeFont::GetAdvanceWidth(std::uint16_t gid)const noexcept{return gid<advanceWidths_.size()?advanceWidths_[gid]:0;}
bool PdfTrueTypeFont::HasTable(const std::string_view tag) const noexcept {
    if (tag.size() != 4U) return false;
    const auto tables = ParseTables(bytes_);
    return tables.find(std::string(tag)) != tables.end();
}
double PdfTrueTypeFont::GetAdvanceWidth(std::uint16_t gid,double size)const{if(size<=0||!std::isfinite(size))throw std::invalid_argument("Font size must be positive and finite.");return double(GetAdvanceWidth(gid))*size/metrics_.unitsPerEm;}
double PdfTrueTypeFont::GetCachedAdvanceWidth(std::uint16_t gid,double size)const{const auto key=(std::bit_cast<std::uint64_t>(size)*0x9E3779B97F4A7C15ULL)^gid;const auto found=advanceCache_.find(key);if(found!=advanceCache_.end()){++advanceCacheHits_;advanceLru_.splice(advanceLru_.end(),advanceLru_,found->second);return found->second->second;}++advanceCacheMisses_;if(advanceCache_.size()>=kGlyphCacheLimit){advanceCache_.erase(advanceLru_.front().first);advanceLru_.pop_front();}const auto value=GetAdvanceWidth(gid,size);advanceLru_.emplace_back(key,value);advanceCache_.emplace(key,std::prev(advanceLru_.end()));return value;}
void PdfTrueTypeFont::ClearGlyphCaches() const noexcept { outlineCache_.clear(); outlineLru_.clear(); advanceCache_.clear(); advanceLru_.clear(); kerningCache_.clear(); kerningLru_.clear(); }
double PdfTrueTypeFont::MeasureTextUtf8(std::string_view text,double size)const{if(size<=0||!std::isfinite(size))throw std::invalid_argument("Font size must be positive and finite.");double w=0;for(auto cp:DecodeUtf8(text)){auto gid=GetGlyphId(cp);if(!gid)throw std::invalid_argument("The TrueType font does not contain a requested Unicode code point.");w+=GetCachedAdvanceWidth(*gid,size);}return w;}
double PdfTrueTypeFont::GetLineHeight(double size,double spacing)const{if(size<=0||!std::isfinite(size)||spacing<=0||!std::isfinite(spacing))throw std::invalid_argument("Font size and line spacing must be positive and finite.");return double(metrics_.ascent-metrics_.descent+metrics_.lineGap)*size/metrics_.unitsPerEm*spacing;}

std::vector<std::uint16_t> PdfTrueTypeFont::ApplyLigatures(
    const std::span<const std::uint16_t> glyphs) const {
    if (ligatures_.empty() || glyphs.empty()) return {glyphs.begin(), glyphs.end()};
    std::vector<std::uint16_t> result;
    result.reserve(glyphs.size());
    std::size_t i = 0U;
    while (i < glyphs.size()) {
        bool matched = false;
        for (const auto& entry : ligatures_) {
            const std::size_t len = entry.components.size();
            if (i + len > glyphs.size()) continue;
            bool allMatch = true;
            for (std::size_t k = 0U; k < len; ++k) {
                if (glyphs[i + k] != entry.components[k]) { allMatch = false; break; }
            }
            if (allMatch) {
                result.push_back(entry.ligatureGlyph);
                i += len;
                matched = true;
                break;
            }
        }
        if (!matched) {
            result.push_back(glyphs[i]);
            ++i;
        }
    }
    return result;
}

double PdfTrueTypeFont::GetKerning(const std::uint16_t left, const std::uint16_t right,
                                   const double size) const {
    if (size <= 0.0 || !std::isfinite(size)) throw std::invalid_argument("Font size must be positive and finite.");
    const auto it = kerning_.find((std::uint64_t(left) << 16U) | right);
    if (it == kerning_.end()) return 0.0;
    return double(it->second) * size / metrics_.unitsPerEm;
}

double PdfTrueTypeFont::GetCachedKerning(const std::uint16_t left, const std::uint16_t right,
                                         const double size) const {
    const std::uint64_t pair = (std::uint64_t(left) << 16U) | right;
    const std::uint64_t key = (std::bit_cast<std::uint64_t>(size) * 0x9E3779B97F4A7C15ULL) ^ pair;
    const auto found = kerningCache_.find(key);
    if (found != kerningCache_.end()) {
        ++kerningCacheHits_;
        kerningLru_.splice(kerningLru_.end(), kerningLru_, found->second);
        return found->second->second;
    }
    ++kerningCacheMisses_;
    if (kerningCache_.size() >= kGlyphCacheLimit) {
        kerningCache_.erase(kerningLru_.front().first);
        kerningLru_.pop_front();
    }
    const double value = GetKerning(left, right, size);
    kerningLru_.emplace_back(key, value);
    kerningCache_.emplace(key, std::prev(kerningLru_.end()));
    return value;
}

PdfTrueTypeGlyphOutline PdfTrueTypeFont::ReadGlyphOutline(
    const std::uint16_t glyphId, std::unordered_set<std::uint16_t>& active) const {
    if (glyphId >= metrics_.glyphCount) throw std::out_of_range("TrueType glyph ID is out of range.");
    if (!active.insert(glyphId).second) throw std::runtime_error("Cyclic TrueType composite glyph.");
    if (active.size() > 64U) throw std::runtime_error("TrueType composite glyph depth exceeds limit.");
    const auto tables = ParseTables(bytes_);
    const auto glyfIt = tables.find("glyf");
    const auto locaIt = tables.find("loca");
    const auto headIt = tables.find("head");
    if (glyfIt == tables.end() || locaIt == tables.end() || headIt == tables.end()) {
        throw std::runtime_error("TrueType font has no glyph outline tables.");
    }
    const auto locaFormat = ReadS16(bytes_, headIt->second.offset + 50U);
    const std::size_t locaEntrySize = locaFormat == 0 ? 2U : 4U;
    const std::size_t locaPosition = locaIt->second.offset + std::size_t(glyphId) * locaEntrySize;
    const std::uint32_t startOffset = locaFormat == 0 ? std::uint32_t(Read16(bytes_, locaPosition)) * 2U
                                                       : Read32(bytes_, locaPosition);
    const std::uint32_t endOffset = locaFormat == 0
        ? std::uint32_t(Read16(bytes_, locaPosition + 2U)) * 2U
        : Read32(bytes_, locaPosition + 4U);
    if (endOffset < startOffset || endOffset > glyfIt->second.length) {
        throw std::runtime_error("Invalid TrueType glyph range.");
    }
    PdfTrueTypeGlyphOutline result;
    const std::size_t glyphStart = glyfIt->second.offset + startOffset;
    if (startOffset == endOffset) { active.erase(glyphId); return result; }
    const auto contours = ReadS16(bytes_, glyphStart);
    result.xMin = ReadS16(bytes_, glyphStart + 2U);
    result.yMin = ReadS16(bytes_, glyphStart + 4U);
    result.xMax = ReadS16(bytes_, glyphStart + 6U);
    result.yMax = ReadS16(bytes_, glyphStart + 8U);
    if (contours < 0) {
        result.composite = true;
        std::size_t position = glyphStart + 10U;
        const std::size_t glyphEnd = glyfIt->second.offset + endOffset;
        bool moreComponents = true;
        std::uint16_t lastFlags{};
        while (moreComponents) {
            if (position + 4U > glyphEnd) throw std::runtime_error("Invalid TrueType composite glyph.");
            const auto flags = Read16(bytes_, position);
            lastFlags = flags;
            const auto componentGlyph = Read16(bytes_, position + 2U);
            position += 4U;
            PdfTrueTypeGlyphOutline::Component component;
            component.glyphId = componentGlyph;
            component.argumentsAreXY = (flags & 0x0002U) != 0U;
            component.roundToGrid = (flags & 0x0004U) != 0U;
            if ((flags & 0x0001U) != 0U) {
                component.argument1 = component.argumentsAreXY
                    ? ReadS16(bytes_, position) : Read16(bytes_, position);
                component.argument2 = component.argumentsAreXY
                    ? ReadS16(bytes_, position + 2U) : Read16(bytes_, position + 2U);
                position += 4U;
            } else {
                if (position + 2U > glyphEnd) throw std::runtime_error("Invalid TrueType composite arguments.");
                component.argument1 = component.argumentsAreXY
                    ? static_cast<std::int8_t>(bytes_[position]) : bytes_[position];
                component.argument2 = component.argumentsAreXY
                    ? static_cast<std::int8_t>(bytes_[position + 1U]) : bytes_[position + 1U];
                position += 2U;
            }
            auto readScale = [&]() {
                return static_cast<double>(ReadS16(bytes_, position)) / 16384.0;
            };
            if ((flags & 0x0008U) != 0U) {
                if (position + 2U > glyphEnd) throw std::runtime_error("Invalid TrueType composite scale.");
                component.xx = component.yy = readScale();
                position += 2U;
            } else if ((flags & 0x0040U) != 0U) {
                if (position + 4U > glyphEnd) throw std::runtime_error("Invalid TrueType composite scale.");
                component.xx = readScale(); position += 2U;
                component.yy = readScale(); position += 2U;
            } else if ((flags & 0x0080U) != 0U) {
                if (position + 8U > glyphEnd) throw std::runtime_error("Invalid TrueType composite transform.");
                component.xx = readScale(); position += 2U;
                component.xy = readScale(); position += 2U;
                component.yx = readScale(); position += 2U;
                component.yy = readScale(); position += 2U;
            }
            result.components.push_back(component);
            moreComponents = (flags & 0x0020U) != 0U;
        }
        if (position + 2U <= glyphEnd && (lastFlags & 0x0100U) != 0U) {
            const auto instructionLength = Read16(bytes_, position);
            if (position + 2U + instructionLength > glyphEnd)
                throw std::runtime_error("Invalid TrueType composite instructions.");
        }
        for (const auto& component : result.components) {
            const auto child = ReadGlyphOutline(component.glyphId, active);
            std::vector<PdfTrueTypePoint> parentPoints;
            for (const auto& contour : result.contours)
                parentPoints.insert(parentPoints.end(), contour.begin(), contour.end());
            std::vector<PdfTrueTypePoint> childPoints;
            for (const auto& contour : child.contours)
                childPoints.insert(childPoints.end(), contour.begin(), contour.end());
            double translateX = component.argumentsAreXY ? component.argument1 : 0.0;
            double translateY = component.argumentsAreXY ? component.argument2 : 0.0;
            if (!component.argumentsAreXY && component.argument1 >= 0 && component.argument2 >= 0 &&
                static_cast<std::size_t>(component.argument1) < parentPoints.size() &&
                static_cast<std::size_t>(component.argument2) < childPoints.size()) {
                const auto& parentPoint = parentPoints[static_cast<std::size_t>(component.argument1)];
                const auto& childPoint = childPoints[static_cast<std::size_t>(component.argument2)];
                const double transformedX = component.xx * childPoint.x + component.xy * childPoint.y;
                const double transformedY = component.yx * childPoint.x + component.yy * childPoint.y;
                translateX = parentPoint.x - transformedX;
                translateY = parentPoint.y - transformedY;
            }
            for (const auto& sourceContour : child.contours) {
                auto& contour = result.contours.emplace_back();
                contour.reserve(sourceContour.size());
                for (const auto point : sourceContour) {
                    const double x = component.xx * point.x + component.xy * point.y +
                        translateX;
                    const double y = component.yx * point.x + component.yy * point.y +
                        translateY;
                    contour.push_back({static_cast<std::int16_t>(std::lround(x)),
                                       static_cast<std::int16_t>(std::lround(y)), point.onCurve});
                }
            }
        }
        active.erase(glyphId);
        return result;
    }
    const std::size_t contourCount = static_cast<std::size_t>(contours);
    if (contourCount > 65535U) throw std::runtime_error("TrueType glyph has too many contours.");
    std::size_t position = glyphStart + 10U;
    if (position + contourCount * 2U + 2U > glyfIt->second.offset + endOffset) {
        throw std::runtime_error("Invalid TrueType simple glyph header.");
    }
    std::vector<std::uint16_t> ends(contourCount);
    for (auto& end : ends) { end = Read16(bytes_, position); position += 2U; }
    const auto instructionLength = Read16(bytes_, position);
    position += 2U;
    const std::size_t glyphEnd = glyfIt->second.offset + endOffset;
    if (position + instructionLength > glyphEnd) throw std::runtime_error("Invalid TrueType glyph instructions.");
    position += instructionLength;
    const std::size_t pointCount = contourCount == 0U ? 0U : static_cast<std::size_t>(ends.back()) + 1U;
    if (pointCount > 1'000'000U) throw std::runtime_error("TrueType glyph has too many points.");
    std::vector<std::uint8_t> flags;
    flags.reserve(pointCount);
    while (flags.size() < pointCount) {
        if (position >= glyphEnd) throw std::runtime_error("Invalid TrueType glyph flags.");
        const auto flag = bytes_[position++];
        flags.push_back(flag);
        if ((flag & 0x08U) != 0U) {
            if (position >= glyphEnd) throw std::runtime_error("Invalid TrueType glyph flag repeat.");
            const auto repeat = bytes_[position++];
            if (flags.size() + repeat > pointCount) throw std::runtime_error("TrueType glyph flag repeat exceeds points.");
            for (std::size_t i = 0; i < repeat; ++i) flags.push_back(flag);
        }
    }
    std::vector<std::int16_t> x(pointCount), y(pointCount);
    std::int32_t currentX = 0;
    for (std::size_t i = 0; i < pointCount; ++i) {
        const auto flag = flags[i];
        std::int32_t delta = 0;
        if ((flag & 0x02U) != 0U) {
            if (position >= glyphEnd) throw std::runtime_error("Invalid TrueType X coordinates.");
            const auto value = bytes_[position++];
            delta = (flag & 0x10U) != 0U ? value : -static_cast<std::int32_t>(value);
        } else if ((flag & 0x10U) == 0U) {
            delta = ReadS16(bytes_, position);
            position += 2U;
        }
        currentX += delta;
        if (currentX < std::numeric_limits<std::int16_t>::min() || currentX > std::numeric_limits<std::int16_t>::max())
            throw std::runtime_error("TrueType X coordinate overflow.");
        x[i] = static_cast<std::int16_t>(currentX);
    }
    std::int32_t currentY = 0;
    for (std::size_t i = 0; i < pointCount; ++i) {
        const auto flag = flags[i];
        std::int32_t delta = 0;
        if ((flag & 0x04U) != 0U) {
            if (position >= glyphEnd) throw std::runtime_error("Invalid TrueType Y coordinates.");
            const auto value = bytes_[position++];
            delta = (flag & 0x20U) != 0U ? value : -static_cast<std::int32_t>(value);
        } else if ((flag & 0x20U) == 0U) {
            delta = ReadS16(bytes_, position);
            position += 2U;
        }
        currentY += delta;
        if (currentY < std::numeric_limits<std::int16_t>::min() || currentY > std::numeric_limits<std::int16_t>::max())
            throw std::runtime_error("TrueType Y coordinate overflow.");
        y[i] = static_cast<std::int16_t>(currentY);
    }
    std::size_t begin = 0;
    for (const auto end : ends) {
        if (end < begin || end >= pointCount) throw std::runtime_error("Invalid TrueType contour endpoint.");
        auto& contour = result.contours.emplace_back();
        contour.reserve(static_cast<std::size_t>(end) - begin + 1U);
        for (std::size_t i = begin; i <= end; ++i) contour.push_back({x[i], y[i], (flags[i] & 1U) != 0U});
        begin = static_cast<std::size_t>(end) + 1U;
    }
    active.erase(glyphId);
    return result;
}

PdfTrueTypeGlyphOutline PdfTrueTypeFont::GetGlyphOutline(const std::uint16_t glyphId) const {
    return GetGlyphOutlineCached(glyphId);
}

const PdfTrueTypeGlyphOutline& PdfTrueTypeFont::GetGlyphOutlineCached(
    const std::uint16_t glyphId) const {
    if (const auto cached = outlineCache_.find(glyphId); cached != outlineCache_.end()) {
        ++outlineCacheHits_;
        outlineLru_.splice(outlineLru_.end(), outlineLru_, cached->second);
        return cached->second->second;
    }
    ++outlineCacheMisses_;
    if (outlineCache_.size() >= kGlyphCacheLimit) {
        outlineCache_.erase(outlineLru_.front().first);
        outlineLru_.pop_front();
    }
    std::unordered_set<std::uint16_t> active;
    auto outline = ReadGlyphOutline(glyphId, active);
    outlineLru_.emplace_back(glyphId, std::move(outline));
    auto entry = std::prev(outlineLru_.end());
    outlineCache_.emplace(glyphId, entry);
    return entry->second;
}

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
