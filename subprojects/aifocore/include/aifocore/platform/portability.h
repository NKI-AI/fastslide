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

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_PLATFORM_PORTABILITY_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_PLATFORM_PORTABILITY_H_

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>

#if defined(_WIN32) || defined(_WIN64)
#define AIFOCORE_WINDOWS
#endif

#ifdef AIFOCORE_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <sys/types.h>
#include <windows.h>

// Map POSIX types/functions to Windows equivalents
using portable_stat_struct = struct _stat64;

namespace aifocore {

inline FILE* portable_fopen(const std::filesystem::path& path,
                            const char* mode) {
  // Convert mode to wstring
  std::wstring wmode;
  for (const char* p = mode; *p; ++p)
    wmode += static_cast<wchar_t>(*p);
  return _wfopen(path.c_str(), wmode.c_str());
}

inline int portable_fseek(FILE* stream, int64_t offset, int origin) {
  return _fseeki64(stream, offset, origin);
}

inline int64_t portable_ftell(FILE* stream) {
  return _ftelli64(stream);
}

inline int portable_open(const char* filename, int flags) {
  return ::_open(filename, flags);
}

inline int portable_open(const char* filename, int flags, int mode) {
  return ::_open(filename, flags, mode);
}

inline int portable_close(int fd) {
  return ::_close(fd);
}

inline int portable_fstat(int fd, portable_stat_struct* buffer) {
  return ::_fstat64(fd, buffer);
}

inline int portable_unlink(const char* filename) {
  return ::_unlink(filename);
}

inline ssize_t portable_pread(int fd, void* buf, size_t count,
                              uint64_t offset) {
  // Get Windows handle from file descriptor
  HANDLE handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
  if (handle == INVALID_HANDLE_VALUE) {
    return -1;
  }

  OVERLAPPED overlapped = {};
  overlapped.Offset = static_cast<DWORD>(offset);
  overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);

  DWORD bytes_read = 0;
  if (!::ReadFile(handle, buf, static_cast<DWORD>(count), &bytes_read,
                  &overlapped)) {
    // Check for EOF or other errors
    if (::GetLastError() == ERROR_HANDLE_EOF) {
      return 0;
    }
    return -1;
  }

  return static_cast<ssize_t>(bytes_read);
}

inline int portable_fileno(FILE* stream) {
  return ::_fileno(stream);
}

}  // namespace aifocore

#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif

#ifndef O_RDONLY
#define O_RDONLY _O_RDONLY
#endif

#else  // POSIX

#include <fcntl.h>
#include <unistd.h>

using portable_stat_struct = struct stat;

namespace aifocore {

inline FILE* portable_fopen(const std::filesystem::path& path,
                            const char* mode) {
  return std::fopen(path.c_str(), mode);
}

inline int portable_fseek(FILE* stream, int64_t offset, int origin) {
  return fseeko(stream, static_cast<off_t>(offset), origin);
}

inline int64_t portable_ftell(FILE* stream) {
  return static_cast<int64_t>(ftello(stream));
}

inline int portable_open(const char* filename, int flags) {
  return ::open(filename, flags);
}

inline int portable_open(const char* filename, int flags, int mode) {
  return ::open(filename, flags, mode);
}

inline int portable_close(int fd) {
  return ::close(fd);
}

inline int portable_fstat(int fd, portable_stat_struct* buffer) {
  return ::fstat(fd, buffer);
}

inline int portable_unlink(const char* filename) {
  return ::unlink(filename);
}

inline ssize_t portable_pread(int fd, void* buf, size_t count,
                              uint64_t offset) {
  return ::pread(fd, buf, count, static_cast<off_t>(offset));
}

inline int portable_fileno(FILE* stream) {
  return ::fileno(stream);
}

}  // namespace aifocore

#ifndef O_BINARY
#define O_BINARY 0
#endif

#endif  // AIFOCORE_WINDOWS

namespace aifocore {

inline size_t portable_fread(void* dest, size_t bytes_to_read, FILE* stream) {
  return std::fread(dest, 1, bytes_to_read, stream);
}

inline size_t portable_fwrite(const void* source, size_t bytes_to_write,
                              FILE* stream) {
  return std::fwrite(source, 1, bytes_to_write, stream);
}

inline int portable_fclose(FILE* stream) {
  return std::fclose(stream);
}

inline int64_t portable_filesize(FILE* stream) {
  if (stream == nullptr) {
    return -1;
  }
  const int fd = portable_fileno(stream);
  if (fd < 0) {
    return -1;
  }
  portable_stat_struct st{};
  if (portable_fstat(fd, &st) != 0) {
    return -1;
  }
  return static_cast<int64_t>(st.st_size);
}

}  // namespace aifocore

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_PLATFORM_PORTABILITY_H_
