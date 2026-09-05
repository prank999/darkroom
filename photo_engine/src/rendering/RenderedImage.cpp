#include "rendering/RenderedImage.h"

namespace photo {

RenderedImage::RenderedImage(int width, int height)
    : width_(width), height_(height) {
    pixels_.resize(static_cast<size_t>(width) * height * 4);  // RGBA
}

float* RenderedImage::getPixelPtr(int x, int y) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        return nullptr;
    }
    return &pixels_[static_cast<size_t>(y) * width_ * 4 + x * 4];
}

const float* RenderedImage::getPixelPtr(int x, int y) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        return nullptr;
    }
    return &pixels_[static_cast<size_t>(y) * width_ * 4 + x * 4];
}

void RenderedImage::clear() {
    std::fill(pixels_.begin(), pixels_.end(), 0.0f);
}

} // namespace photo
