#include "BootLoadGate.h"

namespace boot_load_gate {
namespace {
// Single-core firmware: plain bool is fine; aligned 32-bit stores are atomic
// on the RISC-V cores and the gate is advisory (worst case one extra loop of
// delay before a lazy load runs).
bool gateReady = false;
}  // namespace

void setReady(const bool ready) { gateReady = ready; }
bool ready() { return gateReady; }

}  // namespace boot_load_gate
