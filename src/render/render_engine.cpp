#include "render/render_engine.hpp"
#include "render/palette.hpp"
#include <algorithm>
#include <cwchar>

RenderEngine::RenderEngine() = default;

RenderEngine::~RenderEngine() {
    ResetDCompDevice();
    ReleaseWicTarget(m_submenuTarget);
    ReleaseWicTarget(m_menuTarget);
    ReleaseWicTarget(m_flyoutTarget);
    ReleaseWicTarget(m_widgetTarget);
    SafeRelease(m_fmtFlyBtn);
    SafeRelease(m_fmtFlyCardBig);
    SafeRelease(m_fmtFlyCardLabel);
    SafeRelease(m_fmtFlySub);
    SafeRelease(m_fmtFlyTitle);
    SafeRelease(m_fmtMenuText);
    SafeRelease(m_fmtMinSub);
    SafeRelease(m_fmtMinValue);
    SafeRelease(m_fmtMinLabel);
    SafeRelease(m_fmtValue);
    SafeRelease(m_fmtLabel);
    SafeRelease(m_tabularTypo);
    SafeRelease(m_wicFactory);
    SafeRelease(m_dwFactory);
    SafeRelease(m_d2dFactory);
}

void RenderEngine::ReleaseWicTarget(WicTarget& target) {
    SafeRelease(target.rt);
    SafeRelease(target.bitmap);
    target.width = 0;
    target.height = 0;
}

bool RenderEngine::Initialize() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1), nullptr, reinterpret_cast<void**>(&m_d2dFactory)))) {
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&m_dwFactory)))) {
        return false;
    }
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_wicFactory)))) {
        return false;
    }

    if (SUCCEEDED(m_dwFactory->CreateTypography(&m_tabularTypo))) {
        DWRITE_FONT_FEATURE feat{ DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1 };
        m_tabularTypo->AddFontFeature(feat);
    }

    const wchar_t* fontPrimary = L"Segoe UI Variable Text";
    auto makeFmt = [&](float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align,
                       DWRITE_PARAGRAPH_ALIGNMENT valign, IDWriteTextFormat** out) {
        if (FAILED(m_dwFactory->CreateTextFormat(fontPrimary, nullptr, weight,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", out))) {
            return false;
        }
        (*out)->SetTextAlignment(align);
        (*out)->SetParagraphAlignment(valign);
        (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        return true;
    };

    if (!makeFmt(10.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtLabel)) return false;
    if (!makeFmt(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtValue)) return false;
    if (!makeFmt(9.75f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtMinLabel)) return false;
    if (!makeFmt(11.0f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtMinValue)) return false;
    if (!makeFmt(9.75f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtMinSub)) return false;
    if (!makeFmt(11.25f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtMenuText)) return false;
    if (!makeFmt(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyTitle)) return false;
    if (!makeFmt(10.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlySub)) return false;
    if (!makeFmt(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyCardLabel)) return false;
    if (!makeFmt(18.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyCardBig)) return false;
    if (!makeFmt(11.0f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyBtn)) return false;

    return true;
}

bool RenderEngine::EnsureWicTarget(WicTarget& target, int widthPx, int heightPx, UINT dpi) {
    if (!m_wicFactory || !m_d2dFactory || widthPx <= 0 || heightPx <= 0) return false;
    if (target.bitmap && target.rt && target.width == widthPx && target.height == heightPx) {
        target.rt->SetDpi(static_cast<FLOAT>(dpi), static_cast<FLOAT>(dpi));
        return true;
    }

    ReleaseWicTarget(target);
    target.width = widthPx;
    target.height = heightPx;

    if (FAILED(m_wicFactory->CreateBitmap(static_cast<UINT>(widthPx), static_cast<UINT>(heightPx),
            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &target.bitmap))) {
        ReleaseWicTarget(target);
        return false;
    }

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<FLOAT>(dpi), static_cast<FLOAT>(dpi));
    if (FAILED(m_d2dFactory->CreateWicBitmapRenderTarget(target.bitmap, &props, &target.rt))) {
        ReleaseWicTarget(target);
        return false;
    }
    target.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

void RenderEngine::DrawTextFormatted(
    ID2D1RenderTarget* rt,
    const wchar_t* text,
    IDWriteTextFormat* fmt,
    const D2D1_RECT_F& rect,
    ID2D1Brush* brush,
    bool tabularFigures) {
    if (!text || !*text || !fmt || !brush) return;
    UINT32 len = static_cast<UINT32>(wcslen(text));
    float maxW = std::max(1.0f, rect.right - rect.left);
    float maxH = std::max(1.0f, rect.bottom - rect.top);

    IDWriteTextLayout* layout = nullptr;
    if (SUCCEEDED(m_dwFactory->CreateTextLayout(text, len, fmt, maxW, maxH, &layout)) && layout) {
        if (tabularFigures && m_tabularTypo) {
            layout->SetTypography(m_tabularTypo, DWRITE_TEXT_RANGE{ 0, len });
        }
        DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        layout->SetTrimming(&trim, nullptr);
        rt->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout, brush);
        layout->Release();
    }
}

float RenderEngine::MeasureTextWidth(
    const wchar_t* text,
    IDWriteTextFormat* fmt,
    bool tabularFigures) const {
    if (!text || !*text || !fmt || !m_dwFactory) return 0.0f;
    UINT32 len = static_cast<UINT32>(wcslen(text));
    IDWriteTextLayout* layout = nullptr;
    float width = 0.0f;
    if (SUCCEEDED(m_dwFactory->CreateTextLayout(text, len, fmt, 200.0f, 32.0f, &layout)) && layout) {
        if (tabularFigures && m_tabularTypo) {
            layout->SetTypography(m_tabularTypo, DWRITE_TEXT_RANGE{ 0, len });
        }
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(layout->GetMetrics(&tm))) {
            width = tm.widthIncludingTrailingWhitespace;
        }
        layout->Release();
    }
    return width;
}

void RenderEngine::ResetWidgetTarget() {
    SafeRelease(m_dcompSurface);
    SafeRelease(m_dcompVisual);
    SafeRelease(m_dcompTarget);
    m_dcompHwnd = nullptr;
    m_dcompW = 0;
    m_dcompH = 0;
}

void RenderEngine::ResetDCompDevice() {
    ResetWidgetTarget();
    SafeRelease(m_d2dDc);
    SafeRelease(m_dcompDevice);
    SafeRelease(m_d2dDevice);
    SafeRelease(m_d3dDevice);
}

bool RenderEngine::EnsureDCompDevice() {
    if (m_dcompDevice && m_d2dDc && m_d2dDevice && m_d3dDevice) {
        return true;
    }
    ResetDCompDevice();
    if (!m_d2dFactory) return false;

    D3D_FEATURE_LEVEL fl{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;
    // Prefer WARP for D3D11 so the helper doesn't pull heavy GPU driver private heaps (~4 MB vs ~55 MB);
    // DirectComposition still composites the resulting surface on the GPU inside DWM.
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION, &m_d3dDevice, &fl, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION, &m_d3dDevice, &fl, nullptr);
    }
    if (FAILED(hr) || !m_d3dDevice) {
        ResetDCompDevice();
        return false;
    }

    IDXGIDevice* dxgiDev = nullptr;
    hr = m_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDev));
    if (FAILED(hr) || !dxgiDev) {
        ResetDCompDevice();
        return false;
    }

    hr = m_d2dFactory->CreateDevice(dxgiDev, &m_d2dDevice);
    if (SUCCEEDED(hr) && m_d2dDevice) {
        hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dDc);
    }
    if (SUCCEEDED(hr) && m_d2dDc) {
        hr = DCompositionCreateDevice(dxgiDev, IID_PPV_ARGS(&m_dcompDevice));
    }
    SafeRelease(dxgiDev);

    if (FAILED(hr) || !m_d2dDc || !m_dcompDevice) {
        ResetDCompDevice();
        return false;
    }
    return true;
}

bool RenderEngine::EnsureDCompSurface(HWND hwnd, int widthPx, int heightPx) {
    if (!hwnd || widthPx <= 0 || heightPx <= 0) return false;
    if (!EnsureDCompDevice()) return false;

    if (m_dcompHwnd != hwnd || !m_dcompTarget || !m_dcompVisual) {
        ResetWidgetTarget();
        if (FAILED(m_dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &m_dcompTarget)) || !m_dcompTarget) {
            ResetWidgetTarget();
            return false;
        }
        if (FAILED(m_dcompDevice->CreateVisual(&m_dcompVisual)) || !m_dcompVisual) {
            ResetWidgetTarget();
            return false;
        }
        if (FAILED(m_dcompTarget->SetRoot(m_dcompVisual))) {
            ResetWidgetTarget();
            return false;
        }
        m_dcompHwnd = hwnd;
    }

    if (!m_dcompSurface || m_dcompW != widthPx || m_dcompH != heightPx) {
        SafeRelease(m_dcompSurface);
        if (FAILED(m_dcompDevice->CreateSurface(
                static_cast<UINT>(widthPx), static_cast<UINT>(heightPx),
                DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
                &m_dcompSurface)) || !m_dcompSurface) {
            m_dcompW = 0;
            m_dcompH = 0;
            return false;
        }
        if (FAILED(m_dcompVisual->SetContent(m_dcompSurface))) {
            SafeRelease(m_dcompSurface);
            m_dcompW = 0;
            m_dcompH = 0;
            return false;
        }
        m_dcompW = widthPx;
        m_dcompH = heightPx;
    }
    return true;
}

bool RenderEngine::RenderWidgetViaDComp(
    HWND hwnd,
    int widthPx,
    int heightPx,
    UINT dpi,
    const WidgetDimensions& dim,
    const AppConfig& cfg,
    const SystemSnapshot& snap,
    const HistoryBuffer& history,
    const GoldQuote& gold,
    bool isHovered,
    bool isPressed) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!EnsureDCompSurface(hwnd, widthPx, heightPx)) {
            ResetDCompDevice();
            continue;
        }

        IDXGISurface* dxgiSurf = nullptr;
        POINT offset{};
        HRESULT hr = m_dcompSurface->BeginDraw(nullptr, IID_PPV_ARGS(&dxgiSurf), &offset);
        if (FAILED(hr) || !dxgiSurf) {
            SafeRelease(dxgiSurf);
            ResetDCompDevice();
            continue;
        }

        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<FLOAT>(dpi), static_cast<FLOAT>(dpi));
        ID2D1Bitmap1* d2dBmp = nullptr;
        hr = m_d2dDc->CreateBitmapFromDxgiSurface(dxgiSurf, &bp, &d2dBmp);
        if (FAILED(hr) || !d2dBmp) {
            SafeRelease(d2dBmp);
            SafeRelease(dxgiSurf);
            m_dcompSurface->EndDraw();
            ResetDCompDevice();
            continue;
        }

        m_d2dDc->SetTarget(d2dBmp);
        m_d2dDc->SetDpi(static_cast<FLOAT>(dpi), static_cast<FLOAT>(dpi));
        m_d2dDc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        float scale = static_cast<float>(dpi) / 96.0f;
        float offX = static_cast<float>(offset.x) / scale;
        float offY = static_cast<float>(offset.y) / scale;

        m_d2dDc->BeginDraw();
        m_d2dDc->SetTransform(D2D1::Matrix3x2F::Translation(offX, offY));
        m_d2dDc->PushAxisAlignedClip(
            D2D1::RectF(0.0f, 0.0f, static_cast<float>(widthPx) / scale, static_cast<float>(heightPx) / scale),
            D2D1_ANTIALIAS_MODE_ALIASED);
        m_d2dDc->Clear(D2D1::ColorF(0, 0, 0, 0));

        DrawWidgetContent(m_d2dDc, dim, cfg, snap, history, gold, isHovered, isPressed);

        m_d2dDc->PopAxisAlignedClip();
        m_d2dDc->SetTransform(D2D1::Matrix3x2F::Identity());
        HRESULT hrEnd = m_d2dDc->EndDraw();
        m_d2dDc->SetTarget(nullptr);

        SafeRelease(d2dBmp);
        SafeRelease(dxgiSurf);

        HRESULT hrSurfEnd = m_dcompSurface->EndDraw();
        HRESULT hrCommit = m_dcompDevice->Commit();

        if (SUCCEEDED(hrEnd) && SUCCEEDED(hrSurfEnd) && SUCCEEDED(hrCommit)) {
            return true;
        }
        ResetDCompDevice();
    }
    return false;
}
