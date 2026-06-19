#pragma once

#include <windows.h>
#include <string>

namespace PrevueSync {

    constexpr int DisclaimerVersion = 3;

    std::wstring GetDisclaimerText();

    bool IsDisclaimerAccepted();
    void SetDisclaimerAccepted();

    // Modal acceptance dialog. Returns true if the user accepted.
    bool PromptDisclaimerAcceptance(HWND parent);

    // Read-only disclaimer viewer (Copy supported).
    void ShowDisclaimerDialog(HWND parent);

    void PrintCliDisclaimer();

} // namespace PrevueSync
