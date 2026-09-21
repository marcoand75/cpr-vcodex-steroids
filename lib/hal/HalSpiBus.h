#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Recursive mutex wrapper for the shared SPI bus.
// All display, touch, and SD card SPI transactions must acquire
// this lock to prevent concurrent bus access corruption.
class HalSpiBus {
 public:
  static void begin();

  // Returns the raw semaphore handle for use in xSemaphoreTakeRecursive
  // within driver code that runs from ISR or cannot construct a Lock.
  static SemaphoreHandle_t mutex();

  // RAII lock that acquires the mutex on construction and releases it
  // on destruction.  Safe to nest (recursive).
  class Lock {
   public:
    Lock();
    ~Lock();

   private:
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };
};
