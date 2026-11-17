#include "collectors/gold_price.hpp"
#include <winhttp.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

using HttpHandle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;

// Only the two scalar fields used by the fixed Gold API response are needed.
const char* JsonValue(const std::string& body, const char* key) {
    size_t pos = body.find(std::string("\"") + key + "\"");
    if (pos == std::string::npos) return nullptr;
    pos = body.find(':', pos);
    if (pos == std::string::npos) return nullptr;
    pos = body.find_first_not_of(" \t\r\n", pos + 1);
    return pos == std::string::npos ? nullptr : body.c_str() + pos;
}

bool FetchQuote(GoldQuote& quote) {
    HttpHandle session(WinHttpOpen(L"TaskbarMonitor/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), &WinHttpCloseHandle);
    if (!session) return false;
    WinHttpSetTimeouts(session.get(), 3000, 3000, 3000, 3000);

    HttpHandle connection(WinHttpConnect(session.get(), L"api.gold-api.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0), &WinHttpCloseHandle);
    if (!connection) return false;
    HttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", L"/price/XAU/CNY",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), &WinHttpCloseHandle);
    if (!request) return false;
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) return false;

    DWORD status = 0, statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) return false;

    std::string body;
    char buffer[1024];
    DWORD bytesRead = 0;
    do {
        if (!WinHttpReadData(request.get(), buffer, sizeof(buffer), &bytesRead)) return false;
        body.append(buffer, bytesRead);
    } while (bytesRead != 0);

    const char* priceText = JsonValue(body, "price");
    const char* timeText = JsonValue(body, "updatedAt");
    if (!priceText || !timeText) return false;
    char* end = nullptr;
    double price = std::strtod(priceText, &end);
    if (end == priceText || !std::isfinite(price) || price <= 0.0) return false;

    unsigned year, month, day, hour, minute, second;
    if (std::sscanf(timeText, "\"%u-%u-%uT%u:%u:%uZ\"", &year, &month, &day,
            &hour, &minute, &second) != 6) return false;
    SYSTEMTIME utc{};
    utc.wYear = static_cast<WORD>(year);
    utc.wMonth = static_cast<WORD>(month);
    utc.wDay = static_cast<WORD>(day);
    utc.wHour = static_cast<WORD>(hour);
    utc.wMinute = static_cast<WORD>(minute);
    utc.wSecond = static_cast<WORD>(second);
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &quote.updatedLocal)) return false;

    quote.cnyPerGram = price / 31.1034768;
    quote.fetchSucceeded = true;
    return true;
}

} // namespace

GoldPrice::GoldPrice() {
    m_work = CreateThreadpoolWork(RefreshWorker, this, nullptr);
}

GoldPrice::~GoldPrice() {
    if (m_work) {
        WaitForThreadpoolWorkCallbacks(m_work, TRUE);
        CloseThreadpoolWork(m_work);
    }
}

void GoldPrice::Refresh() {
    if (m_work && !m_busy.exchange(true)) SubmitThreadpoolWork(m_work);
}

GoldQuote GoldPrice::Snapshot() {
    AcquireSRWLockShared(&m_lock);
    GoldQuote quote = m_quote;
    ReleaseSRWLockShared(&m_lock);
    return quote;
}

void CALLBACK GoldPrice::RefreshWorker(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) {
    auto* self = static_cast<GoldPrice*>(context);
    GoldQuote quote;
    bool success = FetchQuote(quote);
    AcquireSRWLockExclusive(&self->m_lock);
    if (success) self->m_quote = quote;
    else self->m_quote.fetchSucceeded = false;
    ReleaseSRWLockExclusive(&self->m_lock);
    self->m_busy = false;
}
