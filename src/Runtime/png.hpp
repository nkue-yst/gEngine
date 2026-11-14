#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

namespace gengine
{
    // Windows標準のPNGエンコーダーを使い、画像は呼出し元へ所有値として返す。
    inline std::vector<std::uint8_t> EncodePng(std::span<std::uint8_t> pixels, UINT width, UINT height)
    {
        const auto check = [](HRESULT hr) {
            if (FAILED(hr)) {
                throw std::runtime_error("PNG encoding failed");
            }
        };
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        Microsoft::WRL::ComPtr<IStream> stream;
        check(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
        check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
        check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
        check(encoder->CreateNewFrame(&frame, nullptr));
        check(frame->Initialize(nullptr));
        check(frame->SetSize(width, height));
        // rEngineのRGBAをPNGエンコーダーが扱うBGRAへ変換する。
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]);
        }
        auto format = GUID_WICPixelFormat32bppBGRA;
        check(frame->SetPixelFormat(&format));
        if (format != GUID_WICPixelFormat32bppBGRA) {
            throw std::runtime_error("PNG pixel format changed");
        }
        check(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
        check(frame->Commit());
        check(encoder->Commit());
        auto stat = STATSTG{};
        check(stream->Stat(&stat, STATFLAG_NONAME));
        // 4KのRGBA画像を収める。通信の上限は画像を送るEditor側で別に検査する。
        if (stat.cbSize.QuadPart > 32 * 1024 * 1024) {
            throw std::runtime_error("PNG exceeds image budget");
        }
        check(stream->Seek({}, STREAM_SEEK_SET, nullptr));
        auto png = std::vector<std::uint8_t>(static_cast<std::size_t>(stat.cbSize.QuadPart));
        auto read = ULONG{};
        check(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read));
        if (read != png.size()) {
            throw std::runtime_error("incomplete PNG stream");
        }
        return png;
    }
} // namespace gengine
