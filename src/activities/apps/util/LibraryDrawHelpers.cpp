#include "LibraryDrawHelpers.h"

#include <algorithm>

#include "CrossPointSettings.h"
#include "FavoritesStore.h"
#include "RecentBooksStore.h"
#include "components/icons/heart24.h"

void fillTopRightTri(GfxRenderer& r, int x, int y, int leg, bool black) {
  for (int dy = 0; dy < leg; ++dy) r.fillRect(x + dy, y + dy, leg - dy, 1, black);
}

void drawCyberpunkSelectionBorder(const GfxRenderer& renderer, int x, int y, int w, int h, bool color) {
  constexpr int c = 4;
  constexpr int cl = 5;
  constexpr int cg = 2;
  const int bx = x - 5;
  const int by = y - 5;
  const int bw = w + 10;
  const int bh = h + 10;
  renderer.drawRect(bx, by, bw, bh, color);
  renderer.drawLine(bx + cg, by, bx + cg + cl, by, 1, color);
  renderer.drawLine(bx, by + cg, bx, by + cg + cl, 1, color);
  renderer.drawLine(bx + bw - cg - cl, by, bx + bw - cg, by, 1, color);
  renderer.drawLine(bx + bw, by + cg, bx + bw, by + cg + cl, 1, color);
  renderer.drawLine(bx + cg, by + bh, bx + cg + cl, by + bh, 1, color);
  renderer.drawLine(bx, by + bh - cg, bx, by + bh - cg - cl, 1, color);
  renderer.drawLine(bx + bw - cg - cl, by + bh, bx + bw - cg, by + bh, 1, color);
  renderer.drawLine(bx + bw, by + bh - cg, bx + bw, by + bh - cg - cl, 1, color);
}

void drawRibbonBadge(GfxRenderer& r, int cx, int cy, int cw, int ch, bool completed, bool favorite, bool opened) {
  (void)ch;
  const int leg = std::max(20, std::min(cw * 2 / 5, 44));
  const int rx = cx + cw - leg;
  const int ry = cy;

  fillTopRightTri(r, rx - 3, ry - 3, leg + 6, false);
  fillTopRightTri(r, rx - 2, ry - 2, leg + 4, true);
  fillTopRightTri(r, rx - 1, ry - 1, leg + 2, false);
  fillTopRightTri(r, rx, ry, leg, true);

  const int symCx = cx + cw - leg / 3;
  const int symCy = cy + leg / 3;
  const int symSz = std::max(8, leg * 22 / 100);

  if (completed) {
    r.drawLine(symCx - 5, symCy, symCx - 1, symCy + 4, 2, false);
    r.drawLine(symCx - 1, symCy + 4, symCx + 6, symCy - 4, 2, false);
  } else if (favorite) {
    constexpr int kHeartSz = 24;
    if (leg >= kHeartSz) {
      int hx = symCx - kHeartSz / 2;
      int hy = symCy - kHeartSz / 2;
      r.drawIconInverted(::Heart24Icon, hx, hy, kHeartSz, kHeartSz);
    }
  } else if (opened) {
    const int dotR = std::max(1, symSz / 4);
    for (int y2 = -dotR; y2 <= dotR; ++y2)
      for (int x2 = -dotR; x2 <= dotR; ++x2)
        if (x2 * x2 + y2 * y2 <= dotR * dotR + dotR)
          r.drawLine(symCx + x2, symCy + y2, symCx + x2, symCy + y2, 1, false);
  }
}

bool includeBookByFilter(const LibraryCache::Entry& e, CrossPointSettings::LIBRARY_FILTER filter) {
  switch (filter) {
    case CrossPointSettings::LIBRARY_FILTER_ALL:
      return true;
    case CrossPointSettings::LIBRARY_FILTER_FAVOURITES:
      return FAVORITES.isFavorite(e.path);
    case CrossPointSettings::LIBRARY_FILTER_LATEST_READ: {
      const auto& recent = RECENT_BOOKS.getBooks();
      for (const auto& rb : recent) {
        if (rb.path == e.path || (!rb.bookId.empty() && rb.bookId == e.path)) return true;
      }
      return false;
    }
  }
  return false;
}
