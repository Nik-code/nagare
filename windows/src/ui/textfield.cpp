#include <windows.h>

#include <imm.h>

#include <cstring>
#include <cwctype>

#include "ui/widgets.h"
#include "ui/widgets_internal.h"

namespace ui
{

namespace
{

using namespace detail;

constexpr float kLayoutSize = 1000000.0F;
constexpr float kRoundedRadius = 8.0F;
constexpr float kPadding = 10.0F;
constexpr float kCapsulePadding = 14.0F;
constexpr float kIconSize = 16.0F;
constexpr float kIconGap = 6.0F;
constexpr float kCaretWidth = 1.5F;
constexpr float kCaretMargin = 2.0F;
constexpr size_t kUndoLimit = 64;

enum class CharClass
{
    Space,
    Punctuation,
    Word,
    Han,
    Hiragana,
    Katakana,
};

CharClass classify(wchar_t c)
{
    if (c >= 0x3040 && c <= 0x309F)
    {
        return CharClass::Hiragana;
    }
    if ((c >= 0x30A0 && c <= 0x30FF) || (c >= 0xFF66 && c <= 0xFF9D))
    {
        return CharClass::Katakana;
    }
    if ((c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF))
    {
        return CharClass::Han;
    }
    if (std::iswspace(c) != 0 || c == 0x3000)
    {
        return CharClass::Space;
    }
    // Surrogates aren't punctuation: an emoji or a rare ideograph should select as a word.
    if (c == L'_' || (c >= 0xD800 && c <= 0xDFFF) || std::iswalnum(c) != 0)
    {
        return CharClass::Word;
    }
    if ((c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFF65) || std::iswpunct(c) != 0)
    {
        return CharClass::Punctuation;
    }
    return CharClass::Word;
}

std::wstring readClipboard()
{
    std::wstring text;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(Window::get().handle()))
    {
        return text;
    }
    if (HANDLE const data = GetClipboardData(CF_UNICODETEXT); data != nullptr)
    {
        if (auto const* locked = static_cast<wchar_t const*>(GlobalLock(data)); locked != nullptr)
        {
            text = locked;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

void writeClipboard(std::wstring_view text)
{
    if (!OpenClipboard(Window::get().handle()))
    {
        return;
    }
    EmptyClipboard();
    size_t const bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL const memory = GlobalAlloc(GMEM_MOVEABLE, bytes); memory != nullptr)
    {
        if (auto* locked = static_cast<wchar_t*>(GlobalLock(memory)); locked != nullptr)
        {
            std::memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
            locked[text.size()] = L'\0';
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        }
        else
        {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

} // namespace

TextField::TextField()
{
    interactive_ = true;
}

TextField::~TextField()
{
    if (focused_)
    {
        // Not stopBlink: it repaints, and the parents are already partway through going away.
        Window::get().cancel(blinkTimer_);
        ImmAssociateContext(Window::get().handle(), nullptr);
        DestroyCaret();
    }
}

void TextField::setText(std::wstring_view text)
{
    if (text == text_)
    {
        return;
    }
    text_ = sanitised(text);
    caret_ = anchor_ = static_cast<int>(text_.size());
    pendingHigh_ = 0;
    undo_.clear();
    redo_.clear();
    lastEdit_ = EditKind::None;
    layoutDirty_ = true;
    revealCaret();
    invalidate();
}

void TextField::setPlaceholder(std::wstring placeholder)
{
    placeholder_ = std::move(placeholder);
    invalidate();
}

void TextField::setTextStyle(TextStyle style)
{
    style_ = style;
    lineHeight_ = 0.0F;
    layoutDirty_ = true;
    invalidate();
}

void TextField::setFieldStyle(FieldStyle style)
{
    fieldStyle_ = style;
    invalidate();
}

void TextField::setIcon(std::wstring name)
{
    icon_ = std::move(name);
    invalidate();
}

void TextField::setAlignment(DWRITE_TEXT_ALIGNMENT alignment)
{
    alignment_ = alignment;
    invalidate();
}

void TextField::setEnabled(bool enabled)
{
    if (enabled == enabled_)
    {
        return;
    }
    enabled_ = enabled;
    interactive_ = enabled;
    if (!enabled && Window::get().focused() == this)
    {
        Window::get().focus(nullptr);
    }
    invalidate();
}

void TextField::setFilter(std::function<bool(wchar_t)> filter)
{
    filter_ = std::move(filter);
}

void TextField::selectAll()
{
    setSelection(0, static_cast<int>(text_.size()));
}

void TextField::setSelection(int anchor, int caret)
{
    int const size = static_cast<int>(text_.size());
    anchor_ = std::clamp(anchor, 0, size);
    caret_ = std::clamp(caret, 0, size);
    caretChanged();
}

void TextField::ensureLayout()
{
    if (!layoutDirty_ && layout_ != nullptr)
    {
        return;
    }
    layoutDirty_ = false;

    Theme& theme = Theme::get();
    IDWriteFactory5* dwrite = Window::get().renderer().dwrite();
    IDWriteTextFormat* format = theme.format(style_);

    if (lineHeight_ <= 0.0F)
    {
        // Measured once, so falling back to a CJK font doesn't nudge the line up and down.
        ComPtr<IDWriteTextLayout> sample;
        dwrite->CreateTextLayout(L"Ag", 2, format, kLayoutSize, kLayoutSize, &sample);
        DWRITE_LINE_METRICS line{};
        UINT32 count = 0;
        sample->GetLineMetrics(&line, 1, &count);
        lineHeight_ = line.height;
        baseline_ = line.baseline;
    }

    layout_.Reset();
    dwrite->CreateTextLayout(text_.c_str(), static_cast<UINT32>(text_.size()), format, kLayoutSize, kLayoutSize, &layout_);
    // The format is shared, and whoever drew with it last left their alignment and trimming on it.
    layout_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    layout_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DWRITE_TRIMMING const none{ DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
    layout_->SetTrimming(&none, nullptr);
    layout_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lineHeight_, baseline_);

    DWRITE_TEXT_METRICS metrics{};
    layout_->GetMetrics(&metrics);
    textWidth_ = metrics.widthIncludingTrailingWhitespace;
}

D2D1_RECT_F TextField::innerRect() const
{
    float const padding = fieldStyle_ == FieldStyle::Plain ? 0.0F : fieldStyle_ == FieldStyle::Capsule ? kCapsulePadding : kPadding;
    float const icon = icon_.empty() ? 0.0F : kIconSize + kIconGap;
    return D2D1::RectF(padding + icon, 0.0F, width() - padding, height());
}

float TextField::textOrigin()
{
    ensureLayout();
    D2D1_RECT_F const inner = innerRect();
    if (alignment_ == DWRITE_TEXT_ALIGNMENT_TRAILING && textWidth_ <= inner.right - inner.left)
    {
        return inner.right - textWidth_;
    }
    return inner.left - scrollX_;
}

float TextField::caretX(int position)
{
    ensureLayout();
    FLOAT x = 0.0F;
    FLOAT y = 0.0F;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout_->HitTestTextPosition(static_cast<UINT32>(position), FALSE, &x, &y, &metrics);
    return x;
}

int TextField::positionAt(float x)
{
    float const local = x - textOrigin();
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout_->HitTestPoint(local, lineHeight_ / 2.0F, &trailing, &inside, &metrics);
    return static_cast<int>(metrics.textPosition + (trailing ? metrics.length : 0));
}

int TextField::nextCluster(int position)
{
    if (position >= static_cast<int>(text_.size()))
    {
        return static_cast<int>(text_.size());
    }
    ensureLayout();
    FLOAT x = 0.0F;
    FLOAT y = 0.0F;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout_->HitTestTextPosition(static_cast<UINT32>(position), FALSE, &x, &y, &metrics);
    return static_cast<int>(metrics.textPosition + std::max<UINT32>(1, metrics.length));
}

int TextField::previousCluster(int position)
{
    if (position <= 0)
    {
        return 0;
    }
    ensureLayout();
    FLOAT x = 0.0F;
    FLOAT y = 0.0F;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout_->HitTestTextPosition(static_cast<UINT32>(position - 1), FALSE, &x, &y, &metrics);
    return static_cast<int>(metrics.textPosition);
}

int TextField::previousCodePoint(int position) const
{
    if (position >= 2 && IS_LOW_SURROGATE(text_[position - 1]) && IS_HIGH_SURROGATE(text_[position - 2]))
    {
        return position - 2;
    }
    return std::max(0, position - 1);
}

int TextField::wordStart(int position) const
{
    while (position > 0 && classify(text_[position - 1]) == CharClass::Space)
    {
        --position;
    }
    if (position == 0)
    {
        return 0;
    }
    CharClass const kind = classify(text_[position - 1]);
    while (position > 0 && classify(text_[position - 1]) == kind)
    {
        --position;
    }
    return position;
}

int TextField::wordEnd(int position) const
{
    int const size = static_cast<int>(text_.size());
    while (position < size && classify(text_[position]) == CharClass::Space)
    {
        ++position;
    }
    if (position == size)
    {
        return size;
    }
    CharClass const kind = classify(text_[position]);
    while (position < size && classify(text_[position]) == kind)
    {
        ++position;
    }
    return position;
}

void TextField::wordAround(int position, int& start, int& end) const
{
    int const size = static_cast<int>(text_.size());
    start = end = position;
    if (size == 0)
    {
        return;
    }
    int const at = position < size ? position : size - 1;
    CharClass const kind = classify(text_[at]);
    start = end = at;
    while (start > 0 && classify(text_[start - 1]) == kind)
    {
        --start;
    }
    while (end < size && classify(text_[end]) == kind)
    {
        ++end;
    }
}

void TextField::moveCaret(int position, bool extend)
{
    caret_ = position;
    if (!extend)
    {
        anchor_ = position;
    }
    caretChanged();
}

void TextField::caretChanged()
{
    lastEdit_ = EditKind::None;
    revealCaret();
    if (focused_)
    {
        startBlink();
        positionIme();
    }
    invalidate();
}

void TextField::revealCaret()
{
    ensureLayout();
    D2D1_RECT_F const inner = innerRect();
    float const room = inner.right - inner.left - kCaretMargin;
    float const x = caretX(caret_);
    float scroll = scrollX_;
    if (x - scroll > room)
    {
        scroll = x - room;
    }
    else if (x - scroll < 0.0F)
    {
        scroll = x;
    }
    scrollX_ = std::clamp(scroll, 0.0F, std::max(0.0F, textWidth_ - room));
}

void TextField::remember(EditKind kind)
{
    // A run of typing undoes as one step, the way the Mac's field editor does.
    if (kind != EditKind::None && kind == lastEdit_ && caret_ == anchor_)
    {
        return;
    }
    undo_.push_back(Snapshot{ text_, caret_, anchor_ });
    if (undo_.size() > kUndoLimit)
    {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
}

void TextField::replaceRange(int start, int end, std::wstring_view with, EditKind kind)
{
    if (start == end && with.empty())
    {
        return;
    }
    remember(kind);
    text_.replace(static_cast<size_t>(start), static_cast<size_t>(end - start), with);
    caret_ = anchor_ = start + static_cast<int>(with.size());
    layoutDirty_ = true;
    caretChanged();
    lastEdit_ = kind;
    if (onChange)
    {
        onChange(text_);
    }
}

void TextField::restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to)
{
    if (from.empty())
    {
        return;
    }
    to.push_back(Snapshot{ text_, caret_, anchor_ });
    Snapshot snapshot = std::move(from.back());
    from.pop_back();
    text_ = std::move(snapshot.text);
    caret_ = snapshot.caret;
    anchor_ = snapshot.anchor;
    layoutDirty_ = true;
    caretChanged();
    if (onChange)
    {
        onChange(text_);
    }
}

std::wstring TextField::sanitised(std::wstring_view text) const
{
    std::wstring clean;
    clean.reserve(text.size());
    for (wchar_t const c : text)
    {
        wchar_t const kept = c == L'\r' || c == L'\n' || c == L'\t' ? L' ' : c;
        if (kept < 0x20 || kept == 0x7F || (filter_ && !filter_(kept)))
        {
            continue;
        }
        clean += kept;
    }
    return clean;
}

void TextField::copy() const
{
    if (caret_ != anchor_)
    {
        int const start = std::min(caret_, anchor_);
        writeClipboard(std::wstring_view(text_).substr(static_cast<size_t>(start), static_cast<size_t>(std::abs(caret_ - anchor_))));
    }
}

void TextField::paste()
{
    std::wstring const clean = sanitised(readClipboard());
    if (!clean.empty())
    {
        replaceRange(std::min(caret_, anchor_), std::max(caret_, anchor_), clean, EditKind::None);
    }
}

bool TextField::handleKey(UINT key, bool shift, bool control)
{
    int const size = static_cast<int>(text_.size());
    int const start = std::min(caret_, anchor_);
    int const end = std::max(caret_, anchor_);
    bool const selection = start != end;

    switch (key)
    {
    case VK_LEFT:
        if (selection && !shift)
        {
            moveCaret(start, false);
        }
        else
        {
            moveCaret(control ? wordStart(caret_) : previousCluster(caret_), shift);
        }
        return true;
    case VK_RIGHT:
        if (selection && !shift)
        {
            moveCaret(end, false);
        }
        else
        {
            moveCaret(control ? wordEnd(caret_) : nextCluster(caret_), shift);
        }
        return true;
    case VK_HOME:
        moveCaret(0, shift);
        return true;
    case VK_END:
        moveCaret(size, shift);
        return true;
    case VK_BACK:
        if (selection)
        {
            replaceRange(start, end, {}, EditKind::None);
        }
        else
        {
            replaceRange(control ? wordStart(caret_) : previousCodePoint(caret_), caret_, {}, EditKind::Deleting);
        }
        return true;
    case VK_DELETE:
        if (selection)
        {
            replaceRange(start, end, {}, EditKind::None);
        }
        else
        {
            replaceRange(caret_, control ? wordEnd(caret_) : nextCluster(caret_), {}, EditKind::Deleting);
        }
        return true;
    case VK_RETURN:
        if (onSubmit)
        {
            onSubmit();
            return true;
        }
        return false;
    case VK_ESCAPE:
        if (onCancel)
        {
            onCancel();
            return true;
        }
        return false;
    default:
        break;
    }

    if (!control)
    {
        return false;
    }
    switch (key)
    {
    case 'A':
        selectAll();
        return true;
    case 'C':
        copy();
        return true;
    case 'X':
        copy();
        replaceRange(start, end, {}, EditKind::None);
        return true;
    case 'V':
        paste();
        return true;
    case 'Z':
        if (shift)
        {
            restore(redo_, undo_);
        }
        else
        {
            restore(undo_, redo_);
        }
        return true;
    case 'Y':
        restore(redo_, undo_);
        return true;
    default:
        return false;
    }
}

bool TextField::onKey(UINT key, bool down)
{
    if (!down)
    {
        return false;
    }
    return handleKey(key, (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
}

bool TextField::onChar(wchar_t character)
{
    if (IS_HIGH_SURROGATE(character))
    {
        pendingHigh_ = character;
        return true;
    }
    wchar_t const high = pendingHigh_;
    pendingHigh_ = 0;
    if (IS_LOW_SURROGATE(character))
    {
        if (high == 0)
        {
            return true;
        }
        wchar_t const pair[2] = { high, character };
        if (!filter_ || (filter_(high) && filter_(character)))
        {
            replaceRange(std::min(caret_, anchor_), std::max(caret_, anchor_), std::wstring_view(pair, 2), EditKind::Typing);
        }
        return true;
    }
    if (character < 0x20 || character == 0x7F || (filter_ && !filter_(character)))
    {
        return false;
    }
    replaceRange(std::min(caret_, anchor_), std::max(caret_, anchor_), std::wstring_view(&character, 1), EditKind::Typing);
    return true;
}

bool TextField::onMouse(MouseEvent const& event)
{
    switch (event.kind)
    {
    case MouseKind::Down:
        dragging_ = true;
        wordDrag_ = false;
        moveCaret(positionAt(event.point.x), event.shift);
        return true;
    case MouseKind::DoubleClick:
    {
        int const position = positionAt(event.point.x);
        wordAround(position, wordDragStart_, wordDragEnd_);
        dragging_ = true;
        wordDrag_ = true;
        setSelection(wordDragStart_, wordDragEnd_);
        return true;
    }
    case MouseKind::Move:
        if (!dragging_)
        {
            return false;
        }
        if (wordDrag_)
        {
            int const position = positionAt(event.point.x);
            int start = 0;
            int end = 0;
            wordAround(position, start, end);
            if (position < wordDragStart_)
            {
                setSelection(wordDragEnd_, start);
            }
            else
            {
                setSelection(wordDragStart_, std::max(end, wordDragEnd_));
            }
        }
        else
        {
            moveCaret(positionAt(event.point.x), true);
        }
        return true;
    case MouseKind::Up:
        dragging_ = false;
        return true;
    default:
        return false;
    }
}

void TextField::onFocus(bool focused)
{
    focused_ = focused;
    pendingHigh_ = 0;
    dragging_ = false;
    HWND const window = Window::get().handle();
    if (focused)
    {
        ImmAssociateContextEx(window, nullptr, IACE_DEFAULT);
        // Hidden, but magnifiers and older input methods follow the system caret.
        ensureLayout();
        CreateCaret(window, nullptr, 1, static_cast<int>(lineHeight_));
        startBlink();
        positionIme();
    }
    else
    {
        stopBlink();
        ImmAssociateContext(window, nullptr);
        DestroyCaret();
        if (onBlur)
        {
            onBlur();
        }
    }
    invalidate();
}

void TextField::onWindowActive(bool active)
{
    // Nothing should tick while another app is in front.
    if (active)
    {
        startBlink();
    }
    else
    {
        stopBlink();
    }
}

void TextField::onImeStart()
{
    positionIme();
}

HCURSOR TextField::cursor() const
{
    return LoadCursorW(nullptr, IDC_IBEAM);
}

void TextField::invalidateCaret()
{
    if (lineHeight_ <= 0.0F)
    {
        invalidate();
        return;
    }
    auto const origin = absoluteOrigin();
    float const x = origin.x + textOrigin() + caretX(caret_);
    float const top = origin.y + (height() - lineHeight_) / 2.0F;
    Window::get().invalidate(D2D1::RectF(x - 2.0F, top, x + 3.0F, top + lineHeight_));
}

void TextField::startBlink()
{
    stopBlink();
    caretOn_ = true;
    invalidateCaret();
    UINT const interval = GetCaretBlinkTime();
    if (interval == 0 || interval == INFINITE)
    {
        return;
    }
    blinkTimer_ = Window::get().every(
        interval,
        [this]
        {
            caretOn_ = !caretOn_;
            invalidateCaret();
        });
}

void TextField::stopBlink()
{
    if (blinkTimer_ != 0)
    {
        Window::get().cancel(blinkTimer_);
        blinkTimer_ = 0;
    }
    if (caretOn_)
    {
        caretOn_ = false;
        invalidateCaret();
    }
}

void TextField::positionIme()
{
    HWND const window = Window::get().handle();
    auto const origin = absoluteOrigin();
    float const scale = Window::get().renderer().dpi() / 96.0F;
    float const x = origin.x + textOrigin() + caretX(caret_);
    float const top = origin.y + (height() - lineHeight_) / 2.0F;
    POINT const caret{ static_cast<LONG>(std::lround(x * scale)), static_cast<LONG>(std::lround(top * scale)) };
    LONG const bottom = caret.y + static_cast<LONG>(std::lround(lineHeight_ * scale));

    SetCaretPos(caret.x, caret.y);

    HIMC const context = ImmGetContext(window);
    if (context == nullptr)
    {
        return;
    }

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = caret;
    ImmSetCompositionWindow(context, &composition);

    // Excluding the line keeps the candidate list from opening over the text being composed.
    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = POINT{ caret.x, bottom };
    candidate.rcArea = RECT{ caret.x, caret.y, caret.x + 1, bottom };
    ImmSetCandidateWindow(context, &candidate);

    LOGFONTW font{};
    font.lfHeight = -static_cast<LONG>(std::lround(style_.size * scale));
    font.lfWeight = FW_NORMAL;
    font.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(font.lfFaceName, L"Segoe UI");
    ImmSetCompositionFontW(context, &font);

    ImmReleaseContext(window, context);
}

void TextField::layout()
{
    if (lineHeight_ > 0.0F)
    {
        revealCaret();
    }
}

void TextField::draw(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();
    ensureLayout();

    if (fieldStyle_ != FieldStyle::Plain)
    {
        float const radius = fieldStyle_ == FieldStyle::Capsule ? height() / 2.0F : kRoundedRadius;
        fillRounded(context, bounds(), radius, enabled_ ? colors.field : withAlpha(colors.field, 0.5F));
        strokeRounded(context, bounds(), radius, focused_ ? withAlpha(colors.accent, 0.7F) : colors.border);
    }

    if (!icon_.empty())
    {
        D2D1_RECT_F const inner = innerRect();
        float const left = inner.left - kIconSize - kIconGap;
        float const top = (height() - kIconSize) / 2.0F;
        theme.icon(context, icon_, D2D1::RectF(left, top, left + kIconSize, top + kIconSize), colors.foreground);
    }

    D2D1_RECT_F const inner = innerRect();
    // A pixel of slack keeps the caret visible at the very end of the text.
    D2D1_RECT_F const clip = D2D1::RectF(inner.left - 1.0F, 0.0F, inner.right + kCaretMargin, height());
    context->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

    float const x = textOrigin();
    float const top = (height() - lineHeight_) / 2.0F;

    if (focused_ && caret_ != anchor_)
    {
        UINT32 const start = static_cast<UINT32>(std::min(caret_, anchor_));
        UINT32 const length = static_cast<UINT32>(std::abs(caret_ - anchor_));
        DWRITE_HIT_TEST_METRICS runs[8];
        UINT32 count = 0;
        // Past eight runs is pathological mixed-direction text, and painting the first eight is fine.
        layout_->HitTestTextRange(start, length, 0.0F, 0.0F, runs, static_cast<UINT32>(std::size(runs)), &count);
        count = std::min(count, static_cast<UINT32>(std::size(runs)));
        for (UINT32 i = 0; i < count; ++i)
        {
            context->FillRectangle(D2D1::RectF(x + runs[i].left, top, x + runs[i].left + runs[i].width, top + lineHeight_), theme.brush(colors.selected));
        }
    }

    if (text_.empty())
    {
        D2D1_RECT_F const rect = D2D1::RectF(inner.left, 0.0F, inner.right, height());
        theme.text(context, placeholder_, style_, rect, roleColor(ColorRole::Muted), alignment_, Trim::End, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    else
    {
        D2D1_COLOR_F const color = enabled_ ? colors.bright : withAlpha(colors.foreground, 0.55F);
        context->DrawTextLayout(D2D1::Point2F(x, top), layout_.Get(), theme.brush(color), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }

    if (focused_ && caretOn_)
    {
        float const caretLeft = snapToPixel(x + caretX(caret_));
        context->FillRectangle(D2D1::RectF(caretLeft, top + 1.0F, caretLeft + kCaretWidth, top + lineHeight_ - 1.0F), theme.brush(colors.accent));
    }

    context->PopAxisAlignedClip();
}

} // namespace ui
