#include "FlashcardStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "FlashcardPaths.h"

namespace {

constexpr char kMagic[4] = {'C', 'P', 'F', 'D'};
constexpr uint8_t kVersion = 1;

bool readU32(HalFile& f, uint32_t& v) {
  uint8_t b[4];
  if (f.read(b, 4) != 4) return false;
  v = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
      (static_cast<uint32_t>(b[3]) << 24);
  return true;
}

bool writeU32(HalFile& f, uint32_t v) {
  uint8_t b[4] = {static_cast<uint8_t>(v & 0xff), static_cast<uint8_t>((v >> 8) & 0xff),
                  static_cast<uint8_t>((v >> 16) & 0xff), static_cast<uint8_t>((v >> 24) & 0xff)};
  return f.write(b, 4) == 4;
}

bool readU16(HalFile& f, uint16_t& v) {
  uint8_t b[2];
  if (f.read(b, 2) != 2) return false;
  v = static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
  return true;
}

bool writeU16(HalFile& f, uint16_t v) {
  uint8_t b[2] = {static_cast<uint8_t>(v & 0xff), static_cast<uint8_t>((v >> 8) & 0xff)};
  return f.write(b, 2) == 2;
}

bool readStringField(HalFile& f, std::string& out, size_t maxLen) {
  uint16_t len = 0;
  if (!readU16(f, len) || len > maxLen) return false;
  if (len == 0) {
    out.clear();
    return true;
  }
  std::vector<char> buf(len);
  if (f.read(buf.data(), len) != static_cast<int>(len)) return false;
  out.assign(buf.data(), len);
  return true;
}

bool writeStringField(HalFile& f, const std::string& s) {
  const size_t n = s.size();
  if (n > flashcard::kMaxFieldBytes) return false;
  if (!writeU16(f, static_cast<uint16_t>(n))) return false;
  if (n > 0 && f.write(s.data(), n) != n) return false;
  return true;
}

bool readHeaderDeckNameCount(HalFile& f, std::string& deckName, uint32_t& nCards) {
  char magic[4];
  if (f.read(magic, 4) != 4 || memcmp(magic, kMagic, 4) != 0) return false;
  uint8_t ver = 0;
  if (f.read(&ver, 1) != 1 || ver != kVersion) return false;
  uint8_t pad[3];
  if (f.read(pad, 3) != 3) return false;
  uint32_t nameLen = 0;
  if (!readU32(f, nameLen) || nameLen > 512) return false;
  deckName.resize(nameLen);
  if (nameLen > 0 && f.read(deckName.data(), nameLen) != static_cast<int>(nameLen)) return false;
  if (!readU32(f, nCards) || nCards == 0 || nCards > 50000) return false;
  return true;
}

}  // namespace

namespace flashcard {

bool hasDeck() { return Storage.exists(kDeckBinPath); }

std::string getDeckName() {
  HalFile f;
  if (!Storage.openFileForRead("FC", kDeckBinPath, f)) return {};
  std::string deckName;
  uint32_t nCards = 0;
  if (!readHeaderDeckNameCount(f, deckName, nCards)) {
    f.close();
    return {};
  }
  f.close();
  return deckName;
}

uint32_t getCardCount() {
  HalFile f;
  if (!Storage.openFileForRead("FC", kDeckBinPath, f)) return 0;
  std::string deckName;
  uint32_t nCards = 0;
  if (!readHeaderDeckNameCount(f, deckName, nCards)) {
    f.close();
    return 0;
  }
  f.close();
  return nCards;
}

bool readCard(uint32_t index, FlashcardCard& out) {
  HalFile f;
  if (!Storage.openFileForRead("FC", kDeckBinPath, f)) return false;
  std::string deckName;
  uint32_t nCards = 0;
  if (!readHeaderDeckNameCount(f, deckName, nCards) || index >= nCards) {
    f.close();
    return false;
  }
  // Header: magic(4) + ver/pad(4) + nameLen(4) + name + nCards(4), then nCards×u32 offsets.
  const uint32_t tableStart = 16 + static_cast<uint32_t>(deckName.size());
  if (!f.seekSet(tableStart + index * 4U)) {
    f.close();
    return false;
  }
  uint32_t cardOff = 0;
  if (!readU32(f, cardOff)) {
    f.close();
    return false;
  }
  if (!f.seekSet(cardOff)) {
    f.close();
    return false;
  }
  if (!readStringField(f, out.front, kMaxFieldBytes) || !readStringField(f, out.back, kMaxFieldBytes) ||
      !readStringField(f, out.notes, kMaxFieldBytes) || !readStringField(f, out.reading, kMaxFieldBytes) ||
      !readStringField(f, out.example, kMaxFieldBytes)) {
    f.close();
    return false;
  }
  f.close();
  return true;
}

bool importDeckFromJsonFile(const char* jsonPath) {
  if (!jsonPath || !Storage.exists(jsonPath)) {
    LOG_ERR("FC", "import: missing file");
    return false;
  }

  HalFile in;
  if (!Storage.openFileForRead("FC", jsonPath, in)) {
    LOG_ERR("FC", "import: open read failed");
    return false;
  }
  const size_t sz = in.fileSize();
  in.close();
  if (sz == 0 || sz > kMaxJsonImportBytes) {
    LOG_ERR("FC", "import: bad size %zu", sz);
    return false;
  }

  auto* buf = static_cast<char*>(malloc(sz + 1));
  if (!buf) {
    LOG_ERR("FC", "import: malloc failed");
    return false;
  }
  const size_t rd = Storage.readFileToBuffer(jsonPath, buf, sz + 1, sz);
  if (rd != sz) {
    LOG_ERR("FC", "import: read %zu expected %zu", rd, sz);
    free(buf);
    return false;
  }
  buf[sz] = 0;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, buf);
  free(buf);
  buf = nullptr;
  if (err) {
    LOG_ERR("FC", "import: JSON %s", err.c_str());
    return false;
  }

  const std::string deckName = doc["name"] | std::string("");
  JsonArrayConst cards = doc["cards"].as<JsonArrayConst>();
  if (deckName.empty() || cards.isNull() || cards.size() == 0) {
    LOG_ERR("FC", "import: invalid deck");
    return false;
  }

  std::vector<FlashcardCard> parsed;
  parsed.reserve(cards.size());
  for (JsonObjectConst c : cards) {
    FlashcardCard row;
    row.front = c["front"] | "";
    row.back = c["back"] | "";
    row.notes = c["notes"] | "";
    row.reading = c["reading"] | "";
    row.example = c["example"] | "";
    if (row.front.empty()) {
      LOG_ERR("FC", "import: empty front");
      return false;
    }
    if (row.front.size() > kMaxFieldBytes || row.back.size() > kMaxFieldBytes || row.notes.size() > kMaxFieldBytes ||
        row.reading.size() > kMaxFieldBytes || row.example.size() > kMaxFieldBytes) {
      LOG_ERR("FC", "import: field too long");
      return false;
    }
    parsed.push_back(std::move(row));
  }

  Storage.mkdir("/.crosspoint");
  if (Storage.exists(kDeckBinPath)) {
    Storage.remove(kDeckBinPath);
  }

  HalFile out;
  if (!Storage.openFileForWrite("FC", kDeckBinPath, out)) {
    LOG_ERR("FC", "import: open write failed");
    return false;
  }

  if (out.write(kMagic, 4) != 4 || out.write(&kVersion, 1) != 1) {
    out.close();
    Storage.remove(kDeckBinPath);
    return false;
  }
  const uint8_t pad[3] = {0, 0, 0};
  if (out.write(pad, 3) != 3) {
    out.close();
    Storage.remove(kDeckBinPath);
    return false;
  }

  const uint32_t nameLen = static_cast<uint32_t>(deckName.size());
  if (!writeU32(out, nameLen) || (nameLen > 0 && out.write(deckName.data(), nameLen) != nameLen)) {
    out.close();
    Storage.remove(kDeckBinPath);
    return false;
  }

  const uint32_t n = static_cast<uint32_t>(parsed.size());
  if (!writeU32(out, n)) {
    out.close();
    Storage.remove(kDeckBinPath);
    return false;
  }

  const uint32_t offsetTablePos = static_cast<uint32_t>(out.position());
  for (uint32_t i = 0; i < n; i++) {
    if (!writeU32(out, 0)) {
      out.close();
      Storage.remove(kDeckBinPath);
      return false;
    }
  }

  std::vector<uint32_t> absOffsets;
  absOffsets.reserve(n);
  for (uint32_t i = 0; i < n; i++) {
    absOffsets.push_back(static_cast<uint32_t>(out.position()));
    const FlashcardCard& row = parsed[i];
    if (!writeStringField(out, row.front) || !writeStringField(out, row.back) || !writeStringField(out, row.notes) ||
        !writeStringField(out, row.reading) || !writeStringField(out, row.example)) {
      out.close();
      Storage.remove(kDeckBinPath);
      return false;
    }
  }

  if (!out.seekSet(offsetTablePos)) {
    out.close();
    Storage.remove(kDeckBinPath);
    return false;
  }
  for (uint32_t i = 0; i < n; i++) {
    if (!writeU32(out, absOffsets[i])) {
      out.close();
      Storage.remove(kDeckBinPath);
      return false;
    }
  }

  out.close();
  LOG_DBG("FC", "import ok: %u cards", static_cast<unsigned>(n));
  return true;
}

}  // namespace flashcard
