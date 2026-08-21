#pragma once

#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <vector>
#include <mutex>

/**
 * @brief 音频捕捉模块 —— 使用 WASAPI Loopback 捕获系统音频输出
 *
 * 工作原理：
 *   1. 获取默认渲染设备（扬声器/耳机）
 *   2. 以 AUDCLNT_STREAMFLAGS_LOOPBACK 模式激活 IAudioClient
 *   3. 通过 IAudioCaptureClient 轮询获取音频帧
 *   4. 将所有声道混合为单声道，存入环形缓冲区
 *
 * 线程安全：Capture() 在主线程（UI线程）调用，WASAPI 内部缓冲由系统管理
 */
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    /// 初始化 WASAPI Loopback 捕获，失败返回 false
    bool Initialize();

    /// 关闭音频设备，释放资源
    void Shutdown();

    /// 从内部缓冲区拉取音频数据
    /// @param waveformOut  输出：单声道浮点采样 [-1.0, 1.0]
    /// @param maxSamples   最多读取的采样数
    /// @param samplesRead  输出：实际读取的采样数
    /// @return 是否成功
    bool Capture(float* waveformOut, int maxSamples, int& samplesRead);

    /// 获取采样率（Hz）
    int  GetSampleRate() const { return m_sampleRate; }

    /// 获取声道数
    int  GetNumChannels() const { return m_numChannels; }

    /// 是否已成功初始化
    bool IsInitialized() const { return m_initialized; }

private:
    // COM 接口
    IMMDeviceEnumerator*  m_enumerator;
    IMMDevice*            m_device;
    IAudioClient*         m_audioClient;
    IAudioCaptureClient*  m_captureClient;

    // 音频格式信息
    int  m_sampleRate;
    int  m_numChannels;
    bool m_isFloatFormat;       // true = IEEE Float, false = PCM int16
    int  m_bytesPerSample;      // 每声道每帧字节数
    int  m_frameSize;           // 一帧的字节数（所有声道）

    // 环形缓冲区
    std::vector<float> m_buffer;
    size_t             m_writePos;
    size_t             m_readPos;
    size_t             m_bufferSize;
    std::mutex         m_bufferMutex;

    bool m_initialized;

    /// 将原始音频帧转换为浮点采样并写入环形缓冲区
    void ProcessAudioFrames(const BYTE* data, UINT32 numFrames);
};
