#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "core/image.hpp"
#include "color/color_space.hpp"
#include <cstring>
#include <stdexcept>

namespace pe {

std::optional<SourceImage> SourceImage::loadFromFile(const std::string& path) {
    int w, h, channels;
    
    // Load image with stb_image (auto-detects PNG/JPEG)
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);  // Force RGBA
    
    if (!data) {
        return std::nullopt;
    }
    
    SourceImage img;
    img.width_ = static_cast<uint32_t>(w);
    img.height_ = static_cast<uint32_t>(h);
    img.channels_ = 4;  // We load as RGBA
    img.bit_depth_ = BitDepth::U8;
    img.has_alpha_ = channels >= 4;
    
    // Copy data
    size_t pixel_count = img.width_ * img.height_;
    img.data_.resize(pixel_count * 4);
    std::memcpy(img.data_.data(), data, img.data_.size());
    
    stbi_image_free(data);
    
    return img;
}

SourceImage SourceImage::fromData(const std::vector<float>& linear_data, uint32_t w, uint32_t h) {
    SourceImage img;
    img.width_ = w;
    img.height_ = h;
    img.channels_ = 3;
    img.bit_depth_ = BitDepth::U8;  // We store as float internally for this case
    img.has_alpha_ = false;
    
    // This is a special constructor for test data
    // Store as normalized float values in a byte buffer (hack for testing)
    img.data_.resize(w * h * 3 * sizeof(float));
    std::memcpy(img.data_.data(), linear_data.data(), linear_data.size() * sizeof(float));
    
    return img;
}

SourceImage SourceImage::fromU8Data(const std::vector<uint8_t>& rgba_data, uint32_t w, uint32_t h, uint8_t channels) {
    SourceImage img;
    img.width_ = w;
    img.height_ = h;
    img.channels_ = channels;
    img.bit_depth_ = BitDepth::U8;
    img.has_alpha_ = channels >= 4;
    img.data_ = rgba_data;
    return img;
}

Pixel32 SourceImage::getPixelLinear(uint32_t x, uint32_t y) const {
    if (x >= width_ || y >= height_) {
        return Pixel32(0, 0, 0);
    }
    
    size_t pixel_idx = (y * width_ + x);
    
    // Special handling for float data (from fromData)
    if (bit_depth_ == BitDepth::U8 && data_.size() == width_ * height_ * 3 * sizeof(float)) {
        const float* float_data = reinterpret_cast<const float*>(data_.data());
        return Pixel32(
            float_data[pixel_idx * 3],
            float_data[pixel_idx * 3 + 1],
            float_data[pixel_idx * 3 + 2]
        );
    }
    
    // Normal U8 path
    const uint8_t* pixel = data_.data() + pixel_idx * channels_;
    
    float r = pixel[0] / 255.0f;
    float g = pixel[1] / 255.0f;
    float b = (channels_ >= 3) ? (pixel[2] / 255.0f) : 0.0f;
    
    // Convert from sRGB to linear light
    SRGBColorSpace::sRGBtoLinear(r, g, b);
    
    return Pixel32(r, g, b);
}

SourceImage SourceImage::createDownsampled(uint32_t target_max_dim) const {
    if (width_ <= target_max_dim && height_ <= target_max_dim) {
        return *this;  // Already small enough
    }
    
    // Calculate scale factor
    float scale = std::min(
        static_cast<float>(target_max_dim) / width_,
        static_cast<float>(target_max_dim) / height_
    );
    
    uint32_t new_w = std::max(1u, static_cast<uint32_t>(width_ * scale));
    uint32_t new_h = std::max(1u, static_cast<uint32_t>(height_ * scale));
    
    // Simple box filter downsampling
    std::vector<uint8_t> new_data(new_w * new_h * channels_);
    
    float x_ratio = static_cast<float>(width_) / new_w;
    float y_ratio = static_cast<float>(height_) / new_h;
    
    for (uint32_t y = 0; y < new_h; ++y) {
        for (uint32_t x = 0; x < new_w; ++x) {
            // Box filter: average over the source region
            float src_x_start = x * x_ratio;
            float src_y_start = y * y_ratio;
            float src_x_end = (x + 1) * x_ratio;
            float src_y_end = (y + 1) * y_ratio;
            
            float sum_r = 0, sum_g = 0, sum_b = 0;
            int count = 0;
            
            for (int sy = static_cast<int>(src_y_start); sy < src_y_end; ++sy) {
                for (int sx = static_cast<int>(src_x_start); sx < src_x_end; ++sx) {
                    if (sx >= 0 && sx < static_cast<int>(width_) &&
                        sy >= 0 && sy < static_cast<int>(height_)) {
                        const uint8_t* pixel = data_.data() + (sy * width_ + sx) * channels_;
                        sum_r += pixel[0];
                        sum_g += pixel[1];
                        sum_b += (channels_ >= 3) ? pixel[2] : pixel[0];
                        count++;
                    }
                }
            }
            
            size_t dst_idx = (y * new_w + x) * channels_;
            new_data[dst_idx] = static_cast<uint8_t>(sum_r / count);
            new_data[dst_idx + 1] = static_cast<uint8_t>(sum_g / count);
            if (channels_ >= 3) {
                new_data[dst_idx + 2] = static_cast<uint8_t>(sum_b / count);
            }
            if (channels_ >= 4) {
                new_data[dst_idx + 3] = 255;  // Alpha
            }
        }
    }
    
    return SourceImage::fromU8Data(new_data, new_w, new_h, channels_);
}

} // namespace pe
