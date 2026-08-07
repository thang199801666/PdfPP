# PdfPP.ModernWin32 Component Kit

The component kit provides DPI-aware macOS-inspired Win32 controls while preserving normal Win32 keyboard and accessibility behavior where practical.

## Buttons

```cpp
HWND save = PdfPP::ModernWin32::CreateActionButton(
    parent, instance, L"Save", ID_SAVE, x, y, 96, 32, true);

PdfPP::ModernWin32::ApplyMacButtonStyle(
    cancel, PdfPP::ModernWin32::ButtonStyle::Secondary);
```

Button geometry and text use anti-aliased GDI+ rendering. Available styles are `Primary`, `Secondary`, `Toolbar`, and `Ghost`.

## Text boxes

```cpp
HWND edit = PdfPP::ModernWin32::CreateMacTextBox(
    parent, instance, L"", ID_NAME, x, y, 240, 30,
    ES_AUTOHSCROLL, PdfPP::ModernWin32::ControlSize::Regular);
```

You can also restyle an existing native edit:

```cpp
PdfPP::ModernWin32::ApplyMacTextBoxStyle(edit);
```

## Panels

```cpp
HWND panel = PdfPP::ModernWin32::CreateMacPanel(
    parent, instance, ID_PANEL, x, y, width, height, true); // floating/shadow
```

`PanelTemplate` can be customized for fill, surrounding surface, border, shadow, and corner radius.

## Dialogs

```cpp
PdfPP::ModernWin32::ApplyMacDialogStyle(dialogWindow);
```

This applies the shared light visual theme and rounded DWM window corners where supported.

## Combo boxes

```cpp
HWND combo = PdfPP::ModernWin32::CreateMacComboBox(
    parent, instance, ID_MODE, x, y, 180, 30, false);
```

Use `editable = true` for a drop-down edit combo.

## Spinners

```cpp
auto spinner = PdfPP::ModernWin32::CreateMacSpinner(
    parent, instance, ID_PAGE, x, y, 90, 30, 1, 999, 1);

PdfPP::ModernWin32::SetMacSpinnerValue(spinner, 12);
int value = PdfPP::ModernWin32::MacSpinnerValue(spinner);
```

## Sliders

```cpp
HWND slider = PdfPP::ModernWin32::CreateMacSlider(
    parent, instance, ID_ZOOM, x, y, 220, 30, 25, 400, 100);
```

The slider is custom rendered with an anti-aliased rounded track and thumb. It sends `WM_HSCROLL` with `TB_THUMBTRACK` while dragging and `TB_ENDTRACK` when the drag finishes.
