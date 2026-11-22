#include "video_capture.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>
#endif
struct VideoCapture::Impl {
    std::atomic<bool> recording{false};
    std::atomic<uint64_t> frameCount{0};
    std::atomic<uint64_t> audioFrameCount{0};
    double frameRate = 60.0;
    int audioSampleRate = 32000;
    std::string path;
    std::string error;
#ifdef _WIN32
    struct Job {
        enum class Kind { Video, Audio } kind = Kind::Video;
        uint64_t index = 0;
        int width = 0;
        int height = 0;
        int logicalWidth = 256;
        int logicalHeight = 0;
        std::vector<uint32_t> pixels;
        std::vector<int16_t> audio;
        std::size_t bytes() const
        {
            return pixels.size() * sizeof(uint32_t) + audio.size() * sizeof(int16_t);
        }
    };
    std::thread worker;
    std::mutex mutex;
    std::condition_variable queueCv;
    std::condition_variable startupCv;
    std::deque<Job> queue;
    std::deque<std::vector<uint32_t>> freeVideoBuffers;
    std::size_t queuedBytes = 0;
    bool stopping = false;
    bool startupDone = false;
    bool startupOk = false;
    uint64_t droppedVideoFrames = 0;
#endif
};
VideoCapture::VideoCapture() : impl_(std::make_unique<Impl>()) {}
VideoCapture::~VideoCapture() { stop(); }
#ifdef _WIN32
namespace {
constexpr int kPresentationWidth = 512;
constexpr int kPresentationHeight = 240;
constexpr int kCaptureWidth = kPresentationWidth * 2;
constexpr int kCaptureHeight = kPresentationHeight * 2;
constexpr uint32_t kVideoBitrate = 3000000;
constexpr uint32_t kAudioBytesPerSecond = 16000;
constexpr LONGLONG kHundredNsPerSecond = 10000000LL;
constexpr std::size_t kMaxQueuedVideoBytes = 128u * 1024u * 1024u;
template <typename T>
void SafeRelease(T*& object)
{
    if (object) {
        object->Release();
        object = nullptr;
    }
}
std::string MfError(const char* operation, HRESULT hr)
{
    char buffer[192] = {};
    std::snprintf(buffer, sizeof(buffer), "%s failed (Media Foundation HRESULT 0x%08lX)",
        operation, static_cast<unsigned long>(hr));
    return buffer;
}
std::wstring NativeWidePath(const std::string& path)
{
    if (path.empty()) return {};
    const int count = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, result.data(), count);
    result.resize(static_cast<std::size_t>(count - 1));
    return result;
}
HRESULT MakeVideoType(IMFMediaType** out, const GUID& subtype, double frameRate, bool compressed)
{
    *out = nullptr;
    IMFMediaType* type = nullptr;
    HRESULT hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, subtype);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(type, MF_MT_FRAME_SIZE, kCaptureWidth, kCaptureHeight);
    const UINT32 rateNumerator = static_cast<UINT32>((std::max)(1LL, std::llround(frameRate * 1000.0)));
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(type, MF_MT_FRAME_RATE, rateNumerator, 1000);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_SMPTE_C);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
    if (SUCCEEDED(hr) && compressed) hr = type->SetUINT32(MF_MT_AVG_BITRATE, kVideoBitrate);
    if (FAILED(hr)) {
        SafeRelease(type);
        return hr;
    }
    *out = type;
    return S_OK;
}
HRESULT MakeAudioOutputType(IMFMediaType** out, int sampleRate)
{
    *out = nullptr;
    IMFMediaType* type = nullptr;
    HRESULT hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(sampleRate));
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioBytesPerSecond);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
    if (FAILED(hr)) {
        SafeRelease(type);
        return hr;
    }
    *out = type;
    return S_OK;
}
HRESULT MakeAudioInputType(IMFMediaType** out, int sampleRate)
{
    *out = nullptr;
    IMFMediaType* type = nullptr;
    constexpr UINT32 channels = 2;
    constexpr UINT32 bits = 16;
    constexpr UINT32 blockAlign = channels * (bits / 8);
    HRESULT hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(sampleRate));
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bits);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(sampleRate) * blockAlign);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(hr)) {
        SafeRelease(type);
        return hr;
    }
    *out = type;
    return S_OK;
}
HRESULT WriteBufferSample(IMFSinkWriter* writer, DWORD stream, const void* data, DWORD bytes,
    LONGLONG timestamp, LONGLONG duration)
{
    IMFMediaBuffer* buffer = nullptr;
    IMFSample* sample = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);
    BYTE* dst = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&dst, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        std::memcpy(dst, data, bytes);
        buffer->Unlock();
        dst = nullptr;
        hr = buffer->SetCurrentLength(bytes);
    } else if (buffer && dst) {
        buffer->Unlock();
    }
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(timestamp);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(duration);
    if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample);
    SafeRelease(sample);
    SafeRelease(buffer);
    return hr;
}
template <typename Job>
HRESULT WriteVideoJob(IMFSinkWriter* writer, DWORD stream, const Job& job,
    double frameRate, std::vector<uint8_t>& frameBuffer)
{
    const int width = job.width;
    const int height = job.height;
    if (width <= 0 || height <= 0 || job.pixels.size() != static_cast<std::size_t>(width) * height)
        return E_INVALIDARG;
    const int logicalWidth = (std::max)(256, job.logicalWidth);
    const int logicalHeight = (std::min)(job.logicalHeight > 0 ? job.logicalHeight : height, 239);
    const int activeHeight = (std::max)(1, (logicalHeight * 256 + logicalWidth / 2) / logicalWidth);
    const int yOffset = (239 - activeHeight) / 2;
    const std::size_t yPlaneBytes = static_cast<std::size_t>(kCaptureWidth) * kCaptureHeight;
    uint8_t* yPlane = frameBuffer.data();
    uint8_t* uvPlane = yPlane + yPlaneBytes;
    std::fill(yPlane, yPlane + yPlaneBytes, static_cast<uint8_t>(16));
    std::fill(uvPlane, frameBuffer.data() + frameBuffer.size(), static_cast<uint8_t>(128));
    auto sampleRgb = [&](int encodedX, int encodedY, int& r, int& g, int& b) {
        const int outX = encodedX >> 1;
        const int outY = encodedY >> 1;
        const int pictureY = outY - yOffset;
        if (pictureY < 0 || pictureY >= activeHeight) {
            r = g = b = 0;
            return;
        }
        const int srcY = static_cast<int>((static_cast<int64_t>(pictureY) * height) / activeHeight);
        const int srcX = static_cast<int>((static_cast<int64_t>(outX) * width) / kPresentationWidth);
        const uint32_t pixel = job.pixels[static_cast<std::size_t>(srcY) * width + srcX];
        r = static_cast<int>(pixel & 0xFFu);
        g = static_cast<int>((pixel >> 8) & 0xFFu);
        b = static_cast<int>((pixel >> 16) & 0xFFu);
    };
    auto clampByte = [](int value) -> uint8_t {
        return static_cast<uint8_t>((std::max)(0, (std::min)(255, value)));
    };
    for (int y = 0; y < kCaptureHeight; ++y) {
        uint8_t* dstY = yPlane + static_cast<std::ptrdiff_t>(y) * kCaptureWidth;
        for (int x = 0; x < kCaptureWidth; ++x) {
            int r = 0, g = 0, b = 0;
            sampleRgb(x, y, r, g, b);
            dstY[x] = clampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }
    }
    for (int y = 0; y < kCaptureHeight; y += 2) {
        uint8_t* dstUV = uvPlane + static_cast<std::ptrdiff_t>(y / 2) * kCaptureWidth;
        for (int x = 0; x < kCaptureWidth; x += 2) {
            int sumU = 0;
            int sumV = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    int r = 0, g = 0, b = 0;
                    sampleRgb(x + dx, y + dy, r, g, b);
                    sumU += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                    sumV += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                }
            }
            dstUV[x] = clampByte((sumU + 2) / 4);
            dstUV[x + 1] = clampByte((sumV + 2) / 4);
        }
    }
    const LONGLONG start = static_cast<LONGLONG>(std::llround(
        static_cast<long double>(job.index) * kHundredNsPerSecond / frameRate));
    const LONGLONG end = static_cast<LONGLONG>(std::llround(
        static_cast<long double>(job.index + 1) * kHundredNsPerSecond / frameRate));
    return WriteBufferSample(writer, stream, frameBuffer.data(),
        static_cast<DWORD>(frameBuffer.size()), start, end - start);
}
}
#endif
bool VideoCapture::start(const std::string& path, double frameRate, int audioSampleRate)
{
    stop();
    impl_->error.clear();
    impl_->path = path;
    impl_->frameRate = frameRate > 1.0 ? frameRate : 60.0;
    impl_->audioSampleRate = audioSampleRate > 0 ? audioSampleRate : 32000;
    impl_->frameCount.store(0, std::memory_order_relaxed);
    impl_->audioFrameCount.store(0, std::memory_order_relaxed);
#ifdef _WIN32
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->queue.clear();
        impl_->freeVideoBuffers.clear();
        constexpr std::size_t kWarmVideoBuffers = 12;
        for (std::size_t i = 0; i < kWarmVideoBuffers; ++i) {
            std::vector<uint32_t> buffer;
            buffer.reserve(static_cast<std::size_t>(kPresentationWidth) * kPresentationHeight);
            impl_->freeVideoBuffers.emplace_back(std::move(buffer));
        }
        impl_->queuedBytes = 0;
        impl_->stopping = false;
        impl_->startupDone = false;
        impl_->startupOk = false;
        impl_->droppedVideoFrames = 0;
    }
    const std::string workerPath = path;
    const double workerFrameRate = impl_->frameRate;
    const int workerAudioRate = impl_->audioSampleRate;
    Impl* state = impl_.get();
    impl_->worker = std::thread([state, workerPath, workerFrameRate, workerAudioRate]() {
        IMFSinkWriter* writer = nullptr;
        DWORD videoStream = 0;
        DWORD audioStream = 0;
        bool mfStarted = false;
        bool comOwned = false;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) comOwned = true;
        else if (hr != RPC_E_CHANGED_MODE) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->error = MfError("CoInitializeEx(capture worker)", hr);
        }
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
            hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
            if (SUCCEEDED(hr)) mfStarted = true;
            else {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->error = MfError("MFStartup(capture worker)", hr);
            }
        }
        const std::wstring widePath = NativeWidePath(workerPath);
        if (SUCCEEDED(hr) && widePath.empty()) {
            hr = E_INVALIDARG;
            std::lock_guard<std::mutex> lock(state->mutex);
            state->error = "Could not convert the capture path for Media Foundation.";
        }
        if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(widePath.c_str(), nullptr, nullptr, &writer);
        IMFMediaType* outputVideo = nullptr;
        IMFMediaType* inputVideo = nullptr;
        IMFMediaType* outputAudio = nullptr;
        IMFMediaType* inputAudio = nullptr;
        if (SUCCEEDED(hr)) hr = MakeVideoType(&outputVideo, MFVideoFormat_H264, workerFrameRate, true);
        if (SUCCEEDED(hr)) hr = writer->AddStream(outputVideo, &videoStream);
        if (SUCCEEDED(hr)) hr = MakeVideoType(&inputVideo, MFVideoFormat_NV12, workerFrameRate, false);
        if (SUCCEEDED(hr)) hr = inputVideo->SetUINT32(MF_MT_DEFAULT_STRIDE, kCaptureWidth);
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(videoStream, inputVideo, nullptr);
        if (SUCCEEDED(hr)) hr = MakeAudioOutputType(&outputAudio, workerAudioRate);
        if (SUCCEEDED(hr)) hr = writer->AddStream(outputAudio, &audioStream);
        if (SUCCEEDED(hr)) hr = MakeAudioInputType(&inputAudio, workerAudioRate);
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audioStream, inputAudio, nullptr);
        if (SUCCEEDED(hr)) hr = writer->BeginWriting();
        SafeRelease(inputAudio);
        SafeRelease(outputAudio);
        SafeRelease(inputVideo);
        SafeRelease(outputVideo);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (FAILED(hr) && state->error.empty())
                state->error = MfError("Configure MP4 H.264/AAC writer", hr);
            state->startupOk = SUCCEEDED(hr);
            state->startupDone = true;
        }
        state->startupCv.notify_one();
        if (SUCCEEDED(hr)) {
            std::vector<uint8_t> frameBuffer(static_cast<std::size_t>(kCaptureWidth) * kCaptureHeight * 3 / 2, 0);
            for (;;) {
                Impl::Job job;
                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->queueCv.wait(lock, [state] { return state->stopping || !state->queue.empty(); });
                    if (state->queue.empty()) {
                        if (state->stopping) break;
                        continue;
                    }
                    job = std::move(state->queue.front());
                    state->queuedBytes -= job.bytes();
                    state->queue.pop_front();
                }
                if (job.kind == Impl::Job::Kind::Video) {
                    hr = WriteVideoJob(writer, videoStream, job, workerFrameRate, frameBuffer);
                    if (!job.pixels.empty()) {
                        job.pixels.clear();
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (state->freeVideoBuffers.size() < 24u)
                            state->freeVideoBuffers.emplace_back(std::move(job.pixels));
                    }
                } else {
                    const uint64_t stereoFrames = job.audio.size() / 2;
                    const std::size_t bytes = job.audio.size() * sizeof(int16_t);
                    if (bytes > static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())) {
                        hr = E_INVALIDARG;
                    } else {
                        const LONGLONG start = static_cast<LONGLONG>((job.index * kHundredNsPerSecond) /
                            static_cast<uint64_t>(workerAudioRate));
                        const LONGLONG end = static_cast<LONGLONG>(((job.index + stereoFrames) * kHundredNsPerSecond) /
                            static_cast<uint64_t>(workerAudioRate));
                        hr = WriteBufferSample(writer, audioStream, job.audio.data(), static_cast<DWORD>(bytes),
                            start, end - start);
                    }
                }
                if (FAILED(hr)) {
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (state->error.empty()) state->error = MfError("Capture worker WriteSample", hr);
                        state->stopping = true;
                        state->queue.clear();
                        state->queuedBytes = 0;
                    }
                    state->recording.store(false, std::memory_order_release);
                    break;
                }
            }
            const HRESULT finalHr = writer->Finalize();
            if (FAILED(finalHr)) {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->error.empty()) state->error = MfError("Finalize MP4", finalHr);
            }
        }
        SafeRelease(writer);
        if (mfStarted) MFShutdown();
        if (comOwned) CoUninitialize();
    });
    {
        std::unique_lock<std::mutex> lock(impl_->mutex);
        impl_->startupCv.wait(lock, [this] { return impl_->startupDone; });
        if (!impl_->startupOk) {
            lock.unlock();
            if (impl_->worker.joinable()) impl_->worker.join();
            return false;
        }
    }
    impl_->recording.store(true, std::memory_order_release);
    return true;
#else
    (void)path;
    (void)frameRate;
    (void)audioSampleRate;
    impl_->error = "Video capture is currently available in the Windows frontend.";
    return false;
#endif
}
void VideoCapture::stop()
{
#ifdef _WIN32
    const bool hadWorker = impl_->worker.joinable();
    impl_->recording.store(false, std::memory_order_release);
    if (hadWorker) {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->stopping = true;
        }
        impl_->queueCv.notify_all();
        impl_->worker.join();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->queue.clear();
        impl_->freeVideoBuffers.clear();
        impl_->queuedBytes = 0;
        impl_->stopping = false;
    }
#else
    impl_->recording.store(false, std::memory_order_release);
#endif
}
bool VideoCapture::writeFrame(const uint32_t* rgba, int width, int height, int stridePixels, int logicalWidth, int logicalHeight)
{
    if (!impl_->recording.load(std::memory_order_acquire)) return true;
    if (!rgba || width <= 0 || height <= 0 || stridePixels < width) {
        impl_->error = "Invalid framebuffer passed to video capture.";
        return false;
    }
#ifdef _WIN32
    Impl::Job job;
    job.kind = Impl::Job::Kind::Video;
    job.index = impl_->frameCount.fetch_add(1, std::memory_order_relaxed);
    job.width = width;
    job.height = height;
    job.logicalWidth = logicalWidth;
    job.logicalHeight = logicalHeight;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->freeVideoBuffers.empty()) {
            job.pixels = std::move(impl_->freeVideoBuffers.front());
            impl_->freeVideoBuffers.pop_front();
        }
    }
    job.pixels.resize(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        std::memcpy(job.pixels.data() + static_cast<std::size_t>(y) * width,
            rgba + static_cast<std::ptrdiff_t>(y) * stridePixels,
            static_cast<std::size_t>(width) * sizeof(uint32_t));
    }
    const std::size_t bytes = job.bytes();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->stopping) return true;
        if (impl_->queuedBytes + bytes > kMaxQueuedVideoBytes) {
            ++impl_->droppedVideoFrames;
            job.pixels.clear();
            if (impl_->freeVideoBuffers.size() < 24u)
                impl_->freeVideoBuffers.emplace_back(std::move(job.pixels));
            return true;
        }
        impl_->queuedBytes += bytes;
        impl_->queue.emplace_back(std::move(job));
    }
    impl_->queueCv.notify_one();
    return true;
#else
    (void)rgba; (void)width; (void)height; (void)stridePixels; (void)logicalWidth; (void)logicalHeight;
    return false;
#endif
}
bool VideoCapture::writeAudio(const int16_t* interleavedStereo, std::size_t sampleCount)
{
    if (!impl_->recording.load(std::memory_order_acquire) || sampleCount == 0) return true;
    if (!interleavedStereo || (sampleCount & 1u) != 0) {
        impl_->error = "Invalid stereo PCM passed to video capture.";
        return false;
    }
#ifdef _WIN32
    Impl::Job job;
    job.kind = Impl::Job::Kind::Audio;
    const uint64_t stereoFrames = sampleCount / 2;
    job.index = impl_->audioFrameCount.fetch_add(stereoFrames, std::memory_order_relaxed);
    job.audio.assign(interleavedStereo, interleavedStereo + sampleCount);
    const std::size_t bytes = job.bytes();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->stopping) return true;
        impl_->queuedBytes += bytes;
        impl_->queue.emplace_back(std::move(job));
    }
    impl_->queueCv.notify_one();
    return true;
#else
    (void)interleavedStereo; (void)sampleCount;
    return false;
#endif
}
bool VideoCapture::isRecording() const { return impl_->recording.load(std::memory_order_acquire); }
uint64_t VideoCapture::frameCount() const { return impl_->frameCount.load(std::memory_order_relaxed); }
uint64_t VideoCapture::droppedVideoFrames() const
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->droppedVideoFrames;
#else
    return 0;
#endif
}
double VideoCapture::secondsRecorded() const
{
    return impl_->frameRate > 0.0 ? static_cast<double>(frameCount()) / impl_->frameRate : 0.0;
}
const char* VideoCapture::codecName() const { return "H.264/AAC MP4 (async)"; }
const std::string& VideoCapture::path() const { return impl_->path; }
const std::string& VideoCapture::lastError() const { return impl_->error; }
