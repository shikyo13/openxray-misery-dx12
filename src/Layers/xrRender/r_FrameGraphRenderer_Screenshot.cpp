#include "stdafx.h"
#include "r_FrameGraphRenderer.h"
#include "xrCore/Media/Image.hpp"
#include "xrEngine/IGame_Level.h"
#include "xrEngine/IRenderBackend.h"

#if defined(XR_PLATFORM_WINDOWS)
#include <DirectXTex.h>
#endif

namespace xray::render
{
void FrameGraphRenderer::Screenshot(IRender::ScreenshotMode mode, pcstr name)
{
    auto* backend = GEnv.Backend;
    if (!backend || !backend->IsInitialized())
        return;

    // Normal input and save-preview requests run between frames. A request made
    // during rendering must wait until that command list has been submitted.
    if (backend->IsInFrame())
    {
        m_pendingScreenshots.emplace_back(mode, name ? name : "");
        return;
    }

#if defined(XR_PLATFORM_WINDOWS)
    if (mode != IRender::SM_NORMAL && (!name || !name[0]))
    {
        Msg("! [Screenshot] A filename is required for mode %u", mode);
        return;
    }

    // This also joins backend garbage collection before accessing NVRHI.
    backend->WaitForIdle();
    auto* device = backend->GetDevice();
    auto* source = backend->GetBackBuffer();
    if (!device || !source)
        return;

    const auto& desc = source->getDesc();
    const bool bgra = desc.format == nvrhi::Format::BGRA8_UNORM || desc.format == nvrhi::Format::SBGRA8_UNORM;
    if (!bgra && desc.format != nvrhi::Format::RGBA8_UNORM && desc.format != nvrhi::Format::SRGBA8_UNORM)
    {
        Msg("! [Screenshot] Unsupported backbuffer format %u", static_cast<u32>(desc.format));
        return;
    }

    auto readback = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
    auto commandList = device->createCommandList();
    if (!readback || !commandList)
    {
        Msg("! [Screenshot] Cannot create readback resources");
        return;
    }
    commandList->open();
    commandList->copyTexture(readback, nvrhi::TextureSlice(), source, nvrhi::TextureSlice());
    commandList->setTextureState(source, nvrhi::AllSubresources, nvrhi::ResourceStates::Present);
    commandList->commitBarriers();
    commandList->close();
    device->executeCommandList(commandList);

    size_t rowPitch = 0;
    // Mapping waits for the copy's completion fence before exposing GPU pixels.
    auto* pixels = static_cast<const u8*>(device->mapStagingTexture(
        readback, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
    if (!pixels)
    {
        Msg("! [Screenshot] Cannot map backbuffer readback");
        return;
    }

    DirectX::ScratchImage image;
    HRESULT result = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, desc.width, desc.height, 1, 1);
    if (SUCCEEDED(result))
    {
        const auto* output = image.GetImage(0, 0, 0);
        for (u32 y = 0; y < desc.height; ++y)
        {
            auto* row = output->pixels + y * output->rowPitch;
            std::memcpy(row, pixels + y * rowPitch, static_cast<size_t>(desc.width) * 4);
            if (bgra)
                for (u32 x = 0; x < desc.width; ++x)
                    std::swap(row[x * 4], row[x * 4 + 2]);
        }
    }
    device->unmapStagingTexture(readback);
    if (FAILED(result))
    {
        Msg("! [Screenshot] Image allocation failed: 0x%08x", static_cast<u32>(result));
        return;
    }

    auto writeBlob = [&](pcstr path, const DirectX::Blob& blob, bool fullPath)
    {
        IWriter* file = fullPath ? FS.w_open(path) : FS.w_open("$screenshots$", path);
        if (!file)
        {
            Msg("! [Screenshot] Cannot write %s", path);
            return false;
        }
        file->w(blob.GetBufferPointer(), blob.GetBufferSize());
        FS.w_close(file);
        return true;
    };

    string_path filename;
    bool saved = false;
    if (mode == IRender::SM_FOR_GAMESAVE)
    {
        DirectX::ScratchImage resized, compressed;
        result = DirectX::Resize(*image.GetImage(0, 0, 0), 128, 128,
            DirectX::TEX_FILTER_TRIANGLE | DirectX::TEX_FILTER_FORCE_NON_WIC, resized);
        if (SUCCEEDED(result))
            result = DirectX::Compress(*resized.GetImage(0, 0, 0), DXGI_FORMAT_BC1_UNORM,
                DirectX::TEX_COMPRESS_DEFAULT, 0.0f, compressed);
        DirectX::Blob blob;
        if (SUCCEEDED(result))
            result = DirectX::SaveToDDSMemory(*compressed.GetImage(0, 0, 0), DirectX::DDS_FLAGS_FORCE_DX9_LEGACY, blob);
        xr_strcpy(filename, name);
        if (SUCCEEDED(result))
            saved = writeBlob(filename, blob, true);
    }
    else if (mode == IRender::SM_NORMAL)
    {
        string64 stamp;
        xr_sprintf(filename, "ss_%s_%s_(%s)_f%u.jpg", Core.UserName, timestamp(stamp),
            g_pGameLevel ? g_pGameLevel->name().c_str() : "mainmenu", Device.dwFrame);
        if (IWriter* file = FS.w_open("$screenshots$", filename))
        {
            XRay::Media::Image jpeg(desc.width, desc.height, image.GetPixels(), XRay::Media::ImageDataFormat::RGBA8);
            saved = jpeg.SaveJPEG(*file, 95);
            FS.w_close(file);
        }
        if (strstr(Core.Params, "-ss_tga"))
        {
            string_path hqName;
            xr_sprintf(hqName, "ssq_%s_%s_(%s)_f%u.tga", Core.UserName, stamp,
                g_pGameLevel ? g_pGameLevel->name().c_str() : "mainmenu", Device.dwFrame);
            DirectX::Blob blob;
            result = DirectX::SaveToTGAMemory(*image.GetImage(0, 0, 0), blob);
            if (SUCCEEDED(result))
                writeBlob(hqName, blob, false);
        }
    }
    else if (mode == IRender::SM_FOR_CUBEMAP || mode == IRender::SM_FOR_LEVELMAP)
    {
        DirectX::ScratchImage resized;
        result = DirectX::Resize(*image.GetImage(0, 0, 0), desc.height, desc.height,
            DirectX::TEX_FILTER_TRIANGLE | DirectX::TEX_FILTER_FORCE_NON_WIC, resized);
        DirectX::Blob blob;
        if (SUCCEEDED(result))
            result = DirectX::SaveToTGAMemory(*resized.GetImage(0, 0, 0), blob);
        xr_sprintf(filename, "%s.tga", name);
        if (SUCCEEDED(result))
            saved = writeBlob(filename, blob, false);
    }
    else
    {
        Msg("! [Screenshot] Unknown mode %u", mode);
        return;
    }

    if (saved)
        Msg("* [Screenshot] Saved %s from %s backbuffer, %ux%u, frame %u",
            filename, backend->GetAPIName(), desc.width, desc.height, Device.dwFrame);
    else
        Msg("! [Screenshot] Failed to save %s: 0x%08x", filename, static_cast<u32>(result));
#else
    Msg("! [Screenshot] FrameGraph image encoding is unavailable on this platform");
#endif
}
} // namespace xray::render
