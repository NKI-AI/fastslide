#include <gtest/gtest.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

// Forward declare the dispatch function
namespace jpgd {
void IdctHighwayDispatch(const int16_t* input, uint8_t* output);
}

// Helper to print 8x8 block
void print_block(const char* name, const uint8_t* data) {
  std::cout << name << ":\n";
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      std::cout << std::setw(4) << (int)data[row * 8 + col];
    }
    std::cout << "\n";
  }
  std::cout << "\n";
}

void print_input_block(const char* name, const int16_t* data) {
  std::cout << name << ":\n";
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      std::cout << std::setw(6) << data[row * 8 + col];
    }
    std::cout << "\n";
  }
  std::cout << "\n";
}

TEST(IdctTest, DCOnly) {
  alignas(16) int16_t input[64] = {0};
  alignas(16) uint8_t output[64] = {0};

  // Test case 1: DC only
  input[0] = 1024;

  print_input_block("Input (DC only)", input);

  jpgd::IdctHighwayDispatch(input, output);

  print_block("Highway output", output);

  // All pixels should be the same (flat block)
  uint8_t first = output[0];
  for (int i = 1; i < 64; i++) {
    EXPECT_EQ(output[i], first) << "All pixels should be equal for DC-only";
  }

  // Value should be around 255 (saturated)
  EXPECT_GE(first, 250)
      << "DC value of 1024 should produce near-saturated output";
}

TEST(IdctTest, ACCoefficients) {
  alignas(16) int16_t input[64] = {0};
  alignas(16) uint8_t output[64] = {0};

  // Test case 2: DC + one AC coefficient
  input[0] = 1024;  // DC
  input[1] = 512;   // AC (0,1)

  print_input_block("Input (DC + AC)", input);

  jpgd::IdctHighwayDispatch(input, output);

  print_block("Highway output", output);

  // Check that rows are identical (constant in Y direction)
  for (int row = 1; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      EXPECT_EQ(output[row * 8 + col], output[col])
          << "Rows should be identical for horizontal AC coefficient";
    }
  }
}

TEST(IdctTest, MultipleCoefficients) {
  alignas(16) int16_t input[64] = {0};
  alignas(16) uint8_t output[64] = {0};

  // Test case 3: Multiple coefficients
  input[0] = 800;
  input[1] = 200;
  input[8] = 150;
  input[9] = -100;
  input[2] = 50;

  print_input_block("Input (Multiple coefficients)", input);

  jpgd::IdctHighwayDispatch(input, output);

  print_block("Highway output", output);

  // Just verify it produces reasonable output (not all same, not all 0 or 255)
  bool all_same = true;
  uint8_t first = output[0];
  for (int i = 1; i < 64; i++) {
    if (output[i] != first) {
      all_same = false;
      break;
    }
  }
  EXPECT_FALSE(all_same) << "Output should have some variation";

  // Check bounds
  for (int i = 0; i < 64; i++) {
    EXPECT_GE(output[i], 0);
    EXPECT_LE(output[i], 255);
  }
}

TEST(IdctTest, ZeroInput) {
  alignas(16) int16_t input[64] = {0};
  alignas(16) uint8_t output[64] = {0};

  jpgd::IdctHighwayDispatch(input, output);

  // All outputs should be 128 (middle gray)
  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(output[i], 128) << "Zero input should produce 128 (middle gray)";
  }
}

TEST(IdctTest, FullTest_RandomCoefficients) {
  alignas(16) int16_t input[64] = {0};
  alignas(16) uint8_t output[64] = {0};

  // More complex test with multiple non-zero coefficients
  input[0] = 512;  // DC
  input[1] = 128;  // AC
  input[2] = 64;
  input[8] = -50;
  input[9] = 80;
  input[16] = -30;

  print_input_block("Input (Random coefficients)", input);

  jpgd::IdctHighwayDispatch(input, output);

  print_block("Highway output", output);

  // Verify basic constraints
  bool all_same = true;
  uint8_t first = output[0];
  for (int i = 1; i < 64; i++) {
    if (output[i] != first) {
      all_same = false;
      break;
    }
  }
  EXPECT_FALSE(all_same) << "Output should have variation";

  // Check all values are in valid range
  for (int i = 0; i < 64; i++) {
    EXPECT_GE(output[i], 0);
    EXPECT_LE(output[i], 255);
  }
}
