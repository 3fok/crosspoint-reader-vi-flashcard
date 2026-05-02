#include "FlashcardMenuActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <strings.h>

#include "activities/ActivityManager.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FlashcardPaths.h"
#include "FlashcardPrefs.h"
#include "FlashcardStore.h"
#include "FlashcardStudyActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr unsigned long kGoRootHoldMs = 1000;

void sortEntries(std::vector<std::string>& strs) {
  std::sort(strs.begin(), strs.end(), [](const std::string& a, const std::string& b) {
    const bool d1 = !a.empty() && a.back() == '/';
    const bool d2 = !b.empty() && b.back() == '/';
    if (d1 != d2) return d1;
    return strcasecmp(a.c_str(), b.c_str()) < 0;
  });
}

std::string joinPath(const std::string& base, const std::string& name) {
  if (base == "/" || base.empty()) return std::string("/") + name;
  if (base.back() == '/') return base + name;
  return base + "/" + name;
}

bool hasBinExt(const std::string& s) { return FsHelpers::checkFileExtension(std::string_view{s}, ".bin"); }

}  // namespace

bool FlashcardMenuActivity::isExcludedSettingsJson(const char* baseName) {
  return strcasecmp(baseName, "flashcard_settings.json") == 0;
}

FlashcardMenuActivity::FlashcardMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("FlashcardMenu", renderer, mappedInput) {}

void FlashcardMenuActivity::onEnter() {
  Activity::onEnter();
  panel = Panel::Root;
  selectorIndex = 0;
  importBasePath = "/";
  importEntries.clear();
  deckEntries.clear();
  statusMessage.clear();
  (void)flashcard::loadShowControls(showControlsPref);
  loadDeckEntries();
  requestUpdate();
}

void FlashcardMenuActivity::onExit() { Activity::onExit(); }

void FlashcardMenuActivity::loadImportEntries() {
  importEntries.clear();
  auto root = Storage.open(importBasePath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }
  root.rewindDirectory();
  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }
    if (file.isDirectory()) {
      importEntries.emplace_back(std::string(name) + "/");
    } else if (FsHelpers::checkFileExtension(std::string_view{name, strlen(name)}, ".json")) {
      if (isExcludedSettingsJson(name)) {
        file.close();
        continue;
      }
      importEntries.emplace_back(name);
    }
    file.close();
  }
  root.close();
  sortEntries(importEntries);
}

void FlashcardMenuActivity::loadDeckEntries() {
  deckEntries.clear();
  auto root = Storage.open("/flashcards");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }
  root.rewindDirectory();
  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }
    if (!file.isDirectory() && hasBinExt(std::string{name})) {
      deckEntries.emplace_back(name);
    }
    file.close();
  }
  root.close();
  sortEntries(deckEntries);
}

int FlashcardMenuActivity::listCountForPanel() const {
  switch (panel) {
    case Panel::Root: return 3;
    case Panel::DeckList: return static_cast<int>(deckEntries.size());
    case Panel::ImportDeck: return static_cast<int>(importEntries.size());
    case Panel::Settings: return 1;
  }
  return 0;
}

void FlashcardMenuActivity::openStudy(const std::string& deckPath) {
  if (!Storage.exists(deckPath.c_str())) return;
  const size_t pos = deckPath.find_last_of('/');
  if (pos != std::string::npos && pos + 1 < deckPath.size()) {
    flashcard::setActiveDeckFile(deckPath.substr(pos + 1));
  }
  startActivityForResult(std::make_unique<FlashcardStudyActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) {
                           (void)flashcard::loadShowControls(showControlsPref);
                           loadDeckEntries();
                           requestUpdate();
                         });
}

void FlashcardMenuActivity::loop() {
  const int count = listCountForPanel();

  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= kGoRootHoldMs &&
      panel == Panel::ImportDeck && importBasePath != "/") {
    importBasePath = "/";
    loadImportEntries();
    selectorIndex = 0;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (panel == Panel::Root) {
      onGoHome();
      return;
    }
    if (panel == Panel::ImportDeck && importBasePath != "/") {
      const size_t pos = importBasePath.find_last_of('/');
      if (pos == 0) importBasePath = "/";
      else if (pos != std::string::npos) importBasePath = importBasePath.substr(0, pos);
      else importBasePath = "/";
      loadImportEntries();
      selectorIndex = 0;
      requestUpdate();
      return;
    }
    statusMessage.clear();
    panel = Panel::Root;
    selectorIndex = 0;
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, count] {
    if (count <= 0) return;
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, count] {
    if (count <= 0) return;
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
    requestUpdate();
  });

  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm) || count <= 0) return;

  switch (panel) {
    case Panel::Root:
      if (selectorIndex == 0) {
        panel = Panel::DeckList;
        selectorIndex = 0;
      } else if (selectorIndex == 1) {
        panel = Panel::ImportDeck;
        importBasePath = "/";
        loadImportEntries();
        selectorIndex = 0;
      } else {
        panel = Panel::Settings;
        selectorIndex = 0;
        (void)flashcard::loadShowControls(showControlsPref);
      }
      requestUpdate();
      return;
    case Panel::DeckList:
      if (!deckEntries.empty()) {
        const std::string deckPath = std::string("/flashcards/") + deckEntries[static_cast<size_t>(selectorIndex)];
        flashcard::setActiveDeckPath(deckPath);
        openStudy(deckPath);
      }
      return;
    case Panel::ImportDeck: {
      if (importEntries.empty()) return;
      const std::string& entry = importEntries[static_cast<size_t>(selectorIndex)];
      if (!entry.empty() && entry.back() == '/') {
        importBasePath = joinPath(importBasePath, entry.substr(0, entry.size() - 1));
        loadImportEntries();
        selectorIndex = 0;
        requestUpdate();
        return;
      }
      const std::string fullPath = joinPath(importBasePath, entry);
      if (flashcard::importDeckFromJsonFile(fullPath.c_str())) {
        statusMessage = tr(STR_FLASHCARD_IMPORT_OK);
        loadDeckEntries();
        panel = Panel::DeckList;
        selectorIndex = 0;
      } else {
        statusMessage = tr(STR_FLASHCARD_IMPORT_FAIL);
      }
      requestUpdate();
      return;
    }
    case Panel::Settings:
      showControlsPref = !showControlsPref;
      flashcard::saveShowControls(showControlsPref);
      requestUpdate();
      return;
  }
}

void FlashcardMenuActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  int mt = 0, mr = 0, mb = 0, ml = 0;
  renderer.getOrientedViewableTRBL(&mt, &mr, &mb, &ml);

  renderer.clearScreen();

  const char* title = tr(STR_FLASHCARD);
  const char* sub = nullptr;
  if (panel == Panel::ImportDeck) sub = importBasePath.c_str();
  else if (panel == Panel::DeckList) sub = tr(STR_FLASHCARD_DECK_LIST);
  else if (panel == Panel::Settings) sub = tr(STR_FLASHCARD_SETTINGS);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, title, sub);

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageH - listTop - metrics.buttonHintsHeight - (statusMessage.empty() ? 0 : metrics.listRowHeight) - mb;
  const Rect listRect{ml + metrics.contentSidePadding, listTop, pageW - ml - mr - 2 * metrics.contentSidePadding, listHeight};

  switch (panel) {
    case Panel::Root: {
      GUI.drawList(renderer, listRect, 3, selectorIndex,
                   [](int i) -> std::string {
                     if (i == 0) return tr(STR_FLASHCARD_DECK_LIST);
                     if (i == 1) return tr(STR_FLASHCARD_CREATE_DB);
                     return tr(STR_FLASHCARD_SETTINGS);
                   },
                   nullptr, nullptr, nullptr, false);
      break;
    }
    case Panel::DeckList: {
      if (deckEntries.empty()) {
        GUI.drawHelpText(renderer, listRect, tr(STR_FLASHCARD_NO_DECK));
      } else {
        GUI.drawList(renderer, listRect, static_cast<int>(deckEntries.size()), selectorIndex,
                     [this](int i) {
                       const size_t idx = static_cast<size_t>(i);
                       const std::string path = std::string("/flashcards/") + deckEntries[idx];
                       return flashcard::getDeckName(path.c_str());
                     },
                     [this](int i) {
                       const size_t idx = static_cast<size_t>(i);
                       const std::string path = std::string("/flashcards/") + deckEntries[idx];
                       char countBuf[48];
                       snprintf(countBuf, sizeof(countBuf), "%s: %u", tr(STR_FLASHCARDS),
                                static_cast<unsigned>(flashcard::getCardCount(path.c_str())));
                       return std::string(countBuf);
                     },
                     nullptr, nullptr, false);
      }
      break;
    }
    case Panel::ImportDeck: {
      const int n = static_cast<int>(importEntries.size());
      if (n == 0) {
        GUI.drawHelpText(renderer, listRect, tr(STR_FLASHCARD_NO_JSON));
      } else {
        GUI.drawList(renderer, listRect, n, selectorIndex,
                     [this](int i) { return importEntries[static_cast<size_t>(i)]; }, nullptr, nullptr, nullptr, false);
      }
      break;
    }
    case Panel::Settings: {
      char buf[80];
      snprintf(buf, sizeof(buf), "%s: %s", tr(STR_FLASHCARD_SHOW_CONTROLS), showControlsPref ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
      const std::string settingsLine(buf);
      GUI.drawList(renderer, listRect, 1, selectorIndex, [&settingsLine](int) { return settingsLine; }, nullptr, nullptr, nullptr, false);
      break;
    }
  }

  if (!statusMessage.empty()) {
    const int msgY = pageH - mb - metrics.buttonHintsHeight - metrics.listRowHeight;
    renderer.drawCenteredText(SMALL_FONT_ID, msgY, statusMessage.c_str(), true, EpdFontFamily::REGULAR);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
