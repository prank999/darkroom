#pragma once
/**
 * @file image.hpp
 * @brief High-precision image representations for professional photo processing
 * 
 * ARCHITECTURAL DECISIONS:
 * 
 * 1. SEPARATION OF ENCODED vs LINEAR DATA:
 *    - EncodedImage: Stores original sRGB-encoded data (uint8_t or uint16_t)
 *    - LinearImageBuffer: Stores linear-light floating-point data for processing
 *    - This distinction is critical for correct photographic operations
 * 
 * 2. IMMUTABILITY:
 *    - Source images are NEVER modified after construction
 *    - All adjustments produce new rendered buffers
 *    - Thread-safe by design
 * 
 * 3. HIGH PRECISION:
 *    - Internal processing uses float32 per channel
 *    - Can exceed [0,1] range for HDR processing
 *    - No lossy conversions during adjustment pipeline
 * 
 * 4. MEMORY EFFICIENCY:
 *    - Row-major layout for cache-friendly access
 *    - Optional alpha channel
 *    - Move semantics to avoid copies
 */

#include <vector>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace pe {

// Forward declarations
class EncodedImage;
class LinearImageBuffer;

/**
 * @brief Pixel format for high-precision linear light processing
 * 
 * Values represent linear light intensity, typically in range [0, 1]
 * but can exceed this for HDR intermediate results.
 */
struct alignas(16) FloatPixel {
    float r, g, b, a;
    
    FloatPixel() : r(0), g(0), b(0), a(1.0f) {}
    FloatPixel(float rr, float gg, float bb, float aa = 1.0f) 
        : r(rr), g(gg), b(bb), a(aa) {}
    
    // Vector operations for SIMD-friendly code
    FloatPixel& operator+=(const FloatPixel& o) {
        r += o.r; g += o.g; b += o.b; a += o.a;
        return *this;
    }
    
    FloatPixel& operator-=(const FloatPixel& o) {
        r -= o.r; g -= o.g; b -= o.b; a -= o.a;
        return *this;
    }
    
    FloatPixel& operator*=(float s) {
        r *= s; g *= s; b *= s; a *= s;
        return *this;
    }
    
    FloatPixel operator*(float s) const {
        return FloatPixel(r * s, g * s, b * s, a * s);
    }
    
    FloatPixel operator+(const FloatPixel& o) const {
        return FloatPixel(r + o.r, g + o.g, b + o.b, a + o.a);
    }
    
    // Component-wise multiplication
    FloatPixel cmul(const FloatPixel& o) const {
        return FloatPixel(r * o.r, g * o.g, b * o.b, a * o.a);
    }
    
    // Get luminance using Rec. 709 coefficients
    float luminance() const {
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }
    
    bool isValid() const {
        return !std::isnan(r) && !std::isnan(g) && !std::isnan(b) &&
               !std::isinf(r) && !std::isinf(g) && !std::isinf(b);
    }
};

/**
 * @brief Immutable encoded image (as loaded from file)
 * 
 * Stores pixel data exactly as decoded from PNG/JPEG.
 * This class is NEVER modified - it's the source of truth.
 */
class EncodedImage {
public:
    enum class BitDepth : uint8_t { U8 = 8, U16 = 16 };
    enum class ColorSpace : uint8_t { SRGB, LINEAR, ADOBE_RGB, PROPHOTO_RGB };
    
private:
    std::vector<uint8_t> data_;  // Raw interleaved pixel data
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint8_t channels_ = 3;
    BitDepth bit_depth_ = BitDepth::U8;
    ColorSpace color_space_ = ColorSpace::SRGB;
    std::string filename_;
    
public:
    EncodedImage() = default;
    
    // Load from file (PNG/JPEG via stb_image)
    static std::optional<EncodedImage> loadFromFile(const std::string& path);
    
    // Create from raw data (for testing)
    static EncodedImage fromU8Data(const std::vector<uint8_t>& data, 
                                   uint32_t width, uint32_t height, 
                                   uint8_t channels = 3,
                                   ColorSpace cs = ColorSpace::SRGB);
    
    static EncodedImage fromU16Data(const std::vector<uint16_t>& data,
                                    uint32_t width, uint32_t height,
                                    uint8_t channels = 3,
                                    ColorSpace cs = ColorSpace::SRGB);
    
    // Accessors
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint8_t channels() const { return channels_; }
    BitDepth bitDepth() const { return bit_depth_; }
    ColorSpace colorSpace() const { return color_space_; }
    size_t dataSize() const { return data_.size(); }
    const uint8_t* rawData() const { return data_.data(); }
    const std::string& filename() const { return filename_; }
    
    // Get pixel as normalized float [0, 1] (still encoded, NOT linearized)
    FloatPixel getPixelNormalized(uint32_t x, uint32_t y) const;
    
    // Get pixel and linearize (convert from encoded to linear light)
    FloatPixel getPixelLinear(uint32_t x, uint32_t y) const;
    
    // Create downsampled version for preview proxy
    EncodedImage createDownsampled(uint32_t max_dimension) const;
    
    // Total pixel count
    size_t pixelCount() const { return static_cast<size_t>(width_) * height_; }
};

/**
 * @brief Mutable linear-light image buffer for rendering
 * 
 * This is the working representation during the render pipeline.
 * All adjustments operate on LinearImageBuffer, not EncodedImage.
 * 
 * Designed for:
 * - Cache-friendly row-major access
 * - SIMD vectorization (16-byte aligned)
 * - Tile-based processing
 * - Multi-threaded writes
 */
class LinearImageBuffer {
private:
    std::vector<FloatPixel> pixels_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool owns_data_ = true;
    
public:
    LinearImageBuffer() = default;
    
    explicit LinearImageBuffer(uint32_t width, uint32_t height, bool initialize = false)
        : width_(width), height_(height) {
        pixels_.resize(static_cast<size_t>(width) * height);
        if (initialize) {
            std::fill(pixels_.begin(), pixels_.end(), FloatPixel());
        }
    }
    
    // Create from existing data (copy)
    LinearImageBuffer(const FloatPixel* data, uint32_t width, uint32_t height)
        : width_(width), height_(height) {
        pixels_.assign(data, data + static_cast<size_t>(width) * height);
    }
    
    // Move constructor
    LinearImageBuffer(LinearImageBuffer&& other) noexcept = default;
    LinearImageBuffer& operator=(LinearImageBuffer&& other) noexcept = default;
    
    // Copy constructor (explicit for performance awareness)
    LinearImageBuffer(const LinearImageBuffer& other) = default;
    LinearImageBuffer& operator=(const LinearImageBuffer& other) = default;
    
    // Access
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    size_t pixelCount() const { return static_cast<size_t>(width_) * height_; }
    
    // Direct pixel access (no bounds checking for performance)
    FloatPixel& at(uint32_t x, uint32_t y) {
        return pixels_[y * width_ + x];
    }
    
    const FloatPixel& at(uint32_t x, uint32_t y) const {
        return pixels_[y * width_ + x];
    }
    
    // Raw data pointer for SIMD/vectorized operations
    FloatPixel* data() { return pixels_.data(); }
    const FloatPixel* data() const { return pixels_.data(); }
    
    // Get row pointer for tile processing
    FloatPixel* row(uint32_t y) {
        return pixels_.data() + y * width_;
    }
    
    const FloatPixel* row(uint32_t y) const {
        return pixels_.data() + y * width_;
    }
    
    // Fill with a value
    void fill(const FloatPixel& value) {
        std::fill(pixels_.begin(), pixels_.end(), value);
    }
    
    // Check for invalid values (NaN/Inf)
    bool hasInvalidValues() const {
        for (const auto& p : pixels_) {
            if (!p.isValid()) return true;
        }
        return false;
    }
    
    // Clamp all values to valid range
    void clamp(float min_val = 0.0f, float max_val = 65504.0f) {
        for (auto& p : pixels_) {
            p.r = std::clamp(p.r, min_val, max_val);
            p.g = std::clamp(p.g, min_val, max_val);
            p.b = std::clamp(p.b, min_val, max_val);
        }
    }
    
    // Convert to 8-bit sRGB for display
    std::vector<uint8_t> toSRGBU8() const;
    
    // Convert to 16-bit sRGB for export
    std::vector<uint16_t> toSRGBU16() const;
};

/**
 * @brief Proxy image for fast preview rendering
 * 
 * A persistent downsampled representation that avoids regenerating
 * the preview on every slider change. Updated only when needed.
 */
class PreviewProxy {
private:
    std::shared_ptr<EncodedImage> low_res_source_;
    uint32_t scale_factor_ = 1;
    uint32_t original_width_ = 0;
    uint32_t original_height_ = 0;
    bool valid_ = false;
    
public:
    PreviewProxy() = default;
    
    // Create proxy from full-resolution source
    static PreviewProxy create(const EncodedImage& source, uint32_t max_dimension = 2048);
    
    const EncodedImage& lowResSource() const { return *low_res_source_; }
    bool hasLowResSource() const { return low_res_source_ != nullptr; }
    
    uint32_t scaleFactor() const { return scale_factor_; }
    uint32_t originalWidth() const { return original_width_; }
    uint32_t originalHeight() const { return original_height_; }
    bool isValid() const { return valid_; }
    
    // Map preview coordinates to original
    uint32_t mapX(uint32_t preview_x) const {
        return std::min(preview_x * scale_factor_, original_width_ - 1);
    }
    
    uint32_t mapY(uint32_t preview_y) const {
        return std::min(preview_y * scale_factor_, original_height_ - 1);
    }
};

} // namespace pe
