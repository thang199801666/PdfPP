# Pdf++ 0.19.0 — AcroForm appearance generation and flattening

## New APIs

- `PdfAcroForm::GenerateAppearances(...)`
- `PdfAcroForm::FlattenFields(...)`

`GenerateAppearances` creates a Form XObject appearance stream for each selected
text, choice, or button widget, installs it as `/AP /N`, and sets
`/NeedAppearances false`.

`FlattenFields` paints the current field value into the owning page content,
removes the widget reference from the page `/Annots` array, and removes the
selected top-level field from `/AcroForm /Fields`.

Both operations use an incremental update and preserve the original PDF bytes.

## Current limitations

- Appearance text uses Base-14 Helvetica.
- Text layout is single-line and does not yet implement autosizing, multiline,
  rich text, alignment, or comb fields.
- Signature fields are intentionally skipped.
- Flattening nested non-terminal field trees is conservative; terminal fields
  are supported, while complex partially selected parent trees require a later
  field-tree pruning pass.
- Existing appearance characteristics such as `/MK`, `/BS`, and rotation are not
  fully interpreted yet.
