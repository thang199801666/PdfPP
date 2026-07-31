# TrueType Font Subsetting — Pdf++ 0.24

Pdf++ now subsets `glyf`-based TrueType fonts during rewrite saves. The subset preserves original glyph IDs so existing Identity-H CID text, `/W`, and ToUnicode mappings remain stable.

## API

```cpp
PdfSaveOptions options;
options.subsetTrueTypeFonts = true; // default
writer.Save("output.pdf", options);
```

Disable subsetting for diagnostics or compatibility:

```cpp
options.subsetTrueTypeFonts = false;
```

`PdfTrueTypeFont::BuildSubset()` is also public for inspection and tooling. It includes glyph 0 and recursively includes composite-glyph dependencies. Unsupported outlines such as CFF/OpenType fall back to the original font bytes.

## Current scope

- TrueType `glyf` + `loca` fonts
- short and long `loca`
- composite glyph dependency closure
- rebuilt SFNT directory/checksums/checkSumAdjustment
- DSIG removal after font modification
- stable original glyph IDs

This is sparse-ID subsetting: unused glyph programs are removed while the original glyph number space is retained. A later compact-remap mode can reduce `loca`/metrics further.
