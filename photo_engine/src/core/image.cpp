#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "core/image.hpp"
#include "color/color_space.hpp"
#include <cmath>
#include <stdexcept>

namespace pe {

// ============================================================================
// EncodedImage Implementation
// ============================================================================

std::optional<EncodedImage> EncodedImage::loadFromFile(const std::string& path) {
    int w, h, channels_in_file;
    
    // Load with stb_image - request 4 channels for consistent RGBA handling
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels_in_file, 4);
    
    if (!data) {
        return std::nullopt;
    }
    
    EncodedImage img;
    img.width_ = static_cast<uint32_t>(w);
    img.height_ = static_cast<uint32_t>(h);
    img.channels_ = 4;  // We force RGBA
    img.bit_depth_ = BitDepth::U8;
    img.color_space_ = ColorSpace::SRGB;  // Assume sRGB unless embedded profile
    img.filename_ = path;
    
    // Copy data
    size_t pixel_count = img.width_ * img.height_;
    img.data_.resize(pixel_count * 4);
    std::memcpy(img.data_.data(), data, img.data_.size());
    
    stbi_image_free(data);
    
    return img;
}

EncodedImage EncodedImage::fromU8Data(const std::vector<uint8_t>& data,
                                       uint32_t width, uint32_t height,
                                       uint8_t channels,
                                       ColorSpace cs) {
    EncodedImage img;
    img.width_ = width;
    img.height_ = height;
    img.channels_ = channels;
    img.bit_depth_ = BitDepth::U8;
    img.color_space_ = cs;
    img.data_ = data;
    return img;
}

EncodedImage EncodedImage::fromU16Data(const std::vector<uint16_t>& data,
                                        uint32_t width, uint32_t height,
                                        uint8_t channels,
                                        ColorSpace cs) {
    EncodedImage img;
    img.width_ = width;
    img.height_ = height;
    img.channels_ = channels;
    img.bit_depth_ = BitDepth::U16;
    img.color_space_ = cs;
    
    // Store U16 data as bytes
    size_t num_pixels = width * height;
    img.data_.resize(num_pixels * channels * 2);  // 2 bytes per channel
    std::memcpy(img.data_.data(), data.data(), img.data_.size());
    
    return img;
}

FloatPixel EncodedImage::getPixelNormalized(uint32_t x, uint32_t y) const {
    if (x >= width_ || y >= height_) {
        return FloatPixel(0, 0, 0, 1);
    }
    
    size_t pixel_idx = static_cast<size_t>(y) * width_ + x;
    
    if (bit_depth_ == BitDepth::U8) {
        const uint8_t* pixel = data_.data() + pixel_idx * channels_;
        float r = pixel[0] / 255.0f;
        float g = (channels_ >= 2) ? (pixel[1] / 255.0f) : r;
        float b = (channels_ >= 3) ? (pixel[2] / 255.0f) : r;
        float a = (channels_ >= 4) ? (pixel[3] / 255.0f) : 1.0f;
        return FloatPixel(r, g, b, a);
    } else {
        // U16
        const uint16_t* pixel = reinterpret_cast<const uint16_t*>(data_.data()) 
                               + pixel_idx * channels_;
        float r = pixel[0] / 65535.0f;
        float g = (channels_ >= 2) ? (pixel[1] / 65535.0f) : r;
        float b = (channels_ >= 3) ? (pixel[2] / 65535.0f) : r;
        float a = (channels_ >= 4) ? (pixel[3] / 65535.0f) : 1.0f;
        return FloatPixel(r, g, b, a);
    }
}

FloatPixel EncodedImage::getPixelLinear(uint32_t x, uint32_t y) const {
    FloatPixel p = getPixelNormalized(x, y);
    
    // Convert from encoded to linear based on color space
    if (color_space_ == ColorSpace::SRGB) {
        p.r = SRGBColorSpace::sRGBtoLinear(p.r);
        p.g = SRGBColorSpace::sRGBtoLinear(p.g);
        p.b = SRGBColorSpace::sRGBtoLinear(p.b);
    }
    // LINEAR space needs no conversion
    // Other color spaces would need their own EOTF
    
    return p;
}

EncodedImage EncodedImage::createDownsampled(uint32_t max_dim) const {
    if (width_ <= max_dim && height_ <= max_dim) {
        return *this;
    }
    
    // Calculate scale factor
    float scale = std::min(
        static_cast<float>(max_dim) / width_,
        static_cast<float>(max_dim) / height_
    );
    
    uint32_t new_w = std::max(1u, static_cast<uint32_t>(width_ * scale));
    uint32_t new_h = std::max(1u, static_cast<uint32_t>(height_ * scale));
    
    // Lanczos-3 downsampling for quality preview
    const int lanczos_kernel = 3;
    
    auto lanczos = [](float x) -> float {
        if (x == 0.0f) return 1.0f;
        x = std::abs(x);
        if (x >= static_cast<float>(lanczos_kernel)) return 0.0f;
        float pi_x = 3.14159265359f * x;
        return std::sin(pi_x) / pi_x * std::sin(pi_x / lanczos_kernel) / (pi_x / lanczos_kernel);
    };
    
    std::vector<uint8_t> new_data(new_w * new_h * channels_);
    
    float x_ratio = static_cast<float>(width_) / new_w;
    float y_ratio = static_cast<float>(height_) / new_h;
    float filter_scale = std::max(x_ratio, y_ratio);
    
    for (uint32_t y = 0; y < new_h; ++y) {
        for (uint32_t x = 0; x < new_w; ++x) {
            float src_x = (x + 0.5f) * x_ratio - 0.5f;
            float src_y = (y + 0.5f) * y_ratio - 0.5f;
            
            float sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            float weight_sum = 0;
            
            int x_start = static_cast<int>(src_x - filter_scale * lanczos_kernel);
            int x_end = static_cast<int>(src_x + filter_scale * lanczos_kernel);
            int y_start = static_cast<int>(src_y - filter_scale * lanczos_kernel);
            int y_end = static_cast<int>(src_y + filter_scale * lanczos_kernel);
            
            for (int sy = y_start; sy <= y_end; ++sy) {
                for (int sx = x_start; sx <= x_end; ++sx) {
                    if (sx >= 0 && sx < static_cast<int>(width_) &&
                        sy >= 0 && sy < static_cast<int>(height_)) {
                        
                        float wx = lanczos((sx - src_x) / filter_scale);
                        float wy = lanczos((sy - src_y) / filter_scale);
                        float w = wx * wy;
                        
                        const uint8_t* pixel = data_.data() 
                            + (static_cast<size_t>(sy) * width_ + sx) * channels_;
                        
                        sum_r += pixel[0] * w;
                        sum_g += pixel[1] * w;
                        sum_b += (channels_ >= 3) ? (pixel[2] * w) : (pixel[0] * w);
                        sum_a += (channels_ >= 4) ? (pixel[3] * w) : (255.0f * w);
                        weight_sum += w;
                    }
                }
            }
            
            if (weight_sum > 0.0001f) {
                size_t dst_idx = (static_cast<size_t>(y) * new_w + x) * channels_;
                new_data[dst_idx] = static_cast<uint8_t>(std::clamp(sum_r / weight_sum, 0.0f, 255.0f));
                new_data[dst_idx + 1] = static_cast<uint8_t>(std::clamp(sum_g / weight_sum, 0.0f, 255.0f));
                if (channels_ >= 3) {
                    new_data[dst_idx + 2] = static_cast<uint8_t>(std::clamp(sum_b / weight_sum, 0.0f, 255.0f));
                }
                if (channels_ >= 4) {
                    new_data[dst_idx + 3] = static_cast<uint8_t>(std::clamp(sum_a / weight_sum, 0.0f, 255.0f));
                }
            }
        }
    }
    
    return EncodedImage::fromU8Data(new_data, new_w, new_h, channels_, color_space_);
}

PreviewProxy PreviewProxy::create(const EncodedImage& source, uint32_t max_dim) {
    PreviewProxy proxy;
    proxy.original_width_ = source.width();
    proxy.original_height_ = source.height();
    
    if (source.width() <= max_dim && source.height() <= max_dim) {
        // No downsampling needed - share the original
        proxy.low_res_source_ = std::make_shared<EncodedImage>(source);
        proxy.scale_factor_ = 1;
    } else {
        proxy.low_res_source_ = std::make_shared<EncodedImage>(source.createDownsampled(max_dim));
        proxy.scale_factor_ = std::max(1u, (source.width() + max_dim - 1) / max_dim);
    }
    
    proxy.valid_ = true;
    return proxy;
}

// ============================================================================
// LinearImageBuffer Implementation
// ============================================================================

std::vector<uint8_t> LinearImageBuffer::toSRGBU8() const {
    std::vector<uint8_t> result(width_ * height_ * 3);
    
    for (size_t i = 0; i < pixelCount(); ++i) {
        const FloatPixel& p = pixels_[i];
        
        // Clamp before tone mapping
        float r = std::clamp(p.r, 0.0f, 1.0f);
        float g = std::clamp(p.g, 0.0f, 1.0f);
        float b = std::clamp(p.b, 0.0f, 1.0f);
        
        result[i * 3] = static_cast<uint8_t>(SRGBColorSpace::lineartosRGB(r) * 255.0f + 0.5f);
        result[i * 3 + 1] = static_cast<uint8_t>(SRGBColorSpace::lineartosRGB(g) * 255.0f + 0.5f);
        result[i * 3 + 2] = static_cast<uint8_t>(SRGBColorSpace::lineartosRGB(b) * 255.0f + 0.5f);
    }
    
    return result;
}

std::vector<uint16_t> LinearImageBuffer::toSRGBU16() const {
    std::vector<uint16_t> result(width_ * height_ * 3);
    
    for (size_t i = 0; i < pixelCount(); ++i) {
        const FloatPixel& p = pixels_[i];
        
        float r = std::clamp(p.r, 0.0f, 1.0f);
        float g = std::clamp(p.g, 0.0f, 1.0f);
        float b = std::clamp(p.b, 0.0f, 1.0f);
        
        result[i * 3] = static_cast<uint16_t>(SRGBColorSpace::lineartosRGB(r) * 65535.0f + 0.5f);
        result[i * 3 + 1] = static_cast<uint16_t>(SRGBColorSpace::lineartosRGB(g) * 65535.0f + 0.5f);
        result[i * 3 + 2] = static_cast<uint16_t>(SRGBColorSpace::lineartosRGB(b) * 65535.0f + 0.5f);
    }
    
    return result;
}

} // namespace pe
