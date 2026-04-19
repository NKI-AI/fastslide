// Copyright 2024 Jonas Teuwen. All Rights Reserved.
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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_RANGES_ZIP_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_RANGES_ZIP_H_

#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>

namespace aifocore::ranges {

// Custom iterator for zip that stores a tuple of iterators
template <typename... Iterators>
class zip_iterator {
 public:
  using value_type =
      std::tuple<typename std::iterator_traits<Iterators>::reference...>;
  using reference = value_type;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::input_iterator_tag;

  explicit zip_iterator(Iterators... iterators) : iterators_(iterators...) {}

  zip_iterator& operator++() {
    std::apply([](auto&... iters) { ((++iters), ...); }, iterators_);
    return *this;
  }

  reference operator*() const {
    return std::apply([](auto&... iters) { return std::tie(*iters...); },
                      iterators_);
  }

  bool operator==(const zip_iterator& other) const {
    return iterators_ == other.iterators_;
  }

  bool operator!=(const zip_iterator& other) const { return !(*this == other); }

 private:
  std::tuple<Iterators...> iterators_;
};

// zip_view that holds the begin and end iterators
template <typename... Containers>
class zip_view {
 public:
  explicit zip_view(Containers&... containers)
      : begin_(std::begin(containers)...), end_(std::end(containers)...) {}

  auto begin() const { return begin_; }

  auto end() const { return end_; }

 private:
  zip_iterator<decltype(std::begin(std::declval<Containers&>()))...> begin_;
  zip_iterator<decltype(std::end(std::declval<Containers&>()))...> end_;
};

// Helper function to create a zip_view
template <typename... Containers>
auto zip(Containers&... containers) {
  return zip_view<Containers...>(containers...);
}

}  // namespace aifocore::ranges

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_RANGES_ZIP_H_
