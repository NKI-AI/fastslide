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

#pragma once
#include <cstdio>
#include "readers/isyntax/third_party/platform/arena.h"
#include "readers/isyntax/third_party/platform/common.h"
#include "readers/isyntax/third_party/utils/mathutils.h"

#if WINDOWS
#include <windows.h>
#else
#include <semaphore.h>
#include <unistd.h>
#endif

#ifdef TARGET_EMSCRIPTEN
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_THREAD_COUNT 128

typedef struct mem_t {
  size_t len;
  size_t capacity;
  uint8_t data[0];
} mem_t;

#if WINDOWS
typedef int file_handle_t;
typedef FILE* file_stream_t;
#else
typedef int file_handle_t;
typedef FILE* file_stream_t;
#endif

#define MAX_ASYNC_IO_EVENTS 32

typedef struct {
#if WINDOWS
  HANDLE async_io_events[MAX_ASYNC_IO_EVENTS];
  int32_t async_io_index;
  OVERLAPPED overlapped;
#else
  // TODO(jonasteuwen): implement this
#endif
  uint64_t thread_memory_raw_size;
  uint64_t thread_memory_usable_size;  // free space from
                                       // aligned_rest_of_thread_memory onward
  void* aligned_rest_of_thread_memory;
  uint32_t pbo;
  arena_t temp_arena;
} thread_memory_t;

typedef struct system_info_t {
  uint32_t os_page_size;
  uint64_t page_alignment_mask;
  int32_t physical_cpu_count;
  int32_t logical_cpu_count;
  int32_t suggested_total_thread_count;
  bool is_macos;
} system_info_t;

void get_system_info(bool verbose);

void init_thread_memory(int32_t logical_thread_index,
                        system_info_t* system_info);

// globals
#if defined(PLATFORM_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern THREAD_LOCAL thread_memory_t* local_thread_memory;

static inline temp_memory_t begin_temp_memory_on_local_thread() {
  return begin_temp_memory(&local_thread_memory->temp_arena);
}

extern int g_argc;
extern const char** g_argv;
extern system_info_t global_system_info;
extern int32_t global_worker_thread_count;
extern int32_t global_active_worker_thread_count;
// extern work_queue_t global_completion_queue;

extern bool is_verbose_mode INIT(= false);

#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif
