#include <CPPPdf/Security/PdfPades.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Writer/PdfIncrementalWriter.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace CPPPdf {
namespace {

std::string readFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open input PDF: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

PdfDss::DssResult PdfDss::AddDocumentSecurityStore(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const DssOptions& options,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    DssResult result{outputPath, options.certificates.size(), options.crls.size(),
                     options.ocspResponses.size()};
    if (options.certificates.empty() && options.crls.empty() && options.ocspResponses.empty()) {
        const std::string source = readFileBytes(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        return result;
    }

    const PdfReference catalogReference = document.GetCatalogReference();
    const PdfObject& catalogObject = document.GetObject(catalogReference);
    const PdfDictionary* catalog = catalogObject.AsDictionary();
    if (!catalog) {
        throw PdfException(PdfErrorCode::MalformedObject, "Catalog is not a dictionary.");
    }

    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    std::uint32_t nextObject = Internal::PdfIncrementalWriter::NextObjectNumber(document);

    PdfDictionary dss;
    dss.Put(PdfName("Type"), PdfObject(PdfName("DSS")));

    // Certificates array of indirect stream references.
    if (!options.certificates.empty()) {
        PdfArray certRefs;
        for (const auto& cert : options.certificates) {
            const PdfReference ref{nextObject++, 0U};
            std::ostringstream body;
            body << "<< /Length " << cert.size() << " >>\nstream\n";
            body.write(reinterpret_cast<const char*>(cert.data()), static_cast<std::streamsize>(cert.size()));
            body << "\nendstream";
            writer.WriteRawObject(ref, body.str());
            certRefs.push_back(PdfObject::IndirectReference(ref.objectNumber, ref.generation));
        }
        dss.Put(PdfName("Certs"), PdfObject(std::move(certRefs)));
    }

    // CRLs array of indirect stream references.
    if (!options.crls.empty()) {
        PdfArray crlRefs;
        for (const auto& crl : options.crls) {
            const PdfReference ref{nextObject++, 0U};
            std::ostringstream body;
            body << "<< /Length " << crl.size() << " >>\nstream\n";
            body.write(reinterpret_cast<const char*>(crl.data()), static_cast<std::streamsize>(crl.size()));
            body << "\nendstream";
            writer.WriteRawObject(ref, body.str());
            crlRefs.push_back(PdfObject::IndirectReference(ref.objectNumber, ref.generation));
        }
        dss.Put(PdfName("CRLs"), PdfObject(std::move(crlRefs)));
    }

    // OCSP responses array.
    if (!options.ocspResponses.empty()) {
        PdfArray ocspRefs;
        for (const auto& ocsp : options.ocspResponses) {
            const PdfReference ref{nextObject++, 0U};
            std::ostringstream body;
            body << "<< /Length " << ocsp.size() << " >>\nstream\n";
            body.write(reinterpret_cast<const char*>(ocsp.data()), static_cast<std::streamsize>(ocsp.size()));
            body << "\nendstream";
            writer.WriteRawObject(ref, body.str());
            ocspRefs.push_back(PdfObject::IndirectReference(ref.objectNumber, ref.generation));
        }
        dss.Put(PdfName("OCSPs"), PdfObject(std::move(ocspRefs)));
    }

    // VRI (validation related info) entry keyed by signature name.
    if (!options.timestamps.empty()) {
        PdfDictionary vri;
        PdfArray tsRefs;
        for (const auto& ts : options.timestamps) {
            const PdfReference ref{nextObject++, 0U};
            std::ostringstream body;
            body << "<< /Length " << ts.size() << " >>\nstream\n";
            body.write(reinterpret_cast<const char*>(ts.data()), static_cast<std::streamsize>(ts.size()));
            body << "\nendstream";
            writer.WriteRawObject(ref, body.str());
            tsRefs.push_back(PdfObject::IndirectReference(ref.objectNumber, ref.generation));
        }
        PdfDictionary vriEntry;
        vriEntry.Put(PdfName("TU"), PdfObject(std::move(tsRefs)));
        vri.Put(PdfName(options.vriSignatureName), PdfObject(std::move(vriEntry)));
        dss.Put(PdfName("VRI"), PdfObject(std::move(vri)));
    }

    PdfDictionary updatedCatalog = *catalog;
    updatedCatalog.Put(PdfName("DSS"), PdfObject(std::move(dss)));
    writer.WriteDictionary(catalogReference, updatedCatalog);
    writer.Finish(nextObject);
    return result;
}

bool PdfDss::HasDocumentSecurityStore(
    const std::filesystem::path& path,
    const PdfReaderOptions& readerOptions) {
    const PdfDocument document = PdfDocument::Open(path, readerOptions);
    const PdfReference catalogReference = document.GetCatalogReference();
    const PdfObject& catalogObject = document.GetObject(catalogReference);
    const PdfDictionary* catalog = catalogObject.AsDictionary();
    return catalog != nullptr && catalog->Contains(PdfName("DSS"));
}

} // namespace CPPPdf
