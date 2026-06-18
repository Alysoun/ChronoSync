#include "PreviewWindow.h"
#include "GuiDialogs.h"

#include <uxtheme.h>
#include <algorithm>
#include <fstream>

static bool ContainsInsensitive(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    std::wstring lowerHay = haystack;
    std::wstring lowerNeedle = needle;
    std::transform(lowerHay.begin(), lowerHay.end(), lowerHay.begin(), towlower);
    std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), towlower);
    return lowerHay.find(lowerNeedle) != std::wstring::npos;
}

static void RebuildPreviewFilter(PreviewWindowContext* ctx) {
    if (!ctx || !ctx->pList) {
        return;
    }

    wchar_t filterBuf[256] = {};
    if (ctx->hwndFilter) {
        GetWindowTextW(ctx->hwndFilter, filterBuf, 256);
    }

    ctx->displayedIndices.clear();
    for (int i = 0; i < static_cast<int>(ctx->pList->size()); ++i) {
        const auto& item = (*ctx->pList)[static_cast<size_t>(i)];
        if (ContainsInsensitive(item.relativePath, filterBuf) ||
            ContainsInsensitive(item.action, filterBuf) ||
            ContainsInsensitive(item.sizeStr, filterBuf)) {
            ctx->displayedIndices.push_back(i);
        }
    }

    if (ctx->hwndLV) {
        SendMessageW(ctx->hwndLV, WM_SETREDRAW, FALSE, 0);
        SendMessageW(ctx->hwndLV, LVM_SETITEMCOUNT, (WPARAM)ctx->displayedIndices.size(), (LPARAM)LVSICF_NOINVALIDATEALL);
        SendMessageW(ctx->hwndLV, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(ctx->hwndLV, NULL, TRUE);
    }

    if (ctx->lblSummary) {
        std::wstring summary;
        if (ctx->hasAnalysis) {
            summary = L"Risk: " + ChronoSync::RiskLevelToString(ctx->analysis.risk)
                    + L" | Transfer: " + std::to_wstring(ctx->analysis.filesToCopyNew + ctx->analysis.filesToCopyUpdate)
                    + L" file(s), "
                    + std::to_wstring(ctx->analysis.deletesPrune + ctx->analysis.deletesReplace)
                    + L" removal(s) | Showing "
                    + std::to_wstring(ctx->displayedIndices.size())
                    + L" of "
                    + std::to_wstring(ctx->pList->size())
                    + L" planned changes.";
        } else {
            summary = L"Showing " + std::to_wstring(ctx->displayedIndices.size()) +
                      L" of " + std::to_wstring(ctx->pList->size()) + L" planned changes.";
        }
        SetWindowTextW(ctx->lblSummary, summary.c_str());
    }
}

static std::wstring ResolvePreviewExplorerPath(const ChronoSync::PreviewItem& item,
                                               const std::wstring& sourceRoot,
                                               const std::wstring& destRoot) {
    const bool targetsDestination =
        item.action.rfind(L"Delete (", 0) == 0 || item.action.rfind(L"Remove (", 0) == 0;
    const std::wstring& root = targetsDestination ? destRoot : sourceRoot;
    return root + L"\\" + item.relativePath;
}

static std::wstring BuildPreviewClipboardText(PreviewWindowContext* ctx) {
    if (!ctx || !ctx->pList) {
        return {};
    }

    const std::vector<int>& indices = !ctx->displayedIndices.empty()
        ? ctx->displayedIndices
        : std::vector<int>();

    std::wstring text = L"Relative Path\tPlanned Action\tSize\r\n";
    auto appendRow = [&](const ChronoSync::PreviewItem& item) {
        text += item.relativePath;
        text += L'\t';
        text += item.action;
        text += L'\t';
        text += item.sizeStr;
        text += L"\r\n";
    };

    if (!indices.empty()) {
        for (int index : indices) {
            appendRow((*ctx->pList)[static_cast<size_t>(index)]);
        }
    } else {
        for (const auto& item : *ctx->pList) {
            appendRow(item);
        }
    }
    return text;
}

static void CopyPreviewToClipboard(PreviewWindowContext* ctx) {
    CopyWideTextToClipboard(BuildPreviewClipboardText(ctx));
}

LRESULT CALLBACK PreviewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            PreviewLaunchData* launchData =
                (PreviewLaunchData*)((LPCREATESTRUCTW)lParam)->lpCreateParams;

            PreviewWindowContext* ctx = new PreviewWindowContext();
            if (launchData) {
                ctx->pList = launchData->pList;
                ctx->analysis = launchData->analysis;
                ctx->hasAnalysis = launchData->hasAnalysis;
                ctx->sourceRoot = std::move(launchData->sourceRoot);
                ctx->destRoot = std::move(launchData->destRoot);
                delete launchData;
            }
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);

            ctx->hwndFilter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                15, 15, 650, 24, hWnd, (HMENU)ID_PREVIEW_FILTER_EDIT, GetModuleHandleW(NULL), NULL);
            SendMessageW(ctx->hwndFilter, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            ctx->hwndLV = CreateWindowExW(
                0, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | WS_BORDER | WS_VSCROLL,
                15, 48, 650, 327,
                hWnd, (HMENU)201, GetModuleHandleW(NULL), NULL
            );

            SendMessageW(ctx->hwndLV, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            LVCOLUMNW col = {0};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

            col.pszText = const_cast<LPWSTR>(L"Relative Path");
            col.cx = 360;
            col.iSubItem = 0;
            ListView_InsertColumn(ctx->hwndLV, 0, &col);

            col.pszText = const_cast<LPWSTR>(L"Planned Action");
            col.cx = 140;
            col.iSubItem = 1;
            ListView_InsertColumn(ctx->hwndLV, 1, &col);

            col.pszText = const_cast<LPWSTR>(L"Size");
            col.cx = 120;
            col.iSubItem = 2;
            ListView_InsertColumn(ctx->hwndLV, 2, &col);

            SendMessageW(ctx->hwndLV, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

            ListView_SetBkColor(ctx->hwndLV, UiTheme::InputBg);
            ListView_SetTextBkColor(ctx->hwndLV, UiTheme::InputBg);
            ListView_SetTextColor(ctx->hwndLV, UiTheme::InputText);

            ctx->lblSummary = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                              15, 395, 450, 20, hWnd, (HMENU)202, GetModuleHandleW(NULL), NULL);
            SendMessageW(ctx->lblSummary, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            if (ctx->pList) {
                RebuildPreviewFilter(ctx);
            }

            ctx->btnExport = CreateWindowExW(0, L"BUTTON", L"Export CSV...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                             300, 390, 115, 30, hWnd, (HMENU)ID_EXPORT_CSV_BUTTON, GetModuleHandleW(NULL), NULL);
            SendMessageW(ctx->btnExport, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            ctx->btnCopy = CreateWindowExW(0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           425, 390, 115, 30, hWnd, (HMENU)ID_PREVIEW_COPY_BUTTON, GetModuleHandleW(NULL), NULL);
            SendMessageW(ctx->btnCopy, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            ctx->btnClose = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                             550, 390, 115, 30, hWnd, (HMENU)IDCANCEL, GetModuleHandleW(NULL), NULL);
            SendMessageW(ctx->btnClose, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            break;
        }
        case WM_SIZE: {
            PreviewWindowContext* ctx =
                (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            if (ctx) {
                int cx = LOWORD(lParam);
                int cy = HIWORD(lParam);

                if (ctx->hwndFilter) {
                    MoveWindow(ctx->hwndFilter, 15, 15, cx - 30, 24, TRUE);
                }
                if (ctx->hwndLV) {
                    MoveWindow(ctx->hwndLV, 15, 48, cx - 30, cy - 103, TRUE);
                }
                if (ctx->lblSummary) {
                    MoveWindow(ctx->lblSummary, 15, cy - 40, cx - 270, 20, TRUE);
                }
                if (ctx->btnExport) {
                    MoveWindow(ctx->btnExport, cx - 380, cy - 45, 115, 30, TRUE);
                }
                if (ctx->btnCopy) {
                    MoveWindow(ctx->btnCopy, cx - 255, cy - 45, 115, 30, TRUE);
                }
                if (ctx->btnClose) {
                    MoveWindow(ctx->btnClose, cx - 130, cy - 45, 115, 30, TRUE);
                }
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hwndCtrl = (HWND)lParam;
            if (IsEditControl(hwndCtrl)) {
                SetTextColor(hdcStatic, UiTheme::InputText);
                SetBkColor(hdcStatic, UiTheme::InputBg);
                return (INT_PTR)g_hbrInputBackground;
            }
            SetTextColor(hdcStatic, UiTheme::LabelText);
            SetBkMode(hdcStatic, TRANSPARENT);
            return (INT_PTR)g_hbrBackground;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, UiTheme::InputText);
            SetBkColor(hdcEdit, UiTheme::InputBg);
            return (INT_PTR)g_hbrInputBackground;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            PreviewWindowContext* ctx =
                (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

            if (wmId == ID_PREVIEW_FILTER_EDIT && HIWORD(wParam) == EN_CHANGE) {
                RebuildPreviewFilter(ctx);
                break;
            }

            if (wmId == IDCANCEL) {
                DestroyWindow(hWnd);
            } else if (wmId == ID_PREVIEW_COPY_BUTTON) {
                CopyPreviewToClipboard(ctx);
            } else if (wmId == ID_PREVIEW_LOCATE_EXPLORER) {
                if (ctx && ctx->pList && ctx->contextMenuItem >= 0 &&
                    ctx->contextMenuItem < static_cast<int>(ctx->displayedIndices.size())) {
                    const auto& item = (*ctx->pList)[static_cast<size_t>(
                        ctx->displayedIndices[static_cast<size_t>(ctx->contextMenuItem)])];
                    std::wstring fullPath = ResolvePreviewExplorerPath(item, ctx->sourceRoot, ctx->destRoot);
                    RevealInExplorer(hWnd, fullPath);
                }
            } else if (wmId == ID_EXPORT_CSV_BUTTON) {
                if (ctx && ctx->pList && !ctx->pList->empty()) {
                    std::wstring selectedPath = SaveCSVDialog(hWnd, L"Export Preview to CSV");
                    if (!selectedPath.empty()) {
                        std::ofstream out(selectedPath, std::ios::binary);
                        if (out) {
                            out << "\xEF\xBB\xBF";
                            out << "Relative Path,Planned Action,Size (Bytes),Formatted Size\n";

                            const auto& indices = (!ctx->displayedIndices.empty())
                                ? ctx->displayedIndices
                                : std::vector<int>();
                            if (!indices.empty()) {
                                for (int index : indices) {
                                    const auto& item = (*ctx->pList)[static_cast<size_t>(index)];
                                    std::wstring escapedPath = EscapeCSV(item.relativePath);
                                    std::wstring escapedAction = EscapeCSV(item.action);
                                    std::wstring rawSizeStr = std::to_wstring(item.fileSize);
                                    std::wstring escapedSize = EscapeCSV(item.sizeStr);

                                    std::string utf8Line = WideToUTF8(escapedPath + L"," + escapedAction + L"," + rawSizeStr + L"," + escapedSize + L"\n");
                                    out.write(utf8Line.data(), utf8Line.size());
                                }
                            } else {
                                for (const auto& item : *ctx->pList) {
                                    std::wstring escapedPath = EscapeCSV(item.relativePath);
                                    std::wstring escapedAction = EscapeCSV(item.action);
                                    std::wstring rawSizeStr = std::to_wstring(item.fileSize);
                                    std::wstring escapedSize = EscapeCSV(item.sizeStr);

                                    std::string utf8Line = WideToUTF8(escapedPath + L"," + escapedAction + L"," + rawSizeStr + L"," + escapedSize + L"\n");
                                    out.write(utf8Line.data(), utf8Line.size());
                                }
                            }
                            out.close();
                            MessageBoxW(hWnd, L"Preview items exported successfully!", L"Export Complete", MB_OK | MB_ICONINFORMATION);
                        } else {
                            MessageBoxW(hWnd, L"Failed to create CSV file.", L"Error", MB_OK | MB_ICONERROR);
                        }
                    }
                } else {
                    MessageBoxW(hWnd, L"No preview items to export.", L"Export Preview", MB_OK | MB_ICONWARNING);
                }
            }
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            PreviewWindowContext* ctx =
                (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

            if (pnmh->code == LVN_GETDISPINFOW) {
                NMLVDISPINFOW* plvdi = (NMLVDISPINFOW*)lParam;
                if (ctx && ctx->pList && plvdi->item.iItem < (int)ctx->displayedIndices.size()) {
                    const auto& item = (*ctx->pList)[static_cast<size_t>(ctx->displayedIndices[static_cast<size_t>(plvdi->item.iItem)])];
                    if (plvdi->item.mask & LVIF_TEXT) {
                        if (plvdi->item.iSubItem == 0) {
                            plvdi->item.pszText = const_cast<wchar_t*>(item.relativePath.c_str());
                        } else if (plvdi->item.iSubItem == 1) {
                            plvdi->item.pszText = const_cast<wchar_t*>(item.action.c_str());
                        } else if (plvdi->item.iSubItem == 2) {
                            plvdi->item.pszText = const_cast<wchar_t*>(item.sizeStr.c_str());
                        }
                    }
                }
            } else if (pnmh->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)lParam;
                if (ctx && ctx->pList && !ctx->displayedIndices.empty()) {
                    int col = pnmlv->iSubItem;
                    if (ctx->sortColumn == col) {
                        ctx->sortAscending = !ctx->sortAscending;
                    } else {
                        ctx->sortColumn = col;
                        ctx->sortAscending = true;
                    }

                    bool asc = ctx->sortAscending;
                    auto compareByIndex = [&](int aIdx, int bIdx) {
                        const auto& a = (*ctx->pList)[static_cast<size_t>(aIdx)];
                        const auto& b = (*ctx->pList)[static_cast<size_t>(bIdx)];
                        if (col == 0) {
                            return asc ? (a.relativePath < b.relativePath) : (a.relativePath > b.relativePath);
                        }
                        if (col == 1) {
                            if (a.action != b.action) {
                                return asc ? (a.action < b.action) : (a.action > b.action);
                            }
                            return asc ? (a.relativePath < b.relativePath) : (a.relativePath > b.relativePath);
                        }
                        if (a.fileSize != b.fileSize) {
                            return asc ? (a.fileSize < b.fileSize) : (a.fileSize > b.fileSize);
                        }
                        return asc ? (a.relativePath < b.relativePath) : (a.relativePath > b.relativePath);
                    };

                    std::sort(ctx->displayedIndices.begin(), ctx->displayedIndices.end(), compareByIndex);
                    InvalidateRect(ctx->hwndLV, NULL, TRUE);
                }
            } else if (pnmh->code == NM_RCLICK && pnmh->idFrom == 201) {
                LPNMITEMACTIVATE itemActivate = (LPNMITEMACTIVATE)lParam;
                int item = itemActivate->iItem;
                if (ctx && item >= 0 && item < static_cast<int>(ctx->displayedIndices.size())) {
                    ctx->contextMenuItem = item;
                    ListView_SetItemState(ctx->hwndLV, item, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);

                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_LOCATE_EXPLORER, L"Show in File Explorer");

                    POINT pt = itemActivate->ptAction;
                    ClientToScreen(pnmh->hwndFrom, &pt);
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
                }
            }
            break;
        }
        case WM_KEYDOWN: {
            PreviewWindowContext* ctx =
                (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            if (ctx && ctx->hwndLV && GetFocus() == ctx->hwndLV &&
                (GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'C' || wParam == 'c')) {
                CopyPreviewToClipboard(ctx);
                return 0;
            }
            break;
        }
        case WM_DESTROY: {
            PreviewWindowContext* ctx =
                (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            if (ctx) {
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
                if (ctx->pList) {
                    delete ctx->pList;
                }
                delete ctx;
            }
            break;
        }
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool RegisterPreviewWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcp = {0};
    wcp.cbSize = sizeof(WNDCLASSEXW);
    wcp.style = CS_HREDRAW | CS_VREDRAW;
    wcp.lpfnWndProc = PreviewWndProc;
    wcp.hInstance = hInstance;
    wcp.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcp.hbrBackground = CreateSolidBrush(UiTheme::WindowBg);
    wcp.lpszClassName = L"ChronoSyncPreviewWindow";
    ApplyAppWindowIcons(wcp, hInstance);
    return RegisterClassExW(&wcp) != 0;
}
