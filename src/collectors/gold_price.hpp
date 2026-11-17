#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>

struct GoldQuote {
    double cnyPerGram = 0.0;
    SYSTEMTIME updatedLocal{};
    bool fetchSucceeded = false;
};

class GoldPrice {
public:
    GoldPrice();
    ~GoldPrice();

    GoldPrice(const GoldPrice&) = delete;
    GoldPrice& operator=(const GoldPrice&) = delete;

    void Refresh();
    GoldQuote Snapshot();

private:
    static void CALLBACK RefreshWorker(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK);

    PTP_WORK m_work = nullptr;
    std::atomic<bool> m_busy{false};
    SRWLOCK m_lock = SRWLOCK_INIT;
    GoldQuote m_quote{};
};
