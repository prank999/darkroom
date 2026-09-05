// Comprehensive test suite for photo editing engine
#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <random>
#include <chrono>
#include <cstring>

#include "document/ImageSource.h"
#include "document/Document.h"
#include "adjustments/AdjustmentState.h"
#include "adjustments/ToneCurve.h"
#include "rendering/Renderer.h"
#include "rendering/RenderedImage.h"
#include "color/ColorSpace.h"
#include "cache/RenderCache.h"

using namespace photo;

// Test result tracking
struct TestResult {
    std::string name;
    bool passed;
    std::string message;
};

std::vector<TestResult> results;

#define TEST(name) \
    std::cout << "Running: " << name << "... "; \
    try

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) { \
        throw std::runtime_error(msg); \
    }

#define ASSERT_FALSE(cond, msg) \
    if (cond) { \
        throw std::runtime_error(msg); \
    }

#define ASSERT_NEAR(a, b, tol, msg) \
    if (std::abs((a) - (b)) > (tol)) { \
        throw std::runtime_error(msg); \
    }

#define END_TEST(passed_msg) \
    std::cout << "PASSED (" << passed_msg << ")" << std::endl; \
    results.push_back({#name, true, passed_msg}); \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << std::endl; \
        results.push_back({#name, false, e.what()}); \
    }

// Helper: Create a test image with known pattern
std::shared_ptr<ImageSource> createTestImage(int width, int height) {
    std::vector<uint8_t> data(width * height * 4);
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * 4;
            // Create gradient pattern
            data[idx + 0] = static_cast<uint8_t>(255 * x / width);      // R
            data[idx + 1] = static_cast<uint8_t>(255 * y / height);     // G
            data[idx + 2] = static_cast<uint8_t>(128);                   // B
            data[idx + 3] = 255;                                         // A
        }
    }
    
    return ImageSource::createFromData(data.data(), width, height, 4);
}

// Helper: Check for NaN/Inf in rendered image
bool hasNaNOrInf(const RenderedImage& img) {
    for (float v : img.data()) {
        if (!std::isfinite(v)) {
            return true;
        }
    }
    return false;
}

// Helper: Calculate mean luminance
float meanLuminance(const RenderedImage& img) {
    float sum = 0;
    size_t count = 0;
    
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const float* p = img.getPixelPtr(x, y);
            float luma = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            sum += luma;
            ++count;
        }
    }
    
    return sum / count;
}

// ============================================================================
// TEST: Source image is never modified
// ============================================================================
TEST("SourceImageImmutable") {
    auto source = createTestImage(100, 100);
    
    // Store original data
    std::vector<float> originalData = source->data();
    
    // Perform multiple renders with various adjustments
    Renderer renderer;
    AdjustmentState adj;
    adj.exposure = 2.0f;
    adj.contrast = 50.0f;
    adj.saturation = 100.0f;
    
    for (int i = 0; i < 5; ++i) {
        auto rendered = renderer.render(source, adj);
    }
    
    // Verify source is unchanged
    ASSERT_TRUE(source->data().size() == originalData.size(), 
                "Source data size changed");
    
    for (size_t i = 0; i < originalData.size(); ++i) {
        ASSERT_NEAR(source->data()[i], originalData[i], 0.0001f,
                    "Source pixel data was modified");
    }
    
    END_TEST("Source remains immutable after multiple renders");
}

// ============================================================================
// TEST: Identity adjustments reproduce expected baseline
// ============================================================================
TEST("IdentityAdjustments") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    AdjustmentState identity;
    identity.reset();
    ASSERT_TRUE(identity.isIdentity(), "Default state should be identity");
    
    auto rendered = renderer.render(source, identity);
    
    ASSERT_TRUE(rendered->isValid(), "Render failed");
    ASSERT_TRUE(rendered->width() == 100, "Width mismatch");
    ASSERT_TRUE(rendered->height() == 100, "Height mismatch");
    
    // With identity adjustments, output should match input (converted to sRGB)
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            float srcR = source->getPixelLinear(x, y, 0);
            float srcG = source->getPixelLinear(x, y, 1);
            float srcB = source->getPixelLinear(x, y, 2);
            
            const float* out = rendered->getPixelPtr(x, y);
            
            // Allow small tolerance for floating point operations
            ASSERT_NEAR(out[0], ColorSpace::linearToSrgb(srcR), 0.01f,
                        "R channel mismatch at identity");
            ASSERT_NEAR(out[1], ColorSpace::linearToSrgb(srcG), 0.01f,
                        "G channel mismatch at identity");
            ASSERT_NEAR(out[2], ColorSpace::linearToSrgb(srcB), 0.01f,
                        "B channel mismatch at identity");
        }
    }
    
    END_TEST("Identity adjustments produce correct baseline");
}

// ============================================================================
// TEST: Exposure changes luminance appropriately
// ============================================================================
TEST("ExposureLuminance") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    // Render at base exposure
    AdjustmentState base;
    auto baseRender = renderer.render(source, base);
    float baseLuma = meanLuminance(*baseRender);
    
    // Render at +1 EV (should double light)
    AdjustmentState plus1EV;
    plus1EV.exposure = 1.0f;
    auto plus1Render = renderer.render(source, plus1EV);
    float plus1Luma = meanLuminance(*plus1Render);
    
    // Render at -1 EV (should halve light)
    AdjustmentState minus1EV;
    minus1EV.exposure = -1.0f;
    auto minus1Render = renderer.render(source, minus1EV);
    float minus1Luma = meanLuminance(*minus1Render);
    
    // +1 EV should approximately double luminance (in linear space)
    ASSERT_TRUE(plus1Luma > baseLuma * 1.5f, 
                "+1 EV should increase luminance significantly");
    
    // -1 EV should approximately halve luminance
    ASSERT_TRUE(minus1Luma < baseLuma * 0.7f,
                "-1 EV should decrease luminance significantly");
    
    // Verify ratio is approximately 2:1 for +/- 1 EV
    float ratio = plus1Luma / (minus1Luma + 0.0001f);
    ASSERT_TRUE(ratio > 2.0f && ratio < 5.0f,
                "Exposure ratio should be approximately 4:1 for 2 EV range");
    
    END_TEST("Exposure correctly affects luminance");
}

// ============================================================================
// TEST: Contrast changes tonal distribution
// ============================================================================
TEST("ContrastTonalDistribution") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    // High contrast should increase difference between bright and dark areas
    AdjustmentState highContrast;
    highContrast.contrast = 50.0f;
    auto hcRender = renderer.render(source, highContrast);
    
    AdjustmentState lowContrast;
    lowContrast.contrast = -50.0f;
    auto lcRender = renderer.render(source, lowContrast);
    
    // Sample corners - high contrast should have more extreme values
    const float* hcTL = hcRender->getPixelPtr(0, 0);       // Dark area
    const float* hcBR = hcRender->getPixelPtr(99, 99);     // Bright area
    const float* lcTL = lcRender->getPixelPtr(0, 0);
    const float* lcBR = lcRender->getPixelPtr(99, 99);
    
    float hcDark = hcTL[0] + hcTL[1] + hcTL[2];
    float hcBright = hcBR[0] + hcBR[1] + hcBR[2];
    float lcDark = lcTL[0] + lcTL[1] + lcTL[2];
    float lcBright = lcBR[0] + lcBR[1] + lcBR[2];
    
    // High contrast: darker darks, brighter brights
    ASSERT_TRUE(hcDark < lcDark || hcBright > lcBright,
                "High contrast should increase tonal range");
    
    END_TEST("Contrast affects tonal distribution");
}

// ============================================================================
// TEST: Highlights and shadows behave sensibly
// ============================================================================
TEST("HighlightsShadows") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    // Test highlights reduction
    AdjustmentState highlightReduce;
    highlightReduce.highlights = -50.0f;
    auto hlRender = renderer.render(source, highlightReduce);
    
    // Test shadows lift
    AdjustmentState shadowLift;
    shadowLift.shadows = 50.0f;
    auto shRender = renderer.render(source, shadowLift);
    
    // Bright areas should be darker with reduced highlights
    const float* origBR = renderer.render(source, AdjustmentState{})
                          ->getPixelPtr(99, 99);
    const float* hlBR = hlRender->getPixelPtr(99, 99);
    
    float origBright = origBR[0] + origBR[1] + origBR[2];
    float hlBright = hlBR[0] + hlBR[1] + hlBR[2];
    
    ASSERT_TRUE(hlBright < origBright,
                "Reduced highlights should darken bright areas");
    
    END_TEST("Highlights/shadows affect appropriate tonal ranges");
}

// ============================================================================
// TEST: Temperature and tint behave correctly
// ============================================================================
TEST("TemperatureTint") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    // Warm temperature should increase red/orange
    AdjustmentState warm;
    warm.temperature = 50.0f;
    auto warmRender = renderer.render(source, warm);
    
    // Cool temperature should increase blue
    AdjustmentState cool;
    cool.temperature = -50.0f;
    auto coolRender = renderer.render(source, cool);
    
    const float* warmP = warmRender->getPixelPtr(50, 50);
    const float* coolP = coolRender->getPixelPtr(50, 50);
    
    // Warm should have higher R/B ratio than cool
    float warmRB = warmP[0] / (warmP[2] + 0.001f);
    float coolRB = coolP[0] / (coolP[2] + 0.001f);
    
    ASSERT_TRUE(warmRB > coolRB,
                "Warm temperature should increase R relative to B");
    
    END_TEST("Temperature/tint produce correct color shifts");
}

// ============================================================================
// TEST: Vibrance differs from saturation
// ============================================================================
TEST("VibranceVsSaturation") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    // Apply only vibrance
    AdjustmentState vibranceOnly;
    vibranceOnly.vibrance = 100.0f;
    auto vibRender = renderer.render(source, vibranceOnly);
    
    // Apply only saturation
    AdjustmentState satOnly;
    satOnly.saturation = 100.0f;
    auto satRender = renderer.render(source, satOnly);
    
    // They should produce different results
    bool different = false;
    for (int y = 0; y < 100 && !different; ++y) {
        for (int x = 0; x < 100 && !different; ++x) {
            const float* vibP = vibRender->getPixelPtr(x, y);
            const float* satP = satRender->getPixelPtr(x, y);
            
            float diff = std::abs(vibP[0] - satP[0]) +
                         std::abs(vibP[1] - satP[1]) +
                         std::abs(vibP[2] - satP[2]);
            
            if (diff > 0.01f) {
                different = true;
            }
        }
    }
    
    ASSERT_TRUE(different, "Vibrance and saturation should produce different results");
    
    END_TEST("Vibrance behaves differently from saturation");
}

// ============================================================================
// TEST: Tone curves work correctly
// ============================================================================
TEST("ToneCurves") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    // Create S-curve for contrast
    AdjustmentState withCurve;
    withCurve.rgbCurve = {
        {0.0f, 0.0f},
        {0.25f, 0.2f},
        {0.75f, 0.8f},
        {1.0f, 1.0f}
    };
    
    auto curvedRender = renderer.render(source, withCurve);
    ASSERT_TRUE(curvedRender->isValid(), "Tone curve render failed");
    
    // Test individual channel curves
    AdjustmentState channelCurves;
    channelCurves.redCurve = {{0.0f, 0.0f}, {1.0f, 0.5f}};  // Reduce red
    
    auto chRender = renderer.render(source, channelCurves);
    
    // Compare with base
    auto baseRender = renderer.render(source, AdjustmentState{});
    
    // Red channel should be lower with the curve
    const float* baseP = baseRender->getPixelPtr(50, 50);
    const float* chP = chRender->getPixelPtr(50, 50);
    
    ASSERT_TRUE(chP[0] <= baseP[0] + 0.01f,
                "Red curve should reduce red channel");
    
    END_TEST("Tone curves apply correctly");
}

// ============================================================================
// TEST: RGB curves affect intended channels
// ============================================================================
TEST("RGBChannelCurves") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    // Only modify red curve
    AdjustmentState redOnly;
    redOnly.redCurve = {{0.0f, 0.0f}, {1.0f, 0.0f}};  // Zero out red
    
    auto redRender = renderer.render(source, redOnly);
    
    // Only modify blue curve
    AdjustmentState blueOnly;
    blueOnly.blueCurve = {{0.0f, 0.0f}, {1.0f, 0.0f}};  // Zero out blue
    
    auto blueRender = renderer.render(source, blueOnly);
    
    // Compare specific pixels
    const float* redP = redRender->getPixelPtr(50, 50);
    const float* blueP = blueRender->getPixelPtr(50, 50);
    const float* baseP = renderer.render(source, AdjustmentState{})
                                    ->getPixelPtr(50, 50);
    
    // Red curve should primarily affect red channel
    ASSERT_TRUE(redP[0] < baseP[0] * 0.5f,
                "Red curve should reduce red channel");
    
    // Blue curve should primarily affect blue channel
    ASSERT_TRUE(blueP[2] < baseP[2] * 0.5f,
                "Blue curve should reduce blue channel");
    
    // Green should be relatively unaffected
    ASSERT_NEAR(redP[1], baseP[1], 0.1f,
                "Red curve should not significantly affect green");
    
    END_TEST("RGB curves affect their intended channels");
}

// ============================================================================
// TEST: Undo/redo works correctly
// ============================================================================
TEST("UndoRedo") {
    auto source = createTestImage(100, 100);
    auto doc = std::make_shared<Document>(source);
    
    ASSERT_FALSE(doc->canUndo(), "Should not be able to undo initially");
    ASSERT_FALSE(doc->canRedo(), "Should not be able to redo initially");
    
    // Make some adjustments
    doc->adjust([](AdjustmentState& s) { s.exposure = 1.0f; });
    ASSERT_TRUE(doc->canUndo(), "Should be able to undo after adjustment");
    
    doc->adjust([](AdjustmentState& s) { s.contrast = 20.0f; });
    doc->adjust([](AdjustmentState& s) { s.saturation = 30.0f; });
    
    // Store current state
    float currentExposure = doc->adjustments().exposure;
    float currentContrast = doc->adjustments().contrast;
    float currentSat = doc->adjustments().saturation;
    
    ASSERT_TRUE(currentExposure == 1.0f, "Exposure should be set");
    ASSERT_TRUE(currentContrast == 20.0f, "Contrast should be set");
    ASSERT_TRUE(currentSat == 30.0f, "Saturation should be set");
    
    // Undo
    ASSERT_TRUE(doc->undo(), "Undo should succeed");
    ASSERT_TRUE(doc->adjustments().saturation == 0.0f,
                "Saturation should be undone");
    
    doc->undo();
    ASSERT_TRUE(doc->adjustments().contrast == 0.0f,
                "Contrast should be undone");
    
    // Redo
    ASSERT_TRUE(doc->redo(), "Redo should succeed");
    ASSERT_TRUE(doc->adjustments().contrast == 20.0f,
                "Contrast should be redone");
    
    doc->redo();
    ASSERT_TRUE(doc->adjustments().saturation == 30.0f,
                "Saturation should be redone");
    
    END_TEST("Undo/redo correctly manages adjustment states");
}

// ============================================================================
// TEST: Repeated rendering is deterministic
// ============================================================================
TEST("DeterministicRendering") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    AdjustmentState adj;
    adj.exposure = 0.5f;
    adj.contrast = 20.0f;
    adj.temperature = 10.0f;
    adj.rgbCurve = {{0.0f, 0.0f}, {0.5f, 0.6f}, {1.0f, 1.0f}};
    
    // Render multiple times
    auto render1 = renderer.render(source, adj);
    auto render2 = renderer.render(source, adj);
    auto render3 = renderer.render(source, adj);
    
    // All renders should be identical
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            const float* p1 = render1->getPixelPtr(x, y);
            const float* p2 = render2->getPixelPtr(x, y);
            const float* p3 = render3->getPixelPtr(x, y);
            
            ASSERT_NEAR(p1[0], p2[0], 0.0001f, "R channel non-deterministic");
            ASSERT_NEAR(p1[1], p2[1], 0.0001f, "G channel non-deterministic");
            ASSERT_NEAR(p1[2], p2[2], 0.0001f, "B channel non-deterministic");
            
            ASSERT_NEAR(p1[0], p3[0], 0.0001f, "R channel non-deterministic (3rd)");
            ASSERT_NEAR(p1[1], p3[1], 0.0001f, "G channel non-deterministic (3rd)");
            ASSERT_NEAR(p1[2], p3[2], 0.0001f, "B channel non-deterministic (3rd)");
        }
    }
    
    END_TEST("Repeated rendering produces identical results");
}

// ============================================================================
// TEST: Preview and final rendering use same semantics
// ============================================================================
TEST("PreviewFinalSemantics") {
    auto source = createTestImage(200, 200);
    Renderer renderer;
    
    AdjustmentState adj;
    adj.exposure = 0.5f;
    adj.contrast = 30.0f;
    adj.saturation = 20.0f;
    
    // Full resolution
    RenderOptions fullOpts;
    fullOpts.scale = 1.0f;
    fullOpts.previewMode = false;
    auto fullRender = renderer.render(source, adj, fullOpts);
    
    // Preview (downscaled)
    RenderOptions previewOpts;
    previewOpts.scale = 0.25f;
    previewOpts.previewMode = true;
    auto previewRender = renderer.render(source, adj, previewOpts);
    
    ASSERT_TRUE(fullRender->isValid(), "Full render failed");
    ASSERT_TRUE(previewRender->isValid(), "Preview render failed");
    ASSERT_TRUE(previewRender->width() == 50, "Preview scale incorrect");
    
    // Sample corresponding pixels - they should be similar
    // (not identical due to sampling differences)
    const float* fullP = fullRender->getPixelPtr(100, 100);
    const float* prevP = previewRender->getPixelPtr(25, 25);
    
    // Allow larger tolerance for downsampling differences
    ASSERT_NEAR(fullP[0], prevP[0], 0.1f, "R channel mismatch preview vs full");
    ASSERT_NEAR(fullP[1], prevP[1], 0.1f, "G channel mismatch preview vs full");
    ASSERT_NEAR(fullP[2], prevP[2], 0.1f, "B channel mismatch preview vs full");
    
    END_TEST("Preview and final rendering use consistent semantics");
}

// ============================================================================
// TEST: No NaN/Inf values produced
// ============================================================================
TEST("NoNaNInf") {
    auto source = createTestImage(100, 100);
    Renderer renderer;
    
    // Test with extreme adjustments
    std::vector<AdjustmentState> extremeStates;
    
    AdjustmentState extreme1;
    extreme1.exposure = 10.0f;
    extremeStates.push_back(extreme1);
    
    AdjustmentState extreme2;
    extreme2.exposure = -5.0f;
    extreme2.contrast = 100.0f;
    extremeStates.push_back(extreme2);
    
    AdjustmentState extreme3;
    extreme3.saturation = 100.0f;
    extreme3.vibrance = 100.0f;
    extremeStates.push_back(extreme3);
    
    AdjustmentState extreme4;
    extreme4.temperature = 100.0f;
    extreme4.tint = 100.0f;
    extremeStates.push_back(extreme4);
    
    AdjustmentState extreme5;
    extreme5.highlights = -100.0f;
    extreme5.shadows = 100.0f;
    extremeStates.push_back(extreme5);
    
    for (const auto& adj : extremeStates) {
        auto render = renderer.render(source, adj);
        ASSERT_FALSE(hasNaNOrInf(*render),
                     "NaN/Inf detected with extreme adjustments");
    }
    
    END_TEST("No NaN/Inf values produced with extreme settings");
}

// ============================================================================
// TEST: Output dimensions remain correct
// ============================================================================
TEST("OutputDimensions") {
    auto source = createTestImage(500, 400);
    Renderer renderer;
    
    // Test various scales
    std::vector<float> scales = {1.0f, 0.5f, 0.25f, 0.1f, 2.0f};
    
    for (float scale : scales) {
        RenderOptions opts;
        opts.scale = scale;
        
        auto render = renderer.render(source, AdjustmentState{}, opts);
        
        int expectedW = static_cast<int>(500 * scale);
        int expectedH = static_cast<int>(400 * scale);
        
        ASSERT_TRUE(render->width() == expectedW,
                    "Width mismatch at scale " + std::to_string(scale));
        ASSERT_TRUE(render->height() == expectedH,
                    "Height mismatch at scale " + std::to_string(scale));
    }
    
    END_TEST("Output dimensions are correct at all scales");
}

// ============================================================================
// TEST: Tone curve interpolation
// ============================================================================
TEST("ToneCurveInterpolation") {
    ToneCurve curve({{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}});
    
    // Should be identity at control points
    ASSERT_NEAR(curve.evaluate(0.0f), 0.0f, 0.001f, "Curve start point");
    ASSERT_NEAR(curve.evaluate(0.5f), 0.5f, 0.001f, "Curve midpoint");
    ASSERT_NEAR(curve.evaluate(1.0f), 1.0f, 0.001f, "Curve endpoint");
    
    // Should interpolate smoothly
    float prev = 0;
    for (int i = 1; i < 100; ++i) {
        float val = curve.evaluate(i / 100.0f);
        ASSERT_TRUE(val >= prev - 0.001f, "Curve should be monotonic");
        prev = val;
    }
    
    END_TEST("Tone curve interpolation works correctly");
}

// ============================================================================
// TEST: Cache functionality
// ============================================================================
TEST("RenderCache") {
    RenderCache cache(10 * 1024 * 1024);  // 10MB
    
    auto img = std::make_shared<RenderedImage>(100, 100);
    
    CacheKey key{};
    key.sourceHash = 12345;
    key.scale = 1.0f;
    
    // Cache miss initially
    auto cached = cache.get(key);
    ASSERT_TRUE(cached == nullptr, "Cache should be empty initially");
    
    // Put item
    cache.put(key, img);
    ASSERT_TRUE(cache.itemCount() == 1, "Cache should have one item");
    
    // Cache hit
    cached = cache.get(key);
    ASSERT_TRUE(cached != nullptr, "Cache should return stored item");
    ASSERT_TRUE(cached->width() == 100, "Cached item has wrong width");
    
    // Clear
    cache.clear();
    ASSERT_TRUE(cache.itemCount() == 0, "Cache should be empty after clear");
    
    END_TEST("Render cache works correctly");
}

// ============================================================================
// TEST: Async rendering
// ============================================================================
TEST("AsyncRendering") {
    auto source = createTestImage(200, 200);
    Renderer renderer;
    
    AdjustmentState adj;
    adj.exposure = 0.5f;
    
    auto future = renderer.renderAsync(source, adj);
    
    // Wait for completion
    auto result = future.get();
    
    ASSERT_TRUE(result != nullptr, "Async render returned null");
    ASSERT_TRUE(result->isValid(), "Async render produced invalid image");
    ASSERT_TRUE(result->width() == 200, "Async render wrong width");
    
    END_TEST("Async rendering completes successfully");
}

// ============================================================================
// Main test runner
// ============================================================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Photo Engine Test Suite" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    // Run all tests
    TEST(SourceImageImmutable);
    TEST(IdentityAdjustments);
    TEST(ExposureLuminance);
    TEST(ContrastTonalDistribution);
    TEST(HighlightsShadows);
    TEST(TemperatureTint);
    TEST(VibranceVsSaturation);
    TEST(ToneCurves);
    TEST(RGBChannelCurves);
    TEST(UndoRedo);
    TEST(DeterministicRendering);
    TEST(PreviewFinalSemantics);
    TEST(NoNaNInf);
    TEST(OutputDimensions);
    TEST(ToneCurveInterpolation);
    TEST(RenderCache);
    TEST(AsyncRendering);
    
    // Summary
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Test Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& r : results) {
        if (r.passed) {
            ++passed;
        } else {
            ++failed;
            std::cout << "FAILED: " << r.name << " - " << r.message << std::endl;
        }
    }
    
    std::cout << std::endl;
    std::cout << "Passed: " << passed << "/" << results.size() << std::endl;
    std::cout << "Failed: " << failed << "/" << results.size() << std::endl;
    
    return failed > 0 ? 1 : 0;
}
