#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  enum class LastScreen : uint8_t { None = 0, Reader = 1, Flashcard = 2 };

  std::string openEpubPath;
  uint8_t lastSleepImage = UINT8_MAX;  // UINT8_MAX = unset sentinel
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  LastScreen lastScreen = LastScreen::None;
  /// When lastScreen == Flashcard, must match the deck stored in flashcard_deck.bin for resume.
  std::string flashcardDeckName;
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();

 private:
  bool loadFromBinaryFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
