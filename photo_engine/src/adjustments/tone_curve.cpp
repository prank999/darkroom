#include "adjustments/tone_curve.hpp"

namespace pe {

ToneCurve::ToneCurve(const std::string& name) : name_(name) {
    reset();
}

ToneCurve ToneCurve::identity(const std::string& name) {
    ToneCurve curve(name);
    curve.reset();
    return curve;
}

void ToneCurve::reset() {
    control_points_.clear();
    control_points_.emplace_back(0.0f, 0.0f);
    control_points_.emplace_back(1.0f, 1.0f);
    rebuildLUT();
}

void ToneCurve::setControlPoints(const std::vector<CurvePoint>& points) {
    control_points_ = points;
    
    if (control_points_.empty()) {
        control_points_.emplace_back(0.0f, 0.0f);
        control_points_.emplace_back(1.0f, 1.0f);
    } else {
        std::sort(control_points_.begin(), control_points_.end());
        
        // Ensure endpoints exist
        if (control_points_.front().x > 0.0001f) {
            control_points_.insert(control_points_.begin(), 
                                   CurvePoint(0.0f, control_points_.front().y));
        }
        if (control_points_.back().x < 0.9999f) {
            control_points_.emplace_back(1.0f, control_points_.back().y);
        }
    }
    
    rebuildLUT();
}

void ToneCurve::addControlPoint(float x, float y) {
    x = std::clamp(x, 0.0f, 1.0f);
    y = std::clamp(y, 0.0f, 1.0f);
    
    auto it = std::lower_bound(control_points_.begin(), control_points_.end(), 
                               CurvePoint(x, y));
    
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

// PCHIP (Piecewise Cubic Hermite Interpolating Polynomial)
// Preserves monotonicity - no overshoot!
float ToneCurve::evalPCHIP(float x) const {
    if (control_points_.size() < 2) return x;
    
    // Clamp to domain
    if (x <= control_points_.front().x) return control_points_.front().y;
    if (x >= control_points_.back().x) return control_points_.back().y;
    
    // Find segment
    size_t idx = 0;
    for (size_t i = 0; i < control_points_.size() - 1; ++i) {
        if (x >= control_points_[i].x && x <= control_points_[i + 1].x) {
            idx = i;
            break;
        }
    }
    
    const auto& p1 = control_points_[idx];
    const auto& p2 = control_points_[idx + 1];
    
    float h = p2.x - p1.x;
    if (h < 1e-6f) return p1.y;
    
    // Calculate slopes
    float d_prev, d_next;
    
    if (idx == 0) {
        d_prev = (p2.y - p1.y) / h;
    } else {
        const auto& p0 = control_points_[idx - 1];
        d_prev = (p1.y - p0.y) / (p1.x - p0.x);
    }
    
    if (idx == control_points_.size() - 2) {
        d_next = (p2.y - p1.y) / h;
    } else {
        const auto& p3 = control_points_[idx + 2];
        d_next = (p3.y - p2.y) / (p3.x - p2.x);
    }
    
    // PCHIP slope calculation (Fritsch-Carlson method)
    float m;
    if (d_prev * d_next <= 0) {
        m = 0;  // Local extremum
    } else {
        float w1 = 2.0f * h + h;
        float w2 = h + 2.0f * h;
        m = (w1 + w2) / ((w1 / d_prev) + (w2 / d_next));
        if (std::isnan(m) || std::isinf(m)) m = 0;
    }
    
    // Hermite interpolation
    float t = (x - p1.x) / h;
    float t2 = t * t;
    float t3 = t2 * t;
    
    float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 = t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 = t3 - t2;
    
    return h00 * p1.y + h10 * h * m + h01 * p2.y + h11 * h * m;
}

void ToneCurve::rebuildLUT() {
    if (control_points_.empty()) {
        for (int i = 0; i < LUT_SIZE; ++i) {
            lut_[i] = static_cast<float>(i) / (LUT_SIZE - 1);
        }
        lut_valid_ = true;
        return;
    }
    
    // Check for identity curve
    if (control_points_.size() == 2 &&
        control_points_[0].x == 0.0f && control_points_[0].y == 0.0f &&
        control_points_[1].x == 1.0f && control_points_[1].y == 1.0f) {
        for (int i = 0; i < LUT_SIZE; ++i) {
            lut_[i] = static_cast<float>(i) / (LUT_SIZE - 1);
        }
        lut_valid_ = true;
        return;
    }
    
    for (int i = 0; i < LUT_SIZE; ++i) {
        float x = static_cast<float>(i) / (LUT_SIZE - 1);
        lut_[i] = evalPCHIP(x);
        lut_[i] = std::clamp(lut_[i], 0.0f, 1.0f);
    }
    
    lut_valid_ = true;
}

float ToneCurve::evaluate(float x) const {
    if (!lut_valid_) return x;
    
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
    : red_("Red"), green_("Green"), blue_("Blue"), rgb_("RGB") {
}

void ToneCurveSet::reset() {
    red_.reset();
    green_.reset();
    blue_.reset();
    rgb_.reset();
}

bool ToneCurveSet::isIdentity(float tolerance) const {
    return red_.isIdentity(tolerance) && 
           green_.isIdentity(tolerance) && 
           blue_.isIdentity(tolerance) && 
           rgb_.isIdentity(tolerance);
}

void ToneCurveSet::apply(float& r, float& g, float& b) const {
    // Apply individual channel curves first
    r = red_.evaluate(r);
    g = green_.evaluate(g);
    b = blue_.evaluate(b);
    
    // Then apply composite RGB curve
    r = rgb_.evaluate(r);
    g = rgb_.evaluate(g);
    b = rgb_.evaluate(b);
}

} // namespace pe
