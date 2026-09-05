#pragma once
/**
 * @file tone_curve.hpp
 * @brief High-precision tone curve system with monotonic interpolation
 * 
 * MATHEMATICAL FOUNDATION:
 * 
 * 1. Curve Representation:
 *    - Control points define the curve shape
 *    - Monotonic cubic Hermite interpolation (PCHIP) prevents overshoot
 *    - 4096-entry LUT for O(1) evaluation with high precision
 * 
 * 2. Why PCHIP over Catmull-Rom:
 *    - Catmull-Rom can produce overshoot/ringing near sharp transitions
 *    - PCHIP preserves monotonicity between control points
 *    - Essential for predictable tonal adjustments
 * 
 * 3. Channel Curves:
 *    - RGB composite curve applied after individual channels
 *    - Allows both global and channel-specific tone shaping
 */

#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include <cmath>

namespace pe {

struct CurvePoint {
    float x, y;  // Both in [0, 1]
    
    CurvePoint(float xx = 0, float yy = 0) : x(xx), y(yy) {}
    bool operator<(const CurvePoint& o) const { return x < o.x; }
};

class ToneCurve {
public:
    static constexpr int LUT_SIZE = 4096;
    
private:
    std::vector<CurvePoint> control_points_;
    std::array<float, LUT_SIZE> lut_;
    bool lut_valid_ = false;
    std::string name_;
    
    void rebuildLUT();
    float evalPCHIP(float x) const;
    
public:
    explicit ToneCurve(const std::string& name = "Unnamed");
    static ToneCurve identity(const std::string& name = "Identity");
    
    void setControlPoints(const std::vector<CurvePoint>& points);
    void addControlPoint(float x, float y);
    void clearControlPoints();
    void reset();
    
    float evaluate(float x) const;
    bool isIdentity(float tolerance = 0.001f) const;
    
    size_t pointCount() const { return control_points_.size(); }
    const std::vector<CurvePoint>& getControlPoints() const { return control_points_; }
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
};

class ToneCurveSet {
private:
    ToneCurve red_, green_, blue_, rgb_;
    
public:
    ToneCurveSet();
    
    ToneCurve& red() { return red_; }
    ToneCurve& green() { return green_; }
    ToneCurve& blue() { return blue_; }
    ToneCurve& rgb() { return rgb_; }
    
    const ToneCurve& red() const { return red_; }
    const ToneCurve& green() const { return green_; }
    const ToneCurve& blue() const { return blue_; }
    const ToneCurve& rgb() const { return rgb_; }
    
    void reset();
    bool isIdentity(float tolerance = 0.001f) const;
    void apply(float& r, float& g, float& b) const;
};

} // namespace pe
