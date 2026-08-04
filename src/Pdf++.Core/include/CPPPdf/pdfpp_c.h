/* Pdf++ C API.
 *
 * A thin C ABI over the Pdf++ core so the library can be consumed from C,
 * FFI, or scripting runtimes. Handles are opaque pointers; all functions
 * return 0 on success and -1 on error (with an error message buffer).
 */
#ifndef PDFPP_C_H
#define PDFPP_C_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef void* PdfDocumentHandle;
typedef void* PdfBitmapHandle;

/* Opens a PDF file. On success returns a handle and stores the page count. */
PdfDocumentHandle pdfpp_open(const char* path, int* outPageCount, char* errbuf, size_t errbufSize);

/* Returns the page count (0 on error). */
int pdfpp_page_count(PdfDocumentHandle doc, char* errbuf, size_t errbufSize);

/* Extracts the text of a page into a caller-owned buffer. Returns the number
 * of characters written (excluding the null terminator), or -1 on error. */
int pdfpp_page_text(PdfDocumentHandle doc, int pageIndex,
                    char* buffer, size_t bufferSize, char* errbuf, size_t errbufSize);

/* Renders a page to PPM at the given DPI, writing the file. Returns 0 on
 * success. */
int pdfpp_render_ppm(PdfDocumentHandle doc, int pageIndex, double dpi,
                     const char* outputPath, char* errbuf, size_t errbufSize);

/* Returns the library version string (static, never null). */
const char* pdfpp_version(void);

/* Frees a document handle. */
void pdfpp_close(PdfDocumentHandle doc);

#ifdef __cplusplus
}
#endif

#endif /* PDFPP_C_H */
