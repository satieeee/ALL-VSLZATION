/**
 * @file SharedState.h
 * @brief 共享内存协议 —— MAIN.exe → 所有可视化子程序
 *
 * 用法：
 *   MAIN.exe 创建命名共享内存，每帧写入 BPM 和节拍状态。
 *   子程序打开同一块共享内存，每帧读取同步。
 *
 * 共享内存名: L"Local\AudioViz_BPM_Shared"
 */

#pragma once
#include <windows.h>

#define SHARED_MEM_NAME     L"Local\\AudioViz_BPM_Shared"
#define SHARED_MEM_SIZE     256

struct SharedBPMState {
    // ---- 由 MAIN.exe 每帧写入 ----
    float   bpm;           // 当前 BPM (40-300)
    float   beatPhase;     // 节拍相位 [0.0, 1.0)，0=节拍起点
    DWORD   beatCount;     // 累计节拍总数
    bool    beatTriggered; // 当前帧是否命中节拍（子程序读取后自行清零）
    DWORD   updateTick;    // 每次 MA

    // ---- 预留扩展 ----
    float   reserved[8];
};

/**
 * 创建/打开共享内存的辅助类
 */
class SharedMem {
public:
    SharedMem() : m_hMap(nullptr), m_state(nullptr) {}

    /// 创建共享内存（MAIN.exe 调用）
    bool Create() {
        m_hMap = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, SHARED_MEM_SIZE, SHARED_MEM_NAME);
        if (!m_hMap) return false;
        m_state = (SharedBPMState*)MapViewOfFile(m_hMap, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEM_SIZE);
        if (m_state) {
            memset(m_state, 0, sizeof(SharedBPMState));
            m_state->bpm = 120.0f;
        }
        return m_state != nullptr;
    }

    /// 打开已有共享内存（子程序调用）
    bool Open() {
        m_hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, SHARED_MEM_NAME);
        if (!m_hMap) return false;
        m_state = (SharedBPMState*)MapViewOfFile(m_hMap, FILE_MAP_READ, 0, 0, SHARED_MEM_SIZE);
        return m_state != nullptr;
    }

    /// 写入（MAIN.exe 调用）
    void Write(float bpm, float phase, DWORD count, bool triggered) {
        if (!m_state) return;
        m_state->bpm           = bpm;
        m_state->beatPhase     = phase;
        m_state->beatCount     = count;
        m_state->beatTriggered = triggered;
        m_state->updateTick++;
    }

    /// 读取（子程序调用）
    const SharedBPMState* Read() const { return m_state; }

    ~SharedMem() {
        if (m_state) UnmapViewOfFile(m_state);
        if (m_hMap)  CloseHandle(m_hMap);
    }

private:
    HANDLE          m_hMap;
    SharedBPMState* m_state;
};
