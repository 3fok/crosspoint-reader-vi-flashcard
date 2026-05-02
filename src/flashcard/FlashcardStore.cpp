#include "FlashcardStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "FlashcardPaths.h"

namespace {

constexpr char kMagic[4] = {'C', 'P', 'F', 'D'};
constexpr uint8_t kVersion = 3;
constexpr size_t kMaxDeckNameBytes = 512;
constexpr size_t kMaxActiveDeckNameBytes = 128;

std::string gActiveDeckPath = flashcard::kDeckBinPath;

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
  out.resize(len);
  if (len > 0 && f.read(out.data(), len) != static_cast<int>(len)) return false;
  return true;
}

bool writeStringField(HalFile& f, const std::string& s) {
  const size_t n = s.size();
  if (n > flashcard::kMaxFieldBytes) return false;
  if (!writeU16(f, static_cast<uint16_t>(n))) return false;
  return n == 0 || f.write(s.data(), n) == n;
}

bool readHeaderDeckNameCount(HalFile& f, std::string& deckName, uint32_t& nCards) {
  char magic[4];
  if (f.read(magic, 4) != 4 || std::memcmp(magic, kMagic, 4) != 0) return false;
  uint8_t ver = 0;
  if (f.read(&ver, 1) != 1 || ver != kVersion) return false;
  uint8_t pad[3];
  if (f.read(pad, 3) != 3) return false;
  uint32_t nameLen = 0;
  if (!readU32(f, nameLen) || nameLen > kMaxDeckNameBytes) return false;
  deckName.resize(nameLen);
  if (nameLen > 0 && f.read(deckName.data(), nameLen) != static_cast<int>(nameLen)) return false;
  if (!readU32(f, nCards) || nCards == 0 || nCards > 50000) return false;
  return true;
}

class JsonStreamReader {
 public:
  explicit JsonStreamReader(HalFile& file) : file_(file) {}

  bool skipWhitespace() {
    while (true) {
      const int c = peek();
      if (c < 0) return false;
      if (!std::isspace(static_cast<unsigned char>(c))) return true;
      read();
    }
  }

  int peek() {
    if (hasPeek_) return peeked_;
    peeked_ = file_.read();
    hasPeek_ = true;
    return peeked_;
  }

  int read() {
    if (hasPeek_) {
      hasPeek_ = false;
      return peeked_;
    }
    return file_.read();
  }

  bool consume(char expected) {
    if (!skipWhitespace()) return false;
    return read() == expected;
  }

  bool readString(std::string& out, size_t maxLen) {
    if (!skipWhitespace() || read() != '"') return false;
    out.clear();
    while (true) {
      const int c = read();
      if (c < 0) return false;
      if (c == '"') return true;
      if (c == '\\') {
        const int esc = read();
        if (esc < 0) return false;
        if (out.size() >= maxLen) return false;
        switch (esc) {
          case '"':
          case '\\':
          case '/': out.push_back(static_cast<char>(esc)); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            for (int i = 0; i < 4; ++i) {
              if (read() < 0) return false;
            }
            out.push_back('?');
            break;
          }
          default: out.push_back(static_cast<char>(esc)); break;
        }
        continue;
      }
      if (out.size() >= maxLen) return false;
      out.push_back(static_cast<char>(c));
    }
  }

  bool skipValue() {
    if (!skipWhitespace()) return false;
    const int c = peek();
    if (c < 0) return false;
    if (c == '"') {
      std::string tmp;
      return readString(tmp, 4096);
    }
    if (c == '{') return skipBraced('{', '}');
    if (c == '[') return skipBraced('[', ']');
    while (true) {
      const int ch = peek();
      if (ch < 0) return false;
      if (ch == ',' || ch == '}' || ch == ']') return true;
      read();
    }
  }

 private:
  bool skipBraced(char open, char close) {
    if (read() != open) return false;
    int depth = 1;
    while (depth > 0) {
      const int c = read();
      if (c < 0) return false;
      if (c == '"') {
        if (!readString(dummy_, 4096)) return false;
      } else if (c == open) {
        depth++;
      } else if (c == close) {
        depth--;
      } else if (c == '\\') {
        if (read() < 0) return false;
      }
    }
    return true;
  }

  HalFile& file_;
  bool hasPeek_ = false;
  int peeked_ = -1;
  std::string dummy_;
};

bool parseDeckFromJsonStream(HalFile& in, std::string& deckName, std::vector<FlashcardCard>& parsed) {
  JsonStreamReader r(in);
  parsed.clear();
  deckName.clear();

  if (!r.consume('{')) return false;

  bool haveName = false;
  bool haveCards = false;
  while (true) {
    if (!r.skipWhitespace()) return false;
    if (r.peek() == '}') {
      r.read();
      break;
    }

    std::string key;
    if (!r.readString(key, 64)) return false;
    if (!r.consume(':')) return false;

    if (key == "name") {
      if (!r.readString(deckName, kMaxDeckNameBytes)) return false;
      haveName = !deckName.empty();
    } else if (key == "cards") {
      if (!r.consume('[')) return false;
      if (!r.skipWhitespace()) return false;
      while (r.peek() != ']') {
        if (!r.consume('{')) return false;
        FlashcardCard row;
        while (true) {
          std::string cardKey;
          if (!r.readString(cardKey, 64)) return false;
          if (!r.consume(':')) return false;
          std::string value;
          if (!r.readString(value, flashcard::kMaxFieldBytes)) return false;
          if (cardKey == "front") row.front = std::move(value);
          else if (cardKey == "back") row.back = std::move(value);
          else if (cardKey == "notes") row.notes = std::move(value);
          else if (cardKey == "reading") row.reading = std::move(value);
          else if (cardKey == "example") row.example = std::move(value);
          if (!r.skipWhitespace()) return false;
          const int next = r.peek();
          if (next == ',') {
            r.read();
            continue;
          }
          if (next == '}') {
            r.read();
            break;
          }
          return false;
        }
        if (row.front.empty()) return false;
        if (row.front.size() > flashcard::kMaxFieldBytes || row.back.size() > flashcard::kMaxFieldBytes ||
            row.notes.size() > flashcard::kMaxFieldBytes || row.reading.size() > flashcard::kMaxFieldBytes ||
            row.example.size() > flashcard::kMaxFieldBytes) {
          return false;
        }
        parsed.push_back(std::move(row));
        if (!r.skipWhitespace()) return false;
        const int next = r.peek();
        if (next == ',') {
          r.read();
          continue;
        }
        if (next == ']') break;
        return false;
      }
      if (!r.consume(']')) return false;
      haveCards = !parsed.empty();
    } else {
      if (!r.skipValue()) return false;
    }

    if (!r.skipWhitespace()) return false;
    const int next = r.peek();
    if (next == ',') {
      r.read();
      continue;
    }
    if (next == '}') {
      r.read();
      break;
    }
    return false;
  }

  return haveName && haveCards;
}

std::string deckBinPathFromJsonPath(const char* jsonPath) {
  const char* base = std::strrchr(jsonPath, '/');
  base = base ? base + 1 : jsonPath;
  std::string name = base ? base : "deck";
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name.erase(dot);
  if (name.empty()) name = "deck";
  return std::string(flashcard::kFlashcardStorageDir) + "/" + name + ".bin";
}

std::string activeDeckPath() { return gActiveDeckPath; }

}  // namespace

namespace flashcard {

void setActiveDeckFile(const std::string& deckFileName) {
  if (deckFileName.empty() || deckFileName.size() > kMaxActiveDeckNameBytes) return;
  gActiveDeckPath = std::string(flashcard::kFlashcardStorageDir) + "/" + deckFileName;
}

std::string getActiveDeckFile() {
  const size_t pos = gActiveDeckPath.find_last_of('/');
  return (pos == std::string::npos) ? gActiveDeckPath : gActiveDeckPath.substr(pos + 1);
}

void setActiveDeckPath(const std::string& deckPath) {
  if (deckPath.empty() || deckPath.size() > kMaxActiveDeckNameBytes) return;
  gActiveDeckPath = deckPath;
}

const std::string& getActiveDeckPath() { return gActiveDeckPath; }

bool hasDeck() { return Storage.exists(activeDeckPath().c_str()); }

std::string getDeckName() { return getDeckName(activeDeckPath().c_str()); }

uint32_t getCardCount() { return getCardCount(activeDeckPath().c_str()); }

std::string getDeckName(const char* deckPath) {
  if (!deckPath || !Storage.exists(deckPath)) return {};
  HalFile f;
  if (!Storage.openFileForRead("FC", deckPath, f)) return {};
  std::string deckName;
  uint32_t nCards = 0;
  if (!readHeaderDeckNameCount(f, deckName, nCards)) {
    f.close();
    return {};
  }
  f.close();
  return deckName;
}

uint32_t getCardCount(const char* deckPath) {
  if (!deckPath || !Storage.exists(deckPath)) return 0;
  HalFile f;
  if (!Storage.openFileForRead("FC", deckPath, f)) return 0;
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
  if (!Storage.openFileForRead("FC", activeDeckPath().c_str(), f)) return false;
  std::string deckName;
  uint32_t nCards = 0;
  if (!readHeaderDeckNameCount(f, deckName, nCards) || index >= nCards) {
    f.close();
    return false;
  }
  const uint32_t tableStart = 16 + static_cast<uint32_t>(deckName.size());
  if (!f.seekSet(tableStart + index * 4U)) {
    f.close();
    return false;
  }
  uint32_t cardOff = 0;
  if (!readU32(f, cardOff) || !f.seekSet(cardOff)) {
    f.close();
    return false;
  }
  const bool ok = readStringField(f, out.front, kMaxFieldBytes) && readStringField(f, out.back, kMaxFieldBytes) &&
                  readStringField(f, out.notes, kMaxFieldBytes) && readStringField(f, out.reading, kMaxFieldBytes) &&
                  readStringField(f, out.example, kMaxFieldBytes);
  f.close();
  return ok;
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

  std::vector<FlashcardCard> parsed;
  parsed.reserve(128);
  std::string deckName;
  const bool ok = parseDeckFromJsonStream(in, deckName, parsed);
  in.close();
  if (!ok) {
    LOG_ERR("FC", "import: parse failed");
    return false;
  }

  Storage.mkdir(kFlashcardStorageDir);
  const std::string deckBinPath = deckBinPathFromJsonPath(jsonPath);
  if (Storage.exists(deckBinPath.c_str())) {
    Storage.remove(deckBinPath.c_str());
  }

  HalFile out;
  if (!Storage.openFileForWrite("FC", deckBinPath.c_str(), out)) {
    LOG_ERR("FC", "import: open write failed");
    return false;
  }

  if (out.write(kMagic, 4) != 4 || out.write(&kVersion, 1) != 1) {
    out.close();
    Storage.remove(deckBinPath.c_str());
    return false;
  }
  const uint8_t pad[3] = {0, 0, 0};
  if (out.write(pad, 3) != 3) {
    out.close();
    Storage.remove(deckBinPath.c_str());
    return false;
  }

  const uint32_t nameLen = static_cast<uint32_t>(deckName.size());
  if (!writeU32(out, nameLen) || (nameLen > 0 && out.write(deckName.data(), nameLen) != nameLen)) {
    out.close();
    Storage.remove(deckBinPath.c_str());
    return false;
  }

  const uint32_t n = static_cast<uint32_t>(parsed.size());
  if (!writeU32(out, n)) {
    out.close();
    Storage.remove(deckBinPath.c_str());
    return false;
  }

  const uint32_t offsetTablePos = static_cast<uint32_t>(out.position());
  for (uint32_t i = 0; i < n; i++) {
    if (!writeU32(out, 0)) {
      out.close();
      Storage.remove(deckBinPath.c_str());
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
      Storage.remove(deckBinPath.c_str());
      return false;
    }
  }

  if (!out.seekSet(offsetTablePos)) {
    out.close();
    Storage.remove(deckBinPath.c_str());
    return false;
  }
  for (uint32_t i = 0; i < n; i++) {
    if (!writeU32(out, absOffsets[i])) {
      out.close();
      Storage.remove(deckBinPath.c_str());
      return false;
    }
  }

  out.close();
  setActiveDeckPath(deckBinPath);
  LOG_DBG("FC", "import ok: %u cards", static_cast<unsigned>(n));
  return true;
}

}  // namespace flashcard
