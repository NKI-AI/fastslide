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

#include <gtest/gtest.h>

#include "readers/isyntax/third_party/cache.h"
#include "readers/isyntax/third_party/isyntax.h"

namespace {

TEST(IsyntaxCacheTest, CreateRejectsInvalidArgs) {
  EXPECT_FALSE(isyntax::IsyntaxCache::CreateAndInject("test", /*cache_size=*/0,
                                                      /*isyntax=*/nullptr)
                   .ok());
}

TEST(IsyntaxCacheTest, InjectSetsAllocatorsAndIsyntaxPointers) {
  // Minimal required fields for allocator sizing.
  isyntax_t isx{};
  isx.block_width = 128;
  isx.block_height = 128;

  auto cache_or =
      isyntax::IsyntaxCache::CreateAndInject("test", /*cache_size=*/10, &isx);
  ASSERT_TRUE(cache_or.ok());
  ASSERT_NE(cache_or->get(), nullptr);

  EXPECT_NE(isx.ll_coeff_block_allocator, nullptr);
  EXPECT_NE(isx.h_coeff_block_allocator, nullptr);
  EXPECT_FALSE(isx.is_block_allocator_owned);
}

TEST(IsyntaxCacheTest, InjectRejectsPreinitializedAllocators) {
  isyntax_t isx{};
  isx.block_width = 128;
  isx.block_height = 128;

  // Simulate an isyntax object that already has allocators.
  isx.ll_coeff_block_allocator =
      reinterpret_cast<block_allocator_t*>(static_cast<uintptr_t>(0x1));

  auto cache_or =
      isyntax::IsyntaxCache::CreateAndInject("test", /*cache_size=*/10, &isx);
  EXPECT_FALSE(cache_or.ok());
}

}  // namespace
