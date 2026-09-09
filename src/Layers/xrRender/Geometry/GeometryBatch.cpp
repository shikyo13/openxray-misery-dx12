// xrRender/Geometry/GeometryBatch.cpp
#include "stdafx.h"
#include "GeometryBatch.h"

namespace xray::render {

// Global geometry collector instance (to be initialized by renderer)
GeometryCollector* g_geometryCollector = nullptr;

GeometryCollector::GeometryCollector() {
    m_batches.reserve(4096);  // Pre-allocate for typical scene
    Msg("* [GeometryCollector] Created");
}

GeometryCollector::~GeometryCollector() {
    Msg("* [GeometryCollector] Destroyed");
}

void GeometryCollector::BeginFrame(size_t staticPrefix) {
    R_ASSERT2(staticPrefix <= m_batches.size(), "Invalid retained static geometry prefix");
    // Static batches already occupy the front of this vector. Avoid releasing and
    // copying their buffer handles every frame; retire only last frame's dynamics.
    m_batches.resize(staticPrefix);
    if (m_batchIndicesValid) {
        while (!m_skinnedBatchIndices.empty() && m_skinnedBatchIndices.back() >= staticPrefix)
            m_skinnedBatchIndices.pop_back();
        while (!m_rigidUpdateBatchIndices.empty() && m_rigidUpdateBatchIndices.back() >= staticPrefix)
            m_rigidUpdateBatchIndices.pop_back();
    }

    // Reset statistics
    m_stats = Stats{};
}

void GeometryCollector::EndFrame() {
    const u64 materialRevision = MaterialSystem::Instance().GetCacheRevision();
    if (!m_batchIndicesValid || m_materialCacheRevision != materialRevision) {
        m_skinnedBatchIndices.clear();
        m_rigidUpdateBatchIndices.clear();
        for (size_t i = 0; i < m_batches.size(); ++i)
            IndexBatch(i);
        m_batchIndicesValid = true;
        m_materialCacheRevision = materialRevision;
    }

    // Optional runtime equivalence check; keep the full scan out of normal play.
    static const bool checkIndex = strstr(Core.Params, "-geometry_batch_check") != nullptr;
    if (checkIndex && Device.dwFrame % 120 == 0) {
        size_t expected = 0;
        for (size_t i = 0; i < m_batches.size(); ++i) {
            if (!m_batches[i].isSkinned) continue;
            R_ASSERT2(expected < m_skinnedBatchIndices.size() && m_skinnedBatchIndices[expected] == i,
                "Skinned batch index differs from full collection scan");
            ++expected;
        }
        R_ASSERT2(expected == m_skinnedBatchIndices.size(), "Skinned batch index contains stale entries");
        size_t rigidExpected = 0;
        for (size_t i = 0; i < m_batches.size(); ++i) {
            const auto& batch = m_batches[i];
            if (batch.isSkinned) continue;
            if (!batch.isTerrain && !batch.IsStrictB2F() && batch.isStatic) continue;
            R_ASSERT2(rigidExpected < m_rigidUpdateBatchIndices.size() && m_rigidUpdateBatchIndices[rigidExpected] == i,
                "Rigid upload index differs from full collection scan");
            ++rigidExpected;
        }
        R_ASSERT2(rigidExpected == m_rigidUpdateBatchIndices.size(), "Rigid upload index contains stale entries");
        Msg("* [GeometryBatchIndex] frame=%u batches=%zu skinned=%zu verified=1 rigid_updates=%zu",
            Device.dwFrame, m_batches.size(), expected, rigidExpected);
    }

    // Update statistics
    m_stats.numBatches = static_cast<u32>(m_batches.size());
}

void GeometryCollector::Submit(const GeometryBatch& batch) {
    VERIFY(batch.vertexBuffer != nullptr);  // nvrhi::BufferHandle is a smart pointer
    VERIFY(batch.indexBuffer != nullptr);
    VERIFY(batch.indexCount > 0);
    // NOTE: pipeline can be nullptr during collection, will be set later from visual->shader

    m_batches.push_back(batch);
    if (m_batchIndicesValid)
        IndexBatch(m_batches.size() - 1);
}

void GeometryCollector::IndexBatch(size_t index) {
    const auto& batch = m_batches[index];
    if (batch.isSkinned)
        m_skinnedBatchIndices.push_back(index);
    else if (!batch.isStatic || batch.isTerrain || batch.IsStrictB2F())
        m_rigidUpdateBatchIndices.push_back(index);
}

void GeometryCollector::Sort() {
    m_batchIndicesValid = false;
    // ═══════════════════════════════════════════════════════
    //  RENDER ORDER SORTING (using SSA + shader flags)
    // ═══════════════════════════════════════════════════════
    // Proper render order for forward rendering:
    //   1. Opaque (iPriority == 0) - SSA descending (front-to-back for early-Z)
    //   2. Alpha-tested (iPriority == 1) - SSA descending (after opaque fills depth)
    //   3. Transparent (bStrictB2F) - SSA ascending (back-to-front for blending)
    //
    // SSA = R / distSQ (larger = closer/bigger = more visually important)
    // SSA descending = front-to-back, SSA ascending = back-to-front

    std::sort(m_batches.begin(), m_batches.end(),
        [](const GeometryBatch& a, const GeometryBatch& b) {
            // Get sorting properties using member functions
            bool aB2F = a.IsStrictB2F();
            bool bB2F = b.IsStrictB2F();
            bool aAref = a.IsAlphaTested();
            bool bAref = b.IsAlphaTested();

            // 1. Transparent (bStrictB2F) batches render LAST
            if (aB2F != bB2F) {
                return !aB2F;  // Non-B2F comes before B2F
            }

            // 2. For transparent batches: sort by SSA ascending (back-to-front)
            if (aB2F && bB2F) {
                return a.ssa < b.ssa;  // Ascending SSA = back-to-front
            }

            // 3. Alpha-tested renders AFTER opaque
            //    This ensures opaque geometry fills depth buffer first,
            //    so clip() in alpha-tested shaders shows correct background.
            if (aAref != bAref) {
                return !aAref;  // Non-aref (opaque) comes before aref
            }

            // 4. Within same category: sort by SSA descending (front-to-back)
            //    This matches vanilla's cmp_ssa: return lhs.ssa > rhs.ssa
            return a.ssa > b.ssa;  // Descending SSA = front-to-back
        });
}

u64 GeometryCollector::ComputeSortKey(const GeometryBatch& batch) {
    // Compute sort key (higher bits = more important)
    u64 key = 0;

    // Bits 48-63: Pipeline (most important - avoid PSO changes)
    if (batch.pipeline) {
        u64 pipelineHash = reinterpret_cast<u64>(batch.pipeline) >> 4;
        key |= (pipelineHash & 0xFFFF) << 48;
    }

    // Bits 32-47: Material ID
    key |= (static_cast<u64>(batch.materialID) & 0xFFFF) << 32;

    // Bits 16-31: Albedo texture
    key |= (batch.albedoTexture.index & 0xFFFF) << 16;

    // Bits 0-15: Normal texture
    key |= (batch.normalTexture.index & 0xFFFF);

    return key;
}

} // namespace xray::render
