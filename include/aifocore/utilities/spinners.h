// The MIT License (MIT)
//
// Copyright (c) 2018 Jan Kuri <jkuri88@gmail.com>
// Copyright 2024 Jonas Teuwen
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// The original spinners.h has been modified by Jonas Teuwen to
// use modern C++20 features

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_SPINNERS_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_SPINNERS_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>  // For std::pair
#include <vector>

namespace aifocore::utilities {

// Helper to convert UTF-8 string literals to std::string
inline std::string U8ToString(const char8_t* u8_str) {
  return std::string(reinterpret_cast<const char*>(u8_str));
}

// Define all spinner data in one place using name and symbol pairs.
// Store raw u8 string literals to allow constexpr initialization.
inline constexpr auto kSpinnerData = []() {
  using SpinnerPair = std::pair<std::string_view, const char8_t*>;
  // clang-format off
  constexpr std::array<SpinnerPair, 48> data = {{
      {"dots",           u8"⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"},
      {"dots2",          u8"⣾⣽⣻⢿⡿⣟⣯⣷"},
      {"dots3",          u8"⠋⠙⠚⠞⠖⠦⠴⠲⠳⠓"},
      {"dots4",          u8"⠄⠆⠇⠋⠙⠸⠰⠠⠰⠸⠙⠋⠇⠆"},
      {"dots5",          u8"⠋⠙⠚⠒⠂⠂⠒⠲⠴⠦⠖⠐⠐⠒⠓⠋"},
      {"dots6",          u8"⠁⠉⠙⠚⠒⠂⠂⠒⠲⠴⠤⠄⠄⠤⠴⠲⠒⠂⠂⠒⠚⠙⠉⠁"},
      {"dots7",          u8"⠈⠉⠋⠓⠒⠐⠐⠒⠖⠦⠤⠠⠠⠤⠦⠖⠒⠐⠐⠒⠓⠋⠉⠈"},
      {"dots8",          u8"⠁⠁⠉⠙⠚⠒⠂⠂⠒⠲⠴⠤⠄⠄⠤⠠⠠⠤⠦⠖⠒⠐⠐⠒⠓⠋⠉⠈⠈"},
      {"dots9",          u8"⢹⢺⢼⣸⣇⡧⡗⡏"},
      {"dots10",         u8"⢄⢂⢁⡁⡈⡐⡠"},
      {"dots11",         u8"⠁⠂⠄⡀⢀⠠⠐⠈"},
      {"pipe",           u8"┤┘┴└├┌┬┐"},
      {"star",           u8"✶✸✹✺✹✷"},
      {"star2",          u8"+x*"},
      {"flip",           u8"___-``'´-___"},
      {"hamburger",      u8"☱☲☴"},
      {"growVertical",   u8" ▃▄▅▆▇▆▅▄▃"},
      {"growHorizontal", u8"▏▎▍▌▋▊▉▊▋▌▍▎"},
      {"balloon",        u8" .oO@* "},
      {"balloon2",       u8".oO°Oo."},
      {"noise",          u8"▓▒░"},
      {"bounce",         u8"⠁⠂⠄⠂"},
      {"boxBounce",      u8"▖▘▝▗"},
      {"boxBounce2",     u8"▌▀▐▄"},
      {"triangle",       u8"◢◣◤◥"},
      {"arc",            u8"◜◠◝◞◡◟"},
      {"circle",         u8"◡⊙◠"},
      {"squareCorners",  u8"◰◳◲◱"},
      {"circleQuarters", u8"◴◷◶◵"},
      {"circleHalves",   u8"◐◓◑◒"},
      {"squish",         u8"╫╪"},
      {"toggle",         u8"⊶⊷"},
      {"toggle2",        u8"▫▪"},
      {"toggle3",        u8"□■"},
      {"toggle4",        u8"■□▪▫"},
      {"toggle5",        u8"▮▯"},
      {"toggle6",        u8"ဝ၀"},
      {"toggle7",        u8"⦾⦿"},
      {"toggle8",        u8"◍◌"},
      {"toggle9",        u8"◉◎"},
      {"toggle10",       u8"㊂㊀㊁"},
      {"toggle11",       u8"⧇⧆"},
      {"toggle12",       u8"☗☖"},
      {"toggle13",       u8"=*-"},
      {"arrow",          u8"←↖↑↗→↘↓↙"}
  }};
  // clang-format on
  // Ensure "dots" is the first element for default fallback behavior.
  static_assert(data[0].first == "dots",
                "Default 'dots' spinner must be the first element.");
  return data;
}();

// Enum definition remains largely the same for strong typing.
// The order MUST match the order in kSpinnerData.
enum class SpinnerType : std::uint8_t {
  kDots,
  kDots2,
  kDots3,
  kDots4,
  kDots5,
  kDots6,
  kDots7,
  kDots8,
  kDots9,
  kDots10,
  kDots11,
  kPipe,
  kStar,
  kStar2,
  kFlip,
  kHamburger,
  kGrowVertical,
  kGrowHorizontal,
  kBalloon,
  kBalloon2,
  kNoise,
  kBounce,
  kBoxBounce,
  kBoxBounce2,
  kTriangle,
  kArc,
  kCircle,
  kSquareCorners,
  kCircleQuarters,
  kCircleHalves,
  kSquish,
  kToggle,
  kToggle2,
  kToggle3,
  kToggle4,
  kToggle5,
  kToggle6,
  kToggle7,
  kToggle8,
  kToggle9,
  kToggle10,
  kToggle11,
  kToggle12,
  kToggle13,
  kArrow
};

/**
 * @brief Gets the symbol sequence for a given SpinnerType.
 * @param type The enum value representing the desired spinner.
 * @return A string_view of the UTF-8 symbols for the spinner. Falls back to
 * kDots if the type is invalid.
 */
[[nodiscard]] inline std::string_view GetSpinnerSymbolsByType(
    SpinnerType type) noexcept {
  auto index = static_cast<size_t>(type);
  const char8_t* symbols_u8 = (index < kSpinnerData.size())
                                  ? kSpinnerData[index].second
                                  : kSpinnerData[0].second;  // Default fallback
  // Construct string_view from the reinterpret_casted pointer.
  return std::string_view(reinterpret_cast<const char*>(symbols_u8));
}

/**
 * @brief Gets the string name for a given SpinnerType.
 * @param type The enum value representing the desired spinner.
 * @return A string_view of the name for the spinner. Falls back to "dots" if
 * the type is invalid.
 */
[[nodiscard]] inline std::string_view GetSpinnerNameByType(
    SpinnerType type) noexcept {
  auto index = static_cast<size_t>(type);
  if (index < kSpinnerData.size()) {
    // Assumes enum values directly correspond to array indices.
    return kSpinnerData[index].first;
  }
  // Fallback to default ("dots") if index is out of bounds.
  return kSpinnerData[0].first;
}

/**
 * @brief Looks up the symbol sequence by spinner name.
 * @param name The string name of the desired spinner.
 * @return A string_view of the UTF-8 symbols for the spinner. Falls back to
 * "dots" symbols if the name is not found.
 */
[[nodiscard]] inline std::string_view LookupSpinnerSymbolsByName(
    std::string_view name) noexcept {
  for (const auto& pair : kSpinnerData) {
    if (pair.first == name) {
      // Construct string_view from the reinterpret_casted pointer.
      return std::string_view(reinterpret_cast<const char*>(pair.second));
    }
  }
  // Fallback to default (dots) if name not found.
  return std::string_view(
      reinterpret_cast<const char*>(kSpinnerData[0].second));
}

/**
 * @class Spinner
 * @brief Displays an animated spinner in the console.
 *
 * Manages a separate thread to display an animated sequence of characters
 * (a spinner) along with optional text. Automatically handles cursor
 * hiding/showing and line clearing.
 */
class Spinner {
 public:
  /**
   * @brief Default constructor. Uses kDots spinner, 80ms interval, no text.
   */
  Spinner() noexcept
      : interval_{80},
        text_{},
        is_running_{false},
        symbols_{GetSpinnerSymbolsByType(SpinnerType::kDots)} {}

  /**
   * @brief Constructor specifying interval, text, and spinner type via enum.
   * @param interval Update interval in milliseconds.
   * @param text Text to display next to the spinner.
   * @param type The type of spinner animation to use.
   */
  Spinner(int interval, std::string_view text, SpinnerType type) noexcept
      : interval_{interval},
        text_{text},
        is_running_{false},
        symbols_{GetSpinnerSymbolsByType(type)} {}

  /**
   * @brief Constructor specifying interval, text, and spinner type via name.
   * @param interval Update interval in milliseconds.
   * @param text Text to display next to the spinner.
   * @param name The name of the spinner animation (e.g., "dots2", "arc").
   */
  Spinner(int interval, std::string_view text, std::string_view name) noexcept
      : interval_{interval},
        text_{text},
        is_running_{false},
        symbols_{LookupSpinnerSymbolsByName(name)} {}

  // Rule of five: Prevent copying, allow moving.
  Spinner(const Spinner&) = delete;
  Spinner& operator=(const Spinner&) = delete;
  Spinner(Spinner&&) noexcept = default;
  Spinner& operator=(Spinner&&) noexcept = default;

  /**
   * @brief Destructor. Stops the spinner thread if running.
   */
  ~Spinner() { Stop(); }

  /**
   * @brief Sets the update interval for the spinner animation.
   * @param interval Interval in milliseconds.
   */
  void SetInterval(int interval) noexcept { interval_ = interval; }

  /**
   * @brief Sets the text displayed next to the spinner.
   * @param text The text to display.
   */
  void SetText(std::string_view text) noexcept { text_ = text; }

  /**
   * @brief Sets the spinner animation type using the enum.
   * @param type The SpinnerType enum value.
   */
  void SetSymbols(SpinnerType type) noexcept {
    symbols_ = GetSpinnerSymbolsByType(type);
  }

  /**
   * @brief Sets the spinner animation type using its string name.
   * @param name The name of the spinner animation.
   */
  void SetSymbols(std::string_view name) noexcept {
    symbols_ = LookupSpinnerSymbolsByName(name);
  }

  /**
   * @brief Starts the spinner animation in a separate thread.
   */
  void Start() {
    if (is_running_)
      return;  // Avoid starting multiple threads
    is_running_ = true;
    t_ = std::thread([this]() { RunSpinner(); });
  }

  /**
   * @brief Stops the spinner animation thread and cleans up the console line.
   */
  void Stop() noexcept {
    if (is_running_) {
      is_running_ = false;
      if (t_.joinable()) {
        t_.join();  // Wait for RunSpinner thread to finish
      }
      // Clear the last line printed by the spinner after the thread has exited.
      // The cursor visibility is handled by CursorGuard's
      // destructor in RunSpinner.
      ClearLine();
    }
  }

 private:
  int interval_;
  std::string text_;  // Store the text view provided by the user
  bool is_running_;
  std::string_view symbols_;
  std::thread t_;

  /**
   * @class CursorGuard
   * @brief RAII helper to hide cursor on creation and show on destruction.
   */
  class CursorGuard {
   public:
    CursorGuard() { std::cout << "\033[?25l" << std::flush; }  // Hide cursor

    ~CursorGuard() { std::cout << "\033[?25h" << std::flush; }  // Show cursor

    CursorGuard(const CursorGuard&) = delete;
    CursorGuard& operator=(const CursorGuard&) = delete;
    CursorGuard(CursorGuard&&) noexcept = default;
    CursorGuard& operator=(CursorGuard&&) noexcept = default;
  };

  /**
   * @brief The main loop executed by the spinner thread.
   *
   * Handles animating the symbols, displaying text, and respecting the running
   * state. Uses CursorGuard for cursor management.
   */
  void RunSpinner() {
    // Pre-calculate UTF-8 character views for efficient iteration.
    std::vector<std::string_view> chars;
    size_t current_pos = 0;
    while (current_pos < symbols_.length()) {
      size_t char_len = 1;
      // Basic UTF-8 leading byte check to determine character length.
      unsigned char first_byte =
          static_cast<unsigned char>(symbols_[current_pos]);
      if ((first_byte & 0b11100000) == 0b11000000)
        char_len = 2;
      else if ((first_byte & 0b11110000) == 0b11100000)
        char_len = 3;
      else if ((first_byte & 0b11111000) == 0b11110000)
        char_len = 4;

      // Ensure we don't read past the end of the string_view.
      if (current_pos + char_len > symbols_.length()) {
        // Invalid sequence or end of string reached
        // unexpectedly. Stop processing.
        break;
      }
      chars.push_back(symbols_.substr(current_pos, char_len));
      current_pos += char_len;
    }

    if (chars.empty()) {
      // No valid symbols to display. Log error? For now, just stop.
      is_running_ = false;
      return;
    }

    size_t idx = 0;
    CursorGuard guard;  // Manages cursor visibility via RAII.

    while (is_running_) {
      // Use \r to return cursor to beginning, \033[K to clear line to end.
      std::cout << "\r\033[K" << chars[idx] << " " << text_ << std::flush;

      idx = (idx + 1) % chars.size();  // Cycle through characters.
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_));
    }
    // Destructor of CursorGuard will run
    // when this function exits, showing cursor.
  }

  /**
   * @brief Clears the current console line from the cursor position to the end.
   */
  static void ClearLine() noexcept {
    // Use \r to return cursor to beginning, \033[K to clear line to end.
    std::cout << "\r\033[K" << std::flush;
  }
};

}  // namespace aifocore::utilities

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_SPINNERS_H_
