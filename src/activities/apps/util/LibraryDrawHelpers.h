#ifndef LIBRARY_DRAW_HELPERS_H
#define LIBRARY_DRAW_HELPERS_H

#include <cstdint>
#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "components/LibraryCache.h"

// ---- Geometric drawing helpers --------------------------------------------

void fillTopRightTri(GfxRenderer& r, int x, int y, int leg, bool black);

void drawCyberpunkSelectionBorder(const GfxRenderer& renderer, int x, int y, int w, int h, bool color = true);

void drawRibbonBadge(GfxRenderer& r, int cx, int cy, int cw, int ch,
                     bool completed, bool favorite, bool opened);

// ---- Filter predicate ------------------------------------------------------

bool includeBookByFilter(const LibraryCache::Entry& e, CrossPointSettings::LIBRARY_FILTER filter);

#endif  // LIBRARY_DRAW_HELPERS_H
