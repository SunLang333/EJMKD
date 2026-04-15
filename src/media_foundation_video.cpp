#include "media_foundation_video.h"

#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ejmdk {
namespace {

using Microsoft::WRL::ComPtr;

[[nodiscard]] std::runtime_error HrError(const HRESULT hr, const char* message) {
    std::ostringstream stream;
    stream << message << " (HRESULT=0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr) << ")";
    return std::runtime_error(stream.str());
}

void ThrowIfFailed(const HRESULT hr, const char* message) {
    if (FAILED(hr)) {
        throw HrError(hr, message);
    }
}

[[nodiscard]] std::uint8_t ClampByte(const float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f));
}

[[nodiscard]] std::vector<std::uint8_t> ConvertRgbaToNv12(const RgbaFrame& frame) {
    if (frame.width % 2U != 0U || frame.height % 2U != 0U) {
        throw std::runtime_error("NV12 encoding requires even output dimensions.");
    }

    const std::size_t yPlaneSize = static_cast<std::size_t>(frame.width) * frame.height;
    std::vector<std::uint8_t> output(yPlaneSize + yPlaneSize / 2U, 0U);
    std::uint8_t* yPlane = output.data();
    std::uint8_t* uvPlane = output.data() + yPlaneSize;

    auto samplePixel = [&](const std::uint32_t x, const std::uint32_t y) {
        const std::size_t index = (static_cast<std::size_t>(y) * frame.width + x) * 4U;
        const float r = static_cast<float>(frame.pixels[index + 0]);
        const float g = static_cast<float>(frame.pixels[index + 1]);
        const float b = static_cast<float>(frame.pixels[index + 2]);
        return std::array<float, 3>{r, g, b};
    };

    for (std::uint32_t y = 0; y < frame.height; ++y) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto rgb = samplePixel(x, y);
            const float yValue = 0.182586f * rgb[0] + 0.614231f * rgb[1] + 0.062007f * rgb[2] + 16.0f;
            yPlane[static_cast<std::size_t>(y) * frame.width + x] = ClampByte(yValue);
        }
    }

    for (std::uint32_t y = 0; y < frame.height; y += 2U) {
        for (std::uint32_t x = 0; x < frame.width; x += 2U) {
            float u = 0.0f;
            float v = 0.0f;
            for (std::uint32_t subY = 0; subY < 2U; ++subY) {
                for (std::uint32_t subX = 0; subX < 2U; ++subX) {
                    const auto rgb = samplePixel(x + subX, y + subY);
                    u += -0.100644f * rgb[0] - 0.338572f * rgb[1] + 0.439216f * rgb[2] + 128.0f;
                    v +=  0.439216f * rgb[0] - 0.398942f * rgb[1] - 0.040274f * rgb[2] + 128.0f;
                }
            }
            u *= 0.25f;
            v *= 0.25f;
            const std::size_t uvIndex = (static_cast<std::size_t>(y) / 2U) * frame.width + x;
            uvPlane[uvIndex + 0] = ClampByte(u);
            uvPlane[uvIndex + 1] = ClampByte(v);
        }
    }

    return output;
}

}  // namespace

struct MfVideoReader::Impl {
    ComPtr<IMFSourceReader> reader;
    VideoStreamInfo info;
    LONG sourceStride = 0;
};

struct MfVideoWriter::Impl {
    ComPtr<IMFSinkWriter> writer;
    DWORD streamIndex = 0;
    std::uint64_t frameDuration = 0;
    LONGLONG nextTimestamp = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool finalized = false;
};

MediaFoundationRuntime::MediaFoundationRuntime() {
    ThrowIfFailed(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx failed");
    ThrowIfFailed(MFStartup(MF_VERSION, MFSTARTUP_FULL), "MFStartup failed");
}

MediaFoundationRuntime::~MediaFoundationRuntime() {
    MFShutdown();
    CoUninitialize();
}

MfVideoReader::MfVideoReader(const std::wstring& inputPath)
    : impl_(std::make_unique<Impl>()) {
    ComPtr<IMFAttributes> attributes;
    ThrowIfFailed(MFCreateAttributes(&attributes, 2), "MFCreateAttributes failed for source reader");
    ThrowIfFailed(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1U), "Failed to enable source reader video processing");
    ThrowIfFailed(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1U), "Failed to enable hardware transforms on source reader");
    ThrowIfFailed(MFCreateSourceReaderFromURL(inputPath.c_str(), attributes.Get(), &impl_->reader), "MFCreateSourceReaderFromURL failed");

    ThrowIfFailed(impl_->reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, 0U), "Failed to clear source reader stream selection");
    ThrowIfFailed(impl_->reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 1U), "Failed to select video stream");

    ComPtr<IMFMediaType> requestedType;
    ThrowIfFailed(MFCreateMediaType(&requestedType), "MFCreateMediaType failed for requested reader type");
    ThrowIfFailed(requestedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Failed to set requested reader major type");
    ThrowIfFailed(requestedType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Failed to request RGB32 video decoding");
    ThrowIfFailed(impl_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, requestedType.Get()), "Failed to set source reader output type to RGB32");

    ComPtr<IMFMediaType> currentType;
    ThrowIfFailed(impl_->reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType), "Failed to query active reader media type");
    ThrowIfFailed(MFGetAttributeSize(currentType.Get(), MF_MT_FRAME_SIZE, &impl_->info.width, &impl_->info.height), "Failed to query frame size");
    ThrowIfFailed(MFGetAttributeRatio(currentType.Get(), MF_MT_FRAME_RATE, &impl_->info.frameRateNumerator, &impl_->info.frameRateDenominator), "Failed to query frame rate");

    UINT32 strideValue = 0;
    if (FAILED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideValue))) {
        LONG computedStride = 0;
        ThrowIfFailed(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, static_cast<LONG>(impl_->info.width), &computedStride), "Failed to determine RGB32 stride");
        impl_->sourceStride = computedStride;
    } else {
        impl_->sourceStride = static_cast<LONG>(strideValue);
    }

    PROPVARIANT duration{};
    PropVariantInit(&duration);
    if (SUCCEEDED(impl_->reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration)) && duration.vt == VT_UI8) {
        impl_->info.durationSeconds = static_cast<double>(duration.uhVal.QuadPart) / 10000000.0;
    }
    PropVariantClear(&duration);
}

MfVideoReader::~MfVideoReader() = default;

const VideoStreamInfo& MfVideoReader::info() const noexcept {
    return impl_->info;
}

bool MfVideoReader::ReadFrame(RgbaFrame& frame) {
    while (true) {
        DWORD streamFlags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        ThrowIfFailed(impl_->reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                0,
                                                nullptr,
                                                &streamFlags,
                                                &timestamp,
                                                &sample),
                      "Failed to read video sample");

        if ((streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) != 0U) {
            return false;
        }
        if (sample == nullptr) {
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        ThrowIfFailed(sample->ConvertToContiguousBuffer(&buffer), "Failed to convert video sample to a contiguous buffer");

        BYTE* rawData = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        ThrowIfFailed(buffer->Lock(&rawData, &maxLength, &currentLength), "Failed to lock sample buffer");

        frame.width = impl_->info.width;
        frame.height = impl_->info.height;
        frame.pts100ns = timestamp;
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height * 4U, 255U);

        const LONG stride = impl_->sourceStride == 0 ? static_cast<LONG>(frame.width * 4U) : impl_->sourceStride;
        const LONG absoluteStride = stride >= 0 ? stride : -stride;
        const BYTE* rowStart = stride >= 0 ? rawData : rawData + static_cast<std::size_t>(frame.height - 1U) * absoluteStride;

        for (std::uint32_t y = 0; y < frame.height; ++y) {
            const BYTE* sourceRow = stride >= 0 ? rowStart + static_cast<std::size_t>(y) * absoluteStride
                                                : rowStart - static_cast<std::size_t>(y) * absoluteStride;
            for (std::uint32_t x = 0; x < frame.width; ++x) {
                const std::size_t sourceIndex = static_cast<std::size_t>(x) * 4U;
                const std::size_t targetIndex = (static_cast<std::size_t>(y) * frame.width + x) * 4U;
                frame.pixels[targetIndex + 0] = sourceRow[sourceIndex + 2];
                frame.pixels[targetIndex + 1] = sourceRow[sourceIndex + 1];
                frame.pixels[targetIndex + 2] = sourceRow[sourceIndex + 0];
                frame.pixels[targetIndex + 3] = 255U;
            }
        }

        buffer->Unlock();
        ++impl_->info.frameCount;
        return true;
    }
}

MfVideoWriter::MfVideoWriter(const std::wstring& outputPath,
                             const std::uint32_t width,
                             const std::uint32_t height,
                             const std::uint32_t frameRateNumerator,
                             const std::uint32_t frameRateDenominator,
                             const std::uint32_t bitrateMbps)
    : impl_(std::make_unique<Impl>()) {
    impl_->width = width;
    impl_->height = height;

    ComPtr<IMFAttributes> attributes;
    ThrowIfFailed(MFCreateAttributes(&attributes, 2), "MFCreateAttributes failed for sink writer");
    ThrowIfFailed(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1U), "Failed to enable hardware transforms for sink writer");
    ThrowIfFailed(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, 1U), "Failed to disable sink writer throttling");
    ThrowIfFailed(MFCreateSinkWriterFromURL(outputPath.c_str(), nullptr, attributes.Get(), &impl_->writer), "MFCreateSinkWriterFromURL failed");

    ComPtr<IMFMediaType> outputType;
    ThrowIfFailed(MFCreateMediaType(&outputType), "MFCreateMediaType failed for output type");
    ThrowIfFailed(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Failed to set output major type");
    ThrowIfFailed(outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "Failed to set output subtype to H264");
    ThrowIfFailed(outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrateMbps * 1000U * 1000U), "Failed to set average bitrate");
    ThrowIfFailed(outputType->SetUINT32(MF_MT_INTERLACE_MODE, static_cast<UINT32>(MFVideoInterlace_Progressive)), "Failed to set progressive output");
    ThrowIfFailed(MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width, height), "Failed to set output frame size");
    ThrowIfFailed(MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, frameRateNumerator, frameRateDenominator), "Failed to set output frame rate");
    ThrowIfFailed(MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Failed to set output pixel aspect ratio");
    ThrowIfFailed(impl_->writer->AddStream(outputType.Get(), &impl_->streamIndex), "Failed to add H264 output stream");

    ComPtr<IMFMediaType> inputType;
    ThrowIfFailed(MFCreateMediaType(&inputType), "MFCreateMediaType failed for input type");
    ThrowIfFailed(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Failed to set input major type");
    ThrowIfFailed(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12), "Failed to set input subtype to NV12");
    ThrowIfFailed(inputType->SetUINT32(MF_MT_INTERLACE_MODE, static_cast<UINT32>(MFVideoInterlace_Progressive)), "Failed to set input progressive scan mode");
    ThrowIfFailed(MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width, height), "Failed to set input frame size");
    ThrowIfFailed(MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, frameRateNumerator, frameRateDenominator), "Failed to set input frame rate");
    ThrowIfFailed(MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Failed to set input pixel aspect ratio");
    ThrowIfFailed(impl_->writer->SetInputMediaType(impl_->streamIndex, inputType.Get(), nullptr), "Failed to configure sink writer input type");

    ThrowIfFailed(MFFrameRateToAverageTimePerFrame(frameRateNumerator, frameRateDenominator, &impl_->frameDuration), "Failed to compute frame duration");
    ThrowIfFailed(impl_->writer->BeginWriting(), "Sink writer failed to begin writing");
}

MfVideoWriter::~MfVideoWriter() {
    if (impl_ != nullptr && !impl_->finalized) {
        try {
            Finalize();
        } catch (...) {
        }
    }
}

void MfVideoWriter::WriteFrame(const RgbaFrame& frame) {
    if (frame.width != impl_->width || frame.height != impl_->height) {
        throw std::runtime_error("Frame dimensions do not match the configured writer output size.");
    }

    const std::vector<std::uint8_t> nv12 = ConvertRgbaToNv12(frame);
    ComPtr<IMFMediaBuffer> buffer;
    ThrowIfFailed(MFCreateMemoryBuffer(static_cast<DWORD>(nv12.size()), &buffer), "MFCreateMemoryBuffer failed for NV12 frame");

    BYTE* bufferData = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    ThrowIfFailed(buffer->Lock(&bufferData, &maxLength, &currentLength), "Failed to lock sink writer buffer");
    std::memcpy(bufferData, nv12.data(), nv12.size());
    ThrowIfFailed(buffer->Unlock(), "Failed to unlock sink writer buffer");
    ThrowIfFailed(buffer->SetCurrentLength(static_cast<DWORD>(nv12.size())), "Failed to set NV12 buffer length");

    ComPtr<IMFSample> sample;
    ThrowIfFailed(MFCreateSample(&sample), "MFCreateSample failed for sink writer sample");
    ThrowIfFailed(sample->AddBuffer(buffer.Get()), "Failed to attach buffer to sink writer sample");
    ThrowIfFailed(sample->SetSampleTime(impl_->nextTimestamp), "Failed to set sample time");
    ThrowIfFailed(sample->SetSampleDuration(static_cast<LONGLONG>(impl_->frameDuration)), "Failed to set sample duration");
    ThrowIfFailed(impl_->writer->WriteSample(impl_->streamIndex, sample.Get()), "Sink writer failed to write sample");

    impl_->nextTimestamp += static_cast<LONGLONG>(impl_->frameDuration);
}

void MfVideoWriter::Finalize() {
    if (impl_->finalized) {
        return;
    }

    ThrowIfFailed(impl_->writer->Finalize(), "Sink writer finalize failed");
    impl_->finalized = true;
}

}  // namespace ejmdk
