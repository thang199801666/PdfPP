# Pdf++ gaps compared with MuPDF

This is a capability roadmap, not a claim of binary or performance parity. MuPDF is
a mature multi-format engine; Pdf++ remains a PDF-focused public beta.

## Closed in this milestone

- Standard Security Handler authentication and object-level crypt filters.
- AES-128 (revision 4) creation and reading; RC4-128 (revision 3) compatibility.
- User and owner passwords, PDF permission bits, password change, and password removal.
- Locked documents can be inspected for encryption state before authentication.
- Password-aware incremental page, annotation, and AcroForm updates with permission checks.

## Highest-priority remaining gaps

1. **AES-256 revision 6 and public-key security handlers.** MuPDF exposes AES-256,
   owner/user passwords, decryption, and permission controls. Pdf++ currently stops at
   AES-128/R4.
2. **Complete rendering model.** Soft masks, patterns, ICC/DeviceN color,
   overprint, and optional JPEG/JPX codecs are still incomplete. Vector clipping,
   transparency groups (Form XObject `/Group` and BDC/EMC), blend-mode compositing,
   and embedded CFF/Type 2 glyph rasterization are now implemented in the CPU
   renderer. TrueType and CFF outlines are both rasterized; simple-CFF encoding
   mapping, hinting, and vertical metrics remain.
3. **Writer cleanup and optimization.** The shared incremental writer is now in place;
   object-stream output, garbage collection,
   duplicate-object removal, linearization, sanitization, and unchanged-stream
   passthrough remain missing.
4. **Annotations and redaction.** Pdf++ has core annotations/highlights, but not the
   broad annotation type coverage, appearance editing, and applied redaction workflow
   exposed by MuPDF.
5. **Digital signatures and compliance.** An external-signing foundation now exists
   (`PdfSignatureManager`): it creates /Sig fields, computes `/ByteRange`, exposes the
   exact digest input for an external signer, and writes the produced signature back
   into the placeholder. CMS/PKCS#7 parsing, certificate chains, signing-key
   management, and validation are still external. PDF/A, PDF/X, and PDF/UA
   validation remain unimplemented beyond the initial conformance checks.

## Secondary gaps

- JavaScript actions and richer AcroForm/XFA behavior.
- Multi-format document support such as XPS, EPUB, CBZ, FB2, and images.
- Full shaping for complex scripts and cross-document font/CMap sharing; Pdf++ now has a bounded per-document parsed font/CMap cache.
- More aggressive damaged-file repair and hostile-input corpus coverage.
- Page/object-level parallel rendering with independent resolver contexts.

## Upstream references

- [MuPDF clean/encryption options](https://mupdf.readthedocs.io/en/1.22.0/mutool-clean.html)
- [MuPDF PDF writer option strings](https://mupdf.readthedocs.io/en/1.28.0/reference/common/option-strings.html)
- [MuPDF PDF document password API](https://mupdf.readthedocs.io/en/1.28.0/reference/javascript/types/PDFDocument.html)
- [MuPDF PDF annotation API](https://mupdf.readthedocs.io/en/latest/reference/javascript/types/PDFAnnotation.html)
- [MuPDF multi-format Document API](https://mupdf.readthedocs.io/en/latest/reference/javascript/types/Document.html)
