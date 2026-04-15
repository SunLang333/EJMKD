#pragma once

#include "quadtree_analyzer.h"
#include "video_types.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ejmdk {

class VulkanEjmdkProcessor {
public:
    VulkanEjmdkProcessor();
    ~VulkanEjmdkProcessor();

    VulkanEjmdkProcessor(const VulkanEjmdkProcessor&) = delete;
    VulkanEjmdkProcessor& operator=(const VulkanEjmdkProcessor&) = delete;

    void Configure(std::uint32_t sourceWidth, std::uint32_t sourceHeight, std::uint32_t scaleFactor);
    [[nodiscard]] std::string DeviceName() const;
    [[nodiscard]] RgbaFrame Process(const RgbaFrame* previousFrame,
                                    const RgbaFrame& currentFrame,
                                    const NodeMap& nodeMap,
                                    float interpolationTime);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ejmdk
