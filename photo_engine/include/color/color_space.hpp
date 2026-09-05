#pragma once
/**
 * @file color_space.hpp
 * @brief Color space conversions and chromatic adaptation
 * 
 * MATHEMATICAL FOUNDATION:
 * 
 * 1. sRGB Transfer Functions (IEC 61966-2-1):
 *    The sRGB OETF/EOTF is piecewise to avoid infinite slope at origin.
 *    
 *    EOTF (sRGB → Linear):
 *      V_lin = V_srgb / 12.92                    for V_srgb ≤ 0.04045
 *      V_lin = ((V_srgb + 0.055) / 1.055)^2.4    for V_srgb > 0.04045
 *    
 *    OETF (Linear → sRGB):
 *      V_srgb = V_lin * 12.92                    for V_lin ≤ 0.0031308
 *      V_srgb = 1.055 * V_lin^(1/2.4) - 0.055    for V_lin > 0.0031308
 * 
 * 2. XYZ Color Space:
 *    Uses CIE 1931 2° standard observer with D65 white point.
 *    Matrix values from IEC 61966-2-1 for sRGB primaries.
 * 
 * 3. Chromatic Adaptation (Bradford):
 *    Most accurate method for common illuminant changes.
 *    Operates in cone response domain (LMS).
 */

#include <cmath>
#include <algorithm>
#include <array>

namespace pe {

class SRGBColorSpace {
public:
    static inline float sRGBtoLinear(float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        if (v <= 0.04045f) {
            return v / 12.92f;
        } else {
            return std::pow((v + 0.055f) / 1.055f, 2.4f);
        }
    }
    
    static inline float lineartosRGB(float v) {
        if (v <= 0.0031308f) {
            return v * 12.92f;
        } else {
            return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        }
    }
    
    static void sRGBtoLinear(float& r, float& g, float& b) {
        r = sRGBtoLinear(r);
        g = sRGBtoLinear(g);
        b = sRGBtoLinear(b);
    }
    
    static void lineartosRGB(float& r, float& g, float& b) {
        r = lineartosRGB(r);
        g = lineartosRGB(g);
        b = lineartosRGB(b);
    }
};

class XYZColorSpace {
public:
    // sRGB (D65) to XYZ matrix (row-major)
    static constexpr std::array<std::array<float, 3>, 3> M_RGB_TO_XYZ = {{
        {0.4124564f, 0.3575761f, 0.1804375f},
        {0.2126729f, 0.7151522f, 0.0721750f},
        {0.0193339f, 0.1191920f, 0.9503041f}
    }};
    
    // XYZ to sRGB (D65) matrix
    static constexpr std::array<std::array<float, 3>, 3> M_XYZ_TO_RGB = {{
        { 3.2404542f, -1.5371385f, -0.4985314f},
        {-0.9692660f,  1.8760108f,  0.0415560f},
        { 0.0556434f, -0.2040259f,  1.0572252f}
    }};
    
    static void RGBtoXYZ(float r, float g, float b, float& X, float& Y, float& Z) {
        X = M_RGB_TO_XYZ[0][0] * r + M_RGB_TO_XYZ[0][1] * g + M_RGB_TO_XYZ[0][2] * b;
        Y = M_RGB_TO_XYZ[1][0] * r + M_RGB_TO_XYZ[1][1] * g + M_RGB_TO_XYZ[1][2] * b;
        Z = M_RGB_TO_XYZ[2][0] * r + M_RGB_TO_XYZ[2][1] * g + M_RGB_TO_XYZ[2][2] * b;
    }
    
    static void XYZtoRGB(float X, float Y, float Z, float& r, float& g, float& b) {
        r = M_XYZ_TO_RGB[0][0] * X + M_XYZ_TO_RGB[0][1] * Y + M_XYZ_TO_RGB[0][2] * Z;
        g = M_XYZ_TO_RGB[1][0] * X + M_XYZ_TO_RGB[1][1] * Y + M_XYZ_TO_RGB[1][2] * Z;
        b = M_XYZ_TO_RGB[2][0] * X + M_XYZ_TO_RGB[2][1] * Y + M_XYZ_TO_RGB[2][2] * Z;
    }
};

class ChromaticAdaptation {
public:
    struct WhitePoint {
        float X, Y, Z;
    };
    
    static WhitePoint getD65() {
        return {0.95047f, 1.0f, 1.08883f};
    }
    
    static WhitePoint getWhitePointForTemperature(float kelvin);
    static void buildBradfordMatrix(const WhitePoint& srcWP, const WhitePoint& dstWP, float matrix[3][3]);
    static void adaptRGB(float& r, float& g, float& b, float srcKelvin, float dstKelvin);
};

} // namespace pe
