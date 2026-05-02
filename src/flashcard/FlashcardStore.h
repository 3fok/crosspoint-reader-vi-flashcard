#pragma once

#include <cstdint>
#include <string>

struct FlashcardCard {
  std::string front;
  std::string back;
  std::string notes;
  std::string reading;
  std::string example;
};

namespace flashcard {

void setActiveDeckFile(const std::string& deckFileName);
std::string getActiveDeckFile();

bool hasDeck();
std::string getDeckName();
uint32_t getCardCount();

std::string getDeckName(const char* deckPath);
uint32_t getCardCount(const char* deckPath);

/// Sets the currently active deck file for study operations.
void setActiveDeckPath(const std::string& deckPath);
const std::string& getActiveDeckPath();

/// Reads one card by index (0 .. count-1). Returns false if index invalid or I/O error.
bool readCard(uint32_t index, FlashcardCard& out);

/// Imports one JSON deck file and writes a matching .bin deck.
/// The output path is derived from the JSON filename and stored in /flashcards.
bool importDeckFromJsonFile(const char* jsonPath);

}  // namespace flashcard
