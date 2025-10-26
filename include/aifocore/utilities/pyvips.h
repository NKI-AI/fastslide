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
#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_PYVIPS_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_PYVIPS_H_

#include <vips/vips8>

#include <pybind11/cast.h>
#include <pybind11/detail/type_caster_base.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>
#include <memory>
#include <mutex>
#include <utility>

namespace py = pybind11;

static std::once_flag pyvips_init_flag;

std::pair<py::object, py::object> InitPyvips() {
  static py::object pyvips_module;
  static py::object ffi_object;

  std::call_once(pyvips_init_flag, []() {
    pyvips_module = py::module::import("pyvips");
    ffi_object = pyvips_module.attr("ffi");
  });

  return {pyvips_module, ffi_object};
}

class VImageWrapper {
 private:
  ::vips::VImage vimage;

 public:
  explicit VImageWrapper(const ::vips::VImage& img) : vimage(img) {}

  ~VImageWrapper() {}

  py::object GetImage() const {
    auto [pyvips_module, ffi_object] = InitPyvips();
    VipsImage* image = vimage.get_image();
    uintptr_t ptr_value = reinterpret_cast<uintptr_t>(image);
    py::object cffi_ptr = ffi_object.attr("cast")("VipsImage *", ptr_value);
    return pyvips_module.attr("Image")(cffi_ptr);
  }
};

namespace pybind11::detail {
template <>
struct type_caster<std::shared_ptr<vips::VImage>> {
 public:
  PYBIND11_TYPE_CASTER(std::shared_ptr<vips::VImage>,
                       _("std::shared_ptr<vips::VImage>"));

  // Python -> C++
  bool load(handle src, bool) {
    if (!src) {
      return false;
    }
  }

  // C++ -> Python
  static handle cast(const std::shared_ptr<vips::VImage>& src,
                     return_value_policy /* policy */, handle /* parent */) {
    if (!src) {
      return py::none().release();
    }

    auto [pyvips_module, ffi_object] = InitPyvips();

    // Get the underlying VipsImage pointer
    VipsImage* image = src->get_image();
    uintptr_t ptr_value = reinterpret_cast<uintptr_t>(image);

    // Convert to a pyvips.Image
    py::object cffi_ptr = ffi_object.attr("cast")("VipsImage *", ptr_value);
    return pyvips_module.attr("Image")(cffi_ptr).release();
  }
};
}  // namespace pybind11::detail

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_PYVIPS_H_
