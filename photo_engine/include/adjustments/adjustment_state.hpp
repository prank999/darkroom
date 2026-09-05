#pragma once
#include "adjustments/tone_curve.hpp"
#include <cmath>

namespace pe {

/**
 * AdjustmentState - Complete non-destructive editing parameters
 * 
 * All values are normalized to sensible ranges for UI and storage.
 * The renderer interprets these values according to photographic principles.
 */
struct AdjustmentState {
    // Basic Exposure (EV units, typically -5 to +5)
    float exposure = 0.0f;
    
    // Contrast (-100 to +100, 0 is neutral)
    float contrast = 0.0f;
    
    // Highlights recovery/compression (-100 to +100)
    float highlights = 0.0f;
    
    // Shadows lift (-100 to +100)
    float shadows = 0.0f;
    
    // Whites point adjustment (-100 to +100)
    float whites = 0.0f;
    
    // Blacks point adjustment (-100 to +100)
    float blacks = 0.0f;
    
    // Color temperature in Kelvin (typically 2000-50000, 6500 is neutral for sRGB)
    float temperature = 6500.0f;
    
    // Tint adjustment (-100 green to +100 magenta, 0 is neutral)
    float tint = 0.0f;
    
    // Vibrance (-100 to +100, 0 is neutral)
    // Vibrance selectively saturates less-saturated colors more
    float vibrance = 0.0f;
    
    // Saturation (-100 to +100, 0 is neutral, -100 is B&W)
    float saturation = 0.0f;
    
    // Tone curves
    ToneCurveSet tone_curves;
    
    // Check if any adjustment is non-default
    bool isDefault() const {
        return (exposure == 0.0f &&
                contrast == 0.0f &&
                highlights == 0.0f &&
                shadows == 0.0f &&
                whites == 0.0f &&
                blacks == 0.0f &&
                temperature == 6500.0f &&
                tint == 0.0f &&
                vibrance == 0.0f &&
                saturation == 0.0f &&
                tone_curves.isIdentity());
    }
    
    // Create a copy with only specific adjustments changed
    AdjustmentState withExposure(float ev) const {
        auto copy = *this;
        copy.exposure = ev;
        return copy;
    }
    
    AdjustmentState withContrast(float c) const {
        auto copy = *this;
        copy.contrast = c;
        return copy;
    }
    
    AdjustmentState withTemperature(float k) const {
        auto copy = *this;
        copy.temperature = k;
        return copy;
    }
    
    // etc. for other parameters
};

} // namespace pe
