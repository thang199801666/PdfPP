# Pdf++ Image Extraction and Writer Update

## Implemented

- Public `PdfImage`, `PdfImageInfo`, `PdfExtractedImage`, and extraction options.
- `PdfDocument::ExtractImages(pageIndex)` for image XObjects actually invoked by page content.
- Recursive image extraction through Form XObjects with form resources, form matrices, recursion limits, and cycle protection.
- Metadata extraction: resource name, object reference, dimensions, bits per component, color space, image mask, filter encoding, and page-space bounding box.
- Decoding for Raw, FlateDecode, ASCIIHexDecode, ASCII85Decode, and RunLengthDecode streams.
- Preservation of encoded bytes for DCT, JPX, CCITT, JBIG2, and unsupported filters.
- Writer-side `PdfImage::FromRgb` and `PdfImage::FromGray`.
- `PdfCanvas::DrawImage` with page resource registration and placement matrix.
- Flate-compressed image XObject output for DeviceRGB and DeviceGray images.
- End-to-end writer/reader test.

## Current limitations

- Inline images (`BI ID EI`) are not yet extracted.
- JPEG/JPX/CCITT/JBIG2 are preserved as encoded bytes but not raster-decoded.
- DecodeParms and image predictors are not yet applied through the typed image path.
- Indexed/ICCBased/Separation/DeviceN color conversion is not yet implemented.
- Soft masks and explicit masks are not yet composited.
- Writer currently accepts raw 8-bit Gray and RGB samples only.

## Next image milestones

1. Inline-image tokenizer and extraction.
2. Typed DecodeParms and PNG/TIFF predictor decoding.
3. JPEG pass-through writer and metadata parser.
4. Image masks and soft masks.
5. Indexed and ICCBased color spaces.
6. PNG/JPEG file loaders.
7. Image deduplication in writer resources.
