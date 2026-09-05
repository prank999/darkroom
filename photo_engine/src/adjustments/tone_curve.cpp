#include "adjustments/tone_curve.hpp"
#include <cstring>
#include <string>

namespace pe {

ToneCurve::ToneCurve(const std::string& name) 
    : lut_valid_(false), name_(name), interpolation_(Interpolation::CatmullRom) {
    reset();
}

ToneCurve ToneCurve::identity(const std::string& name) {
    ToneCurve curve(name);
    curve.reset();
    return curve;
}

void ToneCurve::reset() {
    control_points_.clear();
    // Add identity endpoints
    control_points_.emplace_back(0.0f, 0.0f);
    control_points_.emplace_back(1.0f, 1.0f);
    rebuildLUT();
}

void ToneCurve::setControlPoints(const std::vector<CurvePoint>& points) {
    control_points_ = points;
    
    // Ensure we have at least endpoints
    if (control_points_.empty()) {
        control_points_.emplace_back(0.0f, 0.0f);
        control_points_.emplace_back(1.0f, 1.0f);
    } else {
        // Sort by x value
        std::sort(control_points_.begin(), control_points_.end());
        
        // Ensure first point is at x=0
        if (control_points_.front().x > 0.0001f) {
            control_points_.insert(control_points_.begin(), CurvePoint(0.0f, control_points_.front().y));
        }
        
        // Ensure last point is at x=1
        if (control_points_.back().x < 0.9999f) {
            control_points_.emplace_back(1.0f, control_points_.back().y);
        }
    }
    
    rebuildLUT();
}

void ToneCurve::addControlPoint(float x, float y) {
    x = std::clamp(x, 0.0f, 1.0f);
    y = std::clamp(y, 0.0f, 1.0f);
    
    // Find insertion point
    auto it = std::lower_bound(control_points_.begin(), control_points_.end(), CurvePoint(x, y));
    
    // Check if we're replacing an existing point at same x
    if (it != control_points_.end() && std::abs(it->x - x) < 0.0001f) {
        it->y = y;
    } else {
        control_points_.insert(it, CurvePoint(x, y));
    }
    
    rebuildLUT();
}

void ToneCurve::clearControlPoints() {
    control_points_.clear();
    rebuildLUT();
}

void ToneCurve::rebuildLUT() {
    if (control_points_.empty()) {
        // Identity curve
        for (int i = 0; i < LUT_SIZE; ++i) {
            lut_[i] = static_cast<float>(i) / (LUT_SIZE - 1);
        }
        lut_valid_ = true;
        return;
    }
    
    // Check if we have a simple identity curve (just two points at (0,0) and (1,1))
    if (control_points_.size() == 2 && 
        control_points_[0].x == 0.0f && control_points_[0].y == 0.0f &&
        control_points_[1].x == 1.0f && control_points_[1].y == 1.0f) {
        // Direct identity - no interpolation needed
        for (int i = 0; i < LUT_SIZE; ++i) {
            lut_[i] = static_cast<float>(i) / (LUT_SIZE - 1);
        }
        lut_valid_ = true;
        return;
    }
    
    // Build LUT using specified interpolation
    for (int i = 0; i < LUT_SIZE; ++i) {
        float x = static_cast<float>(i) / (LUT_SIZE - 1);
        
        if (interpolation_ == Interpolation::Linear) {
            lut_[i] = evaluateLinear(x);
        } else {
            lut_[i] = evaluateCatmullRom(x);
        }
    }
    
    lut_valid_ = true;
}

float ToneCurve::evaluateLinear(float x) const {
    if (control_points_.empty()) return x;
    if (x <= control_points_.front().x) return control_points_.front().y;
    if (x >= control_points_.back().x) return control_points_.back().y;
    
    // Find segment
    for (size_t i = 0; i < control_points_.size() - 1; ++i) {
        const auto& p1 = control_points_[i];
        const auto& p2 = control_points_[i + 1];
        
        if (x >= p1.x && x <= p2.x) {
            float t = (p2.x - p1.x > 0.0001f) ? ((x - p1.x) / (p2.x - p1.x)) : 0.0f;
            return p1.y + t * (p2.y - p1.y);
        }
    }
    
    return x;
}

float ToneCurve::evaluateCatmullRom(float x) const {
    if (control_points_.empty()) return x;
    if (control_points_.size() < 2) return control_points_.front().y;
    
    if (x <= control_points_.front().x) return control_points_.front().y;
    if (x >= control_points_.back().x) return control_points_.back().y;
    
    // Find surrounding points for Catmull-Rom interpolation
    // We need 4 points: p0, p1, p2, p3 where we interpolate between p1 and p2
    
    int idx = 0;
    for (size_t i = 0; i < control_points_.size() - 1; ++i) {
        if (x >= control_points_[i].x && x <= control_points_[i + 1].x) {
            idx = static_cast<int>(i);
            break;
        }
    }
    
    // Get the four control points (clamping at boundaries)
    auto getP = [&](int i) -> CurvePoint {
        if (i < 0) {
            // Extrapolate from first two points
            float dx = control_points_[1].x - control_points_[0].x;
            float dy = control_points_[1].y - control_points_[0].y;
            float t = (i + 1);
            return CurvePoint(
                control_points_[0].x + t * dx,
                control_points_[0].y + t * dy
            );
        }
        if (i >= static_cast<int>(control_points_.size())) {
            // Extrapolate from last two points
            int n = control_points_.size() - 1;
            float dx = control_points_[n].x - control_points_[n-1].x;
            float dy = control_points_[n].y - control_points_[n-1].y;
            float t = (i - n + 1);
            return CurvePoint(
                control_points_[n].x + t * dx,
                control_points_[n].y + t * dy
            );
        }
        return control_points_[i];
    };
    
    CurvePoint p0 = getP(idx - 1);
    CurvePoint p1 = getP(idx);
    CurvePoint p2 = getP(idx + 1);
    CurvePoint p3 = getP(idx + 2);
    
    // Calculate t parameter within segment [p1, p2]
    float t = (p2.x - p1.x > 0.0001f) ? ((x - p1.x) / (p2.x - p1.x)) : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    
    // Catmull-Rom spline formula
    float t2 = t * t;
    float t3 = t2 * t;
    
    float y = 0.5f * (
        (2.0f * p1.y) +
        (-p0.y + p2.y) * t +
        (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
        (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3
    );
    
    return std::clamp(y, 0.0f, 1.0f);
}

float ToneCurve::evaluate(float x) const {
    if (!lut_valid_) {
        return x;  // Return identity if LUT not built
    }
    
    x = std::clamp(x, 0.0f, 1.0f);
    int idx = static_cast<int>(x * (LUT_SIZE - 1) + 0.5f);
    idx = std::clamp(idx, 0, LUT_SIZE - 1);
    
    return lut_[idx];
}

bool ToneCurve::isIdentity(float tolerance) const {
    if (!lut_valid_) return true;
    
    for (int i = 0; i < LUT_SIZE; ++i) {
        float expected = static_cast<float>(i) / (LUT_SIZE - 1);
        if (std::abs(lut_[i] - expected) > tolerance) {
            return false;
        }
    }
    return true;
}

// ToneCurveSet implementation

ToneCurveSet::ToneCurveSet() 
    : red_curve_("Red"), green_curve_("Green"), blue_curve_("Blue"), rgb_curve_("RGB") {
}

void ToneCurveSet::reset() {
    red_curve_.reset();
    green_curve_.reset();
    blue_curve_.reset();
    rgb_curve_.reset();
}

bool ToneCurveSet::isIdentity(float tolerance) const {
    return red_curve_.isIdentity(tolerance) &&
           green_curve_.isIdentity(tolerance) &&
           blue_curve_.isIdentity(tolerance) &&
           rgb_curve_.isIdentity(tolerance);
}

void ToneCurveSet::apply(float& r, float& g, float& b) const {
    // Apply individual channel curves first
    r = red_curve_.evaluate(r);
    g = green_curve_.evaluate(g);
    b = blue_curve_.evaluate(b);
    
    // Then apply composite RGB curve
    r = rgb_curve_.evaluate(r);
    g = rgb_curve_.evaluate(g);
    b = rgb_curve_.evaluate(b);
}

} // namespace pe
