#include <PdfPP/Win32/ReaderState.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>

#include <commdlg.h>
#include <shlobj.h>
#include <commctrl.h>

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace PdfPP::Win32 {
namespace {

constexpr wchar_t kPromptWindowClass[] = L"PdfPP.Reader.InputPrompt";
constexpr wchar_t kReorderWindowClass[] = L"PdfPP.Reader.PageReorder";
constexpr int kPromptEditBase = 5200;
constexpr int kReorderListId = 5300;
constexpr int kReorderResetId = 5301;

struct PromptField final {
    std::wstring label;
    std::wstring value;
    bool password{};
};

struct PromptState final {
    std::vector<PromptField>* fields{};
    std::vector<HWND> edits;
    bool accepted{};
    bool done{};
};

struct ReorderState final {
    std::vector<std::size_t>* order{};
    HWND title{};
    HWND subtitle{};
    HWND list{};
    HFONT titleFont{};
    HFONT subtitleFont{};
    HFONT listFont{};
    std::vector<std::size_t> cutItems;
    std::vector<int> dragSelection;
    POINT dragStart{};
    std::size_t expectedPageCount{};
    int dragCandidateIndex{ -1 };
    int dragHoverIndex{ -1 };
    bool mouseDown{};
    bool dragging{};
    bool rebuildingList{};
    bool collapseSelectionOnClick{};
    bool accepted{};
    bool done{};
};

void applyPromptFont(const HWND control) {
    if (control && regularUiFont) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(regularUiFont), TRUE);
    }
}

HFONT createScaledUiFont(const HWND window, const int pointSize,
                         const int weight = FW_NORMAL) {
    HDC dc = GetDC(window ? window : mainWindow);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(window ? window : mainWindow, dc);
    return CreateFontW(-MulDiv(pointSize, dpi, 72), 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

LRESULT CALLBACK promptWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PromptState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<PromptState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    if (message == WM_CREATE) {
        const int margin = scaleDip(16);
        const int labelHeight = scaleDip(18);
        const int editHeight = scaleDip(25);
        const int rowHeight = scaleDip(54);
        RECT client{};
        GetClientRect(window, &client);
        state->edits.reserve(state->fields->size());
        for (std::size_t index = 0; index < state->fields->size(); ++index) {
            const int top = margin + static_cast<int>(index) * rowHeight;
            const PromptField& field = state->fields->at(index);
            HWND label = CreateWindowExW(0, L"STATIC", field.label.c_str(),
                WS_CHILD | WS_VISIBLE,
                margin, top, std::max(1, static_cast<int>(client.right) - margin * 2), labelHeight,
                window, nullptr, GetModuleHandleW(nullptr), nullptr);
            const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                ES_AUTOHSCROLL | ES_CENTER | (field.password ? ES_PASSWORD : 0U);
            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", field.value.c_str(),
                editStyle,
                margin, top + labelHeight + scaleDip(3),
                std::max(1, static_cast<int>(client.right) - margin * 2), editHeight,
                window, reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(kPromptEditBase + static_cast<int>(index))),
                GetModuleHandleW(nullptr), nullptr);
            applyPromptFont(label);
            PdfPP::ModernWin32::ApplyMacTextBoxStyle(
                edit, PdfPP::ModernWin32::ControlSize::Regular);
            state->edits.push_back(edit);
        }

        const int buttonWidth = scaleDip(84);
        const int buttonHeight = scaleDip(27);
        const int clientRight = static_cast<int>(client.right);
        const int clientBottom = static_cast<int>(client.bottom);
        const int buttonTop = clientBottom - margin - buttonHeight;
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            clientRight - margin - buttonWidth, buttonTop,
            buttonWidth, buttonHeight, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
            GetModuleHandleW(nullptr), nullptr);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            clientRight - margin * 2 - buttonWidth * 2, buttonTop,
            buttonWidth, buttonHeight, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
            GetModuleHandleW(nullptr), nullptr);
        applyPromptFont(cancel);
        applyPromptFont(ok);
        PdfPP::ModernWin32::ApplyMacButtonStyle(
            cancel, PdfPP::ModernWin32::ButtonStyle::Secondary);
        PdfPP::ModernWin32::ApplyMacButtonStyle(
            ok, PdfPP::ModernWin32::ButtonStyle::Primary);
        if (!state->edits.empty()) {
            SetFocus(state->edits.front());
            SendMessageW(state->edits.front(), EM_SETSEL, 0, -1);
        }
        return 0;
    }

    if (message == WM_COMMAND) {
        const int command = LOWORD(wParam);
        if (command == IDOK) {
            for (std::size_t index = 0; index < state->edits.size(); ++index) {
                const int length = GetWindowTextLengthW(state->edits[index]);
                std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
                if (length > 0) {
                    GetWindowTextW(state->edits[index], value.data(), length + 1);
                }
                value.resize(static_cast<std::size_t>(std::max(0, length)));
                state->fields->at(index).value = std::move(value);
            }
            state->accepted = true;
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
        if (command == IDCANCEL) {
            state->accepted = false;
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        state->accepted = false;
        state->done = true;
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        state->done = true;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool ensurePromptWindowClass() {
    static bool registered{};
    if (registered) return true;
    WNDCLASSEXW windowClass{ sizeof(windowClass) };
    windowClass.lpfnWndProc = promptWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kPromptWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    registered = true;
    return true;
}

bool promptValues(const std::wstring& title, std::vector<PromptField>& fields) {
    if (fields.empty() || !ensurePromptWindowClass()) return false;
    PromptState state;
    state.fields = &fields;

    const int clientWidth = scaleDip(480);
    const int clientHeight = scaleDip(32) +
        static_cast<int>(fields.size()) * scaleDip(54) + scaleDip(45);
    RECT frame{ 0, 0, clientWidth, clientHeight };
    AdjustWindowRectEx(&frame, WS_POPUP | WS_CAPTION | WS_SYSMENU,
        FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);
    const int width = static_cast<int>(frame.right - frame.left);
    const int height = static_cast<int>(frame.bottom - frame.top);

    RECT ownerRect{};
    GetWindowRect(mainWindow, &ownerRect);
    const int x = static_cast<int>(ownerRect.left) + std::max(0,
        (static_cast<int>(ownerRect.right - ownerRect.left) - width) / 2);
    const int y = static_cast<int>(ownerRect.top) + std::max(0,
        (static_cast<int>(ownerRect.bottom - ownerRect.top) - height) / 2);
    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kPromptWindowClass, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, mainWindow, nullptr,
        GetModuleHandleW(nullptr), &state);
    if (!window) return false;

    PdfPP::ModernWin32::ApplyMacDialogStyle(window);
    EnableWindow(mainWindow, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    bool repostQuit = false;
    int quitCode = 0;
    while (!state.done) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            repostQuit = result == 0;
            quitCode = static_cast<int>(message.wParam);
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(window)) DestroyWindow(window);
    EnableWindow(mainWindow, TRUE);
    SetActiveWindow(mainWindow);
    SetForegroundWindow(mainWindow);
    if (repostQuit) PostQuitMessage(quitCode);
    return state.accepted;
}


std::vector<int> selectedReorderIndices(const HWND list) {
    std::vector<int> selected;
    if (!list) return selected;
    const int count = static_cast<int>(SendMessageW(list, LB_GETSELCOUNT, 0, 0));
    if (count > 0) {
        selected.resize(static_cast<std::size_t>(count));
        const int copied = static_cast<int>(SendMessageW(list, LB_GETSELITEMS,
            static_cast<WPARAM>(count), reinterpret_cast<LPARAM>(selected.data())));
        if (copied >= 0 && copied < count) {
            selected.resize(static_cast<std::size_t>(copied));
        }
    }
    if (selected.empty()) {
        const int caret = static_cast<int>(SendMessageW(list, LB_GETCARETINDEX, 0, 0));
        const int itemCount = static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0));
        if (caret >= 0 && caret < itemCount) selected.push_back(caret);
    }
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
}

void updateReorderSubtitle(ReorderState& state) {
    if (!state.subtitle) return;
    const bool hasPendingCut = !state.cutItems.empty();
    if (hasPendingCut) {
        const std::wstring message = std::to_wstring(state.cutItems.size()) +
            (state.cutItems.size() == 1U ? L" page cut. " : L" pages cut. ") +
            L"Select a destination row and press Ctrl+V to paste after it.";
        SetWindowTextW(state.subtitle, message.c_str());
    } else {
        SetWindowTextW(state.subtitle,
            L"Use Ctrl/Shift to select multiple rows. Drag the selection, or use Ctrl+X and Ctrl+V.");
    }
    const HWND dialog = GetParent(state.subtitle);
    const HWND save = dialog ? GetDlgItem(dialog, IDOK) : nullptr;
    if (save) {
        const bool complete = state.order &&
            state.order->size() == state.expectedPageCount && !hasPendingCut;
        EnableWindow(save, complete ? TRUE : FALSE);
    }
}

void refreshReorderList(ReorderState& state,
                        const std::vector<int>& selectedIndices = {},
                        const int caretIndex = -1) {
    if (!state.list || !state.order) return;
    const int oldTop = static_cast<int>(SendMessageW(state.list, LB_GETTOPINDEX, 0, 0));
    SendMessageW(state.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(state.list, LB_RESETCONTENT, 0, 0);
    for (std::size_t position = 0; position < state.order->size(); ++position) {
        const std::wstring row = L"Page " + std::to_wstring(state.order->at(position) + 1U);
        SendMessageW(state.list, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(row.c_str()));
    }

    const int itemCount = static_cast<int>(state.order->size());
    for (const int index : selectedIndices) {
        if (index >= 0 && index < itemCount) {
            SendMessageW(state.list, LB_SETSEL, TRUE, index);
        }
    }
    if (itemCount > 0) {
        int caret = caretIndex;
        if (caret < 0 && !selectedIndices.empty()) caret = selectedIndices.front();
        caret = std::clamp(caret, 0, itemCount - 1);
        SendMessageW(state.list, LB_SETCARETINDEX, caret, FALSE);
        SendMessageW(state.list, LB_SETANCHORINDEX, caret, 0);
        SendMessageW(state.list, LB_SETTOPINDEX,
            std::clamp(oldTop, 0, itemCount - 1), 0);
    }
    SendMessageW(state.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state.list, nullptr, TRUE);
    UpdateWindow(state.list);
}

int reorderListIndexFromPoint(const HWND list, const LPARAM lParam,
                              const bool clampOutside) {
    const int count = static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0));
    if (count <= 0) return -1;
    const DWORD hit = static_cast<DWORD>(SendMessageW(list, LB_ITEMFROMPOINT, 0, lParam));
    int index = static_cast<int>(LOWORD(hit));
    const bool outside = HIWORD(hit) != 0;
    if (!outside) return std::clamp(index, 0, count - 1);
    if (!clampOutside) return -1;
    RECT client{};
    GetClientRect(list, &client);
    const int y = GET_Y_LPARAM(lParam);
    if (y < 0) return 0;
    if (y >= static_cast<int>(client.bottom)) return count - 1;
    return std::clamp(index, 0, count - 1);
}

bool moveSelectedPagesToTarget(ReorderState& state,
                               const std::vector<int>& selected,
                               const int targetIndex) {
    if (!state.order || selected.empty() || targetIndex < 0 ||
        targetIndex >= static_cast<int>(state.order->size())) {
        return false;
    }
    if (std::binary_search(selected.begin(), selected.end(), targetIndex)) return false;

    const bool moveAfterTarget = targetIndex > selected.front();
    const std::size_t targetPage = state.order->at(static_cast<std::size_t>(targetIndex));
    std::vector<std::size_t> moved;
    moved.reserve(selected.size());
    std::vector<std::size_t> remaining;
    remaining.reserve(state.order->size() - selected.size());

    std::size_t selectedCursor = 0U;
    for (std::size_t index = 0; index < state.order->size(); ++index) {
        if (selectedCursor < selected.size() &&
            index == static_cast<std::size_t>(selected[selectedCursor])) {
            moved.push_back(state.order->at(index));
            ++selectedCursor;
        } else {
            remaining.push_back(state.order->at(index));
        }
    }
    const auto target = std::find(remaining.begin(), remaining.end(), targetPage);
    if (target == remaining.end()) return false;
    std::size_t insertAt = static_cast<std::size_t>(std::distance(remaining.begin(), target));
    if (moveAfterTarget) ++insertAt;
    insertAt = std::min(insertAt, remaining.size());
    remaining.insert(remaining.begin() + static_cast<std::ptrdiff_t>(insertAt),
        moved.begin(), moved.end());
    *state.order = std::move(remaining);

    std::vector<int> newSelection;
    newSelection.reserve(moved.size());
    for (std::size_t offset = 0; offset < moved.size(); ++offset) {
        newSelection.push_back(static_cast<int>(insertAt + offset));
    }
    const bool continueDragging = state.dragging;
    state.rebuildingList = true;
    if (GetCapture() == state.list) ReleaseCapture();
    refreshReorderList(state, newSelection,
        newSelection.empty() ? -1 : newSelection.front());
    state.rebuildingList = false;
    state.dragSelection = newSelection;
    if (continueDragging) {
        state.mouseDown = true;
        state.dragging = true;
        if (GetCapture() != state.list) SetCapture(state.list);
        SetCursor(handDragCursor());
    }
    return true;
}

void cutSelectedReorderPages(ReorderState& state) {
    if (!state.order || !state.list || state.order->empty()) return;
    const std::vector<int> selected = selectedReorderIndices(state.list);
    if (selected.empty()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    state.cutItems.clear();
    state.cutItems.reserve(selected.size());
    for (const int index : selected) {
        if (index >= 0 && index < static_cast<int>(state.order->size())) {
            state.cutItems.push_back(state.order->at(static_cast<std::size_t>(index)));
        }
    }
    for (auto iterator = selected.rbegin(); iterator != selected.rend(); ++iterator) {
        state.order->erase(state.order->begin() + *iterator);
    }

    const int nextCaret = state.order->empty() ? -1 :
        std::min(selected.front(), static_cast<int>(state.order->size()) - 1);
    const std::vector<int> nextSelection = nextCaret >= 0
        ? std::vector<int>{ nextCaret } : std::vector<int>{};
    refreshReorderList(state, nextSelection, nextCaret);
    updateReorderSubtitle(state);
}

void pasteCutReorderPages(ReorderState& state) {
    if (!state.order || !state.list || state.cutItems.empty()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    int caret = static_cast<int>(SendMessageW(state.list, LB_GETCARETINDEX, 0, 0));
    if (state.order->empty()) caret = -1;
    const std::size_t insertAt = caret < 0 ? 0U :
        std::min(static_cast<std::size_t>(caret + 1), state.order->size());
    const std::size_t pastedCount = state.cutItems.size();
    state.order->insert(state.order->begin() + static_cast<std::ptrdiff_t>(insertAt),
        state.cutItems.begin(), state.cutItems.end());
    state.cutItems.clear();

    std::vector<int> selection;
    selection.reserve(pastedCount);
    for (std::size_t offset = 0; offset < pastedCount; ++offset) {
        selection.push_back(static_cast<int>(insertAt + offset));
    }
    refreshReorderList(state, selection,
        selection.empty() ? -1 : selection.front());
    updateReorderSubtitle(state);
}

LRESULT CALLBACK reorderListSubclassProc(HWND window, UINT message,
                                         WPARAM wParam, LPARAM lParam,
                                         UINT_PTR, DWORD_PTR reference) {
    auto* state = reinterpret_cast<ReorderState*>(reference);
    if (!state || !state->order) return DefSubclassProc(window, message, wParam, lParam);

    const auto resetDragState = [&]() {
        state->mouseDown = false;
        state->dragging = false;
        state->collapseSelectionOnClick = false;
        state->dragCandidateIndex = -1;
        state->dragHoverIndex = -1;
        state->dragSelection.clear();
    };

    switch (message) {
    case WM_LBUTTONDOWN: {
        const int index = reorderListIndexFromPoint(window, lParam, false);
        if (index < 0) {
            resetDragState();
            return 0;
        }

        SetFocus(window);
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool alreadySelected = SendMessageW(window, LB_GETSEL, index, 0) > 0;
        const int selectedCount = static_cast<int>(
            SendMessageW(window, LB_GETSELCOUNT, 0, 0));

        state->collapseSelectionOnClick = false;
        if (shift) {
            int anchor = static_cast<int>(SendMessageW(window, LB_GETANCHORINDEX, 0, 0));
            if (anchor < 0) {
                anchor = static_cast<int>(SendMessageW(window, LB_GETCARETINDEX, 0, 0));
            }
            if (anchor < 0) anchor = index;
            if (!control) SendMessageW(window, LB_SETSEL, FALSE, -1);
            const int first = std::min(anchor, index);
            const int last = std::max(anchor, index);
            for (int item = first; item <= last; ++item) {
                SendMessageW(window, LB_SETSEL, TRUE, item);
            }
            SendMessageW(window, LB_SETCARETINDEX, index, FALSE);
        } else if (control) {
            SendMessageW(window, LB_SETSEL, alreadySelected ? FALSE : TRUE, index);
            SendMessageW(window, LB_SETCARETINDEX, index, FALSE);
            SendMessageW(window, LB_SETANCHORINDEX, index, 0);
        } else if (alreadySelected && selectedCount > 1) {
            // Preserve the group while deciding whether this becomes a drag.
            // A simple click without movement collapses it on mouse-up.
            SendMessageW(window, LB_SETCARETINDEX, index, FALSE);
            state->collapseSelectionOnClick = true;
        } else {
            SendMessageW(window, LB_SETSEL, FALSE, -1);
            SendMessageW(window, LB_SETSEL, TRUE, index);
            SendMessageW(window, LB_SETCARETINDEX, index, FALSE);
            SendMessageW(window, LB_SETANCHORINDEX, index, 0);
        }

        state->mouseDown = true;
        state->dragging = false;
        state->dragCandidateIndex = index;
        state->dragHoverIndex = -1;
        state->dragSelection.clear();
        state->dragStart.x = GET_X_LPARAM(lParam);
        state->dragStart.y = GET_Y_LPARAM(lParam);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (state->mouseDown && (GetKeyState(VK_LBUTTON) & 0x8000) != 0) {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (!state->dragging) {
                const int thresholdX = GetSystemMetrics(SM_CXDRAG);
                const int thresholdY = GetSystemMetrics(SM_CYDRAG);
                if (std::abs(x - state->dragStart.x) >= thresholdX ||
                    std::abs(y - state->dragStart.y) >= thresholdY) {
                    state->dragSelection = selectedReorderIndices(window);
                    if (state->dragSelection.empty() &&
                        state->dragCandidateIndex >= 0) {
                        SendMessageW(window, LB_SETSEL, TRUE,
                            state->dragCandidateIndex);
                        state->dragSelection = { state->dragCandidateIndex };
                    }
                    state->dragging = !state->dragSelection.empty();
                    if (state->dragging) {
                        state->collapseSelectionOnClick = false;
                        if (GetCapture() != window) SetCapture(window);
                        SetCursor(handDragCursor());
                    }
                }
            }
            if (state->dragging) {
                const int target = reorderListIndexFromPoint(window, lParam, true);
                if (target >= 0 && target != state->dragHoverIndex) {
                    if (moveSelectedPagesToTarget(*state, state->dragSelection, target)) {
                        state->dragHoverIndex = target;
                    }
                }
                SetCursor(handDragCursor());
            }
            return 0;
        }
        if (state->mouseDown && (GetKeyState(VK_LBUTTON) & 0x8000) == 0) {
            // Defensive recovery if Windows did not deliver the button-up
            // message (for example after focus/capture transitions).
            if (GetCapture() == window) ReleaseCapture();
            resetDragState();
        }
        break;
    case WM_LBUTTONUP:
        if (state->mouseDown) {
            const bool wasDragging = state->dragging;
            const bool collapseSelection = state->collapseSelectionOnClick;
            const int clickedIndex = state->dragCandidateIndex;
            if (GetCapture() == window) ReleaseCapture();
            resetDragState();
            if (!wasDragging && collapseSelection && clickedIndex >= 0) {
                SendMessageW(window, LB_SETSEL, FALSE, -1);
                SendMessageW(window, LB_SETSEL, TRUE, clickedIndex);
                SendMessageW(window, LB_SETCARETINDEX, clickedIndex, FALSE);
                SendMessageW(window, LB_SETANCHORINDEX, clickedIndex, 0);
                InvalidateRect(window, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (state->rebuildingList) return 0;
        resetDragState();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return 0;
    case WM_CANCELMODE:
    case WM_KILLFOCUS:
        if (GetCapture() == window) ReleaseCapture();
        resetDragState();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        break;
    case WM_KEYDOWN: {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (control && wParam == 'X') {
            cutSelectedReorderPages(*state);
            return 0;
        }
        if (control && wParam == 'V') {
            pasteCutReorderPages(*state);
            return 0;
        }
        if (control && (wParam == VK_UP || wParam == VK_DOWN)) {
            const std::vector<int> selected = selectedReorderIndices(window);
            if (!selected.empty()) {
                const int target = wParam == VK_UP
                    ? selected.front() - 1 : selected.back() + 1;
                if (target >= 0 && target < static_cast<int>(state->order->size())) {
                    moveSelectedPagesToTarget(*state, selected, target);
                }
            }
            return 0;
        }
        break;
    }
    case WM_SETCURSOR:
        if (state->dragging) {
            SetCursor(handDragCursor());
            return TRUE;
        }
        break;
    case WM_NCDESTROY:
        if (GetCapture() == window) ReleaseCapture();
        resetDragState();
        RemoveWindowSubclass(window, reorderListSubclassProc, 1);
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

void layoutReorderWindow(const HWND window, ReorderState& state) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = static_cast<int>(client.right);
    const int height = static_cast<int>(client.bottom);
    const int margin = scaleDip(20);
    const int titleHeight = scaleDip(28);
    const int subtitleHeight = scaleDip(34);
    const int headerGap = scaleDip(8);
    const int buttonHeight = scaleDip(34);
    const int buttonWidth = scaleDip(112);
    const int buttonTop = height - margin - buttonHeight;

    if (state.title) {
        MoveWindow(state.title, margin, margin,
            std::max(1, width - margin * 2), titleHeight, TRUE);
    }
    if (state.subtitle) {
        MoveWindow(state.subtitle, margin, margin + titleHeight + scaleDip(2),
            std::max(1, width - margin * 2), subtitleHeight, TRUE);
    }
    if (state.list) {
        const int listTop = margin + titleHeight + subtitleHeight + headerGap;
        MoveWindow(state.list, margin, listTop,
            std::max(1, width - margin * 2),
            std::max(1, buttonTop - scaleDip(14) - listTop), TRUE);
    }
    const HWND reset = GetDlgItem(window, kReorderResetId);
    const HWND cancel = GetDlgItem(window, IDCANCEL);
    const HWND save = GetDlgItem(window, IDOK);
    if (reset) MoveWindow(reset, margin, buttonTop, buttonWidth, buttonHeight, TRUE);
    if (cancel) MoveWindow(cancel,
        width - margin - buttonWidth, buttonTop, buttonWidth, buttonHeight, TRUE);
    if (save) MoveWindow(save,
        width - margin * 2 - buttonWidth * 2, buttonTop,
        buttonWidth, buttonHeight, TRUE);
}

void drawReorderListItem(const DRAWITEMSTRUCT* draw, ReorderState& state) {
    if (!draw || !state.order || draw->itemID == static_cast<UINT>(-1)) return;
    HDC dc = draw->hDC;
    RECT rc = draw->rcItem;
    const bool selected = (draw->itemState & ODS_SELECTED) != 0;

    const COLORREF windowBack = RGB(245, 246, 248);
    const COLORREF itemBack = selected ? RGB(226, 235, 252) : RGB(255, 255, 255);
    const COLORREF border = selected ? RGB(90, 126, 213) : RGB(220, 224, 230);
    const COLORREF textColor = RGB(35, 37, 41);
    const COLORREF mutedColor = RGB(100, 106, 114);

    HBRUSH backBrush = CreateSolidBrush(windowBack);
    FillRect(dc, &draw->rcItem, backBrush);
    DeleteObject(backBrush);

    RECT card = rc;
    InflateRect(&card, -scaleDip(4), -scaleDip(3));
    HBRUSH itemBrush = CreateSolidBrush(itemBack);
    FillRect(dc, &card, itemBrush);
    DeleteObject(itemBrush);
    HBRUSH borderBrush = CreateSolidBrush(border);
    FrameRect(dc, &card, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(dc, TRANSPARENT);
    const std::size_t orderIndex = static_cast<std::size_t>(draw->itemID);
    if (orderIndex >= state.order->size()) return;
    const std::size_t originalPage = state.order->at(orderIndex);

    HFONT oldFont = nullptr;
    if (state.listFont) {
        oldFont = static_cast<HFONT>(SelectObject(dc, state.listFont));
    }

    RECT handleRect = card;
    handleRect.left += scaleDip(10);
    handleRect.right = handleRect.left + scaleDip(26);
    SetTextColor(dc, mutedColor);
    DrawTextW(dc, L"☰", -1, &handleRect,
        DT_VCENTER | DT_SINGLELINE | DT_CENTER);

    RECT leftRect = card;
    leftRect.left = handleRect.right + scaleDip(8);
    leftRect.right = card.left + (card.right - card.left) / 2;
    SetTextColor(dc, textColor);
    const std::wstring leftText = L"Position " + std::to_wstring(orderIndex + 1U);
    DrawTextW(dc, leftText.c_str(), -1, &leftRect,
        DT_VCENTER | DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);

    RECT rightRect = card;
    rightRect.left = leftRect.right + scaleDip(10);
    rightRect.right -= scaleDip(14);
    SetTextColor(dc, mutedColor);
    const std::wstring rightText = L"Original page " + std::to_wstring(originalPage + 1U);
    DrawTextW(dc, rightText.c_str(), -1, &rightRect,
        DT_VCENTER | DT_SINGLELINE | DT_RIGHT | DT_END_ELLIPSIS);

    if ((draw->itemState & ODS_FOCUS) != 0) {
        RECT focus = card;
        InflateRect(&focus, -2, -2);
        DrawFocusRect(dc, &focus);
    }

    if (oldFont) SelectObject(dc, oldFont);
}

LRESULT CALLBACK reorderWindowProc(HWND window, UINT message,
                                   WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ReorderState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<ReorderState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        state->titleFont = createScaledUiFont(window, 14, FW_SEMIBOLD);
        state->subtitleFont = createScaledUiFont(window, 10, FW_NORMAL);
        state->listFont = createScaledUiFont(window, 11, FW_NORMAL);
        state->title = CreateWindowExW(0, L"STATIC", L"Reorder pages",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        state->subtitle = CreateWindowExW(0, L"STATIC",
            L"Drag a row to change the order. Left is the new position; right is the original page.",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        state->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED |
            LBS_HASSTRINGS | LBS_EXTENDEDSEL,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReorderListId)),
            GetModuleHandleW(nullptr), nullptr);
        HWND reset = CreateWindowExW(0, L"BUTTON", L"Reset",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReorderResetId)),
            GetModuleHandleW(nullptr), nullptr);
        HWND save = CreateWindowExW(0, L"BUTTON", L"Save As...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
            GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
            GetModuleHandleW(nullptr), nullptr);
        for (const HWND control : { state->title, state->subtitle, state->list, reset, save, cancel }) {
            applyPromptFont(control);
            PdfPP::ModernWin32::ApplyDarkMode(control);
        }
        PdfPP::ModernWin32::ApplyMacButtonStyle(
            reset, PdfPP::ModernWin32::ButtonStyle::Secondary);
        PdfPP::ModernWin32::ApplyMacButtonStyle(
            save, PdfPP::ModernWin32::ButtonStyle::Primary);
        PdfPP::ModernWin32::ApplyMacButtonStyle(
            cancel, PdfPP::ModernWin32::ButtonStyle::Secondary);
        if (state->titleFont) {
            SendMessageW(state->title, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
        }
        if (state->subtitleFont) {
            SendMessageW(state->subtitle, WM_SETFONT, reinterpret_cast<WPARAM>(state->subtitleFont), TRUE);
        }
        if (state->listFont) {
            SendMessageW(state->list, WM_SETFONT, reinterpret_cast<WPARAM>(state->listFont), TRUE);
        }
        SetWindowSubclass(state->list, reorderListSubclassProc, 1,
            reinterpret_cast<DWORD_PTR>(state));
        refreshReorderList(*state, std::vector<int>{ 0 }, 0);
        updateReorderSubtitle(*state);
        layoutReorderWindow(window, *state);
        SetFocus(state->list);
        return 0;
    }
    case WM_SIZE:
        layoutReorderWindow(window, *state);
        return 0;
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure && measure->CtlID == kReorderListId) {
            measure->itemHeight = static_cast<UINT>(scaleDip(36));
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw && draw->CtlID == kReorderListId) {
            drawReorderListItem(draw, *state);
            return TRUE;
        }
        break;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = scaleDip(560);
        info->ptMinTrackSize.y = scaleDip(500);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            if (!state->cutItems.empty() || !state->order ||
                state->order->size() != state->expectedPageCount) {
                MessageBoxW(window,
                    L"Some pages are still in the cut buffer. Select a destination and press Ctrl+V before saving.",
                    L"Reorder Pages", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (state->list && GetCapture() == state->list) ReleaseCapture();
            state->accepted = true;
            state->done = true;
            DestroyWindow(window);
            return 0;
        case IDCANCEL:
            if (state->list && GetCapture() == state->list) ReleaseCapture();
            state->accepted = false;
            state->done = true;
            DestroyWindow(window);
            return 0;
        case kReorderResetId:
            state->order->resize(state->expectedPageCount);
            std::iota(state->order->begin(), state->order->end(), std::size_t{ 0 });
            state->cutItems.clear();
            refreshReorderList(*state, std::vector<int>{ 0 }, 0);
            updateReorderSubtitle(*state);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        if (state->list && GetCapture() == state->list) ReleaseCapture();
        state->accepted = false;
        state->done = true;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (state->list && GetCapture() == state->list) ReleaseCapture();
        if (state->titleFont) DeleteObject(state->titleFont);
        if (state->subtitleFont) DeleteObject(state->subtitleFont);
        if (state->listFont) DeleteObject(state->listFont);
        state->titleFont = nullptr;
        state->subtitleFont = nullptr;
        state->listFont = nullptr;
        state->done = true;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool ensureReorderWindowClass() {
    static bool registered{};
    if (registered) return true;
    WNDCLASSEXW windowClass{ sizeof(windowClass) };
    windowClass.lpfnWndProc = reorderWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kReorderWindowClass;
    if (!RegisterClassExW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    registered = true;
    return true;
}

bool showReorderPagesDialog(std::vector<std::size_t>& order) {
    if (order.size() < 2U || !ensureReorderWindowClass()) return false;
    ReorderState state;
    state.order = &order;
    state.expectedPageCount = order.size();

    const int clientWidth = scaleDip(620);
    const int clientHeight = scaleDip(700);
    RECT frame{ 0, 0, clientWidth, clientHeight };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU |
        WS_THICKFRAME | WS_MAXIMIZEBOX;
    const DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    AdjustWindowRectEx(&frame, style, FALSE, exStyle);
    const int width = static_cast<int>(frame.right - frame.left);
    const int height = static_cast<int>(frame.bottom - frame.top);
    RECT owner{};
    GetWindowRect(mainWindow, &owner);
    const int x = static_cast<int>(owner.left) + std::max(0,
        (static_cast<int>(owner.right - owner.left) - width) / 2);
    const int y = static_cast<int>(owner.top) + std::max(0,
        (static_cast<int>(owner.bottom - owner.top) - height) / 2);
    HWND window = CreateWindowExW(exStyle, kReorderWindowClass,
        L"Reorder Pages", style, x, y, width, height,
        mainWindow, nullptr, GetModuleHandleW(nullptr), &state);
    if (!window) return false;

    PdfPP::ModernWin32::ApplyMacDialogStyle(window);
    EnableWindow(mainWindow, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    bool repostQuit = false;
    int quitCode = 0;
    while (!state.done) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            repostQuit = result == 0;
            quitCode = static_cast<int>(message.wParam);
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(window)) DestroyWindow(window);
    EnableWindow(mainWindow, TRUE);
    SetActiveWindow(mainWindow);
    SetForegroundWindow(mainWindow);
    if (repostQuit) PostQuitMessage(quitCode);
    return state.accepted;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), result.data(), length,
        nullptr, nullptr);
    return result;
}

bool selectPdfFile(std::wstring& path, const wchar_t* title) {
    wchar_t buffer[32768]{};
    OPENFILENAMEW dialog{ sizeof(dialog) };
    dialog.hwndOwner = mainWindow;
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return false;
    path = buffer;
    return true;
}

bool selectMultiplePdfFiles(std::vector<std::wstring>& paths) {
    std::vector<wchar_t> buffer(65536U, L'\0');
    OPENFILENAMEW dialog{ sizeof(dialog) };
    dialog.hwndOwner = mainWindow;
    dialog.lpstrTitle = L"Select PDF files to merge";
    dialog.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return false;

    const std::wstring first(buffer.data());
    const wchar_t* next = buffer.data() + first.size() + 1U;
    if (!*next) {
        paths = { first };
        return true;
    }
    const std::filesystem::path directory(first);
    paths.clear();
    while (*next) {
        const std::wstring file(next);
        paths.push_back((directory / file).wstring());
        next += file.size() + 1U;
    }
    return true;
}

std::wstring suggestedOutput(const std::wstring& source, const wchar_t* suffix) {
    const std::filesystem::path input(source);
    const std::wstring stem = input.stem().wstring();
    return (input.parent_path() / (stem + suffix + L".pdf")).wstring();
}

bool selectPdfOutput(std::wstring& path, const std::wstring& suggested,
                     const wchar_t* title) {
    std::vector<wchar_t> buffer(32768U, L'\0');
    const std::size_t copyLength = std::min(suggested.size(), buffer.size() - 1U);
    std::copy_n(suggested.data(), copyLength, buffer.data());
    OPENFILENAMEW dialog{ sizeof(dialog) };
    dialog.hwndOwner = mainWindow;
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0";
    dialog.lpstrDefExt = L"pdf";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return false;
    path = buffer.data();
    return true;
}

bool selectOutputDirectory(std::wstring& path) {
    BROWSEINFOW browse{};
    browse.hwndOwner = mainWindow;
    browse.lpszTitle = L"Select output folder";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
    if (!item) return false;
    wchar_t buffer[MAX_PATH]{};
    const BOOL succeeded = SHGetPathFromIDListW(item, buffer);
    CoTaskMemFree(item);
    if (!succeeded || !buffer[0]) return false;
    path = buffer;
    return true;
}

bool samePath(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty()) return false;
    std::error_code error;
    const auto normalizedLeft = std::filesystem::absolute(left, error).lexically_normal().wstring();
    error.clear();
    const auto normalizedRight = std::filesystem::absolute(right, error).lexically_normal().wstring();
    return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
}

bool validateDistinctOutput(const std::vector<std::wstring>& inputs,
                            const std::wstring& output) {
    for (const auto& input : inputs) {
        if (samePath(input, output)) {
            MessageBoxW(mainWindow,
                L"The output file must be different from every input file.",
                L"Pdf++", MB_OK | MB_ICONWARNING);
            return false;
        }
    }
    return true;
}

bool parsePositiveNumber(const std::wstring& text, std::size_t& value) {
    try {
        std::size_t position = 0U;
        const unsigned long long parsed = std::stoull(text, &position, 10);
        while (position < text.size() && std::iswspace(text[position])) ++position;
        if (position != text.size() || parsed == 0U) return false;
        value = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parsePageSelection(const std::wstring& text, const std::size_t maximum,
                        std::vector<std::size_t>& pages, std::wstring& error,
                        const bool preserveDuplicates = false) {
    pages.clear();
    std::set<std::size_t> seen;
    std::size_t offset = 0U;
    while (offset < text.size()) {
        while (offset < text.size() &&
            (std::iswspace(text[offset]) || text[offset] == L',' || text[offset] == L';')) {
            ++offset;
        }
        if (offset >= text.size()) break;
        std::size_t end = offset;
        while (end < text.size() && text[end] != L',' && text[end] != L';') ++end;
        std::wstring token = text.substr(offset, end - offset);
        token.erase(std::remove_if(token.begin(), token.end(),
            [](const wchar_t character) { return std::iswspace(character) != 0; }), token.end());
        const std::size_t dash = token.find(L'-');
        std::size_t first = 0U;
        std::size_t last = 0U;
        if (dash == std::wstring::npos) {
            if (!parsePositiveNumber(token, first)) {
                error = L"Invalid page number: " + token;
                return false;
            }
            last = first;
        } else {
            if (token.find(L'-', dash + 1U) != std::wstring::npos ||
                !parsePositiveNumber(token.substr(0, dash), first) ||
                !parsePositiveNumber(token.substr(dash + 1U), last) || first > last) {
                error = L"Invalid page range: " + token;
                return false;
            }
        }
        if (last > maximum) {
            error = L"Page " + std::to_wstring(last) + L" is outside this document.";
            return false;
        }
        for (std::size_t page = first; page <= last; ++page) {
            const std::size_t zeroBased = page - 1U;
            if (preserveDuplicates || seen.insert(zeroBased).second) pages.push_back(zeroBased);
            if (page == last) break;
        }
        offset = end + 1U;
    }
    if (pages.empty()) {
        error = L"Enter at least one page number.";
        return false;
    }
    return true;
}

bool isValidFilePrefix(const std::wstring& prefix) {
    if (prefix.empty() || prefix == L"." || prefix == L"..") return false;
    constexpr wchar_t invalidCharacters[] = L"<>:\"/\\|?*";
    return prefix.find_first_of(invalidCharacters) == std::wstring::npos &&
        prefix.back() != L'.' && prefix.back() != L' ';
}

bool chooseInputPdf(std::wstring& input, const wchar_t* title) {
    if (document && !currentFilePath.empty() && std::filesystem::exists(currentFilePath)) {
        input = currentFilePath;
        return true;
    }
    return selectPdfFile(input, title);
}

bool runOperation(const std::wstring& progressText,
                  const std::wstring& outputPath,
                  const bool offerOpen,
                  const std::function<bool(std::string&)>& operation) {
    setStatus(progressText);
    RedrawWindow(statusLabel, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    const HCURSOR previousCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    EnableWindow(mainWindow, FALSE);
    std::string error;
    const bool succeeded = operation(error);
    EnableWindow(mainWindow, TRUE);
    SetActiveWindow(mainWindow);
    SetCursor(previousCursor ? previousCursor : LoadCursorW(nullptr, IDC_ARROW));

    if (!succeeded) {
        const std::wstring message = utf8ToWide(error.c_str());
        setStatus(L"Operation failed");
        MessageBoxW(mainWindow,
            (message.empty() ? L"The PDF operation failed." : message.c_str()),
            L"Pdf++", MB_OK | MB_ICONERROR);
        return false;
    }

    setStatus(L"Created " + std::filesystem::path(outputPath).filename().wstring());
    std::wstring message = L"The PDF was created successfully:\n\n" + outputPath;
    if (offerOpen) message += L"\n\nOpen the result now?";
    const int answer = MessageBoxW(mainWindow, message.c_str(), L"Pdf++",
        offerOpen ? MB_YESNO | MB_ICONINFORMATION : MB_OK | MB_ICONINFORMATION);
    if (offerOpen && answer == IDYES) openPath(outputPath);
    return true;
}

std::wstring unlockedOutputPath(const std::wstring& inputPath) {
    const std::filesystem::path input(inputPath);
    const std::filesystem::path directory = input.parent_path();
    const std::wstring extension = input.extension().empty()
        ? std::wstring(L".pdf") : input.extension().wstring();
    const std::wstring stem = input.stem().wstring();

    std::filesystem::path candidate = directory / (stem + L"_unlocked" + extension);
    for (std::size_t suffix = 2U; std::filesystem::exists(candidate); ++suffix) {
        candidate = directory /
            (stem + L"_unlocked_" + std::to_wstring(suffix) + extension);
    }
    return candidate.wstring();
}

bool inspectPdfEncryption(const std::wstring& inputPath,
                          bool& encrypted,
                          bool& passwordRequired,
                          std::string& error) {
    encrypted = false;
    passwordRequired = false;
    error.clear();
    try {
        const std::filesystem::path path(inputPath);
        CPPPdf::PdfDocument probe;
        try {
            probe = CPPPdf::PdfDocument::OpenMapped(path);
        } catch (const std::exception&) {
            probe = CPPPdf::PdfDocument::Open(path);
        }
        encrypted = probe.IsEncrypted();
        passwordRequired = probe.IsPasswordRequired();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "Unable to inspect PDF security.";
    }
    return false;
}

bool runUnlockOperation(const std::wstring& inputPath,
                        const std::wstring& outputPath,
                        const std::string& password) {
    setStatus(L"Removing PDF password...");
    RedrawWindow(statusLabel, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    const HCURSOR previousCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    EnableWindow(mainWindow, FALSE);
    std::string error;
    const bool succeeded = ReaderPdfDocument::RemovePassword(
        inputPath, outputPath, password, error);
    EnableWindow(mainWindow, TRUE);
    SetActiveWindow(mainWindow);
    SetCursor(previousCursor ? previousCursor : LoadCursorW(nullptr, IDC_ARROW));

    if (!succeeded) {
        const std::wstring message = utf8ToWide(error.c_str());
        setStatus(L"Unable to unlock PDF");
        MessageBoxW(mainWindow,
            (message.empty()
                ? L"The password could not be removed. Check the current password."
                : message.c_str()),
            L"Crack Password", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

} // namespace

void mergePdfDocuments() {
    std::vector<std::wstring> inputs;
    if (!selectMultiplePdfFiles(inputs)) return;
    if (inputs.size() < 2U) {
        MessageBoxW(mainWindow, L"Select at least two PDF files.",
            L"Merge PDFs", MB_OK | MB_ICONWARNING);
        return;
    }
    const std::filesystem::path first(inputs.front());
    const std::wstring suggested = (first.parent_path() / L"merged.pdf").wstring();
    std::wstring output;
    if (!selectPdfOutput(output, suggested, L"Save merged PDF")) return;
    if (!validateDistinctOutput(inputs, output)) return;
    runOperation(L"Merging PDF files...", output, true,
        [&](std::string& error) {
            return ReaderPdfDocument::MergeDocuments(inputs, output, error);
        });
}

void extractPdfPages() {
    if (!document || currentFilePath.empty()) return;
    std::vector<PromptField> fields{
        { L"Pages to extract (example: 1-3,5,8)",
          std::to_wstring(pageIndex + 1), false }
    };
    if (!promptValues(L"Extract Pages", fields)) return;
    std::vector<std::size_t> pages;
    std::wstring parseError;
    if (!parsePageSelection(fields[0].value, static_cast<std::size_t>(pageCount),
                            pages, parseError)) {
        MessageBoxW(mainWindow, parseError.c_str(), L"Extract Pages",
            MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring output;
    if (!selectPdfOutput(output, suggestedOutput(currentFilePath, L"_pages"),
                         L"Save extracted pages")) return;
    if (!validateDistinctOutput({ currentFilePath }, output)) return;
    runOperation(L"Extracting pages...", output, true,
        [&](std::string& error) {
            return ReaderPdfDocument::ExtractPages(
                currentFilePath, output, pages, error);
        });
}

void splitPdfDocument() {
    if (!document || currentFilePath.empty()) return;
    std::vector<PromptField> fields{
        { L"Pages per output file", L"1", false },
        { L"Output file prefix", std::filesystem::path(currentFilePath).stem().wstring() + L"_part", false }
    };
    if (!promptValues(L"Split PDF", fields)) return;
    std::size_t pagesPerFile = 0U;
    if (!parsePositiveNumber(fields[0].value, pagesPerFile) ||
        pagesPerFile > static_cast<std::size_t>(pageCount)) {
        MessageBoxW(mainWindow, L"Pages per file must be between 1 and the document page count.",
            L"Split PDF", MB_OK | MB_ICONWARNING);
        return;
    }
    if (fields[1].value.empty()) fields[1].value = L"part";
    if (!isValidFilePrefix(fields[1].value)) {
        MessageBoxW(mainWindow,
            L"The output prefix contains characters that are not valid in a file name.",
            L"Split PDF", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring directory;
    if (!selectOutputDirectory(directory)) return;
    runOperation(L"Splitting PDF...", directory, false,
        [&](std::string& error) {
            return ReaderPdfDocument::SplitEvery(
                currentFilePath, directory, pagesPerFile, fields[1].value, error);
        });
}

void deletePdfPages() {
    if (!document || currentFilePath.empty() || pageCount <= 1) return;
    std::vector<PromptField> fields{
        { L"Pages to delete (example: 2,4-6)",
          std::to_wstring(pageIndex + 1), false }
    };
    if (!promptValues(L"Delete Pages", fields)) return;
    std::vector<std::size_t> pages;
    std::wstring parseError;
    if (!parsePageSelection(fields[0].value, static_cast<std::size_t>(pageCount),
                            pages, parseError)) {
        MessageBoxW(mainWindow, parseError.c_str(), L"Delete Pages",
            MB_OK | MB_ICONWARNING);
        return;
    }
    if (pages.size() >= static_cast<std::size_t>(pageCount)) {
        MessageBoxW(mainWindow, L"At least one page must remain in the output PDF.",
            L"Delete Pages", MB_OK | MB_ICONWARNING);
        return;
    }
    if (MessageBoxW(mainWindow, L"Create a new PDF without the selected pages?",
        L"Delete Pages", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::wstring output;
    if (!selectPdfOutput(output, suggestedOutput(currentFilePath, L"_deleted"),
                         L"Save PDF after deleting pages")) return;
    if (!validateDistinctOutput({ currentFilePath }, output)) return;
    runOperation(L"Deleting pages...", output, true,
        [&](std::string& error) {
            return ReaderPdfDocument::RemovePages(
                currentFilePath, output, pages, error);
        });
}

void duplicatePdfPages() {
    if (!document || currentFilePath.empty()) return;
    std::vector<PromptField> fields{
        { L"Pages to duplicate and append (example: 1,3-4)",
          std::to_wstring(pageIndex + 1), false }
    };
    if (!promptValues(L"Duplicate Pages", fields)) return;
    std::vector<std::size_t> pages;
    std::wstring parseError;
    if (!parsePageSelection(fields[0].value, static_cast<std::size_t>(pageCount),
                            pages, parseError, true)) {
        MessageBoxW(mainWindow, parseError.c_str(), L"Duplicate Pages",
            MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring output;
    if (!selectPdfOutput(output, suggestedOutput(currentFilePath, L"_duplicated"),
                         L"Save PDF with duplicated pages")) return;
    if (!validateDistinctOutput({ currentFilePath }, output)) return;
    runOperation(L"Duplicating pages...", output, true,
        [&](std::string& error) {
            return ReaderPdfDocument::DuplicatePages(
                currentFilePath, output, pages, error);
        });
}

void moveCurrentPdfPage() {
    if (!document || currentFilePath.empty() || pageCount <= 1) return;
    const int suggestedTarget = std::clamp(pageIndex + 2, 1, pageCount);
    std::vector<PromptField> fields{
        { L"Move current page " + std::to_wstring(pageIndex + 1) + L" to position",
          std::to_wstring(suggestedTarget), false }
    };
    if (!promptValues(L"Move Page", fields)) return;
    std::size_t target = 0U;
    if (!parsePositiveNumber(fields[0].value, target) ||
        target > static_cast<std::size_t>(pageCount)) {
        MessageBoxW(mainWindow, L"Enter a valid destination page position.",
            L"Move Page", MB_OK | MB_ICONWARNING);
        return;
    }
    if (target - 1U == static_cast<std::size_t>(pageIndex)) {
        setStatus(L"Page is already at that position");
        return;
    }
    std::wstring output;
    if (!selectPdfOutput(output, suggestedOutput(currentFilePath, L"_moved"),
                         L"Save reordered PDF")) return;
    if (!validateDistinctOutput({ currentFilePath }, output)) return;
    runOperation(L"Moving page...", output, true,
        [&](std::string& error) {
            return ReaderPdfDocument::MovePage(currentFilePath, output,
                static_cast<std::size_t>(pageIndex), target - 1U, error);
        });
}

void reorderPdfPages() {
    if (!document || currentFilePath.empty() || pageCount <= 1) return;
    std::vector<std::size_t> order(static_cast<std::size_t>(pageCount));
    std::iota(order.begin(), order.end(), std::size_t{ 0 });
    if (!showReorderPagesDialog(order)) return;

    bool changed = false;
    for (std::size_t index = 0; index < order.size(); ++index) {
        if (order[index] != index) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        setStatus(L"Page order was not changed");
        return;
    }

    std::wstring output;
    if (!selectPdfOutput(output, suggestedOutput(currentFilePath, L"_reordered"),
                         L"Save reordered PDF")) return;
    if (!validateDistinctOutput({ currentFilePath }, output)) return;
    const bool succeeded = runOperation(L"Reordering pages...", output, false,
        [&](std::string& error) {
            return ReaderPdfDocument::ReorderPages(
                currentFilePath, output, order, error);
        });
    if (succeeded) {
        // Save As creates a new document. Open it immediately so the viewer,
        // page list and subsequent reorder operations reflect the saved order
        // without requiring the user to close and reopen the file manually.
        openPath(output);
    }
}

void reversePdfPages() {
    if (!document || currentFilePath.empty() || pageCount <= 1) return;
    if (MessageBoxW(mainWindow,
        L"Create a new PDF with the complete page order reversed?",
        L"Reverse Page Order", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::vector<std::size_t> order(static_cast<std::size_t>(pageCount));
    std::iota(order.rbegin(), order.rend(), std::size_t{0});
    std::wstring output;
    if (!selectPdfOutput(output, suggestedOutput(currentFilePath, L"_reversed"),
                         L"Save reversed PDF")) return;
    if (!validateDistinctOutput({ currentFilePath }, output)) return;
    runOperation(L"Reversing page order...", output, true,
        [&](std::string& error) {
            return ReaderPdfDocument::ReorderPages(
                currentFilePath, output, order, error);
        });
}

void crackPasswordAndOpenPdf() {
    std::wstring input;
    if (!selectPdfFile(input, L"Select PDF to unlock")) return;

    bool encrypted = false;
    bool passwordRequired = false;
    std::string inspectionError;
    setStatus(L"Checking PDF security...");
    if (!inspectPdfEncryption(input, encrypted, passwordRequired, inspectionError)) {
        const std::wstring message = utf8ToWide(inspectionError.c_str());
        setStatus(L"Unable to inspect PDF");
        MessageBoxW(mainWindow,
            (message.empty() ? L"Unable to inspect the selected PDF." : message.c_str()),
            L"Crack Password", MB_OK | MB_ICONERROR);
        return;
    }

    if (!encrypted) {
        setStatus(L"PDF is not password protected; opening original file");
        openPath(input);
        return;
    }

    std::wstring passwordText;
    if (passwordRequired) {
        std::vector<PromptField> fields{
            { L"Current user or owner password", L"", true }
        };
        if (!promptValues(L"Unlock PDF", fields)) return;
        passwordText = fields.front().value;
        if (passwordText.empty()) {
            MessageBoxW(mainWindow,
                L"This PDF requires its current password. Password cracking or brute-force recovery is not performed.",
                L"Crack Password", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    const std::wstring output = unlockedOutputPath(input);
    if (!runUnlockOperation(input, output, wideToUtf8(passwordText))) return;

    setStatus(L"Password removed; opening " +
        std::filesystem::path(output).filename().wstring());
    openPath(output);
}

void addPdfPassword() {
    std::wstring input;
    if (!chooseInputPdf(input, L"Select PDF to protect")) return;
    std::vector<PromptField> fields{
        { L"Current password (leave blank if the PDF is not protected)", L"", true },
        { L"New user/open password", L"", true },
        { L"New owner password (optional; defaults to user password)", L"", true }
    };
    if (!promptValues(L"Add PDF Password", fields)) return;
    if (fields[1].value.empty()) {
        MessageBoxW(mainWindow, L"Enter a non-empty user/open password.",
            L"Add PDF Password", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring output;
    if (!selectPdfOutput(output, suggestedOutput(input, L"_protected"),
                         L"Save password-protected PDF")) return;
    if (!validateDistinctOutput({ input }, output)) return;
    runOperation(L"Adding AES-256 password protection...", output, false,
        [&](std::string& error) {
            return ReaderPdfDocument::AddPassword(input, output,
                wideToUtf8(fields[0].value), wideToUtf8(fields[1].value),
                wideToUtf8(fields[2].value), error);
        });
}

void removePdfPassword() {
    std::wstring input;
    if (!chooseInputPdf(input, L"Select password-protected PDF")) return;
    std::vector<PromptField> fields{
        { L"Current user or owner password", L"", true }
    };
    if (!promptValues(L"Remove PDF Password", fields)) return;
    std::wstring output;
    if (!selectPdfOutput(output, suggestedOutput(input, L"_unlocked"),
                         L"Save PDF without password")) return;
    if (!validateDistinctOutput({ input }, output)) return;
    runOperation(L"Removing PDF password...", output, false,
        [&](std::string& error) {
            return ReaderPdfDocument::RemovePassword(
                input, output, wideToUtf8(fields[0].value), error);
        });
}

void changePdfPassword() {
    std::wstring input;
    if (!chooseInputPdf(input, L"Select PDF whose password will change")) return;
    std::vector<PromptField> fields{
        { L"Current user or owner password", L"", true },
        { L"New user/open password", L"", true },
        { L"New owner password (optional; defaults to user password)", L"", true }
    };
    if (!promptValues(L"Change PDF Password", fields)) return;
    if (fields[1].value.empty()) {
        MessageBoxW(mainWindow, L"Enter a non-empty new user/open password.",
            L"Change PDF Password", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring output;
    if (!selectPdfOutput(output, suggestedOutput(input, L"_new_password"),
                         L"Save PDF with new password")) return;
    if (!validateDistinctOutput({ input }, output)) return;
    runOperation(L"Changing PDF password...", output, false,
        [&](std::string& error) {
            return ReaderPdfDocument::ChangePassword(input, output,
                wideToUtf8(fields[0].value), wideToUtf8(fields[1].value),
                wideToUtf8(fields[2].value), error);
        });
}


namespace {

struct ToolCatalogEntry final {
    const wchar_t* category;
    const wchar_t* label;
    int command;
};

const ToolCatalogEntry kToolCatalog[] = {
    { L"RECOMMENDED", L"PDF Text Editor (ALPHA)", ID_TOOL_PDF_TEXT_EDITOR },
    { L"RECOMMENDED", L"Merge", ID_MERGE_PDFS },
    { L"RECOMMENDED", L"Compare", ID_TOOL_COMPARE },
    { L"RECOMMENDED", L"Compress", ID_TOOL_COMPRESS },
    { L"RECOMMENDED", L"Convert", ID_TOOL_CONVERT },
    { L"RECOMMENDED", L"OCR / Cleanup scans", ID_TOOL_OCR },
    { L"RECOMMENDED", L"Redact", ID_TOOL_REDACT },
    { L"RECOMMENDED", L"PDF Multi Tool", ID_TOOL_MULTI_TOOL },

    { L"SIGNING", L"Sign with Certificate", ID_TOOL_SIGN_WITH_CERTIFICATE },
    { L"SIGNING", L"Timestamp PDF", ID_TOOL_TIMESTAMP_PDF },
    { L"SIGNING", L"Sign", ID_TOOL_SIGN },
    { L"SIGNING", L"Shared Signing", ID_TOOL_SHARED_SIGNING },

    { L"DOCUMENT SECURITY", L"Add Password", ID_ADD_PASSWORD },
    { L"DOCUMENT SECURITY", L"Add Watermark", ID_TOOL_ADD_WATERMARK },
    { L"DOCUMENT SECURITY", L"Add Stamp to PDF", ID_TOOL_ADD_STAMP },
    { L"DOCUMENT SECURITY", L"Sanitize", ID_TOOL_SANITIZE },
    { L"DOCUMENT SECURITY", L"Flatten", ID_TOOL_FLATTEN },
    { L"DOCUMENT SECURITY", L"Unlock PDF Forms", ID_TOOL_UNLOCK_FORMS },
    { L"DOCUMENT SECURITY", L"Change Permissions", ID_TOOL_CHANGE_PERMISSIONS },

    { L"VERIFICATION", L"Get ALL Info on PDF", ID_TOOL_GET_ALL_INFO },
    { L"VERIFICATION", L"Validate PDF Signature", ID_TOOL_VALIDATE_SIGNATURE },

    { L"DOCUMENT REVIEW", L"Change Metadata", ID_TOOL_CHANGE_METADATA },
    { L"DOCUMENT REVIEW", L"Edit Table of Contents", ID_TOOL_EDIT_TABLE_OF_CONTENTS },
    { L"DOCUMENT REVIEW", L"Read", ID_TOOL_READ },
};

std::wstring toLowerCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

bool containsI(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    return toLowerCopy(std::wstring(haystack)).find(toLowerCopy(std::wstring(needle)))
        != std::wstring::npos;
}

void showPendingTool(const std::wstring& title, const std::wstring& detail) {
    MessageBoxW(mainWindow, detail.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

bool ensureToolDocument(const std::wstring& title) {
    if (document && pageCount > 0) return true;
    MessageBoxW(mainWindow, L"Open a PDF first to use this tool.",
        title.c_str(), MB_OK | MB_ICONINFORMATION);
    return false;
}

} // namespace

void showToolsPanelCatalog() {
    rightPanelMode = RightPanelMode::Tools;
    activeCommentObjectNumber = 0;
    commentPage = -1;
    currentPageComments.clear();
    commentItems.clear();
    if (toolsTitle) {
        SetWindowTextW(toolsTitle, L"Tools");
    }
    if (toolsSearchEdit) {
        SetWindowTextW(toolsSearchEdit, toolsSearchQuery.c_str());
        SendMessageW(toolsSearchEdit, EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(L"Search tools..."));
    }
    populateToolsTree();
}

void populateCommentsTree() {
    if (!toolsTree) return;

    wchar_t buffer[256]{};
    if (toolsSearchEdit) {
        GetWindowTextW(toolsSearchEdit, buffer, static_cast<int>(std::size(buffer)));
    }
    toolsSearchQuery = buffer;

    if (toolsTitle) {
        const std::wstring title = L"Comments " + std::to_wstring(currentPageComments.size());
        SetWindowTextW(toolsTitle, title.c_str());
    }
    if (toolsSearchEdit) {
        SendMessageW(toolsSearchEdit, EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(L"Filter comments..."));
    }

    TreeView_DeleteAllItems(toolsTree);
    commentItems.clear();

    if (commentPage < 0) {
        std::wstring label = L"No comments";
        TVINSERTSTRUCTW item{};
        item.hParent = TVI_ROOT;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;
        item.item.pszText = label.data();
        item.item.lParam = 0;
        TreeView_InsertItem(toolsTree, &item);
        return;
    }

    std::wstring pageLabel = L"Page " + std::to_wstring(commentPage + 1);
    TVINSERTSTRUCTW pageRoot{};
    pageRoot.hParent = TVI_ROOT;
    pageRoot.hInsertAfter = TVI_LAST;
    pageRoot.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
    pageRoot.item.pszText = pageLabel.data();
    pageRoot.item.cChildren = 1;
    pageRoot.item.lParam = 0;
    const HTREEITEM pageItem = TreeView_InsertItem(toolsTree, &pageRoot);

    bool insertedAny = false;
    HTREEITEM selectedItem{};

    for (std::size_t index = 0; index < currentPageComments.size(); ++index) {
        const auto& comment = currentPageComments[index];
        std::wstring label = !comment.subject.empty() ? comment.subject
            : !comment.title.empty() ? comment.title : comment.subtype;
        if (!comment.contents.empty()) {
            if (!label.empty()) label += L": ";
            std::wstring preview = comment.contents;
            std::replace(preview.begin(), preview.end(), L'\n', L' ');
            if (preview.size() > 60) preview = preview.substr(0, 57) + L"...";
            label += preview;
        }
        if (label.empty()) label = L"Comment";
        const bool matches = toolsSearchQuery.empty() ||
            containsI(label, toolsSearchQuery) ||
            containsI(comment.subtype, toolsSearchQuery) ||
            containsI(comment.subject, toolsSearchQuery) ||
            containsI(comment.title, toolsSearchQuery) ||
            containsI(comment.contents, toolsSearchQuery);
        if (!matches) continue;

        TVINSERTSTRUCTW item{};
        item.hParent = pageItem;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;
        item.item.pszText = label.data();
        item.item.lParam = static_cast<LPARAM>(index + 1);
        HTREEITEM inserted = TreeView_InsertItem(toolsTree, &item);
        commentItems.push_back(inserted);
        insertedAny = true;
        if (activeCommentObjectNumber != 0 && comment.objectNumber == activeCommentObjectNumber) {
            selectedItem = inserted;
        }
    }

    if (!insertedAny) {
        std::wstring label = L"No matching comments";
        TVINSERTSTRUCTW item{};
        item.hParent = pageItem;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;
        item.item.pszText = label.data();
        item.item.lParam = 0;
        TreeView_InsertItem(toolsTree, &item);
    }

    TreeView_Expand(toolsTree, pageItem, TVE_EXPAND);
    if (selectedItem) {
        TreeView_SelectItem(toolsTree, selectedItem);
        TreeView_EnsureVisible(toolsTree, selectedItem);
    }
    RedrawWindow(toolsTree, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void populateToolsTree() {
    if (rightPanelMode == RightPanelMode::Comments) {
        populateCommentsTree();
        return;
    }
    if (!toolsTree) return;

    wchar_t buffer[256]{};
    if (toolsSearchEdit) {
        GetWindowTextW(toolsSearchEdit, buffer, static_cast<int>(std::size(buffer)));
    }
    toolsSearchQuery = buffer;
    if (toolsTitle) SetWindowTextW(toolsTitle, L"Tools");
    if (toolsSearchEdit) {
        SendMessageW(toolsSearchEdit, EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(L"Search tools..."));
    }

    TreeView_DeleteAllItems(toolsTree);
    HTREEITEM currentCategory = nullptr;
    std::wstring currentCategoryName;
    bool insertedAny{};

    for (const auto& entry : kToolCatalog) {
        const bool matches = toolsSearchQuery.empty() ||
            containsI(entry.category, toolsSearchQuery) ||
            containsI(entry.label, toolsSearchQuery);
        if (!matches) continue;

        if (currentCategoryName != entry.category) {
            currentCategoryName = entry.category;
            TVINSERTSTRUCTW category{};
            category.hParent = TVI_ROOT;
            category.hInsertAfter = TVI_LAST;
            category.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
            category.item.pszText = const_cast<wchar_t*>(entry.category);
            category.item.cChildren = 1;
            category.item.lParam = 0;
            currentCategory = TreeView_InsertItem(toolsTree, &category);
        }

        TVINSERTSTRUCTW item{};
        item.hParent = currentCategory ? currentCategory : TVI_ROOT;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;
        item.item.pszText = const_cast<wchar_t*>(entry.label);
        item.item.lParam = entry.command;
        TreeView_InsertItem(toolsTree, &item);
        insertedAny = true;
    }

    if (!insertedAny) {
        std::wstring label = L"No matching tools";
        TVINSERTSTRUCTW item{};
        item.hParent = TVI_ROOT;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;
        item.item.pszText = label.data();
        item.item.lParam = 0;
        TreeView_InsertItem(toolsTree, &item);
    }

    HTREEITEM item = TreeView_GetRoot(toolsTree);
    while (item) {
        TreeView_Expand(toolsTree, item, TVE_EXPAND);
        item = TreeView_GetNextSibling(toolsTree, item);
    }

    RedrawWindow(toolsTree, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void showCommentsPanelForPage(int page, std::optional<std::uint32_t> focusObjectNumber) {
    if (!document || page < 0) return;
    rightPanelMode = RightPanelMode::Comments;
    toolsVisible = true;
    commentPage = page;
    currentPageComments = document->CommentsDetailed(page);
    activeCommentObjectNumber = focusObjectNumber.value_or(0);
    toolsSearchQuery.clear();
    if (toolsSearchEdit) {
        SetWindowTextW(toolsSearchEdit, L"");
    }
    RECT client{};
    GetClientRect(mainWindow, &client);
    updateLayout(client.right, client.bottom);
    updateCommandState();
    populateCommentsTree();
    if (toolsTree) SetFocus(toolsTree);
    InvalidateRect(mainWindow, nullptr, TRUE);
}

void executeToolCommand(const int command) {
    switch (command) {
    case ID_MERGE_PDFS:
        showToolsPanelCatalog();
        mergePdfDocuments();
        return;
    case ID_ADD_PASSWORD:
        showToolsPanelCatalog();
        addPdfPassword();
        return;
    case ID_TOOL_READ:
        showToolsPanelCatalog();
        if (canvas) SetFocus(canvas);
        setStatus(L"Read tool ready");
        return;
    case ID_TOOL_GET_ALL_INFO:
        showToolsPanelCatalog();
        if (!ensureToolDocument(L"Get ALL Info on PDF")) return;
        showDocumentProperties();
        return;
    case ID_TOOL_EDIT_TABLE_OF_CONTENTS:
        showToolsPanelCatalog();
        if (!ensureToolDocument(L"Edit Table of Contents")) return;
        sidebarVisible = true;
        updateCommandState();
        { RECT client{}; GetClientRect(mainWindow, &client); updateLayout(client.right, client.bottom); }
        SetFocus(pageList);
        showPendingTool(L"Edit Table of Contents",
            L"The tools panel entry is now wired into the UI. A full TOC editor dialog will be implemented next.\n\n"
            L"For now, the current table of contents is opened on the left side.");
        return;
    case ID_TOOL_CHANGE_METADATA:
        showToolsPanelCatalog();
        if (!ensureToolDocument(L"Change Metadata")) return;
        showPendingTool(L"Change Metadata",
            L"Metadata editing UI is scaffolded in the tools panel, but the dedicated editor dialog has not been implemented yet.");
        return;
    case ID_TOOL_PDF_TEXT_EDITOR:
        showToolsPanelCatalog();
        if (!ensureToolDocument(L"PDF Text Editor")) return;
        showPendingTool(L"PDF Text Editor (ALPHA)",
            L"The PDF Text Editor entry has been added to the right-side tools workspace. The editing engine and annotation/text-edit UI are the next implementation step.");
        return;
    case ID_TOOL_MULTI_TOOL:
        showToolsPanelCatalog();
        if (!ensureToolDocument(L"PDF Multi Tool")) return;
        showPendingTool(L"PDF Multi Tool",
            L"Use the Document menu for the currently implemented operations: Extract Pages, Split PDF, Delete Pages, Duplicate Pages, Move Page, Reorder Pages, Reverse Page Order, and Password tools.");
        return;
    case ID_TOOL_COMPARE:
        showToolsPanelCatalog();
        showPendingTool(L"Compare", L"Compare PDF is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_COMPRESS:
        showToolsPanelCatalog();
        showPendingTool(L"Compress", L"Compress PDF is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_CONVERT:
        showToolsPanelCatalog();
        showPendingTool(L"Convert", L"Convert PDF is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_OCR:
        showToolsPanelCatalog();
        showPendingTool(L"OCR / Cleanup scans", L"OCR and cleanup tools are not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_REDACT:
        showToolsPanelCatalog();
        showPendingTool(L"Redact", L"Redaction tools are not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_SIGN_WITH_CERTIFICATE:
        showToolsPanelCatalog();
        showPendingTool(L"Sign with Certificate", L"Certificate signing UI is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_TIMESTAMP_PDF:
        showToolsPanelCatalog();
        showPendingTool(L"Timestamp PDF", L"Timestamp PDF is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_SIGN:
        showToolsPanelCatalog();
        showPendingTool(L"Sign", L"Signing UI is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_SHARED_SIGNING:
        showToolsPanelCatalog();
        showPendingTool(L"Shared Signing", L"Shared signing is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_ADD_WATERMARK:
        showToolsPanelCatalog();
        showPendingTool(L"Add Watermark", L"Watermark tools are not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_ADD_STAMP:
        showToolsPanelCatalog();
        showPendingTool(L"Add Stamp to PDF", L"Stamp tools are not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_SANITIZE:
        showToolsPanelCatalog();
        showPendingTool(L"Sanitize", L"Sanitize PDF is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_FLATTEN:
        showToolsPanelCatalog();
        showPendingTool(L"Flatten", L"Flatten PDF is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_UNLOCK_FORMS:
        showToolsPanelCatalog();
        showPendingTool(L"Unlock PDF Forms", L"Unlock PDF Forms is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_CHANGE_PERMISSIONS:
        showToolsPanelCatalog();
        showPendingTool(L"Change Permissions", L"Change Permissions is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    case ID_TOOL_VALIDATE_SIGNATURE:
        showToolsPanelCatalog();
        showPendingTool(L"Validate PDF Signature", L"Signature validation is not implemented yet. The tools panel entry and navigation are now in place.");
        return;
    default:
        break;
    }
}


} // namespace PdfPP::Win32
