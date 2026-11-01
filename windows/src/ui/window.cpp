#include "ui/window.h"

#include <dwmapi.h>
#include <imm.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <utility>

#include "ui/theme.h"

namespace ui
{

namespace
{

constexpr int kMinWidth = 760;
constexpr int kMinHeight = 560;
constexpr float kCaptionHeight = 56.0F;
constexpr float kButtonWidth = 39.0F;
constexpr float kButtonsRight = 30.0F;
constexpr float kButtonsTop = 10.0F;
constexpr float kButtonHeight = 36.0F;
constexpr float kButtonsPadding = 2.0F;
constexpr float kButtonHoverSize = 32.0F;
constexpr DWORD kHiddenAnimationWakeMs = 250;

} // namespace

Window& Window::get()
{
    static Window window;
    return window;
}

bool Window::create(std::wstring const& title, int width, int height)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = proc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(windowClass.hInstance, L"IDI_APP");
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(windowClass.hInstance, L"IDI_APP", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    windowClass.lpszClassName = L"NagareWindow";
    RegisterClassExW(&windowClass);

    UINT const dpi = GetDpiForSystem();
    window_ = CreateWindowExW(
        0,
        L"NagareWindow",
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        MulDiv(width, dpi, 96),
        MulDiv(height, dpi, 96),
        nullptr,
        nullptr,
        windowClass.hInstance,
        nullptr);
    if (window_ == nullptr)
    {
        return false;
    }

    MARGINS const margins{ 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea(window_, &margins);
    DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    DragAcceptFiles(window_, TRUE);
    // Only a focused text field turns the input method on, so typing elsewhere can't start composing.
    ImmAssociateContext(window_, nullptr);

    if (!renderer_.init(window_))
    {
        return false;
    }
    Theme::get().init(&renderer_);
    BOOL dark = Theme::get().dark();
    DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    ready_ = true;
    return true;
}

float Window::width() const
{
    RECT rect{};
    GetClientRect(window_, &rect);
    return (rect.right - rect.left) * 96.0F / renderer_.dpi();
}

float Window::height() const
{
    RECT rect{};
    GetClientRect(window_, &rect);
    return (rect.bottom - rect.top) * 96.0F / renderer_.dpi();
}

bool Window::visible() const
{
    return IsWindowVisible(window_) && !IsIconic(window_);
}

void Window::setRoot(std::unique_ptr<View> root)
{
    root_ = std::move(root);
    root_->setFrame(D2D1::RectF(0.0F, 0.0F, width(), height()));
    root_->layout();
    invalidateAll();
}

void Window::setPanel(std::unique_ptr<View> panel)
{
    bool const opening = panel_ == nullptr && panel != nullptr;
    bool const closing = panel_ != nullptr && panel == nullptr;
    if (panel_ != nullptr)
    {
        forgetView(panel_.get());
        retired_.push_back(std::move(panel_));
    }
    if (opening)
    {
        // Nothing below a panel may see input, and that includes keys sent to whatever had focus.
        focusBelowPanel_ = focused_;
        focus(nullptr);
        updateHover(nullptr, D2D1::Point2F(-1.0F, -1.0F));
    }
    panel_ = std::move(panel);
    if (panel_ != nullptr)
    {
        panel_->layout();
    }
    if (closing)
    {
        View* restored = focusBelowPanel_;
        focusBelowPanel_ = nullptr;
        focus(restored);
    }
    invalidateFrame();
}

void Window::setPopup(std::unique_ptr<View> popup)
{
    if (popup_ != nullptr)
    {
        forgetView(popup_.get());
        retired_.push_back(std::move(popup_));
    }
    popup_ = std::move(popup);
    if (popup_ != nullptr)
    {
        popup_->layout();
    }
    invalidateFrame();
}

void Window::invalidate(D2D1_RECT_F rect)
{
    dirty_ = true;
    if (!sceneDirty_)
    {
        dirtyRect_ = rect;
        sceneDirty_ = true;
        return;
    }
    dirtyRect_.left = std::min(dirtyRect_.left, rect.left);
    dirtyRect_.top = std::min(dirtyRect_.top, rect.top);
    dirtyRect_.right = std::max(dirtyRect_.right, rect.right);
    dirtyRect_.bottom = std::max(dirtyRect_.bottom, rect.bottom);
}

void Window::invalidateFrame()
{
    dirty_ = true;
    wholeFrame_ = true;
}

void Window::invalidateAll()
{
    invalidate(D2D1::RectF(0.0F, 0.0F, width(), height()));
}

int Window::after(DWORD delayMs, std::function<void()> callback)
{
    int const id = nextId_++;
    timers_[id] = Timer{ GetTickCount64() + delayMs, 0, std::move(callback) };
    return id;
}

int Window::every(DWORD intervalMs, std::function<void()> callback)
{
    int const id = nextId_++;
    timers_[id] = Timer{ GetTickCount64() + intervalMs, intervalMs, std::move(callback) };
    return id;
}

void Window::cancel(int timer)
{
    timers_.erase(timer);
    animations_.erase(timer);
}

int Window::animate(DWORD durationMs, std::function<void(float)> step, std::function<void()> done)
{
    int const id = nextId_++;
    animations_[id] = Animation{ GetTickCount64(), std::max<DWORD>(1, durationMs), std::move(step), std::move(done) };
    invalidateFrame();
    return id;
}

void Window::focus(View* view)
{
    if (view == focused_)
    {
        return;
    }
    View* previous = focused_;
    focused_ = view;
    if (previous != nullptr)
    {
        previous->onFocus(false);
    }
    if (view != nullptr)
    {
        view->onFocus(true);
    }
}

void Window::forgetView(View* view)
{
    auto const isInside = [view](View* candidate)
    {
        for (; candidate != nullptr; candidate = candidate->parent())
        {
            if (candidate == view)
            {
                return true;
            }
        }
        return false;
    };
    if (isInside(hovered_))
    {
        hovered_ = nullptr;
    }
    if (isInside(pressed_))
    {
        pressed_ = nullptr;
    }
    if (isInside(focused_))
    {
        focused_ = nullptr;
    }
    if (isInside(focusBelowPanel_))
    {
        focusBelowPanel_ = nullptr;
    }
}

void Window::setCursor(HCURSOR cursor)
{
    SetCursor(cursor != nullptr ? cursor : LoadCursorW(nullptr, IDC_ARROW));
}

void Window::saveNextFrame(std::wstring path)
{
    renderer_.saveNextFrame(std::move(path));
    invalidateFrame();
}

void Window::quit(int code)
{
    ShowWindow(window_, SW_HIDE);
    PostQuitMessage(code);
}

void Window::releaseGraphics()
{
    Theme::get().trim();
    // Every target is window sized, so a hidden window keeps them at one pixel.
    renderer_.resize(1, 1, renderer_.dpi());
    renderer_.trim();
    released_ = true;
}

void Window::restoreGraphics()
{
    if (!released_)
    {
        return;
    }
    released_ = false;
    RECT client{};
    GetClientRect(window_, &client);
    renderer_.resize(client.right - client.left, client.bottom - client.top, static_cast<float>(GetDpiForWindow(window_)));
    invalidateAll();
}

void Window::hideToTray()
{
    setPopup(nullptr);
    ShowWindow(window_, SW_HIDE);
    releaseGraphics();
}

void Window::bringForward()
{
    bool const wasHidden = !IsWindowVisible(window_);
    if (!IsIconic(window_))
    {
        restoreGraphics();
    }
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window_);
    if (wasHidden)
    {
        // Drawing now means the first thing on screen is the app, not an empty swap chain.
        dirty_ = false;
        paintFrame();
    }
}

D2D1_POINT_2F Window::dips(POINT client) const
{
    float const scale = 96.0F / renderer_.dpi();
    return D2D1::Point2F(client.x * scale, client.y * scale);
}

D2D1_POINT_2F Window::toLocal(View* view, D2D1_POINT_2F windowPoint) const
{
    auto const origin = view->absoluteOrigin();
    return D2D1::Point2F(windowPoint.x - origin.x, windowPoint.y - origin.y);
}

bool Window::active() const
{
    return GetForegroundWindow() == window_;
}

D2D1_RECT_F Window::captionRect() const
{
    float const right = width() - kButtonsRight + kButtonsPadding;
    float const left = right - kButtonsPadding * 2.0F - 3.0F * kButtonWidth;
    return D2D1::RectF(left, kButtonsTop, right, kButtonsTop + kButtonHeight);
}

void Window::drawCaptionButtons(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();
    float const w = width();
    float const y = kButtonsTop + kButtonHeight / 2.0F;

    context->SetTransform(D2D1::Matrix3x2F::Identity());
    int const hits[3] = { HTMINBUTTON, HTMAXBUTTON, HTCLOSE };
    for (int i = 0; i < 3; ++i)
    {
        float const left = w - kButtonsRight - (3 - i) * kButtonWidth;
        bool const hover = captionHover_ == hits[i];
        if (hover)
        {
            D2D1_COLOR_F const fill = hits[i] == HTCLOSE ? hex(0xc42b1c) : withAlpha(colors.bright, 0.10F);
            float const insetX = (kButtonWidth - kButtonHoverSize) / 2.0F;
            float const insetY = (kButtonHeight - kButtonHoverSize) / 2.0F;
            float const radius = kButtonHoverSize / 2.0F;
            context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left + insetX, kButtonsTop + insetY, left + kButtonWidth - insetX, kButtonsTop + kButtonHeight - insetY), radius, radius), theme.brush(fill));
        }
        float const cx = left + kButtonWidth / 2.0F;
        auto* brush = theme.brush(hover && hits[i] == HTCLOSE ? hex(0xffffff) : colors.bright);
        if (hits[i] == HTCLOSE)
        {
            context->DrawLine(D2D1::Point2F(cx - 5.0F, y - 5.0F), D2D1::Point2F(cx + 5.0F, y + 5.0F), brush, 1.1F);
            context->DrawLine(D2D1::Point2F(cx + 5.0F, y - 5.0F), D2D1::Point2F(cx - 5.0F, y + 5.0F), brush, 1.1F);
        }
        else if (hits[i] == HTMAXBUTTON)
        {
            context->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 5.0F, y - 5.0F, cx + 5.0F, y + 5.0F), 1.5F, 1.5F), brush, 1.1F);
        }
        else
        {
            context->DrawLine(D2D1::Point2F(cx - 5.0F, y), D2D1::Point2F(cx + 5.0F, y), brush, 1.1F);
        }
    }
}

void Window::setCaptionHover(int hit)
{
    if (hit != captionHover_)
    {
        captionHover_ = hit;
        invalidate(captionRect());
    }
}

void Window::paintFrame()
{
    if (root_ == nullptr)
    {
        return;
    }

    D2D1_RECT_F const everything = D2D1::RectF(0.0F, 0.0F, width(), height());
    bool const whole = std::exchange(wholeFrame_, false);
    bool const partial = sceneDirty_ && !whole;
    D2D1_RECT_F const changed = dirtyRect_;
    if (sceneDirty_)
    {
        ID2D1DeviceContext* context = renderer_.beginScene();
        D2D1_RECT_F const clip = D2D1::RectF(
            std::max(0.0F, dirtyRect_.left),
            std::max(0.0F, dirtyRect_.top),
            std::min(everything.right, dirtyRect_.right),
            std::min(everything.bottom, dirtyRect_.bottom));
        context->SetTransform(D2D1::Matrix3x2F::Identity());
        context->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        root_->paint(context, D2D1::Point2F(0.0F, 0.0F), clip, false, false);
        context->SetTransform(D2D1::Matrix3x2F::Identity());
        context->PopAxisAlignedClip();
        renderer_.endScene();
        sceneDirty_ = false;
    }

    std::vector<GlassShape> baseGlass;
    root_->collectGlass(baseGlass, D2D1::Point2F(0.0F, 0.0F));
    std::vector<GlassShape> panelGlass;
    if (panel_ != nullptr)
    {
        auto const& frame = panel_->frame();
        panel_->collectGlass(panelGlass, D2D1::Point2F(frame.left, frame.top));
    }

    auto const drawPopup = [&](ID2D1DeviceContext* context, D2D1_RECT_F clip)
    {
        if (popup_ != nullptr)
        {
            auto const& frame = popup_->frame();
            popup_->paint(context, D2D1::Point2F(frame.left, frame.top), clip, true, true);
            context->SetTransform(D2D1::Matrix3x2F::Identity());
        }
    };

    auto const baseOverlay = [&](ID2D1DeviceContext* context, D2D1_RECT_F clip)
    {
        root_->paint(context, D2D1::Point2F(0.0F, 0.0F), clip, true, false);
        drawCaptionButtons(context);
        context->SetTransform(D2D1::Matrix3x2F::Identity());
        if (panel_ == nullptr)
        {
            drawPopup(context, clip);
        }
    };

    std::vector<GlassLayer> layers{ GlassLayer{ baseGlass, baseOverlay } };
    if (panel_ != nullptr)
    {
        layers.push_back(GlassLayer{ panelGlass,
            [&](ID2D1DeviceContext* context, D2D1_RECT_F clip)
            {
                auto const& frame = panel_->frame();
                panel_->paint(context, D2D1::Point2F(frame.left, frame.top), clip, true, true);
                context->SetTransform(D2D1::Matrix3x2F::Identity());
                drawPopup(context, clip);
            } });
    }
    renderer_.present(layers, partial ? &changed : nullptr);
    ++framesPainted_;
}

void Window::runTimers()
{
    ULONGLONG const now = GetTickCount64();
    std::vector<int> due;
    for (auto const& [id, timer] : timers_)
    {
        if (timer.due <= now)
        {
            due.push_back(id);
        }
    }
    for (int const id : due)
    {
        auto found = timers_.find(id);
        if (found == timers_.end())
        {
            continue;
        }
        auto callback = found->second.callback;
        if (found->second.interval > 0)
        {
            found->second.due = now + found->second.interval;
        }
        else
        {
            timers_.erase(found);
        }
        callback();
    }
}

void Window::runAnimations()
{
    if (animations_.empty())
    {
        return;
    }
    ULONGLONG const now = GetTickCount64();
    std::vector<int> ids;
    for (auto const& [id, animation] : animations_)
    {
        ids.push_back(id);
    }
    for (int const id : ids)
    {
        auto found = animations_.find(id);
        if (found == animations_.end())
        {
            continue;
        }
        float const t = std::min(1.0F, static_cast<float>(now - found->second.start) / found->second.duration);
        found->second.step(t);
        if (t >= 1.0F)
        {
            found = animations_.find(id);
            if (found != animations_.end())
            {
                auto done = std::move(found->second.done);
                animations_.erase(found);
                if (done)
                {
                    done();
                }
            }
        }
    }
    invalidateFrame();
}

DWORD Window::nextWake() const
{
    // A hidden window never paints, so waiting on dirty_ then would spin instead of sleeping.
    if ((dirty_ && visible()) || !animations_.empty())
    {
        return 0;
    }
    ULONGLONG const now = GetTickCount64();
    ULONGLONG next = animations_.empty() ? MAXULONGLONG : now + kHiddenAnimationWakeMs;
    for (auto const& [id, timer] : timers_)
    {
        next = std::min(next, timer.due);
    }
    if (next == MAXULONGLONG)
    {
        return INFINITE;
    }
    return next <= now ? 0 : static_cast<DWORD>(next - now);
}

int Window::run()
{
    if (!sized_)
    {
        sized_ = true;
        ShowWindow(window_, SW_SHOW);
    }

    for (;;)
    {
        // Sleeping here is the whole idle strategy: nothing wakes the app unless a timer is due.
        MsgWaitForMultipleObjectsEx(0, nullptr, nextWake(), QS_ALLINPUT, MWMO_INPUTAVAILABLE);

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                // Views cancel their timers as they go, which needs this object still whole.
                popup_.reset();
                panel_.reset();
                root_.reset();
                retired_.clear();
                return static_cast<int>(message.wParam);
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        runTimers();
        runAnimations();
        retired_.clear();
        if (dirty_ && visible())
        {
            dirty_ = false;
            paintFrame();
        }
    }
}

LRESULT Window::hitTest(POINT screen)
{
    // A popup behaves like a menu: the whole window is one surface whose every click closes it.
    if (popup_ != nullptr)
    {
        return HTCLIENT;
    }

    RECT rect{};
    GetWindowRect(window_, &rect);
    float const scale = renderer_.dpi() / 96.0F;
    int const edge = static_cast<int>(8.0F * scale);
    bool const zoomed = IsZoomed(window_) != FALSE;

    if (!zoomed)
    {
        bool const left = screen.x < rect.left + edge;
        bool const right = screen.x >= rect.right - edge;
        bool const top = screen.y < rect.top + edge;
        bool const bottom = screen.y >= rect.bottom - edge;
        if (top && left)
        {
            return HTTOPLEFT;
        }
        if (top && right)
        {
            return HTTOPRIGHT;
        }
        if (bottom && left)
        {
            return HTBOTTOMLEFT;
        }
        if (bottom && right)
        {
            return HTBOTTOMRIGHT;
        }
        if (top)
        {
            return HTTOP;
        }
        if (left)
        {
            return HTLEFT;
        }
        if (right)
        {
            return HTRIGHT;
        }
        if (bottom)
        {
            return HTBOTTOM;
        }
    }

    POINT client = screen;
    ScreenToClient(window_, &client);
    D2D1_POINT_2F const point = dips(client);
    float const w = width();
    float const fromRight = w - kButtonsRight - point.x;
    // The pill sits inside the corner, but a flick to the corner should still reach Close, so the
    // buttons own the strip above them and everything to their right.
    if (point.y < kButtonsTop + kButtonHeight && fromRight < 3.0F * kButtonWidth)
    {
        if (fromRight < kButtonWidth)
        {
            return HTCLOSE;
        }
        return fromRight < 2.0F * kButtonWidth ? HTMAXBUTTON : HTMINBUTTON;
    }

    if (panel_ != nullptr)
    {
        return HTCLIENT;
    }
    if (point.y < kCaptionHeight && root_ != nullptr && root_->hitTest(point) == nullptr)
    {
        return HTCAPTION;
    }
    return HTCLIENT;
}

void Window::updateHover(View* view, D2D1_POINT_2F windowPoint)
{
    if (view == hovered_)
    {
        return;
    }
    if (hovered_ != nullptr)
    {
        MouseEvent leave;
        leave.kind = MouseKind::Leave;
        leave.point = toLocal(hovered_, windowPoint);
        hovered_->onMouse(leave);
    }
    hovered_ = view;
    if (hovered_ != nullptr)
    {
        MouseEvent enter;
        enter.kind = MouseKind::Enter;
        enter.point = toLocal(hovered_, windowPoint);
        hovered_->onMouse(enter);
    }
}

View* Window::viewAt(D2D1_POINT_2F windowPoint) const
{
    View* top = popup_ != nullptr ? popup_.get() : panel_ != nullptr ? panel_.get() : root_.get();
    if (top == nullptr)
    {
        return nullptr;
    }
    auto const& frame = top->frame();
    return top->hitTest(D2D1::Point2F(windowPoint.x - frame.left, windowPoint.y - frame.top));
}

void Window::mouse(MouseKind kind, LPARAM lparam, float wheel)
{
    POINT client{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    if (kind == MouseKind::Wheel)
    {
        ScreenToClient(window_, &client);
    }
    D2D1_POINT_2F const point = dips(client);
    bool const press = kind == MouseKind::Down || kind == MouseKind::DoubleClick;

    View* target = pressed_ != nullptr && kind != MouseKind::Wheel ? pressed_ : viewAt(point);

    if (popup_ != nullptr && pressed_ == nullptr && target == nullptr)
    {
        if (press || kind == MouseKind::RightClick)
        {
            setPopup(nullptr);
        }
        updateHover(nullptr, point);
        return;
    }

    if (pressed_ == nullptr)
    {
        updateHover(target, point);
    }

    if (press)
    {
        pressed_ = target;
        SetCapture(window_);
        if (popup_ == nullptr && (target == nullptr || target->takesFocus()))
        {
            focus(target);
        }
    }

    MouseEvent event;
    event.kind = kind;
    event.wheelDelta = wheel;
    event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    event.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    for (View* view = target; view != nullptr; view = view->parent())
    {
        event.point = toLocal(view, point);
        if (view->onMouse(event))
        {
            break;
        }
    }

    if (kind == MouseKind::Up)
    {
        pressed_ = nullptr;
        ReleaseCapture();
        updateHover(viewAt(point), point);
    }

    // A click may have closed the view under the pointer, so ask what's there now, not `target`.
    if (kind == MouseKind::Move || kind == MouseKind::Up)
    {
        View* under = pressed_ != nullptr ? pressed_ : hovered_;
        setCursor(under != nullptr ? under->cursor() : nullptr);
    }
}

LRESULT CALLBACK Window::proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    Window& self = get();
    if (self.window_ == nullptr)
    {
        self.window_ = window;
    }
    return self.handle(message, wparam, lparam);
}

LRESULT Window::handle(UINT message, WPARAM wparam, LPARAM lparam)
{
    if (onMessage && onMessage(message, wparam, lparam))
    {
        return 0;
    }

    switch (message)
    {
    case WM_NCCALCSIZE:
        if (wparam != 0)
        {
            if (IsZoomed(window_))
            {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                UINT const dpi = GetDpiForWindow(window_);
                int const frame = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                InflateRect(&params->rgrc[0], -frame, -frame);
            }
            return 0;
        }
        break;

    case WM_GETMINMAXINFO:
    {
        // Below this the header and list stop fitting, and panels overflow.
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = MulDiv(kMinWidth, GetDpiForWindow(window_), 96);
        limits->ptMinTrackSize.y = MulDiv(kMinHeight, GetDpiForWindow(window_), 96);
        return 0;
    }

    case WM_NCHITTEST:
    {
        if (!ready_)
        {
            break;
        }
        LRESULT const hit = hitTest(POINT{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) });
        setCaptionHover(hit == HTCLOSE || hit == HTMAXBUTTON || hit == HTMINBUTTON ? static_cast<int>(hit) : 0);
        return hit;
    }

    case WM_NCMOUSELEAVE:
        setCaptionHover(0);
        tracking_ = false;
        break;

    case WM_NCMOUSEMOVE:
        if (!tracking_)
        {
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE | TME_NONCLIENT, window_, 0 };
            TrackMouseEvent(&track);
            tracking_ = true;
        }
        break;

    case WM_NCLBUTTONDOWN:
        if (wparam == HTCLOSE || wparam == HTMAXBUTTON || wparam == HTMINBUTTON)
        {
            return 0;
        }
        break;

    case WM_NCLBUTTONUP:
        if (wparam == HTCLOSE)
        {
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wparam == HTMAXBUTTON)
        {
            ShowWindow(window_, IsZoomed(window_) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        if (wparam == HTMINBUTTON)
        {
            ShowWindow(window_, SW_MINIMIZE);
            return 0;
        }
        break;

    case WM_SIZE:
        if (!ready_)
        {
            return 0;
        }
        if (wparam == SIZE_MINIMIZED)
        {
            releaseGraphics();
            return 0;
        }
        released_ = false;
        renderer_.resize(LOWORD(lparam), HIWORD(lparam), static_cast<float>(GetDpiForWindow(window_)));
        if (root_ != nullptr)
        {
            root_->setFrame(D2D1::RectF(0.0F, 0.0F, width(), height()));
        }
        setPopup(nullptr);
        if (panel_ != nullptr)
        {
            panel_->layout();
        }
        invalidateAll();
        // Live resizing runs inside DefWindowProc's own loop, so paint from here.
        dirty_ = false;
        paintFrame();
        return 0;

    case WM_DPICHANGED:
    {
        auto const* suggested = reinterpret_cast<RECT const*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        BeginPaint(window_, &ps);
        EndPaint(window_, &ps);
        invalidateFrame();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SHOWWINDOW:
        if (wparam != 0 && onShown)
        {
            onShown();
        }
        break;

    case WM_MOUSEMOVE:
        if (!tracking_)
        {
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window_, 0 };
            TrackMouseEvent(&track);
            tracking_ = true;
        }
        setCaptionHover(0);
        mouse(MouseKind::Move, lparam);
        return 0;

    case WM_MOUSELEAVE:
        tracking_ = false;
        updateHover(nullptr, D2D1::Point2F(-1.0F, -1.0F));
        return 0;

    case WM_SETCURSOR:
        // The class cursor would flash back to an arrow before every move over a text field.
        if (LOWORD(lparam) == HTCLIENT)
        {
            View* under = pressed_ != nullptr ? pressed_ : hovered_;
            setCursor(under != nullptr ? under->cursor() : nullptr);
            return TRUE;
        }
        break;

    case WM_CAPTURECHANGED:
        pressed_ = nullptr;
        return 0;

    case WM_ACTIVATE:
    {
        bool const active = LOWORD(wparam) != WA_INACTIVE;
        if (!active)
        {
            setPopup(nullptr);
        }
        if (focused_ != nullptr)
        {
            focused_->onWindowActive(active);
        }
        break;
    }

    case WM_IME_STARTCOMPOSITION:
        if (focused_ != nullptr)
        {
            focused_->onImeStart();
        }
        break;

    case WM_LBUTTONDOWN:
        mouse(MouseKind::Down, lparam);
        return 0;

    case WM_LBUTTONUP:
        mouse(MouseKind::Up, lparam);
        return 0;

    case WM_LBUTTONDBLCLK:
        mouse(MouseKind::DoubleClick, lparam);
        return 0;

    case WM_RBUTTONUP:
        mouse(MouseKind::RightClick, lparam);
        return 0;

    case WM_MOUSEWHEEL:
        mouse(MouseKind::Wheel, lparam, GET_WHEEL_DELTA_WPARAM(wparam) / static_cast<float>(WHEEL_DELTA));
        return 0;

    case WM_KEYDOWN:
    case WM_KEYUP:
        if (popup_ != nullptr)
        {
            popup_->onKey(static_cast<UINT>(wparam), message == WM_KEYDOWN);
            return 0;
        }
        for (View* view = focused_; view != nullptr; view = view->parent())
        {
            if (view->onKey(static_cast<UINT>(wparam), message == WM_KEYDOWN))
            {
                return 0;
            }
        }
        if (panel_ != nullptr && panel_->onKey(static_cast<UINT>(wparam), message == WM_KEYDOWN))
        {
            return 0;
        }
        if (focused_ == nullptr && panel_ == nullptr && root_ != nullptr)
        {
            root_->onKey(static_cast<UINT>(wparam), message == WM_KEYDOWN);
        }
        return 0;

    case WM_CHAR:
        if (popup_ == nullptr && focused_ != nullptr)
        {
            focused_->onChar(static_cast<wchar_t>(wparam));
        }
        return 0;

    case WM_SETTINGCHANGE:
        if (lparam != 0 && lstrcmpW(reinterpret_cast<wchar_t const*>(lparam), L"ImmersiveColorSet") == 0)
        {
            Theme::get().systemThemeChanged();
            BOOL dark = Theme::get().dark();
            DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            invalidateAll();
        }
        break;

    case WM_DROPFILES:
    {
        auto drop = reinterpret_cast<HDROP>(wparam);
        std::vector<std::wstring> files;
        UINT const count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i)
        {
            std::wstring path(DragQueryFileW(drop, i, nullptr, 0) + 1, L'\0');
            DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size()));
            path.pop_back();
            files.push_back(std::move(path));
        }
        DragFinish(drop);
        if (onFilesDropped)
        {
            onFilesDropped(files);
        }
        return 0;
    }

    case WM_COPYDATA:
    {
        auto const* data = reinterpret_cast<COPYDATASTRUCT const*>(lparam);
        if (data->dwData == kOpenRequest && onOpenRequest)
        {
            std::wstring text;
            if (data->lpData != nullptr)
            {
                text.assign(static_cast<wchar_t const*>(data->lpData), data->cbData / sizeof(wchar_t));
            }
            onOpenRequest(text);
        }
        else if (data->dwData == kQuitRequest)
        {
            quit();
        }
        return TRUE;
    }

    case WM_CLOSE:
        if (onCloseRequest && !onCloseRequest())
        {
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

} // namespace ui
