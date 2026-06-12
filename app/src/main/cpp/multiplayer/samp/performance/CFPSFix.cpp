#include "CFPSFix.h"
#include "samp/main.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <sys/syscall.h>
#include <unistd.h>

void ApplyFPSPatch(uint8_t fps);

namespace
{
    constexpr uint32_t CPU_AFFINITY_MASK_ALL = 0xff;
    constexpr auto THREAD_SCAN_INTERVAL = std::chrono::milliseconds(1000);
    constexpr auto MAX_FRAME_RESET_DELTA = std::chrono::milliseconds(250);
    constexpr auto SLEEP_GUARD = std::chrono::microseconds(900);

    uint8_t ClampFPS(uint8_t fps)
    {
        if (fps < 30) return 30;
        if (fps > 120) return 120;
        return fps;
    }

    uint8_t DisplayCapFor(float refreshRateHz)
    {
        // User rule:
        // - If the active display mode is not really 120Hz, cap the game at 60 FPS.
        // - Only allow 120 FPS when the current active refresh rate reaches 120Hz.
        // 119.0 keeps room for Android reporting 119.88/119.99 instead of exactly 120.
        return refreshRateHz >= 119.0f ? 120 : 60;
    }

    std::chrono::nanoseconds FrameDurationFor(uint8_t fps)
    {
        const uint8_t safeFPS = ClampFPS(fps);
        return std::chrono::nanoseconds(1000000000LL / safeFPS);
    }

    bool SetThreadAffinityMask(pid_t tid, uint32_t mask)
    {
        if (tid <= 0) return false;
        return syscall(__NR_sched_setaffinity, tid, sizeof(mask), &mask) == 0;
    }

    bool ParseThreadId(const char* name, pid_t& outTid)
    {
        if (!name || !*name) return false;

        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(name, &end, 10);
        if (errno != 0 || !end || *end != '\0' || value <= 0) return false;

        outTid = static_cast<pid_t>(value);
        return true;
    }
}

CFPSFix& CFPSFix::Instance()
{
    static CFPSFix instance;
    return instance;
}

CFPSFix::CFPSFix() = default;

CFPSFix::~CFPSFix()
{
    Shutdown();
}

void CFPSFix::Init(uint8_t targetFPS)
{
    SetTargetFPS(targetFPS);
    ApplyFPSPatch(GetTargetFPS());
    ResetFrameClock();
    PushThread(gettid());
    ApplyAffinityToAllThreads();

    bool expected = false;
    if (m_Initialized.compare_exchange_strong(expected, true))
    {
        m_Running.store(true);
        m_Worker = std::thread(&CFPSFix::Routine, this);
        m_Worker.detach();
    }
}

void CFPSFix::Shutdown()
{
    m_Running.store(false);
}

void CFPSFix::SetTargetFPS(uint8_t targetFPS)
{
    m_ConfiguredFPS.store(ClampFPS(targetFPS));
    RecalculateEffectiveFPS(true);
}

void CFPSFix::SetDisplayRefreshRate(float refreshRateHz)
{
    if (refreshRateHz < 1.0f)
        refreshRateHz = 60.0f;

    m_DisplayRefreshRate.store(refreshRateHz);
    RecalculateEffectiveFPS(true);
}

uint8_t CFPSFix::GetTargetFPS() const
{
    return ClampFPS(m_TargetFPS.load());
}

uint8_t CFPSFix::GetConfiguredFPS() const
{
    return ClampFPS(m_ConfiguredFPS.load());
}

uint8_t CFPSFix::GetDisplayCapFPS() const
{
    return DisplayCapFor(m_DisplayRefreshRate.load());
}

float CFPSFix::GetDisplayRefreshRate() const
{
    return m_DisplayRefreshRate.load();
}

void CFPSFix::RecalculateEffectiveFPS(bool applyPatch)
{
    const uint8_t configuredFPS = GetConfiguredFPS();
    const uint8_t displayCap = GetDisplayCapFPS();
    const uint8_t effectiveFPS = std::min(configuredFPS, displayCap);

    const uint8_t oldFPS = m_TargetFPS.exchange(effectiveFPS);

    FLog("FPSFix: Display Hz %.2f, configured FPS %u, display cap %u, effective FPS %u",
         static_cast<double>(GetDisplayRefreshRate()),
         static_cast<unsigned>(configuredFPS),
         static_cast<unsigned>(displayCap),
         static_cast<unsigned>(effectiveFPS));

    if (applyPatch && m_Initialized.load() && oldFPS != effectiveFPS)
    {
        ApplyFPSPatch(effectiveFPS);
        ResetFrameClock();
    }
}

void CFPSFix::ResetFrameClock()
{
    m_LastFrameTime = std::chrono::steady_clock::now();
}

void CFPSFix::ProcessFrame()
{
    if (!m_Initialized.load()) return;

    const auto targetFrameTime = FrameDurationFor(GetTargetFPS());
    auto now = std::chrono::steady_clock::now();

    if (m_LastFrameTime.time_since_epoch().count() == 0)
    {
        m_LastFrameTime = now;
        return;
    }

    auto targetTime = m_LastFrameTime + targetFrameTime;

    // If the game was paused or the current frame is already late, do not sleep.
    // Reset the clock so the limiter does not keep chasing old frame times.
    if (now >= targetTime)
    {
        if (now - m_LastFrameTime > MAX_FRAME_RESET_DELTA)
            m_LastFrameTime = now;
        else
            m_LastFrameTime = now;
        return;
    }

    auto remaining = targetTime - now;
    if (remaining > SLEEP_GUARD)
        std::this_thread::sleep_for(remaining - SLEEP_GUARD);

    // Small yield loop for better frame pacing on Android where sleep can overshoot.
    while (std::chrono::steady_clock::now() < targetTime)
        std::this_thread::yield();

    m_LastFrameTime = targetTime;
}

void CFPSFix::ApplyAffinityToAllThreads()
{
    std::vector<pid_t> tids;

    if (DIR* dir = opendir("/proc/self/task"))
    {
        while (dirent* entry = readdir(dir))
        {
            pid_t tid = 0;
            if (ParseThreadId(entry->d_name, tid))
                tids.push_back(tid);
        }
        closedir(dir);
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        tids.insert(tids.end(), m_Threads.begin(), m_Threads.end());
    }

    std::sort(tids.begin(), tids.end());
    tids.erase(std::unique(tids.begin(), tids.end()), tids.end());

    for (pid_t tid : tids)
        SetThreadAffinityMask(tid, CPU_AFFINITY_MASK_ALL);
}

void CFPSFix::Routine()
{
    while (m_Running.load())
    {
        ApplyAffinityToAllThreads();
        std::this_thread::sleep_for(THREAD_SCAN_INTERVAL);
    }
}

void CFPSFix::PushThread(pid_t tid)
{
    if (tid <= 0) return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    if (std::find(m_Threads.begin(), m_Threads.end(), tid) == m_Threads.end())
        m_Threads.push_back(tid);
}

void InitFPSFix(uint8_t targetFPS)
{
    CFPSFix::Instance().Init(targetFPS);
}

void SetFPSDisplayRefreshRate(float refreshRateHz)
{
    CFPSFix::Instance().SetDisplayRefreshRate(refreshRateHz);
}

uint8_t GetEffectiveFPSLimit()
{
    return CFPSFix::Instance().GetTargetFPS();
}

uint8_t GetConfiguredFPSLimit()
{
    return CFPSFix::Instance().GetConfiguredFPS();
}

uint8_t GetDisplayFPSCap()
{
    return CFPSFix::Instance().GetDisplayCapFPS();
}

float GetFPSDisplayRefreshRate()
{
    return CFPSFix::Instance().GetDisplayRefreshRate();
}

void ShutdownFPSFix()
{
    CFPSFix::Instance().Shutdown();
}

void ProcessFPSFixFrame()
{
    CFPSFix::Instance().ProcessFrame();
}

void RegisterFPSThread(pid_t tid)
{
    CFPSFix::Instance().PushThread(tid);
}
