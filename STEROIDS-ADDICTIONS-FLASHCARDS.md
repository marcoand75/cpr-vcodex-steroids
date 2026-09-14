# CPR-vCodex Steroids — Flashcards (Spaced Repetition)

> **SCOPE:** Complete technical reference for the flashcards subsystem.

---

## 1. Architecture Overview

```
FlashcardsAppActivity (main hub)
    │
    ├─ FlashcardBrowserActivity (deck list)
    ├─ FlashcardReviewActivity (review session)
    ├─ FlashcardDeckStatsActivity (per-deck statistics)
    ├─ FlashcardSessionSummaryActivity (session results)
    ├─ FlashcardSettingsActivity (algorithm config)
    ├─ FlashcardStatsActivity (global stats)
    └─ FlashcardRecentsActivity (recent reviews)
```

---

## 2. Data Model

### 2.1 Deck
```cpp
struct FlashcardDeck {
    uint32_t id;
    std::string name;
    std::string description;
    uint32_t createdAt;
    AlgorithmConfig config;  // SM-2 parameters
};
```

### 2.2 Card
```cpp
struct Flashcard {
    uint32_t id;
    uint32_t deckId;
    std::string front;       // Question
    std::string back;        // Answer
    // SM-2 state:
    int interval;            // Days until next review
    int repetitions;         // Successful recalls
    float easeFactor;        // Default 2.5
    uint32_t nextReview;     // Unix timestamp
    uint32_t lastReviewed;
};
```

---

## 3. SM-2 Algorithm (SuperMemo 2)

### 3.1 Review Grades
| Grade | Meaning | Effect |
|-------|---------|--------|
| 0 | Complete blackout | Reset interval, easeFactor -= 0.2 |
| 1 | Incorrect, recalled after | Reset interval, easeFactor -= 0.15 |
| 2 | Incorrect, easy after | Reset interval, easeFactor -= 0.1 |
| 3 | Correct, difficult | interval *= easeFactor |
| 4 | Correct, hesitant | interval *= easeFactor * 1.3 |
| 5 | Perfect | interval *= easeFactor * 1.3 |

### 3.2 Ease Factor Bounds
```cpp
easeFactor = std::max(1.3f, easeFactor);
```

---

## 4. Review Session Flow

```
FlashcardReviewActivity
    │
    ├─ Load due cards (nextReview <= now)
    ├─ Shuffle (configurable)
    ├─ For each card:
    │   ├─ Show front
    │   ├─ User reveals back
    │   ├─ User grades (0-5)
    │   ├─ Update SM-2 state
    │   └─ Persist immediately
    └─ SessionSummary → FlashcardSessionSummaryActivity
```

---

## 5. Statistics

| Metric | Source |
|--------|--------|
| Cards due today | `nextReview <= now` |
| Review streak | Consecutive days with reviews |
| Average ease | Mean of all card easeFactors |
| Retention rate | (Grade ≥ 3) / Total reviews |
| Time per card | Session duration / card count |

---

## 6. Settings (`FlashcardSettingsActivity`)

```cpp
struct AlgorithmConfig {
    int newCardInterval1 = 1;      // First interval (days)
    int newCardInterval2 = 6;      // Second interval (days)
    float easeFactorInit = 2.5f;   // Starting ease
    float easeFactorMin = 1.3f;    // Minimum ease
    float easyBonus = 1.3f;        // Grade 4/5 multiplier
    int maxInterval = 36500;       // Max interval (days)
    bool shuffleReviews = true;
};
```

---

## 7. Storage

`FlashcardsStore` — JSON persistence:
```
/.crosspoint/flashcards/
├── decks.json      # Deck metadata
└── cards.json      # All cards (SM-2 state)
```

---

## 8. Key Files

| File | Role |
|------|------|
| `src/activities/apps/FlashcardsAppActivity.h/cpp` | Main hub |
| `src/activities/apps/FlashcardBrowserActivity.h/cpp` | Deck browser |
| `src/activities/apps/FlashcardReviewActivity.h/cpp` | Review session |
| `src/activities/apps/FlashcardDeckStatsActivity.h/cpp` | Deck statistics |
| `src/activities/apps/FlashcardSessionSummaryActivity.h/cpp` | Session results |
| `src/activities/apps/FlashcardSettingsActivity.h/cpp` | Algorithm config |
| `src/activities/apps/FlashcardStatsActivity.h/cpp` | Global stats |
| `src/activities/apps/FlashcardRecentsActivity.h/cpp` | Recent reviews |
| `src/FlashcardsStore.h/cpp` | JSON persistence |
| `src/components/FlashcardSM2.h/cpp` | Algorithm implementation |

---

## 9. Related Documents

- **App Registration:** `STEROIDS-ADDICTIONS.md` §3
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` (all flashcard files protected)
- **Optimizations:** `STEROIDS-OPTIMIZATION.md` (unrelated)

---

*Last updated: 2026-09-14*