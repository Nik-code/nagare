#pragma once

#include <windows.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gfx/renderer.h"
#include "ui/view.h"

namespace ui
{

/// WM_COPYDATA from a later launch: a torrent path, a magnet link, or nothing to just come forward.
inline constexpr ULONG_PTR kOpenRequest = 0x4E41;
/// WM_COPYDATA from the uninstaller, which needs the running app gone.
inline constexpr ULONG_PTR kQuitRequest = 0x4E42;

/// The one top-level window. Panels live inside it as a second glass layer, so they can refract the app.
class Window
{
public:
    static Window& get();

    bool create(std::wstring const& title, int width, int height);
    /// Leaves the window hidden when run() starts, for a launch at sign-in.
    void setStartHidden(bool hidden) { sized_ = hidden; }
    int run();
    /// Hides the window at once, since saving can take a few seconds, and ends run().
    void quit(int code = 0);

    /// Hides the window and frees all it can, keeping only the process and the engine going.
    void hideToTray();
    /// Shows the window from any state, on top.
    void bringForward();

    Renderer& renderer() { return renderer_; }
    HWND handle() const { return window_; }
    float width() const;
    float height() const;
    bool visible() const;
    bool active() const;
    /// Where the minimise, maximise and close buttons sit, so the header can put glass behind them.
    D2D1_RECT_F captionRect() const;

    void setRoot(std::unique_ptr<View> root);
    View* root() const { return root_.get(); }

    /// Shows a panel over the window, replacing any open one. Pass nullptr to close it.
    void setPanel(std::unique_ptr<View> panel);
    View* panel() const { return panel_.get(); }

    /// Shows a popup above everything, replacing any open one. It takes all input until closed, and
    /// any press outside it closes it without reaching what's below. Pass nullptr to close it.
    /// A popup never adds a glass layer: it draws in the top layer's overlay, panel open or not.
    void setPopup(std::unique_ptr<View> popup);
    View* popup() const { return popup_.get(); }

    void invalidate(D2D1_RECT_F rect);
    void invalidateAll();

    /// Runs `callback` once after `delayMs`. Timers only ever run on this thread.
    int after(DWORD delayMs, std::function<void()> callback);
    /// Runs `callback` every `intervalMs` until cancelled.
    int every(DWORD intervalMs, std::function<void()> callback);
    void cancel(int timer);

    /// Calls `step` with 0..1 at display rate for `durationMs`, then `done`.
    int animate(DWORD durationMs, std::function<void(float)> step, std::function<void()> done = {});

    void focus(View* view);
    View* focused() const { return focused_; }
    void forgetView(View* view);

    void setCursor(HCURSOR cursor);

    /// Called for files dropped on the window.
    std::function<void(std::vector<std::wstring> const&)> onFilesDropped;
    /// Called with a string sent by another Nagare, such as a magnet link. It may be empty.
    std::function<void(std::wstring const&)> onOpenRequest;
    /// Called on the close button. Return false to keep the process alive, as the tray does.
    std::function<bool()> onCloseRequest;
    /// Sees every message first. Return true to swallow it, which is how the engine wakes the app.
    std::function<bool(UINT, WPARAM, LPARAM)> onMessage;
    /// Called after the app becomes visible again, so views can refresh what they skipped while hidden.
    std::function<void()> onShown;

    /// Captures the next frame to a PNG, for the build checks.
    void saveNextFrame(std::wstring path);

    /// For the self tests: frames shown so far, whether one is waiting, and live timers and animations.
    uint64_t framesPainted() const { return framesPainted_; }
    bool framePending() const { return dirty_; }
    size_t pendingWork() const { return timers_.size() + animations_.size(); }

private:
    struct Timer
    {
        ULONGLONG due = 0;
        DWORD interval = 0;
        std::function<void()> callback;
    };
    struct Animation
    {
        ULONGLONG start = 0;
        DWORD duration = 0;
        std::function<void(float)> step;
        std::function<void()> done;
    };

    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);

    void paintFrame();
    void runTimers();
    void runAnimations();
    DWORD nextWake() const;
    LRESULT hitTest(POINT screen);
    void mouse(MouseKind kind, LPARAM lparam, float wheel = 0.0F);
    View* viewAt(D2D1_POINT_2F windowPoint) const;
    void updateHover(View* view, D2D1_POINT_2F windowPoint);
    D2D1_POINT_2F toLocal(View* view, D2D1_POINT_2F windowPoint) const;
    D2D1_POINT_2F dips(POINT client) const;
    void drawCaptionButtons(ID2D1DeviceContext* context);
    void setCaptionHover(int hit);
    void invalidateFrame();
    void releaseGraphics();
    void restoreGraphics();

    HWND window_ = nullptr;
    Renderer renderer_;
    bool ready_ = false;
    std::unique_ptr<View> root_;
    std::unique_ptr<View> panel_;
    std::unique_ptr<View> popup_;
    // A panel or popup usually closes from inside one of its own handlers, so it dies between messages.
    std::vector<std::unique_ptr<View>> retired_;
    View* hovered_ = nullptr;
    View* pressed_ = nullptr;
    View* focused_ = nullptr;
    View* focusBelowPanel_ = nullptr;
    bool tracking_ = false;
    int captionHover_ = 0;
    uint64_t framesPainted_ = 0;

    bool dirty_ = true;
    // Set by anything that changes the frame without saying where, like a popup or an animation.
    bool wholeFrame_ = true;
    bool sceneDirty_ = true;
    D2D1_RECT_F dirtyRect_{};
    bool sized_ = false;
    bool released_ = false;

    int nextId_ = 1;
    std::map<int, Timer> timers_;
    std::map<int, Animation> animations_;
};

} // namespace ui
