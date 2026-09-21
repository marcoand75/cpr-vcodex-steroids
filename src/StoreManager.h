#pragma once

#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "FavoritesStore.h"
#include "HiddenBooksStore.h"
#include "FlashcardsStore.h"
#include "AchievementsStore.h"
#include "UserCollectionsStore.h"

class StoreManager {
 public:
  static ReadingStatsStore& readingStats() { return ReadingStatsStore::getInstance(); }
  static RecentBooksStore& recentBooks() { return RecentBooksStore::getInstance(); }
  static FavoritesStore& favorites() { return FavoritesStore::getInstance(); }
  static HiddenBooksStore& hiddenBooks() { return HiddenBooksStore::getInstance(); }
  static FlashcardsStore& flashcards() { return FlashcardsStore::getInstance(); }
  static AchievementsStore& achievements() { return AchievementsStore::getInstance(); }
  static UserCollectionsStore& userCollections() { return UserCollectionsStore::getInstance(); }

  static void ensureReadingStatsLoaded() {
    if (readingStats().needsReload()) readingStats().ensureLoaded();
  }
  static void ensureRecentBooksLoaded() {
    if (recentBooks().needsReload()) recentBooks().ensureLoaded();
  }
  static void ensureFavoritesLoaded() {
    if (favorites().needsReload()) favorites().ensureLoaded();
  }
  static void ensureHiddenBooksLoaded() {
    if (hiddenBooks().needsReload()) hiddenBooks().ensureLoaded();
  }
  static void ensureFlashcardsLoaded() {
    if (flashcards().needsReload()) flashcards().ensureLoaded();
  }
  static void ensureAchievementsLoaded() {
    if (achievements().needsReload()) achievements().ensureLoaded();
  }
  static void ensureUserCollectionsLoaded() {
    if (userCollections().needsReload()) userCollections().ensureLoaded();
  }

  static void invalidateReadingStats() { readingStats().resetLoaded(); }
  static void invalidateRecentBooks() { recentBooks().resetLoaded(); }
  static void invalidateFavorites() { favorites().resetLoaded(); }
  static void invalidateHiddenBooks() { hiddenBooks().resetLoaded(); }
  static void invalidateFlashcards() { flashcards().resetLoaded(); }
  static void invalidateAchievements() { achievements().resetLoaded(); }
  static void invalidateUserCollections() { userCollections().resetLoaded(); }

  static uint32_t readingStatsGeneration() { return readingStats().generation(); }
  static uint32_t recentBooksGeneration() { return recentBooks().generation(); }
  static uint32_t favoritesGeneration() { return favorites().generation(); }
  static uint32_t hiddenBooksGeneration() { return hiddenBooks().generation(); }
  static uint32_t flashcardsGeneration() { return flashcards().generation(); }
  static uint32_t achievementsGeneration() { return achievements().generation(); }
  static uint32_t userCollectionsGeneration() { return userCollections().generation(); }
};

#define READING_STATS StoreManager::readingStats()
#define RECENT_BOOKS StoreManager::recentBooks()
#define FAVORITES StoreManager::favorites()
#define HIDDEN_BOOKS StoreManager::hiddenBooks()
#define FLASHCARDS StoreManager::flashcards()
#define ACHIEVEMENTS StoreManager::achievements()
#define USER_COLLECTIONS StoreManager::userCollections()
