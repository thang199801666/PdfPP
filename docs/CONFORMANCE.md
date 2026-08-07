# PDF/A and PDF/UA conformance

Pdf++ 1.0.0 provides fail-closed creation profiles and an internal rule-based
preflight validator. The internal validator is intended to catch authoring and
serialization defects early; it is not a replacement for an ISO conformance
validator or an accessibility audit.

## Creation profiles

```cpp
PdfWriter writer;
writer.ConfigureForPdfA(PdfConformanceProfile::PdfA4F, iccBytes,
                        "sRGB IEC61966-2.1");
writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA2,
                         "en-US", "Accessible engineering report");
```

Supported creation identifiers:

- PDF/A-1A and PDF/A-1B
- PDF/A-2A, PDF/A-2B and PDF/A-2U
- PDF/A-3A, PDF/A-3B and PDF/A-3U
- PDF/A-4, PDF/A-4E and PDF/A-4F
- PDF/UA-1 and PDF/UA-2

PDF/UA-1 may be combined with PDF/A-2 or PDF/A-3. PDF/UA-2 may be combined
with PDF/A-4. Incompatible combinations are rejected before serialization.

When conformance enforcement is enabled, serialization rejects known-invalid
states such as encryption in PDF/A, unembedded Base-14 fonts, untagged real
content, missing Figure/Formula alternative text, inaccessible annotations,
invalid associated-file relationships, and incompatible profiles.

`SetConformanceEnforcement(false)` exists only for diagnostics, import repair,
and negative tests. It should not be used for production creation.

## Validation reports

```cpp
const auto report = PdfConformanceValidator::ValidateFile(
    path, PdfConformanceProfile::PdfUA2);

std::cout << report.ToText();
std::ofstream("report.json") << report.ToJson();
```

Each issue includes a stable rule code, severity, optional ISO clause label,
page index, object number, and object path. `PdfValidationOptions` supports
fail-fast operation, an issue limit, and subsystem-level inspection switches.

## External validation gate

Install veraPDF and make the `verapdf` executable available on `PATH`. CMake
then registers external tests for the conformance smoke documents:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build -R 'pdfpp\.(pdfa_ua_conformance|verapdf)' --output-on-failure
```

The same gate can be run explicitly:

```bash
python tools/run_conformance_gate.py \
  --verapdf verapdf \
  --pdfa4f-ua2 build/tests/pdfa4f_ua2_smoke.pdf \
  --pdfa2a-ua1 build/tests/pdfa2a_ua1_smoke.pdf
```

Use a current veraPDF 1.30.x release. For PDF/UA, also perform semantic and
human accessibility checks: reading order, meaningful alternative text,
heading hierarchy, link purpose, table associations, language changes, color
contrast, keyboard operation, and assistive-technology testing cannot be
proven from low-level syntax alone.

## Current trust boundary

Pdf++ checks a substantial set of structural requirements, including XMP
identification, output intents, embedded fonts and Unicode mappings, forbidden
PDF/A actions, associated files, tagged-content coverage, ParentTree/MCID
integrity, PDF 2.0 namespaces, annotation OBJR linkage, table-header
associations, document title/language, and PDF/UA annotation descriptions.

A file must not be advertised as conforming solely because the Pdf++ internal
report passes. Release pipelines should require an external validator report
and retain that report with the produced artifact.

## Semantic structure hardening

The authoring preflight and post-save validator now apply the same semantic
structure model. With PDF/UA enforcement enabled, Pdf++ rejects:

- unresolved or cyclic RoleMap entries;
- invalid BCP 47-style document or structure-element language tags;
- duplicate structure IDs;
- list children outside `L -> LI -> (Lbl, LBody)`;
- rows or cells outside the permitted table hierarchy;
- empty lists, empty tables, or rows without cells;
- Ruby structures without both `RB` and `RT`;
- Warichu structures without both `WT` and `WP`;
- zero RowSpan or ColSpan values; and
- table `Headers` entries that do not reference a `TH` structure ID.

The post-save validator additionally checks parent/child back-links, structure
MCIDs in both directions, ParentTree key ordering, duplicate keys,
`ParentTreeNextKey`, page and annotation key membership, and the PDF 2.0
standard structure namespace used by every PDF/UA-2 structure element.

Form widget checks include inherited field type, field name, alternate name
(`/TU`), a normal appearance (`/AP /N`), annotation visibility, and OBJR
linkage. These checks can be disabled separately with
`PdfValidationOptions::inspectFormAccessibility` when diagnosing legacy files.

## XMP extension schemas

Combined PDF/A and PDF/UA output declares the `pdfuaid` namespace through a
PDF/A extension schema. The declaration includes the `part` property and, for
PDF/UA-2, the `rev` property. Pdf++ also writes `dc:language` and verifies that
it matches catalog `/Lang`.

The PDF/A validator reports `PDFA-XMP-EXT-001` or `PDFA-XMP-EXT-002` when a
PDF/A file uses PDF/UA identification properties without the corresponding
extension-schema declarations.

## Batch 9 hardening

The PDF/A preflight now verifies associated-file integrity instead of only
checking that `/AFRelationship` exists. Writer-created embedded files include
an uncompressed `/Size`, an MD5 `/CheckSum`, and a UTC `/ModDate`. Validation
also checks the EmbeddedFiles name tree, `/AF` association, MIME subtype,
file-spec names, decoded size, and checksum contents.

PDF/UA diagnostics include logical-structure cycle detection, duplicate
structure references, numbered-heading hierarchy warnings, and a reading-order
diagnostic when physical MCID order differs from structure-tree order. The
reading-order diagnostic is a warning because physical content order may
legitimately differ from assistive-technology order; the structure tree remains
the source of logical order.

Reader limits now include `maxDecodedStreamExpansionRatio`. The default is
10,000 decoded bytes per encoded byte for each filter stage; set it to zero to
disable ratio checking while retaining the absolute decoded-size limit.
