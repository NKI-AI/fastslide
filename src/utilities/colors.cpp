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

#include "fastslide/utilities/colors.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

namespace fastslide {

ColorRGB HSBtoRGB(float hue, float saturation, float brightness) {
  // Clamp inputs to valid ranges
  hue = std::fmod(hue, 1.0F);
  if (hue < 0.0F)
    hue += 1.0F;
  saturation = std::clamp(saturation, 0.0F, 1.0F);
  brightness = std::clamp(brightness, 0.0F, 1.0F);

  if (saturation == 0.0F) {
    // Achromatic (gray)
    uint8_t gray = static_cast<uint8_t>(brightness * 255.0F);
    return ColorRGB{gray, gray, gray};
  }

  float h = hue * 6.0F;  // Convert to 0-6 range
  int i = static_cast<int>(std::floor(h));
  float f = h - static_cast<float>(i);  // Fractional part
  float p = brightness * (1.0F - saturation);
  float q = brightness * (1.0F - saturation * f);
  float t = brightness * (1.0F - saturation * (1.0F - f));

  float r, g, b;
  switch (i % 6) {
    case 0:
      r = brightness;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = brightness;
      b = p;
      break;
    case 2:
      r = p;
      g = brightness;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = brightness;
      break;
    case 4:
      r = t;
      g = p;
      b = brightness;
      break;
    case 5:
      r = brightness;
      g = p;
      b = q;
      break;
    default:
      r = g = b = 0.0F;
      break;  // Should never happen
  }

  return ColorRGB{static_cast<uint8_t>(r * 255.0F),
                  static_cast<uint8_t>(g * 255.0F),
                  static_cast<uint8_t>(b * 255.0F)};
}

ColorRGB GetDefaultChannelColor(int channel) {
  constexpr int n = 360;
  if (channel >= n) {
    channel = channel % n;
  }

  // Predefined colors for channels 0-5
  switch (channel) {
    case 0:
      return ColorRGB{255, 0, 0};  // Red
    case 1:
      return ColorRGB{0, 255, 0};  // Green
    case 2:
      return ColorRGB{0, 0, 255};  // Blue
    case 3:
      return ColorRGB{255, 224, 0};  // Yellow
    case 4:
      return ColorRGB{0, 224, 224};  // Cyan
    case 5:
      return ColorRGB{255, 0, 224};  // Magenta
    default: {
      // HSB-based color generation for channels > 5
      int c = channel;
      constexpr int hue_inc = 128;
      float hue = static_cast<float>((c * hue_inc) % 360) / 360.0F;
      float saturation = 1.0F - static_cast<float>(c / 10) / 20.0F;
      float brightness = 1.0F - static_cast<float>(c / 10) / 20.0F;

      // Clamp saturation and brightness to reasonable ranges
      saturation = std::clamp(saturation, 0.2F, 1.0F);
      brightness = std::clamp(brightness, 0.2F, 1.0F);

      return HSBtoRGB(hue, saturation, brightness);
    }
  }
}

uint32_t PackRGB(uint8_t red, uint8_t green, uint8_t blue) {
  return (static_cast<uint32_t>(red) << 16) |
         (static_cast<uint32_t>(green) << 8) | static_cast<uint32_t>(blue);
}

absl::StatusOr<std::array<uint8_t, 3>> ParseRgb(const std::string& str) {
  std::array<uint8_t, 3> rgb{};
  std::istringstream ss(str);

  for (auto& component : rgb) {
    int value = 0;
    ss >> value;

    if (ss.peek() == ',') {
      ss.ignore();
    }

    if (value < 0 || value > 255) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Invalid RGB value: %d (must be in range [0, 255])", value));
    }

    component = static_cast<uint8_t>(value);
  }

  return rgb;
}

}  // namespace fastslide
