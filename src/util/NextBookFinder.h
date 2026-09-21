#pragma once

#include <string>
#include <vector>

namespace NextBookFinder {

// Returns at most maxCount supported sibling books following the current one
// in the same natural ordering used by the file browser.
std::vector<std::string> findNextBooks(const std::string& currentBookPath, size_t maxCount);

}  // namespace NextBookFinder
