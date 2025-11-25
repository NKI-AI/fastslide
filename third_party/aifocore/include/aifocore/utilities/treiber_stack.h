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

/**
 * @file treiber_stack.h
 * @brief Generic lock-free stack implementation using the Treiber algorithm
 * @author Jonas Teuwen
 * @date 2025
 *
 * This file provides a generic lock-free stack (Treiber stack) that can be
 * used for high-performance concurrent data structures. The implementation
 * uses atomic compare-and-swap operations to ensure thread safety without
 * mutex contention.
 *
 * The Treiber stack is a foundational building block for many lock-free
 * data structures including object pools, memory allocators, and caches.
 */

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_TREIBER_STACK_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_TREIBER_STACK_H_

#include <atomic>
#include <cstddef>
#include <memory>

namespace aifocore {

/**
 * @brief Lock-free stack node for use with TreiberStack
 *
 * This is a simple intrusive node structure. Types that want to be stored
 * in a TreiberStack should either inherit from this or contain it as a member.
 *
 * @tparam T The type being stored (typically the derived class)
 */
template <typename T>
struct TreiberNode {
  std::atomic<TreiberNode<T>*> next{nullptr};
};

/**
 * @brief Generic lock-free stack using the Treiber algorithm
 *
 * This class implements a lock-free stack (LIFO) data structure using
 * atomic compare-and-swap operations. It provides thread-safe push and pop
 * operations without requiring mutexes, making it ideal for high-concurrency
 * scenarios.
 *
 * Key features:
 * - Lock-free push and pop operations
 * - Zero mutex overhead
 * - Scales well with thread count
 * - Simple and efficient ABA-resistant design
 *
 * Usage example:
 * @code
 * struct MyNode : public TreiberNode<MyNode> {
 *   int data;
 * };
 *
 * TreiberStack<MyNode> stack;
 * MyNode* node = new MyNode{42};
 * stack.Push(node);
 * MyNode* popped = stack.Pop();
 * @endcode
 *
 * @note This is a non-owning container - it does not manage memory.
 *       The caller is responsible for allocation and deallocation.
 *
 * @note The stack is intrusive - nodes must inherit from or contain
 *       TreiberNode<T> to participate in the linked structure.
 *
 * @tparam T The node type (must inherit from or contain TreiberNode<T>)
 */
template <typename T>
class TreiberStack {
 public:
  /**
   * @brief Construct an empty Treiber stack
   */
  TreiberStack() : head_(nullptr), size_(0) {}

  /**
   * @brief Destructor
   *
   * @note Does NOT delete nodes - caller must manage memory
   * @warning Ensure all nodes are removed or externally managed before
   * destruction
   */
  ~TreiberStack() = default;

  // Non-copyable
  TreiberStack(const TreiberStack&) = delete;
  TreiberStack& operator=(const TreiberStack&) = delete;

  /**
   * @brief Push a node onto the stack (lock-free)
   *
   * Atomically pushes a node onto the top of the stack using compare-and-swap.
   * This operation is lock-free and safe for concurrent access.
   *
   * @param node Pointer to node to push (must not be nullptr)
   * @note Thread-safe - can be called concurrently from multiple threads
   * @note The node must remain valid until popped from the stack
   */
  void Push(T* node) {
    if (node == nullptr) {
      return;
    }

    TreiberNode<T>* treiber_node = static_cast<TreiberNode<T>*>(node);
    TreiberNode<T>* old_head = head_.load(std::memory_order_relaxed);

    do {
      treiber_node->next.store(old_head, std::memory_order_relaxed);
    } while (!head_.compare_exchange_weak(old_head, treiber_node,
                                          std::memory_order_release,
                                          std::memory_order_relaxed));

    size_.fetch_add(1, std::memory_order_relaxed);
  }

  /**
   * @brief Pop a node from the stack (lock-free)
   *
   * Atomically pops and returns the top node from the stack using
   * compare-and-swap. Returns nullptr if the stack is empty.
   *
   * @return Pointer to the popped node, or nullptr if stack is empty
   * @note Thread-safe - can be called concurrently from multiple threads
   * @note Caller is responsible for managing the returned node's memory
   */
  T* Pop() {
    TreiberNode<T>* old_head = head_.load(std::memory_order_acquire);

    while (old_head != nullptr) {
      TreiberNode<T>* new_head = old_head->next.load(std::memory_order_relaxed);

      if (head_.compare_exchange_weak(old_head, new_head,
                                      std::memory_order_release,
                                      std::memory_order_acquire)) {
        // Successfully popped
        size_.fetch_sub(1, std::memory_order_relaxed);
        return static_cast<T*>(old_head);
      }
      // CAS failed, old_head is updated with current value, retry
    }

    return nullptr;  // Stack is empty
  }

  /**
   * @brief Check if the stack is empty
   *
   * @return true if stack is empty, false otherwise
   * @note This is an approximation in concurrent scenarios
   */
  bool Empty() const {
    return head_.load(std::memory_order_acquire) == nullptr;
  }

  /**
   * @brief Get approximate size of the stack
   *
   * @return Approximate number of nodes in the stack
   * @note Due to concurrent operations, this is approximate only
   */
  size_t Size() const { return size_.load(std::memory_order_relaxed); }

  /**
   * @brief Clear all nodes from the stack
   *
   * Atomically removes all nodes from the stack and returns them as a
   * linked list. The caller is responsible for processing/deleting the nodes.
   *
   * @return Pointer to the head of the removed linked list, or nullptr if
   * empty
   * @note Thread-safe but should typically be called when no other operations
   *       are in progress
   */
  T* Clear() {
    TreiberNode<T>* old_head =
        head_.exchange(nullptr, std::memory_order_acquire);
    size_.store(0, std::memory_order_relaxed);
    return static_cast<T*>(old_head);
  }

 private:
  std::atomic<TreiberNode<T>*> head_;  ///< Head of the stack
  std::atomic<size_t> size_;           ///< Approximate size counter
};

}  // namespace aifocore

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_TREIBER_STACK_H_
