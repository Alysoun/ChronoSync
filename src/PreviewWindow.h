#pragma once

#include "GuiCommon.h"
#include <vector>
#include <string>

struct PreviewWindowContext {
    std::vector<ChronoSync::PreviewItem>* pList = nullptr;
    ChronoSync::SyncPlanAnalysis analysis;
    bool hasAnalysis = false;
    std::vector<int> displayedIndices;
    std::wstring sourceRoot;
    std::wstring destRoot;
    int contextMenuItem = -1;
    HWND hwndLV = NULL;
    HWND hwndFilter = NULL;
    HWND lblSummary = NULL;
    HWND btnExport = NULL;
    HWND btnCopy = NULL;
    HWND btnClose = NULL;
    int sortColumn = -1;
    bool sortAscending = true;
};

bool RegisterPreviewWindowClass(HINSTANCE hInstance);
LRESULT CALLBACK PreviewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
