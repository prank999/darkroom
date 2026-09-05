#include "document/ImageSource.h"
#include "color/ColorSpace.h"
#include <stdexcept>
#include <fstream>
#include <cstring>

// STB image loading/saving - single header library
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "../../third_party/stb_image.h"

namespace photo {

std::shared_ptr<ImageSource> ImageSource::loadFromFile(const std::string& path) {
    int width, height, channels;
    
    // Load image using stb_image (handles PNG, JPEG automatically)
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    
    if (!data) {
        throw std::runtime_error("Failed to load image: " + path);
    }
    
    auto source = std::make_shared<ImageSource>();
    source->width_ = width;
    source->height_ = height;
    source->channels_ = 3;  // Internal RGB
    
    // Convert from sRGB uint8 to linear float
    size_t pixelCount = static_cast<size_t>(width) * height;
    source->pixels_.resize(pixelCount * 3);  // RGB
    
    for (size_t i = 0; i < pixelCount; ++i) {
        size_t srcIdx = i * 4;  // RGBA input
        size_t dstIdx = i * 3;  // RGB output
        
        // Convert sRGB [0-255] to linear [0-1]
        source->pixels_[dstIdx + 0] = ColorSpace::srgbToLinear(data[srcIdx + 0] / 255.0f);
        source->pixels_[dstIdx + 1] = ColorSpace::srgbToLinear(data[srcIdx + 1] / 255.0f);
        source->pixels_[dstIdx + 2] = ColorSpace::srgbToLinear(data[srcIdx + 2] / 255.0f);
    }
    
    stbi_image_free(data);
    
    return source;
}

std::shared_ptr<ImageSource> ImageSource::createFromData(
    const uint8_t* rgba_data,
    int width,
    int height,
    int channels
) {
    if (!rgba_data || width <= 0 || height <= 0) {
        return nullptr;
    }
    
    auto source = std::make_shared<ImageSource>();
    source->width_ = width;
    source->height_ = height;
    source->channels_ = 3;
    
    size_t pixelCount = static_cast<size_t>(width) * height;
    source->pixels_.resize(pixelCount * 3);
    
    for (size_t i = 0; i < pixelCount; ++i) {
        size_t srcIdx = i * channels;
        size_t dstIdx = i * 3;
        
        float r = rgba_data[srcIdx + 0] / 255.0f;
        float g = (channels >= 2) ? rgba_data[srcIdx + 1] / 255.0f : r;
        float b = (channels >= 3) ? rgba_data[srcIdx + 2] / 255.0f : r;
        
        // Convert to linear
        source->pixels_[dstIdx + 0] = ColorSpace::srgbToLinear(r);
        source->pixels_[dstIdx + 1] = ColorSpace::srgbToLinear(g);
        source->pixels_[dstIdx + 2] = ColorSpace::srgbToLinear(b);
    }
    
    return source;
}

float ImageSource::getPixelLinear(int x, int y, int channel) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_ || 
        channel < 0 || channel >= 3) {
        return 0.0f;
    }
    
    size_t idx = static_cast<size_t>(y) * width_ * 3 + x * 3 + channel;
    return pixels_[idx];
}

const float* ImageSource::getRowLinear(int y) const {
    if (y < 0 || y >= height_) {
        return nullptr;
    }
    return &pixels_[static_cast<size_t>(y) * width_ * 3];
}

} // namespace photo
