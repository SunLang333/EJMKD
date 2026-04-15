#pragma once

#include <cstdint>
#include <vector>

namespace ejmdk {

struct RgbaFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int64_t pts100ns = 0;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool empty() const noexcept {
        return pixels.empty() || width == 0 || height == 0;
    }
};

struct VideoStreamInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frameRateNumerator = 0;
    std::uint32_t frameRateDenominator = 1;
    std::uint64_t frameCount = 0;
    double durationSeconds = 0.0;
};

struct CliOptions {
    std::uint32_t scale = 2;
    bool enableFrameGeneration = true;
};

}  // namespace ejmdk
