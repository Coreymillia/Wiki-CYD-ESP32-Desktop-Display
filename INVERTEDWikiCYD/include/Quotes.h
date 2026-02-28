#pragma once

#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Quote / joke data
// ---------------------------------------------------------------------------
struct QuoteData {
  char text[400];
  char author[64];
  bool isJoke;
};

static QuoteData wkQuote;
static bool      wkHasQuote      = false;
static int       wkQuoteHttpCode = 0;

// ---------------------------------------------------------------------------
// Fetch a random dad joke from icanhazdadjoke.com
// Endpoint: GET / with Accept: application/json
// Response: { "id": "...", "joke": "...", "status": 200 }
// ---------------------------------------------------------------------------
static int jokeFetch() {
  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("icanhazdadjoke.com", 443, 15000)) {
    wkQuoteHttpCode = -1;
    return -1;
  }
  client.setTimeout(15000);

  client.print("GET / HTTP/1.0\r\n"
               "Host: icanhazdadjoke.com\r\n"
               "Accept: application/json\r\n"
               "User-Agent: WikiCYD/1.0 ESP32\r\n"
               "Connection: close\r\n"
               "\r\n");

  String sl = client.readStringUntil('\n');
  sl.trim();
  int code = 0;
  if (sl.startsWith("HTTP/")) {
    int sp = sl.indexOf(' ');
    if (sp > 0) code = sl.substring(sp + 1, sp + 4).toInt();
  }
  wkQuoteHttpCode = code;
  if (code != 200) { client.stop(); return -1; }

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
  }

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, client);
  client.stop();
  if (err) return -2;

  memset(&wkQuote, 0, sizeof(wkQuote));
  strncpy(wkQuote.text,   doc["joke"] | "", sizeof(wkQuote.text)   - 1);
  strncpy(wkQuote.author, "icanhazdadjoke.com", sizeof(wkQuote.author) - 1);
  wkQuote.isJoke = true;
  wkHasQuote     = true;
  Serial.printf("[Joke] fetched: %.60s\n", wkQuote.text);
  return 1;
}

// ---------------------------------------------------------------------------
// Fetch a random quote from zenquotes.io
// Endpoint: GET /api/random
// Response: [{ "q": "...", "a": "author", "h": "..." }]
// ---------------------------------------------------------------------------
static int quoteFetch() {
  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("zenquotes.io", 443, 15000)) {
    wkQuoteHttpCode = -1;
    return -1;
  }
  client.setTimeout(15000);

  client.print("GET /api/random HTTP/1.0\r\n"
               "Host: zenquotes.io\r\n"
               "User-Agent: WikiCYD/1.0 ESP32\r\n"
               "Connection: close\r\n"
               "\r\n");

  String sl = client.readStringUntil('\n');
  sl.trim();
  int code = 0;
  if (sl.startsWith("HTTP/")) {
    int sp = sl.indexOf(' ');
    if (sp > 0) code = sl.substring(sp + 1, sp + 4).toInt();
  }
  wkQuoteHttpCode = code;
  if (code != 200) { client.stop(); return -1; }

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, client);
  client.stop();
  if (err) return -2;

  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) return -3;
  JsonObject obj = arr[0];

  memset(&wkQuote, 0, sizeof(wkQuote));
  strncpy(wkQuote.text,   obj["q"] | "", sizeof(wkQuote.text)   - 1);
  strncpy(wkQuote.author, obj["a"] | "", sizeof(wkQuote.author) - 1);
  wkQuote.isJoke = false;
  wkHasQuote     = true;
  Serial.printf("[Quote] %s — %s\n", wkQuote.text, wkQuote.author);
  return 1;
}
