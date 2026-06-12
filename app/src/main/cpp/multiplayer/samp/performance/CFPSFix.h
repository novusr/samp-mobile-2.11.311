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
    uint8_t GetTargetFPS() const;

private:
    CFPSFix();
    ~CFPSFix();

    CFPSFix(const CFPSFix&) = delete;
    CFPSFix& operator=(const CFPSFix&) = delete;

    void Routine();
    void ApplyAffinityToAllThreads();
    void ResetFrameClock();

private:
    std::mutex m_Mutex;
    std::vector<pid_t> m_Threads;
    std::thread m_Worker;
    std::atomic_bool m_Initialized{false};
    std::atomic_bool m_Running{false};
    std::atomic<uint8_t> m_TargetFPS{120};
    std::chrono::steady_clock::time_point m_LastFrameTime{};
};

void InitFPSFix(uint8_t targetFPS);
void ShutdownFPSFix();
void ProcessFPSFixFrame();
void RegisterFPSThread(pid_t tid);
