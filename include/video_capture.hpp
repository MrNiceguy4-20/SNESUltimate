#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
class VideoCapture {
public:
    VideoCapture();
    ~VideoCapture();
    VideoCapture(const VideoCapture&) = delete;
    VideoCapture& operator=(const VideoCapture&) = delete;
    bool start(const std::string& path, double frameRate, int audioSampleRate);
    void stop();
    bool writeFrame(const uint32_t* rgba, int width, int height, int stridePixels, int logicalWidth = 256, int logicalHeight = 0);
    bool writeAudio(const int16_t* interleavedStereo, std::size_t sampleCount);
    bool isRecording() const;
    uint64_t frameCount() const;
    uint64_t droppedVideoFrames() const;
    double secondsRecorded() const;
    const char* codecName() const;
    const std::string& path() const;
    const std::string& lastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
