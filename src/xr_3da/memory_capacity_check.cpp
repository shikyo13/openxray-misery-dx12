#include "stdafx.h"

// Explicit, isolated capacity check; never runs during normal gameplay.
// Uses the same allocator and free path as engine objects and buffers.
int RunMemoryCapacityCheck()
{
#if defined(_WIN64)
    constexpr size_t gib = size_t(1024) * 1024 * 1024;
    constexpr size_t bytes = 4 * gib + 64 * 1024 * 1024;
    constexpr size_t words = bytes / sizeof(u64);
    constexpr u64 salt = 0x9E3779B97F4A7C15ull;
    constexpr u64 multiplier = 0xD6E8FEB86659FD93ull;

    MEMORYSTATUSEX available{};
    available.dwLength = sizeof(available);
    if (!GlobalMemoryStatusEx(&available) || available.ullAvailPhys < bytes + 2 * gib ||
        available.ullAvailPageFile < bytes + 2 * gib)
    {
        Msg("! [MemoryCapacity] SKIPPED: need allocation plus 2 GiB free RAM and commit headroom");
        FlushLog();
        return 2;
    }

    Msg("* [MemoryCapacity] System RAM check, not VRAM or gameplay usage: bytes=%llu, free_physical=%llu, free_commit=%llu",
        static_cast<unsigned long long>(bytes), available.ullAvailPhys, available.ullAvailPageFile);
    FlushLog();
    const auto started = GetTickCount64();
    auto* allocation = static_cast<u64*>(Memory.mem_alloc(bytes, std::nothrow));
    if (!allocation)
    {
        Msg("! [MemoryCapacity] FAILED: engine allocation returned null");
        FlushLog();
        return 1;
    }

    // Volatile accesses ensure the compiler cannot elide any write or read.
    // Every word, including words on both sides of 4 GiB, gets a distinct value.
    volatile u64* data = allocation;
    for (size_t i = 0; i < words; ++i)
        data[i] = (static_cast<u64>(i) * multiplier) ^ salt;
    const auto written = GetTickCount64();
    Msg("* [MemoryCapacity] Wrote %llu bytes through engine allocation at %p in %llu ms",
        static_cast<unsigned long long>(bytes), allocation, written - started);
    FlushLog();

    bool matches = true;
    for (size_t i = 0; i < words; ++i)
    {
        const u64 actual = data[i];
        const u64 expected = (static_cast<u64>(i) * multiplier) ^ salt;
        if (actual != expected)
        {
            Msg("! [MemoryCapacity] MISMATCH offset=%llu actual=%llu expected=%llu",
                static_cast<unsigned long long>(i * sizeof(u64)), actual, expected);
            matches = false;
            break;
        }
    }
    const auto verified = GetTickCount64();
    Memory.mem_free(allocation);
    Msg("%s [MemoryCapacity] %s: verified_bytes=%llu read_ms=%llu allocation_freed=true",
        matches ? "*" : "!", matches ? "PASS" : "FAILED",
        matches ? static_cast<unsigned long long>(bytes) : 0ull, verified - written);
    FlushLog();
    return matches ? 0 : 1;
#else
    Msg("! [MemoryCapacity] SKIPPED: this check requires native Windows x64");
    FlushLog();
    return 2;
#endif
}
