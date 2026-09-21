#pragma once

#include <string>

namespace StringUtils {

/**
 * Sanitize a string for use as a filename.
 * Replaces invalid characters with underscores, trims spaces/dots,
 * and limits length to maxBytes bytes.
 */
std::string sanitizeFilename(const std::string& name, size_t maxBytes = 100);

/**
 * Convert a string to lowercase ASCII in-place and return it.
 */
std::string toLowerAscii(std::string value);

/**
 * Copy a std::string into a fixed-size char buffer safely.
 * Replaces the repeated:
 *   strncpy(dest, src.c_str(), sizeof(dest) - 1);
 *   dest[sizeof(dest) - 1] = '\0';
 */
void copyToFixedBuffer(char* dest, size_t destSize, const std::string& src);

}  // namespace StringUtils
