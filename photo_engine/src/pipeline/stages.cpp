#include "pipeline/stages.hpp"
#include <cmath>
#include <algorithm>

namespace pe {

// ============================================================================
// Exposure Stage - Physically correct exposure in linear light
// ============================================================================

void ExposureStage::processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                                 uint32_t tile_x, uint32_t tile_y,
                                 uint32_t tile_w, uint32_t tile_h,
                                 const AdjustmentState& state) const {
    if (std::abs(state.exposure) < 0.001f) return;
    
    float multiplier = std::pow(2.0f, state.exposure);
    
    for (uint32_t y = tile_y; y < tile_y + tile_h && y < height; ++y) {
        FloatPixel* row = pixels + y * width;
        for (uint32_t x = tile_x; x < tile_x + tile_w && x < width; ++x) {
            FloatPixel& p = row[x];
            p.r *= multiplier;
            p.g *= multiplier;
            p.b *= multiplier;
        }
    }
}

// ============================================================================
// White Balance Stage - Bradford chromatic adaptation
// ============================================================================

void WhiteBalanceStage::processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                                     uint32_t tile_x, uint32_t tile_y,
                                     uint32_t tile_w, uint32_t tile_h,
                                     const AdjustmentState& state) const {
    float temp = state.temperature;
    float tint = state.tint;
    
    // Apply tint adjustment to temperature-derived white point
    // This is a simplified model - full implementation would use DCP profiles
    float tint_factor = 1.0f + tint / 100.0f * 0.15f;
    
    // Build adaptation matrix from D65 to target temperature
    ChromaticAdaptation::WhitePoint srcWP = ChromaticAdaptation::getD65();
    ChromaticAdaptation::WhitePoint dstWP = ChromaticAdaptation::getWhitePointForTemperature(temp);
    
    // Apply tint as green-magenta shift in XYZ space
    dstWP.X *= tint_factor;
    dstWP.Z /= tint_factor;
    
    float adapt_matrix[3][3];
    ChromaticAdaptation::buildBradfordMatrix(srcWP, dstWP, adapt_matrix);
    
    for (uint32_t y = tile_y; y < tile_y + tile_h && y < height; ++y) {
        FloatPixel* row = pixels + y * width;
        for (uint32_t x = tile_x; x < tile_x + tile_w && x < width; ++x) {
            FloatPixel& p = row[x];
            
            float new_r = adapt_matrix[0][0] * p.r + adapt_matrix[0][1] * p.g + adapt_matrix[0][2] * p.b;
            float new_g = adapt_matrix[1][0] * p.r + adapt_matrix[1][1] * p.g + adapt_matrix[1][2] * p.b;
            float new_b = adapt_matrix[2][0] * p.r + adapt_matrix[2][1] * p.g + adapt_matrix[2][2] * p.b;
            
            p.r = new_r;
            p.g = new_g;
            p.b = new_b;
        }
    }
}

// ============================================================================
// Tone Mapping Stage - Highlights, shadows, contrast, whites, blacks
// Uses smooth sigmoidal curves for natural tonal compression
// ============================================================================

void ToneMappingStage::processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                                    uint32_t tile_x, uint32_t tile_y,
                                    uint32_t tile_w, uint32_t tile_h,
                                    const AdjustmentState& state) const {
    bool has_contrast = state.contrast != 0.0f;
    bool has_highlights = state.highlights != 0.0f;
    bool has_shadows = state.shadows != 0.0f;
    bool has_whites = state.whites != 0.0f;
    bool has_blacks = state.blacks != 0.0f;
    
    if (!has_contrast && !has_highlights && !has_shadows && !has_whites && !has_blacks) {
        return;
    }
    
    // Precompute parameters
    float contrast_factor = 1.0f + state.contrast / 100.0f;
    float highlight_amt = -state.highlights / 100.0f;  // Negative highlights compress
    float shadow_amt = state.shadows / 100.0f;
    float white_shift = state.whites / 100.0f * 0.2f;
    float black_shift = state.blacks / 100.0f * 0.2f;
    
    for (uint32_t y = tile_y; y < tile_y + tile_h && y < height; ++y) {
        FloatPixel* row = pixels + y * width;
        for (uint32_t x = tile_x; x < tile_x + tile_w && x < width; ++x) {
            FloatPixel& p = row[x];
            
            // Process each channel
            auto process_channel = [&](float& c) {
                // Apply contrast first (pivot around middle gray ~0.18)
                if (has_contrast) {
                    c = 0.18f + (c - 0.18f) * contrast_factor;
                }
                
                // Compress highlights using smooth rolloff
                if (has_highlights && c > 0.5f) {
                    float highlight_region = (c - 0.5f) * 2.0f;  // 0-1 in highlights
                    float compression = 1.0f / (1.0f + highlight_amt * highlight_region);
                    c = 0.5f + (c - 0.5f) * compression;
                }
                
                // Lift shadows using smooth curve
                if (has_shadows && c < 0.5f) {
                    float shadow_region = 1.0f - c * 2.0f;  // 1 at black, 0 at mid
                    float lift = shadow_amt * shadow_region * shadow_region;
                    c += lift * 0.3f;
                }
                
                // Whites/blacks adjust endpoints
                if (has_whites) {
                    c = c + white_shift * c * c;
                }
                if (has_blacks) {
                    c = c + black_shift * (1.0f - c) * (1.0f - c);
                }
                
                c = std::max(0.0f, c);
            };
            
            process_channel(p.r);
            process_channel(p.g);
            process_channel(p.b);
        }
    }
}

// ============================================================================
// Color Adjustment Stage - Vibrance and saturation
// Uses HSL-like approach with proper luminance preservation
// ============================================================================

void ColorAdjustmentStage::processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                                        uint32_t tile_x, uint32_t tile_y,
                                        uint32_t tile_w, uint32_t tile_h,
                                        const AdjustmentState& state) const {
    if (state.vibrance == 0.0f && state.saturation == 0.0f) return;
    
    float vib_factor = state.vibrance / 100.0f;
    float sat_factor = 1.0f + state.saturation / 100.0f;
    
    for (uint32_t y = tile_y; y < tile_y + tile_h && y < height; ++y) {
        FloatPixel* row = pixels + y * width;
        for (uint32_t x = tile_x; x < tile_x + tile_w && x < width; ++x) {
            FloatPixel& p = row[x];
            
            float max_rgb = std::max({p.r, p.g, p.b});
            float min_rgb = std::min({p.r, p.g, p.b});
            float chroma = max_rgb - min_rgb;
            float luminance = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
            
            // Skip achromatic pixels for vibrance
            if (chroma < 0.0001f && state.vibrance != 0.0f) {
                // No vibrance effect on grayscale
            } else if (state.vibrance != 0.0f) {
                // Vibrance: selective saturation based on existing saturation
                float current_sat = (max_rgb > 0.0001f) ? (chroma / max_rgb) : 0.0f;
                
                // Less saturated colors get more boost
                float vibrance_weight = (1.0f - current_sat) * (1.0f - current_sat);
                float effective_vib = vib_factor * vibrance_weight * 0.5f;
                
                // Scale chroma
                float new_chroma = chroma * (1.0f + effective_vib);
                new_chroma = std::max(0.0f, new_chroma);
                
                if (chroma > 0.0001f) {
                    float scale = new_chroma / chroma;
                    float mid = luminance;  // Preserve luminance
                    p.r = mid + (p.r - mid) * scale;
                    p.g = mid + (p.g - mid) * scale;
                    p.b = mid + (p.b - mid) * scale;
                }
            }
            
            // Recalculate after vibrance
            max_rgb = std::max({p.r, p.g, p.b});
            min_rgb = std::min({p.r, p.g, p.b});
            chroma = max_rgb - min_rgb;
            
            // Apply saturation uniformly
            if (state.saturation != 0.0f) {
                float effective_sat = std::clamp(sat_factor, 0.0f, 3.0f);
                
                if (luminance > 0.0001f) {
                    float new_chroma = chroma * effective_sat;
                    new_chroma = std::min(new_chroma, luminance * 2.0f);  // Prevent clipping
                    
                    if (chroma > 0.0001f) {
                        float scale = new_chroma / chroma;
                        float mid = luminance;
                        p.r = mid + (p.r - mid) * scale;
                        p.g = mid + (p.g - mid) * scale;
                        p.b = mid + (p.b - mid) * scale;
                    }
                }
            }
            
            p.r = std::max(0.0f, p.r);
            p.g = std::max(0.0f, p.g);
            p.b = std::max(0.0f, p.b);
        }
    }
}

// ============================================================================
// Tone Curve Stage - Per-channel and composite curves
// ============================================================================

void ToneCurveStage::processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                                  uint32_t tile_x, uint32_t tile_y,
                                  uint32_t tile_w, uint32_t tile_h,
                                  const AdjustmentState& state) const {
    if (state.tone_curves.isIdentity()) return;
    
    for (uint32_t y = tile_y; y < tile_y + tile_h && y < height; ++y) {
        FloatPixel* row = pixels + y * width;
        for (uint32_t x = tile_x; x < tile_x + tile_w && x < width; ++x) {
            FloatPixel& p = row[x];
            state.tone_curves.apply(p.r, p.g, p.b);
        }
    }
}

// ============================================================================
// Render Pipeline
// ============================================================================

RenderPipeline::RenderPipeline() {
    stages_.push_back(std::make_unique<ExposureStage>());
    stages_.push_back(std::make_unique<WhiteBalanceStage>());
    stages_.push_back(std::make_unique<ToneMappingStage>());
    stages_.push_back(std::make_unique<ColorAdjustmentStage>());
    stages_.push_back(std::make_unique<ToneCurveStage>());
}

void RenderPipeline::execute(LinearImageBuffer& buffer, const AdjustmentState& state) const {
    executeTile(buffer, 0, 0, buffer.width(), buffer.height(), state);
}

void RenderPipeline::executeTile(LinearImageBuffer& buffer,
                                  uint32_t tile_x, uint32_t tile_y,
                                  uint32_t tile_w, uint32_t tile_h,
                                  const AdjustmentState& state) const {
    FloatPixel* data = buffer.data();
    uint32_t w = buffer.width();
    uint32_t h = buffer.height();
    
    for (const auto& stage : stages_) {
        if (stage->isActive(state)) {
            stage->processTile(data, w, h, tile_x, tile_y, tile_w, tile_h, state);
        }
    }
}

} // namespace pe
