#include "HiddenBooksStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <algorithm>
#include <unordered_set>

#include "Logging.h"
#include "util/BookIdentity.h"
#include "util/BookStoreUtils.h"

constexpr char HIDDEN_BOOKS_FILE[] = "/.crosspoint/hidden_books.json";
constexpr int CURRENT_FORMAT_VERSION = 1;

HiddenBooksStore& HiddenBooksStore::getInstance() {
    static HiddenBooksStore instance;
    return instance;
}

bool HiddenBooksStore::loadFromFile() {
    FsFile file;
    if (!Storage.openFileForRead("HBN", HIDDEN_BOOKS_FILE, file)) {
        LOG_DBG("HBN", "Hidden books file not found or unreadable: %s", HIDDEN_BOOKS_FILE);
        hiddenBooks.clear();
        return false;
    }

    // Read the entire file byte-by-byte into a string.  serialization::readString
    // is designed for length-prefixed binary blobs (RecentBooksStore format) and
    // truncates the leading size field — that's why the JSON was missing the first
    // few characters ("{\"fo").
    std::string json;
    json.reserve(static_cast<size_t>(file.size()));
    int c;
    while ((c = file.read()) >= 0) {
        json.push_back(static_cast<char>(c));
    }
    file.close();
    
    LOG_DBG("HBN", "loadFromFile raw JSON (%zu bytes): %s", json.size(), json.c_str());

    if (json.empty()) {
        LOG_DBG("HBN", "Hidden books file is empty");
        hiddenBooks.clear();
        return false;
    }

    JsonDocument doc;
    const auto error = deserializeJson(doc, json);
    if (error) {
        LOG_ERR("HBN", "JSON parse error: %s", error.c_str());
        hiddenBooks.clear();
        return false;
    }

    // Validazione versione formato per migrazioni future
    const int version = doc["formatVersion"] | 0;
    if (version > CURRENT_FORMAT_VERSION) {
        LOG_ERR("HBN", "Unknown format version %d, attempting best-effort load", version);
    }

    hiddenBooks.clear();
    JsonArray arr = doc["books"].as<JsonArray>();
    if (!arr.isNull()) {
        hiddenBooks.reserve(arr.size());
        for (JsonObject obj : arr) {
            // Validazione rigorosa: scarta entry malformate invece di creare entry vuote
            if (!obj.containsKey("path")) continue;
            
            HiddenBookEntry entry;
            entry.path = obj["path"].as<std::string>();
            if (entry.path.empty()) continue; // Path vuoto non valido
            
            if (obj.containsKey("bookId")) {
                entry.bookId = obj["bookId"].as<std::string>();
            }
            hiddenBooks.push_back(std::move(entry));
        }
    }

    normalizeAndDeduplicate();
    LOG_DBG("HBN", "Loaded %zu hidden books from disk", hiddenBooks.size());
    for (const auto& e : hiddenBooks) {
        LOG_DBG("HBN", "  hidden: bookId=%s path=%s", e.bookId.c_str(), e.path.c_str());
    }
    loaded_ = true;
    return true;
}

bool HiddenBooksStore::ensureLoaded() {
  if (loaded_) return true;
  loaded_ = loadFromFile();
  return loaded_;
}

bool HiddenBooksStore::saveToFile() const {
    // mkdir returns false if the directory already exists — that's fine.
    Storage.mkdir("/.crosspoint");

    JsonDocument doc;
    doc["formatVersion"] = CURRENT_FORMAT_VERSION;
    JsonArray arr = doc["books"].to<JsonArray>();
    
    for (const auto& e : hiddenBooks) {
        JsonObject obj = arr.add<JsonObject>();
        obj["bookId"] = e.bookId;
        obj["path"] = e.path;
    }

    // Write directly to the target file using serializeJson(doc, HalFile).
    // HalFile inherits from Arduino Print, so this works identically to
    // how other stores (Favorites, RecentBooks, Settings) write JSON.
    HalFile file;
    if (!Storage.openFileForWrite("HBN", HIDDEN_BOOKS_FILE, file)) {
        LOG_ERR("HBN", "Failed to open %s for writing", HIDDEN_BOOKS_FILE);
        return false;
    }
    serializeJson(doc, file);
    file.flush();
    file.close();

    LOG_DBG("HBN", "Saved %zu hidden books to %s", hiddenBooks.size(), HIDDEN_BOOKS_FILE);
    return true;
}

bool HiddenBooksStore::isHidden(const std::string& key) const {
    const std::string normKey = BookIdentity::normalizePath(key);
    return findBookIndex(normKey) >= 0;
}

int HiddenBooksStore::findBookIndex(const std::string& normalizedKey) const {
    // Assunzione: normalizedKey è già normalizzata dal chiamante
    return BookStoreUtils::findBookIndex(hiddenBooks, normalizedKey, normalizedKey);
}

bool HiddenBooksStore::addBook(const std::string& path) {
    const std::string normPath = BookIdentity::normalizePath(path);
    if (normPath.empty()) {
        LOG_ERR("HBN", "Attempted to add book with empty/unresolvable path");
        return false;
    }

    if (findBookIndex(normPath) >= 0) {
        LOG_DBG("HBN", "Book already hidden: %s", normPath.c_str());
        return true; // Già presente, successo idempotente
    }

    HiddenBookEntry entry;
    entry.path = normPath;
    
    // Risolvi bookId solo se il file esiste fisicamente
    if (Storage.exists(normPath.c_str())) {
        entry.bookId = BookIdentity::resolveStableBookId(normPath);
    } else {
        LOG_ERR("HBN", "Adding hidden entry for non-existent file: %s", normPath.c_str());
    }

    hiddenBooks.push_back(std::move(entry));
    normalizeAndDeduplicate();
    
    LOG_DBG("HBN", "Added hidden book: %s", normPath.c_str());
    return saveToFile();
}

bool HiddenBooksStore::removeBook(const std::string& path) {
    const std::string normPath = BookIdentity::normalizePath(path);
    const int idx = findBookIndex(normPath);
    
    if (idx < 0) {
        LOG_DBG("HBN", "Cannot remove, book not in hidden list: %s", normPath.c_str());
        return false;
    }
    
    hiddenBooks.erase(hiddenBooks.begin() + idx);
    LOG_DBG("HBN", "Removed hidden book: %s", normPath.c_str());
    return saveToFile();
}

bool HiddenBooksStore::toggleBook(const std::string& path) {
    const std::string normPath = BookIdentity::normalizePath(path);
    const int idx = findBookIndex(normPath);
    
    if (idx >= 0) {
        hiddenBooks.erase(hiddenBooks.begin() + idx);
        LOG_DBG("HBN", "Toggled OFF hidden book: %s", normPath.c_str());
        return saveToFile();
    }
    
    LOG_DBG("HBN", "Toggling ON hidden book: %s", normPath.c_str());
    return addBook(normPath); // addBook gestisce già normalizzazione e salvataggio
}

void HiddenBooksStore::normalizeAndDeduplicate() {
    // Normalizza tutti i path
    for (auto& e : hiddenBooks) {
        e.path = BookIdentity::normalizePath(e.path);
    }

    // Rimuovi entry con path vuoto dopo normalizzazione
    hiddenBooks.erase(
        std::remove_if(hiddenBooks.begin(), hiddenBooks.end(),
                       [](const HiddenBookEntry& e) { return e.path.empty(); }),
        hiddenBooks.end());

    // Deduplicazione O(n) usando set di chiavi composite
    // Priorità: mantiene la prima occorrenza trovata
    std::vector<HiddenBookEntry> deduped;
    deduped.reserve(hiddenBooks.size());
    
    std::unordered_set<std::string> seenPaths;
    std::unordered_set<std::string> seenIds;

    for (auto& e : hiddenBooks) {
        const bool pathSeen = seenPaths.count(e.path) > 0;
        const bool idSeen = !e.bookId.empty() && seenIds.count(e.bookId) > 0;
        
        if (!pathSeen && !idSeen) {
            seenPaths.insert(e.path);
            if (!e.bookId.empty()) {
                seenIds.insert(e.bookId);
            }
            deduped.push_back(std::move(e));
        }
    }

    hiddenBooks = std::move(deduped);
    hiddenBooks.shrink_to_fit();
}