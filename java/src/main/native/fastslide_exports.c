// Force the linker to export all FastSlide C API symbols.
// Without referencing them from a source file in the cc_binary,
// the linker may discard them during dead-code elimination / stripping.

#include "fastslide/c/fastslide.h"

typedef void (*fn_ptr)(void);

// Array of function pointers that ensures the linker retains the C API symbols.
__attribute__((used)) static const fn_ptr fastslide_exports[] = {
    (fn_ptr)fastslide_initialize,
    (fn_ptr)fastslide_cleanup,
    (fn_ptr)fastslide_c_api_get_version,
    (fn_ptr)fastslide_get_version,
    (fn_ptr)fastslide_get_last_error,
    (fn_ptr)fastslide_clear_last_error,
    (fn_ptr)fastslide_create_reader,
    (fn_ptr)fastslide_get_supported_extensions,
    (fn_ptr)fastslide_registry_free_extensions,
    (fn_ptr)fastslide_is_supported,
    (fn_ptr)fastslide_slide_reader_free,
    (fn_ptr)fastslide_slide_reader_get_level_count,
    (fn_ptr)fastslide_slide_reader_get_base_dimensions,
    (fn_ptr)fastslide_slide_reader_get_level_dimensions,
    (fn_ptr)fastslide_slide_reader_get_level_downsample,
    (fn_ptr)fastslide_slide_reader_get_best_level_for_downsample,
    (fn_ptr)fastslide_slide_reader_get_format_name,
    (fn_ptr)fastslide_slide_reader_get_image_format,
    (fn_ptr)fastslide_slide_reader_get_tile_size,
    (fn_ptr)fastslide_slide_reader_get_properties,
    (fn_ptr)fastslide_slide_reader_free_properties,
    (fn_ptr)fastslide_slide_reader_get_associated_image_names,
    (fn_ptr)fastslide_slide_reader_free_associated_image_names,
    (fn_ptr)fastslide_slide_reader_get_associated_image_dimensions,
    (fn_ptr)fastslide_slide_reader_read_region_coords,
    (fn_ptr)fastslide_slide_reader_get_stack_info,
    (fn_ptr)fastslide_slide_reader_get_channel_metadata,
    (fn_ptr)fastslide_slide_reader_free_channel_metadata,
    // Multi-image container API.
    (fn_ptr)fastslide_slide_reader_get_image_count,
    (fn_ptr)fastslide_slide_reader_get_primary_image_index,
    (fn_ptr)fastslide_slide_reader_get_image_names,
    (fn_ptr)fastslide_slide_reader_free_image_names,
    (fn_ptr)fastslide_slide_reader_get_image,
    // Per-image (per-series) API.
    (fn_ptr)fastslide_slide_image_free,
    (fn_ptr)fastslide_slide_image_get_level_count,
    (fn_ptr)fastslide_slide_image_get_level_dimensions,
    (fn_ptr)fastslide_slide_image_get_level_downsample,
    (fn_ptr)fastslide_slide_image_get_base_dimensions,
    (fn_ptr)fastslide_slide_image_get_tile_size,
    (fn_ptr)fastslide_slide_image_get_image_format,
    (fn_ptr)fastslide_slide_image_get_data_type,
    (fn_ptr)fastslide_slide_image_get_channel_metadata,
    (fn_ptr)fastslide_slide_image_get_properties,
    (fn_ptr)fastslide_slide_image_get_stack_info,
    (fn_ptr)fastslide_slide_image_read_region_coords,
    (fn_ptr)fastslide_image_free,
    (fn_ptr)fastslide_image_get_info,
    (fn_ptr)fastslide_image_get_size_bytes,
    (fn_ptr)fastslide_image_copy_data,
};
