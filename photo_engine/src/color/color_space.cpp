#include "color/color_space.hpp"
#include <cmath>
#include <algorithm>

namespace pe {

// McCamy's formula and related approximations for Planckian locus
ChromaticAdaptation::WhitePoint 
ChromaticAdaptation::getWhitePointForTemperature(float kelvin) {
    kelvin = std::clamp(kelvin, 1000.0f, 50000.0f);
    
    float x, y;
    
    // Use Kim et al. approximation for better accuracy
    if (kelvin <= 6600.0f) {
        x = -0.2661239f * (1e9f / std::pow(kelvin, 3)) 
            - 0.2343589f * (1e6f / (kelvin * kelvin)) 
            + 0.8776956f * (1000.0f / kelvin) 
            + 0.179910f;
    } else {
        x = -3.0258469f * (1e9f / std::pow(kelvin, 3)) 
            + 2.1070379f * (1e6f / (kelvin * kelvin)) 
            + 0.2226347f * (1000.0f / kelvin) 
            + 0.240390f;
    }
    
    if (kelvin <= 2222.0f) {
        y = -1.2368923f * x * x * x + 3.1796539f * x * x - 2.8799668f * x + 1.4103195f;
    } else if (kelvin <= 4000.0f) {
        y = -2.8004247f * x * x * x + 6.7321977f * x * x - 5.4819878f * x + 2.1111747f;
    } else {
        y = 0.0000029f * x * x * x - 0.0001274f * x * x + 0.0178984f * x + 0.2685703f;
    }
    
    // Convert xyY to XYZ (Y = 1.0)
    WhitePoint wp;
    wp.Y = 1.0f;
    wp.X = wp.Y * x / y;
    wp.Z = wp.Y * (1.0f - x - y) / y;
    
    return wp;
}

void ChromaticAdaptation::buildBradfordMatrix(const WhitePoint& srcWP, 
                                               const WhitePoint& dstWP, 
                                               float matrix[3][3]) {
    // Bradford cone response transformation
    constexpr float bradford[3][3] = {
        { 0.8951f,  0.2664f, -0.1614f},
        {-0.0172f,  0.6659f,  0.0313f},
        { 0.0000f,  0.0119f,  0.9881f}
    };
    
    constexpr float bradford_inv[3][3] = {
        { 1.096124f, -0.439574f,  0.163448f},
        {-0.028073f,  1.507227f, -0.029154f},
        {-0.000000f,  0.000000f,  1.000000f}
    };
    
    // Convert white points to cone response domain
    float src_cone[3], dst_cone[3];
    
    src_cone[0] = bradford[0][0] * srcWP.X + bradford[0][1] * srcWP.Y + bradford[0][2] * srcWP.Z;
    src_cone[1] = bradford[1][0] * srcWP.X + bradford[1][1] * srcWP.Y + bradford[1][2] * srcWP.Z;
    src_cone[2] = bradford[2][0] * srcWP.X + bradford[2][1] * srcWP.Y + bradford[2][2] * srcWP.Z;
    
    dst_cone[0] = bradford[0][0] * dstWP.X + bradford[0][1] * dstWP.Y + bradford[0][2] * dstWP.Z;
    dst_cone[1] = bradford[1][0] * dstWP.X + bradford[1][1] * dstWP.Y + bradford[1][2] * dstWP.Z;
    dst_cone[2] = bradford[2][0] * dstWP.X + bradford[2][1] * dstWP.Y + bradford[2][2] * dstWP.Z;
    
    // Build diagonal adaptation matrix in cone domain
    float D[3];
    for (int i = 0; i < 3; ++i) {
        D[i] = (std::abs(src_cone[i]) > 1e-6f) ? (dst_cone[i] / src_cone[i]) : 1.0f;
    }
    
    // M = B^-1 * D * B
    float DB[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            DB[i][j] = D[i] * bradford[i][j];
        }
    }
    
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            matrix[i][j] = 0;
            for (int k = 0; k < 3; ++k) {
                matrix[i][j] += bradford_inv[i][k] * DB[k][j];
            }
        }
    }
}

void ChromaticAdaptation::adaptRGB(float& r, float& g, float& b, 
                                    float srcKelvin, float dstKelvin) {
    if (std::abs(srcKelvin - dstKelvin) < 1.0f) return;
    
    WhitePoint srcWP = getWhitePointForTemperature(srcKelvin);
    WhitePoint dstWP = getWhitePointForTemperature(dstKelvin);
    
    float adapt_matrix[3][3];
    buildBradfordMatrix(srcWP, dstWP, adapt_matrix);
    
    // Apply adaptation matrix to linear RGB
    float new_r = adapt_matrix[0][0] * r + adapt_matrix[0][1] * g + adapt_matrix[0][2] * b;
    float new_g = adapt_matrix[1][0] * r + adapt_matrix[1][1] * g + adapt_matrix[1][2] * b;
    float new_b = adapt_matrix[2][0] * r + adapt_matrix[2][1] * g + adapt_matrix[2][2] * b;
    
    r = new_r;
    g = new_g;
    b = new_b;
}

} // namespace pe
