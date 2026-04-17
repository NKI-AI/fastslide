Examples
========

Complete working examples demonstrating FastSlide usage patterns.

.. toctree::
   :maxdepth: 2
   :caption: Example Applications:

   basic_usage
   performance_optimization  
   scientific_computing
   deep_learning_integration
   plugin_development

Quick Examples
--------------

Python Quick Start
~~~~~~~~~~~~~~~~~~

.. code-block:: python

   import fastslide
   import numpy as np
   import matplotlib.pyplot as plt
   
   # Open slide and read region
   with fastslide.FastSlide.from_file_path("slide.mrxs") as slide:
       print(f"Slide: {slide.format} - {slide.dimensions}")
       
       # Read thumbnail from associated images
       if "thumbnail" in slide.associated_images:
           thumbnail = slide.associated_images["thumbnail"]
           plt.imshow(thumbnail)
           plt.title("Slide Thumbnail")
           plt.show()
       
       # Read full resolution region
       region = slide.read_region((1000, 1000), 0, (512, 512))
       plt.figure(figsize=(8, 8))
       plt.imshow(region)
       plt.title("512x512 Region at Full Resolution")
       plt.axis('off')
       plt.show()

C++ Quick Start
~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include <fastslide/slide_reader.h>
   #include <iostream>
   
   int main() {
       // Open slide
       auto reader_or = fastslide::SlideReader::Open("slide.svs");
       if (!reader_or.ok()) {
           std::cerr << "Error: " << reader_or.status() << std::endl;
           return 1;
       }
       
       auto reader = std::move(reader_or).value();
       
       // Print slide information
       const auto props = reader->GetProperties();
       std::cout << "Slide dimensions: " 
                 << props.base_dimensions.width << "x" 
                 << props.base_dimensions.height << std::endl;
       std::cout << "Levels: " << reader->GetLevelCount() << std::endl;
       
       // Read region
       fastslide::RegionSpec spec{
           .bounds = {0, 0, 1024, 1024},
           .level = 0
       };
       
       auto image_or = reader->ReadRegion(spec);
       if (image_or.ok()) {
           const auto& image = image_or.value();
           std::cout << "Successfully read " 
                     << image.GetWidth() << "x" << image.GetHeight() 
                     << " region" << std::endl;
       }
       
       return 0;
   }

Complete Applications
---------------------

Batch Processing Pipeline
~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   """
   Example: Batch processing of whole slide images
   Process multiple slides and extract regions for analysis
   """
   import fastslide
   import numpy as np
   from pathlib import Path
   from concurrent.futures import ThreadPoolExecutor
   import logging
   
   logging.basicConfig(level=logging.INFO)
   logger = logging.getLogger(__name__)
   
   class SlideBatchProcessor:
       def __init__(self, cache_size=1000):
           self.cache_manager = fastslide.CacheManager.create(cache_size)
           
       def process_slide_batch(self, slide_paths, output_dir):
           """Process a batch of slides concurrently"""
           output_dir = Path(output_dir)
           output_dir.mkdir(exist_ok=True)
           
           with ThreadPoolExecutor(max_workers=4) as executor:
               futures = []
               for slide_path in slide_paths:
                   future = executor.submit(self._process_single_slide, 
                                          slide_path, output_dir)
                   futures.append(future)
               
               results = [f.result() for f in futures]
           
           # Print cache statistics
           stats = self.cache_manager.get_detailed_stats()
           logger.info(f"Cache performance: {stats.hit_ratio:.3f} hit ratio, "
                      f"{stats.memory_usage_mb:.1f} MB used")
           
           return results
       
       def _process_single_slide(self, slide_path, output_dir):
           """Process a single slide"""
           slide_path = Path(slide_path)
           
           try:
               with fastslide.FastSlide.from_file_path(slide_path) as slide:
                   # Set cache for performance
                   slide.set_cache_manager(self.cache_manager)
                   
                   logger.info(f"Processing {slide_path.name}: "
                              f"{slide.format} {slide.dimensions}")
                   
                   # Extract regions at multiple levels
                   results = {}
                   for level in range(min(3, slide.level_count)):
                       regions = self._extract_regions(slide, level)
                       results[f'level_{level}'] = regions
                   
                   # Save results
                   output_file = output_dir / f"{slide_path.stem}_analysis.npz"
                   np.savez_compressed(output_file, **results)
                   
                   return {
                       'slide_path': str(slide_path),
                       'status': 'success',
                       'regions_extracted': sum(len(r) for r in results.values()),
                       'output_file': str(output_file)
                   }
                   
           except Exception as e:
               logger.error(f"Failed to process {slide_path}: {e}")
               return {
                   'slide_path': str(slide_path),
                   'status': 'error', 
                   'error': str(e)
               }
       
       def _extract_regions(self, slide, level, region_size=512, stride=256):
           """Extract overlapping regions from a slide level"""
           level_dims = slide.level_dimensions[level]
           regions = []
           
           for y in range(0, level_dims[1], stride):
               for x in range(0, level_dims[0], stride):
                   try:
                       # Convert to level-native coordinates  
                       native_coords = slide.convert_level0_to_level_native(x, y, level)
                       region = slide.read_region(native_coords, level, 
                                                (region_size, region_size))
                       regions.append(region)
                   except Exception as e:
                       logger.warning(f"Failed to read region at ({x},{y}): {e}")
                       
           return regions
   
   if __name__ == "__main__":
       processor = SlideBatchProcessor(cache_size=2000)
       
       slide_files = [
           "slides/slide1.mrxs",
           "slides/slide2.svs", 
           "slides/slide3.qptiff"
       ]
       
       results = processor.process_slide_batch(slide_files, "output/")
       
       # Print summary
       successful = sum(1 for r in results if r['status'] == 'success')
       print(f"Successfully processed {successful}/{len(results)} slides")

High-Performance Tile Server
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   /*
   Example: High-performance tile server using FastSlide
   Serves tiles via HTTP API with caching and concurrent access
   */
   #include <fastslide/slide_reader.h>
   #include <fastslide/utilities/cache.h>
   #include <httplib.h>  // Simple HTTP library
   #include <nlohmann/json.hpp>
   #include <thread>
   #include <unordered_map>
   
   using json = nlohmann::json;
   
   class FastSlideTileServer {
   private:
       struct SlideInfo {
           std::unique_ptr<fastslide::SlideReader> reader;
           std::shared_ptr<fastslide::ITileCache> cache;
           std::string format;
           fastslide::SlideProperties properties;
       };
       
       std::unordered_map<std::string, SlideInfo> slides_;
       std::mutex slides_mutex_;
       
   public:
       absl::Status LoadSlide(const std::string& slide_id, 
                             const std::string& file_path) {
           auto reader_or = fastslide::SlideReader::Open(file_path);
           if (!reader_or.ok()) {
               return reader_or.status();
           }
           
           auto reader = std::move(reader_or).value();
           auto cache = fastslide::GlobalTileCache::Create(5000);  // Large cache
           reader->SetTileCache(cache);
           
           SlideInfo info{
               .reader = std::move(reader),
               .cache = cache,
               .format = info.reader->GetFormat(),
               .properties = info.reader->GetProperties()
           };
           
           std::lock_guard<std::mutex> lock(slides_mutex_);
           slides_[slide_id] = std::move(info);
           
           return absl::OkStatus();
       }
       
       void StartServer(int port = 8080) {
           httplib::Server server;
           
           // Slide info endpoint
           server.Get(R"(/slides/([^/]+)/info)", [this](
               const httplib::Request& req, httplib::Response& res) {
               
               std::string slide_id = req.matches[1];
               auto slide_info = GetSlideInfo(slide_id);
               if (!slide_info) {
                   res.status = 404;
                   res.set_content("Slide not found", "text/plain");
                   return;
               }
               
               json info_json = {
                   {"slide_id", slide_id},
                   {"format", slide_info->format},
                   {"dimensions", {slide_info->properties.base_dimensions.width,
                                  slide_info->properties.base_dimensions.height}},
                   {"level_count", slide_info->reader->GetLevelCount()},
                   {"mpp", {slide_info->properties.mpp_x, slide_info->properties.mpp_y}}
               };
               
               res.set_content(info_json.dump(), "application/json");
           });
           
           // Tile endpoint
           server.Get(R"(/slides/([^/]+)/tile/(\d+)/(\d+)/(\d+)/(\d+)/(\d+))", 
               [this](const httplib::Request& req, httplib::Response& res) {
               
               std::string slide_id = req.matches[1];
               int level = std::stoi(req.matches[2]);
               int x = std::stoi(req.matches[3]);
               int y = std::stoi(req.matches[4]);
               int width = std::stoi(req.matches[5]);
               int height = std::stoi(req.matches[6]);
               
               auto image_or = ReadTile(slide_id, level, x, y, width, height);
               if (!image_or.ok()) {
                   res.status = 400;
                   res.set_content(std::string(image_or.status().message()), 
                                  "text/plain");
                   return;
               }
               
               // Convert image to PNG and return
               auto png_data = ImageToPNG(image_or.value());
               res.set_content(png_data, "image/png");
               res.set_header("Cache-Control", "public, max-age=3600");
           });
           
           std::cout << "Starting tile server on port " << port << std::endl;
           server.listen("0.0.0.0", port);
       }
       
   private:
       const SlideInfo* GetSlideInfo(const std::string& slide_id) {
           std::lock_guard<std::mutex> lock(slides_mutex_);
           auto it = slides_.find(slide_id);
           return (it != slides_.end()) ? &it->second : nullptr;
       }
       
       absl::StatusOr<fastslide::Image> ReadTile(
           const std::string& slide_id, int level, 
           int x, int y, int width, int height) {
           
           auto slide_info = GetSlideInfo(slide_id);
           if (!slide_info) {
               return absl::NotFoundError("Slide not found: " + slide_id);
           }
           
           fastslide::RegionSpec spec{
               .bounds = {x, y, width, height},
               .level = level
           };
           
           return slide_info->reader->ReadRegion(spec);
       }
   };
   
   int main(int argc, char* argv[]) {
       FastSlideTileServer server;
       
       // Load some example slides
       server.LoadSlide("slide1", "data/example.mrxs").IgnoreError();
       server.LoadSlide("slide2", "data/example.svs").IgnoreError();
       
       // Start server
       server.StartServer(8080);
       
       return 0;
   }

Scientific Analysis Pipeline
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   """
   Example: Scientific analysis pipeline with FastSlide
   Demonstrates integration with scientific Python ecosystem
   """
   import fastslide
   import numpy as np
   import pandas as pd
   from skimage import measure, filters, morphology, segmentation
   from scipy import ndimage
   import matplotlib.pyplot as plt
   from pathlib import Path
   
   class TissueAnalyzer:
       def __init__(self, cache_size=1000):
           self.cache_manager = fastslide.CacheManager.create(cache_size)
           
       def analyze_slide(self, slide_path, analysis_level=2):
           """Complete tissue analysis pipeline"""
           with fastslide.FastSlide.from_file_path(slide_path) as slide:
               slide.set_cache_manager(self.cache_manager)
               
               print(f"Analyzing {Path(slide_path).name}")
               print(f"Format: {slide.format}")
               print(f"Dimensions: {slide.dimensions}")
               
               # Step 1: Read overview image
               overview = self._get_overview_image(slide, analysis_level)
               
               # Step 2: Tissue detection
               tissue_mask = self._detect_tissue(overview)
               
               # Step 3: Region segmentation  
               regions = self._segment_regions(overview, tissue_mask)
               
               # Step 4: Feature extraction
               features = self._extract_features(overview, regions)
               
               # Step 5: Visualization
               self._visualize_results(overview, tissue_mask, regions, features)
               
               return {
                   'slide_path': slide_path,
                   'tissue_area_mm2': self._calculate_tissue_area(tissue_mask, slide),
                   'region_count': len(regions),
                   'features': features
               }
       
       def _get_overview_image(self, slide, level):
           """Get overview image at specified level"""
           dims = slide.level_dimensions[level]
           
           # Read entire level (efficient for overview levels)
           overview = slide.read_region((0, 0), level, dims)
           return overview
       
       def _detect_tissue(self, image):
           """Detect tissue regions using color-based segmentation"""
           # Convert to grayscale
           gray = np.mean(image, axis=2)
           
           # Otsu thresholding
           threshold = filters.threshold_otsu(gray)
           binary = gray < threshold  # Tissue is darker
           
           # Morphological cleanup
           binary = morphology.remove_small_objects(binary, min_size=1000)
           binary = morphology.remove_small_holes(binary, area_threshold=5000)
           
           return binary
       
       def _segment_regions(self, image, tissue_mask):
           """Segment tissue into distinct regions"""
           # Apply mask
           masked_image = image.copy()
           masked_image[~tissue_mask] = [255, 255, 255]  # White background
           
           # Convert to Lab color space for better segmentation
           from skimage import color
           lab = color.rgb2lab(masked_image / 255.0)
           
           # Watershed segmentation
           distance = ndimage.distance_transform_edt(tissue_mask)
           local_maxima = morphology.local_maxima(distance, min_distance=50)
           markers = measure.label(local_maxima)
           
           regions = segmentation.watershed(-distance, markers, mask=tissue_mask)
           
           return regions
       
       def _extract_features(self, image, regions):
           """Extract quantitative features from segmented regions"""
           features = []
           
           for region_id in np.unique(regions)[1:]:  # Skip background (0)
               mask = regions == region_id
               
               # Morphological features
               props = measure.regionprops(mask.astype(int))[0]
               
               # Color features
               region_pixels = image[mask]
               mean_rgb = np.mean(region_pixels, axis=0)
               std_rgb = np.std(region_pixels, axis=0)
               
               features.append({
                   'region_id': region_id,
                   'area': props.area,
                   'perimeter': props.perimeter,
                   'eccentricity': props.eccentricity,
                   'solidity': props.solidity,
                   'mean_red': mean_rgb[0],
                   'mean_green': mean_rgb[1], 
                   'mean_blue': mean_rgb[2],
                   'std_red': std_rgb[0],
                   'std_green': std_rgb[1],
                   'std_blue': std_rgb[2]
               })
           
           return pd.DataFrame(features)
       
       def _calculate_tissue_area(self, tissue_mask, slide):
           """Calculate tissue area in mm²"""
           pixel_area = np.sum(tissue_mask)
           mpp = slide.mpp  # microns per pixel
           
           if mpp[0] > 0 and mpp[1] > 0:
               # Convert to mm²
               area_mm2 = (pixel_area * mpp[0] * mpp[1]) / (1000 * 1000)
               return area_mm2
           else:
               return None  # No calibration data
       
       def _visualize_results(self, image, tissue_mask, regions, features):
           """Create visualization of analysis results"""
           fig, axes = plt.subplots(2, 2, figsize=(12, 12))
           
           # Original image
           axes[0,0].imshow(image)
           axes[0,0].set_title('Original Image')
           axes[0,0].axis('off')
           
           # Tissue detection
           axes[0,1].imshow(tissue_mask, cmap='gray')
           axes[0,1].set_title('Tissue Detection')
           axes[0,1].axis('off')
           
           # Region segmentation
           axes[1,0].imshow(regions, cmap='tab20')
           axes[1,0].set_title('Region Segmentation')
           axes[1,0].axis('off')
           
           # Feature analysis
           if len(features) > 0:
               axes[1,1].scatter(features['area'], features['eccentricity'], 
                               c=features['mean_red'], cmap='viridis', alpha=0.7)
               axes[1,1].set_xlabel('Region Area')
               axes[1,1].set_ylabel('Eccentricity')
               axes[1,1].set_title('Feature Space')
           
           plt.tight_layout()
           plt.show()
   
   # Usage example
   if __name__ == "__main__":
       analyzer = TissueAnalyzer(cache_size=2000)
       
       slide_paths = [
           "data/H&E_sample_1.mrxs",
           "data/H&E_sample_2.svs",
           "data/IHC_sample.qptiff"
       ]
       
       results = analyzer.analyze_slide_batch(slide_paths, "analysis_output/")
       
       # Create summary report
       summary_df = pd.DataFrame(results)
       summary_df.to_csv("analysis_summary.csv", index=False)
       print("Analysis complete - see analysis_summary.csv")

Deep Learning Integration
~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   """
   Example: PyTorch integration for deep learning workflows
   """
   import fastslide
   import torch
   import torch.nn as nn
   from torch.utils.data import Dataset, DataLoader
   import torchvision.transforms as transforms
   from pathlib import Path
   import numpy as np
   
   class WholeSlideImageDataset(Dataset):
       """Dataset for training on whole slide images"""
       
       def __init__(self, slide_paths, tile_size=224, level=1, 
                    stride_ratio=1.0, transform=None):
           self.tile_size = tile_size
           self.level = level 
           self.transform = transform or self._default_transform()
           self.stride = int(tile_size * stride_ratio)
           
           # Initialize slides and cache
           self.cache = fastslide.CacheManager.create(capacity=1000)
           self.slides = []
           self.tile_coordinates = []
           
           for slide_path in slide_paths:
               slide = fastslide.FastSlide.from_file_path(slide_path)
               slide.set_cache_manager(self.cache)
               self.slides.append(slide)
               
               # Generate tile coordinates for this slide
               coords = self._generate_tile_coordinates(slide)
               self.tile_coordinates.extend([(len(self.slides)-1, *coord) 
                                           for coord in coords])
       
       def _default_transform(self):
           return transforms.Compose([
               transforms.ToPILImage(),
               transforms.Resize((224, 224)),
               transforms.ToTensor(),
               transforms.Normalize(mean=[0.485, 0.456, 0.406],
                                  std=[0.229, 0.224, 0.225])
           ])
       
       def _generate_tile_coordinates(self, slide):
           """Generate non-overlapping tile coordinates"""
           level_dims = slide.level_dimensions[self.level]
           coordinates = []
           
           for y in range(0, level_dims[1] - self.tile_size, self.stride):
               for x in range(0, level_dims[0] - self.tile_size, self.stride):
                   # Convert to level-native coordinates
                   native_coords = slide.convert_level0_to_level_native(x, y, self.level)
                   coordinates.append(native_coords)
           
           return coordinates
       
       def __len__(self):
           return len(self.tile_coordinates)
       
       def __getitem__(self, idx):
           slide_idx, x, y = self.tile_coordinates[idx]
           slide = self.slides[slide_idx]
           
           # Read tile
           tile = slide.read_region((x, y), self.level, 
                                  (self.tile_size, self.tile_size))
           
           # Apply transforms
           if self.transform:
               tile = self.transform(tile)
           
           return tile, slide_idx  # Return tensor and slide index
       
       def get_cache_stats(self):
           """Get cache performance statistics"""
           return self.cache.get_detailed_stats()
   
   # Example training loop
   def train_model():
       # Create dataset
       slide_paths = list(Path("training_slides/").glob("*.mrxs"))
       dataset = WholeSlideImageDataset(slide_paths, tile_size=224, level=1)
       
       # Create data loader
       loader = DataLoader(dataset, batch_size=32, shuffle=True, 
                          num_workers=4, pin_memory=True)
       
       # Example model (ResNet-18)
       model = torch.hub.load('pytorch/vision:v0.10.0', 'resnet18', pretrained=True)
       model.fc = nn.Linear(model.fc.in_features, 10)  # 10 classes
       model = model.cuda()
       
       optimizer = torch.optim.Adam(model.parameters(), lr=0.001)
       criterion = nn.CrossEntropyLoss()
       
       # Training loop
       model.train()
       for epoch in range(5):
           total_loss = 0
           for batch_idx, (tiles, slide_indices) in enumerate(loader):
               tiles = tiles.cuda()
               
               # Generate pseudo labels for demonstration
               labels = slide_indices.cuda()  # Use slide index as label
               
               optimizer.zero_grad()
               outputs = model(tiles)
               loss = criterion(outputs, labels)
               loss.backward()
               optimizer.step()
               
               total_loss += loss.item()
               
               if batch_idx % 10 == 0:
                   print(f'Epoch {epoch}, Batch {batch_idx}, Loss: {loss.item():.4f}')
           
           print(f'Epoch {epoch} complete, Average Loss: {total_loss/len(loader):.4f}')
       
       # Print cache performance
       stats = dataset.get_cache_stats()
       print(f"Cache hit ratio: {stats.hit_ratio:.3f}")
       print(f"Memory usage: {stats.memory_usage_mb:.1f} MB")
   
   if __name__ == "__main__":
       train_model()
