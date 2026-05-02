#include "FlashcardPrefs.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include "FlashcardPaths.h"

namespace flashcard {

bool loadShowControls(bool& outShowControls) {
  outShowControls = true;
  if (!Storage.exists(kSettingsJsonPath)) {
    return true;
  }
  const String json = Storage.readFile(kSettingsJsonPath);
  if (json.isEmpty()) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    LOG_ERR("FC", "flashcard_settings.json parse error");
    return false;
  }
  outShowControls = doc["showControls"] | true;
  return true;
}

bool saveShowControls(bool showControls) {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  doc["showControls"] = showControls;
  String out;
  serializeJson(doc, out);
  return Storage.writeFile(kSettingsJsonPath, out);
}

}  // namespace flashcard
