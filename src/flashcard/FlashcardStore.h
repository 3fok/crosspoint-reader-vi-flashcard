#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct FlashcardCard {
  std::string front;
  std::string back;
  std::string notes;
  std::string reading;
  std::string example;
};

namespace flashcard {

bool hasDeck();
std::string getDeckName();
uint32_t getCardCount();

/// Reads one card by index (0 .. count-1). Returns false if index invalid or I/O error.
bool readCard(uint32_t index, FlashcardCard& out);

/// Deletes any existing deck file, parses JSON from SD path, writes binary deck. Returns false on error.
bool importDeckFromJsonFile(const char* jsonPath);

}  // namespace flashcard
