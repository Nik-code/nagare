#include "app/main_drop.h"

#include <ole2.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <vector>

#include "app/app.h"
#include "app/transfer_row.h"
#include "ui/widgets_internal.h"

namespace app
{

namespace
{

using namespace ui::detail;
using ui::TextStyle;
using ui::Theme;

// DragOverlayView.mm and DragOverlayWindow.mm
constexpr float kBadgeWidth = 325.0F;
constexpr float kBadgeHeight = 84.0F;
constexpr float kBadgeRadius = 15.0F;
constexpr float kPadding = 10.0F;
constexpr float kIconSize = 64.0F;
constexpr float kIconInset = 12.0F;
constexpr float kTextGap = 5.0F;
constexpr float kLineGap = 2.0F;
constexpr float kMainHeight = 24.0F;
constexpr float kSubHeight = 19.0F;
constexpr float kDimAlpha = 0.5F;
constexpr float kBadgeAlpha = 0.75F;
constexpr DWORD kFadeInMs = 150;
constexpr DWORD kFadeOutMs = 500;
constexpr TextStyle kMainStyle{ 18.0F, DWRITE_FONT_WEIGHT_BOLD, false };
constexpr TextStyle kSubStyle{ 14.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

bool isTorrentPath(std::wstring const& path)
{
    constexpr std::wstring_view kExtension = L".torrent";
    if (path.size() < kExtension.size())
    {
        return false;
    }
    return _wcsicmp(path.c_str() + path.size() - kExtension.size(), kExtension.data()) == 0;
}

std::vector<std::wstring> droppedFiles(IDataObject* data)
{
    std::vector<std::wstring> files;
    FORMATETC format{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&format, &medium)))
    {
        return files;
    }
    // An HDROP is the global handle itself, not what locking it gives.
    auto drop = static_cast<HDROP>(medium.hGlobal);
    UINT const count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i)
    {
        std::wstring path(DragQueryFileW(drop, i, nullptr, 0) + 1, L'\0');
        DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size()));
        path.pop_back();
        files.push_back(std::move(path));
    }
    ReleaseStgMedium(&medium);
    return files;
}

std::wstring droppedText(IDataObject* data)
{
    std::wstring text;
    FORMATETC format{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&format, &medium)))
    {
        return text;
    }
    if (auto const* chars = static_cast<wchar_t const*>(GlobalLock(medium.hGlobal)); chars != nullptr)
    {
        text = chars;
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);

    auto const isSpace = [](wchar_t c) { return std::iswspace(c) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), isSpace));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), isSpace).base(), text.end());
    return text;
}

bool isLink(std::wstring const& text)
{
    return text.starts_with(L"magnet:") || text.starts_with(L"http://") || text.starts_with(L"https://");
}

} // namespace

DropOverlay::DropOverlay()
{
    floating_ = true;
    opacity_ = 0.0F;
}

void DropOverlay::fadeTo(float target, DWORD durationMs)
{
    float const from = opacity_;
    setHidden(false);
    fade_.run(
        durationMs,
        [this, from, target](float t) { setOpacity(lerp(from, target, t)); },
        [this, target]
        {
            if (target <= 0.0F)
            {
                setHidden(true);
            }
        });
}

void DropOverlay::showBadge(std::wstring icon, std::wstring mainLine, std::wstring subLine)
{
    icon_ = std::move(icon);
    main_ = std::move(mainLine);
    sub_ = std::move(subLine);
    fadeTo(1.0F, kFadeInMs);
}

void DropOverlay::hideBadge()
{
    if (!hidden())
    {
        fadeTo(0.0F, kFadeOutMs);
    }
}

void DropOverlay::draw(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    context->FillRectangle(bounds(), theme.brush(D2D1::ColorF(0.0F, 0.0F, 0.0F, kDimAlpha)));

    float const left = std::round((width() - kBadgeWidth) / 2.0F);
    float const top = std::round((height() - kBadgeHeight) / 2.0F);
    D2D1_RECT_F const badge = D2D1::RectF(left, top, left + kBadgeWidth, top + kBadgeHeight);
    fillRounded(context, badge, kBadgeRadius, D2D1::ColorF(0.0F, 0.0F, 0.0F, kBadgeAlpha));

    D2D1_COLOR_F const white = D2D1::ColorF(1.0F, 1.0F, 1.0F);
    float const iconLeft = left + kPadding;
    float const iconTop = top + (kBadgeHeight - kIconSize) / 2.0F;
    theme.icon(context, icon_, D2D1::RectF(iconLeft + kIconInset, iconTop + kIconInset, iconLeft + kIconSize - kIconInset, iconTop + kIconSize - kIconInset), white);

    float const textLeft = iconLeft + kIconSize + kTextGap;
    float const textRight = left + kBadgeWidth - kPadding;
    float const lines = kMainHeight + kLineGap + kSubHeight;
    float const mainTop = top + (kBadgeHeight - lines) / 2.0F;
    float const subTop = mainTop + kMainHeight + kLineGap;
    theme.text(context, main_, kMainStyle, D2D1::RectF(textLeft, mainTop, textRight, mainTop + kMainHeight), white, DWRITE_TEXT_ALIGNMENT_LEADING, ui::Trim::Middle);
    theme.text(context, sub_, kSubStyle, D2D1::RectF(textLeft, subTop, textRight, subTop + kSubHeight), white, DWRITE_TEXT_ALIGNMENT_LEADING, ui::Trim::Middle);
}

class DropTarget final : public IDropTarget
{
public:
    explicit DropTarget(DropOverlay* overlay)
        : overlay_(overlay)
    {
    }

    /// The view is going away, though OLE may still hold a reference for a moment.
    void detach() { overlay_ = nullptr; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (id == IID_IUnknown || id == IID_IDropTarget)
        {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG const left = --references_;
        if (left == 0)
        {
            delete this;
        }
        return left;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override
    {
        accepted_ = describe(data);
        *effect = accepted_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override
    {
        *effect = accepted_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        hide();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override
    {
        hide();
        *effect = accepted_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (!accepted_)
        {
            return S_OK;
        }

        App& app = App::get();
        std::vector<std::wstring> torrents;
        for (std::wstring& path : droppedFiles(data))
        {
            if (isTorrentPath(path))
            {
                torrents.push_back(std::move(path));
            }
        }
        if (!torrents.empty())
        {
            app.openPaths(torrents);
            return S_OK;
        }

        std::wstring const text = droppedText(data);
        if (text.starts_with(L"magnet:"))
        {
            app.openMagnet(text);
        }
        else if (isLink(text))
        {
            app.engine().fetchTorrent(
                text,
                [](std::string bytes, std::wstring const&)
                {
                    if (!bytes.empty())
                    {
                        auto preview = App::get().engine().parseTorrentData(std::move(bytes));
                        if (preview.valid)
                        {
                            App::get().openPreview(std::move(preview));
                        }
                    }
                });
        }
        return S_OK;
    }

private:
    void hide() const
    {
        if (overlay_ != nullptr)
        {
            overlay_->hideBadge();
        }
    }

    /// setTorrents and setURL in DragOverlayWindow. Creating a torrent has no Windows engine call yet,
    /// so a lone plain file isn't accepted the way it is on the Mac.
    bool describe(IDataObject* data) const
    {
        if (overlay_ == nullptr)
        {
            return false;
        }

        App& app = App::get();
        uint64_t size = 0;
        size_t fileCount = 0;
        int count = 0;
        std::wstring name;
        for (std::wstring const& path : droppedFiles(data))
        {
            if (!isTorrentPath(path))
            {
                continue;
            }
            auto const preview = app.engine().parseTorrentFile(path);
            if (!preview.valid)
            {
                continue;
            }
            ++count;
            size += preview.totalBytes;
            fileCount += preview.files.fileCount;
            if (count == 1)
            {
                name = preview.name;
            }
        }

        if (count == 1)
        {
            overlay_->showBadge(fileCount <= 1 ? L"File" : L"Folder", name, sizeText(size));
            return true;
        }
        if (count > 1)
        {
            std::wstring sub = fileCount == 1 ? L"1 file, " : countText(static_cast<int>(fileCount)) + L" files, ";
            sub += sizeText(size);
            sub += L" total";
            overlay_->showBadge(L"Files", countText(count) + L" Torrent Files", std::move(sub));
            return true;
        }

        std::wstring const text = droppedText(data);
        if (isLink(text))
        {
            overlay_->showBadge(L"Link", L"Web Address", text);
            return true;
        }
        return false;
    }

    ULONG references_ = 1;
    DropOverlay* overlay_ = nullptr;
    bool accepted_ = false;
};

IDropTarget* dropInterface(DropTarget* target)
{
    return target;
}

DropTarget* registerDropTarget(DropOverlay* overlay)
{
    // A return of S_FALSE only says COM was already up, which is the normal case here.
    OleInitialize(nullptr);
    auto* target = new DropTarget(overlay);
    if (FAILED(RegisterDragDrop(ui::Window::get().handle(), target)))
    {
        target->detach();
        target->Release();
        return nullptr;
    }
    return target;
}

void releaseDropTarget(DropTarget* target)
{
    if (target != nullptr)
    {
        target->detach();
        RevokeDragDrop(ui::Window::get().handle());
        target->Release();
    }
}

} // namespace app
