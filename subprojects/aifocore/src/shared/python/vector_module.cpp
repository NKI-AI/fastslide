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
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>
#include "aifocore/shared/exceptions.h"
#include "aifocore/shared/python/vector.h"
#include "aifocore/shared/vector.h"

namespace py = pybind11;
using aifocore::shared::SharedVector;
using aifocore::shared::exceptions::ArraySizeError;
using aifocore::shared::exceptions::DataModifiedError;
using aifocore::shared::exceptions::InUseError;
using aifocore::shared::exceptions::MemoryError;
using aifocore::shared::exceptions::OutOfMemoryError;
namespace bi = boost::interprocess;

PYBIND11_MODULE(vector, m) {
  py::class_<SharedVector>(m, "SharedVector")
      .def(py::init<const std::string&, size_t, size_t>(), py::arg("name"),
           py::arg("chunk_size") = 1024 * 1024 * 1024,
           py::arg("max_memory_size") = 1024 * 1024 * 10)
      .def("append", &aifocore::shared::python::AppendPython<float>,
           py::arg("array"))
      .def("get", &aifocore::shared::python::GetPython<float>, py::arg("index"))
      .def("replace", &aifocore::shared::python::ReplacePython<float>,
           py::arg("index"), py::arg("array"))
      .def("size", &SharedVector::size)
      .def("__len__", &SharedVector::size)
      .def("get_chunk_ref_count", &SharedVector::GetChunkRefCount,
           py::arg("index"))
      .def(
          "get_chunk_shape",
          [](const SharedVector& self, size_t index) {
            std::vector<size_t> vec = self.GetChunkShape(index);
            py::tuple result = py::cast(vec);
            return result;
          },
          "Get the shape of a chunk as a tuple")
      .def("get_chunk_pointer", &SharedVector::GetChunkPointer,
           py::arg("index"))
      .def_property_readonly("free_memory", &SharedVector::GetFreeMemory)
      .def_property_readonly("ref_count", &SharedVector::GetRefCount);

  // Expose the remove_shared_memory function
  m.def("remove_shared_memory", [](const std::string& name) {
    bi::shared_memory_object::remove(name.c_str());
  });

  // Register exceptions
  py::register_exception<MemoryError>(m, "MemoryError");
  py::register_exception<OutOfMemoryError>(m, "OutOfMemoryError");
  py::register_exception<InUseError>(m, "InUseError");
  py::register_exception<ArraySizeError>(m, "ArraySizeError");
  py::register_exception<DataModifiedError>(m, "DataModifiedError");
}
