/**
 * @file fastslide.cpp
 * @brief Implementation of C++20 wrapper for OpenSlide library.
 */

#include "fastslide.h"

#include <openslide.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <fstream>
#include <cstdio>  // For snprintf

namespace fastslide {

// Helper function to format property names
std::string FormatPropertyName(const char* format, int value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), format, value);
  return std::string(buffer);
}

std::string FormatPropertyName(const char* format, const std::string& value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), format, value.c_str());
  return std::string(buffer);
}

// SlideCache implementation
std::shared_ptr<SlideCache> SlideCache::Create(size_t capacity) {
  openslide_cache_t* cache = openslide_cache_create(capacity);
  if (!cache) {
    throw Slide::SlideError("Failed to create slide cache");
  }
  return std::shared_ptr<SlideCache>(new SlideCache(cache));
}

SlideCache::SlideCache(openslide_cache_t* cache) : cache_(cache) {}

SlideCache::~SlideCache() {
  if (cache_) {
    openslide_cache_release(cache_);
    cache_ = nullptr;
  }
}

// Methods for Slide class
std::string Slide::DetectVendor(const std::string& filename) {
  const char* vendor = openslide_detect_vendor(filename.c_str());
  return vendor ? vendor : "";
}

std::shared_ptr<Slide> Slide::Open(const std::string& filename, std::shared_ptr<SlideCache> cache) {
  std::shared_ptr<Slide> slide(new Slide());
  
  // Check if the file exists and is valid
  if (!std::ifstream(filename).good()) {
    slide->error_ = "File does not exist or cannot be accessed: " + filename;
    return slide;
  }
  
  // Check vendor
  std::string vendor = DetectVendor(filename);
  if (vendor.empty()) {
    slide->error_ = "Unrecognized file format: " + filename;
    return slide;
  }
  
  // Open the slide
  openslide_t* osr = openslide_open(filename.c_str());
  if (!osr) {
    slide->error_ = "Failed to open slide file: " + filename;
    return slide;
  }
  
  slide->osr_ = osr;
  slide->filename_ = filename;
  
  // Set cache if provided
  if (cache && slide->osr_) {
    openslide_set_cache(slide->osr_, cache->cache_);
  }
  
  // Check for OpenSlide errors
  const char* error = openslide_get_error(osr);
  if (error) {
    slide->error_ = error;
  }
  
  // Only copy properties if there is no error
  if (slide->error_.empty()) {
    // Add vendor information
    slide->properties_[PROPERTY_NAME_VENDOR] = vendor;
    
    // Add level count property
    int32_t level_count = openslide_get_level_count(osr);
    slide->properties_[PROPERTY_NAME_LEVEL_COUNT] = std::to_string(level_count);
    
    // Get level 0 dimensions to determine the bounds
    int64_t width = 0, height = 0;
    openslide_get_level0_dimensions(osr, &width, &height);
    
    // Add bounds properties - using actual dimensions as bounds
    slide->properties_[PROPERTY_NAME_BOUNDS_WIDTH] = std::to_string(width);
    slide->properties_[PROPERTY_NAME_BOUNDS_HEIGHT] = std::to_string(height);
    
    // Process each level
    for (int32_t i = 0; i < level_count; i++) {
      openslide_get_level_dimensions(osr, i, &width, &height);
      double downsample = openslide_get_level_downsample(osr, i);
      
      // Add level dimension properties
      slide->properties_[FormatPropertyName(PROPERTY_NAME_TEMPLATE_LEVEL_WIDTH, i)] = std::to_string(width);
      slide->properties_[FormatPropertyName(PROPERTY_NAME_TEMPLATE_LEVEL_HEIGHT, i)] = std::to_string(height);
      slide->properties_[FormatPropertyName(PROPERTY_NAME_TEMPLATE_LEVEL_DOWNSAMPLE, i)] = std::to_string(downsample);
    }
    
    // Process associated images
    const char* const* associated_image_names = openslide_get_associated_image_names(osr);
    for (int i = 0; associated_image_names[i] != nullptr; i++) {
      std::string name = associated_image_names[i];
      int64_t width = 0, height = 0;
      
      openslide_get_associated_image_dimensions(osr, name.c_str(), &width, &height);
      
      slide->properties_[FormatPropertyName(PROPERTY_NAME_TEMPLATE_ASSOCIATED_WIDTH, name)] = std::to_string(width);
      slide->properties_[FormatPropertyName(PROPERTY_NAME_TEMPLATE_ASSOCIATED_HEIGHT, name)] = std::to_string(height);
      
      // Check for ICC profile
      int64_t icc_size = openslide_get_associated_image_icc_profile_size(osr, name.c_str());
      if (icc_size > 0) {
        slide->properties_[FormatPropertyName(PROPERTY_NAME_TEMPLATE_ASSOCIATED_ICC_SIZE, name)] = std::to_string(icc_size);
      }
    }
    
    // Check for slide ICC profile
    int64_t icc_size = openslide_get_icc_profile_size(osr);
    if (icc_size > 0) {
      slide->properties_[PROPERTY_NAME_ICC_SIZE] = std::to_string(icc_size);
    }
    
    // Copy all properties from OpenSlide
    const char* const* property_names = openslide_get_property_names(osr);
    for (int i = 0; property_names[i] != nullptr; i++) {
      const char* name = property_names[i];
      const char* value = openslide_get_property_value(osr, name);
      if (value) {
        slide->properties_[name] = value;
      }
    }
  }
  
  return slide;
}

Slide::Slide() : osr_(nullptr) {}

Slide::~Slide() {
  if (osr_) {
    openslide_close(osr_);
    osr_ = nullptr;
  }
}

Slide::Slide(Slide&& other)
    : osr_(other.osr_),
      filename_(std::move(other.filename_)),
      error_(std::move(other.error_)),
      properties_(std::move(other.properties_)) {
  other.osr_ = nullptr;
}

Slide& Slide::operator=(Slide&& other) {
  if (this != &other) {
    if (osr_) {
      openslide_close(osr_);
    }
    
    osr_ = other.osr_;
    filename_ = std::move(other.filename_);
    error_ = std::move(other.error_);
    properties_ = std::move(other.properties_);
    
    other.osr_ = nullptr;
  }
  return *this;
}

void Slide::CheckError() const {
  if (!osr_) {
    if (error_.empty()) {
      throw SlideError("Slide not initialized");
    } else {
      throw SlideError(error_);
    }
  }
  
  const char* error = openslide_get_error(osr_);
  if (error) {
    throw SlideError(error);
  }
}

bool Slide::HasError() const {
  if (!osr_) {
    return !error_.empty();
  }
  return openslide_get_error(osr_) != nullptr;
}

std::string Slide::GetErrorMessage() const {
  if (!osr_) {
    return error_;
  }
  
  const char* error = openslide_get_error(osr_);
  return error ? error : "";
}

int32_t Slide::GetLevelCount() const {
  CheckError();
  return openslide_get_level_count(osr_);
}

std::pair<int64_t, int64_t> Slide::GetLevelDimensions(int32_t level) const {
  CheckError();
  int64_t width = 0, height = 0;
  openslide_get_level_dimensions(osr_, level, &width, &height);
  return std::make_pair(width, height);
}

std::pair<int64_t, int64_t> Slide::GetLevel0Dimensions() const {
  CheckError();
  int64_t width = 0, height = 0;
  openslide_get_level0_dimensions(osr_, &width, &height);
  return std::make_pair(width, height);
}

double Slide::GetLevelDownsample(int32_t level) const {
  CheckError();
  return openslide_get_level_downsample(osr_, level);
}

int32_t Slide::GetBestLevelForDownsample(double downsample) const {
  CheckError();
  return openslide_get_best_level_for_downsample(osr_, downsample);
}

void Slide::ReadRegion(void* dest, int64_t x, int64_t y, int32_t level, int64_t width, int64_t height) const {
  CheckError();
  openslide_read_region(osr_, static_cast<uint32_t*>(dest), x, y, level, width, height);
  
  // Check for errors that may have occurred during reading
  const char* error = openslide_get_error(osr_);
  if (error) {
    throw SlideError(error);
  }
}

std::vector<std::string> Slide::GetPropertyNames() const {
  CheckError();
  std::vector<std::string> names;
  const char* const* property_names = openslide_get_property_names(osr_);
  for (int i = 0; property_names[i] != nullptr; i++) {
    names.push_back(property_names[i]);
  }
  return names;
}

std::string Slide::GetPropertyValue(const std::string& name) const {
  CheckError();
  const char* value = openslide_get_property_value(osr_, name.c_str());
  return value ? value : "";
}

std::map<std::string, std::string> Slide::GetProperties() const {
  CheckError();
  
  // Return cached properties if available
  if (!properties_.empty()) {
    return properties_;
  }
  
  // Otherwise load properties on demand
  std::map<std::string, std::string> properties;
  std::vector<std::string> names = GetPropertyNames();
  for (const auto& name : names) {
    std::string value = GetPropertyValue(name);
    if (!value.empty()) {
      properties[name] = value;
    }
  }
  
  return properties;
}

std::vector<std::string> Slide::GetAssociatedImageNames() const {
  CheckError();
  std::vector<std::string> names;
  const char* const* image_names = openslide_get_associated_image_names(osr_);
  for (int i = 0; image_names[i] != nullptr; i++) {
    names.push_back(image_names[i]);
  }
  return names;
}

std::pair<int64_t, int64_t> Slide::GetAssociatedImageDimensions(const std::string& name) const {
  CheckError();
  int64_t width = 0, height = 0;
  openslide_get_associated_image_dimensions(osr_, name.c_str(), &width, &height);
  return std::make_pair(width, height);
}

void Slide::ReadAssociatedImage(const std::string& name, void* dest) const {
  CheckError();
  openslide_read_associated_image(osr_, name.c_str(), static_cast<uint32_t*>(dest));
  
  // Check for errors that may have occurred during reading
  const char* error = openslide_get_error(osr_);
  if (error) {
    throw SlideError(error);
  }
}

int64_t Slide::GetAssociatedImageICCProfileSize(const std::string& name) const {
  CheckError();
  return openslide_get_associated_image_icc_profile_size(osr_, name.c_str());
}

void Slide::ReadAssociatedImageICCProfile(const std::string& name, void* dest) const {
  CheckError();
  openslide_read_associated_image_icc_profile(osr_, name.c_str(), dest);
  
  // Check for errors that may have occurred during reading
  const char* error = openslide_get_error(osr_);
  if (error) {
    throw SlideError(error);
  }
}

int64_t Slide::GetICCProfileSize() const {
  CheckError();
  return openslide_get_icc_profile_size(osr_);
}

void Slide::ReadICCProfile(void* dest) const {
  CheckError();
  openslide_read_icc_profile(osr_, dest);
  
  // Check for errors that may have occurred during reading
  const char* error = openslide_get_error(osr_);
  if (error) {
    throw SlideError(error);
  }
}

std::string Slide::GetVersion() {
  const char* version = openslide_get_version();
  return version ? version : "";
}

}  // namespace fastslide 
