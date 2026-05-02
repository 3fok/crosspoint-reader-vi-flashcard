#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FlashcardMenuActivity final : public Activity {
 public:
  explicit FlashcardMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  enum class Panel { Root, DeckList, ImportDeck, Settings };

  ButtonNavigator buttonNavigator;
  Panel panel = Panel::Root;
  int selectorIndex = 0;
  std::string importBasePath = "/";
  std::vector<std::string> importEntries;
  bool showControlsPref = true;
  std::string statusMessage;
  std::vector<std::string> deckEntries;

  void loadImportEntries();
  void loadDeckEntries();
  int listCountForPanel() const;
  void openStudy(const std::string& deckPath);
  static bool isExcludedSettingsJson(const char* baseName);
};
