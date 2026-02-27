#pragma once

#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Article data
// ---------------------------------------------------------------------------
struct Article {
  char title[128];
  char description[128];  // short tagline (may be empty)
  char extract[2048];      // plain-text article summary
  char url[160];           // desktop article URL for QR code
};

static Article wkArticle;
static bool    wkHasArticle  = false;
static int     wkLastHttpCode = 0;
static char    wkLastErr[32]  = "";

// ---------------------------------------------------------------------------
// Fetch a random Wikipedia article summary.
// Wikipedia's /page/random/summary always returns HTTP 303 → Location path.
// We make two explicit connections: one to get the redirect path, one to
// fetch the actual article JSON. No recursion, no string edge cases.
// Returns 1 on success, negative on error.
// ---------------------------------------------------------------------------
static int wkFetch() {
  const char* host = "en.wikipedia.org";

  // ── Step 1: follow the 303 redirect to get the real article path ──────────
  WiFiClientSecure c1;
  c1.setInsecure();
  if (!c1.connect(host, 443, 15000)) { wkLastHttpCode = -1; return -1; }
  c1.setTimeout(15000);

  c1.print("GET /api/rest_v1/page/random/summary HTTP/1.0\r\n"
           "Host: en.wikipedia.org\r\n"
           "User-Agent: WikiCYD/1.0 ESP32\r\n"
           "Connection: close\r\n"
           "\r\n");

  // Read status code
  String sl = c1.readStringUntil('\n');
  sl.trim();
  int code = 0;
  if (sl.startsWith("HTTP/")) {
    int sp = sl.indexOf(' ');
    if (sp > 0) code = sl.substring(sp + 1, sp + 4).toInt();
  }
  wkLastHttpCode = code;

  // Read headers, grab Location regardless of code
  String articlePath = "";
  while (c1.connected() || c1.available()) {
    String line = c1.readStringUntil('\n');
    line.replace("\r", "");
    line.trim();
    if (line.length() == 0) break;
    // Case-insensitive Location match
    String lower = line; lower.toLowerCase();
    if (lower.startsWith("location:")) {
      articlePath = line.substring(9);
      articlePath.replace("\r", "");
      articlePath.trim();
    }
  }
  c1.stop();

  // Strip scheme+host if present, leaving a bare path
  if      (articlePath.startsWith("https://en.wikipedia.org")) articlePath = articlePath.substring(24);
  else if (articlePath.startsWith("http://en.wikipedia.org"))  articlePath = articlePath.substring(23);

  if (articlePath.length() == 0) {
    Serial.printf("[Wiki] step1 code=%d, no Location header\n", code);
    return -1;
  }
  Serial.printf("[Wiki] redirect → %s\n", articlePath.c_str());

  // ── Step 2: fetch the actual article from the redirect path ───────────────
  delay(100);  // brief pause before second TLS handshake
  WiFiClientSecure c2;
  c2.setInsecure();
  if (!c2.connect(host, 443, 15000)) { wkLastHttpCode = -1; return -1; }
  c2.setTimeout(15000);

  c2.print("GET ");
  c2.print(articlePath);
  c2.print(" HTTP/1.0\r\n"
           "Host: en.wikipedia.org\r\n"
           "User-Agent: WikiCYD/1.0 ESP32\r\n"
           "Accept: application/json\r\n"
           "Connection: close\r\n"
           "\r\n");

  sl = c2.readStringUntil('\n');
  sl.trim();
  code = 0;
  if (sl.startsWith("HTTP/")) {
    int sp = sl.indexOf(' ');
    if (sp > 0) code = sl.substring(sp + 1, sp + 4).toInt();
  }
  wkLastHttpCode = code;
  if (code != 200) { c2.stop(); return -1; }

  // Discard headers
  while (c2.connected() || c2.available()) {
    String line = c2.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
  }

  // Parse JSON (only the fields we need)
  DynamicJsonDocument filter(256);
  filter["title"]                           = true;
  filter["description"]                     = true;
  filter["extract"]                         = true;
  filter["content_urls"]["desktop"]["page"] = true;

  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, c2,
                               DeserializationOption::Filter(filter),
                               DeserializationOption::NestingLimit(10));
  c2.stop();

  if (err) {
    strncpy(wkLastErr, err.c_str(), sizeof(wkLastErr) - 1);
    return -2;
  }
  wkLastErr[0] = '\0';

  memset(&wkArticle, 0, sizeof(wkArticle));
  strncpy(wkArticle.title,       doc["title"]       | "", sizeof(wkArticle.title)       - 1);
  strncpy(wkArticle.description, doc["description"] | "", sizeof(wkArticle.description) - 1);
  strncpy(wkArticle.extract,     doc["extract"]     | "", sizeof(wkArticle.extract)     - 1);
  strncpy(wkArticle.url,
          doc["content_urls"]["desktop"]["page"] | "", sizeof(wkArticle.url) - 1);

  wkHasArticle = true;
  Serial.printf("[Wiki] fetched: %s\n", wkArticle.title);
  return 1;
}
