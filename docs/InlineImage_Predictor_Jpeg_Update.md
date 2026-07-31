# Pdf++ Inline Image, Predictor, and JPEG Update

## Implemented

- Inline image content parsing for `BI ... ID ... EI`.
- `PdfContentEventType::RenderInlineImage` with inline dictionary entries, encoded bytes, and current CTM.
- `PdfDocument::ExtractImages()` now returns inline images when `includeInlineImages` is enabled.
- Inline abbreviations supported for `W`, `H`, `BPC`, `CS`, `F`, `DP`, and `IM`.
- Inline color-space abbreviations supported for `G`, `RGB`, and `CMYK`.
- Flate `/DecodeParms` predictor processing in `PdfFilterPipeline`.
- TIFF predictor 2.
- PNG predictors 10 through 15 and row filters 0 through 4.
- Image XObject `/DecodeParms` dictionary and array mapping to filter chains.
- `PdfImage::FromJpeg()` and `PdfImage::FromJpegFile()`.
- JPEG SOF dimension, precision, and component parsing.
- JPEG pass-through writer output using `/Filter /DCTDecode` without recompression.
- DeviceGray, DeviceRGB, and DeviceCMYK JPEG output.

## Validation

- Inline RGB image extraction and CTM bounding box.
- PNG Sub predictor reconstruction after FlateDecode.
- JPEG writer/readback byte preservation.
- Existing unit, reader integration, and writer smoke tests remain passing.

## Current limitations

- Inline-image `EI` detection is delimiter-based and should later be strengthened with filter-aware length detection.
- JPEG raster decoding is not included; encoded JPEG bytes are returned unchanged.
- JPX, CCITT, and JBIG2 remain pass-through extraction formats.
- Indexed, ICCBased, masks, and soft masks are not yet converted to RGB output.
