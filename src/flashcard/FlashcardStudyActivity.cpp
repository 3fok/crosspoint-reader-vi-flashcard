#include "FlashcardStudyActivity.h"

#include <esp_random.h>

#include <GfxRenderer.h>
#include <I18n.h>

#include "activities/ActivityManager.h"
#include "CrossPointState.h"
#include "FlashcardPrefs.h"
#include "FlashcardStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

std::string formatNotesReading(const FlashcardCard& c) {
  std::string out = c.notes;
  if (!c.reading.empty()) {
    if (!out.empty()) {
      out += ' ';
    }
    out += '/';
    out += c.reading;
    out += '/';
  }
  return out;
}

int pickFrontFont(const GfxRenderer& r, const char* text, int maxW) {
  const int ids[] = {NOTOSANS_18_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID};
  for (const int id : ids) {
    if (r.getTextWidth(id, text, EpdFontFamily::BOLD) <= maxW) {
      return id;
    }
  }
  return NOTOSANS_12_FONT_ID;
}

void drawCenteredLines(const GfxRenderer& r, int fontId, int left, int width, int topY, const std::vector<std::string>& lines,
                       EpdFontFamily::Style style, int lineSkip) {
  int y = topY;
  for (const auto& ln : lines) {
    const int tw = r.getTextWidth(fontId, ln.c_str(), style);
    const int x = left + (width - tw) / 2;
    r.drawText(fontId, x, y, ln.c_str(), true, style);
    y += lineSkip;
  }
}

}  // namespace

FlashcardStudyActivity::FlashcardStudyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("FlashcardStudy", renderer, mappedInput) {}

void FlashcardStudyActivity::persistStudyState() {
  APP_STATE.lastScreen = CrossPointState::LastScreen::Flashcard;
  APP_STATE.flashcardDeckName = flashcard::getDeckName();
  APP_STATE.saveToFile();
}

void FlashcardStudyActivity::clearStudyState() {
  APP_STATE.lastScreen = CrossPointState::LastScreen::None;
  APP_STATE.flashcardDeckName.clear();
  APP_STATE.saveToFile();
}

void FlashcardStudyActivity::onEnter() {
  Activity::onEnter();
  (void)flashcard::loadShowControls(showControls);
  flipped = false;
  history.clear();
  cardLoaded = false;
  const uint32_t n = flashcard::getCardCount();
  if (n == 0) {
    requestUpdate();
    return;
  }
  currentIndex = static_cast<uint32_t>(esp_random() % static_cast<uint32_t>(n));
  cardLoaded = flashcard::readCard(currentIndex, card);
  persistStudyState();
  requestUpdate();
}

void FlashcardStudyActivity::onExit() {
  clearStudyState();
  Activity::onExit();
}

void FlashcardStudyActivity::pickRandomCard(bool pushCurrentToHistory) {
  const uint32_t n = flashcard::getCardCount();
  if (n == 0) {
    cardLoaded = false;
    return;
  }
  if (pushCurrentToHistory && cardLoaded) {
    history.push_back(currentIndex);
  }
  currentIndex = static_cast<uint32_t>(esp_random() % static_cast<uint32_t>(n));
  cardLoaded = flashcard::readCard(currentIndex, card);
  flipped = false;
}

void FlashcardStudyActivity::flipCard() { flipped = !flipped; }

void FlashcardStudyActivity::historyBack() {
  if (history.empty()) {
    return;
  }
  currentIndex = history.back();
  history.pop_back();
  cardLoaded = flashcard::readCard(currentIndex, card);
  flipped = false;
}

void FlashcardStudyActivity::exitStudy() {
  if (activityManager.canPopActivity()) {
    finish();
  } else {
    activityManager.goToFlashcardMenu();
  }
}

void FlashcardStudyActivity::drawStudyContent(const int contentTop, const int contentBottom, const int contentLeft,
                                              const int contentWidth) const {
  const int lh14 = renderer.getLineHeight(NOTOSANS_14_FONT_ID);

  // Fixed slot heights (stable layout when flipping).
  constexpr int kFrontSlot = 80;
  constexpr int kNotesSlot = 44;
  constexpr int kDividerSlot = 28;
  constexpr int kBackSlot = 72;
  constexpr int kExampleSlot = 120;
  const int blockH = kFrontSlot + kNotesSlot + kDividerSlot + kBackSlot + kExampleSlot;
  const int availH = contentBottom - contentTop;
  int y = contentTop;
  if (availH > blockH) {
    y += (availH - blockH) / 2;
  }

  const int frontFont = pickFrontFont(renderer, card.front.c_str(), contentWidth - 8);
  const int frontLh = renderer.getLineHeight(frontFont);
  const std::string frontDraw =
      renderer.truncatedText(frontFont, card.front.c_str(), contentWidth - 8, EpdFontFamily::BOLD);
  const int fy = y + (kFrontSlot - frontLh) / 2;
  const int frontTw = renderer.getTextWidth(frontFont, frontDraw.c_str(), EpdFontFamily::BOLD);
  const int frontX = contentLeft + (contentWidth - frontTw) / 2;
  renderer.drawText(frontFont, frontX, fy, frontDraw.c_str(), true, EpdFontFamily::BOLD);

  y += kFrontSlot;
  const std::string nr = formatNotesReading(card);
  const auto nrLines = renderer.wrappedText(NOTOSANS_14_FONT_ID, nr.c_str(), contentWidth - 8, 2, EpdFontFamily::REGULAR);
  drawCenteredLines(renderer, NOTOSANS_14_FONT_ID, contentLeft, contentWidth, y + (kNotesSlot - static_cast<int>(nrLines.size()) * lh14) / 2,
                    nrLines, EpdFontFamily::REGULAR, lh14);

  y += kNotesSlot;
  const int lineY = y + kDividerSlot / 2;
  renderer.drawLine(contentLeft + 8, lineY, contentLeft + contentWidth - 8, lineY, 2, true);
  y += kDividerSlot;

  const std::string backText = flipped ? card.back : std::string();
  const std::string exText = flipped ? card.example : std::string();

  const int backFont = NOTOSANS_16_FONT_ID;
  const auto backLines =
      backText.empty() ? std::vector<std::string>{}
                       : renderer.wrappedText(backFont, backText.c_str(), contentWidth - 8, 3, EpdFontFamily::REGULAR);
  const int backLh = renderer.getLineHeight(backFont);
  const int backBlockH = static_cast<int>(backLines.empty() ? backLh : backLines.size() * backLh);
  drawCenteredLines(renderer, backFont, contentLeft, contentWidth, y + (kBackSlot - backBlockH) / 2, backLines, EpdFontFamily::REGULAR,
                    backLh);
  y += kBackSlot;

  const int exFont = NOTOSANS_12_FONT_ID;
  const auto exLines =
      exText.empty() ? std::vector<std::string>{}
                     : renderer.wrappedText(exFont, exText.c_str(), contentWidth - 8, 6, EpdFontFamily::REGULAR);
  const int exLh = renderer.getLineHeight(exFont);
  const int exBlockH = static_cast<int>(exLines.empty() ? exLh : exLines.size() * exLh);
  drawCenteredLines(renderer, exFont, contentLeft, contentWidth, y + (kExampleSlot - exBlockH) / 2, exLines, EpdFontFamily::REGULAR, exLh);

}

void FlashcardStudyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    flipCard();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    pickRandomCard(true);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    historyBack();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    pickRandomCard(true);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    flipCard();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    exitStudy();
    return;
  }
}

void FlashcardStudyActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  int mt = 0, mr = 0, mb = 0, ml = 0;
  renderer.getOrientedViewableTRBL(&mt, &mr, &mb, &ml);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, tr(STR_FLASHCARD));

  constexpr int kControlBar = 52;
  const int bottomReserve = showControls ? (kControlBar + metrics.buttonHintsHeight) : metrics.buttonHintsHeight;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageH - mb - bottomReserve;
  const int contentLeft = ml + metrics.contentSidePadding;
  const int contentWidth = pageW - ml - mr - 2 * metrics.contentSidePadding;

  if (!cardLoaded) {
    renderer.drawCenteredText(UI_12_FONT_ID, contentTop + 40, tr(STR_FLASHCARD_NO_DECK), true, EpdFontFamily::REGULAR);
  } else {
    drawStudyContent(contentTop, contentBottom, contentLeft, contentWidth);
  }

  if (showControls) {
    const int barY = pageH - mb - metrics.buttonHintsHeight - kControlBar;
    renderer.drawLine(contentLeft, barY, contentLeft + contentWidth, barY, 1, true);
    const int seg = contentWidth / 4;
    const char* labels[] = {tr(STR_FLASHCARD_BAR_BACK), "Ease", tr(STR_FLASHCARD_FLIP), tr(STR_FLASHCARD_NEXT)};
    for (int i = 0; i < 4; i++) {
      const int cx = contentLeft + seg * i + seg / 2;
      const int tw = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
      renderer.drawText(SMALL_FONT_ID, cx - tw / 2, barY + 18, labels[i]);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_FLASHCARD_FLIP), tr(STR_FLASHCARD_PREV_CARD),
                                              tr(STR_FLASHCARD_NEXT_CARD));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_FLASHCARD_FLIP), tr(STR_FLASHCARD_NEXT));

  renderer.displayBuffer();
}
