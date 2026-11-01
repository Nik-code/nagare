#pragma once

#include <functional>
#include <string>

namespace app
{

/// A question sheet in the Mac alert's shape: bold title, message, confirm on the right.
struct ConfirmRequest
{
    std::wstring title;
    std::wstring message;
    std::wstring confirm = L"OK";
    /// Empty leaves only the confirm button, for a plain notice.
    std::wstring cancel;
    /// Paints the confirm button as the accent, since removing is the expected answer here.
    std::function<void()> onConfirm;
};

void openConfirmPanel(ConfirmRequest request);

/// Asks for a new name, from FileRenameSheetController.
void openRenamePanel(std::wstring const& current, std::function<void(std::wstring const&)> onRename);

} // namespace app
