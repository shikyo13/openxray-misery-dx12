// SmokeTrailManager.cpp
// GPU smoke trail manager: owns buffers and per-frame compute constants.
// Shot-driven emission with simulation independent of the active weapon.
#include "stdafx.h"
#include "SmokeTrailManager.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"

namespace xray::render::fg {
    extern float ps_r_smoke_max_emit_rate;
    extern float ps_r_smoke_point_lifetime;
    extern float ps_r_smoke_max_width;
    extern float ps_r_smoke_gravity;
    extern float ps_r_smoke_buoyancy;
    extern float ps_r_smoke_turbulence;
    extern int   ps_r_smoke_trail_enabled;
}

namespace xray::render::fg::passes {

bool SmokeTrailManager::Initialize(fg::RenderDevice* device)
{
    m_device = device;
    nvrhi::IDevice* nvDevice = device->GetNVRHIDevice();

    // Simulation ring buffer (UAV): SmokeSimPoint[MAX_POINTS]
    {
        nvrhi::BufferDesc desc;
        desc.structStride     = sizeof(SmokeSimPoint);
        desc.byteSize         = MAX_POINTS * sizeof(SmokeSimPoint);
        desc.initialState     = nvrhi::ResourceStates::UnorderedAccess;
        desc.keepInitialState = true;
        desc.canHaveUAVs      = true;
        desc.debugName        = "SmokeSimBuffer";
        m_simBuffer = nvDevice->createBuffer(desc);
        if (!m_simBuffer)
            return false;
    }

    // Compact output buffer (UAV during compact, SRV during draw): GPUTrailControlPoint[MAX_POINTS]
    {
        nvrhi::BufferDesc desc;
        desc.structStride     = sizeof(GPUTrailControlPoint);
        desc.byteSize         = MAX_POINTS * sizeof(GPUTrailControlPoint);
        desc.initialState     = nvrhi::ResourceStates::UnorderedAccess;
        desc.keepInitialState = true;
        desc.canHaveUAVs      = true;
        desc.debugName        = "SmokeCompactBuffer";
        m_compactBuffer = nvDevice->createBuffer(desc);
        if (!m_compactBuffer)
            return false;
    }

    // Raw state buffer (UAV/SRV): {head, totalSpawned, liveCount, totalDist_bits}
    {
        nvrhi::BufferDesc desc;
        desc.byteSize         = 4 * sizeof(u32);
        desc.initialState     = nvrhi::ResourceStates::UnorderedAccess;
        desc.keepInitialState = true;
        desc.canHaveUAVs      = true;
        desc.canHaveRawViews  = true;
        desc.debugName        = "SmokeStateBuffer";
        m_stateBuffer = nvDevice->createBuffer(desc);
        if (!m_stateBuffer)
            return false;
    }

    // DrawIndirectArguments buffer (non-indexed: vertexCount, instanceCount, startVertex, startInstance)
    {
        nvrhi::BufferDesc desc;
        desc.byteSize          = 4 * sizeof(u32);
        desc.initialState      = nvrhi::ResourceStates::UnorderedAccess;
        desc.keepInitialState  = true;
        desc.canHaveUAVs       = true;
        desc.canHaveRawViews   = true;
        desc.isDrawIndirectArgs = true;
        desc.debugName         = "SmokeDrawArgs";
        m_drawArgsBuffer = nvDevice->createBuffer(desc);
        if (!m_drawArgsBuffer)
            return false;
    }

    // Zero-initialize all GPU buffers
    {
        auto cmdList = nvDevice->createCommandList();
        cmdList->open();
        cmdList->clearBufferUInt(m_simBuffer, 0);
        cmdList->clearBufferUInt(m_compactBuffer, 0);
        cmdList->clearBufferUInt(m_stateBuffer, 0);
        cmdList->clearBufferUInt(m_drawArgsBuffer, 0);
        cmdList->close();
        nvDevice->executeCommandList(cmdList);
    }

    m_initialized = true;
    return true;
}

void SmokeTrailManager::Shutdown()
{
    m_simBuffer      = nullptr;
    m_compactBuffer  = nullptr;
    m_stateBuffer    = nullptr;
    m_drawArgsBuffer = nullptr;

    m_hasPrevMuzzle = false;
    m_muzzleUpdated = false;
    m_weaponId = u16(-1);
    m_emitAccum = 0.f;
    m_heat = 0.f;
    m_emitParams = {};
    m_simParams = {};
    m_compactParams = {};
    m_device = nullptr;
    m_initialized   = false;
}

void SmokeTrailManager::SelectWeapon(u16 weaponId)
{
    if (m_weaponId == weaponId)
        return;

    m_weaponId = weaponId;
    m_hasPrevMuzzle = false;
    m_muzzleUpdated = false;
    m_emitAccum = 0.f;
    m_heat = 0.f;
}

void SmokeTrailManager::UpdateMuzzle(u16 weaponId, const Fvector& muzzlePos, const Fvector& muzzleDir)
{
    SelectWeapon(weaponId);
    m_muzzlePos = muzzlePos;
    m_muzzleDir = muzzleDir;
    m_muzzleUpdated = true;
}

void SmokeTrailManager::OnShot(u16 weaponId)
{
    if (!m_initialized || !ps_r_smoke_trail_enabled)
        return;

    SelectWeapon(weaponId);
    m_heat = std::min(m_heat + 0.4f, 1.f);
    if (strstr(Core.Params, "-smoke_trail_trace"))
        Msg("* [SmokeTrailTrace] shot frame=%u weapon=%u heat=%.3f", Device.dwFrame, weaponId, m_heat);
}

void SmokeTrailManager::PrepareFrame(float dt)
{
    using namespace xray::render::fg;

    if (!m_initialized)
        return;

    u32 emitCount = 0;
    if (!m_muzzleUpdated || !ps_r_smoke_trail_enabled)
    {
        m_heat = 0.f;
        m_emitAccum = 0.f;
        m_hasPrevMuzzle = false;
    }
    else
    {
        // Integrate linear cooling over this frame, including a partial final
        // frame, so emission does not depend on the rendering frame rate.
        constexpr float coolingRate = 0.5f;
        const float emittingTime = std::min(dt, m_heat / coolingRate);
        const float cooledHeat = std::max(0.f, m_heat - coolingRate * dt);
        m_emitAccum += ps_r_smoke_max_emit_rate * 0.5f * (m_heat + cooledHeat) * emittingTime;
        const u32 wholePoints = static_cast<u32>(m_emitAccum);
        m_emitAccum -= static_cast<float>(wholePoints);
        emitCount = std::min(wholePoints, MAX_POINTS);
        m_heat = cooledHeat;
    }

    if (!m_hasPrevMuzzle)
    {
        m_prevMuzzlePos = m_muzzlePos;
        m_hasPrevMuzzle = m_muzzleUpdated;
    }

    // Emit CB
    m_emitParams.prevPosX        = m_prevMuzzlePos.x;
    m_emitParams.prevPosY        = m_prevMuzzlePos.y;
    m_emitParams.prevPosZ        = m_prevMuzzlePos.z;
    m_emitParams.currPosX        = m_muzzlePos.x;
    m_emitParams.currPosY        = m_muzzlePos.y;
    m_emitParams.currPosZ        = m_muzzlePos.z;
    m_emitParams.emitDirX        = m_muzzleDir.x;
    m_emitParams.emitDirY        = m_muzzleDir.y;
    m_emitParams.emitDirZ        = m_muzzleDir.z;
    m_emitParams.baseLifetime    = ps_r_smoke_point_lifetime;
    m_emitParams.lifetimeVariance = 0.f;
    m_emitParams.maxWidth        = ps_r_smoke_max_width;
    m_emitParams.emitCount       = emitCount;
    m_emitParams.maxPoints       = MAX_POINTS;

    static u32 s_frameSeed = 0;
    s_frameSeed = s_frameSeed * 1664525u + 1013904223u;
    m_emitParams.frameSeed = static_cast<float>(s_frameSeed) * (1.f / 4294967296.f);
    m_emitParams.pad0      = 0.f;

    static const bool trace = strstr(Core.Params, "-smoke_trail_trace") != nullptr;
    if (trace && (emitCount || Device.dwFrame % 60 == 0))
        Msg("* [SmokeTrailTrace] frame=%u weapon=%u muzzle=%u heat=%.3f emit=%u dt=%.6f",
            Device.dwFrame, m_weaponId, m_muzzleUpdated ? 1u : 0u, m_heat, emitCount, dt);

    m_prevMuzzlePos = m_muzzlePos;
    m_muzzleUpdated = false;

    // Sim CB
    m_simParams.dt         = dt;
    m_simParams.gravity    = ps_r_smoke_gravity;
    m_simParams.buoyancy   = ps_r_smoke_buoyancy;
    m_simParams.turbulence = ps_r_smoke_turbulence;
    m_simParams.drag       = 0.7f;
    m_simParams.time       = Device.fTimeGlobal;
    m_simParams.maxPoints  = MAX_POINTS;
    m_simParams.heat01     = m_heat;

    // Compact CB
    m_compactParams.maxPoints      = MAX_POINTS;
    m_compactParams.subdivisions   = TRAIL_SUBDIVISIONS;
    m_compactParams.maxWidth       = ps_r_smoke_max_width;
    m_compactParams.turbAmount     = ps_r_smoke_turbulence;
    m_compactParams.turbFrequency  = 2.0f;
    m_compactParams.turbEvolution  = Device.fTimeGlobal * 0.5f;
    m_compactParams.sphereRadius   = 3.0f;
    m_compactParams.pad0           = 0.f;
    m_compactParams.sphereCenterX  = m_muzzlePos.x;
    m_compactParams.sphereCenterY  = m_muzzlePos.y;
    m_compactParams.sphereCenterZ  = m_muzzlePos.z;
    m_compactParams.pad1           = 0.f;
}

} // namespace xray::render::fg::passes
