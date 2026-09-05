#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <optional>
#include <string>

namespace pe {

/**
 * Tone Curve System
 * 
 * MATHEMATICAL FOUNDATION:
 * Tone curves are piecewise linear or cubic interpolations through control points.
 * They map input tonal values [0, 1] to output tonal values [0, 1].
 * 
 * We use Catmull-Rom spline interpolation for smooth curves, with fallback to
 * linear interpolation for performance-critical paths.
 * 
 * The curve is stored as a lookup table (LUT) with 4096 entries for high precision,
 * allowing O(1) evaluation during rendering.
 */

struct CurvePoint {
    float x;  // Input value [0, 1]
    float y;  // Output value [0, 1]
    
    CurvePoint(float xx = 0, float yy = 0) : x(xx), y(yy) {}
    
    bool operator<(const CurvePoint& other) const {
        return x < other.x;
    }
};

class ToneCurve {
public:
    static constexpr int LUT_SIZE = 4096;  // High precision for professional use
    
private:
    std::vector<CurvePoint> control_points_;
    std::array<float, LUT_SIZE> lut_;
    bool lut_valid_;
    std::string name_;
    
    // Interpolation method
    enum class Interpolation { Linear, CatmullRom };
    Interpolation interpolation_;
    
    void rebuildLUT();
    float evaluateLinear(float x) const;
    float evaluateCatmullRom(float x) const;
    
public:
    ToneCurve(const std::string& name = "Unnamed");
    
    // Create identity curve (straight line y = x)
    static ToneCurve identity(const std::string& name = "Identity");
    
    // Set control points and rebuild LUT
    void setControlPoints(const std::vector<CurvePoint>& points);
    void addControlPoint(float x, float y);
    void clearControlPoints();
    
    // Reset to identity
    void reset();
    
    // Evaluate curve at position x in [0, 1]
    float evaluate(float x) const;
    
    // Check if curve is identity (within tolerance)
    bool isIdentity(float tolerance = 0.001f) const;
    
    // Get number of control points
    size_t pointCount() const { return control_points_.size(); }
    
    // Get control points
    const std::vector<CurvePoint>& getControlPoints() const { return control_points_; }
    
    // Name for debugging/UI
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
};

/**
 * Multi-channel tone curve system supporting:
 * - RGB composite curve
 * - Individual R, G, B curves
 * 
 * The curves are applied in sequence: first individual channel curves,
 * then the composite RGB curve.
 */
class ToneCurveSet {
private:
    ToneCurve red_curve_;
    ToneCurve green_curve_;
    ToneCurve blue_curve_;
    ToneCurve rgb_curve_;
    
public:
    ToneCurveSet();
    
    // Access individual curves
    ToneCurve& red() { return red_curve_; }
    ToneCurve& green() { return green_curve_; }
    ToneCurve& blue() { return blue_curve_; }
    ToneCurve& rgb() { return rgb_curve_; }
    
    const ToneCurve& red() const { return red_curve_; }
    const ToneCurve& green() const { return green_curve_; }
    const ToneCurve& blue() const { return blue_curve_; }
    const ToneCurve& rgb() const { return rgb_curve_; }
    
    // Reset all curves to identity
    void reset();
    
    // Check if all curves are identity
    bool isIdentity(float tolerance = 0.001f) const;
    
    // Apply curves to RGB values (in linear space)
    void apply(float& r, float& g, float& b) const;
};

} // namespace pe
