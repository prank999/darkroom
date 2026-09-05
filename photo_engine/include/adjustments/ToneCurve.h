#pragma once

#include "AdjustmentState.h"
#include <vector>
#include <functional>

namespace photo {

/**
 * Tone curve evaluator with smooth interpolation.
 * Supports arbitrary control points with monotonic cubic interpolation.
 */
class ToneCurve {
public:
    using Point = AdjustmentState::CurvePoint;
    
    ToneCurve() = default;
    
    // Initialize with control points (will be sorted by x)
    explicit ToneCurve(std::vector<Point> points);
    
    // Evaluate curve at input value [0, 1]
    float evaluate(float x) const;
    
    // Reset to identity (linear)
    void reset();
    
    // Check if curve is identity
    bool isIdentity() const;
    
    // Add a control point
    void addPoint(float x, float y);
    
    // Clear all points
    void clear();
    
    // Get current points
    const std::vector<Point>& points() const { return points_; }

private:
    std::vector<Point> points_;
    mutable std::vector<float> lookupTable_;
    mutable bool lookupValid_ = false;
    
    void buildLookupTable() const;
    float interpolate(float x) const;
};

/**
 * Composite tone curve manager handling RGB + individual channel curves.
 */
class ToneCurveSet {
public:
    ToneCurve rgbCurve;
    ToneCurve redCurve;
    ToneCurve greenCurve;
    ToneCurve blueCurve;
    
    // Apply all curves to a pixel (in-place)
    void apply(float& r, float& g, float& b) const;
    
    // Reset all curves to identity
    void reset();
    
    // Check if all curves are identity
    bool isIdentity() const;
};

} // namespace photo
