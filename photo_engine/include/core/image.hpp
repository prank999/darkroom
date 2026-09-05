#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <optional>

namespace pe {

/**
 * High-precision floating-point pixel format for internal processing.
 * Values are in linear light space, typically in range [0, 1] but can exceed for HDR.
 */
struct Pixel32 {
    float r, g, b;
    
    Pixel32() : r(0), g(0), b(0) {}
    Pixel32(float rr, float gg, float bb) : r(rr), g(gg), b(bb) {}
    
    Pixel32& operator+=(const Pixel32& o) {
        r += o.r; g += o.g; b += o.b;
        return *this;
    }
    
    Pixel32& operator*=(float s) {
        r *= s; g *= s; b *= s;
        return *this;
    }
};

/**
 * Immutable source image representation.
 * Stores original pixel data as loaded (sRGB encoded 8-bit or 16-bit).
 * This class is NEVER modified after construction.
 */
class SourceImage {
public:
    enum class BitDepth { U8, U16 };
    
private:
    std::vector<uint8_t> data_;  // Raw pixel data (interleaved RGB or RGBA)
    uint32_t width_, height_;
    uint8_t channels_;
    BitDepth bit_depth_;
    bool has_alpha_;
    
public:
    SourceImage() : width_(0), height_(0), channels_(0), bit_depth_(BitDepth::U8), has_alpha_(false) {}
    
    // Load from file (PNG/JPEG via stb_image)
    static std::optional<SourceImage> loadFromFile(const std::string& path);
    
    // Create from raw data (for testing)
    static SourceImage fromData(const std::vector<float>& linear_data, uint32_t w, uint32_t h);
    static SourceImage fromU8Data(const std::vector<uint8_t>& rgba_data, uint32_t w, uint32_t h, uint8_t channels);
    
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint8_t channels() const { return channels_; }
    BitDepth bitDepth() const { return bit_depth_; }
    bool hasAlpha() const { return has_alpha_; }
    
    // Access pixel at (x, y) - returns linearized float values
    Pixel32 getPixelLinear(uint32_t x, uint32_t y) const;
    
    // Access raw byte (for debugging/testing)
    const uint8_t* rawData() const { return data_.data(); }
    size_t dataSize() const { return data_.size(); }
    
    // Create a downsampled version for preview rendering
    SourceImage createDownsampled(uint32_t target_max_dim) const;
};

} // namespace pe
