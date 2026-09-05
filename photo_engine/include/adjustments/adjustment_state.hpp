#pragma once
#include "adjustments/tone_curve.hpp"
#include <cmath>

namespace pe {

/**
 * @brief Complete set of non-destructive editing parameters
 * 
 * All values are normalized to sensible ranges for UI and storage.
 */
struct AdjustmentState {
    float exposure = 0.0f;       // EV units (-5 to +5)
    float contrast = 0.0f;       // (-100 to +100)
    float highlights = 0.0f;     // (-100 to +100)
    float shadows = 0.0f;        // (-100 to +100)
    float whites = 0.0f;         // (-100 to +100)
    float blacks = 0.0f;         // (-100 to +100)
    float temperature = 6500.0f; // Kelvin (2000-50000)
    float tint = 0.0f;           // (-100 green to +100 magenta)
    float vibrance = 0.0f;       // (-100 to +100)
    float saturation = 0.0f;     // (-100 to +100)
    ToneCurveSet tone_curves;
    
    bool isDefault() const {
        return (exposure == 0.0f && contrast == 0.0f && highlights == 0.0f &&
                shadows == 0.0f && whites == 0.0f && blacks == 0.0f &&
                temperature == 6500.0f && tint == 0.0f &&
                vibrance == 0.0f && saturation == 0.0f &&
                tone_curves.isIdentity());
    }
    
    size_t hash() const {
        size_t h = 0;
        auto combine = [&](float v) {
            h ^= std::hash<float>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
        };
        combine(exposure); combine(contrast); combine(highlights);
        combine(shadows); combine(whites); combine(blacks);
        combine(temperature); combine(tint); combine(vibrance); combine(saturation);
        return h;
    }
};

} // namespace pe
