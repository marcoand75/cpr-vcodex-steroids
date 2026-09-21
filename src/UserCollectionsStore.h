#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct UserCollection {
  std::string id;
  std::string name;
  uint32_t createdAt = 0;
};

struct CollectionMember {
  std::string collectionId;
  uint32_t bookId;
  float position = 0.0f;
};

class UserCollectionsStore;
namespace JsonSettingsIO {
bool saveUserCollections(const UserCollectionsStore& store, const char* path);
bool loadUserCollections(UserCollectionsStore& store, const char* json);
}  // namespace JsonSettingsIO

class UserCollectionsStore {
  static UserCollectionsStore instance;

  std::vector<UserCollection> collections_;
  std::vector<CollectionMember> members_;
  mutable bool loaded_ = false;
  uint32_t generation_ = 0;

  friend bool JsonSettingsIO::saveUserCollections(const UserCollectionsStore&, const char*);
  friend bool JsonSettingsIO::loadUserCollections(UserCollectionsStore&, const char*);

 public:
  ~UserCollectionsStore() = default;

  static UserCollectionsStore& getInstance() { return instance; }

  uint32_t generation() const { return generation_; }
  bool needsReload() const { return !loaded_; }
  void bumpGeneration() { ++generation_; }

  // Queries
  int totalCollections() const { return static_cast<int>(collections_.size()); }
  const std::vector<UserCollection>& collections() const { return collections_; }
  const std::vector<CollectionMember>& allMembers() const { return members_; }
  std::vector<CollectionMember> members(const std::string& collectionId) const;
  int memberCount(const std::string& collectionId) const;
  bool hasBook(const std::string& collectionId, uint32_t bookId) const;
   const UserCollection* findCollection(const std::string& collectionId) const;
   const UserCollection* findCollectionByName(const std::string& name) const;

  // Mutations
  bool createCollection(const std::string& name, std::string& outId);
  bool renameCollection(const std::string& collectionId, const std::string& newName);
  bool deleteCollection(const std::string& collectionId);
  bool addBook(const std::string& collectionId, uint32_t bookId, float position = 0.0f);
  bool removeBook(const std::string& collectionId, uint32_t bookId);
  void removeBookFromAll(uint32_t bookId);

  // Persistence
  bool saveToFile() const;
  bool loadFromFile();
  bool isLoaded() const { return loaded_; }
  bool ensureLoaded();
  void resetLoaded() { loaded_ = false; bumpGeneration(); }

 private:
  UserCollectionsStore() = default;

  int findCollectionIndex(const std::string& collectionId) const;
  int findMemberIndex(const std::string& collectionId, uint32_t bookId) const;
  std::string generateId() const;
};

#define USER_COLLECTIONS UserCollectionsStore::getInstance()
