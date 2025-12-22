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

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_CONCEPTS_CEREAL_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_CONCEPTS_CEREAL_H_

#include "aifocore/concepts/numeric.h"

#include <cstddef>

#include <cereal/cereal.hpp>

namespace cereal {

template <class Archive, aifocore::GenericNumber T, std::size_t N>
void serialize(Archive& archive, aifocore::Size<T, N>& value) {
  for (std::size_t i = 0; i < N; ++i) {
    archive(value[i]);
  }
}

}  // namespace cereal

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_CONCEPTS_CEREAL_H_
