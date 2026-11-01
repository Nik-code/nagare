#include "ui/theme.h"

#include <windows.h>

#include <shlwapi.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>

namespace ui
{

namespace
{

Palette const kLight{
    hex(0xf3eee4), hex(0xfbf8f1), hex(0xdcd6c8), hex(0x47524f), hex(0x1c2a30),
    hex(0xebe5d8), hex(0xe4decf), hex(0x0c5f82), hex(0xa3442a), hex(0xa3301a),
};
Palette const kDark{
    hex(0x23211f), hex(0x1c1a18), hex(0x383531), hex(0xb5ac9f), hex(0xefe6d6),
    hex(0x2c2a27), hex(0x34312d), hex(0x8fd8cc), hex(0xf0a98a), hex(0xff9a7a),
};

/// Stands in for the owner of the font bytes. They belong to the mapped image, which never goes away,
/// and without an owner DirectWrite would take a private copy of the whole file.
class ImageOwned final : public IUnknown
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (id != __uuidof(IUnknown))
        {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        *out = this;
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
};

} // namespace

D2D1_COLOR_F hex(uint32_t rgb, float alpha)
{
    return D2D1::ColorF(rgb, alpha);
}

D2D1_COLOR_F withAlpha(D2D1_COLOR_F color, float alpha)
{
    color.a = alpha;
    return color;
}

std::span<uint8_t const> loadAsset(std::wstring_view relativePath)
{
    std::wstring name(relativePath);
    std::replace_if(name.begin(), name.end(), [](wchar_t c) { return !iswalnum(c); }, L'_');
    HMODULE const module = GetModuleHandleW(nullptr);
    HRSRC const found = FindResourceW(module, name.c_str(), RT_RCDATA);
    HGLOBAL const loaded = found != nullptr ? LoadResource(module, found) : nullptr;
    if (loaded == nullptr)
    {
        return {};
    }
    return { static_cast<uint8_t const*>(LockResource(loaded)), SizeofResource(module, found) };
}

Theme& Theme::get()
{
    static Theme theme;
    return theme;
}

void Theme::init(Renderer* renderer)
{
    renderer_ = renderer;
    dark_ = resolveDark();

    renderer_->d2d()->CreateSolidColorBrush(hex(0xffffff), &brush_);

    IDWriteFactory5* dwrite = renderer_->dwrite();
    ComPtr<IDWriteFontCollection> system;
    UINT32 familyIndex = 0;
    BOOL variable = FALSE;
    // Windows 10 doesn't have the variable font, and what it would fall back to isn't Segoe UI.
    if (SUCCEEDED(dwrite->GetSystemFontCollection(&system)) && SUCCEEDED(system->FindFamilyName(L"Segoe UI Variable Text", &familyIndex, &variable)) && variable)
    {
        bodyFamily_ = L"Segoe UI Variable Text";
    }

    auto const font = loadAsset(L"fonts\\Outfit-Medium.ttf");
    static ImageOwned imageOwned;
    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    ComPtr<IDWriteFontSetBuilder1> builder;
    ComPtr<IDWriteFontFile> file;
    if (!font.empty() && SUCCEEDED(dwrite->CreateInMemoryFontFileLoader(&loader)) && SUCCEEDED(dwrite->RegisterFontFileLoader(loader.Get()))
        && SUCCEEDED(dwrite->CreateFontSetBuilder(&builder))
        && SUCCEEDED(loader->CreateInMemoryFontFileReference(dwrite, font.data(), static_cast<UINT32>(font.size()), &imageOwned, &file)))
    {
        builder->AddFontFile(file.Get());
        ComPtr<IDWriteFontSet> set;
        builder->CreateFontSet(&set);
        dwrite->CreateFontCollectionFromFontSet(set.Get(), &outfit_);
    }
}

bool Theme::resolveDark() const
{
    if (mode_ != ThemeMode::System)
    {
        return mode_ == ThemeMode::Dark;
    }
    DWORD light = 0;
    DWORD size = sizeof(light);
    LSTATUS const status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &light,
        &size);
    return status == ERROR_SUCCESS ? light == 0 : true;
}

void Theme::setMode(ThemeMode mode)
{
    mode_ = mode;
    systemThemeChanged();
}

void Theme::systemThemeChanged()
{
    bool const dark = resolveDark();
    if (dark != dark_)
    {
        dark_ = dark;
        clearThemed();
    }
}

void Theme::clearThemed()
{
    art_.clear();
    icons_.clear();
}

Palette const& Theme::colors() const
{
    return dark_ ? kDark : kLight;
}

ID2D1SolidColorBrush* Theme::brush(D2D1_COLOR_F color)
{
    brush_->SetColor(color);
    return brush_.Get();
}

IDWriteTextFormat* Theme::format(TextStyle style)
{
    auto const found = formats_.find(style);
    if (found != formats_.end())
    {
        return found->second.Get();
    }

    bool const useOutfit = style.display && outfit_ != nullptr;
    ComPtr<IDWriteTextFormat> format;
    renderer_->dwrite()->CreateTextFormat(
        useOutfit ? L"Outfit" : bodyFamily_,
        useOutfit ? outfit_.Get() : nullptr,
        style.weight,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        style.size,
        L"en-us",
        &format);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    formats_.emplace(style, format);
    return format.Get();
}

std::wstring Theme::middleTrimmed(std::wstring_view string, TextStyle style, float width)
{
    if (measure(string, style).width <= width)
    {
        return std::wstring(string);
    }
    size_t low = 0;
    size_t high = string.size();
    while (low + 1 < high)
    {
        size_t const keep = (low + high) / 2;
        std::wstring candidate(string.substr(0, keep - keep / 2));
        candidate += L"...";
        candidate += string.substr(string.size() - keep / 2);
        if (measure(candidate, style).width <= width)
        {
            low = keep;
        }
        else
        {
            high = keep;
        }
    }
    std::wstring result(string.substr(0, low - low / 2));
    result += L"...";
    result += string.substr(string.size() - low / 2);
    return result;
}

void Theme::text(
    ID2D1DeviceContext* context,
    std::wstring_view string,
    TextStyle style,
    D2D1_RECT_F rect,
    D2D1_COLOR_F color,
    DWRITE_TEXT_ALIGNMENT align,
    Trim trim,
    DWRITE_PARAGRAPH_ALIGNMENT vertical)
{
    IDWriteTextFormat* textFormat = format(style);
    textFormat->SetTextAlignment(align);
    textFormat->SetParagraphAlignment(vertical);

    DWRITE_TRIMMING trimming{};
    if (trim == Trim::End)
    {
        if (ellipsis_ == nullptr)
        {
            renderer_->dwrite()->CreateEllipsisTrimmingSign(textFormat, &ellipsis_);
        }
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        textFormat->SetTrimming(&trimming, ellipsis_.Get());
    }
    else
    {
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_NONE;
        textFormat->SetTrimming(&trimming, nullptr);
    }

    std::wstring trimmed;
    if (trim == Trim::Middle)
    {
        trimmed = middleTrimmed(string, style, rect.right - rect.left);
        string = trimmed;
    }
    context->DrawText(string.data(), static_cast<UINT32>(string.size()), textFormat, rect, brush(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

D2D1_SIZE_F Theme::measure(std::wstring_view string, TextStyle style, float maxWidth)
{
    ComPtr<IDWriteTextLayout> layout;
    renderer_->dwrite()->CreateTextLayout(string.data(), static_cast<UINT32>(string.size()), format(style), maxWidth, 10000.0F, &layout);
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return D2D1::SizeF(metrics.widthIncludingTrailingWhitespace, metrics.height);
}

ID2D1Bitmap1* Theme::art(std::wstring_view name, UINT pixelWidth, UINT pixelHeight)
{
    pixelWidth = std::max<UINT>(1, pixelWidth);
    pixelHeight = std::max<UINT>(1, pixelHeight);

    auto const found = art_.find(name);
    if (found != art_.end() && found->second.width == pixelWidth && found->second.height == pixelHeight)
    {
        return found->second.bitmap.Get();
    }

    std::wstring path = L"art\\";
    path += name;
    path += dark_ ? L"-dark.jpg" : L"-light.jpg";
    auto const bytes = loadAsset(path);
    if (bytes.empty())
    {
        return nullptr;
    }

    // Reads the mapped resource directly, where a memory IStream would copy the whole JPEG first.
    IWICImagingFactory* wic = renderer_->wic();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))
        || FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)))
    {
        return nullptr;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    frame->GetSize(&sourceWidth, &sourceHeight);

    // Scale so the art covers the target, then centre-crop the overflow.
    double const scale = std::max(static_cast<double>(pixelWidth) / sourceWidth, static_cast<double>(pixelHeight) / sourceHeight);
    UINT const scaledWidth = std::max(pixelWidth, static_cast<UINT>(sourceWidth * scale + 0.5));
    UINT const scaledHeight = std::max(pixelHeight, static_cast<UINT>(sourceHeight * scale + 0.5));

    ComPtr<IWICBitmapScaler> scaler;
    wic->CreateBitmapScaler(&scaler);
    scaler->Initialize(frame.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant);

    ComPtr<IWICBitmapClipper> clipper;
    wic->CreateBitmapClipper(&clipper);
    WICRect const crop{
        static_cast<INT>((scaledWidth - pixelWidth) / 2),
        static_cast<INT>((scaledHeight - pixelHeight) / 2),
        static_cast<INT>(pixelWidth),
        static_cast<INT>(pixelHeight),
    };
    clipper->Initialize(scaler.Get(), &crop);

    ComPtr<IWICFormatConverter> converter;
    wic->CreateFormatConverter(&converter);
    converter->Initialize(clipper.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    ComPtr<ID2D1Bitmap1> bitmap;
    D2D1_BITMAP_PROPERTIES1 const props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        renderer_->dpi(),
        renderer_->dpi());
    if (FAILED(renderer_->d2d()->CreateBitmapFromWicBitmap(converter.Get(), &props, &bitmap)))
    {
        return nullptr;
    }

    art_[std::wstring(name)] = ArtEntry{ pixelWidth, pixelHeight, bitmap };
    return bitmap.Get();
}

void Theme::icon(ID2D1DeviceContext* context, std::wstring_view name, D2D1_RECT_F rect, D2D1_COLOR_F color)
{
    int const red = static_cast<int>(color.r * 255.0F + 0.5F);
    int const green = static_cast<int>(color.g * 255.0F + 0.5F);
    int const blue = static_cast<int>(color.b * 255.0F + 0.5F);

    // A stack buffer, because list rows draw icons on every repaint.
    wchar_t key[64];
    int const length = swprintf(key, std::size(key), L"%.*s#%02x%02x%02x", static_cast<int>(name.size()), name.data(), red, green, blue);
    std::wstring_view const keyView(key, length > 0 ? static_cast<size_t>(length) : 0U);

    if (context5_ == nullptr)
    {
        context->QueryInterface(IID_PPV_ARGS(&context5_));
    }
    if (context5_ == nullptr)
    {
        return;
    }

    auto found = icons_.find(keyView);
    if (found == icons_.end())
    {
        std::wstring path = L"icons\\";
        path += name;
        path += L".svg";
        auto const bytes = loadAsset(path);
        if (bytes.empty())
        {
            return;
        }

        std::string svg(bytes.begin(), bytes.end());
        char narrow[16];
        snprintf(narrow, sizeof(narrow), "#%02x%02x%02x", red, green, blue);
        for (size_t at = svg.find("#000000"); at != std::string::npos; at = svg.find("#000000", at + 7))
        {
            svg.replace(at, 7, narrow);
        }

        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(reinterpret_cast<BYTE const*>(svg.data()), static_cast<UINT>(svg.size())));
        ComPtr<ID2D1SvgDocument> document;
        if (FAILED(context5_->CreateSvgDocument(stream.Get(), D2D1::SizeF(24.0F, 24.0F), &document)))
        {
            return;
        }
        found = icons_.emplace(std::wstring(keyView), document).first;
    }

    float const size = rect.right - rect.left;
    D2D1_MATRIX_3X2_F saved{};
    context5_->GetTransform(&saved);
    context5_->SetTransform(D2D1::Matrix3x2F::Scale(size / 24.0F, size / 24.0F) * D2D1::Matrix3x2F::Translation(rect.left, rect.top) * saved);
    if (color.a < 1.0F)
    {
        context5_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), color.a), nullptr);
    }
    context5_->DrawSvgDocument(found->second.Get());
    if (color.a < 1.0F)
    {
        context5_->PopLayer();
    }
    context5_->SetTransform(saved);
}

GlassShape Theme::tileGlass() const
{
    GlassShape shape;
    shape.radius = 22.0F;
    shape.tint = dark_ ? hex(0xefe6d6, 0.10F) : hex(0xffffff, 0.22F);
    return shape;
}

GlassShape Theme::panelGlass() const
{
    GlassShape shape;
    shape.radius = 28.0F;
    shape.band = 22.0F;
    shape.refraction = 24.0F;
    shape.shadow = 0.32F;
    shape.rimLight = 0.18F;
    shape.clarity = 0.0F;
    shape.saturation = 1.45F;
    shape.tint = dark_ ? hex(0x4a4743, 0.36F) : hex(0xf5f6ee, 0.58F);
    return shape;
}

void Theme::trim()
{
    art_.clear();
    icons_.clear();
}

} // namespace ui
