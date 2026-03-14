// WikiCYD — Random Wikipedia articles on the CYD (Cheap Yellow Display)
//
// MODE_ARTICLE: Current article — title, description, scrollable extract
//               Body: tap top half = scroll up, bottom half = scroll down
// MODE_QR:      QR code linking to full Wikipedia article
//               Tap anywhere to go back
//
// Footer (5 zones, 64px each):
//   CONTENT(0-63): WIKI→JOKE→QUOT→ALL  |  QR(64-127)  |  NEW(128-191)
//   AUTO(192-255): toggle auto-fetch    |  SCROLL(256-319): cycle scroll speed
// BOOT button: short press = new content, long press (2s+) = re-enter setup

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include <Arduino_GFX_Library.h>
#include <XPT2046_Touchscreen.h>
#include <qrcode.h>
#include "Portal.h"
#include "Wikipedia.h"
#include "Quotes.h"

// ---------------------------------------------------------------------------
// Device identity — reported via GET /identify on port 80
// ---------------------------------------------------------------------------
#define DEVICE_NAME      "INVERTEDWikiCYD"
#define FIRMWARE_VERSION "1.0.0"
#include "CYDIdentity.h"

// Forward declarations
void fetchArticle();
void fetchContent();
void renderArticle();
void renderQuote();
void renderCurrent();
void renderQR();
void renderFooter();

// ---------------------------------------------------------------------------
// Display — CYD ILI9341 320x240 landscape
// ---------------------------------------------------------------------------
#define GFX_BL 21
Arduino_DataBus *bus = new Arduino_HWSPI(2/*DC*/, 15/*CS*/, 14/*SCK*/, 13/*MOSI*/, 12/*MISO*/);
Arduino_GFX    *gfx = new Arduino_ILI9341(bus, GFX_NOT_DEFINED, 1/*landscape*/);

// ---------------------------------------------------------------------------
// Touch — XPT2046 on VSPI
// ---------------------------------------------------------------------------
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33
#define TOUCH_DEBOUNCE 300

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
static unsigned long lastTouchTime = 0;

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
#define HEADER_H 20
#define FOOTER_H 28

// Wikipedia blue (#3870F8 approx) and dim gray
#define WIKI_BLUE 0x3B9F
#define DIM_GRAY  0x7BEF

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------
#define MODE_ARTICLE 0
#define MODE_QR      1

// ---------------------------------------------------------------------------
// Content types
// ---------------------------------------------------------------------------
#define CONTENT_WIKI  0
#define CONTENT_JOKE  1
#define CONTENT_QUOTE 2
#define CONTENT_ALL   3   // cycles WIKI→JOKE→QUOTE one at a time

// ---------------------------------------------------------------------------
// Timers & BOOT button
// ---------------------------------------------------------------------------
#define BOOT_PIN     0
#define BOOT_LONG_MS 2000UL

// Refresh intervals matching AP setting index
static const unsigned long REFRESH_INTERVALS[] = {
  15UL  * 1000UL,           // 0 = 15 seconds
  30UL  * 1000UL,           // 1 = 30 seconds
  60UL  * 1000UL,           // 2 = 1 minute
  3UL   * 60UL * 1000UL,   // 3 = 3 minutes
  5UL   * 60UL * 1000UL,   // 4 = 5 minutes
  30UL  * 60UL * 1000UL,   // 5 = 30 minutes
  60UL  * 60UL * 1000UL,   // 6 = 1 hour
};

// Auto-scroll intervals matching AP setting index (0 = off)
static const unsigned long SCROLL_INTERVALS[] = {
  0,                        // 0 = off
  30UL  * 1000UL,           // 1 = 30 seconds
  60UL  * 1000UL,           // 2 = 1 minute
  90UL  * 1000UL,           // 3 = 90 seconds
  2UL   * 60UL * 1000UL,   // 4 = 2 minutes
};

// ---------------------------------------------------------------------------
// Color palette for multicolor mode
// ---------------------------------------------------------------------------
static const uint16_t MULTI_PALETTE[6] = {
  WIKI_BLUE, 0x07FF, 0xFFE0, 0x07E0, 0xF81F, 0xFFFF,
};

static uint16_t getThemeColor(int idx) {
  if (strcmp(wk_font_color, "orange") == 0) return 0xFB20;
  if (strcmp(wk_font_color, "green")  == 0) return 0x07E0;
  if (strcmp(wk_font_color, "cyan")   == 0) return 0x07FF;
  if (strcmp(wk_font_color, "white")  == 0) return 0xFFFF;
  if (strcmp(wk_font_color, "multi")  == 0) return MULTI_PALETTE[((unsigned)idx) % 6];
  return WIKI_BLUE;  // default: blue
}

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------
static int           sc_mode      = MODE_ARTICLE;
static int           wk_content   = CONTENT_WIKI;  // WIKI / JOKE / QUOTE / ALL
static int           wk_all_sub   = 0;      // sub-content in ALL mode (0=WIKI,1=JOKE,2=QUOTE)
static int           wk_artLine   = 0;      // current scroll position in extract
static bool          wk_autoFetch = false;  // auto-fetch new content on interval
static unsigned long wk_autoLast  = 0;      // millis() of last auto-fetch trigger
static unsigned long wk_scrollLast = 0;     // millis() of last auto-scroll step

// ---------------------------------------------------------------------------
// Article line-wrap cache
// ---------------------------------------------------------------------------
#define MAX_ARTICLE_LINES 120
#define ARTICLE_CHARS      52

static char artLines[MAX_ARTICLE_LINES][ARTICLE_CHARS + 1];
static int  artLineCount = 0;

static void buildArticleLines(const char* text, int charsPerLine) {
  artLineCount = 0;
  String s(text);
  while (s.length() > 0 && artLineCount < MAX_ARTICLE_LINES) {
    if ((int)s.length() <= charsPerLine) {
      strncpy(artLines[artLineCount++], s.c_str(), ARTICLE_CHARS);
      break;
    }
    int cut = charsPerLine;
    while (cut > 0 && s.charAt(cut) != ' ') cut--;
    if (cut == 0) cut = charsPerLine;
    strncpy(artLines[artLineCount++], s.substring(0, cut).c_str(), ARTICLE_CHARS);
    s = s.substring(cut);
    s.trim();
  }
}

// ---------------------------------------------------------------------------
// Status banner
// ---------------------------------------------------------------------------
void showStatus(const char* msg) {
  gfx->fillRect(0, 0, gfx->width(), HEADER_H, RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(4, 6);
  gfx->print(msg);
  Serial.println(msg);
}

// ---------------------------------------------------------------------------
// Word-wrap helper — returns y after last line printed
// ---------------------------------------------------------------------------
static int drawWrapped(const char* text, int x, int y,
                       int maxChars, int lineH,
                       uint16_t color, uint8_t textSz) {
  gfx->setTextColor(color);
  gfx->setTextSize(textSz);
  String s(text);
  while (s.length() > 0) {
    if ((int)s.length() <= maxChars) {
      gfx->setCursor(x, y);
      gfx->print(s);
      y += lineH;
      break;
    }
    int cut = maxChars;
    while (cut > 0 && s.charAt(cut) != ' ') cut--;
    if (cut == 0) cut = maxChars;
    gfx->setCursor(x, y);
    gfx->print(s.substring(0, cut));
    y += lineH;
    s = s.substring(cut);
    s.trim();
    if (y > gfx->height() - lineH) break;
  }
  return y;
}

// ---------------------------------------------------------------------------
// Fetch content for the active content type
// ---------------------------------------------------------------------------
void fetchContent() {
  wk_artLine = 0;
  // In ALL mode, advance to the next sub-content type then fetch it
  if (wk_content == CONTENT_ALL) {
    wk_all_sub = (wk_all_sub + 1) % 3;
    if (wk_all_sub == 0) { fetchArticle(); return; }
    wkHasQuote = false;
    showStatus(wk_all_sub == 1 ? "Fetching joke..." : "Fetching quote...");
    int r = (wk_all_sub == 1) ? jokeFetch() : quoteFetch();
    wk_autoLast = millis();
    if (r < 0) {
      char msg[48];
      snprintf(msg, sizeof(msg), "Fetch failed (HTTP %d) — retrying...", wkQuoteHttpCode);
      showStatus(msg);
      delay(2000);
    }
    return;
  }
  if (wk_content == CONTENT_WIKI) {
    fetchArticle();
    return;
  }
  wkHasQuote = false;
  showStatus(wk_content == CONTENT_JOKE ? "Fetching joke..." : "Fetching quote...");
  int r = (wk_content == CONTENT_JOKE) ? jokeFetch() : quoteFetch();
  wk_autoLast = millis();
  if (r < 0) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Fetch failed (HTTP %d) — retrying...", wkQuoteHttpCode);
    showStatus(msg);
    delay(2000);
  }
}

// ---------------------------------------------------------------------------
// Fetch a random Wikipedia article
// ---------------------------------------------------------------------------
void fetchArticle() {
  showStatus("Fetching Wikipedia article...");
  wk_artLine   = 0;
  int r = wkFetch();
  wk_autoLast  = millis();

  if (r < 0) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Fetch failed (HTTP %d) — retrying...", wkLastHttpCode);
    showStatus(msg);
    identity_error_flags |= 0x01;
    delay(2000);
  } else {
    char msg[48];
    snprintf(msg, sizeof(msg), "%.40s", wkArticle.title);
    showStatus(msg);
    identity_last_fetch   = millis() / 1000UL;
    identity_error_flags &= ~0x01;
    delay(300);
  }
}

// ---------------------------------------------------------------------------
// Render helper — picks article or quote view based on active content type
// ---------------------------------------------------------------------------
void renderCurrent() {
  int eff = (wk_content == CONTENT_ALL) ? wk_all_sub : wk_content;
  if (eff == CONTENT_WIKI) renderArticle();
  else renderQuote();
}

// ---------------------------------------------------------------------------
// Footer renderer — 5 zones (64px each):
//   CONTENT(0-63) | QR(64-127) | NEW(128-191) | AUTO(192-255) | SCROLL(256-319)
// ---------------------------------------------------------------------------
void renderFooter() {
  gfx->fillRect(0, gfx->height() - FOOTER_H, gfx->width(), FOOTER_H, 0x0841);
  gfx->setTextSize(1);
  const int y = gfx->height() - 18;

  // Zone 0 (0-63): content type — tap to cycle WIKI→JOKE→QUOT→ALL
  static const char*    ctLabels[] = { "WIKI", "JOKE", "QUOT", "ALL" };
  static const uint16_t ctColors[] = { WIKI_BLUE, 0xFFE0, 0xF81F, 0x07E0 };
  gfx->setTextColor(ctColors[wk_content]);
  gfx->setCursor(4, y);
  gfx->print(ctLabels[wk_content]);

  // Zone 1 (64-127): QR — active only in WIKI / ALL-showing-WIKI
  bool wikiActive = (wk_content == CONTENT_WIKI) ||
                    (wk_content == CONTENT_ALL && wk_all_sub == 0);
  gfx->setTextColor(wikiActive ? DIM_GRAY : 0x2104);
  gfx->setCursor(88, y);
  gfx->print("QR");

  // Zone 2 (128-191): NEW
  gfx->setTextColor(WIKI_BLUE);
  gfx->setCursor(151, y);
  gfx->print("NEW");

  // Zone 3 (192-255): AUTO / PAUSE
  if (wk_autoFetch) {
    gfx->setTextColor(0x07FF);
    gfx->setCursor(207, y);
    gfx->print("PAUSE");
  } else {
    gfx->setTextColor(DIM_GRAY);
    gfx->setCursor(212, y);
    gfx->print("AUTO");
  }

  // Zone 4 (256-319): auto-scroll speed — tap to cycle
  static const char* scrLabels[] = { "SCR", "30S", " 1M", "90S", " 2M" };
  uint8_t si = constrain(wk_scroll_interval, 0, 4);
  gfx->setTextColor(si == 0 ? 0x2104 : 0x07FF);
  gfx->setCursor(273, y);
  gfx->print(scrLabels[si]);
}

// ---------------------------------------------------------------------------
// MODE_ARTICLE renderer
// ---------------------------------------------------------------------------
void renderArticle() {
  gfx->fillScreen(RGB565_BLACK);

  // Header bar
  gfx->fillRect(0, 0, gfx->width(), HEADER_H, 0x1082);
  gfx->setTextSize(1);
  gfx->setTextColor(WIKI_BLUE);
  gfx->setCursor(4, 6);
  gfx->print("Wiki");

  if (!wkHasArticle) {
    gfx->setTextColor(DIM_GRAY);
    gfx->setTextSize(1);
    gfx->setCursor(80, 116);
    gfx->print("Loading...");
    renderFooter();
    return;
  }

  // Abbreviated title on right of header
  char abbr[28];
  strncpy(abbr, wkArticle.title, 27); abbr[27] = '\0';
  int tw = strlen(abbr) * 6;
  if (tw < 230) {
    gfx->setTextColor(DIM_GRAY);
    gfx->setCursor(gfx->width() - tw - 4, 6);
    gfx->print(abbr);
  }

  // Title — textSize 2 (12×16px), 26 chars/line, up to 3 lines
  int y = HEADER_H + 4;
  int titleY = y;
  y = drawWrapped(wkArticle.title, 4, y, 26, 18, RGB565_WHITE, 2);
  // Cap at 3 lines to protect layout
  if (y > titleY + 3 * 18) y = titleY + 3 * 18;
  y += 2;

  // Description (if present) — single line, gray
  if (wkArticle.description[0] != '\0') {
    char desc[50];
    strncpy(desc, wkArticle.description, 49); desc[49] = '\0';
    gfx->setTextSize(1);
    gfx->setTextColor(DIM_GRAY);
    gfx->setCursor(4, y);
    gfx->print(desc);
    y += 11;
  }

  // Divider
  gfx->drawFastHLine(0, y, gfx->width(), WIKI_BLUE);
  y += 5;

  // Build extract lines with dynamic font size
  uint8_t textSz   = wk_font_size;
  int charsPerLine = (gfx->width() - 8) / (6 * textSz);
  int lineH        = 9 * textSz;
  buildArticleLines(wkArticle.extract, charsPerLine);

  int bodyBottom = gfx->height() - FOOTER_H - 2;
  int visLines   = (bodyBottom - y) / lineH;
  int maxScroll  = artLineCount - visLines;
  if (maxScroll < 0) maxScroll = 0;
  if (wk_artLine > maxScroll) wk_artLine = maxScroll;

  gfx->setTextSize(textSz);
  for (int i = 0; i < visLines; i++) {
    int li = wk_artLine + i;
    if (li >= artLineCount) break;
    gfx->setTextColor(getThemeColor(li));
    gfx->setCursor(4, y + i * lineH);
    gfx->print(artLines[li]);
  }

  // Scroll indicator on right edge
  if (artLineCount > visLines) {
    int barH   = bodyBottom - y;
    int thumbH = max(8, barH * visLines / artLineCount);
    int thumbY = y + (barH - thumbH) * wk_artLine / max(1, maxScroll);
    gfx->drawFastVLine(gfx->width() - 3, y, barH, 0x2104);
    gfx->fillRect(gfx->width() - 4, thumbY, 4, thumbH, DIM_GRAY);
  }

  renderFooter();
}

// ---------------------------------------------------------------------------
// JOKE / QUOTE renderer — same scrollable layout as renderArticle()
// ---------------------------------------------------------------------------
void renderQuote() {
  gfx->fillScreen(RGB565_BLACK);

  // Header
  uint16_t hColor = (wk_content == CONTENT_JOKE) ? 0xFFE0 : 0xF81F;
  gfx->fillRect(0, 0, gfx->width(), HEADER_H, 0x1082);
  gfx->setTextSize(1);
  gfx->setTextColor(hColor);
  gfx->setCursor(4, 6);
  gfx->print(wk_content == CONTENT_JOKE ? "Dad Joke" : "Quote");

  if (!wkHasQuote) {
    gfx->setTextColor(DIM_GRAY);
    gfx->setCursor(80, 116);
    gfx->print("Loading...");
    renderFooter();
    return;
  }

  int y = HEADER_H + 4;

  // Author line (quotes only)
  if (!wkQuote.isJoke && wkQuote.author[0] != '\0') {
    char authLine[52];
    snprintf(authLine, sizeof(authLine), "— %.49s", wkQuote.author);
    gfx->setTextSize(1);
    gfx->setTextColor(DIM_GRAY);
    gfx->setCursor(4, y);
    gfx->print(authLine);
    y += 11;
  }

  // Divider
  gfx->drawFastHLine(0, y, gfx->width(), hColor);
  y += 5;

  // Reuse article line-wrap cache
  uint8_t textSz   = wk_font_size;
  int charsPerLine = (gfx->width() - 8) / (6 * textSz);
  int lineH        = 9 * textSz;
  buildArticleLines(wkQuote.text, charsPerLine);

  int bodyBottom = gfx->height() - FOOTER_H - 2;
  int visLines   = (bodyBottom - y) / lineH;
  int maxScroll  = artLineCount - visLines;
  if (maxScroll < 0) maxScroll = 0;
  if (wk_artLine > maxScroll) wk_artLine = maxScroll;

  gfx->setTextSize(textSz);
  for (int i = 0; i < visLines; i++) {
    int li = wk_artLine + i;
    if (li >= artLineCount) break;
    gfx->setTextColor(getThemeColor(li));
    gfx->setCursor(4, y + i * lineH);
    gfx->print(artLines[li]);
  }

  // Scroll indicator
  if (artLineCount > visLines) {
    int barH   = bodyBottom - y;
    int thumbH = max(8, barH * visLines / artLineCount);
    int thumbY = y + (barH - thumbH) * wk_artLine / max(1, maxScroll);
    gfx->drawFastVLine(gfx->width() - 3, y, barH, 0x2104);
    gfx->fillRect(gfx->width() - 4, thumbY, 4, thumbH, DIM_GRAY);
  }

  renderFooter();
}

// ---------------------------------------------------------------------------
// MODE_QR renderer — QR code for the Wikipedia article URL
// ---------------------------------------------------------------------------
void renderQR() {
  if (!wkHasArticle) { sc_mode = MODE_ARTICLE; renderArticle(); return; }

  QRCode qrcode;
  // Version 5 (37×37 modules) handles up to 108 bytes at ECC_LOW
  // — enough for any Wikipedia URL
  char url[109];
  strncpy(url, wkArticle.url, 108); url[108] = '\0';
  uint8_t qrcodeData[qrcode_getBufferSize(5)];
  qrcode_initText(&qrcode, qrcodeData, 5, ECC_LOW, url);

  int availH  = gfx->height() - HEADER_H - FOOTER_H - 14;
  int modSize = availH / qrcode.size;
  if (modSize < 3) modSize = 3;
  if (modSize > 6) modSize = 6;

  int qrPx    = qrcode.size * modSize;
  int offsetX = (gfx->width()  - qrPx) / 2;
  int offsetY = HEADER_H + 4;

  gfx->fillRect(offsetX - 4, offsetY - 4, qrPx + 8, qrPx + 8, RGB565_WHITE);
  for (uint8_t row = 0; row < qrcode.size; row++) {
    for (uint8_t col = 0; col < qrcode.size; col++) {
      uint16_t c = qrcode_getModule(&qrcode, col, row) ? RGB565_BLACK : RGB565_WHITE;
      gfx->fillRect(offsetX + col * modSize, offsetY + row * modSize, modSize, modSize, c);
    }
  }

  // Header (drawn after QR to avoid being covered)
  gfx->fillRect(0, 0, gfx->width(), HEADER_H, 0x1082);
  gfx->setTextSize(1);
  gfx->setTextColor(WIKI_BLUE);
  gfx->setCursor(4, 6);
  gfx->print("Wiki  Scan to open article");

  // Article title hint below QR
  int textY = offsetY + qrPx + 6;
  gfx->setTextSize(1);
  gfx->setTextColor(DIM_GRAY);
  gfx->setCursor(4, textY);
  char abbr[46];
  strncpy(abbr, wkArticle.title, 45); abbr[45] = '\0';
  gfx->print(abbr);

  // Footer
  gfx->fillRect(0, gfx->height() - FOOTER_H, gfx->width(), FOOTER_H, 0x0841);
  gfx->setTextSize(1);
  gfx->setTextColor(DIM_GRAY);
  gfx->setCursor(gfx->width() / 2 - 42, gfx->height() - 18);
  gfx->print("tap anywhere to go back");
}

// ---------------------------------------------------------------------------
// Touch handler
// ---------------------------------------------------------------------------
void handleTouch(int tx, int ty) {
  const int footerY = gfx->height() - FOOTER_H;

  // QR mode: any tap goes back
  if (sc_mode == MODE_QR) {
    sc_mode = MODE_ARTICLE;
    renderArticle();
    return;
  }

  if (ty >= footerY) {
    // Footer 5 zones (64px each): CONTENT(0-63)|QR(64-127)|NEW(128-191)|AUTO(192-255)|SCROLL(256-319)
    if (tx < 64) {
      // Cycle content type: WIKI → JOKE → QUOTE → ALL → WIKI
      wk_content = (wk_content + 1) % 4;
      wk_artLine = 0;
      sc_mode    = MODE_ARTICLE;
      if (wk_content == CONTENT_ALL) {
        // Start ALL mode from WIKI, reuse cached article if available
        wk_all_sub = 0;
        if (!wkHasArticle) fetchArticle();
        renderArticle();
      } else if (wk_content == CONTENT_WIKI) {
        if (!wkHasArticle) fetchArticle();
        renderArticle();
      } else {
        fetchContent();
        renderQuote();
      }
    } else if (tx < 128) {
      // QR — only active when WIKI content is visible
      bool wikiActive = (wk_content == CONTENT_WIKI) ||
                        (wk_content == CONTENT_ALL && wk_all_sub == 0);
      if (wikiActive) { sc_mode = MODE_QR; renderQR(); }
    } else if (tx < 192) {
      // NEW — fetch next content for current type
      wk_artLine = 0;
      fetchContent();
      renderCurrent();
    } else if (tx < 256) {
      // Toggle auto-fetch
      wk_autoFetch = !wk_autoFetch;
      wk_autoLast  = millis();
      renderFooter();
    } else {
      // Cycle auto-scroll speed: off→30s→1m→3m→5m→30m→1hr→off
      wk_scroll_interval = (wk_scroll_interval + 1) % 5;
      wk_scrollLast = millis();
      renderFooter();
    }
  } else if (ty >= HEADER_H) {
    // Body: top half = scroll up, bottom half = scroll down
    int bodyMid = HEADER_H + (footerY - HEADER_H) / 2;
    if (ty < bodyMid) {
      if (wk_artLine > 0) wk_artLine--;
    } else {
      wk_artLine++;
    }
    renderCurrent();
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(GFX_BL, OUTPUT);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(GFX_BL, 0);
  ledcWrite(0, 255);

  gfx->begin();
  gfx->invertDisplay(true); // CYDs with inverted display hardware
  gfx->fillScreen(RGB565_BLACK);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  pinMode(BOOT_PIN, INPUT_PULLUP);

  wkLoadSettings();
  ledcWrite(0, wk_brightness);
  bool showPortal = !wk_has_settings;
  if (!showPortal) {
    showStatus("Hold BOOT to change settings...");
    for (int i = 0; i < 30 && !showPortal; i++) {
      if (digitalRead(BOOT_PIN) == LOW) showPortal = true;
      delay(100);
    }
  }
  if (showPortal) {
    wkInitPortal();
    while (!portalDone) wkRunPortal();
    wkClosePortal();
    ledcWrite(0, wk_brightness);
  }

  showStatus("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(wk_wifi_ssid, wk_wifi_pass);
  uint32_t wt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wt < 15000) delay(300);
  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi failed - re-running setup");
    delay(2000);
    wkInitPortal();
    while (!portalDone) wkRunPortal();
    wkClosePortal();
    ESP.restart();
  }
  showStatus("WiFi connected!");
  delay(400);

  identityBegin();

  configTime(0, 0, "pool.ntp.org");
  uint32_t ntpStart = millis();
  while (time(nullptr) < 946684800UL && millis() - ntpStart < 5000) delay(100);

  fetchArticle();
  renderArticle();
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
  identityHandle();  // service any pending /identify requests before blocking ops

  // Auto-fetch new content on interval
  int effContent = (wk_content == CONTENT_ALL) ? wk_all_sub : wk_content;
  bool hasContent = (effContent == CONTENT_WIKI) ? wkHasArticle : wkHasQuote;
  if (wk_autoFetch && hasContent) {
    unsigned long interval = REFRESH_INTERVALS[constrain(wk_refresh_interval, 0, 6)];
    if (millis() - wk_autoLast >= interval) {
      sc_mode = MODE_ARTICLE;
      fetchContent();
      renderCurrent();
    }
  }

  // Auto-scroll: advance one line on interval, wrap to top at end
  if (wk_scroll_interval > 0 && sc_mode == MODE_ARTICLE) {
    unsigned long scrollInterval = SCROLL_INTERVALS[constrain(wk_scroll_interval, 1, 4)];
    if (millis() - wk_scrollLast >= scrollInterval) {
      wk_scrollLast = millis();
      wk_artLine++;
      if (wk_artLine >= artLineCount) wk_artLine = 0;
      renderCurrent();
    }
  }

  // BOOT button: short press = new content, long press = re-enter setup
  if (digitalRead(BOOT_PIN) == LOW) {
    delay(50);
    if (digitalRead(BOOT_PIN) == LOW) {
      unsigned long pressStart = millis();
      while (digitalRead(BOOT_PIN) == LOW) delay(10);
      unsigned long held = millis() - pressStart;

      if (held >= BOOT_LONG_MS) {
        showStatus("Entering setup...");
        delay(500);
        portalDone = false;
        wkInitPortal();
        while (!portalDone) wkRunPortal();
        wkClosePortal();
        ESP.restart();
      } else {
        // Short press — fetch new content for current type
        sc_mode    = MODE_ARTICLE;
        wk_artLine = 0;
        fetchContent();
        renderCurrent();
      }
    }
  }

  // Touch
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
    unsigned long now = millis();
    if (now - lastTouchTime > TOUCH_DEBOUNCE) {
      lastTouchTime = now;
      int tx = map(p.x, 200, 3900, 0, gfx->width());
      int ty = map(p.y, 240, 3900, 0, gfx->height());
      tx = constrain(tx, 0, gfx->width()  - 1);
      ty = constrain(ty, 0, gfx->height() - 1);
      handleTouch(tx, ty);
    }
  }

  delay(10);
  identityHandle();
}
