#include "HalSpiBus.h"

namespace {
SemaphoreHandle_t s_spiMutex = nullptr;
}  // namespace

void HalSpiBus::begin() {
  if (!s_spiMutex) {
    s_spiMutex = xSemaphoreCreateRecursiveMutex();
  }
}

SemaphoreHandle_t HalSpiBus::mutex() {
  return s_spiMutex;
}

HalSpiBus::Lock::Lock() {
  if (s_spiMutex) {
    xSemaphoreTakeRecursive(s_spiMutex, portMAX_DELAY);
  }
}

HalSpiBus::Lock::~Lock() {
  if (s_spiMutex) {
    xSemaphoreGiveRecursive(s_spiMutex);
  }
}
