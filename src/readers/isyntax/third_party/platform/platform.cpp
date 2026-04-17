/*
  BSD 2-Clause License

  Copyright (c) 2019-2024, Pieter Valkema

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "fastslide/readers/isyntax/third_party/platform/common.h"

#define PLATFORM_IMPL
#include "fastslide/readers/isyntax/third_party/platform/intrinsics.h"
#include "fastslide/readers/isyntax/third_party/platform/platform.h"

#include "aifocore/platform/portability.h"

#if APPLE
#include <sys/sysctl.h>  // for sysctlbyname()
#endif

#include <cstdint>

namespace {

uint32_t GetOsPageSize() {
#if WINDOWS
  SYSTEM_INFO win32_system_info;
  GetSystemInfo(&win32_system_info);
  return static_cast<uint32_t>(win32_system_info.dwPageSize);
#else
  const int page_size = getpagesize();
  return (page_size > 0) ? static_cast<uint32_t>(page_size) : 4096u;
#endif
}

}  // namespace

extern "C" {

void get_system_info(bool verbose) {
  system_info_t system_info = {0};
#if WINDOWS
  SYSTEM_INFO win32_system_info;
  GetSystemInfo(&win32_system_info);
  system_info.logical_cpu_count =
      static_cast<int32_t>(win32_system_info.dwNumberOfProcessors);
  system_info.physical_cpu_count =
      system_info
          .logical_cpu_count;  // TODO(pvalkema): how to read this on Windows?
  system_info.os_page_size = win32_system_info.dwPageSize;
#elif APPLE
  size_t physical_cpu_count_len = sizeof(system_info.physical_cpu_count);
  size_t logical_cpu_count_len = sizeof(system_info.logical_cpu_count);
  sysctlbyname("hw.physicalcpu", &system_info.physical_cpu_count,
               &physical_cpu_count_len, nullptr, 0);
  sysctlbyname("hw.logicalcpu", &system_info.logical_cpu_count,
               &logical_cpu_count_len, nullptr, 0);
  system_info.os_page_size = static_cast<uint32_t>(getpagesize());
  system_info.page_alignment_mask =
      ~static_cast<uint64_t>(sysconf(_SC_PAGE_SIZE) - 1);
  system_info.is_macos = true;
#elif LINUX
  system_info.logical_cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
  system_info.physical_cpu_count =
      system_info
          .logical_cpu_count;  // TODO(pvalkema): how to read this on Linux?
  system_info.os_page_size = static_cast<uint32_t>(getpagesize());
  system_info.page_alignment_mask =
      ~static_cast<uint64_t>(sysconf(_SC_PAGE_SIZE) - 1);
#endif
  system_info.suggested_total_thread_count =
      MIN(system_info.logical_cpu_count, MAX_THREAD_COUNT);

  // TODO(pvalkema): think about returning this instead of setting global state.
  global_system_info = system_info;
}

void init_thread_memory(int32_t logical_thread_index,
                        system_info_t* system_info) {
  static_cast<void>(logical_thread_index);

  uint32_t os_page_size = 0;
  if (system_info != nullptr) {
    os_page_size = system_info->os_page_size;
  }
  if (os_page_size == 0) {
    os_page_size = GetOsPageSize();
    if (system_info != nullptr) {
      system_info->os_page_size = os_page_size;
    }
  }

  // Allocate a private memory buffer
  uint64_t thread_memory_size = MEGABYTES(16);
  local_thread_memory = static_cast<thread_memory_t*>(
      malloc(thread_memory_size));  // how much actually needed?
  thread_memory_t* thread_memory = local_thread_memory;
  memset(thread_memory, 0, sizeof(thread_memory_t));
#if !WINDOWS
  // TODO(pvalkema): think about whether implement creation of async I/O events
  // is needed here
#endif
  thread_memory->thread_memory_raw_size = thread_memory_size;

  const std::uintptr_t thread_memory_addr =
      reinterpret_cast<std::uintptr_t>(thread_memory);
  const std::uintptr_t aligned_addr =
      ((thread_memory_addr + sizeof(thread_memory_t) + os_page_size - 1) /
       os_page_size) *
      os_page_size;  // round up to next page boundary
  thread_memory->aligned_rest_of_thread_memory =
      reinterpret_cast<void*>(aligned_addr);
  thread_memory->thread_memory_usable_size =
      thread_memory_size -
      static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(
                                thread_memory->aligned_rest_of_thread_memory) -
                            thread_memory_addr);
  init_arena(&thread_memory->temp_arena,
             thread_memory->thread_memory_usable_size,
             thread_memory->aligned_rest_of_thread_memory);
}

}  // extern "C"
