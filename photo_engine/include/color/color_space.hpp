#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace pe {

/**
 * sRGB color space conversion utilities.
 * 
 * MATHEMATICAL FOUNDATION:
 * sRGB uses a piecewise transfer function (gamma curve) that approximates
 * a power function with gamma ≈ 2.2, but has a linear segment near zero
 * to avoid infinite slope at the origin.
 * 
 * The exact sRGB OETF (Opto-Electronic Transfer Function):
 *   V_srgb = 12.92 * V_linear           for V_linear <= 0.0031308
 *   V_srgb = 1.055 * V_linear^(1/2.4) - 0.055  for V_linear > 0.0031308
 * 
 * The inverse (EOTF) for converting sRGB to linear:
 *   V_linear = V_srgb / 12.92                    for V_srgb <= 0.04045
 *   V_linear = ((V_srgb + 0.055) / 1.055)^2.4    for V_srgb > 0.04045
 * 
 * Why linear light matters:
 * Photographic operations like exposure, blending, and tone mapping are
 * physically-based and must operate on linear light values. Operating
 * directly on gamma-encoded sRGB values produces incorrect results,
 * especially for exposure adjustments and blending operations.
 */
class SRGBColorSpace {
public:
    // Convert sRGB encoded value (0-1) to linear light
    static inline float sRGBtoLinear(float v) {
        if (v <= 0.04045f) {
            return v / 12.92f;
        } else {
            return std::pow((v + 0.055f) / 1.055f, 2.4f);
        }
    }
    
    // Convert linear light to sRGB encoded value (0-1)
    static inline float lineartosRGB(float v) {
        if (v <= 0.0031308f) {
            return v * 12.92f;
        } else {
            return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        }
    }
    
    // Fast approximation using simple gamma 2.2 (less accurate but faster)
    static inline float sRGBtoLinearFast(float v) {
        return std::pow(v, 2.2f);
    }
    
    static inline float lineartosRGBFast(float v) {
        return std::pow(v, 1.0f / 2.2f);
    }
    
    // Convert RGB triplet from sRGB to linear
    static void sRGBtoLinear(float& r, float& g, float& b) {
        r = sRGBtoLinear(r);
        g = sRGBtoLinear(g);
        b = sRGBtoLinear(b);
    }
    
    // Convert RGB triplet from linear to sRGB
    static void lineartosRGB(float& r, float& g, float& b) {
        r = lineartosRGB(r);
        g = lineartosRGB(g);
        b = lineartosRGB(b);
    }
};

/**
 * XYZ color space conversion.
 * 
 * Uses the standard sRGB primaries and D65 white point.
 * Matrix values from IEC 61966-2-1.
 */
class XYZColorSpace {
public:
    // sRGB (D65) to XYZ conversion matrix
    static constexpr float M_RGB_TO_XYZ[3][3] = {
        {0.4124564f, 0.3575761f, 0.1804375f},
        {0.2126729f, 0.7151522f, 0.0721750f},
        {0.0193339f, 0.1191920f, 0.9503041f}
    };
    
    // XYZ to sRGB (D65) conversion matrix
    static constexpr float M_XYZ_TO_RGB[3][3] = {
        { 3.2404542f, -1.5371385f, -0.4985314f},
        {-0.9692660f,  1.8760108f,  0.0415560f},
        { 0.0556434f, -0.2040259f,  1.0572252f}
    };
    
    // Convert linear sRGB to XYZ
    static void RGBtoXYZ(float r, float g, float b, float& X, float& Y, float& Z) {
        X = M_RGB_TO_XYZ[0][0] * r + M_RGB_TO_XYZ[0][1] * g + M_RGB_TO_XYZ[0][2] * b;
        Y = M_RGB_TO_XYZ[1][0] * r + M_RGB_TO_XYZ[1][1] * g + M_RGB_TO_XYZ[1][2] * b;
        Z = M_RGB_TO_XYZ[2][0] * r + M_RGB_TO_XYZ[2][1] * g + M_RGB_TO_XYZ[2][2] * b;
    }
    
    // Convert XYZ to linear sRGB
    static void XYZtoRGB(float X, float Y, float Z, float& r, float& g, float& b) {
        r = M_XYZ_TO_RGB[0][0] * X + M_XYZ_TO_RGB[0][1] * Y + M_XYZ_TO_RGB[0][2] * Z;
        g = M_XYZ_TO_RGB[1][0] * X + M_XYZ_TO_RGB[1][1] * Y + M_XYZ_TO_RGB[1][2] * Z;
        b = M_XYZ_TO_RGB[2][0] * X + M_XYZ_TO_RGB[2][1] * Y + M_XYZ_TO_RGB[2][2] * Z;
    }
};

/**
 * White point adaptation using Bradford chromatic adaptation.
 * Used for temperature/tint adjustments.
 */
class ChromaticAdaptation {
public:
    // Color temperatures in Kelvin and their corresponding xy chromaticities
    // Using Planckian radiator approximation
    
    struct WhitePoint {
        float X, Y, Z;
    };
    
    // Get D65 white point (standard sRGB)
    static WhitePoint getD65() {
        return {0.95047f, 1.0f, 1.08883f};
    }
    
    // Approximate white point for a given color temperature (Kelvin)
    // Uses McCamy's formula and related approximations
    static WhitePoint getWhitePointForTemperature(float kelvin);
    
    // Build Bradford adaptation matrix from source white to destination white
    static void buildBradfordMatrix(const WhitePoint& srcWP, const WhitePoint& dstWP, float matrix[3][3]);
    
    // Apply chromatic adaptation to linear RGB
    static void adaptRGB(float& r, float& g, float& b, float srcKelvin, float dstKelvin);
};

} // namespace pe
