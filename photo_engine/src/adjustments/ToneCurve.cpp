#include "adjustments/ToneCurve.h"
#include <algorithm>
#include <cmath>

namespace photo {

ToneCurve::ToneCurve(std::vector<Point> points) 
    : points_(std::move(points)) {
    // Sort points by x coordinate
    std::sort(points_.begin(), points_.end());
}

float ToneCurve::evaluate(float x) const {
    x = std::clamp(x, 0.0f, 1.0f);
    
    // Fast path: identity or empty curve
    if (points_.empty()) {
        return x;
    }
    
    // Use lookup table for speed
    if (!lookupValid_) {
        buildLookupTable();
    }
    
    // Interpolate from lookup table
    size_t idx = static_cast<size_t>(x * 255.999f);
    float t = (x * 256.0f) - idx;
    
    if (idx >= 255) {
        return lookupTable_[255];
    }
    
    return lookupTable_[idx] * (1 - t) + lookupTable_[idx + 1] * t;
}

void ToneCurve::reset() {
    points_.clear();
    lookupValid_ = false;
}

bool ToneCurve::isIdentity() const {
    return points_.empty();
}

void ToneCurve::addPoint(float x, float y) {
    points_.push_back({x, y});
    std::sort(points_.begin(), points_.end());
    lookupValid_ = false;
}

void ToneCurve::clear() {
    points_.clear();
    lookupValid_ = false;
}

void ToneCurve::buildLookupTable() const {
    lookupTable_.resize(256);
    
    if (points_.empty()) {
        // Identity curve
        for (int i = 0; i < 256; ++i) {
            lookupTable_[i] = i / 255.0f;
        }
    } else {
        // Ensure endpoints exist
        std::vector<Point> pts = points_;
        
        if (pts.front().x > 0.0f) {
            pts.insert(pts.begin(), {0.0f, pts.front().y});
        }
        if (pts.back().x < 1.0f) {
            pts.push_back({1.0f, pts.back().y});
        }
        
        // Build LUT using monotonic cubic interpolation
        for (int i = 0; i < 256; ++i) {
            float x = i / 255.0f;
            lookupTable_[i] = interpolate(x);
        }
    }
    
    lookupValid_ = true;
}

float ToneCurve::interpolate(float x) const {
    if (points_.empty()) {
        return x;
    }
    
    // Find segment containing x
    auto it = std::lower_bound(points_.begin(), points_.end(), Point{x, 0});
    
    if (it == points_.begin()) {
        return points_.front().y;
    }
    if (it == points_.end()) {
        return points_.back().y;
    }
    
    // Linear interpolation between control points
    // (Could use Catmull-Rom or Hermite for smoother curves)
    const Point& p1 = *(it - 1);
    const Point& p2 = *it;
    
    float t = (x - p1.x) / (p2.x - p1.x);
    t = std::clamp(t, 0.0f, 1.0f);
    
    return p1.y + t * (p2.y - p1.y);
}

// ToneCurveSet implementation

void ToneCurveSet::apply(float& r, float& g, float& b) const {
    // Apply RGB composite curve first
    if (!rgbCurve.isIdentity()) {
        float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        float mappedLuma = rgbCurve.evaluate(luma);
        float scale = (luma > 0.0001f) ? (mappedLuma / luma) : 1.0f;
        r *= scale;
        g *= scale;
        b *= scale;
    }
    
    // Apply individual channel curves
    if (!redCurve.isIdentity()) {
        r = redCurve.evaluate(r);
    }
    if (!greenCurve.isIdentity()) {
        g = greenCurve.evaluate(g);
    }
    if (!blueCurve.isIdentity()) {
        b = blueCurve.evaluate(b);
    }
}

void ToneCurveSet::reset() {
    rgbCurve.reset();
    redCurve.reset();
    greenCurve.reset();
    blueCurve.reset();
}

bool ToneCurveSet::isIdentity() const {
    return rgbCurve.isIdentity() &&
           redCurve.isIdentity() &&
           greenCurve.isIdentity() &&
           blueCurve.isIdentity();
}

} // namespace photo
