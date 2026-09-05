#include "render/renderer.hpp"
#include "color/color_space.hpp"
#include <thread>
#include <mutex>
#include <map>
#include <functional>
#include <cstring>
#include <cmath>

namespace pe {

// ============================================================================
// RenderResult utilities
// ============================================================================

std::vector<uint8_t> RenderResult::toSRGBU8() const {
    if (!success || data.empty()) {
        return {};
    }
    
    std::vector<uint8_t> result(width * height * 3);
    
    for (size_t i = 0; i < width * height; ++i) {
        float r = data[i * 3];
        float g = data[i * 3 + 1];
        float b = data[i * 3 + 2];
        
        // Clamp to valid range before tone mapping
        r = std::clamp(r, 0.0f, 1.0f);
        g = std::clamp(g, 0.0f, 1.0f);
        b = std::clamp(b, 0.0f, 1.0f);
        
        // Convert linear to sRGB
        result[i * 3] = static_cast<uint8_t>(SRGBColorSpace::lineartosRGB(r) * 255.0f + 0.5f);
        result[i * 3 + 1] = static_cast<uint8_t>(SRGBColorSpace::lineartosRGB(g) * 255.0f + 0.5f);
        result[i * 3 + 2] = static_cast<uint8_t>(SRGBColorSpace::lineartosRGB(b) * 255.0f + 0.5f);
    }
    
    return result;
}

// ============================================================================
// RenderCache implementation
// ============================================================================

size_t RenderCache::computeStateHash(const AdjustmentState& state) const {
    // Simple hash combining all adjustment parameters
    size_t h = 0;
    
    auto combine = [&](float v) {
        h ^= std::hash<float>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
    };
    
    combine(state.exposure);
    combine(state.contrast);
    combine(state.highlights);
    combine(state.shadows);
    combine(state.whites);
    combine(state.blacks);
    combine(state.temperature);
    combine(state.tint);
    combine(state.vibrance);
    combine(state.saturation);
    
    // Hash curve LUTs for quick comparison
    for (int i = 0; i < ToneCurve::LUT_SIZE; ++i) {
        combine(state.tone_curves.rgb().evaluate(static_cast<float>(i) / ToneCurve::LUT_SIZE));
    }
    
    return h;
}

std::optional<RenderResult> RenderCache::get(const CacheKey& key) {
    auto it = cache_.find(key.state_hash);
    if (it != cache_.end() && 
        it->second.result.width == key.width &&
        it->second.result.height == key.height) {
        it->second.last_used = ++access_counter_;
        return it->second.result;
    }
    return std::nullopt;
}

void RenderCache::put(const CacheKey& key, const RenderResult& result) {
    // Check memory limit
    size_t new_memory = result.data.size() * sizeof(float);
    while (current_memory_usage_ + new_memory > MAX_CACHE_MEMORY_MB * 1024 * 1024 &&
           !cache_.empty()) {
        // Remove least recently used entry
        size_t lru_key = 0;
        uint64_t lru_time = UINT64_MAX;
        for (const auto& [k, v] : cache_) {
            if (v.last_used < lru_time) {
                lru_time = v.last_used;
                lru_key = k;
            }
        }
        current_memory_usage_ -= cache_[lru_key].result.data.size() * sizeof(float);
        cache_.erase(lru_key);
    }
    
    CacheEntry entry;
    entry.result = result;
    entry.last_used = ++access_counter_;
    
    cache_[key.state_hash] = entry;
    current_memory_usage_ += new_memory;
}

void RenderCache::clear() {
    cache_.clear();
    current_memory_usage_ = 0;
}

// ============================================================================
// IRenderer default implementations
// ============================================================================

std::future<RenderResult> IRenderer::renderAsync(const SourceImage& source,
                                                  const AdjustmentState& state,
                                                  const RenderOptions& options) {
    return std::async(std::launch::async, [this, &source, &state, options]() {
        return this->render(source, state, options);
    });
}

// ============================================================================
// CPURenderer Implementation
// ============================================================================

/**
 * RENDERING PIPELINE - MATHEMATICAL FOUNDATION
 * 
 * The rendering pipeline processes pixels in the following order:
 * 
 * 1. LINEARIZATION: Convert sRGB-encoded source to linear light
 *    - Uses exact sRGB EOTF (piecewise function)
 *    - This is critical for physically-correct exposure
 * 
 * 2. EXPOSURE: Apply photographic exposure in linear space
 *    - Formula: L_out = L_in * 2^EV
 *    - EV is in stops (doubling/halving of light)
 *    - Operating in linear space ensures correct behavior
 * 
 * 3. WHITE BALANCE: Chromatic adaptation using Bradford transform
 *    - Converts between color temperatures via XYZ intermediate
 *    - Preserves luminance while shifting chromaticity
 * 
 * 4. TONE MAPPING (Highlights/Shadows/Contrast):
 *    - Uses smooth, tone-aware transformations
 *    - Highlights: Compress values above middle gray using smooth knee
 *    - Shadows: Lift dark values using sigmoidal curve
 *    - Contrast: Adjust slope around middle gray
 * 
 * 5. TONE CURVES: Apply parametric curves per channel
 *    - Catmull-Rom spline interpolation through control points
 *    - High-precision LUT (4096 entries)
 * 
 * 6. COLOR ADJUSTMENTS (Vibrance/Saturation):
 *    - Convert to HSV/HSL-like representation
 *    - Vibrance: Weighted saturation boost favoring less-saturated colors
 *    - Saturation: Uniform scaling of chroma
 * 
 * 7. GAMMA ENCODING: Convert back to sRGB for display
 *    - Uses exact sRGB OETF
 */

Pixel32 CPURenderer::applyAdjustments(Pixel32 pixel, const AdjustmentState& state) const {
    float r = pixel.r;
    float g = pixel.g;
    float b = pixel.b;
    
    // =========================================================================
    // STEP 1: Exposure (in linear light)
    // =========================================================================
    // Exposure formula: L_out = L_in * 2^EV
    // This is the physically-correct way to simulate camera exposure
    float exposure_multiplier = std::pow(2.0f, state.exposure);
    r *= exposure_multiplier;
    g *= exposure_multiplier;
    b *= exposure_multiplier;
    
    // =========================================================================
    // STEP 2: White Balance (Chromatic Adaptation)
    // =========================================================================
    // Reference white point is D65 (6500K) for sRGB
    // Adjust temperature shifts the white point
    if (std::abs(state.temperature - 6500.0f) > 1.0f || std::abs(state.tint) > 0.1f) {
        // Apply chromatic adaptation from reference (6500K) to target temperature
        ChromaticAdaptation::adaptRGB(r, g, b, 6500.0f, state.temperature);
        
        // Tint adjustment (green-magenta shift)
        // This is a simplified approximation
        if (state.tint != 0.0f) {
            float tint_factor = state.tint / 100.0f;
            g -= tint_factor * 0.1f;  // Green reduction for positive tint (magenta)
            b += tint_factor * 0.05f; // Slight blue shift with magenta
            g = std::max(0.0f, g);
            b = std::max(0.0f, b);
        }
    }
    
    // =========================================================================
    // STEP 3: Calculate luminance for tone-aware adjustments
    // =========================================================================
    // Rec. 709 luminance coefficients
    float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    
    // =========================================================================
    // STEP 4: Highlights and Shadows (Tone-aware)
    // =========================================================================
    // Highlights: Compress bright areas using smooth knee function
    // Shadows: Lift dark areas using inverse knee
    
    const float middle_gray = 0.18f;  // Standard middle gray reference
    
    // Highlights processing
    if (state.highlights != 0.0f) {
        float highlight_amount = state.highlights / 100.0f;
        
        // Create smooth mask for highlights (values above middle gray)
        float highlight_mask = 0.0f;
        if (luminance > middle_gray) {
            // Smoothstep function for gradual transition
            float t = (luminance - middle_gray) / (1.0f - middle_gray);
            t = std::clamp(t, 0.0f, 1.0f);
            highlight_mask = t * t * (3.0f - 2.0f * t);  // Smoothstep
        }
        
        // Compress or expand highlights
        float highlight_factor = 1.0f - highlight_amount * 0.5f;
        highlight_factor = std::max(0.1f, highlight_factor);
        
        // Apply only to highlights
        float scale = 1.0f + (highlight_factor - 1.0f) * highlight_mask;
        r *= scale;
        g *= scale;
        b *= scale;
    }
    
    // Shadows processing
    if (state.shadows != 0.0f) {
        float shadow_amount = state.shadows / 100.0f;
        
        // Create smooth mask for shadows (values below middle gray)
        float shadow_mask = 0.0f;
        if (luminance < middle_gray) {
            float t = luminance / middle_gray;
            t = std::clamp(t, 0.0f, 1.0f);
            shadow_mask = 1.0f - t * t * (3.0f - 2.0f * t);  // Inverse smoothstep
        }
        
        // Lift or compress shadows
        float shadow_lift = shadow_amount * 0.3f;  // Max lift amount
        
        // Apply shadow adjustment
        float shadow_scale = 1.0f + shadow_lift * shadow_mask;
        r = r * shadow_scale + shadow_lift * shadow_mask * 0.1f;
        g = g * shadow_scale + shadow_lift * shadow_mask * 0.1f;
        b = b * shadow_scale + shadow_lift * shadow_mask * 0.1f;
    }
    
    // =========================================================================
    // STEP 5: Contrast (Sigmoidal curve around middle gray)
    // =========================================================================
    if (state.contrast != 0.0f) {
        float contrast_factor = 1.0f + state.contrast / 100.0f;
        contrast_factor = std::clamp(contrast_factor, 0.1f, 10.0f);
        
        // Sigmoidal contrast curve
        // f(x) = 0.5 + sign(x-0.5) * (|x-0.5|^contrast) / 2
        // But we apply it relative to middle gray
        
        auto apply_contrast = [&](float c) -> float {
            float normalized = c / (c + 1.0f);  // Bring to ~0-1 range
            float centered = normalized - 0.5f;
            float scaled = centered * contrast_factor;
            float result = 0.5f + scaled;
            result = result * (c + 1.0f);  // Restore scale
            return std::max(0.0f, result);
        };
        
        // Simpler approach: scale distance from middle gray
        r = middle_gray + (r - middle_gray) * contrast_factor;
        g = middle_gray + (g - middle_gray) * contrast_factor;
        b = middle_gray + (b - middle_gray) * contrast_factor;
        
        r = std::max(0.0f, r);
        g = std::max(0.0f, g);
        b = std::max(0.0f, b);
    }
    
    // =========================================================================
    // STEP 6: Whites and Blacks (Point adjustments)
    // =========================================================================
    if (state.whites != 0.0f || state.blacks != 0.0f) {
        float white_adjust = state.whites / 100.0f * 0.2f;  // Subtle adjustment
        float black_adjust = state.blacks / 100.0f * 0.2f;
        
        // Adjust white point (scale bright values)
        if (luminance > 0.5f) {
            float white_mask = (luminance - 0.5f) * 2.0f;
            r += white_adjust * white_mask;
            g += white_adjust * white_mask;
            b += white_adjust * white_mask;
        }
        
        // Adjust black point (offset dark values)
        if (luminance < 0.5f) {
            float black_mask = 1.0f - luminance * 2.0f;
            r += black_adjust * black_mask;
            g += black_adjust * black_mask;
            b += black_adjust * black_mask;
            
            r = std::max(0.0f, r);
            g = std::max(0.0f, g);
            b = std::max(0.0f, b);
        }
    }
    
    // =========================================================================
    // STEP 7: Tone Curves
    // =========================================================================
    state.tone_curves.apply(r, g, b);
    
    // =========================================================================
    // STEP 8: Vibrance and Saturation
    // =========================================================================
    // Convert to HSV-like representation for saturation adjustments
    
    float max_rgb = std::max({r, g, b});
    float min_rgb = std::min({r, g, b});
    float chroma = max_rgb - min_rgb;
    float value = max_rgb;
    
    // Calculate saturation (chroma relative to value)
    float saturation_current = (value > 0.0001f) ? (chroma / value) : 0.0f;
    
    // Apply vibrance first (selective saturation)
    if (state.vibrance != 0.0f) {
        float vibrance_factor = state.vibrance / 100.0f;
        
        // Vibrance affects less-saturated colors more
        // This prevents oversaturation of already-saturated colors
        float vibrance_weight = 1.0f - saturation_current;  // More weight for less saturated
        vibrance_weight = vibrance_weight * vibrance_weight;  // Emphasize the effect
        
        float vibrance_adjust = vibrance_factor * vibrance_weight * 0.5f;
        
        // Scale chroma
        float new_chroma = chroma * (1.0f + vibrance_adjust);
        new_chroma = std::max(0.0f, new_chroma);
        
        if (chroma > 0.0001f) {
            float scale = new_chroma / chroma;
            float mid = value - chroma * 0.5f;
            r = mid + (r - mid) * scale;
            g = mid + (g - mid) * scale;
            b = mid + (b - mid) * scale;
        }
    }
    
    // Apply saturation (uniform)
    if (state.saturation != 0.0f) {
        float sat_factor = 1.0f + state.saturation / 100.0f;
        sat_factor = std::clamp(sat_factor, 0.0f, 3.0f);  // Can go to B&W (0) or oversaturate
        
        // Recalculate after vibrance
        max_rgb = std::max({r, g, b});
        min_rgb = std::min({r, g, b});
        chroma = max_rgb - min_rgb;
        value = max_rgb;
        
        if (value > 0.0001f) {
            float new_chroma = chroma * sat_factor;
            new_chroma = std::min(new_chroma, value);  // Don't exceed value
            
            if (chroma > 0.0001f) {
                float scale = new_chroma / chroma;
                float mid = value - chroma * 0.5f;
                r = mid + (r - mid) * scale;
                g = mid + (g - mid) * scale;
                b = mid + (b - mid) * scale;
            }
        } else {
            // Achromatic color - add slight saturation if increasing
            if (sat_factor > 1.0f) {
                // Leave as-is (can't create saturation from nothing)
            }
        }
    }
    
    // Ensure non-negative
    r = std::max(0.0f, r);
    g = std::max(0.0f, g);
    b = std::max(0.0f, b);
    
    return Pixel32(r, g, b);
}

void CPURenderer::processTile(const SourceImage& source,
                               const AdjustmentState& state,
                               float* output_buffer,
                               uint32_t tile_x, uint32_t tile_y,
                               uint32_t tile_w, uint32_t tile_h,
                               const RenderOptions& options) {
    uint32_t src_width = source.width();
    uint32_t src_height = source.height();
    
    for (uint32_t y = 0; y < tile_h; ++y) {
        // Check cancellation
        if (options.cancel_flag && *options.cancel_flag) {
            return;
        }
        
        for (uint32_t x = 0; x < tile_w; ++x) {
            uint32_t src_x = tile_x + x;
            uint32_t src_y = tile_y + y;
            
            if (src_x >= src_width || src_y >= src_height) {
                continue;
            }
            
            // Get linear pixel from source
            Pixel32 pixel = source.getPixelLinear(src_x, src_y);
            
            // Apply all adjustments
            Pixel32 adjusted = applyAdjustments(pixel, state);
            
            // Write to output buffer
            size_t out_idx = ((tile_y + y) * src_width + (tile_x + x)) * 3;
            output_buffer[out_idx] = adjusted.r;
            output_buffer[out_idx + 1] = adjusted.g;
            output_buffer[out_idx + 2] = adjusted.b;
        }
        
        // Progress callback
        if (options.progress_callback) {
            float progress = static_cast<float>(y + 1) / tile_h;
            options.progress_callback(progress);
        }
    }
}

RenderResult CPURenderer::render(const SourceImage& source,
                                  const AdjustmentState& state,
                                  const RenderOptions& options) {
    RenderResult result;
    
    // Determine output dimensions
    uint32_t out_width = source.width();
    uint32_t out_height = source.height();
    
    if (options.preview_mode) {
        // Create downsampled source for preview
        SourceImage preview_source = source.createDownsampled(options.preview_max_dim);
        out_width = preview_source.width();
        out_height = preview_source.height();
        
        // Render at preview resolution
        result.data.resize(out_width * out_height * 3);
        result.width = out_width;
        result.height = out_height;
        
        // Process in tiles for parallelization opportunity
        const uint32_t tile_size = 256;
        
        for (uint32_t ty = 0; ty < out_height; ty += tile_size) {
            uint32_t tile_h = std::min(tile_size, out_height - ty);
            
            for (uint32_t tx = 0; tx < out_width; tx += tile_size) {
                uint32_t tile_w = std::min(tile_size, out_width - tx);
                
                processTile(preview_source, state, result.data.data(),
                           tx, ty, tile_w, tile_h, options);
                
                if (options.cancel_flag && *options.cancel_flag) {
                    result.success = false;
                    result.error_message = "Render cancelled";
                    return result;
                }
            }
        }
    } else {
        // Full resolution render
        result.data.resize(out_width * out_height * 3);
        result.width = out_width;
        result.height = out_height;
        
        // Check cache
        if (options.use_cache) {
            RenderCache::CacheKey key;
            key.state_hash = cache_.publicComputeStateHash(state);
            key.width = out_width;
            key.height = out_height;
            key.is_preview = false;
            
            if (auto cached = cache_.get(key)) {
                return *cached;
            }
        }
        
        // Process in tiles
        const uint32_t tile_size = 256;
        
        for (uint32_t ty = 0; ty < out_height; ty += tile_size) {
            uint32_t tile_h = std::min(tile_size, out_height - ty);
            
            for (uint32_t tx = 0; tx < out_width; tx += tile_size) {
                uint32_t tile_w = std::min(tile_size, out_width - tx);
                
                processTile(source, state, result.data.data(),
                           tx, ty, tile_w, tile_h, options);
                
                if (options.cancel_flag && *options.cancel_flag) {
                    result.success = false;
                    result.error_message = "Render cancelled";
                    return result;
                }
            }
        }
        
        // Store in cache
        if (options.use_cache) {
            RenderCache::CacheKey key;
            key.state_hash = cache_.publicComputeStateHash(state);
            key.width = out_width;
            key.height = out_height;
            key.is_preview = false;
            cache_.put(key, result);
        }
    }
    
    // Validate output (no NaN/Inf)
    for (size_t i = 0; i < result.data.size(); ++i) {
        float v = result.data[i];
        if (std::isnan(v) || std::isinf(v)) {
            result.data[i] = 0.0f;  // Clamp invalid values
        }
    }
    
    result.success = true;
    return result;
}

// ============================================================================
// Document Implementation
// ============================================================================

Document::Document() : renderer_(std::make_unique<CPURenderer>()) {
    // Default constructor - no source image yet
}

Document::Document(std::shared_ptr<SourceImage> source) 
    : source_image_(source), renderer_(std::make_unique<CPURenderer>()) {
    // Start with default adjustment state
    history_.emplace_back();
}

std::optional<Document> Document::loadFromFile(const std::string& path) {
    auto source = SourceImage::loadFromFile(path);
    if (!source) {
        return std::nullopt;
    }
    
    Document doc(std::make_shared<SourceImage>(std::move(*source)));
    return doc;
}

const AdjustmentState& Document::getCurrentState() const {
    if (history_.empty()) {
        static AdjustmentState default_state;
        return default_state;
    }
    return history_[current_index_];
}

void Document::applyState(const AdjustmentState& state) {
    // If we're not at the end of history, truncate future states
    if (current_index_ < history_.size() - 1) {
        history_.resize(current_index_ + 1);
    }
    
    // Add new state
    history_.push_back(state);
    current_index_ = history_.size() - 1;
    
    // Invalidate cached render
    cached_state_index_ = SIZE_MAX;
}

void Document::modifyCurrentState(std::function<void(AdjustmentState&)> modifier) {
    AdjustmentState new_state = getCurrentState();
    modifier(new_state);
    applyState(new_state);
}

bool Document::undo() {
    if (!canUndo()) return false;
    --current_index_;
    cached_state_index_ = SIZE_MAX;
    return true;
}

bool Document::redo() {
    if (!canRedo()) return false;
    ++current_index_;
    cached_state_index_ = SIZE_MAX;
    return true;
}

RenderResult Document::render(const RenderOptions& options) const {
    // Check if we have a cached render for current state
    if (cached_state_index_ == current_index_ && !cached_render_.data.empty()) {
        if (options.preview_mode == (cached_render_.width < source_image_->width())) {
            return cached_render_;
        }
    }
    
    // Render using renderer
    RenderResult result = renderer_->render(*source_image_, getCurrentState(), options);
    
    // Cache the result
    if (result.success) {
        cached_render_ = result;
        cached_state_index_ = current_index_;
    }
    
    return result;
}

std::future<RenderResult> Document::renderAsync(const RenderOptions& options) {
    return renderer_->renderAsync(*source_image_, getCurrentState(), options);
}

} // namespace pe
