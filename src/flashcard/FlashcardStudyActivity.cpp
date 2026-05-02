#include "FlashcardStudyActivity.h"

#include <esp_random.h>

#include <algorithm>
#include <sstream>

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

struct BlockLayout {
  int fontId = NOTOSANS_12_FONT_ID;
  int lineHeight = 0;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  std::vector<std::string> lines;
  int height = 0;
};

std::vector<std::string> splitUtf8Chars(const std::string& s) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len = 1;
    if ((c & 0x80) == 0) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    if (i + len > s.size()) len = 1;
    out.emplace_back(s.substr(i, len));
    i += len;
  }
  return out;
}

std::vector<std::string> wrapWithHyphenation(const GfxRenderer& r, int fontId, const std::string& text, int maxWidth,
                                             EpdFontFamily::Style style, int maxLines) {
  std::vector<std::string> lines;
  std::istringstream iss(text);
  std::string word;
  std::string line;

  auto pushLine = [&]() {
    if (!line.empty()) {
      lines.push_back(line);
      line.clear();
    }
  };

  auto fits = [&](const std::string& s) {
    return r.getTextWidth(fontId, s.c_str(), style) <= maxWidth;
  };

  while (iss >> word) {
    const std::string candidate = line.empty() ? word : line + ' ' + word;
    if (fits(candidate)) {
      line = candidate;
      continue;
    }

    pushLine();
    if (static_cast<int>(lines.size()) >= maxLines) {
      break;
    }

    if (fits(word)) {
      line = word;
      continue;
    }

    std::string part;
    for (const auto& ch : splitUtf8Chars(word)) {
      const std::string trial = part + ch;
      if (fits(trial + '-')) {
        part = trial;
        continue;
      }
      if (!part.empty()) {
        lines.push_back(part + '-');
        part = ch;
        if (static_cast<int>(lines.size()) >= maxLines) {
          break;
        }
      } else {
        part = ch;
      }
    }
    if (static_cast<int>(lines.size()) >= maxLines) {
      break;
    }
    line = part;
  }

  if (!line.empty() && static_cast<int>(lines.size()) < maxLines) {
    lines.push_back(line);
  }
  return lines;
}

BlockLayout fitBlock(const GfxRenderer& r, const std::string& text, int maxWidth, int maxHeight, EpdFontFamily::Style style,
                     const int* fonts, size_t fontCount, int maxLines) {
  BlockLayout best;
  for (size_t i = 0; i < fontCount; i++) {
    const int fontId = fonts[i];
    const int lineHeight = r.getLineHeight(fontId);
    const auto lines = wrapWithHyphenation(r, fontId, text, maxWidth, style, maxLines);
    const int height = static_cast<int>(lines.size()) * lineHeight;
    if (height <= maxHeight || i == fontCount - 1) {
      best.fontId = fontId;
      best.lineHeight = lineHeight;
      best.style = style;
      best.lines = lines;
      best.height = height;
      return best;
    }
  }
  return best;
}

void drawCenteredBlock(const GfxRenderer& r, const BlockLayout& block, int left, int width, int topY) {
  int y = topY;
  for (const auto& ln : block.lines) {
    const int tw = r.getTextWidth(block.fontId, ln.c_str(), block.style);
    const int x = left + (width - tw) / 2;
    r.drawText(block.fontId, x, y, ln.c_str(), true, block.style);
    y += block.lineHeight;
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

void FlashcardStudyActivity::onExit() { Activity::onExit(); }

void FlashcardStudyActivity::pickRandomCard() {
  const uint32_t n = flashcard::getCardCount();
  if (n == 0) {
    cardLoaded = false;
    return;
  }
  const uint32_t prevIndex = currentIndex;
  do {
    currentIndex = static_cast<uint32_t>(esp_random() % static_cast<uint32_t>(n));
  } while (n > 1 && currentIndex == prevIndex);
  cardLoaded = flashcard::readCard(currentIndex, card);
  flipped = false;
}

void FlashcardStudyActivity::flipCard() { flipped = !flipped; }

void FlashcardStudyActivity::exitStudy() {
  clearStudyState();
  activityManager.goToFlashcardMenu();
}

void FlashcardStudyActivity::drawStudyContent(const int contentTop, const int contentBottom, const int contentLeft,
                                              const int contentWidth) const {
  constexpr int kGap = 6;
  constexpr int kDividerHeight = 3;
  constexpr int kMaxFrontLines = 4;
  constexpr int kMaxNotesLines = 4;
  constexpr int kMaxBackLines = 4;
  constexpr int kMaxExampleLines = 999;

  const int availableH = contentBottom - contentTop;
  const int textWidth = contentWidth - 8;

  const int frontFonts[] = {NOTOSANS_18_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID};
  const int notesFonts[] = {DOULOSSIL_12_FONT_ID, NOTOSANS_12_FONT_ID};
  const int bodyFonts[] = {NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID};
  const int exampleFonts[] = {NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID};

  auto fitFront = [&](const std::string& text, int maxH) {
    for (size_t i = 0; i < sizeof(frontFonts) / sizeof(frontFonts[0]); i++) {
      const int fontId = frontFonts[i];
      const int lh = renderer.getLineHeight(fontId);
      const int maxLines = std::max(1, maxH / lh);
      const auto lines = wrapWithHyphenation(renderer, fontId, text, textWidth, EpdFontFamily::BOLD, std::min(maxLines, kMaxFrontLines));
      if (static_cast<int>(lines.size()) * lh <= maxH || i == (sizeof(frontFonts) / sizeof(frontFonts[0])) - 1) {
        return BlockLayout{fontId, lh, EpdFontFamily::BOLD, lines, static_cast<int>(lines.size()) * lh};
      }
    }
    return BlockLayout{};
  };

  auto fitRegular = [&](const std::string& text, int maxH, const int* fonts, size_t fontCount, int maxLines, EpdFontFamily::Style style) {
    for (size_t i = 0; i < fontCount; i++) {
      const int fontId = fonts[i];
      const int lh = renderer.getLineHeight(fontId);
      const auto lines = wrapWithHyphenation(renderer, fontId, text, textWidth, style, std::min(maxLines, std::max(1, maxH / lh)));
      if (static_cast<int>(lines.size()) * lh <= maxH || i == fontCount - 1) {
        return BlockLayout{fontId, lh, style, lines, static_cast<int>(lines.size()) * lh};
      }
    }
    return BlockLayout{};
  };

  // Reserve space for back/example and compute a centered layout dynamically.
  auto front = fitFront(card.front, availableH / 3);
  auto notes = fitRegular(formatNotesReading(card), availableH / 4, notesFonts, sizeof(notesFonts) / sizeof(notesFonts[0]), kMaxNotesLines, EpdFontFamily::REGULAR);
  auto back = flipped ? fitRegular(card.back, availableH / 4, bodyFonts, sizeof(bodyFonts) / sizeof(bodyFonts[0]), kMaxBackLines, EpdFontFamily::BOLD) : BlockLayout{};
  auto example = flipped ? fitRegular(card.example, availableH - 2 * kGap, exampleFonts, sizeof(exampleFonts) / sizeof(exampleFonts[0]), kMaxExampleLines, EpdFontFamily::REGULAR) : BlockLayout{};

  int blockH = front.height + kGap + notes.height;
  if (flipped) {
    blockH += kGap + kDividerHeight + kGap + back.height + kGap + example.height;
  }
  const int startY = contentTop + std::max(0, (availableH - blockH) / 2);
  int y = startY;

  drawCenteredBlock(renderer, front, contentLeft, contentWidth, y);
  y += front.height + kGap;
  drawCenteredBlock(renderer, notes, contentLeft, contentWidth, y);
  y += notes.height;
  if (flipped) {
    y += kGap;
    renderer.drawLine(contentLeft + 8, y + kDividerHeight / 2, contentLeft + contentWidth - 8, y + kDividerHeight / 2, kDividerHeight, true);
    y += kDividerHeight + kGap;
  }

  if (flipped) {
    drawCenteredBlock(renderer, back, contentLeft, contentWidth, y);
    y += back.height + kGap;
    drawCenteredBlock(renderer, example, contentLeft, contentWidth, y);
  }
}

void FlashcardStudyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    exitStudy();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    flipCard();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    pickRandomCard();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    flipCard();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    pickRandomCard();
    requestUpdate();
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

  const int bottomReserve = showControls ? metrics.buttonHintsHeight : 0;
  const int contentTop = metrics.topPadding;
  const int contentBottom = pageH - mb - bottomReserve;
  const int contentLeft = ml + metrics.contentSidePadding;
  const int contentWidth = pageW - ml - mr - 2 * metrics.contentSidePadding;

  if (!cardLoaded) {
    renderer.drawCenteredText(UI_12_FONT_ID, contentTop + 40, tr(STR_FLASHCARD_NO_DECK), true, EpdFontFamily::REGULAR);
  } else {
    drawStudyContent(contentTop, contentBottom, contentLeft, contentWidth);
  }

  if (showControls) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_FLASHCARD_FLIP), tr(STR_FLASHCARD_NEXT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
