#include <CPPPdf/CPPPdf.h>
#include "TestRunner.hpp"

#include <array>
#include <cstddef>
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::vector<std::byte> makeMinimalPdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 5> offsets{};

    auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };

    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>");
    addObject(4, "<< /Length 17 >>\nstream\nBT (Hello) Tj ET\nendstream");

    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 0\n0 5\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();

    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) {
        result[i] = static_cast<std::byte>(pdf[i]);
    }
    return result;
}

std::vector<std::byte> makeObjectStreamPdf() {
    std::string pdf = "%PDF-1.7\n";
    std::array<std::size_t, 12> offsets{};

    auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };
    auto makeObjectStream = [](const std::uint32_t firstNumber,
                               const std::uint32_t secondNumber,
                               const std::string_view firstBody,
                               const std::string_view secondBody) {
        const std::string header = std::to_string(firstNumber) + " 0 " +
            std::to_string(secondNumber) + " " + std::to_string(firstBody.size() + 1U) + " ";
        const std::string decoded = header + std::string(firstBody) + " " + std::string(secondBody);
        return "<< /Type /ObjStm /N 2 /First " + std::to_string(header.size()) +
            " /Length " + std::to_string(decoded.size()) + " >>\nstream\n" +
            decoded + "\nendstream";
    };

    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] >>");
    addObject(6, makeObjectStream(4, 5, "<< /Value (One) >>", "<< /Value (Two) >>"));
    addObject(10, makeObjectStream(8, 9, "<< /Value (Three) >>", "<< /Value (Four) >>"));

    offsets[11] = pdf.size();
    std::string xrefData;
    xrefData.reserve(12U * 7U);
    auto appendBigEndian = [&xrefData](std::uint64_t value, const std::size_t width) {
        for (std::size_t i = width; i > 0U; --i) {
            xrefData.push_back(static_cast<char>((value >> ((i - 1U) * 8U)) & 0xFFU));
        }
    };
    auto addXrefEntry = [&](const std::uint8_t type,
                            const std::uint32_t field2,
                            const std::uint16_t field3) {
        appendBigEndian(type, 1U);
        appendBigEndian(field2, 4U);
        appendBigEndian(field3, 2U);
    };
    for (std::uint32_t number = 0U; number < offsets.size(); ++number) {
        if (number == 0U) {
            addXrefEntry(0U, 0U, 65535U);
        } else if (number == 4U || number == 5U) {
            addXrefEntry(2U, 6U, static_cast<std::uint16_t>(number - 4U));
        } else if (number == 8U || number == 9U) {
            addXrefEntry(2U, 10U, static_cast<std::uint16_t>(number - 8U));
        } else if (offsets[number] != 0U) {
            addXrefEntry(1U, static_cast<std::uint32_t>(offsets[number]), 0U);
        } else {
            addXrefEntry(0U, 0U, 0U);
        }
    }
    pdf += "11 0 obj\n<< /Type /XRef /Size 12 /Root 1 0 R /W [1 4 2] /Length " +
        std::to_string(xrefData.size()) + " >>\nstream\n";
    pdf += xrefData;
    pdf += "\nendstream\nendobj\nstartxref\n" + std::to_string(offsets[11]) + "\n%%EOF\n";

    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) {
        result[i] = static_cast<std::byte>(static_cast<unsigned char>(pdf[i]));
    }
    return result;
}


std::vector<std::byte> makeFontResourcePdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 6> offsets{};

    auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };

    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                 "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>");
    const std::string content = "BT /F1 10 Tf 1 0 0 1 72 720 Tm (AA) Tj ET";
    addObject(4, "<< /Length " + std::to_string(content.size()) + ">>\nstream\n" +
                 content + "\nendstream");
    addObject(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                 "/FirstChar 65 /Widths [600] "
                 "/Encoding << /BaseEncoding /WinAnsiEncoding /Differences [65 /Omega] >> >>");

    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 6\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();

    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) result[i] = static_cast<std::byte>(pdf[i]);
    return result;
}


std::vector<std::byte> makeFormXObjectPdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 8> offsets{};

    auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };

    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                 "/Resources << /XObject << /Fm1 5 0 R >> >> /Contents 4 0 R >>");
    const std::string pageContent = "/Fm1 Do";
    addObject(4, "<< /Length " + std::to_string(pageContent.size()) + ">>\nstream\n" +
                 pageContent + "\nendstream");
    const std::string formContent = "/GS1 gs BT /F2 10 Tf 1 0 0 1 5 7 Tm (AA) Tj ET";
    addObject(5, "<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] "
                 "/Matrix [2 0 0 2 100 200] "
                  "/Resources << /Font << /F2 6 0 R >> /ExtGState << /GS1 7 0 R >> >> "
                 "/Length " + std::to_string(formContent.size()) + ">>\nstream\n" +
                 formContent + "\nendstream");
    addObject(6, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                 "/FirstChar 65 /Widths [600] "
                  "/Encoding << /BaseEncoding /WinAnsiEncoding /Differences [65 /Omega] >> >>");
    addObject(7, "<< /ca 0.25 /CA 0.5 >>");

    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 8\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();

    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) result[i] = static_cast<std::byte>(pdf[i]);
    return result;
}


std::vector<std::byte> makeInlineImagePdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 5> offsets{};
    auto addObject = [&](const std::size_t number, const std::string& body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
    };
    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 4 0 R >>");
    std::string content = "q 20 0 0 30 10 15 cm BI /W 2 /H 1 /BPC 8 /CS /RGB ID ";
    const std::array<unsigned char, 6> pixels{1, 2, 3, 4, 5, 6};
    for (const auto value : pixels) content.push_back(static_cast<char>(value));
    content += " EI Q";
    addObject(4, "<< /Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 5\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i)
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    xref << "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();
    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) result[i] = static_cast<std::byte>(pdf[i]);
    return result;
}

std::vector<std::byte> makeSoftMaskImagePdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 7> offsets{};
    auto addObject = [&](const std::size_t number, const std::string& body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
    };
    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Resources << /XObject << /Im1 5 0 R >> >> /Contents 4 0 R >>");
    const std::string content = "10 0 0 10 5 6 cm /Im1 Do";
    addObject(4, "<< /Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
    std::string imageBody = "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 6 0 R /Length 3 >>\nstream\n";
    imageBody.push_back(char(10)); imageBody.push_back(char(20)); imageBody.push_back(char(30)); imageBody += "\nendstream";
    addObject(5, imageBody);
    std::string maskBody = "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8 /Length 1 >>\nstream\n";
    maskBody.push_back(char(128)); maskBody += "\nendstream";
    addObject(6, maskBody);
    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref; xref << "xref\n0 7\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    xref << "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();
    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) result[i] = static_cast<std::byte>(static_cast<unsigned char>(pdf[i]));
    return result;
}

} // namespace

int TestMinimalPdfParsing() {
    const auto bytes = makeMinimalPdf();
    auto document = CPPPdf::PdfDocument::Open(std::span<const std::byte>(bytes));

    if (document.GetVersion() != "1.4" || document.GetPageCount() != 1U) {
        std::cerr << "Minimal PDF metadata was parsed incorrectly.\n";
        return 1;
    }

    const auto page = document.GetPage(0U);
    if (page.GetMediaBox().width() != 612.0 || page.GetMediaBox().height() != 792.0) {
        std::cerr << "Page geometry was parsed incorrectly.\n";
        return 2;
    }

    if (document.GetPageText(0U).find("Hello") == std::string::npos) {
        std::cerr << "Page text extraction failed.\n";
        return 3;
    }
    if (document.GetContentStreamCacheHits() == 0U ||
        document.GetCachedContentStreamCount() == 0U) {
        std::cerr << "Decoded content-stream cache did not reuse the page stream.\n";
        return 26;
    }
    return 0;
}

int TestXrefRecovery() {
const auto bytes = makeMinimalPdf();
auto document = CPPPdf::PdfDocument::Open(std::span<const std::byte>(bytes));
    auto damagedBytes = bytes;
    std::string damagedPdf(reinterpret_cast<const char*>(damagedBytes.data()), damagedBytes.size());
    const std::size_t startxref = damagedPdf.rfind("startxref");
    const std::size_t offsetBegin = startxref == std::string::npos
        ? std::string::npos : damagedPdf.find_first_of("0123456789", startxref + 9U);
    if (offsetBegin != std::string::npos) {
        const std::size_t offsetEnd = damagedPdf.find_first_not_of("0123456789", offsetBegin);
        damagedPdf.replace(offsetBegin, offsetEnd - offsetBegin,
                           offsetEnd - offsetBegin, '0');
        for (std::size_t index = 0; index < damagedPdf.size(); ++index) {
            damagedBytes[index] = static_cast<std::byte>(
                static_cast<unsigned char>(damagedPdf[index]));
        }
    }
    auto recoveredDocument = CPPPdf::PdfDocument::Open(
        std::span<const std::byte>(damagedBytes));
    if (recoveredDocument.GetPageCount() != 1U) {
        std::cerr << "Damaged startxref recovery did not restore the page tree.\n";
        return 28;
    }

    auto malformedXrefBytes = bytes;
    std::string malformedXref(reinterpret_cast<const char*>(malformedXrefBytes.data()),
                              malformedXrefBytes.size());
    const std::size_t subsection = malformedXref.find("0 5\n");
    if (subsection != std::string::npos) {
        malformedXref.replace(subsection, 4U, "0 5X");
        for (std::size_t index = 0; index < malformedXref.size(); ++index) {
            malformedXrefBytes[index] = static_cast<std::byte>(
                static_cast<unsigned char>(malformedXref[index]));
        }
    }
    CPPPdf::PdfReaderOptions strictXrefOptions;
    strictXrefOptions.strictParsing = true;
    bool malformedXrefRejected = false;
    try {
        (void)CPPPdf::PdfDocument::Open(
            std::span<const std::byte>(malformedXrefBytes), strictXrefOptions);
    } catch (const CPPPdf::PdfException& error) {
        malformedXrefRejected = error.code() == CPPPdf::PdfErrorCode::MalformedXref;
    }
    if (!malformedXrefRejected) {
        std::cerr << "Strict xref parsing accepted a malformed subsection header.\n";
        return 29;
    }

    auto malformedPrevBytes = bytes;
    std::string malformedPrev(reinterpret_cast<const char*>(malformedPrevBytes.data()),
                              malformedPrevBytes.size());
    const std::size_t prevTrailer = malformedPrev.find("/Root 1 0 R >>");
    if (prevTrailer != std::string::npos) {
        malformedPrev.replace(prevTrailer, 14U, "/Root 1 0 R /Prev -1 >>");
        malformedPrevBytes.resize(malformedPrev.size());
        for (std::size_t index = 0; index < malformedPrev.size(); ++index) {
            malformedPrevBytes[index] = static_cast<std::byte>(
                static_cast<unsigned char>(malformedPrev[index]));
        }
    }
    bool malformedPrevRejected = false;
    try {
        (void)CPPPdf::PdfDocument::Open(
            std::span<const std::byte>(malformedPrevBytes), strictXrefOptions);
    } catch (const CPPPdf::PdfException& error) {
        malformedPrevRejected = error.code() == CPPPdf::PdfErrorCode::MalformedXref;
    }
    if (!malformedPrevRejected) {
        std::cerr << "Strict xref parsing accepted an invalid Prev offset.\n";
        return 31;
    }

    auto cyclicPrevBytes = bytes;
    std::string cyclicPrev(reinterpret_cast<const char*>(cyclicPrevBytes.data()), cyclicPrevBytes.size());
    const std::size_t xrefMarker = cyclicPrev.find("xref\n");
    const std::size_t cyclicTrailer = cyclicPrev.find("/Root 1 0 R >>");
    if (xrefMarker != std::string::npos && cyclicTrailer != std::string::npos) {
        cyclicPrev.replace(cyclicTrailer, 14U,
            "/Root 1 0 R /Prev " + std::to_string(xrefMarker) + " >>");
        cyclicPrevBytes.resize(cyclicPrev.size());
        for (std::size_t index = 0; index < cyclicPrev.size(); ++index) {
            cyclicPrevBytes[index] = static_cast<std::byte>(
                static_cast<unsigned char>(cyclicPrev[index]));
        }
    }
    bool cyclicPrevRejected = false;
    try {
        (void)CPPPdf::PdfDocument::Open(
            std::span<const std::byte>(cyclicPrevBytes), strictXrefOptions);
    } catch (const CPPPdf::PdfException& error) {
        cyclicPrevRejected = error.code() == CPPPdf::PdfErrorCode::MalformedXref;
    }
    if (!cyclicPrevRejected) {
        std::cerr << "Strict xref parsing accepted a cyclic revision chain.\n";
        return 32;
    }
    return 0;
}

int TestObjectCacheAndLimits() {
const auto bytes = makeMinimalPdf();
auto document = CPPPdf::PdfDocument::Open(std::span<const std::byte>(bytes));
    document.ClearObjectCache();
    (void)document.GetObject(CPPPdf::PdfReference{1U, 0U});
    (void)document.GetObject(CPPPdf::PdfReference{1U, 1U});
    if (document.GetCachedObjectCount() != 2U) {
        std::cerr << "Object cache does not distinguish generations.\n";
        return 4;
    }

    document.ClearObjectCache();
    if (document.GetCachedObjectCount() != 0U) {
        std::cerr << "Object cache did not clear.\n";
        return 5;
    }

    CPPPdf::PdfReaderOptions boundedOptions;
    boundedOptions.limits.maxCachedObjects = 1U;
    auto boundedDocument = CPPPdf::PdfDocument::Open(
        std::span<const std::byte>(bytes), boundedOptions);
    (void)boundedDocument.GetObject(CPPPdf::PdfReference{1U, 0U});
    (void)boundedDocument.GetObject(CPPPdf::PdfReference{2U, 0U});
    if (boundedDocument.GetObjectCacheCapacity() != 1U ||
        boundedDocument.GetCachedObjectCount() > 1U) {
        std::cerr << "Bounded LRU object cache exceeded its configured capacity.\n";
        return 13;
    }

    CPPPdf::PdfReaderOptions objectLimitOptions;
    objectLimitOptions.limits.maxIndirectObjectBytes = 16U;
    bool oversizedObjectRejected = false;
    try {
        auto limitedDocument = CPPPdf::PdfDocument::Open(
            std::span<const std::byte>(bytes), objectLimitOptions);
        (void)limitedDocument.GetPageCount();
    } catch (const CPPPdf::PdfException&) {
        oversizedObjectRejected = true;
    }
    if (!oversizedObjectRejected) {
        std::cerr << "Indirect-object size limit did not reject an oversized object.\n";
        return 27;
    }

    const auto objectStreamBytes = makeObjectStreamPdf();
    auto malformedXrefStreamBytes = objectStreamBytes;
    std::string malformedXrefStream(reinterpret_cast<const char*>(malformedXrefStreamBytes.data()),
                                    malformedXrefStreamBytes.size());
    const std::size_t widthMarker = malformedXrefStream.find("/W [1 4 2]");
    if (widthMarker != std::string::npos) {
        malformedXrefStream[widthMarker + 4U] = '9';
        for (std::size_t index = 0; index < malformedXrefStream.size(); ++index) {
            malformedXrefStreamBytes[index] = static_cast<std::byte>(
                static_cast<unsigned char>(malformedXrefStream[index]));
        }
    }
    CPPPdf::PdfReaderOptions strictXrefStreamOptions;
    strictXrefStreamOptions.strictParsing = true;
    bool malformedXrefStreamRejected = false;
    try {
        (void)CPPPdf::PdfDocument::Open(
            std::span<const std::byte>(malformedXrefStreamBytes), strictXrefStreamOptions);
    } catch (const CPPPdf::PdfException& error) {
        malformedXrefStreamRejected = error.code() == CPPPdf::PdfErrorCode::MalformedXref;
    }
    if (!malformedXrefStreamRejected) {
        std::cerr << "Strict xref stream parsing accepted an invalid field width.\n";
        return 30;
    }

    CPPPdf::PdfReaderOptions objectStreamOptions;
    objectStreamOptions.limits.maxCachedObjects = 0U;
    objectStreamOptions.limits.maxCachedObjectStreams = 1U;
    auto objectStreamDocument = CPPPdf::PdfDocument::Open(
        std::span<const std::byte>(objectStreamBytes), objectStreamOptions);
    (void)objectStreamDocument.GetObject(CPPPdf::PdfReference{4U, 0U});
    (void)objectStreamDocument.GetObject(CPPPdf::PdfReference{5U, 0U});
    if (objectStreamDocument.GetObjectStreamCacheMisses() != 1U ||
        objectStreamDocument.GetObjectStreamCacheHits() != 1U ||
        objectStreamDocument.GetCachedObjectStreamCount() != 1U ||
        objectStreamDocument.GetCachedObjectStreamBytes() == 0U) {
        std::cerr << "Decoded object-stream cache did not record the expected hit.\n";
        return 21;
    }
    (void)objectStreamDocument.GetObject(CPPPdf::PdfReference{8U, 0U});
    (void)objectStreamDocument.GetObject(CPPPdf::PdfReference{4U, 0U});
    if (objectStreamDocument.GetCachedObjectStreamCount() != 1U ||
        objectStreamDocument.GetObjectStreamCacheMisses() != 3U) {
        std::cerr << "Decoded object-stream LRU did not evict at its configured limit.\n";
        return 22;
    }
    objectStreamDocument.ClearObjectCache();
    if (objectStreamDocument.GetCachedObjectStreamCount() != 0U ||
        objectStreamDocument.GetCachedObjectStreamBytes() != 0U) {
        std::cerr << "Decoded object-stream cache did not clear.\n";
        return 23;
    }

    CPPPdf::PdfReaderOptions tinyObjectStreamOptions;
    tinyObjectStreamOptions.limits.maxCachedObjects = 0U;
    tinyObjectStreamOptions.limits.maxCachedObjectStreamBytes = 1U;
    auto tinyObjectStreamDocument = CPPPdf::PdfDocument::Open(
        std::span<const std::byte>(objectStreamBytes), tinyObjectStreamOptions);
    (void)tinyObjectStreamDocument.GetObject(CPPPdf::PdfReference{4U, 0U});
    (void)tinyObjectStreamDocument.GetObject(CPPPdf::PdfReference{5U, 0U});
    if (tinyObjectStreamDocument.GetCachedObjectStreamCount() != 0U ||
        tinyObjectStreamDocument.GetObjectStreamCacheMisses() != 2U) {
        std::cerr << "Decoded object-stream cache exceeded its byte budget.\n";
        return 24;
    }

    CPPPdf::PdfReaderOptions pageLimitedOptions;
    pageLimitedOptions.limits.maxPageCount = 0U;
    auto pageLimitedDocument = CPPPdf::PdfDocument::Open(
        std::span<const std::byte>(bytes), pageLimitedOptions);
    try {
        (void)pageLimitedDocument.GetPageCount();
        std::cerr << "Expected page-count limit to reject the document.\n";
        return 19;
    } catch (const CPPPdf::PdfException& error) {
        if (error.code() != CPPPdf::PdfErrorCode::InvalidPageTree) {
            std::cerr << "Page-count limit returned the wrong error code.\n";
            return 20;
        }
    }
    return 0;
}

int TestObjectStreamCache() {
    const auto objectStreamBytes = makeObjectStreamPdf();
    auto malformedXrefStreamBytes = objectStreamBytes;
    std::string malformedXrefStream(reinterpret_cast<const char*>(malformedXrefStreamBytes.data()),
                                    malformedXrefStreamBytes.size());
    const std::size_t widthMarker = malformedXrefStream.find("/W [1 4 2]");
    if (widthMarker != std::string::npos) {
        malformedXrefStream[widthMarker + 4U] = '9';
        for (std::size_t index = 0; index < malformedXrefStream.size(); ++index) {
            malformedXrefStreamBytes[index] = static_cast<std::byte>(
                static_cast<unsigned char>(malformedXrefStream[index]));
        }
    }
    CPPPdf::PdfReaderOptions strictXrefStreamOptions;
    strictXrefStreamOptions.strictParsing = true;
    bool malformedXrefStreamRejected = false;
    try {
        (void)CPPPdf::PdfDocument::Open(
            std::span<const std::byte>(malformedXrefStreamBytes), strictXrefStreamOptions);
    } catch (const CPPPdf::PdfException& error) {
        malformedXrefStreamRejected = error.code() == CPPPdf::PdfErrorCode::MalformedXref;
    }
    if (!malformedXrefStreamRejected) {
        std::cerr << "Strict xref stream parsing accepted an invalid field width.\n";
        return 30;
    }

    CPPPdf::PdfReaderOptions objectStreamOptions;
    objectStreamOptions.limits.maxCachedObjects = 0U;
    objectStreamOptions.limits.maxCachedObjectStreams = 1U;
    auto objectStreamDocument = CPPPdf::PdfDocument::Open(
        std::span<const std::byte>(objectStreamBytes), objectStreamOptions);
    (void)objectStreamDocument.GetObject(CPPPdf::PdfReference{4U, 0U});
    (void)objectStreamDocument.GetObject(CPPPdf::PdfReference{5U, 0U});
    if (objectStreamDocument.GetObjectStreamCacheMisses() != 1U ||
        objectStreamDocument.GetObjectStreamCacheHits() != 1U ||
        objectStreamDocument.GetCachedObjectStreamCount() != 1U ||
        objectStreamDocument.GetCachedObjectStreamBytes() == 0U) {
        std::cerr << "Decoded object-stream cache did not record the expected hit.\n";
        return 21;
    }
    (void)objectStreamDocument.GetObject(CPPPdf::PdfReference{8U, 0U});
    (void)objectStreamDocument.GetObject(CPPPdf::PdfReference{4U, 0U});
    if (objectStreamDocument.GetCachedObjectStreamCount() != 1U ||
        objectStreamDocument.GetObjectStreamCacheMisses() != 3U) {
        std::cerr << "Decoded object-stream LRU did not evict at its configured limit.\n";
        return 22;
    }
    objectStreamDocument.ClearObjectCache();
    if (objectStreamDocument.GetCachedObjectStreamCount() != 0U ||
        objectStreamDocument.GetCachedObjectStreamBytes() != 0U) {
        std::cerr << "Decoded object-stream cache did not clear.\n";
        return 23;
    }

    CPPPdf::PdfReaderOptions tinyObjectStreamOptions;
    tinyObjectStreamOptions.limits.maxCachedObjects = 0U;
    tinyObjectStreamOptions.limits.maxCachedObjectStreamBytes = 1U;
    auto tinyObjectStreamDocument = CPPPdf::PdfDocument::Open(
        std::span<const std::byte>(objectStreamBytes), tinyObjectStreamOptions);
    (void)tinyObjectStreamDocument.GetObject(CPPPdf::PdfReference{4U, 0U});
    (void)tinyObjectStreamDocument.GetObject(CPPPdf::PdfReference{5U, 0U});
    if (tinyObjectStreamDocument.GetCachedObjectStreamCount() != 0U ||
        tinyObjectStreamDocument.GetObjectStreamCacheMisses() != 2U) {
        std::cerr << "Decoded object-stream cache exceeded its byte budget.\n";
        return 24;
    }
    return 0;
}

int TestFontExtraction() {
    const auto fontBytes = makeFontResourcePdf();
    auto fontDocument = CPPPdf::PdfDocument::Open(std::span<const std::byte>(fontBytes));
    const auto fontChunks = fontDocument.ExtractTextChunks(0U);
    if (fontChunks.size() != 1U || fontChunks.front().utf8Text != "ΩΩ") {
        std::cerr << "Page font resources were not applied during Unicode extraction.\n";
        return 6;
    }
    const double measuredWidth = fontChunks.front().end.x - fontChunks.front().start.x;
    if (!fontChunks.front().usedEmbeddedFontMetrics ||
        fontChunks.front().glyphCount != 2U ||
        measuredWidth < 11.99 || measuredWidth > 12.01) {
        std::cerr << "Embedded font widths were not applied to text positioning.\n";
        return 7;
    }
    const auto fontChunksAgain = fontDocument.ExtractTextChunks(0U);
    if (fontChunksAgain.size() != 1U || fontDocument.GetFontResourceCacheHits() == 0U ||
        fontDocument.GetCachedFontResourceCount() != 1U) {
        std::cerr << "Document-level font resource cache did not reuse an indirect font.\n";
        return 25;
    }
    return 0;
}

int TestFormXObjectExtraction() {
    const auto formBytes = makeFormXObjectPdf();
    auto formDocument = CPPPdf::PdfDocument::Open(std::span<const std::byte>(formBytes));
    const auto formChunks = formDocument.ExtractTextChunks(0U);
    if (formChunks.size() != 1U || formChunks.front().utf8Text != "ΩΩ") {
        std::cerr << "Form XObject text or scoped font resources were not extracted.\n";
        return 8;
    }
    if (formChunks.front().sourceObjectNumber != 5U ||
        std::abs(formChunks.front().start.x - 110.0) > 0.01 ||
        std::abs(formChunks.front().start.y - 214.0) > 0.01) {
        std::cerr << "Form XObject matrix was not applied to text geometry.\n";
        return 9;
    }
    if (std::abs(formChunks.front().fillAlpha - 0.25) > 0.01 ||
        std::abs(formChunks.front().strokeAlpha - 0.5) > 0.01) {
        std::cerr << "Form XObject ExtGState alpha was not scoped correctly.\n";
        return 26;
    }
    return 0;
}

int TestInlineImageExtraction() {
    const auto inlineBytes = makeInlineImagePdf();
    auto inlineDocument = CPPPdf::PdfDocument::Open(std::span<const std::byte>(inlineBytes));
    const auto inlineImages = inlineDocument.ExtractImages(0U);
    if (inlineImages.size() != 1U || !inlineImages.front().info.inlineImage ||
        inlineImages.front().info.width != 2U || inlineImages.front().info.height != 1U ||
        inlineImages.front().decodedBytes.size() != 6U) {
        std::cerr << "Inline image extraction failed.\n";
        return 10;
    }
    if (std::abs(inlineImages.front().info.boundingBox.left - 10.0) > 0.01 ||
        std::abs(inlineImages.front().info.boundingBox.bottom - 15.0) > 0.01 ||
        std::abs(inlineImages.front().info.boundingBox.right - 30.0) > 0.01 ||
        std::abs(inlineImages.front().info.boundingBox.top - 45.0) > 0.01) {
        std::cerr << "Inline image CTM was not applied.\n";
        return 11;
    }
    return 0;
}

int TestSoftMaskImageExtraction() {
    const auto softMaskBytes = makeSoftMaskImagePdf();
    auto softMaskDocument = CPPPdf::PdfDocument::Open(std::span<const std::byte>(softMaskBytes));
    const auto maskedImages = softMaskDocument.ExtractImages(0U);
    if (maskedImages.size() != 1U || !maskedImages.front().info.hasSoftMask ||
        maskedImages.front().info.softMaskReference.objectNumber != 6U ||
        maskedImages.front().alphaBytes.size() != 1U ||
        std::to_integer<unsigned int>(maskedImages.front().alphaBytes.front()) != 128U) {
        std::cerr << "Soft-mask image extraction failed.\n";
        return 12;
    }
    if (maskedImages.front().info.fillAlpha != 1.0 ||
        maskedImages.front().info.strokeAlpha != 1.0) {
        std::cerr << "Soft-mask image graphics alpha state was not preserved.\n";
        return 27;
    }
    return 0;
}

int TestAnnotationEditor() {
const auto bytes = makeMinimalPdf();
    const auto annotationInput = std::filesystem::temp_directory_path() / "pdfpp_annotation_input.pdf";
    const auto annotationOutput = std::filesystem::temp_directory_path() / "pdfpp_annotation_output.pdf";
    {
        std::ofstream file(annotationInput, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    std::vector<CPPPdf::PdfAnnotation> annotations;
    {
        CPPPdf::PdfAnnotation annotation;
        annotation.pageIndex = 0U;
        annotation.type = CPPPdf::PdfAnnotationType::Underline;
        annotation.rectangle = {10, 10, 80, 25};
        annotation.quadrilaterals = {{10, 10, 80, 25}};
        annotation.color = {0.0, 0.0, 1.0};
        annotation.opacity = 0.8;
        annotation.contents = "Underline test";
        annotation.title = "Pdf++";
        annotations.push_back(annotation);
    }
    {
        CPPPdf::PdfAnnotation annotation;
        annotation.pageIndex = 0U;
        annotation.type = CPPPdf::PdfAnnotationType::TextNote;
        annotation.rectangle = {100, 100, 120, 120};
        annotation.color = {1.0, 1.0, 0.0};
        annotation.opacity = 1.0;
        annotation.contents = "Review this";
        annotation.title = "Pdf++";
        annotation.open = true;
        annotations.push_back(annotation);
    }
    {
        CPPPdf::PdfAnnotation annotation;
        annotation.pageIndex = 0U;
        annotation.type = CPPPdf::PdfAnnotationType::Link;
        annotation.rectangle = {30, 30, 130, 50};
        annotation.color = {0.0, 0.0, 1.0};
        annotation.opacity = 1.0;
        annotation.uri = "https://example.com";
        annotations.push_back(annotation);
    }
    const auto editResult = CPPPdf::PdfAnnotationEditor::AddAnnotations(
        annotationInput, annotationOutput, annotations);
    if (editResult.annotationCount != 3U || editResult.modifiedPageCount != 1U) {
        std::cerr << "Generic annotation editor returned incorrect counts.\n";
        return 14;
    }
    auto annotatedDocument = CPPPdf::PdfDocument::Open(annotationOutput);
    const auto annotatedPage = annotatedDocument.GetObject(annotatedDocument.GetPageReference(0U));
    const auto* annotatedDictionary = annotatedPage.AsDictionary();
    const auto* annotationArray = annotatedDictionary ? annotatedDictionary->GetAsArray(CPPPdf::PdfName("Annots")) : nullptr;
    if (!annotationArray || annotationArray->size() != 3U) {
        std::cerr << "Generic annotations were not persisted through incremental update.\n";
        return 15;
    }
    std::error_code cleanupError;
    std::filesystem::remove(annotationInput, cleanupError);
    std::filesystem::remove(annotationOutput, cleanupError);
    return 0;
}

int TestPageEditor() {
const auto bytes = makeMinimalPdf();
    std::error_code cleanupError;
    const auto pageEditInput = std::filesystem::temp_directory_path() / "pdfpp_page_edit_input.pdf";
    const auto pageEditOutput = std::filesystem::temp_directory_path() / "pdfpp_page_edit_output.pdf";
    {
        std::ofstream file(pageEditInput, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    CPPPdf::PdfPageEdit pageEdit;
    pageEdit.pageIndex = 0U;
    pageEdit.foregroundContent = CPPPdf::PdfContentCommands::StrokeRectangle(
        {20.0, 30.0, 120.0, 80.0}, 2.0, 1.0, 0.0, 0.0);
    pageEdit.backgroundContent = CPPPdf::PdfContentCommands::FillRectangle(
        {5.0, 5.0, 15.0, 15.0}, 0.9, 0.9, 0.9);
    pageEdit.rotation = 90;
    pageEdit.cropBox = CPPPdf::PdfRectangle{10.0, 20.0, 500.0, 700.0};
    const auto pageEditResult = CPPPdf::PdfPageEditor::ApplyEdits(
        pageEditInput, pageEditOutput, {pageEdit});
    if (pageEditResult.modifiedPageCount != 1U ||
        pageEditResult.appendedContentStreamCount != 2U) {
        std::cerr << "Incremental page editor returned incorrect counts.\n";
        return 16;
    }
    auto editedDocument = CPPPdf::PdfDocument::Open(pageEditOutput);
    const auto editedInfo = editedDocument.GetPageInfo(0U);
    if (editedInfo.rotation != 90 ||
        std::abs(editedInfo.cropBox.left - 10.0) > 0.01 ||
        std::abs(editedInfo.cropBox.top - 700.0) > 0.01 ||
        editedDocument.GetPageText(0U).find("Hello") == std::string::npos) {
        std::cerr << "Page geometry/content incremental edit did not persist correctly.\n";
        return 17;
    }
    const auto& editedPageObject = editedDocument.GetObject(editedDocument.GetPageReference(0U));
    const auto* editedPageDictionary = editedPageObject.AsDictionary();
    const auto* editedContents = editedPageDictionary
        ? editedPageDictionary->GetAsArray(CPPPdf::PdfName::Contents)
        : nullptr;
    if (!editedContents || editedContents->size() != 3U) {
        std::cerr << "Background/original/foreground content ordering was not stored as an array.\n";
        return 18;
    }
    std::filesystem::remove(pageEditInput, cleanupError);
    std::filesystem::remove(pageEditOutput, cleanupError);
    return 0;
}

int TestMalformedRejection() {
    constexpr std::string_view invalid = "%PDF-1.7\nnot-a-complete-pdf";
    std::array<std::byte, invalid.size()> invalidBytes{};
    for (std::size_t i = 0; i < invalid.size(); ++i) {
        invalidBytes[i] = static_cast<std::byte>(invalid[i]);
    }

    try {
        (void)CPPPdf::PdfDocument::Open(std::span<const std::byte>(invalidBytes));
        std::cerr << "Expected malformed input to fail.\n";
        return 6;
    } catch (const CPPPdf::PdfException&) {
        return 0;
    }
    return 0;
}

int RunReaderIntegrationTests() {
    CPPPdfTest::TestRunner runner;
    runner.Run("Reader.MinimalPdfParsing", TestMinimalPdfParsing);
    runner.Run("Reader.XrefRecovery", TestXrefRecovery);
    runner.Run("Reader.ObjectCacheAndLimits", TestObjectCacheAndLimits);
    runner.Run("Reader.ObjectStreamCache", TestObjectStreamCache);
    runner.Run("Reader.FontExtraction", TestFontExtraction);
    runner.Run("Reader.FormXObjectExtraction", TestFormXObjectExtraction);
    runner.Run("Reader.InlineImageExtraction", TestInlineImageExtraction);
    runner.Run("Reader.SoftMaskImageExtraction", TestSoftMaskImageExtraction);
    runner.Run("Reader.AnnotationEditor", TestAnnotationEditor);
    runner.Run("Reader.PageEditor", TestPageEditor);
    runner.Run("Reader.MalformedRejection", TestMalformedRejection);
    return runner.PrintSummary("Reader integration");
}
