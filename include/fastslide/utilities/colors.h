// Copyright 2025 Jonas Teuwen. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_COLORS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_COLORS_H_

#include <array>
#include <cstdint>
#include <string>

#include "absl/status/statusor.h"

namespace fastslide {

/// @brief RGB color structure with uint8_t components
struct ColorRGB {
  uint8_t r{0};
  uint8_t g{0};
  uint8_t b{0};

  /// @brief Default constructor
  constexpr ColorRGB() = default;

  /// @brief Constructor with RGB values
  constexpr ColorRGB(uint8_t red, uint8_t green, uint8_t blue)
      : r(red), g(green), b(blue) {}

  /// @brief Array access operator for compatibility
  constexpr uint8_t& operator[](std::size_t index) { return (&r)[index]; }

  /// @brief Const array access operator for compatibility
  constexpr const uint8_t& operator[](std::size_t index) const {
    return (&r)[index];
  }

  /// @brief Equality comparison
  constexpr bool operator==(const ColorRGB& other) const {
    return r == other.r && g == other.g && b == other.b;
  }

  /// @brief Inequality comparison
  constexpr bool operator!=(const ColorRGB& other) const {
    return !(*this == other);
  }
};

/// @brief Convert HSB (Hue, Saturation, Brightness) to RGB
/// @param hue Hue value in range [0.0, 1.0]
/// @param saturation Saturation value in range [0.0, 1.0]
/// @param brightness Brightness value in range [0.0, 1.0]
/// @return RGB color triplet with values in range [0, 255]
ColorRGB HSBtoRGB(float hue, float saturation, float brightness);

/// @brief Get default channel color using QuPath-style algorithm
/// @param channel Channel index (0-based)
/// @return RGB color triplet with values in range [0, 255]
///
/// This function replicates QuPath's getDefaultChannelColor algorithm:
/// - Channels 0-5 use predefined colors (red, green, blue, yellow, cyan,
/// magenta)
/// - Higher channels use HSB color space with calculated hue, saturation, and
/// brightness
/// - Channels >= 360 wrap around using modulo operation
ColorRGB GetDefaultChannelColor(int channel);

/// @brief Pack RGB values into a 32-bit integer
/// @param red Red component [0, 255]
/// @param green Green component [0, 255]
/// @param blue Blue component [0, 255]
/// @return Packed RGB value as 32-bit integer
uint32_t PackRGB(uint8_t red, uint8_t green, uint8_t blue);

/// @brief Parse RGB color from a comma-separated string
/// @param str String containing RGB values like "255,0,128"
/// @return Array of RGB components [r, g, b] or error if parsing fails
///
/// This function parses color strings in the format "R,G,B" where each
/// component is an integer in the range [0, 255].
absl::StatusOr<std::array<uint8_t, 3>> ParseRgb(const std::string& str);

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_COLORS_H_
