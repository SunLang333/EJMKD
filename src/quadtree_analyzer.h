#pragma once

#include "video_types.h"

#include <cstdint>
#include <vector>

namespace ejmdk {

struct NodeMap {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> texels;
};

struct QuadTreeSettings {
    std::uint32_t maxDepth = 5;
    std::uint32_t minLeafSize = 16;
    float varianceThreshold = 0.0015f;
    float temporalThreshold = 0.0200f;
};

class QuadTreeAnalyzer {
public:
    explicit QuadTreeAnalyzer(QuadTreeSettings settings = {});

    [[nodiscard]] NodeMap Build(const RgbaFrame* previousFrame, const RgbaFrame& currentFrame) const;

private:
    struct RegionStats {
        float variance = 0.0f;
        float temporalDifference = 0.0f;
    };

    [[nodiscard]] static std::vector<float> BuildLumaPlane(const RgbaFrame& frame);
    [[nodiscard]] RegionStats AnalyzeRegion(const std::vector<float>* previousLuma,
                                            const std::vector<float>& currentLuma,
                                            std::uint32_t frameWidth,
                                            std::uint32_t x,
                                            std::uint32_t y,
                                            std::uint32_t width,
                                            std::uint32_t height) const;
    void BuildRecursive(const std::vector<float>* previousLuma,
                        const std::vector<float>& currentLuma,
                        std::uint32_t frameWidth,
                        std::uint32_t frameHeight,
                        std::uint32_t x,
                        std::uint32_t y,
                        std::uint32_t width,
                        std::uint32_t height,
                        std::uint32_t depth,
                        NodeMap& nodeMap) const;
    static void FillRegion(NodeMap& nodeMap,
                           std::uint32_t x,
                           std::uint32_t y,
                           std::uint32_t width,
                           std::uint32_t height,
                           float detailStrength,
                           float temporalConfidence,
                           float searchRadiusNorm,
                           float leafRefinement);

    QuadTreeSettings settings_{};
};

}  // namespace ejmdk
