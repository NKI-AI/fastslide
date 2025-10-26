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
#include <aifocore/tiling/grid.h>
#include <aifocore/tiling/python/grid_wrapper.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace py = pybind11;
using aifocore::tiling::GridOrder;
using aifocore::tiling::TilingMode;
using aifocore::tiling::python::GridWrapper;
using aifocore::tiling::python::TilesGridCoordinatesWrapper;

PYBIND11_MODULE(_grid, m) {
  py::enum_<TilingMode>(m, "TilingMode")
      .value("skip", TilingMode::kSkip)
      .value("overflow", TilingMode::kOverflow)
      .export_values();

  py::enum_<GridOrder>(m, "GridOrder")
      .value("C", GridOrder::kC)
      .value("F", GridOrder::kF)
      .export_values();

  m.def("tiles_grid_coordinates", &TilesGridCoordinatesWrapper,
        "Generate grid coordinates with automatic type selection.",
        py::arg("size"), py::arg("tile_size"),
        py::arg("tile_overlap") = py::list(),
        py::arg("mode") = TilingMode::kSkip);

  py::class_<GridWrapper>(m, "Grid")
      .def(py::init<const py::list&, const py::object&>(),
           py::arg("coordinates"), py::arg("order") = GridOrder::kC,
           "Create a Grid from a list of coordinates and order.")
      .def_static(
          "from_tiling", &GridWrapper::FromTiling, py::arg("offset"),
          py::arg("size"), py::arg("tile_size"),
          py::arg("tile_overlap") = std::vector<int>{0},
          // TODO(j.teuwen): This is problematic because it will be actually "0"
          py::arg("mode") = TilingMode::kSkip, py::arg("order") = GridOrder::kC,
          "Create a Grid from tiling parameters.")
      .def_property_readonly("order", &GridWrapper::Order,
                             "Grid order ('C' or 'F').")
      .def_property_readonly("size", &GridWrapper::Size,
                             "Size of the grid in each dimension.")
      .def("__len__", &GridWrapper::Length,
           "Total number of points in the grid.")
      .def(
          "__getitem__",
          [](const GridWrapper& grid_wrapper,
             const py::object& index) -> py::object {
            if (py::isinstance<py::int_>(index)) {
              // Return a single tuple for an integer index
              return grid_wrapper.GetItem(index.cast<size_t>());
            } else if (py::isinstance<py::slice>(index)) {
              // Handle slicing
              py::list result;
              py::slice slice = index.cast<py::slice>();
              size_t start, stop, step, length;

              if (!slice.compute(grid_wrapper.Length(), &start, &stop, &step,
                                 &length)) {
                throw py::error_already_set();
              }

              for (size_t i = start; i < stop; i += step) {
                result.append(
                    grid_wrapper.GetItem(i));  // Use GetItem for each index
              }
              return result;  // Return a list of tuples
            } else {
              throw std::invalid_argument("Index must be an integer or slice");
            }
          },
          "Get the coordinate at the specified index or range.")
      .def(
          "__iter__",
          [](const GridWrapper& grid_wrapper) {
            return py::make_iterator(grid_wrapper.begin(), grid_wrapper.end());
          },
          py::keep_alive<0, 1>())  // Keep object alive while iterator exists
      .def("__repr__", [](const GridWrapper& grid_wrapper) {
        // Get string representation of GridOrder
        std::string order_str;
        if (grid_wrapper.Order() == GridOrder::kC) {
          order_str = "C";
        } else if (grid_wrapper.Order() == GridOrder::kF) {
          order_str = "F";
        } else {
          order_str = "Unknown";
        }

        // Get the memory address as a hexadecimal string
        std::ostringstream oss;
        oss << std::hex << std::showbase
            << reinterpret_cast<std::uintptr_t>(&grid_wrapper);

        std::string result = "<Grid with ";
        result += std::to_string(grid_wrapper.Length());
        result += " points in ";
        result += order_str;
        result += " order at ";
        result += oss.str();
        result += ">";
        return result;
      });
}
