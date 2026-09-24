#pragma once

// Steroids-only boot-order gate.
//
// Heavy store loads (reading stats JSON, achievements) must not run while the
// foreground activity is doing memory-sensitive boot work (Home cover
// generation needs the largest contiguous heap block available). main.cpp
// refreshes the gate from ActivityManager::deferredStoreLoadReady() on every
// loop iteration; store lazy-load paths check it and stay unloaded until the
// gate opens. This build is -fno-exceptions, so a failed vector reallocation
// inside a store load aborts the device instead of throwing — the gate is what
// keeps those loads off the memory-critical path.
//
// Store code (src/ReadingStatsStore.cpp) must not depend on activity classes,
// hence this tiny leaf utility.

namespace boot_load_gate {

void setReady(bool ready);
bool ready();

}  // namespace boot_load_gate
