// exposure_adapt.cs - Compute adapted exposure from luminance histogram
// Implements histogram-based auto-exposure with temporal eye adaptation
//
// References:
// - Krzysztof Narkowicz: "Automatic Exposure" (2016)
// - Epic Games: "Auto Exposure in UE 4.25" (2020)
//
#define SM_5_0
#include "common.h"

// ═══════════════════════════════════════════════════════
//  CONSTANTS
// ═══════════════════════════════════════════════════════

cbuffer ExposureAdaptParams : register(b5)  // b5 to avoid conflicts with common.h (b0-b2)
{
    float g_min_log_luminance;     // Minimum log2 luminance
    float g_log_luminance_range;   // Range of log2 luminance
    float g_low_percentile;        // Skip this fraction of darkest pixels (0.5 = 50%)
    float g_high_percentile;       // Skip pixels above this percentile (0.98 = 98%)

    float g_adapt_speed_up;        // Speed when brightening (f-stops/sec)
    float g_adapt_speed_down;      // Speed when darkening (f-stops/sec)
    float g_delta_time;            // Frame delta time in seconds
    float g_exposure_compensation; // Manual EV adjustment

    float g_min_exposure;          // Minimum exposure clamp
    float g_max_exposure;          // Maximum exposure clamp
    float g_middle_gray;
    float g_amount;
    float g_low_luminance;
    float g_padding;
    float g_padding2;
    float g_padding3;
};

// ═══════════════════════════════════════════════════════
//  RESOURCES
// ═══════════════════════════════════════════════════════

StructuredBuffer<uint> g_histogram : register(t0);  // 64 bins from histogram pass
RWTexture2D<float> g_exposure : register(u0);       // 1x1 output (also read for adaptation)

// ═══════════════════════════════════════════════════════
//  EXPOSURE CALCULATION
// ═══════════════════════════════════════════════════════

float ComputeAverageLuminance()
{
    // Count total pixels in histogram
    uint totalPixels = 0;
    for (uint i = 0; i < 64; i++)
    {
        totalPixels += g_histogram[i];
    }

    if (totalPixels == 0)
        return 0.5;  // Default mid-gray

    // Find percentile boundaries
    uint lowThreshold = (uint)(totalPixels * g_low_percentile);
    uint highThreshold = (uint)(totalPixels * g_high_percentile);

    // Accumulate weighted luminance, skipping extremes
    float weightedSum = 0.0;
    uint validPixels = 0;
    uint runningCount = 0;

    for (uint bin = 0; bin < 64; bin++)
    {
        uint binCount = g_histogram[bin];
        uint prevCount = runningCount;
        runningCount += binCount;

        // Skip if entirely below low percentile
        if (runningCount <= lowThreshold)
            continue;

        // Stop if we've passed high percentile
        if (prevCount >= highThreshold)
            break;

        // Calculate how many pixels in this bin are within our range
        uint startInBin = max(prevCount, lowThreshold) - prevCount;
        uint endInBin = min(runningCount, highThreshold) - prevCount;
        uint countInRange = endInBin - startInBin;

        if (countInRange > 0)
        {
            // Convert bin index to log luminance (use bin center)
            float t = (float(bin) + 0.5) / 64.0;
            float logLum = g_min_log_luminance + t * g_log_luminance_range;
            float luminance = exp2(logLum);

            weightedSum += luminance * float(countInRange);
            validPixels += countInRange;
        }
    }

    if (validPixels == 0)
        return 0.5;

    return weightedSum / float(validPixels);
}

float ComputeTargetExposure(float avgLuminance)
{
    // Match r2_rendertarget_phase_luminance / bloom_luminance_3:
    // MiddleGray = lerp((1, 0, 1), (middlegray, 1, lowlum), amount).
    // The content's r2_tonemap settings are authored for these units.
    float numerator = lerp(1.0, g_middle_gray, g_amount);
    float denominator = avgLuminance * g_amount + lerp(1.0, g_low_luminance, g_amount);
    float exposure = numerator / max(denominator, 0.0001);

    // Apply exposure compensation (in EV stops)
    exposure *= exp2(g_exposure_compensation);

    // Clamp to valid range
    return clamp(exposure, g_min_exposure, g_max_exposure);
}

float AdaptExposure(float currentExposure, float targetExposure)
{
    // Asymmetric adaptation: faster when brightening, slower when darkening
    // This mimics human eye adaptation behavior
    float adaptSpeed = (targetExposure > currentExposure)
        ? g_adapt_speed_up
        : g_adapt_speed_down;

    // Exponential approach to target
    // adaptFactor = 1 - e^(-dt * speed)
    float adaptFactor = 1.0 - exp(-g_delta_time * adaptSpeed);

    // Lerp toward target
    return lerp(currentExposure, targetExposure, adaptFactor);
}

// ═══════════════════════════════════════════════════════
//  MAIN COMPUTE SHADER
// ═══════════════════════════════════════════════════════

[numthreads(1, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    // Read current exposure (for temporal adaptation)
    float currentExposure = g_exposure[uint2(0, 0)];

    // Compute average luminance from histogram
    float avgLuminance = ComputeAverageLuminance();

    // Compute target exposure
    float targetExposure = ComputeTargetExposure(avgLuminance);

    // Apply temporal adaptation
    float newExposure = AdaptExposure(currentExposure, targetExposure);

    // Write output
    g_exposure[uint2(0, 0)] = newExposure;
}
