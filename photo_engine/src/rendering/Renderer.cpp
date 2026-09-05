#include "rendering/Renderer.h"
#include "color/ColorSpace.h"
#include <thread>
#include <algorithm>

namespace photo {

std::shared_ptr<RenderedImage> Renderer::render(
    const std::shared_ptr<ImageSource>& source,
    const AdjustmentState& adjustments,
    const RenderOptions& options
) {
    cancelled_ = false;
    return renderImpl(source, adjustments, options);
}

std::future<std::shared_ptr<RenderedImage>> Renderer::renderAsync(
    const std::shared_ptr<ImageSource>& source,
    const AdjustmentState& adjustments,
    const RenderOptions& options
) {
    cancelled_ = false;
    
    // Capture by value to ensure thread safety
    auto srcPtr = source;
    auto adj = adjustments;
    auto opts = options;
    
    return std::async(std::launch::async, [this, srcPtr, adj, opts]() {
        if (cancelled_) {
            return std::shared_ptr<RenderedImage>();
        }
        return renderImpl(srcPtr, adj, opts);
    });
}

void Renderer::cancel() {
    cancelled_ = true;
}

std::shared_ptr<RenderedImage> Renderer::renderImpl(
    const std::shared_ptr<ImageSource>& source,
    const AdjustmentState& adjustments,
    const RenderOptions& options
) {
    if (!source || !source->isValid()) {
        return nullptr;
    }
    
    // Calculate output dimensions
    int outWidth = static_cast<int>(source->width() * options.scale);
    int outHeight = static_cast<int>(source->height() * options.scale);
    
    if (outWidth <= 0 || outHeight <= 0) {
        return nullptr;
    }
    
    // Handle ROI
    int roiX = options.roiX > 0 ? std::min(options.roiX, source->width() - 1) : 0;
    int roiY = options.roiY > 0 ? std::min(options.roiY, source->height() - 1) : 0;
    int roiW = options.roiWidth > 0 ? std::min(options.roiWidth, source->width() - roiX) : source->width();
    int roiH = options.roiHeight > 0 ? std::min(options.roiHeight, source->height() - roiY) : source->height();
    
    // Create output image
    auto output = std::make_shared<RenderedImage>(outWidth, outHeight);
    
    // Build tone curves
    ToneCurveSet curves;
    curves.rgbCurve = ToneCurve(adjustments.rgbCurve);
    curves.redCurve = ToneCurve(adjustments.redCurve);
    curves.greenCurve = ToneCurve(adjustments.greenCurve);
    curves.blueCurve = ToneCurve(adjustments.blueCurve);
    
    // Calculate step for downsampling in preview mode
    float step = 1.0f / options.scale;
    bool useStep = (step > 1.01f);  // Only if significantly downscaling
    
    // Pre-calculate exposure multiplier
    // Exposure: each stop doubles/halves light
    float exposureMult = std::pow(2.0f, adjustments.exposure);
    
    // Process each pixel
    for (int outY = 0; outY < outHeight; ++outY) {
        if (cancelled_) {
            return nullptr;
        }
        
        for (int outX = 0; outX < outWidth; ++outX) {
            // Map output coordinate to source coordinate
            float srcX = (outX + 0.5f) * step;
            float srcY = (outY + 0.5f) * step;
            
            // Clamp to valid range
            srcX = std::clamp(srcX, 0.0f, static_cast<float>(source->width() - 1));
            srcY = std::clamp(srcY, 0.0f, static_cast<float>(source->height() - 1));
            
            // Sample source pixel (bilinear interpolation for quality)
            int x0 = static_cast<int>(srcX);
            int y0 = static_cast<int>(srcY);
            int x1 = std::min(x0 + 1, source->width() - 1);
            int y1 = std::min(y0 + 1, source->height() - 1);
            
            float tx = srcX - x0;
            float ty = srcY - y0;
            
            // Get four corner pixels
            const float* p00 = source->getRowLinear(y0) + x0 * 3;
            const float* p10 = source->getRowLinear(y0) + x1 * 3;
            const float* p01 = source->getRowLinear(y1) + x0 * 3;
            const float* p11 = source->getRowLinear(y1) + x1 * 3;
            
            // Bilinear interpolation
            float r = p00[0] * (1-tx)*(1-ty) + p10[0] * tx*(1-ty) +
                      p01[0] * (1-tx)*ty + p11[0] * tx*ty;
            float g = p00[1] * (1-tx)*(1-ty) + p10[1] * tx*(1-ty) +
                      p01[1] * (1-tx)*ty + p11[1] * tx*ty;
            float b = p00[2] * (1-tx)*(1-ty) + p10[2] * tx*(1-ty) +
                      p01[2] * (1-tx)*ty + p11[2] * tx*ty;
            
            // Process pixel through pipeline
            float outR, outG, outB, outA;
            processPixel(r, g, b, 1.0f, outR, outG, outB, outA, adjustments, curves);
            
            // Write to output
            float* dst = output->getPixelPtr(outX, outY);
            dst[0] = outR;
            dst[1] = outG;
            dst[2] = outB;
            dst[3] = outA;
        }
    }
    
    return output;
}

void Renderer::processPixel(
    float r, float g, float b, float a,
    float& outR, float& outG, float& outB, float& outA,
    const AdjustmentState& adjustments,
    const ToneCurveSet& curves
) const {
    // All processing happens in linear light space
    
    // 1. Apply exposure (in linear space - this is correct photographic exposure)
    float exposureMult = std::pow(2.0f, adjustments.exposure);
    r *= exposureMult;
    g *= exposureMult;
    b *= exposureMult;
    
    // 2. Apply white balance (temperature/tint)
    ColorSpace::applyWhiteBalance(r, g, b, adjustments.temperature, adjustments.tint);
    
    // 3. Apply highlights/shadows (tone-aware)
    // Highlights affect bright areas, shadows affect dark areas
    if (adjustments.highlights != 0 || adjustments.shadows != 0) {
        float luma = ColorSpace::toLuminance(r, g, b);
        
        // Smooth weighting functions
        float highlightWeight = luma * luma;  // Quadratic - affects brights more
        float shadowWeight = (1 - luma) * (1 - luma);  // Affects darks more
        
        float highlightAdj = 1.0f + (adjustments.highlights / 100.0f);
        float shadowAdj = 1.0f + (adjustments.shadows / 100.0f);
        
        // Apply with smooth falloff
        float combinedAdj = 1.0f + 
            (highlightAdj - 1.0f) * highlightWeight +
            (shadowAdj - 1.0f) * shadowWeight;
        
        r *= combinedAdj;
        g *= combinedAdj;
        b *= combinedAdj;
    }
    
    // 4. Apply whites/blacks (affect extreme tones)
    if (adjustments.whites != 0 || adjustments.blacks != 0) {
        float luma = ColorSpace::toLuminance(r, g, b);
        
        // Whites affect very bright areas
        if (adjustments.whites != 0 && luma > 0.8f) {
            float whiteAdj = 1.0f + (adjustments.whites / 100.0f) * ((luma - 0.8f) / 0.2f);
            r *= whiteAdj;
            g *= whiteAdj;
            b *= whiteAdj;
        }
        
        // Blacks affect very dark areas
        if (adjustments.blacks != 0 && luma < 0.2f) {
            float blackAdj = 1.0f + (adjustments.blacks / 100.0f) * ((0.2f - luma) / 0.2f);
            r *= blackAdj;
            g *= blackAdj;
            b *= blackAdj;
        }
    }
    
    // 5. Apply contrast (using smooth sigmoid curve)
    if (adjustments.contrast != 0) {
        float contrastFactor = 1.0f + (adjustments.contrast / 100.0f);
        contrastFactor = std::clamp(contrastFactor, 0.1f, 10.0f);
        
        // Apply contrast to luminance while preserving hue
        float luma = ColorSpace::toLuminance(r, g, b);
        
        // Sigmoid contrast curve centered at 0.5
        float contrastedLuma = 0.5f + (luma - 0.5f) * contrastFactor;
        contrastedLuma = std::clamp(contrastedLuma, 0.0f, 1.0f);
        
        // Scale RGB to match new luminance
        if (luma > 0.0001f) {
            float scale = contrastedLuma / luma;
            r *= scale;
            g *= scale;
            b *= scale;
        }
    }
    
    // 6. Apply color adjustments (saturation/vibrance)
    ColorSpace::applyColorAdjustments(r, g, b, adjustments.saturation, adjustments.vibrance);
    
    // 7. Apply tone curves
    if (!curves.isIdentity()) {
        curves.apply(r, g, b);
    }
    
    // 8. Convert back to sRGB for output
    ColorSpace::linearToSrgb(r, g, b);
    
    // 9. Final clamp and validation
    outR = ColorSpace::clamp(r);
    outG = ColorSpace::clamp(g);
    outB = ColorSpace::clamp(b);
    outA = a;
    
    // Ensure no NaN/Inf
    if (!ColorSpace::isValid(outR, outG, outB)) {
        outR = outG = outB = 0.0f;
    }
}

} // namespace photo
