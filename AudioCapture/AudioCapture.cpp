#include "AudioCapture.h"

#include <stdio.h>
#include <cmath>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

// WASAPI GUID 定义 —— Windows SDK 10.0.26100 的 mmdeviceapi.h 和 audioclient.h
// 仅声明（extern），不实例化。此处在一个翻译单元中提供实际定义。
extern "C" {
    const GUID CLSID_MMDeviceEnumerator  = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
    const GUID IID_IMMDeviceEnumerator   = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
    const GUID IID_IAudioClient          = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
    const GUID IID_IAudioCaptureClient   = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
}

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

AudioCapture::AudioCapture()
    : m_enumerator(nullptr)
    , m_device(nullptr)
    , m_audioClient(nullptr)
    , m_captureClient(nullptr)
    , m_sampleRate(44100)
    , m_numChannels(2)
    , m_isFloatFormat(true)
    , m_bytesPerSample(4)
    , m_frameSize(8)
    , m_writePos(0)
    , m_readPos(0)
    , m_bufferSize(0)
    , m_initialized(false)
{
}

AudioCapture::~AudioCapture() {
    Shutdown();
}

// ---------------------------------------------------------------------------
// Initialize —— 初始化 WASAPI Loopback
// ---------------------------------------------------------------------------

bool AudioCapture::Initialize() {
    if (m_initialized) return true;

    HRESULT hr;

    // 1. 初始化 COM
    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        // RPC_E_CHANGED_MODE 表示 COM 已被其他模式初始化，可以接受
        // 真正的失败才返回 false
    }

    // 2. 创建 MMDeviceEnumerator
    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
        IID_IMMDeviceEnumerator, (void**)&m_enumerator);
    if (FAILED(hr)) {
        printf("[AudioCapture] Failed to create device enumerator: 0x%08X\n", hr);
        return false;
    }

    // 3. 获取默认渲染设备（扬声器），用于 Loopback
    hr = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device);
    if (FAILED(hr)) {
        printf("[AudioCapture] Failed to get default audio endpoint: 0x%08X\n", hr);
        return false;
    }

    // 4. 激活 IAudioClient
    hr = m_device->Activate(IID_IAudioClient, CLSCTX_ALL,
                             nullptr, (void**)&m_audioClient);
    if (FAILED(hr)) {
        printf("[AudioCapture] Failed to activate audio client: 0x%08X\n", hr);
        return false;
    }

    // 5. 获取混合格式
    WAVEFORMATEX* pwfx = nullptr;
    hr = m_audioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
        printf("[AudioCapture] Failed to get mix format: 0x%08X\n", hr);
        return false;
    }

    // 解析格式
    m_sampleRate = pwfx->nSamplesPerSec;
    m_numChannels = pwfx->nChannels;

    if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
        m_isFloatFormat = true;
        m_bytesPerSample = 4;
    } else {
        m_isFloatFormat = false;
        m_bytesPerSample = pwfx->wBitsPerSample / 8;
        if (m_bytesPerSample < 2) m_bytesPerSample = 2;
    }
    m_frameSize = m_bytesPerSample * m_numChannels;

    printf("[AudioCapture] Format: %d Hz, %d ch, %s, %d bits\n",
           m_sampleRate, m_numChannels,
           m_isFloatFormat ? "Float32" : "PCM",
           m_bytesPerSample * 8);

    // 6. 初始化音频客户端（Loopback 模式）
    REFERENCE_TIME hnsRequestedDuration = 10000000; // 1 秒
    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        hnsRequestedDuration,
        0,           // 对齐（Loopback 模式必须为 0）
        pwfx,
        nullptr);

    CoTaskMemFree(pwfx);

    if (FAILED(hr)) {
        printf("[AudioCapture] Failed to initialize audio client: 0x%08X\n", hr);
        return false;
    }

    // 7. 获取缓冲区大小
    UINT32 bufferFrameCount;
    hr = m_audioClient->GetBufferSize(&bufferFrameCount);
    if (FAILED(hr)) {
        printf("[AudioCapture] Failed to get buffer size: 0x%08X\n", hr);
        return false;
    }
    printf("[AudioCapture] Buffer size: %u frames\n", bufferFrameCount);

    // 8. 获取 IAudioCaptureClient
    hr = m_audioClient->GetService(IID_IAudioCaptureClient,
                                    (void**)&m_captureClient);
    if (FAILED(hr)) {
        printf("[AudioCapture] Failed to get capture client: 0x%08X\n", hr);
        return false;
    }

    // 9. 初始化环形缓冲区（存储 ~2 秒音频）
    m_bufferSize = m_sampleRate * 4; // 4 秒容量
    m_buffer.resize(m_bufferSize, 0.0f);
    m_writePos = 0;
    m_readPos = 0;

    // 10. 启动音频流
    hr = m_audioClient->Start();
    if (FAILED(hr)) {
        printf("[AudioCapture] Failed to start audio client: 0x%08X\n", hr);
        return false;
    }

    m_initialized = true;
    printf("[AudioCapture] Initialized successfully.\n");
    return true;
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void AudioCapture::Shutdown() {
    if (m_audioClient) {
        m_audioClient->Stop();
    }
    if (m_captureClient) {
        m_captureClient->Release();
        m_captureClient = nullptr;
    }
    if (m_audioClient) {
        m_audioClient->Release();
        m_audioClient = nullptr;
    }
    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }
    if (m_enumerator) {
        m_enumerator->Release();
        m_enumerator = nullptr;
    }
    m_initialized = false;
}

// ---------------------------------------------------------------------------
// Capture —— 拉取音频数据
// ---------------------------------------------------------------------------

bool AudioCapture::Capture(float* waveformOut, int maxSamples, int& samplesRead) {
    if (!m_initialized) {
        samplesRead = 0;
        return false;
    }

    // 1. 从 WASAPI 读取新数据到环形缓冲区
    UINT32 packetLength = 0;
    HRESULT hr = m_captureClient->GetNextPacketSize(&packetLength);

    while (SUCCEEDED(hr) && packetLength > 0) {
        BYTE* data;
        UINT32 numFrames;
        DWORD flags;

        hr = m_captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
        if (FAILED(hr)) break;

        // 忽略静音帧（AUDCLNT_BUFFERFLAGS_SILENT），但仍推进缓冲区
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
            ProcessAudioFrames(data, numFrames);
        }

        hr = m_captureClient->ReleaseBuffer(numFrames);

        // 限制写入位置，防止溢出
        // 如果缓冲区快满了就跳帧（丢旧数据）
        {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            size_t used = (m_writePos >= m_readPos)
                ? (m_writePos - m_readPos)
                : (m_bufferSize - m_readPos + m_writePos);
            // 如果已用超过 75%，丢弃一些旧数据
            if (used > m_bufferSize * 3 / 4) {
                m_readPos = (m_writePos + m_bufferSize / 4) % m_bufferSize;
            }
        }

        hr = m_captureClient->GetNextPacketSize(&packetLength);
    }

    // 2. 从环形缓冲区读取数据
    samplesRead = 0;
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);

        size_t available;
        if (m_writePos >= m_readPos) {
            available = m_writePos - m_readPos;
        } else {
            available = m_bufferSize - m_readPos + m_writePos;
        }

        samplesRead = static_cast<int>(std::min(static_cast<size_t>(maxSamples), available));

        for (int i = 0; i < samplesRead; ++i) {
            waveformOut[i] = m_buffer[(m_readPos + i) % m_bufferSize];
        }

        m_readPos = (m_readPos + samplesRead) % m_bufferSize;
    }

    return true;
}

// ---------------------------------------------------------------------------
// ProcessAudioFrames —— 原始字节 → 浮点采样
// ---------------------------------------------------------------------------

void AudioCapture::ProcessAudioFrames(const BYTE* data, UINT32 numFrames) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    for (UINT32 frame = 0; frame < numFrames; ++frame) {
        float sample = 0.0f;

        if (m_isFloatFormat) {
            // IEEE Float 格式：每声道 4 字节 float
            const float* floatFrame = reinterpret_cast<const float*>(
                data + frame * m_frameSize);
            for (int ch = 0; ch < m_numChannels; ++ch) {
                sample += floatFrame[ch];
            }
            sample /= m_numChannels;
        } else {
            // PCM 格式：根据位深处理
            if (m_bytesPerSample == 2) {
                // int16
                const short* pcmFrame = reinterpret_cast<const short*>(
                    data + frame * m_frameSize);
                for (int ch = 0; ch < m_numChannels; ++ch) {
                    sample += pcmFrame[ch] / 32768.0f;
                }
                sample /= m_numChannels;
            } else if (m_bytesPerSample == 3) {
                // int24
                for (int ch = 0; ch < m_numChannels; ++ch) {
                    const BYTE* b = data + frame * m_frameSize + ch * 3;
                    int val = b[0] | (b[1] << 8) | (b[2] << 16);
                    if (val & 0x800000) val |= 0xFF000000; // 符号扩展
                    sample += val / 2147483648.0f;
                }
                sample /= m_numChannels;
            } else if (m_bytesPerSample == 4) {
                // int32
                const int* pcmFrame = reinterpret_cast<const int*>(
                    data + frame * m_frameSize);
                for (int ch = 0; ch < m_numChannels; ++ch) {
                    sample += pcmFrame[ch] / 2147483648.0f;
                }
                sample /= m_numChannels;
            }
        }

        // 限幅
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;

        m_buffer[m_writePos] = sample;
        m_writePos = (m_writePos + 1) % m_bufferSize;
    }
}
