#include "renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "ps_blur.h"
#include "ps_down.h"
#include "ps_glass.h"
#include "vs_quad.h"

namespace
{

struct Params
{
    float viewport[2];
    float texelStep[2];
    float quad[4];
    float rect[4];
    float radius;
    float band;
    float refraction;
    float dispersion;
    float tint[4];
    float rimLight;
    float rimDark;
    float shadow;
    float clarity;
    float saturation;
    float padding[3];
};
static_assert(sizeof(Params) % 16 == 0);

constexpr float kShadowReach = 44.0F;
// Quarter-size texels that a changed pixel can reach through the downsample and both blur passes.
constexpr LONG kBlurReach = 8;

[[nodiscard]] bool isEmpty(RECT const& rect)
{
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

[[nodiscard]] bool overlaps(RECT const& a, RECT const& b)
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

void grow(RECT& rect, RECT const& other)
{
    rect.left = std::min(rect.left, other.left);
    rect.top = std::min(rect.top, other.top);
    rect.right = std::max(rect.right, other.right);
    rect.bottom = std::max(rect.bottom, other.bottom);
}

[[nodiscard]] RECT toPixels(D2D1_RECT_F const& rect, float scale, UINT width, UINT height)
{
    RECT out{};
    out.left = std::clamp(static_cast<LONG>(std::floor(rect.left * scale)), 0L, static_cast<LONG>(width));
    out.top = std::clamp(static_cast<LONG>(std::floor(rect.top * scale)), 0L, static_cast<LONG>(height));
    out.right = std::clamp(static_cast<LONG>(std::ceil(rect.right * scale)), 0L, static_cast<LONG>(width));
    out.bottom = std::clamp(static_cast<LONG>(std::ceil(rect.bottom * scale)), 0L, static_cast<LONG>(height));
    return out;
}

[[nodiscard]] RECT shapePixels(GlassShape const& shape, float scale, UINT width, UINT height)
{
    D2D1_RECT_F const rect = D2D1::RectF(shape.rect.left - kShadowReach, shape.rect.top - kShadowReach, shape.rect.right + kShadowReach, shape.rect.bottom + kShadowReach);
    return toPixels(rect, scale, width, height);
}

} // namespace

bool Renderer::init(HWND window)
{
    window_ = window;

    D3D_FEATURE_LEVEL const levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    UINT const flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;
    // WARP is Windows' own CPU rasteriser, so the app holds no video memory and loads no graphics driver.
    // The GPU is only the fallback for a system that has had WARP removed.
    D3D_DRIVER_TYPE const preferred = GetEnvironmentVariableW(L"NAGARE_RENDER_GPU", nullptr, 0) > 0 ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
    HRESULT hr = E_FAIL;
    for (D3D_DRIVER_TYPE const type : { preferred, preferred == D3D_DRIVER_TYPE_WARP ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP })
    {
        hr = D3D11CreateDevice(nullptr, type, nullptr, flags, levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device_, nullptr, &immediate_);
        if (SUCCEEDED(hr))
        {
            break;
        }
    }
    if (FAILED(hr))
    {
        return false;
    }

    device_->CreateVertexShader(g_vsQuad, sizeof(g_vsQuad), nullptr, &vsQuad_);
    device_->CreatePixelShader(g_psBlur, sizeof(g_psBlur), nullptr, &psBlur_);
    device_->CreatePixelShader(g_psDown, sizeof(g_psDown), nullptr, &psDown_);
    device_->CreatePixelShader(g_psGlass, sizeof(g_psGlass), nullptr, &psGlass_);

    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(Params);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&buffer, nullptr, &params_);

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&sampler, &sampler_);

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.ScissorEnable = TRUE;
    device_->CreateRasterizerState(&raster, &raster_);

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&blend, &glassBlend_);

    ComPtr<IDXGIDevice1> dxgiDevice;
    device_.As(&dxgiDevice);
    dxgiDevice->SetMaximumFrameLatency(1);

    D2D1_FACTORY_OPTIONS options{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, d2dFactory_.GetAddressOf())))
    {
        return false;
    }
    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_)))
    {
        return false;
    }
    ComPtr<ID2D1DeviceContext2> context;
    d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context);
    context.As(&context_);
    // Glyph and tessellation caches are the main thing that grows, so keep them small.
    d2dDevice_->SetMaximumTextureMemory(16U * 1024U * 1024U);

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5), reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));

    RECT client{};
    GetClientRect(window_, &client);
    width_ = std::max<UINT>(1, client.right - client.left);
    height_ = std::max<UINT>(1, client.bottom - client.top);
    dpi_ = static_cast<float>(GetDpiForWindow(window_));

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    // Sequential, because a partial frame builds on what the buffer already holds.
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (FAILED(factory->CreateSwapChainForHwnd(device_.Get(), window_, &desc, nullptr, nullptr, &swapChain_)))
    {
        return false;
    }
    factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);

    return createTargets();
}

bool Renderer::createTargets()
{
    D2D1_BITMAP_PROPERTIES1 const targetProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpi_,
        dpi_);

    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = width_;
    texture.Height = height_;
    texture.MipLevels = 1;
    texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&texture, nullptr, &scene_)))
    {
        return false;
    }
    device_->CreateShaderResourceView(scene_.Get(), nullptr, &sceneView_);

    ComPtr<IDXGISurface> surface;
    scene_.As(&surface);
    if (FAILED(context_->CreateBitmapFromDxgiSurface(surface.Get(), targetProps, &sceneTarget_)))
    {
        return false;
    }

    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&back_))))
    {
        return false;
    }
    device_->CreateRenderTargetView(back_.Get(), nullptr, &backView_);
    ComPtr<IDXGISurface> backSurface;
    back_.As(&backSurface);
    if (FAILED(context_->CreateBitmapFromDxgiSurface(backSurface.Get(), targetProps, &backTarget_)))
    {
        return false;
    }

    texture.Width = std::max<UINT>(1, width_ / 4);
    texture.Height = std::max<UINT>(1, height_ / 4);
    for (size_t i = 0; i < std::size(blur_); ++i)
    {
        device_->CreateTexture2D(&texture, nullptr, &blur_[i]);
        device_->CreateShaderResourceView(blur_[i].Get(), nullptr, &blurView_[i]);
        device_->CreateRenderTargetView(blur_[i].Get(), nullptr, &blurTarget_[i]);
    }

    context_->SetDpi(dpi_, dpi_);
    targetsFresh_ = true;
    blurFresh_ = false;
    return true;
}

bool Renderer::createComposed()
{
    D3D11_TEXTURE2D_DESC texture{};
    scene_->GetDesc(&texture);
    if (FAILED(device_->CreateTexture2D(&texture, nullptr, &composed_)))
    {
        return false;
    }
    device_->CreateShaderResourceView(composed_.Get(), nullptr, &composedView_);
    device_->CreateRenderTargetView(composed_.Get(), nullptr, &composedTarget_);
    ComPtr<IDXGISurface> surface;
    composed_.As(&surface);
    D2D1_BITMAP_PROPERTIES1 const props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpi_,
        dpi_);
    return SUCCEEDED(context_->CreateBitmapFromDxgiSurface(surface.Get(), props, &composedBitmap_));
}

void Renderer::releaseTargets()
{
    context_->SetTarget(nullptr);
    composedBitmap_.Reset();
    composedTarget_.Reset();
    composedView_.Reset();
    composed_.Reset();
    sceneTarget_.Reset();
    sceneView_.Reset();
    scene_.Reset();
    backTarget_.Reset();
    backView_.Reset();
    back_.Reset();
    for (size_t i = 0; i < std::size(blur_); ++i)
    {
        blurTarget_[i].Reset();
        blurView_[i].Reset();
        blur_[i].Reset();
    }
    immediate_->ClearState();
    immediate_->Flush();
}

bool Renderer::resize(UINT width, UINT height, float dpi)
{
    width = std::max<UINT>(1, width);
    height = std::max<UINT>(1, height);
    if (width == width_ && height == height_ && dpi == dpi_)
    {
        return true;
    }
    releaseTargets();
    width_ = width;
    height_ = height;
    dpi_ = dpi;
    if (FAILED(swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0)))
    {
        return false;
    }
    return createTargets();
}

ID2D1DeviceContext* Renderer::beginScene()
{
    context_->SetTarget(sceneTarget_.Get());
    context_->BeginDraw();
    return context_.Get();
}

void Renderer::endScene()
{
    context_->EndDraw();
    context_->SetTarget(nullptr);
}

RECT Renderer::changedRegion(GlassLayer const& layer, D2D1_RECT_F const& changed) const
{
    float const scale = dpi_ / 96.0F;
    RECT region = toPixels(changed, scale, width_, height_);
    if (isEmpty(region))
    {
        return region;
    }

    // Glass bends what's behind it, so touching part of a sheet changes all of it. Twice, because the
    // first sheet can grow the region into a second.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (GlassShape const& shape : layer.glass)
        {
            RECT const quad = shapePixels(shape, scale, width_, height_);
            if (overlaps(region, quad))
            {
                grow(region, quad);
            }
        }
    }
    return region;
}

void Renderer::blur(ID3D11ShaderResourceView* source, RECT const& region)
{
    UINT const quarterW = std::max<UINT>(1, width_ / 4);
    UINT const quarterH = std::max<UINT>(1, height_ / 4);

    D3D11_RECT const scissor{ std::max(0L, region.left / 4 - kBlurReach),
                              std::max(0L, region.top / 4 - kBlurReach),
                              std::min(static_cast<LONG>(quarterW), region.right / 4 + kBlurReach),
                              std::min(static_cast<LONG>(quarterH), region.bottom / 4 + kBlurReach) };
    immediate_->RSSetScissorRects(1, &scissor);

    auto const pass = [&](ID3D11RenderTargetView* target, ID3D11ShaderResourceView* input, ID3D11PixelShader* shader, float stepX, float stepY)
    {
        Params params{};
        params.viewport[0] = static_cast<float>(quarterW);
        params.viewport[1] = static_cast<float>(quarterH);
        params.texelStep[0] = stepX;
        params.texelStep[1] = stepY;
        params.quad[2] = params.viewport[0];
        params.quad[3] = params.viewport[1];

        D3D11_MAPPED_SUBRESOURCE mapped{};
        immediate_->Map(params_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        std::memcpy(mapped.pData, &params, sizeof(params));
        immediate_->Unmap(params_.Get(), 0);

        ID3D11ShaderResourceView* const none[2] = {};
        immediate_->PSSetShaderResources(0, 2, none);
        immediate_->OMSetRenderTargets(1, &target, nullptr);
        immediate_->PSSetShaderResources(0, 1, &input);
        immediate_->PSSetShader(shader, nullptr, 0);
        immediate_->Draw(4, 0);
    };

    D3D11_VIEWPORT viewport{ 0.0F, 0.0F, static_cast<float>(quarterW), static_cast<float>(quarterH), 0.0F, 1.0F };
    immediate_->RSSetViewports(1, &viewport);
    immediate_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    pass(blurTarget_[2].Get(), source, psDown_.Get(), 1.0F / width_, 1.0F / height_);
    pass(blurTarget_[1].Get(), blurView_[2].Get(), psBlur_.Get(), 1.0F / quarterW, 0.0F);
    pass(blurTarget_[0].Get(), blurView_[1].Get(), psBlur_.Get(), 0.0F, 1.0F / quarterH);
}

void Renderer::drawGlass(GlassShape const& shape)
{
    float const scale = dpi_ / 96.0F;
    Params params{};
    params.viewport[0] = static_cast<float>(width_);
    params.viewport[1] = static_cast<float>(height_);
    params.rect[0] = shape.rect.left * scale;
    params.rect[1] = shape.rect.top * scale;
    params.rect[2] = shape.rect.right * scale;
    params.rect[3] = shape.rect.bottom * scale;
    float const reach = kShadowReach * scale;
    params.quad[0] = params.rect[0] - reach;
    params.quad[1] = params.rect[1] - reach;
    params.quad[2] = params.rect[2] + reach;
    params.quad[3] = params.rect[3] + reach;
    params.radius = shape.radius * scale;
    params.band = shape.band * scale;
    params.refraction = shape.refraction * scale;
    params.dispersion = shape.dispersion;
    params.tint[0] = shape.tint.r;
    params.tint[1] = shape.tint.g;
    params.tint[2] = shape.tint.b;
    params.tint[3] = shape.tint.a;
    params.rimLight = shape.rimLight;
    params.rimDark = shape.rimDark;
    params.shadow = shape.shadow;
    params.clarity = shape.clarity;
    params.saturation = shape.saturation;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    immediate_->Map(params_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    std::memcpy(mapped.pData, &params, sizeof(params));
    immediate_->Unmap(params_.Get(), 0);
    immediate_->Draw(4, 0);
}

void Renderer::drawLayer(GlassLayer const& layer, RECT const& blurRegion, RECT const& region, ID3D11ShaderResourceView* source, ID3D11Texture2D* copySource, ID3D11Texture2D* target, ID3D11RenderTargetView* targetView, ID2D1Bitmap1* targetBitmap)
{
    if (!layer.glass.empty())
    {
        blur(source, blurRegion);
    }

    ID3D11RenderTargetView* const noTarget = nullptr;
    ID3D11ShaderResourceView* const none[2] = {};
    immediate_->PSSetShaderResources(0, 2, none);
    immediate_->OMSetRenderTargets(1, &noTarget, nullptr);
    D3D11_BOX const box{ static_cast<UINT>(region.left), static_cast<UINT>(region.top), 0, static_cast<UINT>(region.right), static_cast<UINT>(region.bottom), 1 };
    immediate_->CopySubresourceRegion(target, 0, box.left, box.top, 0, copySource, 0, &box);

    float const scale = dpi_ / 96.0F;
    if (!layer.glass.empty())
    {
        D3D11_VIEWPORT viewport{ 0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_), 0.0F, 1.0F };
        immediate_->RSSetViewports(1, &viewport);
        immediate_->RSSetScissorRects(1, &region);
        immediate_->OMSetRenderTargets(1, &targetView, nullptr);
        immediate_->OMSetBlendState(glassBlend_.Get(), nullptr, 0xFFFFFFFF);
        ID3D11ShaderResourceView* const views[2] = { source, blurView_[0].Get() };
        immediate_->PSSetShaderResources(0, 2, views);
        immediate_->PSSetShader(psGlass_.Get(), nullptr, 0);
        for (GlassShape const& shape : layer.glass)
        {
            if (overlaps(region, shapePixels(shape, scale, width_, height_)))
            {
                drawGlass(shape);
            }
        }
        immediate_->PSSetShaderResources(0, 2, none);
        immediate_->OMSetRenderTargets(1, &noTarget, nullptr);
    }

    if (layer.overlay)
    {
        D2D1_RECT_F const clip = D2D1::RectF(region.left / scale, region.top / scale, region.right / scale, region.bottom / scale);
        context_->SetTarget(targetBitmap);
        context_->BeginDraw();
        context_->SetTransform(D2D1::Matrix3x2F::Identity());
        context_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        layer.overlay(context_.Get(), clip);
        context_->SetTransform(D2D1::Matrix3x2F::Identity());
        context_->PopAxisAlignedClip();
        context_->EndDraw();
        context_->SetTarget(nullptr);
    }
}

bool Renderer::present(std::span<GlassLayer const> layers, D2D1_RECT_F const* changed)
{
    immediate_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    immediate_->IASetInputLayout(nullptr);
    immediate_->VSSetShader(vsQuad_.Get(), nullptr, 0);
    immediate_->VSSetConstantBuffers(0, 1, params_.GetAddressOf());
    immediate_->PSSetConstantBuffers(0, 1, params_.GetAddressOf());
    immediate_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    immediate_->RSSetState(raster_.Get());
    immediate_->OMSetDepthStencilState(nullptr, 0);

    if (layers.size() > 1 && composed_ == nullptr && !createComposed())
    {
        return false;
    }
    if (layers.size() <= 1 && composed_ != nullptr)
    {
        composedBitmap_.Reset();
        composedTarget_.Reset();
        composedView_.Reset();
        composed_.Reset();
    }

    RECT const whole{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
    bool const hasGlass = layers.size() == 1 && !layers[0].glass.empty();
    bool const partial = changed != nullptr && hasGlass && blurFresh_ && !targetsFresh_ && savePath_.empty();
    RECT const region = partial ? changedRegion(layers[0], *changed) : whole;
    if (isEmpty(region))
    {
        return true;
    }
    RECT painted = region;
    if (partial)
    {
        grow(painted, lastRegion_);
    }

    if (layers.size() > 1)
    {
        drawLayer(layers[0], whole, whole, sceneView_.Get(), scene_.Get(), composed_.Get(), composedTarget_.Get(), composedBitmap_.Get());
        drawLayer(layers[1], whole, whole, composedView_.Get(), composed_.Get(), back_.Get(), backView_.Get(), backTarget_.Get());
    }
    else if (!layers.empty())
    {
        drawLayer(layers[0], region, painted, sceneView_.Get(), scene_.Get(), back_.Get(), backView_.Get(), backTarget_.Get());
    }

    // Present hands the buffer away, so a requested capture has to happen first.
    if (!savePath_.empty())
    {
        writeFrame(savePath_);
        savePath_.clear();
    }

    lastRegion_ = region;
    targetsFresh_ = false;
    blurFresh_ = hasGlass;

    RECT dirty = region;
    DXGI_PRESENT_PARAMETERS parameters{};
    if (partial)
    {
        parameters.DirtyRectsCount = 1;
        parameters.pDirtyRects = &dirty;
    }
    return SUCCEEDED(swapChain_->Present1(1, 0, &parameters));
}

bool Renderer::writeFrame(std::wstring const& path)
{
    D3D11_TEXTURE2D_DESC desc{};
    back_->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging)))
    {
        return false;
    }
    immediate_->CopyResource(staging.Get(), back_.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(immediate_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    {
        return false;
    }

    ComPtr<IWICStream> stream;
    wic_->CreateStream(&stream);
    stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    ComPtr<IWICBitmapEncoder> encoder;
    wic_->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(desc.Width, desc.Height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&format);
    frame->WritePixels(desc.Height, mapped.RowPitch, mapped.RowPitch * desc.Height, static_cast<BYTE*>(mapped.pData));
    frame->Commit();
    encoder->Commit();
    immediate_->Unmap(staging.Get(), 0);
    return true;
}

void Renderer::trim()
{
    ComPtr<IDXGIDevice3> dxgi;
    if (SUCCEEDED(device_.As(&dxgi)))
    {
        immediate_->ClearState();
        immediate_->Flush();
        dxgi->Trim();
    }
    d2dDevice_->ClearResources();
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}
