#pragma once

#include "document/ImageSource.h"
#include "adjustments/AdjustmentState.h"
#include "rendering/RenderedImage.h"
#include "adjustments/ToneCurve.h"
#include <memory>
#include <future>
#include <atomic>

namespace photo {

/**
 * Rendering options.
 */
struct RenderOptions {
    // Output scale (1.0 = full resolution, 0.5 = half, etc.)
    float scale = 1.0f;
    
    // Region of interest (if non-zero)
    int roiX = 0;
    int roiY = 0;
    int roiWidth = 0;
    int roiHeight = 0;
    
    // Quality settings
    bool highQuality = true;
    
    // Preview mode (faster, lower quality)
    bool previewMode = false;
};

/**
 * Core rendering pipeline.
 * Takes immutable source + adjustment state and produces rendered output.
 * Does NOT modify the source or previous renders.
 */
class Renderer {
public:
    Renderer() = default;
    
    /**
     * Render synchronously.
     * @param source Immutable source image
     * @param adjustments Adjustment parameters
     * @param options Rendering options
     * @return Rendered image at specified resolution
     */
    std::shared_ptr<RenderedImage> render(
        const std::shared_ptr<ImageSource>& source,
        const AdjustmentState& adjustments,
        const RenderOptions& options = RenderOptions{}
    );
    
    /**
     * Render asynchronously.
     * Returns a future that resolves to the rendered image.
     */
    std::future<std::shared_ptr<RenderedImage>> renderAsync(
        const std::shared_ptr<ImageSource>& source,
        const AdjustmentState& adjustments,
        const RenderOptions& options = RenderOptions{}
    );
    
    /**
     * Cancel pending async render.
     */
    void cancel();

private:
    std::atomic<bool> cancelled_{false};
    
    // Internal render implementation
    std::shared_ptr<RenderedImage> renderImpl(
        const std::shared_ptr<ImageSource>& source,
        const AdjustmentState& adjustments,
        const RenderOptions& options
    );
    
    // Process a single pixel through the pipeline
    void processPixel(
        float r, float g, float b, float a,
        float& outR, float& outG, float& outB, float& outA,
        const AdjustmentState& adjustments,
        const ToneCurveSet& curves
    ) const;
};

} // namespace photo
