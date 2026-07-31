# Pdf++ 0.17.0 — AcroForm merge foundation

- Merges AcroForm `/Fields` from complete source documents.
- Preserves `/DR`, `/DA`, `/Q`, `/NeedAppearances`, and `/SigFlags` from the first form.
- Uses the same object-reference map as page import, so widget `/P`, field `/Kids`, appearances, fonts, and resources are remapped consistently.
- Duplicate top-level field names default to `SourceN.<name>`; callers can keep duplicates or request an error.
- Partial-page imports intentionally skip that source document's AcroForm to avoid orphan widgets and fields.
