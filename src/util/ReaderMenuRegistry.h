#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "I18n.h"

enum class ReaderMenuItemId : uint8_t {
    ReaderSettings = 0,
    SelectChapter,
    Footnotes,
    LookUpWord,
    LookupHistory,
    Dictionary,
    ViewBookmarks,
    SaveBookmark,
    CreateClipping,
    ViewClippings,
    GoToPercent,
    AutoPageTurn,
    RotateScreen,
    Screenshot,
    DisplayQr,
    MarkAsFinished,
    GoHome,
    Sync,
    DeleteCache,
    COUNT // Keep last
};

enum class ReaderMenuGroupId {
    Navigation = 0,
    Annotation,
    Tools,
    Display,
    Book,
    System,
    COUNT
};

struct ReaderMenuItemDefinition {
    ReaderMenuItemId id;
    ReaderMenuGroupId groupId;
    StrId nameId;
    StrId descriptionId; // Optional, for settings description
};

// Function to get all reader menu item definitions
inline const std::array<ReaderMenuItemDefinition, static_cast<size_t>(ReaderMenuItemId::COUNT)>& getReaderMenuDefinitions() {
    static const std::array<ReaderMenuItemDefinition, static_cast<size_t>(ReaderMenuItemId::COUNT)> definitions = {{
        {ReaderMenuItemId::ReaderSettings, ReaderMenuGroupId::System, StrId::STR_READING_QUICK_SETTINGS, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::SelectChapter, ReaderMenuGroupId::Navigation, StrId::STR_SELECT_CHAPTER, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::Footnotes, ReaderMenuGroupId::Annotation, StrId::STR_FOOTNOTES, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::LookUpWord, ReaderMenuGroupId::Tools, StrId::STR_LOOK_UP_WORD, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::LookupHistory, ReaderMenuGroupId::Tools, StrId::STR_LOOKUP_HISTORY, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::Dictionary, ReaderMenuGroupId::Tools, StrId::STR_DICTIONARY, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::ViewBookmarks, ReaderMenuGroupId::Annotation, StrId::STR_VIEW_BOOKMARKS, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::SaveBookmark, ReaderMenuGroupId::Annotation, StrId::STR_SAVE_BOOKMARK, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::CreateClipping, ReaderMenuGroupId::Annotation, StrId::STR_CREATE_CLIPPING, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::ViewClippings, ReaderMenuGroupId::Annotation, StrId::STR_VIEW_CLIPPINGS, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::GoToPercent, ReaderMenuGroupId::Navigation, StrId::STR_GO_TO_PERCENT, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::AutoPageTurn, ReaderMenuGroupId::Display, StrId::STR_AUTO_TURN_PAGES_PER_MIN, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::RotateScreen, ReaderMenuGroupId::Display, StrId::STR_ORIENTATION, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::Screenshot, ReaderMenuGroupId::Display, StrId::STR_SCREENSHOT_BUTTON, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::DisplayQr, ReaderMenuGroupId::Display, StrId::STR_DISPLAY_QR, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::MarkAsFinished, ReaderMenuGroupId::Book, StrId::STR_MARK_AS_FINISHED, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::GoHome, ReaderMenuGroupId::Navigation, StrId::STR_GO_HOME_BUTTON, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::Sync, ReaderMenuGroupId::System, StrId::STR_SYNC_PROGRESS, StrId::STR_NONE_OPT},
        {ReaderMenuItemId::DeleteCache, ReaderMenuGroupId::System, StrId::STR_DELETE_CACHE, StrId::STR_NONE_OPT}
    }};
    return definitions;
}

// Bitmask helpers
inline uint32_t getReaderMenuVisibilityMask(const CrossPointSettings& settings) {
    return settings.readerMenuVisibilityMask;
}

inline uint32_t& getReaderMenuVisibilityMaskRef(CrossPointSettings& settings) {
    return settings.readerMenuVisibilityMask;
}

inline bool getReaderMenuItemVisibility(ReaderMenuItemId id, const CrossPointSettings& settings) {
    const uint32_t mask = getReaderMenuVisibilityMask(settings);
    const size_t index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(ReaderMenuItemId::COUNT)) return true;
    return (mask & (1u << index)) != 0;
}

inline void setReaderMenuItemVisibility(ReaderMenuItemId id, bool visible, CrossPointSettings& settings) {
    uint32_t& mask = getReaderMenuVisibilityMaskRef(settings);
    const size_t index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(ReaderMenuItemId::COUNT)) return;
    if (visible) {
        mask |= (1u << index);
    } else {
        mask &= ~(1u << index);
    }
}

inline bool isReaderMenuItemAlwaysVisible(ReaderMenuItemId /*id*/) {
    // None are always visible; all can be toggled
    return false;
}

// ---- Ordering helpers ----
//
 // We persist a permutation (a small `uint8_t` per item) where each value is the
 // 0-based display position of the corresponding item. The default initialiser
 // in CrossPointSettings.h provides a stable identity permutation (0..N-1).
 // Hidden items are not rendered in the popup, but their slot is still kept so
 // that toggling visibility back on keeps the same order.

inline uint8_t getReaderMenuItemOrder(ReaderMenuItemId id, const CrossPointSettings& settings) {
    const size_t index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(ReaderMenuItemId::COUNT)) {
        return static_cast<uint8_t>(index);
    }
    return settings.readerMenuOrderMask[index];
}

inline uint8_t& getReaderMenuItemOrderRef(CrossPointSettings& settings, ReaderMenuItemId id) {
    const size_t index = static_cast<size_t>(id);
    return settings.readerMenuOrderMask[index];
}

// Renormalise the order array to a contiguous 0..N-1 permutation. Duplicate or
// out-of-range values get compacted while preserving relative ordering.
inline void normalizeReaderMenuOrderSettings(CrossPointSettings& settings) {
    struct OrderSlot {
        int stableIndex;
        uint8_t* value;
    };
    std::vector<OrderSlot> slots;
    slots.reserve(static_cast<size_t>(ReaderMenuItemId::COUNT));
    int stableIndex = 0;
    for (size_t i = 0; i < static_cast<size_t>(ReaderMenuItemId::COUNT); i++) {
        slots.push_back(OrderSlot{stableIndex++, &settings.readerMenuOrderMask[i]});
    }
    std::stable_sort(slots.begin(), slots.end(), [](const OrderSlot& lhs, const OrderSlot& rhs) {
        if (*lhs.value != *rhs.value) {
            return *lhs.value < *rhs.value;
        }
        return lhs.stableIndex < rhs.stableIndex;
    });
    for (size_t index = 0; index < slots.size(); ++index) {
        *slots[index].value = static_cast<uint8_t>(index);
    }
}

 // Return all menu item definitions sorted by the current user-defined order.
 // Hidden items are also returned so that the order activity can show them (and
 // allow the user to rearrange them while keeping them hidden). Callers that
 // want only visible items should filter via getReaderMenuItemVisibility.
 inline std::vector<const ReaderMenuItemDefinition*> getReaderMenuItemsInOrder(const CrossPointSettings& settings) {
     std::vector<const ReaderMenuItemDefinition*> items;
     items.reserve(getReaderMenuDefinitions().size());
     for (const auto& definition : getReaderMenuDefinitions()) {
         items.push_back(&definition);
     }
     std::stable_sort(items.begin(), items.end(), [&settings](const ReaderMenuItemDefinition* lhs,
                                                               const ReaderMenuItemDefinition* rhs) {
         const uint8_t lhsOrder = getReaderMenuItemOrder(lhs->id, settings);
         const uint8_t rhsOrder = getReaderMenuItemOrder(rhs->id, settings);
         if (lhsOrder != rhsOrder) {
             return lhsOrder < rhsOrder;
         }
         return static_cast<size_t>(lhs->id) < static_cast<size_t>(rhs->id);
     });
     return items;
 }

// Helper to get group title string ID
inline StrId getReaderMenuGroupTitle(ReaderMenuGroupId group) {
    switch (group) {
        case ReaderMenuGroupId::Navigation: return StrId::STR_MENU_GROUP_NAVIGATION;
        case ReaderMenuGroupId::Annotation: return StrId::STR_MENU_GROUP_ANNOTATION;
        case ReaderMenuGroupId::Tools: return StrId::STR_MENU_GROUP_TOOLS;
        case ReaderMenuGroupId::Display: return StrId::STR_MENU_GROUP_DISPLAY;
        case ReaderMenuGroupId::Book: return StrId::STR_MENU_GROUP_BOOK;
        case ReaderMenuGroupId::System: return StrId::STR_MENU_GROUP_SYSTEM;
        default: return StrId::STR_NONE_OPT;
    }
}