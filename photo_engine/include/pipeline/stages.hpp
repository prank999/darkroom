#pragma once
/**
 * @file stages.hpp
 * @brief Modular render pipeline stages for professional photo processing
 * 
 * ARCHITECTURE:
 * - Each stage is independent and can be optimized/replaced separately
 * - Stages operate on LinearImageBuffer in-place where possible
 * - Designed for future GPU offloading (each stage maps to a compute shader)
 * - Supports tile-based processing for memory efficiency
 */

#include "core/image.hpp"
#include "adjustments/adjustment_state.hpp"
#include <functional>
#include <memory>

namespace pe {

/**
 * @brief Base class for all pipeline stages
 */
class IRenderStage {
public:
    virtual ~IRenderStage() = default;
    virtual const char* name() const = 0;
    
    // Process a tile of the image
    virtual void processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                            uint32_t tile_x, uint32_t tile_y,
                            uint32_t tile_w, uint32_t tile_h,
                            const AdjustmentState& state) const = 0;
    
    // Check if stage needs processing for given state
    virtual bool isActive(const AdjustmentState& state) const = 0;
};

/**
 * @brief Stage 1: Exposure adjustment in linear light
 * Formula: L_out = L_in * 2^EV
 */
class ExposureStage : public IRenderStage {
public:
    const char* name() const override { return "Exposure"; }
    
    void processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                    uint32_t tile_x, uint32_t tile_y,
                    uint32_t tile_w, uint32_t tile_h,
                    const AdjustmentState& state) const override;
    
    bool isActive(const AdjustmentState& state) const override {
        return std::abs(state.exposure) > 0.001f;
    }
};

/**
 * @brief Stage 2: White balance via chromatic adaptation
 */
class WhiteBalanceStage : public IRenderStage {
public:
    const char* name() const override { return "WhiteBalance"; }
    
    void processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                    uint32_t tile_x, uint32_t tile_y,
                    uint32_t tile_w, uint32_t tile_h,
                    const AdjustmentState& state) const override;
    
    bool isActive(const AdjustmentState& state) const override {
        return std::abs(state.temperature - 6500.0f) > 1.0f || 
               std::abs(state.tint) > 0.1f;
    }
};

/**
 * @brief Stage 3: Tone mapping (highlights/shadows/contrast)
 */
class ToneMappingStage : public IRenderStage {
public:
    const char* name() const override { return "ToneMapping"; }
    
    void processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                    uint32_t tile_x, uint32_t tile_y,
                    uint32_t tile_w, uint32_t tile_h,
                    const AdjustmentState& state) const override;
    
    bool isActive(const AdjustmentState& state) const override {
        return state.contrast != 0.0f || state.highlights != 0.0f || 
               state.shadows != 0.0f || state.whites != 0.0f || 
               state.blacks != 0.0f;
    }
};

/**
 * @brief Stage 4: Color adjustments (vibrance/saturation)
 */
class ColorAdjustmentStage : public IRenderStage {
public:
    const char* name() const override { return "ColorAdjustment"; }
    
    void processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                    uint32_t tile_x, uint32_t tile_y,
                    uint32_t tile_w, uint32_t tile_h,
                    const AdjustmentState& state) const override;
    
    bool isActive(const AdjustmentState& state) const override {
        return state.vibrance != 0.0f || state.saturation != 0.0f;
    }
};

/**
 * @brief Stage 5: Tone curves (per-channel + composite)
 */
class ToneCurveStage : public IRenderStage {
public:
    const char* name() const override { return "ToneCurve"; }
    
    void processTile(FloatPixel* pixels, uint32_t width, uint32_t height,
                    uint32_t tile_x, uint32_t tile_y,
                    uint32_t tile_w, uint32_t tile_h,
                    const AdjustmentState& state) const override;
    
    bool isActive(const AdjustmentState& state) const override {
        return !state.tone_curves.isIdentity();
    }
};

/**
 * @brief Pipeline executor that chains stages together
 */
class RenderPipeline {
private:
    std::vector<std::unique_ptr<IRenderStage>> stages_;
    
public:
    RenderPipeline();
    
    void execute(LinearImageBuffer& buffer, const AdjustmentState& state) const;
    
    void executeTile(LinearImageBuffer& buffer, 
                    uint32_t tile_x, uint32_t tile_y,
                    uint32_t tile_w, uint32_t tile_h,
                    const AdjustmentState& state) const;
};

} // namespace pe
