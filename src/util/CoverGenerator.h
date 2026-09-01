#pragma once

#include <string>

namespace CoverGenerator {

// Generates a cover thumbnail for the given book file.
// Returns true if a valid thumbnail was created at the expected location.
// Width and height specify the desired thumbnail dimensions.
bool generateCover(const std::string& bookPath, int width, int height);

}  // namespace CoverGenerator