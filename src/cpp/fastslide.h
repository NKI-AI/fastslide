/**
 * @file fastslide.h
 * @brief C++20 wrapper for OpenSlide library.
 */

#ifndef FASTSLIDE_H_
#define FASTSLIDE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <stdexcept>

// Forward declare OpenSlide types
typedef struct _openslide openslide_t;
typedef struct _openslide_cache openslide_cache_t;
typedef struct _openslide_format _openslide_format;
typedef struct _openslide_tifflike _openslide_tifflike;

// Forward declare GLib types needed for OpenSlide internals
typedef struct _GError GError;

namespace fastslide {

// Property name constants
static const std::string PROPERTY_NAME_VENDOR = "fastslide.vendor";
static const std::string PROPERTY_NAME_BACKGROUND_COLOR = "fastslide.background-color";
static const std::string PROPERTY_NAME_BOUNDS_HEIGHT = "fastslide.bounds-height";
static const std::string PROPERTY_NAME_BOUNDS_WIDTH = "fastslide.bounds-width";
static const std::string PROPERTY_NAME_BOUNDS_X = "fastslide.bounds-x";
static const std::string PROPERTY_NAME_BOUNDS_Y = "fastslide.bounds-y";
static const std::string PROPERTY_NAME_COMMENT = "fastslide.comment";
static const std::string PROPERTY_NAME_MPP_X = "fastslide.mpp-x";
static const std::string PROPERTY_NAME_MPP_Y = "fastslide.mpp-y";
static const std::string PROPERTY_NAME_OBJECTIVE_POWER = "fastslide.objective-power";
static const std::string PROPERTY_NAME_QUICKHASH1 = "fastslide.quickhash-1";
static const std::string PROPERTY_NAME_LEVEL_COUNT = "fastslide.level-count";
static const std::string PROPERTY_NAME_ICC_SIZE = "fastslide.icc-size";

// Template property names
static const std::string PROPERTY_NAME_TEMPLATE_LEVEL_WIDTH = "fastslide.level[{}].width";
static const std::string PROPERTY_NAME_TEMPLATE_LEVEL_HEIGHT = "fastslide.level[{}].height";
static const std::string PROPERTY_NAME_TEMPLATE_LEVEL_DOWNSAMPLE = "fastslide.level[{}].downsample";
static const std::string PROPERTY_NAME_TEMPLATE_ASSOCIATED_WIDTH = "fastslide.associated-image[{}].width";
static const std::string PROPERTY_NAME_TEMPLATE_ASSOCIATED_HEIGHT = "fastslide.associated-image[{}].height";
static const std::string PROPERTY_NAME_TEMPLATE_ASSOCIATED_ICC_SIZE = "fastslide.associated-image[{}].icc-size";

// Helper functions for string formatting
std::string FormatPropertyName(const std::string& name_template, int32_t level);
std::string FormatPropertyName(const std::string& name_template, const std::string& associated);

// SlideCache class manages the OpenSlide cache
class SlideCache {
 public:
  // Create a new cache with the specified capacity
  static std::shared_ptr<SlideCache> Create(int64_t cache_size);
  
  // Constructor is made public for use with make_shared
  explicit SlideCache(int64_t cache_size);
  
  SlideCache(const SlideCache&) = delete;
  SlideCache& operator=(const SlideCache&) = delete;
  
  ~SlideCache();
  
  // Get the cache size
  int64_t GetSize() const;
  
 private:
  // Friend declaration to allow Slide to access private members
  friend class Slide;
  
  // The cache size
  int64_t cache_size_;
};

// Slide class encapsulates an OpenSlide object
class Slide {
 public:
  // Exception class for OpenSlide errors
  class SlideError : public std::runtime_error {
   public:
    explicit SlideError(const std::string& message);
  };
  
  /**
   * @brief Format information struct
   */
  struct FormatInfo {
    std::string vendor;     ///< Vendor name (e.g., "Aperio", "Hamamatsu")
    bool is_valid;          ///< Whether the format was successfully detected
    std::map<std::string, std::string> properties; ///< Additional properties detected
    std::string error_msg;  ///< Error message if detection failed
  };
  
  // Static methods
  static std::string DetectVendor(const std::string& filename);
  static std::shared_ptr<Slide> Open(const std::string& filename,
                                     std::shared_ptr<SlideCache> cache = nullptr);
  static std::string GetVersion();
  
  /**
   * @brief Detect the format of a slide file with enhanced information
   * 
   * This function provides more information than DetectVendor by briefly
   * opening the slide to extract key metadata.
   * 
   * @param filename Path to the slide file
   * @return FormatInfo containing information about the detected format
   */
  static FormatInfo DetectFormat(const std::string& filename);
  
  // Constructor and destructor
  explicit Slide(const std::string& filename, std::shared_ptr<SlideCache> cache = nullptr);
  ~Slide();
  
  // Delete copy and move operations
  Slide(const Slide&) = delete;
  Slide& operator=(const Slide&) = delete;
  Slide(Slide&&) = delete;
  Slide& operator=(Slide&&) = delete;
  
  // Error handling
  void CheckError() const;
  bool HasError() const;
  std::string GetErrorMessage() const;
  
  // Level information
  int32_t GetLevelCount() const;
  std::pair<int64_t, int64_t> GetLevel0Dimensions() const;
  std::pair<int64_t, int64_t> GetLevelDimensions(int32_t level) const;
  double GetLevelDownsample(int32_t level) const;
  int32_t GetBestLevelForDownsample(double downsample) const;
  
  // Reading regions
  void ReadRegion(uint32_t* dest, int64_t x, int64_t y, int32_t level,
                 int64_t width, int64_t height) const;
  
  // Properties
  std::vector<std::string> GetPropertyNames() const;
  std::string GetPropertyValue(const std::string& name) const;
  std::map<std::string, std::string> GetProperties() const;
  
  // Associated images
  std::vector<std::string> GetAssociatedImageNames() const;
  std::pair<int64_t, int64_t> GetAssociatedImageDimensions(const std::string& name) const;
  void ReadAssociatedImage(const std::string& name, uint32_t* dest) const;
  
  // ICC profile handling
  int64_t GetAssociatedImageICCProfileSize(const std::string& name) const;
  void ReadAssociatedImageICCProfile(const std::string& name, uint8_t* dest) const;
  int64_t GetICCProfileSize() const;
  void ReadICCProfile(uint8_t* dest) const;
  
  // Storyboard
  std::string GetStoryboardFile() const;
  
 private:
  // The OpenSlide handle
  openslide_t* osr_;
  
  // Path to the slide file
  std::string filename_;
  
  // Error message if initialization failed
  mutable std::string error_;
  
  // Properties cache
  mutable std::map<std::string, std::string> properties_;
  
  // Cache for slide reading
  std::shared_ptr<SlideCache> cache_;
};

}  // namespace fastslide

#endif  // FASTSLIDE_H_ 
