#pragma once

// Steroids compatibility shim.
//
// Downstream Steroids code refers to the storage file type as `FsFile`, which
// in Steroids was SdFat's file class. CPR-vCodex wraps SD access behind the
// thread-safe `HalFile`, so map the Steroids name onto it here instead of
// touching `lib/hal/HalStorage.h` (which cannot define this alias because
// `HalStorage.cpp` itself includes SdFat's real `FsFile`).
//
// Downstream code must still use the `Storage` singleton for all file access.

#include <HalStorage.h>

using FsFile = HalFile;
