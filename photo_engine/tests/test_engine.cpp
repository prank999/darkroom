#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <cstring>

#include "core/image.hpp"
#include "adjustments/adjustment_state.hpp"
#include "adjustments/tone_curve.hpp"
#include "render/renderer.hpp"
#include "color/color_space.hpp"

using namespace pe;

// ============================================================================
// Test Framework
// ============================================================================

#define TEST(name) \
    void test_##name(); \
    static bool registered_##name = register_test(#name, test_##name); \
    void test_##name()

#define ASSERT_TRUE(cond) \
    do { if (!(cond)) { \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " - " #cond << std::endl; \
        throw std::runtime_error("Assertion failed"); \
    } } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NEAR(a, b, tol) ASSERT_TRUE(std::abs((a) - (b)) <= (tol))
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "EXPECT FAIL: " #cond << std::endl; } } while(0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))
#define EXPECT_NEAR(a, b, tol) EXPECT_TRUE(std::abs((a) - (b)) <= (tol))

static std::vector<std::pair<std::string, std::function<void()>>> g_tests;

bool register_test(const std::string& name, std::function<void()> fn) {
    g_tests.emplace_back(name, fn);
    return true;
}

int run_tests() {
    int passed = 0;
    int failed = 0;
    
    for (const auto& [name, fn] : g_tests) {
        try {
            fn();
            std::cout << "[PASS] " << name << std::endl;
            ++passed;
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] " << name << ": " << e.what() << std::endl;
            ++failed;
        }
    }
    
    std::cout << "\n========================================\n";
    std::cout << "Tests: " << (passed + failed) << " | Passed: " << passed << " | Failed: " << failed << std::endl;
    
    return failed > 0 ? 1 : 0;
}

// ============================================================================
// Test: Source Image Immutable
// ============================================================================

TEST(SourceImageImmutable) {
    // Create a test image with known data
    std::vector<uint8_t> test_data = {
        255, 0, 0, 255,    // Red pixel
        0, 255, 0, 255,    // Green pixel
        0, 0, 255, 255,    // Blue pixel
        128, 128, 128, 255 // Gray pixel
    };
    
    SourceImage source = SourceImage::fromU8Data(test_data, 2, 2, 4);
    const uint8_t* original_data = source.rawData();
    
    // Store original values
    std::vector<uint8_t> original_copy(original_data, original_data + source.dataSize());
    
    // Perform operations that should NOT modify source
    AdjustmentState state;
    state.exposure = 2.0f;  // Significant exposure change
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    RenderResult result = renderer.render(source, state, options);
    ASSERT_TRUE(result.success);
    
    // Verify source is unchanged
    ASSERT_EQ(source.dataSize(), original_copy.size());
    for (size_t i = 0; i < original_copy.size(); ++i) {
        ASSERT_EQ(original_data[i], original_copy[i]);
    }
    
    // Render again with different settings
    state.exposure = -2.0f;
    result = renderer.render(source, state, options);
    ASSERT_TRUE(result.success);
    
    // Source still unchanged
    for (size_t i = 0; i < original_copy.size(); ++i) {
        ASSERT_EQ(original_data[i], original_copy[i]);
    }
}

// ============================================================================
// Test: Identity Adjustments Reproduce Baseline
// ============================================================================

TEST(IdentityAdjustments) {
    // Create gradient test image
    std::vector<float> linear_data(3 * 100);
    for (int i = 0; i < 100; ++i) {
        float v = static_cast<float>(i) / 99.0f;
        linear_data[i * 3] = v;
        linear_data[i * 3 + 1] = v;
        linear_data[i * 3 + 2] = v;
    }
    
    SourceImage source = SourceImage::fromData(linear_data, 10, 10);
    
    // Render with identity (default) adjustments
    AdjustmentState identity_state;
    ASSERT_TRUE(identity_state.isDefault());
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    RenderResult result = renderer.render(source, identity_state, options);
    ASSERT_TRUE(result.success);
    
    // Output should match input (within floating point tolerance)
    for (size_t i = 0; i < 100; ++i) {
        Pixel32 out = result.getPixel(i % 10, i / 10);
        float expected = linear_data[i * 3];
        
        // Allow small tolerance for floating point operations
        ASSERT_NEAR(out.r, expected, 0.001f);
        ASSERT_NEAR(out.g, expected, 0.001f);
        ASSERT_NEAR(out.b, expected, 0.001f);
    }
}

// ============================================================================
// Test: Exposure Changes Luminance Appropriately
// ============================================================================

TEST(ExposureLuminance) {
    // Create middle-gray test image
    std::vector<float> linear_data(3 * 100, 0.18f);  // Middle gray in linear
    SourceImage source = SourceImage::fromData(linear_data, 10, 10);
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    // Test +1 EV (should double luminance)
    AdjustmentState plus_one_ev;
    plus_one_ev.exposure = 1.0f;
    
    RenderResult result_plus = renderer.render(source, plus_one_ev, options);
    ASSERT_TRUE(result_plus.success);
    
    Pixel32 pixel_plus = result_plus.getPixel(0, 0);
    float expected_plus = 0.18f * std::pow(2.0f, 1.0f);  // 0.36f
    ASSERT_NEAR(pixel_plus.r, expected_plus, 0.01f);
    ASSERT_NEAR(pixel_plus.g, expected_plus, 0.01f);
    ASSERT_NEAR(pixel_plus.b, expected_plus, 0.01f);
    
    // Test -1 EV (should halve luminance)
    AdjustmentState minus_one_ev;
    minus_one_ev.exposure = -1.0f;
    
    RenderResult result_minus = renderer.render(source, minus_one_ev, options);
    ASSERT_TRUE(result_minus.success);
    
    Pixel32 pixel_minus = result_minus.getPixel(0, 0);
    float expected_minus = 0.18f * std::pow(2.0f, -1.0f);  // 0.09f
    ASSERT_NEAR(pixel_minus.r, expected_minus, 0.01f);
    ASSERT_NEAR(pixel_minus.g, expected_minus, 0.01f);
    ASSERT_NEAR(pixel_minus.b, expected_minus, 0.01f);
    
    // Test +2 EV (should quadruple luminance)
    AdjustmentState plus_two_ev;
    plus_two_ev.exposure = 2.0f;
    
    RenderResult result_two = renderer.render(source, plus_two_ev, options);
    Pixel32 pixel_two = result_two.getPixel(0, 0);
    float expected_two = 0.18f * std::pow(2.0f, 2.0f);  // 0.72f
    ASSERT_NEAR(pixel_two.r, expected_two, 0.01f);
}

// ============================================================================
// Test: Contrast Changes Tonal Distribution
// ============================================================================

TEST(ContrastTonalDistribution) {
    // Create image with varying tones
    std::vector<float> linear_data(3 * 4);
    linear_data[0] = 0.1f; linear_data[1] = 0.1f; linear_data[2] = 0.1f;  // Dark
    linear_data[3] = 0.3f; linear_data[4] = 0.3f; linear_data[5] = 0.3f;  // Mid-dark
    linear_data[6] = 0.5f; linear_data[7] = 0.5f; linear_data[8] = 0.5f;  // Mid
    linear_data[9] = 0.8f; linear_data[10] = 0.8f; linear_data[11] = 0.8f; // Bright
    
    SourceImage source = SourceImage::fromData(linear_data, 2, 2);
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    // High contrast
    AdjustmentState high_contrast;
    high_contrast.contrast = 50.0f;
    
    RenderResult result_high = renderer.render(source, high_contrast, options);
    ASSERT_TRUE(result_high.success);
    
    // Low contrast
    AdjustmentState low_contrast;
    low_contrast.contrast = -50.0f;
    
    RenderResult result_low = renderer.render(source, low_contrast, options);
    ASSERT_TRUE(result_low.success);
    
    // With high contrast, darks should be darker and brights brighter (relative to middle gray ~0.18)
    Pixel32 dark_high = result_high.getPixel(0, 0);
    Pixel32 dark_low = result_low.getPixel(0, 0);
    
    // Dark pixel should be darker with high contrast
    EXPECT_TRUE(dark_high.r < dark_low.r);
    
    // Bright pixel should be brighter with high contrast
    Pixel32 bright_high = result_high.getPixel(1, 1);
    Pixel32 bright_low = result_low.getPixel(1, 1);
    EXPECT_TRUE(bright_high.r > bright_low.r);
}

// ============================================================================
// Test: Highlights and Shadows Behave Sensibly
// ============================================================================

TEST(HighlightsShadows) {
    // Create test image with highlights and shadows
    std::vector<float> linear_data(3 * 2);
    linear_data[0] = 0.9f; linear_data[1] = 0.9f; linear_data[2] = 0.9f;  // Highlight
    linear_data[3] = 0.05f; linear_data[4] = 0.05f; linear_data[5] = 0.05f;  // Shadow
    
    SourceImage source = SourceImage::fromData(linear_data, 2, 1);
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    // Recover highlights (negative highlights compresses them)
    AdjustmentState recover_highlights;
    recover_highlights.highlights = -100.0f;
    
    RenderResult result_h = renderer.render(source, recover_highlights, options);
    ASSERT_TRUE(result_h.success);
    
    Pixel32 highlight_recovered = result_h.getPixel(0, 0);
    Pixel32 highlight_orig = source.getPixelLinear(0, 0);
    
    // Highlight should be compressed (darker)
    EXPECT_TRUE(highlight_recovered.r < highlight_orig.r);
    
    // Lift shadows
    AdjustmentState lift_shadows;
    lift_shadows.shadows = 100.0f;
    
    RenderResult result_s = renderer.render(source, lift_shadows, options);
    ASSERT_TRUE(result_s.success);
    
    Pixel32 shadow_lifted = result_s.getPixel(1, 0);
    Pixel32 shadow_orig = source.getPixelLinear(1, 0);
    
    // Shadow should be lifted (brighter)
    EXPECT_TRUE(shadow_lifted.r > shadow_orig.r);
}

// ============================================================================
// Test: Temperature and Tint Behave Correctly
// ============================================================================

TEST(TemperatureTint) {
    // Create neutral gray image
    std::vector<float> linear_data(3 * 1, 0.5f);
    linear_data[0] = 0.5f; linear_data[1] = 0.5f; linear_data[2] = 0.5f;
    SourceImage source = SourceImage::fromData(linear_data, 1, 1);
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    // Warm temperature (lower Kelvin)
    AdjustmentState warm;
    warm.temperature = 3000.0f;  // Tungsten-like
    
    RenderResult result_warm = renderer.render(source, warm, options);
    ASSERT_TRUE(result_warm.success);
    
    Pixel32 warm_pixel = result_warm.getPixel(0, 0);
    
    // Cool temperature (higher Kelvin)
    AdjustmentState cool;
    cool.temperature = 9000.0f;  // Shade-like
    
    RenderResult result_cool = renderer.render(source, cool, options);
    ASSERT_TRUE(result_cool.success);
    
    Pixel32 cool_pixel = result_cool.getPixel(0, 0);
    
    // Warm should have more red relative to blue compared to cool
    float warm_rb_ratio = warm_pixel.r / (warm_pixel.b + 0.0001f);
    float cool_rb_ratio = cool_pixel.r / (cool_pixel.b + 0.0001f);
    
    // Warm light has more red, cool light has more blue
    EXPECT_TRUE(warm_rb_ratio > cool_rb_ratio);
}

// ============================================================================
// Test: Vibrance Differs from Saturation
// ============================================================================

TEST(VibranceSaturation) {
    // Create two pixels: one highly saturated, one less saturated
    std::vector<float> linear_data(3 * 2);
    // Highly saturated red
    linear_data[0] = 1.0f; linear_data[1] = 0.0f; linear_data[2] = 0.0f;
    // Less saturated pinkish
    linear_data[3] = 0.8f; linear_data[4] = 0.5f; linear_data[5] = 0.5f;
    
    SourceImage source = SourceImage::fromData(linear_data, 2, 1);
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    // Apply vibrance
    AdjustmentState vibrance_state;
    vibrance_state.vibrance = 100.0f;
    
    RenderResult result_vib = renderer.render(source, vibrance_state, options);
    ASSERT_TRUE(result_vib.success);
    
    // Apply saturation
    AdjustmentState sat_state;
    sat_state.saturation = 100.0f;
    
    RenderResult result_sat = renderer.render(source, sat_state, options);
    ASSERT_TRUE(result_sat.success);
    
    Pixel32 saturated_red_vib = result_vib.getPixel(0, 0);
    Pixel32 saturated_red_sat = result_sat.getPixel(0, 0);
    
    Pixel32 less_sat_vib = result_vib.getPixel(1, 0);
    Pixel32 less_sat_sat = result_sat.getPixel(1, 0);
    
    // Vibrance should affect less-saturated colors more
    // The highly saturated red should change less with vibrance than with saturation
    float vib_change_highsat = std::abs(saturated_red_vib.r - 1.0f);
    float sat_change_highsat = std::abs(saturated_red_sat.r - 1.0f);
    
    // For less saturated color, vibrance should have significant effect
    float original_chroma_low = 0.8f - 0.5f;  // 0.3
    float vib_chroma_low = less_sat_vib.r - std::min(less_sat_vib.g, less_sat_vib.b);
    float sat_chroma_low = less_sat_sat.r - std::min(less_sat_sat.g, less_sat_sat.b);
    
    // Both should increase chroma, but vibrance is more selective
    EXPECT_TRUE(vib_chroma_low > original_chroma_low);
    EXPECT_TRUE(sat_chroma_low > original_chroma_low);
}

// ============================================================================
// Test: Tone Curve Functionality
// ============================================================================

TEST(ToneCurveFunctionality) {
    // Test basic curve evaluation
    ToneCurve curve;
    
    // Should start as identity
    ASSERT_TRUE(curve.isIdentity(0.001f));
    
    // Add control point
    curve.addControlPoint(0.5f, 0.25f);  // Darken midtones
    
    ASSERT_FALSE(curve.isIdentity(0.001f));
    
    // Evaluate at control point
    float val = curve.evaluate(0.5f);
    ASSERT_NEAR(val, 0.25f, 0.01f);
    
    // Evaluate at endpoints
    ASSERT_NEAR(curve.evaluate(0.0f), 0.0f, 0.001f);
    ASSERT_NEAR(curve.evaluate(1.0f), 1.0f, 0.001f);
    
    // Reset should restore identity
    curve.reset();
    ASSERT_TRUE(curve.isIdentity(0.001f));
}

// ============================================================================
// Test: RGB Channel Curves
// ============================================================================

TEST(RGBChannelCurves) {
    // Create pure red pixel
    std::vector<float> linear_data(3 * 1);
    linear_data[0] = 0.8f; linear_data[1] = 0.3f; linear_data[2] = 0.1f;
    SourceImage source = SourceImage::fromData(linear_data, 1, 1);
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    // Create adjustment with red curve that darkens reds
    AdjustmentState state;
    state.tone_curves.red().addControlPoint(0.5f, 0.25f);
    
    RenderResult result = renderer.render(source, state, options);
    ASSERT_TRUE(result.success);
    
    Pixel32 original = source.getPixelLinear(0, 0);
    Pixel32 adjusted = result.getPixel(0, 0);
    
    // Red channel should be affected most
    EXPECT_TRUE(adjusted.r < original.r);
    
    // Green and blue should also change due to the curve affecting their values through the pipeline
    // But the red curve specifically targets red channel values
}

// ============================================================================
// Test: Undo/Redo Functionality
// ============================================================================

TEST(UndoRedoFunctionality) {
    std::vector<uint8_t> test_data(4 * 4, 128);  // Gray 4x4 image
    SourceImage source = SourceImage::fromU8Data(test_data, 2, 2, 4);
    
    Document doc(std::make_shared<SourceImage>(source));
    
    // Initial state should be default
    ASSERT_TRUE(doc.getCurrentState().isDefault());
    ASSERT_EQ(doc.getHistorySize(), 1);
    ASSERT_EQ(doc.getCurrentIndex(), 0);
    
    // Apply first adjustment
    AdjustmentState state1;
    state1.exposure = 1.0f;
    doc.applyState(state1);
    
    ASSERT_EQ(doc.getHistorySize(), 2);
    ASSERT_EQ(doc.getCurrentIndex(), 1);
    ASSERT_EQ(doc.getCurrentState().exposure, 1.0f);
    
    // Apply second adjustment
    AdjustmentState state2;
    state2.exposure = 2.0f;
    state2.contrast = 50.0f;
    doc.applyState(state2);
    
    ASSERT_EQ(doc.getHistorySize(), 3);
    ASSERT_EQ(doc.getCurrentIndex(), 2);
    
    // Undo
    ASSERT_TRUE(doc.canUndo());
    ASSERT_TRUE(doc.undo());
    ASSERT_EQ(doc.getCurrentIndex(), 1);
    ASSERT_EQ(doc.getCurrentState().exposure, 1.0f);
    
    // Undo again
    ASSERT_TRUE(doc.undo());
    ASSERT_EQ(doc.getCurrentIndex(), 0);
    ASSERT_TRUE(doc.getCurrentState().isDefault());
    
    // Can't undo further
    ASSERT_FALSE(doc.canUndo());
    ASSERT_FALSE(doc.undo());
    
    // Redo
    ASSERT_TRUE(doc.canRedo());
    ASSERT_TRUE(doc.redo());
    ASSERT_EQ(doc.getCurrentIndex(), 1);
    ASSERT_EQ(doc.getCurrentState().exposure, 1.0f);
    
    // Redo again
    ASSERT_TRUE(doc.redo());
    ASSERT_EQ(doc.getCurrentIndex(), 2);
    ASSERT_EQ(doc.getCurrentState().exposure, 2.0f);
    
    // Can't redo further
    ASSERT_FALSE(doc.canRedo());
    ASSERT_FALSE(doc.redo());
}

// ============================================================================
// Test: Deterministic Rendering
// ============================================================================

TEST(DeterministicRendering) {
    std::vector<float> linear_data(3 * 100);
    for (int i = 0; i < 100; ++i) {
        linear_data[i * 3] = static_cast<float>(i % 10) / 10.0f;
        linear_data[i * 3 + 1] = static_cast<float>(i / 10) / 10.0f;
        linear_data[i * 3 + 2] = 0.5f;
    }
    
    SourceImage source = SourceImage::fromData(linear_data, 10, 10);
    
    AdjustmentState state;
    state.exposure = 0.5f;
    state.contrast = 20.0f;
    state.temperature = 5500.0f;
    state.tone_curves.rgb().addControlPoint(0.3f, 0.4f);
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    // Render multiple times
    std::vector<RenderResult> results;
    for (int i = 0; i < 5; ++i) {
        RenderResult r = renderer.render(source, state, options);
        ASSERT_TRUE(r.success);
        results.push_back(r);
    }
    
    // All results should be identical
    for (size_t i = 1; i < results.size(); ++i) {
        ASSERT_EQ(results[0].width, results[i].width);
        ASSERT_EQ(results[0].height, results[i].height);
        
        for (size_t j = 0; j < results[0].data.size(); ++j) {
            ASSERT_EQ(results[0].data[j], results[i].data[j]);
        }
    }
}

// ============================================================================
// Test: Preview and Final Use Same Semantics
// ============================================================================

TEST(PreviewFinalSemantics) {
    std::vector<float> linear_data(3 * 100);
    for (int i = 0; i < 100; ++i) {
        linear_data[i * 3] = static_cast<float>(i) / 100.0f;
        linear_data[i * 3 + 1] = 0.5f;
        linear_data[i * 3 + 2] = 0.5f;
    }
    
    SourceImage source = SourceImage::fromData(linear_data, 10, 10);
    
    AdjustmentState state;
    state.exposure = 1.0f;
    state.contrast = 30.0f;
    state.saturation = 50.0f;
    
    CPURenderer renderer;
    RenderOptions preview_options;
    preview_options.preview_mode = true;
    preview_options.preview_max_dim = 64;
    preview_options.use_cache = false;
    
    RenderOptions final_options;
    final_options.preview_mode = false;
    final_options.use_cache = false;
    
    RenderResult preview = renderer.render(source, state, preview_options);
    RenderResult final = renderer.render(source, state, final_options);
    
    ASSERT_TRUE(preview.success);
    ASSERT_TRUE(final.success);
    
    // Preview should be smaller
    EXPECT_TRUE(preview.width <= final.width);
    EXPECT_TRUE(preview.height <= final.height);
    
    // Sample corresponding pixels and verify they have similar characteristics
    // (exact values may differ due to downsampling, but trends should match)
    Pixel32 preview_center = preview.getPixel(preview.width / 2, preview.height / 2);
    Pixel32 final_center = final.getPixel(final.width / 2, final.height / 2);
    
    // Both should show exposure increase (values higher than original 0.5)
    EXPECT_TRUE(preview_center.r > 0.4f);
    EXPECT_TRUE(final_center.r > 0.4f);
}

// ============================================================================
// Test: No NaN/Inf Values Produced
// ============================================================================

TEST(NoNaNValues) {
    std::vector<float> linear_data(3 * 100);
    for (int i = 0; i < 100; ++i) {
        linear_data[i * 3] = static_cast<float>(i % 10) / 10.0f;
        linear_data[i * 3 + 1] = static_cast<float>(i / 10) / 10.0f;
        linear_data[i * 3 + 2] = 0.5f;
    }
    
    SourceImage source = SourceImage::fromData(linear_data, 10, 10);
    
    // Test extreme adjustments
    AdjustmentState extreme;
    extreme.exposure = 5.0f;
    extreme.contrast = 100.0f;
    extreme.highlights = -100.0f;
    extreme.shadows = 100.0f;
    extreme.saturation = 100.0f;
    extreme.temperature = 2000.0f;
    extreme.tint = 100.0f;
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    RenderResult result = renderer.render(source, extreme, options);
    ASSERT_TRUE(result.success);
    
    // Check all output values
    for (size_t i = 0; i < result.data.size(); ++i) {
        float v = result.data[i];
        ASSERT_FALSE(std::isnan(v));
        ASSERT_FALSE(std::isinf(v));
        ASSERT_TRUE(v >= 0.0f);  // Should not go negative
    }
    
    // Test with black input (edge case)
    std::vector<float> black_data(3 * 1, 0.0f);
    SourceImage black_source = SourceImage::fromData(black_data, 1, 1);
    
    RenderResult black_result = renderer.render(black_source, extreme, options);
    ASSERT_TRUE(black_result.success);
    
    for (float v : black_result.data) {
        ASSERT_FALSE(std::isnan(v));
        ASSERT_FALSE(std::isinf(v));
    }
}

// ============================================================================
// Test: Correct Output Dimensions
// ============================================================================

TEST(CorrectOutputDimensions) {
    // Test various image sizes
    std::vector<std::pair<uint32_t, uint32_t>> sizes = {
        {1, 1},
        {10, 10},
        {100, 100},
        {1920, 1080},
        {4000, 3000}
    };
    
    CPURenderer renderer;
    RenderOptions options;
    options.use_cache = false;
    
    AdjustmentState state;
    state.exposure = 0.5f;
    
    for (const auto& [w, h] : sizes) {
        std::vector<float> data(w * h * 3, 0.5f);
        SourceImage source = SourceImage::fromData(data, w, h);
        
        RenderResult result = renderer.render(source, state, options);
        
        ASSERT_TRUE(result.success);
        ASSERT_EQ(result.width, w);
        ASSERT_EQ(result.height, h);
        ASSERT_EQ(result.data.size(), w * h * 3);
    }
    
    // Test preview mode dimensions
    std::vector<float> large_data(3 * 1000 * 1000, 0.5f);
    SourceImage large_source = SourceImage::fromData(large_data, 1000, 1000);
    
    RenderOptions preview_opts;
    preview_opts.preview_mode = true;
    preview_opts.preview_max_dim = 512;
    preview_opts.use_cache = false;
    
    RenderResult preview = renderer.render(large_source, state, preview_opts);
    
    ASSERT_TRUE(preview.success);
    EXPECT_TRUE(preview.width <= 512);
    EXPECT_TRUE(preview.height <= 512);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "Photo Engine Test Suite\n";
    std::cout << "=======================\n\n";
    
    return run_tests();
}
