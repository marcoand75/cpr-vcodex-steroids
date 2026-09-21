#pragma once

#include <string>

// Forward declarations to avoid heavy includes in header
class GfxRenderer; // Not strictly needed here, but good for context

namespace EpubParser {

/**
 * @brief Extracts title and author from an EPUB file.
 * Tries direct ZIP reading of container.xml/content.opf first (lightweight).
 * Falls back to reading the pre-existing book.bin cache if direct read fails.
 *
 * Series/collection extraction:
 * - Reads calibre:series / calibre:series_index from OPF <meta> tags.
 * - If no calibre series, reads EPUB3 belongs-to-collection / group-position.
 * - Does NOT fall back to parent folder name. Folder fallback is handled
 *   by LibraryIndex::scan(), not by this parser.
 *
 * @param epubPath Path to the .epub file.
 * @param cacheDir Base cache directory (e.g., "/.crosspoint").
 * @param outTitle Output string for the book title.
 * @param outAuthor Output string for the book author.
 * @param outSeries Optional output string for the series name.
 * @param outSeriesIndex Optional output float for the series position.
 * @return true if metadata was successfully extracted.
 */
bool extractMetadata(const std::string& epubPath, const std::string& cacheDir,
                     std::string& outTitle, std::string& outAuthor,
                     std::string* outSeries = nullptr, float* outSeriesIndex = nullptr);

} // namespace EpubParser