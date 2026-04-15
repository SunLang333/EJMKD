#include "quadtree_analyzer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ejmdk {
namespace {

[[nodiscard]] float Clamp01(const float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

}  // namespace

QuadTreeAnalyzer::QuadTreeAnalyzer(QuadTreeSettings settings)
    : settings_(settings) {
}

NodeMap QuadTreeAnalyzer::Build(const RgbaFrame* previousFrame, const RgbaFrame& currentFrame) const {
    if (currentFrame.empty()) {
        throw std::runtime_error("Cannot build a node map from an empty frame.");
    }

    std::vector<float> currentLuma = BuildLumaPlane(currentFrame);
    std::vector<float> previousLuma;
    const std::vector<float>* previousLumaPtr = nullptr;

    if (previousFrame != nullptr && !previousFrame->empty()) {
        if (previousFrame->width != currentFrame.width || previousFrame->height != currentFrame.height) {
            throw std::runtime_error("Previous and current frames must share the same dimensions.");
        }
        previousLuma = BuildLumaPlane(*previousFrame);
        previousLumaPtr = &previousLuma;
    }

    NodeMap nodeMap;
    nodeMap.width = currentFrame.width;
    nodeMap.height = currentFrame.height;
    nodeMap.texels.assign(static_cast<std::size_t>(nodeMap.width) * static_cast<std::size_t>(nodeMap.height) * 4U, 0.0f);

    BuildRecursive(previousLumaPtr,
                   currentLuma,
                   currentFrame.width,
                   currentFrame.height,
                   0,
                   0,
                   currentFrame.width,
                   currentFrame.height,
                   0,
                   nodeMap);

    return nodeMap;
}

std::vector<float> QuadTreeAnalyzer::BuildLumaPlane(const RgbaFrame& frame) {
    std::vector<float> luma(frame.width * frame.height, 0.0f);

    for (std::uint32_t y = 0; y < frame.height; ++y) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const std::size_t pixelIndex = (static_cast<std::size_t>(y) * frame.width + x) * 4U;
            const float r = static_cast<float>(frame.pixels[pixelIndex + 0]) / 255.0f;
            const float g = static_cast<float>(frame.pixels[pixelIndex + 1]) / 255.0f;
            const float b = static_cast<float>(frame.pixels[pixelIndex + 2]) / 255.0f;
            luma[static_cast<std::size_t>(y) * frame.width + x] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        }
    }

    return luma;
}

QuadTreeAnalyzer::RegionStats QuadTreeAnalyzer::AnalyzeRegion(const std::vector<float>* previousLuma,
                                                              const std::vector<float>& currentLuma,
                                                              const std::uint32_t frameWidth,
                                                              const std::uint32_t x,
                                                              const std::uint32_t y,
                                                              const std::uint32_t width,
                                                              const std::uint32_t height) const {
    double sum = 0.0;
    double sumSquared = 0.0;
    double temporalDifference = 0.0;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    for (std::uint32_t row = y; row < y + height; ++row) {
        for (std::uint32_t column = x; column < x + width; ++column) {
            const std::size_t index = static_cast<std::size_t>(row) * frameWidth + column;
            const double value = currentLuma[index];
            sum += value;
            sumSquared += value * value;
            if (previousLuma != nullptr) {
                temporalDifference += std::abs(value - static_cast<double>((*previousLuma)[index]));
            }
        }
    }

    const double mean = sum / static_cast<double>(pixelCount);
    const double variance = std::max(0.0, (sumSquared / static_cast<double>(pixelCount)) - (mean * mean));

    RegionStats stats;
    stats.variance = static_cast<float>(variance);
    stats.temporalDifference = previousLuma != nullptr
        ? static_cast<float>(temporalDifference / static_cast<double>(pixelCount))
        : 0.0f;
    return stats;
}

void QuadTreeAnalyzer::BuildRecursive(const std::vector<float>* previousLuma,
                                      const std::vector<float>& currentLuma,
                                      const std::uint32_t frameWidth,
                                      const std::uint32_t frameHeight,
                                      const std::uint32_t x,
                                      const std::uint32_t y,
                                      const std::uint32_t width,
                                      const std::uint32_t height,
                                      const std::uint32_t depth,
                                      NodeMap& nodeMap) const {
    const RegionStats stats = AnalyzeRegion(previousLuma, currentLuma, frameWidth, x, y, width, height);

    const bool largeEnoughToSplit = width > settings_.minLeafSize * 2U && height > settings_.minLeafSize * 2U;
    const float adaptiveVarianceThreshold = settings_.varianceThreshold * std::max(0.55f, 1.0f - depth * 0.12f);
    const bool shouldSplit = depth < settings_.maxDepth && largeEnoughToSplit &&
        (stats.variance > adaptiveVarianceThreshold || stats.temporalDifference > settings_.temporalThreshold);

    if (shouldSplit) {
        const std::uint32_t halfWidth = width / 2U;
        const std::uint32_t halfHeight = height / 2U;
        const std::uint32_t widthRemainder = width - halfWidth;
        const std::uint32_t heightRemainder = height - halfHeight;

        BuildRecursive(previousLuma, currentLuma, frameWidth, frameHeight, x, y, halfWidth, halfHeight, depth + 1U, nodeMap);
        BuildRecursive(previousLuma, currentLuma, frameWidth, frameHeight, x + halfWidth, y, widthRemainder, halfHeight, depth + 1U, nodeMap);
        BuildRecursive(previousLuma, currentLuma, frameWidth, frameHeight, x, y + halfHeight, halfWidth, heightRemainder, depth + 1U, nodeMap);
        BuildRecursive(previousLuma, currentLuma, frameWidth, frameHeight, x + halfWidth, y + halfHeight, widthRemainder, heightRemainder, depth + 1U, nodeMap);
        return;
    }

    const float detailStrength = Clamp01(std::sqrt(std::max(stats.variance, 0.0f)) * 9.0f);
    const float temporalConfidence = previousLuma == nullptr
        ? 1.0f
        : Clamp01(1.0f - stats.temporalDifference * 6.5f);
    const float searchRadiusNorm = Clamp01((1.0f + stats.temporalDifference * 18.0f + detailStrength * 1.5f) / 4.0f);
    const float maxDimension = static_cast<float>(std::max(frameWidth, frameHeight));
    const float leafSize = static_cast<float>(std::max(width, height));
    const float leafRefinement = 1.0f - Clamp01(std::log2(std::max(leafSize, 1.0f)) / std::log2(std::max(maxDimension, 2.0f)));

    FillRegion(nodeMap, x, y, width, height, detailStrength, temporalConfidence, searchRadiusNorm, leafRefinement);
}

void QuadTreeAnalyzer::FillRegion(NodeMap& nodeMap,
                                  const std::uint32_t x,
                                  const std::uint32_t y,
                                  const std::uint32_t width,
                                  const std::uint32_t height,
                                  const float detailStrength,
                                  const float temporalConfidence,
                                  const float searchRadiusNorm,
                                  const float leafRefinement) {
    for (std::uint32_t row = y; row < y + height; ++row) {
        for (std::uint32_t column = x; column < x + width; ++column) {
            const std::size_t index = (static_cast<std::size_t>(row) * nodeMap.width + column) * 4U;
            nodeMap.texels[index + 0] = detailStrength;
            nodeMap.texels[index + 1] = temporalConfidence;
            nodeMap.texels[index + 2] = searchRadiusNorm;
            nodeMap.texels[index + 3] = leafRefinement;
        }
    }
}

}  // namespace ejmdk
