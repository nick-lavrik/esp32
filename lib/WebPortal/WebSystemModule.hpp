#pragma once

// Розділ "system": розширена діагностика пристрою для вкладки System
// (heap-фрагментація, флеш і таблиця розділів, статистика NVS, SD-картка,
// місткість LittleFS).
//
// Чому не /api/status. Ці дані або чіпають flash (partition table, NVS
// stats, SD), або дорогі рахувати на кожен тік (heap_caps_* по всій купі), а
// /api/status - гарячий роут швидкого опиту (кожні 10 с, завжди). Тому вони
// живуть в окремому GET-роуті, що йде через WebJobQueue, і сторінка тягне
// його не за таймером, а один раз при відкритті вкладки System і за кліком
// Refresh (assets/www/index.html, кнопка поруч із h2 "Memory" - той самий
// шаблон варто повторити для наступних дорогих/рідкісних блоків порталу).

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <functional>

#include "IWebModule.hpp"

// Дані про SD-картку. Модуль сам SD не чіпає: яка саме шина (SD чи SD_MMC)
// підключена, вирішує плата (BOARD_HAS_SD/SD_USE_SDMMC, src/main.cpp) - тому
// дані приходять через колбек, а не пряме звернення до SD.h/SD_MMC.h звідси.
struct WebSystemSdInfo {
  bool present = false;
  String cardType;
  uint64_t sizeBytes = 0;
  uint64_t usedBytes = 0;
};

class WebSystemModule : public IWebModule {
public:
  // Той самий колбек, що йде у WebFilesModule (main.cpp, setupWebPortal()) -
  // формула used/total для LittleFS одна на весь портал.
  using FsUsageFn = std::function<bool(size_t& used, size_t& total)>;

  // nullptr = на платі немає SD (BOARD_HAS_SD=0) - блок ховається в UI.
  using SdInfoFn = std::function<bool(WebSystemSdInfo&)>;

  explicit WebSystemModule(FsUsageFn littleFsUsage, SdInfoFn sdInfo = nullptr)
      : _littleFsUsage(std::move(littleFsUsage)), _sdInfo(std::move(sdInfo)) {}

  const char* name() const override { return "system"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;

private:
  // WebJobQueue (loop()): partition table і NVS-статистика - flash I/O, тому
  // весь звіт іде одним job'ом, а не окремою задачею на кожен блок.
  String _infoJob();

  FsUsageFn _littleFsUsage;
  SdInfoFn _sdInfo;
};
