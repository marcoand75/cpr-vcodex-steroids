#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ReaderMenuRegistry.h"
#include "../util/ListInputMapper.h"

class ReaderMenuOrderActivity final : public Activity {
   public:
    explicit ReaderMenuOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("ReaderMenuOrder", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    void render(RenderLock&&) override;

    std::vector<const ReaderMenuItemDefinition*> entries;
    int selectedIndex = 0;
    bool moveMode = false;

private:
     void reloadEntries();
   public:
     void moveSelectedEntry(int delta);

    ListInputMapper listInputMapper;
    const char* getTitle() const;

    static void onBack(void* ctx);
    static void onConfirm(void* ctx);
    static void onNav(void* ctx, int delta);
};