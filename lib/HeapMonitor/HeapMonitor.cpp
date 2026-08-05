#include "HeapMonitor.hpp"

HeapMonitor::HeapMonitor(uint32_t intervalMs) : _intervalMs(intervalMs), _lastPrintMs(0) {}

void HeapMonitor::begin() {
  printNow();
  _lastPrintMs = millis();
}

void HeapMonitor::update() {
  uint32_t now = millis();
  if (now - _lastPrintMs >= _intervalMs) {
    _lastPrintMs = now;
    printNow();
  }
}

void HeapMonitor::setInterval(uint32_t intervalMs) { _intervalMs = intervalMs; }

size_t HeapMonitor::getFreeHeap() const { return heap_caps_get_free_size(MALLOC_CAP_DEFAULT); }

size_t HeapMonitor::getLargestFreeBlock() const {
  return heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
}

bool HeapMonitor::_psramAvailable() const {
  // Якщо PSRAM не сконфігурований/відсутній, total buffers для SPIRAM = 0
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
  return (info.total_free_bytes + info.total_allocated_bytes) > 0;
}

void HeapMonitor::printNow() {
  multi_heap_info_t defaultInfo;
  heap_caps_get_info(&defaultInfo, MALLOC_CAP_DEFAULT);

  multi_heap_info_t internalInfo;
  heap_caps_get_info(&internalInfo, MALLOC_CAP_INTERNAL);

  _logger.info("---- HeapMonitor ----");

  _logger.info("Default   | free: %8u | largest block: %8u | min ever: %8u",
               (unsigned)defaultInfo.total_free_bytes, (unsigned)defaultInfo.largest_free_block,
               (unsigned)defaultInfo.minimum_free_bytes);

  _logger.info("Internal  | free: %8u | largest block: %8u | min ever: %8u",
               (unsigned)internalInfo.total_free_bytes, (unsigned)internalInfo.largest_free_block,
               (unsigned)internalInfo.minimum_free_bytes);

  if (_psramAvailable()) {
    multi_heap_info_t psramInfo;
    heap_caps_get_info(&psramInfo, MALLOC_CAP_SPIRAM);

    _logger.info("PSRAM     | free: %8u | largest block: %8u | min ever: %8u",
                 (unsigned)psramInfo.total_free_bytes, (unsigned)psramInfo.largest_free_block,
                 (unsigned)psramInfo.minimum_free_bytes);
  } else {
    _logger.info("PSRAM     | not available");
  }

  _logger.info("---------------------");
}
