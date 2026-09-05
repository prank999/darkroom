#pragma once

#include <cmath>
#include <algorithm>
#include <vector>

namespace photo {

/**
 * Adjustment parameters - all values are normalized and can be serialized.
 * This is the complete state needed to render an image non-destructively.
 */
struct AdjustmentState {
    // Basic exposure controls
    float exposure = 0.0f;        // EV stops, typically -10 to +10
    float contrast = 0.0f;        // -100 to +100, 0 is neutral
    float highlights = 0.0f;      // -100 to +100
    float shadows = 0.0f;         // -100 to +100
    float whites = 0.0f;          // -100 to +100
    float blacks = 0.0f;          // -100 to +100
    
    // White balance
    float temperature = 0.0f;     // -100 (cool) to +100 (warm)
    float tint = 0.0f;            // -100 (green) to +100 (magenta)
    
    // Color adjustments
    float vibrance = 0.0f;        // -100 to +100
    float saturation = 0.0f;      // -100 to +100
    
    // Tone curves (control points for each curve)
    struct CurvePoint {
        float x;  // Input value [0, 1]
        float y;  // Output value [0, 1]
        
        bool operator<(const CurvePoint& other) const {
            return x < other.x;
        }
        
        bool operator==(const CurvePoint& other) const {
            return x == other.x && y == other.y;
        }
    };
    
    // Each curve has control points; empty means identity
    std::vector<CurvePoint> rgbCurve;
    std::vector<CurvePoint> redCurve;
    std::vector<CurvePoint> greenCurve;
    std::vector<CurvePoint> blueCurve;
    
    // Check if state is identity (no adjustments)
    bool isIdentity() const {
        return exposure == 0.0f &&
               contrast == 0.0f &&
               highlights == 0.0f &&
               shadows == 0.0f &&
               whites == 0.0f &&
               blacks == 0.0f &&
               temperature == 0.0f &&
               tint == 0.0f &&
               vibrance == 0.0f &&
               saturation == 0.0f &&
               rgbCurve.empty() &&
               redCurve.empty() &&
               greenCurve.empty() &&
               blueCurve.empty();
    }
    
    // Reset to identity
    void reset() {
        *this = AdjustmentState();
    }
    
    // Comparison for undo/redo
    bool operator==(const AdjustmentState& other) const {
        return exposure == other.exposure &&
               contrast == other.contrast &&
               highlights == other.highlights &&
               shadows == other.shadows &&
               whites == other.whites &&
               blacks == other.blacks &&
               temperature == other.temperature &&
               tint == other.tint &&
               vibrance == other.vibrance &&
               saturation == other.saturation &&
               rgbCurve == other.rgbCurve &&
               redCurve == other.redCurve &&
               greenCurve == other.greenCurve &&
               blueCurve == other.blueCurve;
    }
};

} // namespace photo
