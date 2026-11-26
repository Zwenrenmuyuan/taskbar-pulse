#include "render/render_engine.hpp"
#include "render/draw_primitives.hpp"
#include "render/palette.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {

IDCompositionAnimation* CreateEaseOutCubicAnim(IDCompositionDevice* dev, float from, float to, float dur) {
    if (!dev || dur <= 0.001f) return nullptr;
    IDCompositionAnimation* anim = nullptr;
    if (FAILED(dev->CreateAnimation(&anim)) || !anim) return nullptr;
    // Front-loaded C1-continuous two-segment Fluent decelerate curve (zero overshoot):
    // reaches 76% progress in the first 35% of duration, then feathers smoothly to 100%.
    const float delta = to - from;
    const float t1 = dur * 0.35f;
    const float t2 = dur - t1;
    const float invT1 = 1.0f / t1;
    const float invT1_2 = invT1 * invT1;
    const float invT1_3 = invT1_2 * invT1;
    const float invT2 = 1.0f / t2;
    const float invT2_2 = invT2 * invT2;
    const float invT2_3 = invT2_2 * invT2;

    constexpr float kMid = 0.76f;
    constexpr float kRem = 1.0f - kMid;
    constexpr float kC1 = 1.65f;
    constexpr float kC2 = -1.4077f;
    constexpr float kC3 = 0.5177f;

    anim->AddCubic(0.0, from, delta * kC1 * invT1, delta * kC2 * invT1_2, delta * kC3 * invT1_3);
    anim->AddCubic(static_cast<double>(t1),
        from + delta * kMid,
        delta * kRem * 3.0f * invT2,
        -delta * kRem * 3.0f * invT2_2,
        delta * kRem * invT2_3);
    anim->End(static_cast<double>(dur), to);
    return anim;
}

IDCompositionAnimation* CreateEaseInQuadAnim(IDCompositionDevice* dev, float from, float to, float dur) {
    if (!dev || dur <= 0.001f) return nullptr;
    IDCompositionAnimation* anim = nullptr;
    if (FAILED(dev->CreateAnimation(&anim)) || !anim) return nullptr;
    float delta = to - from;
    float invD = 1.0f / dur;
    // f(t) = from + delta * (t/D)^2
    anim->AddCubic(0.0, from, 0.0f, delta * invD * invD, 0.0f);
    anim->End(static_cast<double>(dur), to);
    return anim;
}

} // namespace

RenderEngine::RenderEngine() = default;

RenderEngine::~RenderEngine() {
    ResetDCompDevice();
    ReleaseWicTarget(m_submenuTarget);
    ReleaseWicTarget(m_menuTarget);
    ReleaseWicTarget(m_flyoutTarget);
    ReleaseWicTarget(m_widgetTarget);
    SafeRelease(m_fmtFlyBtn);
    SafeRelease(m_fmtFlyBadge);
    SafeRelease(m_fmtFlyUnit);
    SafeRelease(m_fmtFlyCardBig);
    SafeRelease(m_fmtFlyCardLabel);
    SafeRelease(m_fmtFlyTiny);
    SafeRelease(m_fmtFlySub);
    SafeRelease(m_fmtFlyTitle);
    SafeRelease(m_fmtMenuText);
    SafeRelease(m_fmtMinSubVal);
    SafeRelease(m_fmtMinSub);
    SafeRelease(m_fmtMinValue);
    SafeRelease(m_fmtMinLabel);
    SafeRelease(m_fmtValue);
    SafeRelease(m_fmtLabel);
    SafeRelease(m_roundStroke);
    SafeRelease(m_tabularTypo);
    SafeRelease(m_wicFactory);
    SafeRelease(m_dwFactory);
    SafeRelease(m_d2dFactory);
}

void RenderEngine::ReleaseWicTarget(WicTarget& target) {
    SafeRelease(target.brAux);
    SafeRelease(target.brMain);
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

    m_roundStroke = CreateRoundStrokeStyle(m_d2dFactory);

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

    if (!makeFmt(9.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtLabel)) return false;
    if (!makeFmt(11.4f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtValue)) return false;
    if (!makeFmt(9.25f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtMinLabel)) return false;
    if (!makeFmt(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtMinValue)) return false;
    if (!makeFmt(9.4f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtMinSub)) return false;
    if (!makeFmt(10.25f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtMinSubVal)) return false;
    if (!makeFmt(11.25f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtMenuText)) return false;
    if (!makeFmt(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyTitle)) return false;
    if (!makeFmt(11.25f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlySub)) return false;
    if (!makeFmt(10.25f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyTiny)) return false;
    if (!makeFmt(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyCardLabel)) return false;
    if (!makeFmt(21.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyCardBig)) return false;
    if (!makeFmt(11.5f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyUnit)) return false;
    if (!makeFmt(10.25f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyBadge)) return false;
    if (!makeFmt(10.75f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &m_fmtFlyBtn)) return false;

    // Eagerly initialize DirectComposition so popup windows can opt into WS_EX_NOREDIRECTIONBITMAP.
    EnsureDCompDevice();

    return true;
}

bool RenderEngine::EnsureWicTarget(WicTarget& target, int widthPx, int heightPx, UINT dpi) {
    if (!m_wicFactory || !m_d2dFactory || widthPx <= 0 || heightPx <= 0) return false;
    if (target.bitmap && target.rt && target.brMain && target.brAux &&
        target.width == widthPx && target.height == heightPx) {
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
    if (FAILED(target.rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &target.brMain)) ||
        FAILED(target.rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &target.brAux))) {
        ReleaseWicTarget(target);
        return false;
    }
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

float RenderEngine::MeasureMenuLabelWidth(const wchar_t* text) const {
    if (!text || !*text || !m_fmtMenuText || !m_dwFactory) return 0.0f;
    UINT32 len = static_cast<UINT32>(wcslen(text));
    IDWriteTextLayout* layout = nullptr;
    float width = 0.0f;
    if (SUCCEEDED(m_dwFactory->CreateTextLayout(text, len, m_fmtMenuText, 600.0f, 32.0f, &layout)) && layout) {
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(layout->GetMetrics(&tm))) {
            width = tm.widthIncludingTrailingWhitespace;
        }
        layout->Release();
    }
    return width;
}

void RenderEngine::ReleaseDCompTargetEntry(DCompTargetEntry& entry) {
    SafeRelease(entry.transformGroup);
    SafeRelease(entry.scale);
    SafeRelease(entry.translate);
    SafeRelease(entry.effectGroup);
    SafeRelease(entry.surface);
    SafeRelease(entry.visual);
    SafeRelease(entry.target);
    entry.hwnd = nullptr;
    entry.width = 0;
    entry.height = 0;
}

void RenderEngine::ResetWidgetTarget(HWND hwnd) {
    if (!hwnd) {
        for (auto& entry : m_dcompTargets) {
            ReleaseDCompTargetEntry(entry);
        }
        m_dcompTargets.clear();
        return;
    }
    for (auto it = m_dcompTargets.begin(); it != m_dcompTargets.end(); ++it) {
        if (it->hwnd == hwnd) {
            ReleaseDCompTargetEntry(*it);
            m_dcompTargets.erase(it);
            break;
        }
    }
}

void RenderEngine::ResetDCompDevice() {
    ResetWidgetTarget(nullptr);
    SafeRelease(m_dcompBrAux);
    SafeRelease(m_dcompBrMain);
    SafeRelease(m_d2dDc);
    SafeRelease(m_dcompDevice);
    SafeRelease(m_d2dDevice);
    SafeRelease(m_d3dDevice);
}

bool RenderEngine::EnsureDCompDevice() {
    if (m_dcompDevice && m_d2dDc && m_d2dDevice && m_d3dDevice && m_dcompBrMain && m_dcompBrAux) {
        return true;
    }
    ResetDCompDevice();
    if (!m_d2dFactory) return false;

    D3D_FEATURE_LEVEL fl{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;
    // Prefer WARP for D3D11 so the helper doesn't pull heavy GPU driver private heaps (~4 MB vs ~55 MB);
    // DirectComposition still composites the resulting surface and runs Visual animations on the GPU inside DWM.
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
        hr = m_d2dDc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &m_dcompBrMain);
    }
    if (SUCCEEDED(hr) && m_d2dDc) {
        hr = m_d2dDc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &m_dcompBrAux);
    }
    if (SUCCEEDED(hr) && m_d2dDc) {
        hr = DCompositionCreateDevice(dxgiDev, IID_PPV_ARGS(&m_dcompDevice));
    }
    SafeRelease(dxgiDev);

    if (FAILED(hr) || !m_d2dDc || !m_dcompDevice || !m_dcompBrMain || !m_dcompBrAux) {
        ResetDCompDevice();
        return false;
    }
    return true;
}

RenderEngine::DCompTargetEntry* RenderEngine::FindDCompEntry(HWND hwnd) {
    for (auto& existing : m_dcompTargets) {
        if (existing.hwnd == hwnd) return &existing;
    }
    return nullptr;
}

RenderEngine::DCompTargetEntry* RenderEngine::EnsureDCompSurface(HWND hwnd, int widthPx, int heightPx) {
    if (!hwnd || widthPx <= 0 || heightPx <= 0) return nullptr;
    if (!EnsureDCompDevice()) return nullptr;

    DCompTargetEntry* entry = FindDCompEntry(hwnd);

    if (!entry) {
        DCompTargetEntry created;
        created.hwnd = hwnd;
        if (FAILED(m_dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &created.target)) || !created.target) {
            ReleaseDCompTargetEntry(created);
            return nullptr;
        }
        if (FAILED(m_dcompDevice->CreateVisual(&created.visual)) || !created.visual) {
            ReleaseDCompTargetEntry(created);
            return nullptr;
        }
        created.visual->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR);

        if (SUCCEEDED(m_dcompDevice->CreateEffectGroup(&created.effectGroup)) && created.effectGroup) {
            created.effectGroup->SetOpacity(1.0f);
            created.visual->SetEffect(created.effectGroup);
        }
        if (SUCCEEDED(m_dcompDevice->CreateScaleTransform(&created.scale)) &&
            SUCCEEDED(m_dcompDevice->CreateTranslateTransform(&created.translate)) &&
            created.scale && created.translate) {
            created.scale->SetScaleX(1.0f);
            created.scale->SetScaleY(1.0f);
            created.translate->SetOffsetX(0.0f);
            created.translate->SetOffsetY(0.0f);
            IDCompositionTransform* xforms[2] = { created.scale, created.translate };
            if (SUCCEEDED(m_dcompDevice->CreateTransformGroup(xforms, 2, &created.transformGroup)) &&
                created.transformGroup) {
                created.visual->SetTransform(created.transformGroup);
            }
        }

        if (FAILED(created.target->SetRoot(created.visual))) {
            ReleaseDCompTargetEntry(created);
            return nullptr;
        }
        m_dcompTargets.push_back(created);
        entry = &m_dcompTargets.back();
    }

    if (!entry->surface || entry->width != widthPx || entry->height != heightPx) {
        SafeRelease(entry->surface);
        if (FAILED(m_dcompDevice->CreateSurface(
                static_cast<UINT>(widthPx), static_cast<UINT>(heightPx),
                DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
                &entry->surface)) || !entry->surface) {
            entry->width = 0;
            entry->height = 0;
            return nullptr;
        }
        if (FAILED(entry->visual->SetContent(entry->surface))) {
            SafeRelease(entry->surface);
            entry->width = 0;
            entry->height = 0;
            return nullptr;
        }
        entry->width = widthPx;
        entry->height = heightPx;
    }
    return entry;
}

bool RenderEngine::UsesDCompWindow(HWND hwnd) const {
    if (!hwnd) return false;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_NOREDIRECTIONBITMAP) != 0) return true;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    return (style & WS_CHILD) != 0;
}

void RenderEngine::SyncPopupWindowBounds(HWND hwnd, POINT ptScreen, int widthPx, int heightPx) {
    if (!hwnd) return;
    RECT rc{};
    if (GetWindowRect(hwnd, &rc) &&
        rc.left == ptScreen.x &&
        rc.top == ptScreen.y &&
        (rc.right - rc.left) == widthPx &&
        (rc.bottom - rc.top) == heightPx) {
        return;
    }
    SetWindowPos(hwnd, nullptr, ptScreen.x, ptScreen.y, widthPx, heightPx,
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
}

bool RenderEngine::AnimateWindowVisual(HWND hwnd, UINT dpi, const PopupMotionSpec& spec) {
    if (!hwnd || !m_dcompDevice) return false;
    DCompTargetEntry* entry = FindDCompEntry(hwnd);
    if (!entry) return false;

    const float scale = static_cast<float>(dpi ? dpi : 96) / 96.0f;
    const float dur = std::max(0.02f, spec.durationSec);

    auto makeMotion = [&](float from, float to, bool easeOutCurve) -> IDCompositionAnimation* {
        return easeOutCurve
            ? CreateEaseOutCubicAnim(m_dcompDevice, from, to, dur)
            : CreateEaseInQuadAnim(m_dcompDevice, from, to, dur);
    };

    if (entry->effectGroup) {
        if (std::fabs(spec.toOpacity - spec.fromOpacity) > 0.001f) {
            // Opacity always uses ease-out cubic so both reveal and dismiss feel responsive from frame 1.
            if (IDCompositionAnimation* a = CreateEaseOutCubicAnim(m_dcompDevice, spec.fromOpacity, spec.toOpacity, dur)) {
                entry->effectGroup->SetOpacity(a);
                a->Release();
            }
        } else {
            entry->effectGroup->SetOpacity(spec.toOpacity);
        }
    }

    if (entry->translate) {
        float fx = spec.fromOffsetXDip * scale;
        float tx = spec.toOffsetXDip * scale;
        float fy = spec.fromOffsetYDip * scale;
        float ty = spec.toOffsetYDip * scale;
        if (std::fabs(tx - fx) > 0.1f) {
            if (IDCompositionAnimation* a = makeMotion(fx, tx, spec.easeOut)) {
                entry->translate->SetOffsetX(a);
                a->Release();
            }
        } else {
            entry->translate->SetOffsetX(tx);
        }
        if (std::fabs(ty - fy) > 0.1f) {
            if (IDCompositionAnimation* a = makeMotion(fy, ty, spec.easeOut)) {
                entry->translate->SetOffsetY(a);
                a->Release();
            }
        } else {
            entry->translate->SetOffsetY(ty);
        }
    }

    if (entry->scale) {
        entry->scale->SetCenterX(static_cast<float>(entry->width) * spec.centerXRatio);
        entry->scale->SetCenterY(static_cast<float>(entry->height) * spec.centerYRatio);
        if (std::fabs(spec.toScale - spec.fromScale) > 0.0005f) {
            if (IDCompositionAnimation* ax = makeMotion(spec.fromScale, spec.toScale, spec.easeOut)) {
                entry->scale->SetScaleX(ax);
                ax->Release();
            }
            if (IDCompositionAnimation* ay = makeMotion(spec.fromScale, spec.toScale, spec.easeOut)) {
                entry->scale->SetScaleY(ay);
                ay->Release();
            }
        } else {
            entry->scale->SetScaleX(spec.toScale);
            entry->scale->SetScaleY(spec.toScale);
        }
    }

    return SUCCEEDED(m_dcompDevice->Commit());
}

bool RenderEngine::ResetWindowVisual(HWND hwnd) {
    if (!hwnd || !m_dcompDevice) return false;
    DCompTargetEntry* entry = FindDCompEntry(hwnd);
    if (!entry) return false;
    if (entry->effectGroup) entry->effectGroup->SetOpacity(1.0f);
    if (entry->translate) {
        entry->translate->SetOffsetX(0.0f);
        entry->translate->SetOffsetY(0.0f);
    }
    if (entry->scale) {
        entry->scale->SetScaleX(1.0f);
        entry->scale->SetScaleY(1.0f);
    }
    return SUCCEEDED(m_dcompDevice->Commit());
}
