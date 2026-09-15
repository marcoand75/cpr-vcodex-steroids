#include "LibraryNavigation.h"

#include "components/LibraryIndex.h"
#include "SilentRestart.h"

namespace {

bool isSeriesTile(const LibraryIndex::BookRef& ref) {
  return (ref.id & 0x80000000u) != 0;
}

bool isCollectionTile(const LibraryNavState& state, const LibraryIndex::BookRef& ref) {
  return isSeriesTile(ref) && state.currentCollectionIdx < 0;
}

}  // namespace

bool LibraryNavigation::handleBack(const LibraryNavState& state, bool launchFromApps,
                                    LibraryNavActionResult* outAction) {
  if (!outAction) return false;
  outAction->handled = false;

  // In mixed/collections mode: go back to root from a specific collection/series
  if ((state.collectionsMode || state.mixedMode) && state.currentCollectionIdx >= 0) {
    outAction->handled = true;
    outAction->forceRender = true;
    outAction->requestUpdate = true;
    outAction->newSelectorIndex = state.prevSelectorBeforeCollection;
    outAction->newCollectionIdx = -1;
    outAction->newCollectionName.clear();
    outAction->newCollectionIsUser = false;
    outAction->newPrevSelectorBeforeCollection = -1;
    // Caller must also refresh counts and page cache after applying this action.
    return true;
  }

  // At root: let caller handle silent restart / go home.
  return false;
}

bool LibraryNavigation::handleConfirm(const LibraryNavState& state,
                                      const LibraryIndex::BookRef& pageRef,
                                      LibraryNavActionResult* outAction) {
  if (!outAction) return false;
  outAction->handled = false;

  // In mixed root view: check if selected item is a series tile
  if (state.mixedMode && state.currentCollectionIdx < 0 && isSeriesTile(pageRef)) {
    outAction->handled = true;
    outAction->newCollectionIdx = static_cast<int>(pageRef.id & 0x7FFFFFFFu);
    outAction->newCollectionName = pageRef.title;
    outAction->newPrevSelectorBeforeCollection = state.selectorIndex;
    outAction->newSelectorIndex = 0;
    // Caller must ensure USER_COLLECTIONS loaded and set isUser flag.
    outAction->newCollectionIsUser = state.currentCollectionIsUser;
    return true;
  }

  // In collections root view: enter the selected collection
  if (state.collectionsMode && state.currentCollectionIdx < 0 && isSeriesTile(pageRef)) {
    outAction->handled = true;
    outAction->newCollectionIdx = static_cast<int>(pageRef.id & 0x7FFFFFFFu);
    outAction->newCollectionName = pageRef.title;
    outAction->newPrevSelectorBeforeCollection = state.selectorIndex;
    outAction->newSelectorIndex = 0;
    outAction->newCollectionIsUser = state.currentCollectionIsUser;
    return true;
  }

  return false;
}

bool LibraryNavigation::isRootCollectionTile(const LibraryNavState& state,
                                             const LibraryIndex::BookRef& ref) {
  return state.currentCollectionIdx < 0 && isCollectionTile(state, ref);
}
