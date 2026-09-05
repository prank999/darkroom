#pragma once

#include <array>
#include <cmath>

namespace photo {

/**
 * Color space conversion utilities.
 * All internal processing uses linear RGB.
 */
class ColorSpace {
public:
    // Convert sRGB encoded value to linear light
    static float srgbToLinear(float srgb);
    
    // Convert linear light to sRGB encoded
    static float linearToSrgb(float linear);
    
    // Convert sRGB pixel (r,g,b in [0,1]) to linear
    static void srgbToLinear(float& r, float& g, float& b);
    
    // Convert linear pixel to sRGB
    static void linearToSrgb(float& r, float& g, float& b);
    
    // Apply white balance temperature/tint adjustment in linear space
    // temperature: -100 (cool/blue) to +100 (warm/yellow)
    // tint: -100 (green) to +100 (magenta)
    static void applyWhiteBalance(float& r, float& g, float& b, 
                                   float temperature, float tint);
    
    // Convert RGB to HSL for vibrance/saturation calculations
    static void rgbToHsl(float r, float g, float b, 
                         float& h, float& s, float& l);
    
    // Convert HSL back to RGB
    static void hslToRgb(float h, float s, float l,
                         float& r, float& g, float& b);
    
    // Apply saturation adjustment
    // saturation: -100 (desaturate) to +100 (oversaturate), 0 = no change
    static void applySaturation(float& r, float& g, float& b, float saturation);
    
    // Apply vibrance adjustment (smart saturation that protects skin tones)
    // vibrance: -100 to +100, 0 = no change
    static void applyVibrance(float& r, float& g, float& b, float vibrance);
    
    // Combined saturation and vibrance
    static void applyColorAdjustments(float& r, float& g, float& b,
                                       float saturation, float vibrance);
    
    // Convert to luminance (perceptual)
    static float toLuminance(float r, float g, float b);
    
    // Clamp value to [0, 1]
    static float clamp(float v) {
        return std::max(0.0f, std::min(1.0f, v));
    }
    
    // Check for NaN or Inf
    static bool isValid(float v) {
        return std::isfinite(v);
    }
    
    static bool isValid(float r, float g, float b) {
        return isValid(r) && isValid(g) && isValid(b);
    }
};

} // namespace photo
