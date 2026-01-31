/*
 * Tests for Highway-optimized color conversion functions
 * Compares Highway SIMD implementations against scalar versions
 */

#include <gtest/gtest.h>
#include <cmath>
#include <iostream>
#include <vector>
#include "jpgd_color_highway.h"

namespace jpgd {

// ============================================================================
// Helper Functions: Scalar Reference Implementations
// ============================================================================

// Scalar implementation for H1V1 RGB passthrough
void ConvertH1V1RGB_Scalar(uint8_t* dst, const uint8_t* sample_buf, int row,
                           int max_mcus_per_row) {
  uint8_t* d = dst;
  const uint8_t* s = sample_buf + row * 8;

  for (int mcu = 0; mcu < max_mcus_per_row; mcu++) {
    for (int j = 0; j < 8; j++) {
      d[0] = s[j];        // Y -> R
      d[1] = s[64 + j];   // Cb -> G
      d[2] = s[128 + j];  // Cr -> B
      d[3] = 255;
      d += 4;
    }
    s += 64 * 3;
  }
}

// Scalar implementation for H1V1 YCbCr to RGB
void ConvertH1V1YCbCr_Scalar(uint8_t* dst, const uint8_t* sample_buf, int row,
                             int max_mcus_per_row, const int* crr,
                             const int* cbb, const int* crg, const int* cbg) {
  uint8_t* d = dst;
  const uint8_t* s = sample_buf + row * 8;

  auto clamp = [](int val) -> uint8_t {
    return (val < 0) ? 0 : ((val > 255) ? 255 : static_cast<uint8_t>(val));
  };

  for (int mcu = 0; mcu < max_mcus_per_row; mcu++) {
    for (int j = 0; j < 8; j++) {
      int y = s[j];
      int cb = s[64 + j];
      int cr = s[128 + j];

      d[0] = clamp(y + crr[cr]);
      d[1] = clamp(y + ((crg[cr] + cbg[cb]) >> 16));
      d[2] = clamp(y + cbb[cb]);
      d[3] = 255;
      d += 4;
    }
    s += 64 * 3;
  }
}

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

class ColorConversionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize color conversion lookup tables (typical JPEG values)
    for (int i = 0; i <= 255; i++) {
      int k = i - 128;
      crr[i] = (int)((1.40200f * k * (1 << 16)) + 0.5f) >> 16;
      cbb[i] = (int)((1.77200f * k * (1 << 16)) + 0.5f) >> 16;
      crg[i] = (int)((-0.71414f * k * (1 << 16)) + 0.5f);
      cbg[i] = (int)((-0.34414f * k * (1 << 16)) + 0.5f);
    }
  }

  // Create test sample buffer with known pattern
  std::vector<uint8_t> CreateTestSampleBuffer(int num_mcus, uint8_t pattern) {
    std::vector<uint8_t> buffer(num_mcus * 64 * 3);
    for (size_t i = 0; i < buffer.size(); i++) {
      buffer[i] = static_cast<uint8_t>((i + pattern) & 0xFF);
    }
    return buffer;
  }

  int crr[256], cbb[256], crg[256], cbg[256];
};

// ============================================================================
// Test Cases: H1V1 RGB Passthrough
// ============================================================================

TEST_F(ColorConversionTest, H1V1RGB_SingleMCU) {
  const int num_mcus = 1;
  auto sample_buf = CreateTestSampleBuffer(num_mcus, 42);

  std::vector<uint8_t> output_scalar(num_mcus * 8 * 4);
  std::vector<uint8_t> output_highway(num_mcus * 8 * 4);

  // Test row 0
  ConvertH1V1RGB_Scalar(output_scalar.data(), sample_buf.data(), 0, num_mcus);
  ConvertH1V1RGB_HighwayDispatch(output_highway.data(), sample_buf.data(), 0,
                                 num_mcus);

  // Compare results
  for (size_t i = 0; i < output_scalar.size(); i++) {
    EXPECT_EQ(output_scalar[i], output_highway[i])
        << "Mismatch at pixel component " << i << " (pixel " << i / 4
        << ", component " << i % 4 << ")";
  }
}

TEST_F(ColorConversionTest, H1V1RGB_MultipleMCUs) {
  const int num_mcus = 10;
  auto sample_buf = CreateTestSampleBuffer(num_mcus, 123);

  std::vector<uint8_t> output_scalar(num_mcus * 8 * 4);
  std::vector<uint8_t> output_highway(num_mcus * 8 * 4);

  // Test multiple rows
  for (int row = 0; row < 8; row++) {
    ConvertH1V1RGB_Scalar(output_scalar.data(), sample_buf.data(), row,
                          num_mcus);
    ConvertH1V1RGB_HighwayDispatch(output_highway.data(), sample_buf.data(),
                                   row, num_mcus);

    for (size_t i = 0; i < output_scalar.size(); i++) {
      EXPECT_EQ(output_scalar[i], output_highway[i])
          << "Mismatch at row " << row << ", pixel component " << i;
    }
  }
}

// ============================================================================
// Test Cases: H1V1 YCbCr to RGB
// ============================================================================

TEST_F(ColorConversionTest, H1V1YCbCr_SingleMCU) {
  const int num_mcus = 1;
  auto sample_buf = CreateTestSampleBuffer(num_mcus, 77);

  std::vector<uint8_t> output_scalar(num_mcus * 8 * 4);
  std::vector<uint8_t> output_highway(num_mcus * 8 * 4);

  // Test row 0
  ConvertH1V1YCbCr_Scalar(output_scalar.data(), sample_buf.data(), 0, num_mcus,
                          crr, cbb, crg, cbg);
  ConvertH1V1YCbCr_HighwayDispatch(output_highway.data(), sample_buf.data(), 0,
                                   num_mcus, crr, cbb, crg, cbg);

  // Compare results (allow small differences due to rounding)
  int max_diff = 0;
  for (size_t i = 0; i < output_scalar.size(); i++) {
    int diff = std::abs(static_cast<int>(output_scalar[i]) -
                        static_cast<int>(output_highway[i]));
    max_diff = std::max(max_diff, diff);

    EXPECT_LE(diff, 1) << "Mismatch at pixel component " << i << " (pixel "
                       << i / 4 << ", component " << i % 4 << ")"
                       << " scalar=" << static_cast<int>(output_scalar[i])
                       << " highway=" << static_cast<int>(output_highway[i]);
  }

  std::cout << "Max difference: " << max_diff << " (should be 0 or 1)\n";
}

TEST_F(ColorConversionTest, H1V1YCbCr_MultipleMCUs) {
  const int num_mcus = 10;
  auto sample_buf = CreateTestSampleBuffer(num_mcus, 200);

  std::vector<uint8_t> output_scalar(num_mcus * 8 * 4);
  std::vector<uint8_t> output_highway(num_mcus * 8 * 4);

  // Test multiple rows
  for (int row = 0; row < 8; row++) {
    ConvertH1V1YCbCr_Scalar(output_scalar.data(), sample_buf.data(), row,
                            num_mcus, crr, cbb, crg, cbg);
    ConvertH1V1YCbCr_HighwayDispatch(output_highway.data(), sample_buf.data(),
                                     row, num_mcus, crr, cbb, crg, cbg);

    for (size_t i = 0; i < output_scalar.size(); i++) {
      int diff = std::abs(static_cast<int>(output_scalar[i]) -
                          static_cast<int>(output_highway[i]));
      EXPECT_LE(diff, 1) << "Mismatch at row " << row << ", pixel component "
                         << i;
    }
  }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ColorConversionTest, EdgeCases_Extremes) {
  const int num_mcus = 1;

  // Test with all zeros
  {
    std::vector<uint8_t> sample_buf(num_mcus * 64 * 3, 0);
    std::vector<uint8_t> output_scalar(num_mcus * 8 * 4);
    std::vector<uint8_t> output_highway(num_mcus * 8 * 4);

    ConvertH1V1YCbCr_Scalar(output_scalar.data(), sample_buf.data(), 0,
                            num_mcus, crr, cbb, crg, cbg);
    ConvertH1V1YCbCr_HighwayDispatch(output_highway.data(), sample_buf.data(),
                                     0, num_mcus, crr, cbb, crg, cbg);

    for (size_t i = 0; i < output_scalar.size(); i++) {
      EXPECT_NEAR(output_scalar[i], output_highway[i], 1)
          << "Mismatch with all-zero input at component " << i;
    }
  }

  // Test with all 255s
  {
    std::vector<uint8_t> sample_buf(num_mcus * 64 * 3, 255);
    std::vector<uint8_t> output_scalar(num_mcus * 8 * 4);
    std::vector<uint8_t> output_highway(num_mcus * 8 * 4);

    ConvertH1V1YCbCr_Scalar(output_scalar.data(), sample_buf.data(), 0,
                            num_mcus, crr, cbb, crg, cbg);
    ConvertH1V1YCbCr_HighwayDispatch(output_highway.data(), sample_buf.data(),
                                     0, num_mcus, crr, cbb, crg, cbg);

    for (size_t i = 0; i < output_scalar.size(); i++) {
      EXPECT_NEAR(output_scalar[i], output_highway[i], 1)
          << "Mismatch with all-255 input at component " << i;
    }
  }
}

}  // namespace jpgd
