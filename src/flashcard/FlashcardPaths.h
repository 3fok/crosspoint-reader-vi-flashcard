#pragma once

#include <cstddef>

namespace flashcard {

constexpr const char* kFlashcardStorageDir = "/flashcards";
constexpr const char* kDeckBinPath = "/flashcards/deck.bin";
constexpr const char* kSettingsJsonPath = "/flashcards/settings.json";
constexpr size_t kMaxJsonImportBytes = 384000;
constexpr size_t kMaxFieldBytes = 2000;

}  // namespace flashcard
