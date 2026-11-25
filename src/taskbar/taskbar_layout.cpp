#include "taskbar/taskbar_layout.hpp"
#include <algorithm>
#include <uiautomation.h>
#include <wrl/client.h>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {

bool CreateReader(ComPtr<IUIAutomation>& automation,
                  ComPtr<IUIAutomationCondition>& controls,
                  ComPtr<IUIAutomationCacheRequest>& cache) {
    ComPtr<IUIAutomation2> client;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(client.GetAddressOf())))) return false;
    client->put_ConnectionTimeout(500);
    client->put_TransactionTimeout(500);
    if (FAILED(client.As(&automation))) return false;

    // Win11 task-list buttons are XAML elements, not individual child HWNDs.
    // Include other interactive items (Start, Search, Widgets, tray) as well.
    const CONTROLTYPEID types[] = {
        UIA_ButtonControlTypeId, UIA_ListItemControlTypeId,
        UIA_CheckBoxControlTypeId, UIA_RadioButtonControlTypeId,
        UIA_EditControlTypeId, UIA_ComboBoxControlTypeId,
        UIA_SplitButtonControlTypeId, UIA_MenuItemControlTypeId,
    };
    for (CONTROLTYPEID type : types) {
        VARIANT value{};
        value.vt = VT_I4;
        value.lVal = type;
        ComPtr<IUIAutomationCondition> condition;
        if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,
                value, condition.GetAddressOf()))) return false;
        if (!controls) controls = condition;
        else {
            ComPtr<IUIAutomationCondition> combined;
            if (FAILED(automation->CreateOrCondition(controls.Get(), condition.Get(),
                    combined.GetAddressOf()))) return false;
            controls = combined;
        }
    }
    if (FAILED(automation->CreateCacheRequest(cache.GetAddressOf()))) return false;
    cache->put_TreeScope(TreeScope_Element);
    cache->put_AutomationElementMode(AutomationElementMode_None);
    return SUCCEEDED(cache->AddProperty(UIA_BoundingRectanglePropertyId)) &&
           SUCCEEDED(cache->AddProperty(UIA_IsOffscreenPropertyId)) &&
           SUCCEEDED(cache->AddProperty(UIA_ProcessIdPropertyId));
}

bool ReadOccupied(IUIAutomation* automation, IUIAutomationCondition* controls,
                  IUIAutomationCacheRequest* cache, HWND taskbar,
                  const RECT& expectedBounds, UINT dpi, std::vector<RECT>& occupied) {
    RECT before{};
    if (!GetWindowRect(taskbar, &before) || !EqualRect(&before, &expectedBounds) ||
        GetDpiForWindow(taskbar) != dpi) return false;
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(taskbar, root.GetAddressOf()))) return false;
    ComPtr<IUIAutomationElementArray> elements;
    if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, controls, cache,
            elements.GetAddressOf()))) return false;
    int count = 0;
    if (FAILED(elements->get_Length(&count))) return false;
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        BOOL offscreen = TRUE;
        int pid = 0;
        RECT rect{}, clipped{};
        if (FAILED(elements->GetElement(i, element.GetAddressOf())) ||
            FAILED(element->get_CachedIsOffscreen(&offscreen)) ||
            FAILED(element->get_CachedProcessId(&pid)) ||
            FAILED(element->get_CachedBoundingRectangle(&rect))) return false;
        if (!offscreen && static_cast<DWORD>(pid) != GetCurrentProcessId() &&
            IntersectRect(&clipped, &rect, &expectedBounds)) {
            occupied.push_back(clipped);
        }
    }
    RECT after{};
    // An empty tree or a changing taskbar is not evidence of empty space.
    return !occupied.empty() && GetWindowRect(taskbar, &after) &&
           EqualRect(&after, &expectedBounds) && GetDpiForWindow(taskbar) == dpi;
}

} // namespace

TaskbarLayoutReader::TaskbarLayoutReader() : m_thread(&TaskbarLayoutReader::Run, this) {}

TaskbarLayoutReader::~TaskbarLayoutReader() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_one();
    m_thread.join();
}

bool TaskbarLayoutReader::SameDisplay(const Layout& a, const Layout& b) {
    return a.taskbar == b.taskbar && a.dpi == b.dpi && EqualRect(&a.bounds, &b.bounds);
}

bool TaskbarLayoutReader::HasPendingLocked() const {
    for (const auto& entry : m_entries) {
        if (entry.pending) return true;
    }
    return false;
}

void TaskbarLayoutReader::Prune(const std::vector<HWND>& activeTaskbars) {
    std::lock_guard lock(m_mutex);
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            return std::find(activeTaskbars.begin(), activeTaskbars.end(), e.request.taskbar) == activeTaskbars.end();
        }),
        m_entries.end());
    if (m_entries.empty()) {
        m_nextCursor = 0;
    } else if (m_nextCursor >= m_entries.size()) {
        m_nextCursor = 0;
    }
}

bool TaskbarLayoutReader::Read(HWND taskbar, const RECT& bounds, UINT dpi,
                              std::vector<RECT>& occupied) {
    Layout requested;
    requested.taskbar = taskbar;
    requested.bounds = bounds;
    requested.dpi = dpi;
    ULONGLONG now = GetTickCount64();
    std::lock_guard lock(m_mutex);
    auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
        return e.request.taskbar == taskbar;
    });
    if (it == m_entries.end()) {
        Entry fresh;
        fresh.request.taskbar = taskbar;
        m_entries.push_back(std::move(fresh));
        it = std::prev(m_entries.end());
    }
    bool displayChanged = !SameDisplay(requested, it->request);
    if (displayChanged) it->result.valid = false;
    if (displayChanged || now - it->lastRequest >= 1000) {
        it->request = requested;
        it->lastRequest = now;
        it->pending = true;
        m_wake.notify_one();
    }
    if (!it->result.valid || !SameDisplay(requested, it->result)) return false;
    occupied = it->result.occupied;
    return true;
}

void TaskbarLayoutReader::Run() {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IUIAutomation> automation;
        ComPtr<IUIAutomationCondition> controls;
        ComPtr<IUIAutomationCacheRequest> cache;
        for (;;) {
            Layout result;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [&] { return m_stop || HasPendingLocked(); });
                if (m_stop) break;
                const size_t n = m_entries.size();
                for (size_t offset = 0; offset < n; ++offset) {
                    size_t idx = (m_nextCursor + offset) % n;
                    if (m_entries[idx].pending) {
                        result = m_entries[idx].request;
                        m_entries[idx].pending = false;
                        m_nextCursor = (idx + 1) % n;
                        break;
                    }
                }
                if (!result.taskbar) continue;
            }
            if (SUCCEEDED(com) && !automation) {
                if (!CreateReader(automation, controls, cache)) {
                    automation.Reset();
                    controls.Reset();
                    cache.Reset();
                }
            }
            result.valid = automation && ReadOccupied(automation.Get(), controls.Get(),
                cache.Get(), result.taskbar, result.bounds, result.dpi, result.occupied);
            std::lock_guard lock(m_mutex);
            // Explorer can temporarily stop answering UI Automation while a
            // tray popup is open. Keep the last successful layout for this
            // taskbar; only a successful read may replace it. Read() already
            // invalidates it when the taskbar handle, bounds or DPI changes.
            auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) {
                return e.request.taskbar == result.taskbar;
            });
            if (it != m_entries.end() && SameDisplay(result, it->request) && result.valid) {
                it->result = std::move(result);
            }
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
}
