#include "media_foundation_video.h"
#include "quadtree_analyzer.h"
#include "vulkan_ejmdk.h"

#include <Windows.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <algorithm>

namespace ejmdk {
namespace {

namespace fs = std::filesystem;

struct CommandLineOptions {
    fs::path inputPath;
    fs::path outputPath;
    std::uint32_t scale = 2;
    bool enableFrameGeneration = true;
};

[[nodiscard]] std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        throw std::runtime_error("Failed to convert UTF-16 string to UTF-8.");
    }

    std::string output(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), length, nullptr, nullptr);
    return output;
}

void PrintUsage() {
    std::cout
        << "Usage:\n"
        << "  ejmdk.exe <input.mp4> [output.mp4] [--scale 2]\n"
        << "  ejmdk.exe --input <input.mp4> --output <output.mp4> [--scale 2] [--no-fg]\n\n"
        << "Examples:\n"
        << "  ejmdk.exe sample-5.mp4\n"
        << "  ejmdk.exe -i sample-5.mp4 -o sample-5_ejmdk.mp4 --scale 2\n";
}

[[nodiscard]] fs::path BuildDefaultOutputPath(const fs::path& inputPath, const std::uint32_t scale, const bool enableFrameGeneration) {
    const std::wstring suffix = enableFrameGeneration ? L"_ejmdk_fg_x" : L"_ejmdk_sr_x";
    return inputPath.parent_path() / (inputPath.stem().wstring() + suffix + std::to_wstring(scale) + L".mp4");
}

[[nodiscard]] CommandLineOptions ParseArguments(int argc, wchar_t** argv) {
    CommandLineOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--help" || argument == L"-h") {
            PrintUsage();
            std::exit(0);
        }
        if (argument == L"--input" || argument == L"-i") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value for --input.");
            }
            options.inputPath = argv[++index];
            continue;
        }
        if (argument == L"--output" || argument == L"-o") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value for --output.");
            }
            options.outputPath = argv[++index];
            continue;
        }
        if (argument == L"--scale") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value for --scale.");
            }
            options.scale = static_cast<std::uint32_t>(std::stoul(argv[++index]));
            if (options.scale == 0U) {
                throw std::runtime_error("Scale must be at least 1.");
            }
            continue;
        }
        if (argument == L"--no-fg") {
            options.enableFrameGeneration = false;
            continue;
        }
        if (!argument.empty() && argument[0] == L'-') {
            throw std::runtime_error("Unknown argument: " + WideToUtf8(argument));
        }

        if (options.inputPath.empty()) {
            options.inputPath = argument;
        } else if (options.outputPath.empty()) {
            options.outputPath = argument;
        } else {
            throw std::runtime_error("Too many positional arguments.");
        }
    }

    if (options.inputPath.empty()) {
        PrintUsage();
        throw std::runtime_error("An input video path is required.");
    }

    if (options.outputPath.empty()) {
        options.outputPath = BuildDefaultOutputPath(options.inputPath, options.scale, options.enableFrameGeneration);
    }

    return options;
}

[[nodiscard]] std::uint32_t EstimateBitrateMbps(const std::uint32_t width,
                                                const std::uint32_t height,
                                                const bool enableFrameGeneration) {
    const double megapixels = static_cast<double>(width) * static_cast<double>(height) / 1'000'000.0;
    const double factor = enableFrameGeneration ? 14.0 : 10.0;
    return std::max<std::uint32_t>(8U, static_cast<std::uint32_t>(std::lround(std::max(1.0, megapixels) * factor)));
}

[[nodiscard]] std::string FormatSeconds(const double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

}  // namespace
}  // namespace ejmdk

int wmain(int argc, wchar_t** argv) {
    using namespace ejmdk;
    using Clock = std::chrono::steady_clock;

    try {
        const CommandLineOptions options = ParseArguments(argc, argv);
        const auto startTime = Clock::now();

        MediaFoundationRuntime mediaFoundation;
        MfVideoReader reader(options.inputPath.wstring());
        const VideoStreamInfo& inputInfo = reader.info();

        if (inputInfo.width == 0U || inputInfo.height == 0U) {
            throw std::runtime_error("Input stream has invalid video dimensions.");
        }

        const std::uint32_t outputWidth = inputInfo.width * options.scale;
        const std::uint32_t outputHeight = inputInfo.height * options.scale;
        const std::uint32_t outputFrameRateNumerator = inputInfo.frameRateNumerator * (options.enableFrameGeneration ? 2U : 1U);
        const std::uint32_t bitrateMbps = EstimateBitrateMbps(outputWidth, outputHeight, options.enableFrameGeneration);

        MfVideoWriter writer(options.outputPath.wstring(),
                             outputWidth,
                             outputHeight,
                             outputFrameRateNumerator,
                             inputInfo.frameRateDenominator,
                             bitrateMbps);

        QuadTreeAnalyzer analyzer;
        VulkanEjmdkProcessor processor;
        processor.Configure(inputInfo.width, inputInfo.height, options.scale);

        std::cout << "Input : " << WideToUtf8(options.inputPath.wstring()) << "\n";
        std::cout << "Output: " << WideToUtf8(options.outputPath.wstring()) << "\n";
        std::cout << "Mode  : " << (options.enableFrameGeneration ? "SR + FG" : "SR only") << "\n";
        std::cout << "Scale : x" << options.scale << "\n";
        std::cout << "GPU   : " << processor.DeviceName() << "\n";

        RgbaFrame currentFrame;
        if (!reader.ReadFrame(currentFrame)) {
            throw std::runtime_error("Input video does not contain any decodable frames.");
        }

        NodeMap firstNodeMap = analyzer.Build(nullptr, currentFrame);
        writer.WriteFrame(processor.Process(nullptr, currentFrame, firstNodeMap, 1.0f));

        std::uint64_t sourceFramesProcessed = 1;
        std::uint64_t outputFramesWritten = 1;
        RgbaFrame previousFrame = std::move(currentFrame);

        while (reader.ReadFrame(currentFrame)) {
            NodeMap nodeMap = analyzer.Build(&previousFrame, currentFrame);
            if (options.enableFrameGeneration) {
                writer.WriteFrame(processor.Process(&previousFrame, currentFrame, nodeMap, 0.5f));
                ++outputFramesWritten;
            }

            writer.WriteFrame(processor.Process(&previousFrame, currentFrame, nodeMap, 1.0f));
            ++outputFramesWritten;
            ++sourceFramesProcessed;
            previousFrame = std::move(currentFrame);

            if (sourceFramesProcessed % 16U == 0U) {
                std::cout << "Processed source frames: " << sourceFramesProcessed
                          << ", output frames written: " << outputFramesWritten << "\r" << std::flush;
            }
        }

        writer.Finalize();
        const double elapsedSeconds = std::chrono::duration<double>(Clock::now() - startTime).count();
        std::cout << "\nCompleted in " << FormatSeconds(elapsedSeconds) << " s"
                  << " | source frames: " << sourceFramesProcessed
                  << " | output frames: " << outputFramesWritten << "\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ejmdk failed: " << exception.what() << "\n";
        return 1;
    }
}
