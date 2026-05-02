#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "FlashcardStore.h"

class FlashcardStudyActivity final : public Activity {
 public:
  explicit FlashcardStudyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  bool showControls = true;
  bool flipped = false;
  uint32_t currentIndex = 0;
  FlashcardCard card;
  bool cardLoaded = false;

  void persistStudyState();
  void clearStudyState();
  void pickRandomCard();
  void flipCard();
  void exitStudy();
  void drawStudyContent(int contentTop, int contentBottom, int contentLeft, int contentWidth) const;
};
