#pragma once

#include <ole2.h>

#include <string>

#include "ui/widgets.h"

namespace app
{

/// The dimmed window and badge shown while something is dragged over Nagare, from DragOverlayView.
class DropOverlay final : public ui::View
{
public:
    DropOverlay();

    void showBadge(std::wstring icon, std::wstring mainLine, std::wstring subLine);
    void hideBadge();

    void draw(ID2D1DeviceContext* context) override;
    ui::View* hitTest(D2D1_POINT_2F) override { return nullptr; }

private:
    void fadeTo(float target, DWORD durationMs);

    std::wstring icon_;
    std::wstring main_;
    std::wstring sub_;
    ui::Animator fade_;
};

class DropTarget;

/// Hooks the window up to OLE drag and drop. WM_DROPFILES only speaks once the mouse lets go, and the
/// overlay has to appear while the drag is still over the window.
DropTarget* registerDropTarget(DropOverlay* overlay);
void releaseDropTarget(DropTarget* target);
/// The COM face of the target, so the tests can play the part of a drag source.
IDropTarget* dropInterface(DropTarget* target);

} // namespace app
