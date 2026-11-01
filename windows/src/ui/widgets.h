#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ui/theme.h"
#include "ui/view.h"
#include "ui/window.h"

namespace ui
{

enum class ColorRole
{
    Foreground,
    Bright,
    Muted, // the Mac's tertiary label, for placeholders and hints
    Accent,
    Seed,
    Danger,
};

D2D1_COLOR_F roleColor(ColorRole role);

/// Runs one animation at a time and cancels it when its owner goes away.
class Animator
{
public:
    Animator() = default;
    Animator(Animator const&) = delete;
    Animator& operator=(Animator const&) = delete;
    ~Animator();

    void run(DWORD durationMs, std::function<void(float)> step, std::function<void()> done = {});
    void cancel();
    bool running() const { return id_ != 0; }

private:
    int id_ = 0;
};

class Label : public View
{
public:
    Label() = default;
    explicit Label(std::wstring text, TextStyle style = {}, ColorRole role = ColorRole::Foreground);

    std::wstring const& text() const { return text_; }
    /// Repaints only if the text really changed, so calling it from a stats tick is free.
    void setText(std::wstring_view text);
    void setTextStyle(TextStyle style);
    void setRole(ColorRole role);
    void setAlignment(DWRITE_TEXT_ALIGNMENT alignment);
    void setVerticalAlignment(DWRITE_PARAGRAPH_ALIGNMENT alignment);
    void setTrim(Trim trim);

    float preferredWidth() const;
    float preferredHeight() const;

    void draw(ID2D1DeviceContext* context) override;

private:
    std::wstring text_;
    TextStyle style_;
    ColorRole role_ = ColorRole::Foreground;
    DWRITE_TEXT_ALIGNMENT alignment_ = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT vertical_ = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    Trim trim_ = Trim::End;
};

/// Hover, press and enabled tracking for everything clickable. Leaves keyboard focus alone.
class Control : public View
{
public:
    Control();

    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled);

    bool onMouse(MouseEvent const& event) override;

protected:
    bool isHovered() const { return hovered_; }
    /// Held down with the pointer still inside.
    bool isPressed() const { return pressed_; }
    /// A full click landed. Fire callbacks last: they may close the panel this control lives in.
    virtual void activate() {}

private:
    bool enabled_ = true;
    bool hovered_ = false;
    bool pressed_ = false;
    bool tracking_ = false;
};

enum class ButtonStyle
{
    Glass,  // subtle tinted fill and hairline, like Cancel in the add panel
    Accent, // filled accent, like Get
    Plain,  // nothing until hovered
};

class Button : public Control
{
public:
    static constexpr float kHeight = 28.0F;
    static constexpr float kLargeHeight = 32.0F;
    static constexpr float kSmallHeight = 24.0F;

    Button() = default;
    explicit Button(std::wstring title, ButtonStyle style = ButtonStyle::Glass, std::function<void()> onClick = {});

    void setTitle(std::wstring_view title);
    void setStyle(ButtonStyle style);
    void setIcon(std::wstring name);
    /// Fully round ends instead of the small radius. Accent buttons start out capsule.
    void setCapsule(bool capsule);

    float preferredWidth() const;

    std::function<void()> onClick;

    void draw(ID2D1DeviceContext* context) override;

protected:
    void activate() override;

private:
    float titleWidth() const;

    std::wstring title_;
    std::wstring icon_;
    ButtonStyle style_ = ButtonStyle::Glass;
    bool capsule_ = false;
    mutable float titleWidth_ = -1.0F;
};

class IconButton : public Control
{
public:
    static constexpr float kSize = 30.0F;

    IconButton() = default;
    explicit IconButton(std::wstring name, std::function<void()> onClick = {});

    void setIcon(std::wstring name);
    void setIconSize(float size);
    void setRole(ColorRole role);

    std::function<void()> onClick;

    void draw(ID2D1DeviceContext* context) override;

protected:
    void activate() override;

private:
    std::wstring name_;
    float iconSize_ = 20.0F;
    ColorRole role_ = ColorRole::Foreground;
};

class Switch : public Control
{
public:
    static constexpr float kWidth = 60.0F;
    static constexpr float kHeight = 28.0F;

    bool on() const { return on_; }
    void setOn(bool on, bool animated = false);
    /// 0 off, 1 on, in between while sliding.
    float thumbPosition() const { return position_; }

    std::function<void(bool)> onChange;

    void draw(ID2D1DeviceContext* context) override;

protected:
    void activate() override;

private:
    bool on_ = false;
    float position_ = 0.0F;
    Animator slide_;
};

enum class CheckState
{
    Off,
    On,
    Mixed, // some of a folder's files are wanted
};

class Checkbox : public Control
{
public:
    static constexpr float kSize = 18.0F;

    CheckState state() const { return state_; }
    void setState(CheckState state);
    /// `Mixed` is only ever set from code: a click turns it on, then off.
    std::function<void(CheckState)> onChange;

    /// Draws the box alone, for list delegates: `box` is kSize square in the caller's coordinates.
    static void paint(ID2D1DeviceContext* context, D2D1_RECT_F box, CheckState state, bool enabled = true, bool hovered = false, bool pressed = false);

    void draw(ID2D1DeviceContext* context) override;

protected:
    void activate() override;

private:
    CheckState state_ = CheckState::Off;
};

struct Pill
{
    std::wstring label;
    int count = -1; // shown after the label when not negative
};

/// The segmented control over the transfer list and in the details window.
class PillBar : public View
{
public:
    static constexpr float kHeight = 36.0F;

    PillBar();

    void setPills(std::vector<Pill> pills);
    void setCount(int index, int count);
    int selected() const { return selected_; }
    void setSelected(int index, bool animated = false);

    float preferredWidth() const;
    D2D1_RECT_F pillRect(int index) const;

    std::function<void(int)> onChange;

    void draw(ID2D1DeviceContext* context) override;
    bool onMouse(MouseEvent const& event) override;

private:
    struct Item
    {
        Pill pill;
        std::wstring text;
        float width = 0.0F;
    };

    int pillAt(D2D1_POINT_2F point) const;
    void measureItem(Item& item) const;
    void slideTo(int index, bool animated);

    std::vector<Item> items_;
    int selected_ = 0;
    int hovered_ = -1;
    int pressed_ = -1;
    float highlightLeft_ = 0.0F;
    float highlightRight_ = 0.0F;
    Animator slide_;
};

enum class FieldStyle
{
    Rounded, // field colour and hairline with a small radius, for values in rows
    Capsule, // the same, fully round, for filters
    Plain,   // only text, for the search panel's big query
};

/// A single-line editor drawn with DirectWrite. The system's own floating window handles composition.
class TextField : public View
{
public:
    static constexpr float kHeight = 30.0F;

    TextField();
    ~TextField() override;

    std::wstring const& text() const { return text_; }
    /// Replaces the text without firing onChange, and forgets undo history.
    void setText(std::wstring_view text);
    void setPlaceholder(std::wstring placeholder);
    void setTextStyle(TextStyle style);
    void setFieldStyle(FieldStyle style);
    void setIcon(std::wstring name);
    /// Leading or trailing. Trailing is for numbers in rows.
    void setAlignment(DWRITE_TEXT_ALIGNMENT alignment);
    void setEnabled(bool enabled);
    /// Drops typed or pasted characters this returns false for, such as non-digits.
    void setFilter(std::function<bool(wchar_t)> filter);

    void selectAll();
    void setSelection(int anchor, int caret);
    int caret() const { return caret_; }
    int anchor() const { return anchor_; }
    float scrollX() const { return scrollX_; }

    std::function<void(std::wstring const&)> onChange;
    /// Return. Left unset, the key travels up to the panel instead.
    std::function<void()> onSubmit;
    /// Esc. Left unset, the key travels up to the panel, which closes it.
    std::function<void()> onCancel;
    /// Focus went elsewhere, which is when a rename commits.
    std::function<void()> onBlur;

    /// What onKey does, with the modifiers passed in. Up and Down fall through for result lists.
    bool handleKey(UINT key, bool shift, bool control);

    void layout() override;
    void draw(ID2D1DeviceContext* context) override;
    bool onMouse(MouseEvent const& event) override;
    bool onKey(UINT key, bool down) override;
    bool onChar(wchar_t character) override;
    void onFocus(bool focused) override;
    void onWindowActive(bool active) override;
    void onImeStart() override;
    HCURSOR cursor() const override;

private:
    enum class EditKind
    {
        None,
        Typing,
        Deleting,
    };
    struct Snapshot
    {
        std::wstring text;
        int caret = 0;
        int anchor = 0;
    };

    void ensureLayout();
    D2D1_RECT_F innerRect() const;
    float textOrigin();
    float caretX(int position);
    int positionAt(float x);
    int nextCluster(int position);
    int previousCluster(int position);
    int previousCodePoint(int position) const;
    int wordStart(int position) const;
    int wordEnd(int position) const;
    void wordAround(int position, int& start, int& end) const;

    void moveCaret(int position, bool extend);
    void caretChanged();
    void revealCaret();
    void replaceRange(int start, int end, std::wstring_view with, EditKind kind);
    void remember(EditKind kind);
    void restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to);
    std::wstring sanitised(std::wstring_view text) const;
    void paste();
    void copy() const;

    void startBlink();
    void stopBlink();
    void positionIme();
    void invalidateCaret();

    std::wstring text_;
    std::wstring placeholder_;
    std::wstring icon_;
    TextStyle style_;
    FieldStyle fieldStyle_ = FieldStyle::Rounded;
    DWRITE_TEXT_ALIGNMENT alignment_ = DWRITE_TEXT_ALIGNMENT_LEADING;
    std::function<bool(wchar_t)> filter_;
    bool enabled_ = true;

    int caret_ = 0;
    int anchor_ = 0;
    float scrollX_ = 0.0F;
    wchar_t pendingHigh_ = 0;

    ComPtr<IDWriteTextLayout> layout_;
    bool layoutDirty_ = true;
    float textWidth_ = 0.0F;
    float lineHeight_ = 0.0F;
    float baseline_ = 0.0F;

    bool focused_ = false;
    bool caretOn_ = false;
    int blinkTimer_ = 0;
    bool dragging_ = false;
    bool wordDrag_ = false;
    int wordDragStart_ = 0;
    int wordDragEnd_ = 0;

    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    EditKind lastEdit_ = EditKind::None;
};

/// The overlay scrollbar. It fades in on scroll or hover and out again, and is off when not needed.
class ScrollBar : public View
{
public:
    ScrollBar();
    ~ScrollBar() override;

    /// Called with the offset a drag asks for.
    std::function<void(float)> onScroll;

    void setRange(float offset, float viewport, float content);
    /// Shows the bar and starts the countdown to hiding it again.
    void flash();

    void draw(ID2D1DeviceContext* context) override;
    bool onMouse(MouseEvent const& event) override;
    View* hitTest(D2D1_POINT_2F point) override;

private:
    D2D1_RECT_F thumbRect() const;
    void invalidateThumb();
    bool needed() const { return content_ > viewport_ + 0.5F; }
    void fadeTo(float alpha, DWORD durationMs);

    float offset_ = 0.0F;
    float viewport_ = 0.0F;
    float content_ = 0.0F;
    float alpha_ = 0.0F;
    float fadeTarget_ = 0.0F;
    bool hovered_ = false;
    bool dragging_ = false;
    float dragGrab_ = 0.0F;
    int hideTimer_ = 0;
    Animator fade_;
};

/// What ScrollView and ListView share: an offset, eased wheel scrolling and the bar.
class Scrollable : public View
{
public:
    float scrollOffset() const { return offset_; }
    float maxScroll() const;
    void scrollTo(float offset, bool animated = false);

    void layout() override;
    bool onMouse(MouseEvent const& event) override;

protected:
    Scrollable();

    /// Adds the bar, which has to be the last child so it stays on top.
    void attachBar();
    void setContentHeight(float height);
    float contentHeight() const { return contentHeight_; }
    virtual void scrolled() { invalidate(); }

private:
    void apply(float offset);

    ScrollBar* bar_ = nullptr;
    float offset_ = 0.0F;
    float target_ = 0.0F;
    float contentHeight_ = 0.0F;
    Animator ease_;
};

/// Clips and scrolls one content view. Add children to content() and set its height.
class ScrollView : public Scrollable
{
public:
    ScrollView();

    View* content() const { return content_; }
    /// Swaps the content for your own view type.
    template<typename T, typename... Args>
    T* setContent(Args&&... args)
    {
        auto view = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = view.get();
        replaceContent(std::move(view));
        return raw;
    }
    void setContentHeight(float height);

    void layout() override;

protected:
    void scrolled() override;

private:
    class Viewport;

    void replaceContent(std::unique_ptr<View> view);

    Viewport* viewport_ = nullptr;
    View* content_ = nullptr;
};

struct RowState
{
    bool hover = false;
    bool selected = false;
};

/// Virtualised rows of a fixed height. Selection and hairline match ThemedRowView; the delegate
/// draws the rest. `rect` is in the list's own coordinates and already scrolled.
class ListView : public Scrollable
{
public:
    using DrawRow = std::function<void(ID2D1DeviceContext*, int, D2D1_RECT_F, RowState)>;

    ListView();

    void setRowCount(int count);
    int rowCount() const { return rowCount_; }
    void setRowHeight(float height);
    float rowHeight() const { return rowHeight_; }
    /// Same meaning as ThemedRowView. A negative hairline inset turns the hairline off.
    void setRowInsets(float hairlineInset, float selectionInset);

    /// Repaints one row, and only that row.
    void reloadRow(int index);
    void reloadData() { invalidate(); }

    bool isSelected(int index) const;
    int selectedCount() const { return selectedCount_; }
    std::vector<int> selectedRows() const;
    void selectRow(int index, bool extend = false);
    void setRowSelected(int index, bool selected);
    void selectAll();
    void clearSelection();
    /// Where keyboard movement continues from, or -1.
    int cursorRow() const { return cursor_; }

    int rowAt(D2D1_POINT_2F point) const;
    D2D1_RECT_F rowRect(int index) const;
    void revealRow(int index);

    DrawRow drawRow;
    std::function<void()> onSelectionChanged;
    std::function<void(int)> onDoubleClick;
    /// Gets the point in window coordinates, ready for PopupMenu::show.
    std::function<void(int, D2D1_POINT_2F)> onRightClick;
    std::function<void()> onDelete;
    std::function<void()> onEnter;

    bool handleKey(UINT key, bool shift, bool control);

    void draw(ID2D1DeviceContext* context) override;
    bool onMouse(MouseEvent const& event) override;
    bool onKey(UINT key, bool down) override;

protected:
    void scrolled() override;

private:
    void updateHover(int row);
    void selectRange(int from, int to);
    void moveCursor(int row, bool extend);
    void selectionChanged();
    void updateExtent();

    int rowCount_ = 0;
    float rowHeight_ = 44.0F;
    float hairlineInset_ = 24.0F;
    float selectionInset_ = 12.0F;

    std::vector<bool> selected_;
    int selectedCount_ = 0;
    int anchor_ = -1;
    int cursor_ = -1;
    int hover_ = -1;
    bool pointerInside_ = false;
    D2D1_POINT_2F pointer_{};
};

struct MenuItem
{
    std::wstring title;
    std::function<void()> action;
    bool checked = false;
    bool enabled = true;
    bool separator = false;
    std::wstring icon;
    std::vector<MenuItem> submenu;

    static MenuItem line();
};

/// The context menu. Solid, so it works over the window and over a panel alike.
class PopupMenu : public View
{
public:
    PopupMenu(std::vector<MenuItem> items, D2D1_POINT_2F anchor, float minWidth);

    /// Opens below and to the right of `anchor` (window coordinates), flipping to stay inside the window.
    static PopupMenu* show(std::vector<MenuItem> items, D2D1_POINT_2F anchor, float minWidth = 0.0F);

    int highlighted() const { return highlight_; }
    PopupMenu* submenu() const { return sub_; }

    void draw(ID2D1DeviceContext* context) override;
    bool onMouse(MouseEvent const& event) override;
    bool onKey(UINT key, bool down) override;
    View* hitTest(D2D1_POINT_2F point) override;

private:
    float rowTop(int index) const;
    float rowHeight(int index) const;
    int rowAt(D2D1_POINT_2F point) const;
    bool selectable(int index) const;
    void highlight(int index);
    void step(int direction);
    void activate(int index);
    void openSubmenu(int index, bool highlightFirst);
    void closeSubmenu();
    void buildShadow(ID2D1DeviceContext* context);

    std::vector<MenuItem> items_;
    D2D1_POINT_2F anchor_{};
    bool placed_ = false;
    int highlight_ = -1;
    int subIndex_ = -1;
    bool hasLeadingColumn_ = false;
    PopupMenu* sub_ = nullptr;
    ComPtr<ID2D1Bitmap> shadow_;
};

/// A compact selector, like "All sources" in the search panel.
class Dropdown : public Control
{
public:
    static constexpr float kHeight = 28.0F;

    Dropdown() = default;
    explicit Dropdown(std::vector<std::wstring> items, int selected = 0);

    void setItems(std::vector<std::wstring> items);
    int selected() const { return selected_; }
    void setSelected(int index);
    std::wstring const& title() const;

    float preferredWidth() const;

    std::function<void(int)> onChange;

    void draw(ID2D1DeviceContext* context) override;

protected:
    void activate() override;

private:
    std::vector<std::wstring> items_;
    int selected_ = 0;
};

/// A floating sheet of glass that captures input for everything below it. Subclass, add children
/// and position them in layoutContent().
class Panel : public View
{
public:
    static constexpr DWORD kAnimationMs = 180;
    static constexpr float kTopMargin = 72.0F;

    Panel(float panelWidth, float panelHeight);

    /// Hands the panel to the window and plays it in. Replaces any open panel.
    static void present(std::unique_ptr<Panel> panel);

    /// Builds a panel and presents it, returning it for wiring up.
    template<typename T, typename... Args>
    static T* open(Args&&... args)
    {
        auto panel = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = panel.get();
        present(std::move(panel));
        return raw;
    }

    /// Plays the panel out, then destroys it, so don't touch it after the close finishes.
    void close();
    bool closing() const { return closing_; }

    void setPanelSize(float panelWidth, float panelHeight);
    void setClosesOnOutsideClick(bool closes) { closesOnOutsideClick_ = closes; }

    /// Runs once the panel has finished closing.
    std::function<void()> onClosed;

    /// Runs on Esc and on an outside click. Returning false keeps the panel open.
    std::function<bool()> shouldClose;

    void layout() final;
    bool onMouse(MouseEvent const& event) override;
    bool onKey(UINT key, bool down) override;
    View* hitTest(D2D1_POINT_2F point) override;

protected:
    virtual void layoutContent() {}
    void adjustGlass(GlassShape& shape) const override;

private:
    void animateTo(float target, std::function<void()> done);
    void requestClose();

    float panelWidth_ = 0.0F;
    float panelHeight_ = 0.0F;
    float progress_ = 0.0F;
    bool closesOnOutsideClick_ = true;
    bool closing_ = false;
    Animator animation_;
};

} // namespace ui
