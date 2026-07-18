#pragma once

#include <string>

// Forward declarations to avoid heavy includes in header
class GfxRenderer; // Not strictly needed here, but good for context

namespace EpubParser {

/**
 * @brief Extracts title and author from an EPUB file.
 * Tries direct ZIP reading of container.xml/content.opf first (lightweight).
 * Falls back to reading the pre-existing book.bin cache if direct read fails.
 * @param epubPath Path to the .epub file.
 * @param cacheDir Base cache directory (e.g., "/.crosspoint").
 * @param outTitle Output string for the book title.
 * @param outAuthor Output string for the book author.
 * @return true if metadata was successfully extracted.
 */
bool extractMetadata(const std::string& epubPath, const std::string& cacheDir, std::string& outTitle, std::string& outAuthor);

/**
 * @brief Extracts and generates a 1-bit BMP cover thumbnail from an EPUB.
 * Handles JPEG and PNG formats, including EXIF thumbnail fallback for low-memory situations.
 * @param epubPath Path to the .epub file.
 * @param coverW Target thumbnail width.
 * @param coverH Target thumbnail height.
 * @return true if the thumbnail was successfully generated and saved.
 */
bool generateCover(const std::string& epubPath, int coverW, int coverH);

} // namespace EpubParser