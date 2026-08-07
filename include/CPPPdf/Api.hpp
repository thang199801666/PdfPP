#pragma once

// Stable public umbrella header. Prefer this header for application code.
#include <CPPPdf/Version.hpp>
#include <CPPPdf/Types.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/Reader.hpp>
#include <CPPPdf/Document.hpp>
#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
#include <CPPPdf/Fonts/PdfFont.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>
#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>
#include <CPPPdf/Fonts/PdfType3Font.hpp>
#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Text.hpp>
#include <CPPPdf/Graphics.hpp>
#include <CPPPdf/Rendering.hpp>
#include <CPPPdf/Writer.hpp>
#include <CPPPdf/Security/PdfSecurity.hpp>
#include <CPPPdf/Security/PdfSignature.hpp>
#include <CPPPdf/Security/PdfPades.hpp>
#include <CPPPdf/Annotations.hpp>
#include <CPPPdf/Forms.hpp>
#include <CPPPdf/Validation/PdfConformance.hpp>
