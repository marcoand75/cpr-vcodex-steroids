#pragma once

#include <string>

#include "components/LibraryIndex.h"

struct LibraryNavState {
  bool collectionsMode = false;
  bool mixedMode = false;
  int currentCollectionIdx = -1;
  std::string currentCollectionName;
  bool currentCollectionIsUser = false;
  int selectorIndex = 0;
  int prevSelectorBeforeCollection = -1;
};

struct LibraryNavActionResult {
  bool handled = false;
  bool forceRender = false;
  bool requestUpdate = false;
  bool pendingCollectionsRebuild = false;
  int newSelectorIndex = -1;
  int newCollectionIdx = -1;
  std::string newCollectionName;
  bool newCollectionIsUser = false;
  int newPrevSelectorBeforeCollection = -1;
};

class LibraryNavigation {
 public:
  // Handle Back button navigation in library context.
  // Returns true if the back press was handled by navigation logic.
  static bool handleBack(const LibraryNavState& state, bool launchFromApps,
                         LibraryNavActionResult* outAction);

  // Handle Confirm button for collection/series entry or book open.
  // Returns true if the confirm was handled by navigation logic.
  static bool handleConfirm(const LibraryNavState& state,
                            const LibraryIndex::BookRef& pageRef,
                            LibraryNavActionResult* outAction);

  // Check if the current selection represents a root-level collection/series tile.
  static bool isRootCollectionTile(const LibraryNavState& state,
                                   const LibraryIndex::BookRef& pageRef);
};
