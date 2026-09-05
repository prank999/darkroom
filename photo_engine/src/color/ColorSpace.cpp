#include "color/ColorSpace.h"
#include <cmath>
#include <algorithm>

namespace photo {

// sRGB to linear conversion using the official formula
float ColorSpace::srgbToLinear(float srgb) {
    if (srgb <= 0.04045f) {
        return srgb / 12.92f;
    } else {
        return std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }
}

// Linear to sRGB conversion using the official formula
float ColorSpace::linearToSrgb(float linear) {
    if (linear <= 0.0031308f) {
        return linear * 12.92f;
    } else {
        return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }
}

void ColorSpace::srgbToLinear(float& r, float& g, float& b) {
    r = srgbToLinear(r);
    g = srgbToLinear(g);
    b = srgbToLinear(b);
}

void ColorSpace::linearToSrgb(float& r, float& g, float& b) {
    r = linearToSrgb(r);
    g = linearToSrgb(g);
    b = linearToSrgb(b);
}

void ColorSpace::applyWhiteBalance(float& r, float& g, float& b,
                                    float temperature, float tint) {
    // Temperature: negative = cool (blue), positive = warm (yellow/orange)
    // Tint: negative = green, positive = magenta
    
    // Normalize to [-1, 1] range
    float tempNorm = temperature / 100.0f;
    float tintNorm = tint / 100.0f;
    
    // Calculate RGB multipliers based on temperature
    // This approximates blackbody radiation color shift
    float rMult, gMult, bMult;
    
    // Simplified white balance model
    // Warm (positive temp): increase R, decrease B
    // Cool (negative temp): decrease R, increase B
    if (tempNorm >= 0) {
        // Warm: add yellow/orange
        rMult = 1.0f;
        gMult = 1.0f - tempNorm * 0.3f;
        bMult = 1.0f - tempNorm * 0.8f;
    } else {
        // Cool: add blue
        rMult = 1.0f + tempNorm * 0.6f;
        gMult = 1.0f + tempNorm * 0.2f;
        bMult = 1.0f;
    }
    
    // Apply tint
    // Green (negative): reduce magenta channel (R+B relative to G)
    // Magenta (positive): increase magenta
    if (tintNorm >= 0) {
        // Magenta: increase R and B slightly
        rMult *= (1.0f + tintNorm * 0.3f);
        bMult *= (1.0f + tintNorm * 0.3f);
    } else {
        // Green: increase G
        gMult *= (1.0f - tintNorm * 0.3f);
    }
    
    // Apply multipliers
    r *= rMult;
    g *= gMult;
    b *= bMult;
}

void ColorSpace::rgbToHsl(float r, float g, float b,
                           float& h, float& s, float& l) {
    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float delta = maxVal - minVal;
    
    // Lightness
    l = (maxVal + minVal) / 2.0f;
    
    if (delta == 0) {
        h = 0;
        s = 0;
    } else {
        // Saturation
        if (l > 0.5f) {
            s = delta / (2.0f - maxVal - minVal);
        } else {
            s = delta / (maxVal + minVal);
        }
        
        // Hue
        if (maxVal == r) {
            h = ((g - b) / delta) + (g < b ? 6.0f : 0.0f);
        } else if (maxVal == g) {
            h = ((b - r) / delta) + 2.0f;
        } else {
            h = ((r - g) / delta) + 4.0f;
        }
        h /= 6.0f;
    }
}

void ColorSpace::hslToRgb(float h, float s, float l,
                           float& r, float& g, float& b) {
    if (s == 0) {
        r = g = b = l;
        return;
    }
    
    auto hueToRgb = [](float p, float q, float t) -> float {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f/6.0f) return p + (q - p) * 6 * t;
        if (t < 1.0f/2.0f) return q;
        if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6;
        return p;
    };
    
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    float p = 2 * l - q;
    
    r = hueToRgb(p, q, h + 1.0f/3.0f);
    g = hueToRgb(p, q, h);
    b = hueToRgb(p, q, h - 1.0f/3.0f);
}

void ColorSpace::applySaturation(float& r, float& g, float& b, float saturation) {
    // saturation: -100 to +100, 0 = no change
    // -100 = grayscale, +100 = double saturation
    float factor = 1.0f + saturation / 100.0f;
    
    float h, s, l;
    rgbToHsl(r, g, b, h, s, l);
    
    // Scale saturation
    s = clamp(s * factor);
    
    hslToRgb(h, s, l, r, g, b);
}

void ColorSpace::applyVibrance(float& r, float& g, float& b, float vibrance) {
    // Vibrance is a smart saturation that:
    // - Applies less saturation to already-saturated colors
    // - Protects skin tones (orange/yellow hues)
    // - Prevents clipping
    
    float vibranceFactor = vibrance / 100.0f;
    
    float h, s, l;
    rgbToHsl(r, g, b, h, s, l);
    
    // Reduce effect on already saturated colors
    float saturationWeight = 1.0f - s * s;
    
    // Detect skin tones (hue roughly in orange/red range: 0-0.1 or 0.9-1.0)
    float skinToneWeight = 0.0f;
    if (h < 0.1f || h > 0.9f) {
        skinToneWeight = 1.0f - (h < 0.1f ? h * 10.0f : (1.0f - h) * 10.0f);
    }
    
    // Combine weights: protect saturated colors and skin tones
    float protection = 0.5f * saturationWeight + 0.5f * (1.0f - skinToneWeight);
    
    // Apply reduced saturation boost
    float effectiveVibrance = vibranceFactor * (1.0f - protection * 0.7f);
    float newSaturation = s + effectiveVibrance * (1.0f - s);
    newSaturation = clamp(newSaturation);
    
    hslToRgb(h, newSaturation, l, r, g, b);
}

void ColorSpace::applyColorAdjustments(float& r, float& g, float& b,
                                        float saturation, float vibrance) {
    // Apply vibrance first (smarter), then saturation for remaining effect
    if (vibrance != 0) {
        applyVibrance(r, g, b, vibrance);
    }
    if (saturation != 0) {
        applySaturation(r, g, b, saturation);
    }
}

float ColorSpace::toLuminance(float r, float g, float b) {
    // Rec. 709 luminance coefficients for linear RGB
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

} // namespace photo
