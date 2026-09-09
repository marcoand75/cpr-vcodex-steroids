#include "UserCollectionsStore.h"
#include "StoreManager.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <algorithm>

namespace {
constexpr char USER_COLLECTIONS_FILE_JSON[] = "/.crosspoint/user_collections.json";
}  // namespace

UserCollectionsStore UserCollectionsStore::instance;

std::vector<CollectionMember> UserCollectionsStore::members(const std::string& collectionId) const {
  std::vector<CollectionMember> result;
  for (const auto& m : members_) {
    if (m.collectionId == collectionId) {
      result.push_back(m);
    }
  }
  return result;
}

int UserCollectionsStore::memberCount(const std::string& collectionId) const {
  int count = 0;
  for (const auto& m : members_) {
    if (m.collectionId == collectionId) ++count;
  }
  return count;
}

bool UserCollectionsStore::hasBook(const std::string& collectionId, uint32_t bookId) const {
  return findMemberIndex(collectionId, bookId) >= 0;
}

const UserCollection* UserCollectionsStore::findCollection(const std::string& collectionId) const {
  for (const auto& c : collections_) {
    if (c.id == collectionId) return &c;
  }
  return nullptr;
}

int UserCollectionsStore::findCollectionIndex(const std::string& collectionId) const {
  for (int i = 0; i < static_cast<int>(collections_.size()); ++i) {
    if (collections_[i].id == collectionId) return i;
  }
  return -1;
}

int UserCollectionsStore::findMemberIndex(const std::string& collectionId, uint32_t bookId) const {
  for (int i = 0; i < static_cast<int>(members_.size()); ++i) {
    if (members_[i].collectionId == collectionId && members_[i].bookId == bookId) return i;
  }
  return -1;
}

std::string UserCollectionsStore::generateId() const {
  // Simple incrementing id: c_001, c_002, ...
  int maxId = 0;
  for (const auto& c : collections_) {
    if (c.id.rfind("c_", 0) == 0) {
      int num = std::atoi(c.id.c_str() + 2);
      if (num > maxId) maxId = num;
    }
  }
  char buf[16];
  std::snprintf(buf, sizeof(buf), "c_%03d", maxId + 1);
  return buf;
}

bool UserCollectionsStore::createCollection(const std::string& name, std::string& outId) {
  if (name.empty()) return false;
  outId = generateId();
  collections_.push_back({outId, name, static_cast<uint32_t>(millis())});
  bumpGeneration();
  saveToFile();
  return true;
}

bool UserCollectionsStore::renameCollection(const std::string& collectionId, const std::string& newName) {
  if (newName.empty()) return false;
  const int idx = findCollectionIndex(collectionId);
  if (idx < 0) return false;
  collections_[idx].name = newName;
  bumpGeneration();
  saveToFile();
  return true;
}

bool UserCollectionsStore::deleteCollection(const std::string& collectionId) {
  const int collIdx = findCollectionIndex(collectionId);
  if (collIdx < 0) return false;

  // Remove all members
  members_.erase(std::remove_if(members_.begin(), members_.end(),
                                [&collectionId](const CollectionMember& m) { return m.collectionId == collectionId; }),
                 members_.end());
  collections_.erase(collections_.begin() + collIdx);
  bumpGeneration();
  saveToFile();
  return true;
}

bool UserCollectionsStore::addBook(const std::string& collectionId, uint32_t bookId, float position) {
  const int collIdx = findCollectionIndex(collectionId);
  if (collIdx < 0) return false;

  // Idempotent: if already present, no-op
  if (findMemberIndex(collectionId, bookId) >= 0) return true;

  members_.push_back({collectionId, bookId, position});
  bumpGeneration();
  saveToFile();
  return true;
}

bool UserCollectionsStore::removeBook(const std::string& collectionId, uint32_t bookId) {
  const int idx = findMemberIndex(collectionId, bookId);
  if (idx < 0) return false;

  members_.erase(members_.begin() + idx);
  bumpGeneration();
  saveToFile();
  return true;
}

void UserCollectionsStore::removeBookFromAll(uint32_t bookId) {
  bool changed = false;
  for (auto it = members_.begin(); it != members_.end(); ) {
    if (it->bookId == bookId) {
      it = members_.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (changed) {
    bumpGeneration();
    saveToFile();
  }
}

bool UserCollectionsStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveUserCollections(*this, USER_COLLECTIONS_FILE_JSON);
}

bool UserCollectionsStore::loadFromFile() {
  const std::string tempPath = std::string(USER_COLLECTIONS_FILE_JSON) + ".tmp";
  if (!Storage.exists(USER_COLLECTIONS_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), USER_COLLECTIONS_FILE_JSON)) {
      LOG_DBG("UCS", "Recovered user_collections.json from interrupted temp file");
    }
  }

  if (!Storage.exists(USER_COLLECTIONS_FILE_JSON)) {
    return false;
  }

  const String json = Storage.readFile(USER_COLLECTIONS_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }

  const bool loaded = JsonSettingsIO::loadUserCollections(*this, json.c_str());
  if (loaded) {
    loaded_ = true;
    bumpGeneration();
  }
  return loaded;
}

bool UserCollectionsStore::ensureLoaded() {
  if (loaded_) return true;
  loaded_ = loadFromFile();
  return loaded_;
}
