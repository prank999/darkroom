#pragma once

#include "document/ImageSource.h"
#include "adjustments/AdjustmentState.h"
#include <memory>
#include <future>
#include <functional>

namespace photo {

/**
 * Rendered image output.
 * Can be at any resolution (preview or full).
 */
class RenderedImage {
public:
    RenderedImage() = default;
    
    RenderedImage(int width, int height);
    
    // Accessors
    int width() const { return width_; }
    int height() const { return height_; }
    int channels() const { return 4; }  // Always RGBA output
    
    // Get mutable pixel pointer for writing
    float* getPixelPtr(int x, int y);
    
    // Get const pixel pointer for reading
    const float* getPixelPtr(int x, int y) const;
    
    // Full data access
    std::vector<float>& data() { return pixels_; }
    const std::vector<float>& data() const { return pixels_; }
    
    // Check validity
    bool isValid() const { return width_ > 0 && height_ > 0; }
    
    // Clear to transparent black
    void clear();

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<float> pixels_;  // RGBA float, row-major
};

} // namespace photo
