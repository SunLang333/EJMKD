#pragma once

#include "video_types.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ejmdk {

class MediaFoundationRuntime {
public:
    MediaFoundationRuntime();
    ~MediaFoundationRuntime();

    MediaFoundationRuntime(const MediaFoundationRuntime&) = delete;
    MediaFoundationRuntime& operator=(const MediaFoundationRuntime&) = delete;
};

class MfVideoReader {
public:
    explicit MfVideoReader(const std::wstring& inputPath);
    ~MfVideoReader();

    MfVideoReader(const MfVideoReader&) = delete;
    MfVideoReader& operator=(const MfVideoReader&) = delete;

    [[nodiscard]] const VideoStreamInfo& info() const noexcept;
    bool ReadFrame(RgbaFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class MfVideoWriter {
public:
    MfVideoWriter(const std::wstring& outputPath,
                  std::uint32_t width,
                  std::uint32_t height,
                  std::uint32_t frameRateNumerator,
                  std::uint32_t frameRateDenominator,
                  std::uint32_t bitrateMbps);
    ~MfVideoWriter();

    MfVideoWriter(const MfVideoWriter&) = delete;
    MfVideoWriter& operator=(const MfVideoWriter&) = delete;

    void WriteFrame(const RgbaFrame& frame);
    void Finalize();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ejmdk
