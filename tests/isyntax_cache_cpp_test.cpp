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

#include "fastslide/readers/isyntax/third_party/cache.h"
#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include "fastslide/readers/isyntax/third_party/reader.h"

namespace {

TEST(IsyntaxCacheTest, CreateRejectsInvalidArgs) {
  EXPECT_FALSE(isyntax::IsyntaxCache::CreateAndInject("test",
                                                      /*capacity_bytes=*/0,
                                                      /*isyntax=*/nullptr)
                   .ok());
}

TEST(IsyntaxCacheTest, InjectSetsAllocatorsAndIsyntaxPointers) {
  // Minimal required fields for allocator sizing.
  isyntax_t isx{};
  isx.block_width = 128;
  isx.block_height = 128;

  auto cache_or = isyntax::IsyntaxCache::CreateAndInject(
      "test", /*capacity_bytes=*/1 << 20, &isx);
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

  auto cache_or = isyntax::IsyntaxCache::CreateAndInject(
      "test", /*capacity_bytes=*/1 << 20, &isx);
  EXPECT_FALSE(cache_or.ok());
}

TEST(IsyntaxCacheTest, FilesWithTheSameBlockSizeShareACache) {
  isyntax_t first{};
  first.block_width = 128;
  first.block_height = 128;
  isyntax_t second = first;
  isyntax_t other{};
  other.block_width = 64;
  other.block_height = 64;

  auto first_cache = isyntax::IsyntaxCache::Shared(&first);
  auto second_cache = isyntax::IsyntaxCache::Shared(&second);
  auto other_cache = isyntax::IsyntaxCache::Shared(&other);
  ASSERT_TRUE(first_cache.ok());
  ASSERT_TRUE(second_cache.ok());
  ASSERT_TRUE(other_cache.ok());

  EXPECT_EQ(first_cache->get(), second_cache->get());
  EXPECT_EQ(second.ll_coeff_block_allocator, first.ll_coeff_block_allocator);
  EXPECT_EQ(second.h_coeff_block_allocator, first.h_coeff_block_allocator);
  EXPECT_NE(other_cache->get(), first_cache->get());
}

TEST(IsyntaxCacheTest, SharedCachesTakeTheSharedCapacity) {
  isyntax_t isx{};
  isx.block_width = 128;
  isx.block_height = 128;
  auto cache = isyntax::IsyntaxCache::Shared(&isx);
  ASSERT_TRUE(cache.ok());
  const size_t previous = isyntax::IsyntaxCache::SharedCapacityBytes();

  isyntax::IsyntaxCache::SetSharedCapacityBytes(size_t{3} << 20);

  EXPECT_EQ((*cache)->get()->capacity_bytes, size_t{3} << 20);
  isyntax::IsyntaxCache::SetSharedCapacityBytes(previous);
}

}  // namespace
