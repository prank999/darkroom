#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <mutex>

namespace photo {

/**
 * Immutable source image representation.
 * Stores pixels in linear RGB float format for consistent processing.
 */
class ImageSource {
public:
    ImageSource() = default;
    
    // Load from file (PNG, JPEG)
    static std::shared_ptr<ImageSource> loadFromFile(const std::string& path);
    
    // Create from raw pixel data (for testing)
    static std::shared_ptr<ImageSource> createFromData(
        const uint8_t* rgba_data,
        int width,
        int height,
        int channels
    );
    
    // Accessors - source is immutable after construction
    int width() const { return width_; }
    int height() const { return height_; }
    int channels() const { return channels_; }
    
    // Get pixel in linear RGB space [0, 1]
    float getPixelLinear(int x, int y, int channel) const;
    
    // Get row pointer for efficient access
    const float* getRowLinear(int y) const;
    
    // Full data access
    const std::vector<float>& data() const { return pixels_; }
    
    // Check if valid
    bool isValid() const { return width_ > 0 && height_ > 0; }

private:
    int width_ = 0;
    int height_ = 0;
    int channels_ = 3;  // Always RGB internally
    std::vector<float> pixels_;  // Linear RGB, row-major
    mutable std::mutex mutex_;
};

} // namespace photo
