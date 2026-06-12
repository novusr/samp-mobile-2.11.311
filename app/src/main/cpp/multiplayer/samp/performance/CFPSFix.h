#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <sys/types.h>

class CFPSFix
{
public:
    static CFPSFix& Instance();

    void Init(uint8_t targetFPS);
    void Shutdown();
    void ProcessFrame();
    void PushThread(pid_t tid);
    void SetTargetFPS(uint8_t targetFPS);
    void SetDisplayRefreshRate(float refreshRateHz);
    uint8_t GetTargetFPS() const;
    uint8_t GetConfiguredFPS() const;
    uint8_t GetDisplayCapFPS() const;
    float GetDisplayRefreshRate() const;

private:
    CFPSFix();
    ~CFPSFix();

    CFPSFix(const CFPSFix&) = delete;
    CFPSFix& operator=(const CFPSFix&) = delete;

    void Routine();
    void ApplyAffinityToAllThreads();
    void ResetFrameClock();
    void RecalculateEffectiveFPS(bool applyPatch);

private:
    std::mutex m_Mutex;
    std::vector<pid_t> m_Threads;
    std::thread m_Worker;
    std::atomic_bool m_Initialized{false};
    std::atomic_bool m_Running{false};
    std::atomic<uint8_t> m_ConfiguredFPS{120};
    std::atomic<uint8_t> m_TargetFPS{60};
    std::atomic<float> m_DisplayRefreshRate{60.0f};
    std::chrono::steady_clock::time_point m_LastFrameTime{};
};

void InitFPSFix(uint8_t targetFPS);
void ShutdownFPSFix();
void ProcessFPSFixFrame();
void RegisterFPSThread(pid_t tid);
void SetFPSDisplayRefreshRate(float refreshRateHz);
uint8_t GetEffectiveFPSLimit();
uint8_t GetConfiguredFPSLimit();
uint8_t GetDisplayFPSCap();
float GetFPSDisplayRefreshRate();
