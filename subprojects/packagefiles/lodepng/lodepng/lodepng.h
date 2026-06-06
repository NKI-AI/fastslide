// Shim so consumers can include lodepng as "lodepng/lodepng.h" (matching the
// Bazel include_prefix). The real header lives at the subproject root.
#include "../lodepng.h"
