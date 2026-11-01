#pragma once

#include <functional>
#include <span>
#include <string>

#include <d2d1_3.h>
#include <d3d11_4.h>
#include <dwrite_3.h>
#include <dxgi1_6.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

/// One sheet of glass, in device-independent pixels.
struct GlassShape
{
    D2D1_RECT_F rect{};
    float radius = 22.0F;
    float band = 16.0F;
    float refraction = 14.0F;
    float dispersion = 0.18F;
    D2D1_COLOR_F tint{ 1.0F, 1.0F, 1.0F, 0.06F };
    float rimLight = 0.34F;
    float rimDark = 0.10F;
    float shadow = 0.28F;
    float clarity = 0.12F;
    float saturation = 1.25F;
};

/// One tier of the window. A later layer's glass refracts everything in the layers before it.
struct GlassLayer
{
    std::span<GlassShape const> glass;
    std::function<void(ID2D1DeviceContext*, D2D1_RECT_F)> overlay;
};

/// Draws the UI with Direct2D, then lenses it with a small D3D11 pass. Nothing runs between frames.
class Renderer
{
public:
    bool init(HWND window);
    bool resize(UINT width, UINT height, float dpi);

    /// Starts drawing the layer that glass refracts.
    ID2D1DeviceContext* beginScene();
    void endScene();

    /// Lenses the scene layer by layer, at most two: the window, then a panel over it. `changed` is what
    /// differs from the last frame, in DIPs, and null means everything. A frame costs what it changes,
    /// which matters because all of this runs on the CPU.
    bool present(std::span<GlassLayer const> layers, D2D1_RECT_F const* changed = nullptr);

    /// Writes the next frame to a PNG, which also works while the window is covered.
    void saveNextFrame(std::wstring path) { savePath_ = std::move(path); }

    /// Frees what can be rebuilt while nothing is visible.
    void trim();

    ID2D1DeviceContext* d2d() const { return context_.Get(); }
    IDWriteFactory5* dwrite() const { return dwrite_.Get(); }
    IWICImagingFactory* wic() const { return wic_.Get(); }
    float dpi() const { return dpi_; }

private:
    bool createTargets();
    void releaseTargets();
    bool createComposed();
    [[nodiscard]] RECT changedRegion(GlassLayer const& layer, D2D1_RECT_F const& changed) const;
    void blur(ID3D11ShaderResourceView* source, RECT const& region);
    void drawLayer(GlassLayer const& layer, RECT const& blurRegion, RECT const& region, ID3D11ShaderResourceView* source, ID3D11Texture2D* copySource, ID3D11Texture2D* target, ID3D11RenderTargetView* targetView, ID2D1Bitmap1* targetBitmap);
    void drawGlass(GlassShape const& shape);
    bool writeFrame(std::wstring const& path);

    std::wstring savePath_;
    HWND window_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    float dpi_ = 96.0F;

    // A partial frame relies on the last one: the other swap chain buffer is two frames old, so it needs
    // the last frame's region too, and the blur only stays whole if the last frame left it on this scene.
    RECT lastRegion_{};
    bool targetsFresh_ = true;
    bool blurFresh_ = false;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> immediate_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID2D1Factory3> d2dFactory_;
    ComPtr<ID2D1Device2> d2dDevice_;
    ComPtr<ID2D1DeviceContext> context_;
    ComPtr<IDWriteFactory5> dwrite_;
    ComPtr<IWICImagingFactory> wic_;

    ComPtr<ID3D11VertexShader> vsQuad_;
    ComPtr<ID3D11PixelShader> psBlur_;
    ComPtr<ID3D11PixelShader> psDown_;
    ComPtr<ID3D11PixelShader> psGlass_;
    ComPtr<ID3D11Buffer> params_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11BlendState> glassBlend_;

    ComPtr<ID3D11Texture2D> scene_;
    ComPtr<ID3D11ShaderResourceView> sceneView_;
    ComPtr<ID2D1Bitmap1> sceneTarget_;
    ComPtr<ID3D11Texture2D> back_;
    ComPtr<ID3D11RenderTargetView> backView_;
    ComPtr<ID2D1Bitmap1> backTarget_;

    // Only exists while a panel is open, so an ordinary frame doesn't pay for it.
    ComPtr<ID3D11Texture2D> composed_;
    ComPtr<ID3D11ShaderResourceView> composedView_;
    ComPtr<ID3D11RenderTargetView> composedTarget_;
    ComPtr<ID2D1Bitmap1> composedBitmap_;

    // Quarter size, so the blur costs a sixteenth of the window. The result, the horizontal pass and the
    // downsample each keep their own texture, so a partial blur can read valid pixels past its edge.
    ComPtr<ID3D11Texture2D> blur_[3];
    ComPtr<ID3D11ShaderResourceView> blurView_[3];
    ComPtr<ID3D11RenderTargetView> blurTarget_[3];
};
