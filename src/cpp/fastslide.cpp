/**
 * @file fastslide.cpp
 * @brief Implementation of C++20 wrapper for OpenSlide library.
 */

#include "fastslide.h"

#include <openslide.h>
#include <glib.h>  // For GError definition

#include <algorithm>
#include <cstring>
#include <utility>
#include <fstream>
#include <fmt/core.h>

// Declare internal OpenSlide functions
extern "C" {
  // Functions from openslide-decode-tifflike.h
  _openslide_tifflike* _openslide_tifflike_create(const char *filename, GError **err);
  void _openslide_tifflike_destroy(_openslide_tifflike *tl);
  
  // Functions and data from openslide-private.h
  extern const _openslide_format * const _openslide_formats[];
}

// Define the partial structure of _openslide_format (based on openslide-private.h)
// This is necessary for accessing its fields
struct _openslide_format_partial {
  const char *name;
  const char *vendor;
  bool (*detect)(const char *filename, _openslide_tifflike *tl, GError **err);
  // Other fields omitted
};

namespace fastslide {


// Enhanced format detection function - uses public API plus some heuristics
Slide::FormatInfo Slide::DetectFormat(const std::string& filename) {
  FormatInfo info;
  info.is_valid = false;

  return info;
}

// SlideCache implementation
std::shared_ptr<SlideCache> SlideCache::Create(int64_t cache_size) {
  return std::make_shared<SlideCache>(cache_size);
}

SlideCache::SlideCache(int64_t cache_size) : cache_size_(cache_size) {}

SlideCache::~SlideCache() {}

int64_t SlideCache::GetSize() const {
  return cache_size_;
}

// Methods for Slide class
Slide::SlideError::SlideError(const std::string& message) : std::runtime_error(message) {}

std::string Slide::GetVersion() {
  const char* version = openslide_get_version();
  return version ? version : "";
}

std::string Slide::DetectVendor(const std::string& filename) {
  const char* vendor = openslide_detect_vendor(filename.c_str());
  return vendor ? vendor : "";
}

std::shared_ptr<Slide> Slide::Open(const std::string& filename, std::shared_ptr<SlideCache> cache) {
  auto slide = std::make_shared<Slide>(filename, cache);
  
  // Check for errors
  if (slide->error_.empty()) {
    // Copy original OpenSlide properties into C++ map
    const char* const* property_names = openslide_get_property_names(slide->osr_);
    if (property_names) {
      for (int i = 0; property_names[i] != nullptr; i++) {
        const char* name = property_names[i];
        const char* value = openslide_get_property_value(slide->osr_, name);
        if (value) {
          // Store the original OpenSlide property
          slide->properties_[name] = value;
          
          // Also store with fastslide prefix by replacing openslide prefix
          std::string prop_name(name);
          if (prop_name.compare(0, 10, "openslide.") == 0) {
            std::string fastslide_prop = "fastslide." + prop_name.substr(10);
            slide->properties_[fastslide_prop] = value;
          }
        }
      }
    }

    // Get level count and properties
    int32_t level_count = openslide_get_level_count(slide->osr_);
    slide->properties_[PROPERTY_NAME_LEVEL_COUNT] = std::to_string(level_count);

    // Add level dimensions and downsample properties
    for (int32_t i = 0; i < level_count; i++) {
      int64_t w, h;
      openslide_get_level_dimensions(slide->osr_, i, &w, &h);
      
      // Set level dimensions property
      slide->properties_[fmt::format(PROPERTY_NAME_TEMPLATE_LEVEL_WIDTH, i)] = std::to_string(w);
      slide->properties_[fmt::format(PROPERTY_NAME_TEMPLATE_LEVEL_HEIGHT, i)] = std::to_string(h);
      
      // Set level downsample property
      double downsample = openslide_get_level_downsample(slide->osr_, i);
      slide->properties_[fmt::format(PROPERTY_NAME_TEMPLATE_LEVEL_DOWNSAMPLE, i)] = std::to_string(downsample);
    }

    // Add associated images properties
    const char* const* associated_image_names = openslide_get_associated_image_names(slide->osr_);
    if (associated_image_names) {
      for (int i = 0; associated_image_names[i] != nullptr; i++) {
        const char* name = associated_image_names[i];
        int64_t w, h;
        openslide_get_associated_image_dimensions(slide->osr_, name, &w, &h);
        
        // Set associated image dimensions
        slide->properties_[fmt::format(PROPERTY_NAME_TEMPLATE_ASSOCIATED_WIDTH, name)] = std::to_string(w);
        slide->properties_[fmt::format(PROPERTY_NAME_TEMPLATE_ASSOCIATED_HEIGHT, name)] = std::to_string(h);
        
        // Add ICC profile size if available
        int64_t icc_size = openslide_get_associated_image_icc_profile_size(slide->osr_, name);
        if (icc_size > 0) {
          slide->properties_[fmt::format(PROPERTY_NAME_TEMPLATE_ASSOCIATED_ICC_SIZE, name)] = std::to_string(icc_size);
        }
      }
    }

    // Check for slide ICC profile
    int64_t icc_size = openslide_get_icc_profile_size(slide->osr_);
    if (icc_size > 0) {
      slide->properties_[PROPERTY_NAME_ICC_SIZE] = std::to_string(icc_size);
    }
    
    // Make sure we have all the standard properties
    const char* vendor = openslide_get_property_value(slide->osr_, "openslide.vendor");
    if (vendor) {
      slide->properties_[PROPERTY_NAME_VENDOR] = vendor;
    }
    
    // Copy other standard properties if they exist
    const char* bg_color = openslide_get_property_value(slide->osr_, "openslide.background-color");
    if (bg_color) {
      slide->properties_[PROPERTY_NAME_BACKGROUND_COLOR] = bg_color;
    }
    
    const char* bounds_height = openslide_get_property_value(slide->osr_, "openslide.bounds-height");
    if (bounds_height) {
      slide->properties_[PROPERTY_NAME_BOUNDS_HEIGHT] = bounds_height;
    }
    
    const char* bounds_width = openslide_get_property_value(slide->osr_, "openslide.bounds-width");
    if (bounds_width) {
      slide->properties_[PROPERTY_NAME_BOUNDS_WIDTH] = bounds_width;
    }
    
    const char* bounds_x = openslide_get_property_value(slide->osr_, "openslide.bounds-x");
    if (bounds_x) {
      slide->properties_[PROPERTY_NAME_BOUNDS_X] = bounds_x;
    }
    
    const char* bounds_y = openslide_get_property_value(slide->osr_, "openslide.bounds-y");
    if (bounds_y) {
      slide->properties_[PROPERTY_NAME_BOUNDS_Y] = bounds_y;
    }
    
    const char* comment = openslide_get_property_value(slide->osr_, "openslide.comment");
    if (comment) {
      slide->properties_[PROPERTY_NAME_COMMENT] = comment;
    }
    
    const char* mpp_x = openslide_get_property_value(slide->osr_, "openslide.mpp-x");
    if (mpp_x) {
      slide->properties_[PROPERTY_NAME_MPP_X] = mpp_x;
    }
    
    const char* mpp_y = openslide_get_property_value(slide->osr_, "openslide.mpp-y");
    if (mpp_y) {
      slide->properties_[PROPERTY_NAME_MPP_Y] = mpp_y;
    }
    
    const char* objective_power = openslide_get_property_value(slide->osr_, "openslide.objective-power");
    if (objective_power) {
      slide->properties_[PROPERTY_NAME_OBJECTIVE_POWER] = objective_power;
    }
    
    const char* quickhash1 = openslide_get_property_value(slide->osr_, "openslide.quickhash-1");
    if (quickhash1) {
      slide->properties_[PROPERTY_NAME_QUICKHASH1] = quickhash1;
    }
  }
  
  return slide;
}

Slide::Slide(const std::string& filename, std::shared_ptr<SlideCache> cache)
    : filename_(filename), cache_(cache) {
  // Open the slide
  osr_ = openslide_open(filename.c_str());
  
  // Check for errors
  const char* error = openslide_get_error(osr_);
  if (error) {
    error_ = error;
  }
}

Slide::~Slide() {
  if (osr_) {
    openslide_close(osr_);
    osr_ = nullptr;
  }
}

void Slide::CheckError() const {
  if (osr_) {
    const char* error = openslide_get_error(osr_);
    if (error) {
      error_ = error;
    }
  }
  
  if (!error_.empty()) {
    throw SlideError(error_);
  }
}

bool Slide::HasError() const {
  return !error_.empty();
}

std::string Slide::GetErrorMessage() const {
  return error_;
}

int32_t Slide::GetLevelCount() const {
  CheckError();
  
  int32_t count = openslide_get_level_count(osr_);
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return count;
}

std::pair<int64_t, int64_t> Slide::GetLevelDimensions(int32_t level) const {
  CheckError();
  
  int64_t w, h;
  openslide_get_level_dimensions(osr_, level, &w, &h);
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return std::make_pair(w, h);
}

std::pair<int64_t, int64_t> Slide::GetLevel0Dimensions() const {
  CheckError();
  
  int64_t w, h;
  openslide_get_level0_dimensions(osr_, &w, &h);
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return std::make_pair(w, h);
}

double Slide::GetLevelDownsample(int32_t level) const {
  CheckError();
  
  double downsample = openslide_get_level_downsample(osr_, level);
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return downsample;
}

int32_t Slide::GetBestLevelForDownsample(double downsample) const {
  CheckError();
  
  int32_t level = openslide_get_best_level_for_downsample(osr_, downsample);
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return level;
}

void Slide::ReadRegion(uint32_t* dest,
                      int64_t x, int64_t y,
                      int32_t level,
                      int64_t width, int64_t height) const {
  CheckError();
  
  openslide_read_region(osr_, dest, x, y, level, width, height);
  
  // Check for errors after calling OpenSlide
  CheckError();
}

std::vector<std::string> Slide::GetPropertyNames() const {
  CheckError();
  
  std::vector<std::string> names;
  const char* const* property_names = openslide_get_property_names(osr_);
  
  if (property_names) {
    for (int i = 0; property_names[i] != nullptr; i++) {
      names.push_back(property_names[i]);
    }
  }
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return names;
}

std::string Slide::GetPropertyValue(const std::string& name) const {
  CheckError();
  
  const char* value = openslide_get_property_value(osr_, name.c_str());
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return value ? value : "";
}

std::map<std::string, std::string> Slide::GetProperties() const {
  CheckError();
  
  // Return cached properties if available
  if (!properties_.empty()) {
    return properties_;
  }
  
  // Otherwise build the map
  std::map<std::string, std::string> properties;
  auto property_names = GetPropertyNames();
  
  for (const auto& name : property_names) {
    properties[name] = GetPropertyValue(name);
  }
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return properties;
}

std::vector<std::string> Slide::GetAssociatedImageNames() const {
  CheckError();
  
  std::vector<std::string> names;
  const char* const* image_names = openslide_get_associated_image_names(osr_);
  
  if (image_names) {
    for (int i = 0; image_names[i] != nullptr; i++) {
      names.push_back(image_names[i]);
    }
  }
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return names;
}

std::pair<int64_t, int64_t> Slide::GetAssociatedImageDimensions(const std::string& name) const {
  CheckError();
  
  int64_t w, h;
  openslide_get_associated_image_dimensions(osr_, name.c_str(), &w, &h);
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return std::make_pair(w, h);
}

void Slide::ReadAssociatedImage(const std::string& name, uint32_t* dest) const {
  CheckError();
  
  openslide_read_associated_image(osr_, name.c_str(), dest);
  
  // Check for errors after calling OpenSlide
  CheckError();
}

int64_t Slide::GetAssociatedImageICCProfileSize(const std::string& name) const {
  CheckError();
  
  int64_t size = openslide_get_associated_image_icc_profile_size(osr_, name.c_str());
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return size;
}

void Slide::ReadAssociatedImageICCProfile(const std::string& name, uint8_t* dest) const {
  CheckError();
  
  openslide_read_associated_image_icc_profile(osr_, name.c_str(), dest);
  
  // Check for errors after calling OpenSlide
  CheckError();
}

int64_t Slide::GetICCProfileSize() const {
  CheckError();
  
  int64_t size = openslide_get_icc_profile_size(osr_);
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return size;
}

void Slide::ReadICCProfile(uint8_t* dest) const {
  CheckError();
  
  openslide_read_icc_profile(osr_, dest);
  
  // Check for errors after calling OpenSlide
  CheckError();
}

std::string Slide::GetStoryboardFile() const {
  CheckError();
  
  const char* file = nullptr;
  // This function is not in all OpenSlide versions, so we'll return empty for now
  // const char* file = openslide_get_storyboard_file(osr_);
  
  // Check for errors after calling OpenSlide
  CheckError();
  
  return file ? file : "";
}

}  // namespace fastslide 
