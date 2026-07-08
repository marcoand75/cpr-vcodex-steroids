#pragma once

// Version symbols are defined in version.generated.inc (included by version.cpp)
// so dev-counter bumps recompile only that translation unit.

extern const char CPR_CROSSPOINT_VERSION[];
#define CROSSPOINT_VERSION CPR_CROSSPOINT_VERSION

extern const char CPR_VCODEX_BASE_VERSION[];
#define VCODEX_BASE_VERSION CPR_VCODEX_BASE_VERSION

extern const int CPR_VCODEX_BUILD_SEQ;
#define VCODEX_BUILD_SEQ CPR_VCODEX_BUILD_SEQ

extern const int CPR_VCODEX_RELEASE_SEQ;
#define VCODEX_RELEASE_SEQ CPR_VCODEX_RELEASE_SEQ

extern const char CPR_VCODEX_BUILD_KIND[];
#define VCODEX_BUILD_KIND CPR_VCODEX_BUILD_KIND
