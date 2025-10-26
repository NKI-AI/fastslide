// Copyright 2025 Jonas Teuwen & Joren Brunekreef. All Rights Reserved.
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

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "absl/status/status.h"

#include "ahcore/pathology/config/inference_config.h"
#include "ahcore/pathology/inference_engine.h"

namespace py = pybind11;

using aifo::pathology::config::InferenceConfig;
using aifo::pathology::inference::InferenceEngine;

namespace {

void ThrowPyErrorFromStatus(const absl::Status& status) {
  if (status.code() == absl::StatusCode::kInvalidArgument) {
    throw py::value_error(status.ToString());
  }
  // Raise generic runtime error for all other cases
  throw std::runtime_error(status.ToString());
}

}  // namespace

PYBIND11_MODULE(inference_engine, m) {
  spdlog::set_level(
      spdlog::level::warn);  // prevent spdlog info from printing to stdout

  m.doc() = "Python bindings for ahcore InferenceEngine";

  m.def("hello_world", []() { return "Hello, World!"; });
  py::class_<InferenceConfig>(m, "InferenceConfig")
      .def(py::init<>([]() { return InferenceConfig{}; }))
      .def(py::init<>(
               [](const std::filesystem::path& output_dir,
                  const std::filesystem::path& model_path,
                  double mask_threshold, int batch_size, double tiff_tile_size,
                  std::optional<std::string> device, bool create_thumbnail) {
                 InferenceConfig cfg;
                 cfg.output_dir = output_dir;
                 cfg.model_path = model_path;
                 cfg.mask_threshold = mask_threshold;
                 cfg.batch_size = batch_size;
                 cfg.tiff_tile_size = tiff_tile_size;
                 cfg.device = device;
                 cfg.create_thumbnail = create_thumbnail;
                 return cfg;
               }),
           py::arg("output_dir"), py::arg("model_path"),
           py::arg("mask_threshold") = 0.0, py::arg("batch_size") = 4,
           py::arg("tiff_tile_size") = 512.0, py::arg("device") = std::nullopt,
           py::arg("create_thumbnail") = false)
      .def_readwrite("output_dir", &InferenceConfig::output_dir,
                     "Path to the output directory")
      .def_readwrite("model_path", &InferenceConfig::model_path,
                     "Path to the model pack (.zip)")
      .def_readwrite("mask_threshold", &InferenceConfig::mask_threshold,
                     "Foreground mask threshold")
      .def_readwrite("batch_size", &InferenceConfig::batch_size,
                     "Batch size for inference")
      .def_readwrite("tiff_tile_size", &InferenceConfig::tiff_tile_size,
                     "TIFF tile size for output")
      .def_readwrite("device", &InferenceConfig::device,
                     "Device string: 'cpu', 'cuda', or 'mps'")
      .def_readwrite("create_thumbnail", &InferenceConfig::create_thumbnail,
                     "Create a thumbnail of the overlay");

  py::class_<InferenceEngine>(m, "InferenceEngine")
      // Construct via the factory and return by value (pybind11 will move)
      .def(py::init<>([](const InferenceConfig& config) {
             auto result = InferenceEngine::Create(config);
             if (!result.ok()) {
               ThrowPyErrorFromStatus(result.status());
             }
             return std::move(result).value();
           }),
           py::arg("config"))
      .def(
          "process_image",
          [](InferenceEngine& self, const std::filesystem::path& image_path) {
            absl::Status status = self.ProcessImage(image_path);
            if (!status.ok()) {
              ThrowPyErrorFromStatus(status);
            }
          },
          py::arg("image_path"),
          "Run inference for a single image without a mask, writing output to"
          " the configured output_dir")
      .def(
          "process_image",
          [](InferenceEngine& self, const std::filesystem::path& image_path,
             const std::filesystem::path& mask_path) {
            absl::Status status = self.ProcessImage(image_path, mask_path);
            if (!status.ok()) {
              ThrowPyErrorFromStatus(status);
            }
          },
          py::arg("image_path"), py::arg("mask_path"),
          "Run inference for a single image with a foreground mask, writing"
          " output to the configured output_dir")
      .def(
          "set_progress_callback",
          [](InferenceEngine& self, py::object py_callback) {
            if (py_callback.is_none()) {
              self.SetProgressCallback(nullptr);
              return;
            }
            // Keep a shared reference to the Python callback to ensure its
            // lifetime while the engine is alive.
            auto cb_holder =
                std::make_shared<py::object>(std::move(py_callback));
            self.SetProgressCallback([cb_holder](int current_batch,
                                                 int total_batches,
                                                 int tile_index) {
              try {
                py::gil_scoped_acquire gil;
                (*cb_holder)(current_batch, total_batches, tile_index);
              } catch (const py::error_already_set& e) {
                // Print the Python exception but do not crash the C++ side.
                py::print("Progress callback error:", e.what());
              }
            });
          },
          py::arg("callback"),
          "Set a Python progress callback with signature (current, total, "
          "tile_index).\n"
          "Pass None to clear the callback.");
}
