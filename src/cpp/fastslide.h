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

namespace fastslide {

// Property name constants
static const char PROPERTY_NAME_VENDOR[] = "openslide.vendor";
static const char PROPERTY_NAME_BACKGROUND_COLOR[] = "openslide.background-color";
static const char PROPERTY_NAME_BOUNDS_HEIGHT[] = "openslide.bounds-height";
static const char PROPERTY_NAME_BOUNDS_WIDTH[] = "openslide.bounds-width";
static const char PROPERTY_NAME_BOUNDS_X[] = "openslide.bounds-x";
static const char PROPERTY_NAME_BOUNDS_Y[] = "openslide.bounds-y";
static const char PROPERTY_NAME_COMMENT[] = "openslide.comment";
static const char PROPERTY_NAME_MPP_X[] = "openslide.mpp-x";
static const char PROPERTY_NAME_MPP_Y[] = "openslide.mpp-y";
static const char PROPERTY_NAME_OBJECTIVE_POWER[] = "openslide.objective-power";
static const char PROPERTY_NAME_QUICKHASH1[] = "openslide.quickhash-1";
static const char PROPERTY_NAME_LEVEL_COUNT[] = "openslide.level-count";
static const char PROPERTY_NAME_ICC_SIZE[] = "openslide.icc-size";

// Template property names
static const char PROPERTY_NAME_TEMPLATE_LEVEL_WIDTH[] = "openslide.level[%d].width";
static const char PROPERTY_NAME_TEMPLATE_LEVEL_HEIGHT[] = "openslide.level[%d].height";
static const char PROPERTY_NAME_TEMPLATE_LEVEL_DOWNSAMPLE[] = "openslide.level[%d].downsample";
static const char PROPERTY_NAME_TEMPLATE_ASSOCIATED_WIDTH[] = "openslide.associated-image[%s].width";
static const char PROPERTY_NAME_TEMPLATE_ASSOCIATED_HEIGHT[] = "openslide.associated-image[%s].height";
static const char PROPERTY_NAME_TEMPLATE_ASSOCIATED_ICC_SIZE[] = "openslide.associated-image[%s].icc-size";

// SlideCache class manages the OpenSlide cache
class SlideCache {
 public:
  // Create a new cache with the specified capacity
  static std::shared_ptr<SlideCache> Create(size_t capacity);
  
  // SlideCache is not copyable or assignable
  SlideCache(const SlideCache&) = delete;
  SlideCache& operator=(const SlideCache&) = delete;
  
  ~SlideCache();
  
 private:
  explicit SlideCache(openslide_cache_t* cache);
  
  // Friend declaration to allow Slide to access the cache_ member
  friend class Slide;
  
  // The OpenSlide cache object
  openslide_cache_t* cache_;
};

// Slide class encapsulates an OpenSlide object
class Slide {
 public:
  // Exception class for OpenSlide errors
  class SlideError : public std::runtime_error {
   public:
    explicit SlideError(const std::string& message) : std::runtime_error(message) {}
  };
  
  // Static methods
  static std::string DetectVendor(const std::string& filename);
  static std::shared_ptr<Slide> Open(const std::string& filename,
                                     std::shared_ptr<SlideCache> cache = nullptr);
  static std::string GetVersion();
  
  // Constructors and destructor
  Slide();
  ~Slide();
  
  // Move operations (prevent copies)
  Slide(Slide&& other);
  Slide& operator=(Slide&& other);
  
  // Delete copy operations
  Slide(const Slide&) = delete;
  Slide& operator=(const Slide&) = delete;
  
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
  void ReadRegion(void* dest, int64_t x, int64_t y, int32_t level,
                 int64_t width, int64_t height) const;
  
  // Properties
  std::vector<std::string> GetPropertyNames() const;
  std::string GetPropertyValue(const std::string& name) const;
  std::map<std::string, std::string> GetProperties() const;
  
  // Associated images
  std::vector<std::string> GetAssociatedImageNames() const;
  std::pair<int64_t, int64_t> GetAssociatedImageDimensions(const std::string& name) const;
  void ReadAssociatedImage(const std::string& name, void* dest) const;
  
  // ICC profile handling
  int64_t GetAssociatedImageICCProfileSize(const std::string& name) const;
  void ReadAssociatedImageICCProfile(const std::string& name, void* dest) const;
  int64_t GetICCProfileSize() const;
  void ReadICCProfile(void* dest) const;
  
 private:
  // The OpenSlide handle
  openslide_t* osr_;
  
  // Path to the slide file
  std::string filename_;
  
  // Error message if initialization failed
  std::string error_;
  
  // Properties cache
  mutable std::map<std::string, std::string> properties_;
};

}  // namespace fastslide

#endif  // FASTSLIDE_H_ 
