#pragma once

#include <Arduino_GFX_Library.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

extern Arduino_GFX *gfx;

// ---------------------------------------------------------------------------
// Persisted settings
// ---------------------------------------------------------------------------
static char    wk_wifi_ssid[64]      = "";
static char    wk_wifi_pass[64]      = "";
static char    wk_font_color[16]     = "blue";  // blue|orange|green|cyan|white|multi
static uint8_t wk_font_size          = 1;        // 1=small 2=medium 3=large
static uint8_t wk_refresh_interval   = 4;        // 0=15s 1=30s 2=1m 3=3m 4=5m 5=30m 6=1hr
static uint8_t wk_scroll_interval    = 0;        // 0=off 1=30s 2=1m 3=90s 4=2m
static bool    wk_has_settings       = false;

// ---------------------------------------------------------------------------
// Portal state
// ---------------------------------------------------------------------------
static WebServer *portalServer = nullptr;
static DNSServer *portalDNS    = nullptr;
static bool       portalDone   = false;

// ---------------------------------------------------------------------------
// NVS load / save
// ---------------------------------------------------------------------------
static void wkLoadSettings() {
  Preferences prefs;
  prefs.begin("wikicyd", true);
  String ssid     = prefs.getString("ssid",     "");
  String pass     = prefs.getString("pass",     "");
  String fcolor   = prefs.getString("fcolor",   "blue");
  wk_font_size        = (uint8_t)prefs.getUChar("fsize",    1);
  wk_refresh_interval = (uint8_t)prefs.getUChar("interval", 4);
  wk_scroll_interval  = (uint8_t)prefs.getUChar("scroll",   0);
  prefs.end();
  ssid.toCharArray(wk_wifi_ssid,    sizeof(wk_wifi_ssid));
  pass.toCharArray(wk_wifi_pass,    sizeof(wk_wifi_pass));
  fcolor.toCharArray(wk_font_color, sizeof(wk_font_color));
  if (wk_font_size < 1 || wk_font_size > 3) wk_font_size = 1;
  if (wk_refresh_interval > 6) wk_refresh_interval = 4;
  if (wk_scroll_interval  > 4) wk_scroll_interval  = 0;
  wk_has_settings = (ssid.length() > 0);
}

static void wkSaveSettings(const char* ssid, const char* pass,
                            const char* fcolor, uint8_t fsize,
                            uint8_t interval, uint8_t scroll) {
  Preferences prefs;
  prefs.begin("wikicyd", false);
  prefs.putString("ssid",     ssid);
  prefs.putString("pass",     pass);
  prefs.putString("fcolor",   fcolor);
  prefs.putUChar("fsize",     fsize);
  prefs.putUChar("interval",  interval);
  prefs.putUChar("scroll",    scroll);
  prefs.end();
  strncpy(wk_wifi_ssid,   ssid,   sizeof(wk_wifi_ssid)    - 1);
  strncpy(wk_wifi_pass,   pass,   sizeof(wk_wifi_pass)    - 1);
  strncpy(wk_font_color,  fcolor, sizeof(wk_font_color)   - 1);
  wk_font_size        = fsize;
  wk_refresh_interval = interval;
  wk_scroll_interval  = scroll;
  wk_has_settings     = true;
}

// ---------------------------------------------------------------------------
// On-screen setup instructions
// ---------------------------------------------------------------------------
static void wkShowPortalScreen() {
  gfx->fillScreen(RGB565_BLACK);

  gfx->setTextColor(0x3B9F);  // Wikipedia blue
  gfx->setTextSize(2);
  gfx->setCursor(20, 8);
  gfx->print("WikiCYD Setup");

  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(50, 32);
  gfx->print("Wikipedia for CYD");

  gfx->setTextColor(0xFFE0);
  gfx->setCursor(4, 54);
  gfx->print("1. Connect to WiFi:");
  gfx->setTextColor(0x3B9F);
  gfx->setTextSize(2);
  gfx->setCursor(14, 66);
  gfx->print("WikiCYD_Setup");

  gfx->setTextColor(0xFFE0);
  gfx->setTextSize(1);
  gfx->setCursor(4, 92);
  gfx->print("2. Open browser:");
  gfx->setTextColor(0x3B9F);
  gfx->setTextSize(2);
  gfx->setCursor(50, 104);
  gfx->print("192.168.4.1");

  gfx->setTextColor(0xFFE0);
  gfx->setTextSize(1);
  gfx->setCursor(4, 130);
  gfx->print("3. Enter WiFi and tap Save.");

  if (wk_has_settings) {
    gfx->setTextColor(0x07E0);
    gfx->setCursor(4, 154);
    gfx->print("Existing settings found.");
    gfx->setCursor(4, 166);
    gfx->print("Tap 'No Changes' to keep.");
  }
}

// ---------------------------------------------------------------------------
// Web handlers
// ---------------------------------------------------------------------------
static void wkHandleRoot() {
  String html =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>WikiCYD Setup</title>"
    "<style>"
    "body{background:#0d0d1a;color:#3b7cf8;font-family:Arial,sans-serif;"
         "text-align:center;padding:20px;max-width:480px;margin:auto;}"
    "h1{color:#3b7cf8;font-size:1.6em;margin-bottom:4px;}"
    "p{color:#334488;font-size:0.9em;}"
    "label{display:block;text-align:left;margin:14px 0 4px;color:#6699cc;"
          "font-weight:bold;}"
    "input[type=text],input[type=password]{width:100%;box-sizing:border-box;"
          "background:#0a0a20;color:#6699ff;border:2px solid #223388;"
          "border-radius:6px;padding:10px;font-size:1em;}"
    ".btn{display:block;width:100%;padding:14px;margin:10px 0;font-size:1.05em;"
         "border-radius:8px;border:none;cursor:pointer;font-weight:bold;}"
    ".btn-save{background:#111133;color:#3b7cf8;border:2px solid #2244aa;}"
    ".btn-save:hover{background:#222255;}"
    ".btn-skip{background:#111;color:#555;border:2px solid #333;}"
    ".btn-skip:hover{background:#222;color:#888;}"
    ".note{color:#223355;font-size:0.82em;margin-top:16px;}"
    "hr{border:1px solid #112244;margin:20px 0;}"
    ".rg{text-align:left;margin:6px 0 14px;display:flex;flex-wrap:wrap;gap:4px 18px;}"
    ".rl{color:#6699cc;cursor:pointer;font-weight:normal;margin:0;}"
    ".rl input{width:auto;border:none;padding:0;background:none;margin-right:4px;}"
    "</style></head><body>"
    "<h1>&#128218; WikiCYD</h1>"
    "<p>Random Wikipedia articles on your CYD.</p>"
    "<form method='post' action='/save'>"
    "<label>WiFi Network (SSID):</label>"
    "<input type='text' name='ssid' value='";
  html += String(wk_wifi_ssid);
  html +=
    "' placeholder='Your 2.4 GHz WiFi name' maxlength='63' required>"
    "<label>WiFi Password:</label>"
    "<input type='password' name='pass' value='";
  html += String(wk_wifi_pass);
  html += "' placeholder='Leave blank if open network' maxlength='63'><br>";

  // Font color
  html += "<label>Font Color Theme:</label><div class='rg'>";
  const char* colorVals[]   = {"blue","orange","green","cyan","white","multi"};
  const char* colorLabels[] = {
    "&#128153; Blue (default)", "&#127818; Orange", "&#129376; Green",
    "&#128306; Cyan", "&#11036; White", "&#127752; Multicolor (each line)"
  };
  for (int i = 0; i < 6; i++) {
    html += "<label class='rl'><input type='radio' name='fcolor' value='";
    html += colorVals[i]; html += "'";
    if (strcmp(wk_font_color, colorVals[i]) == 0) html += " checked";
    html += "> "; html += colorLabels[i]; html += "</label>";
  }
  html += "</div>";

  // Font size
  html += "<label>Font Size:</label><div class='rg'>";
  const char* sizeVals[]   = {"1","2","3"};
  const char* sizeLabels[] = {"Small","Medium","Large"};
  for (int i = 0; i < 3; i++) {
    html += "<label class='rl'><input type='radio' name='fsize' value='";
    html += sizeVals[i]; html += "'";
    if (wk_font_size == (uint8_t)(i + 1)) html += " checked";
    html += "> "; html += sizeLabels[i]; html += "</label>";
  }
  html += "</div>";

  // Refresh interval
  html += "<label>Auto-Refresh Interval:</label><div class='rg'>";
  const char* intVals[]   = {"0","1","2","3","4","5","6"};
  const char* intLabels[] = {"15 seconds","30 seconds","1 minute","3 minutes","5 minutes (default)","30 minutes","1 hour"};
  for (int i = 0; i < 7; i++) {
    html += "<label class='rl'><input type='radio' name='interval' value='";
    html += intVals[i]; html += "'";
    if (wk_refresh_interval == (uint8_t)i) html += " checked";
    html += "> "; html += intLabels[i]; html += "</label>";
  }
  html +=
    "</div>";

  // Auto-scroll speed
  html += "<label>Auto-Scroll Speed (on-screen text):</label><div class='rg'>";
  const char* scrollVals[]   = {"0","1","2","3","4"};
  const char* scrollLabels[] = {"Off (default)","30 seconds","1 minute","90 seconds","2 minutes"};
  for (int i = 0; i < 5; i++) {
    html += "<label class='rl'><input type='radio' name='scroll' value='";
    html += scrollVals[i]; html += "'";
    if (wk_scroll_interval == (uint8_t)i) html += " checked";
    html += "> "; html += scrollLabels[i]; html += "</label>";
  }
  html +=
    "</div><br>"
    "<button class='btn btn-save' type='submit'>&#128190; Save &amp; Connect</button>"
    "</form>";

  if (wk_has_settings) {
    html +=
      "<hr>"
      "<form method='post' action='/nochange'>"
      "<button class='btn btn-skip' type='submit'>"
      "&#10006; No Changes &mdash; Use Current Settings"
      "</button></form>";
  }

  html +=
    "<p class='note'>&#9888; ESP32 supports 2.4 GHz WiFi only.</p>"
    "</body></html>";

  portalServer->send(200, "text/html", html);
}

static void wkHandleSave() {
  String ssid = portalServer->hasArg("ssid") ? portalServer->arg("ssid") : "";
  String pass = portalServer->hasArg("pass") ? portalServer->arg("pass") : "";

  if (ssid.length() == 0) {
    portalServer->send(400, "text/html",
      "<html><body style='background:#0d0d1a;color:#ff5555;font-family:Arial;"
      "text-align:center;padding:40px'>"
      "<h2>&#10060; SSID cannot be empty!</h2>"
      "<a href='/' style='color:#3b7cf8'>&#8592; Go Back</a></body></html>");
    return;
  }

  wkSaveSettings(ssid.c_str(), pass.c_str(),
    (portalServer->hasArg("fcolor")   ? portalServer->arg("fcolor").c_str()                                 : "blue"),
    (portalServer->hasArg("fsize")    ? (uint8_t)constrain(portalServer->arg("fsize").toInt(),    1, 3)     : 1),
    (portalServer->hasArg("interval") ? (uint8_t)constrain(portalServer->arg("interval").toInt(), 0, 6)     : 4),
    (portalServer->hasArg("scroll")   ? (uint8_t)constrain(portalServer->arg("scroll").toInt(),   0, 4)     : 0));

  portalServer->send(200, "text/html",
    "<html><head><meta charset='UTF-8'>"
    "<style>body{background:#0d0d1a;color:#3b7cf8;font-family:Arial;"
    "text-align:center;padding:40px;}"
    "h2{color:#6699ff;}p{color:#334488;}</style></head><body>"
    "<h2>&#9989; Settings Saved!</h2>"
    "<p>Connecting to <b>" + ssid + "</b>...</p>"
    "<p>You can close this page and disconnect from <b>WikiCYD_Setup</b>.</p>"
    "</body></html>");
  delay(1500);
  portalDone = true;
}

static void wkHandleNoChange() {
  portalServer->send(200, "text/html",
    "<html><head><meta charset='UTF-8'>"
    "<style>body{background:#0d0d1a;color:#3b7cf8;font-family:Arial;"
    "text-align:center;padding:40px;}"
    "h2{color:#6699ff;}p{color:#334488;}</style></head><body>"
    "<h2>&#128077; No Changes</h2>"
    "<p>Using saved settings. Device connecting now.</p>"
    "<p>Disconnect from <b>WikiCYD_Setup</b>.</p>"
    "</body></html>");
  delay(1500);
  portalDone = true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
static void wkInitPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("WikiCYD_Setup", "");
  delay(500);

  portalDNS    = new DNSServer();
  portalServer = new WebServer(80);

  portalDNS->start(53, "*", WiFi.softAPIP());
  portalServer->on("/",         wkHandleRoot);
  portalServer->on("/save",     HTTP_POST, wkHandleSave);
  portalServer->on("/nochange", HTTP_POST, wkHandleNoChange);
  portalServer->onNotFound(wkHandleRoot);
  portalServer->begin();

  portalDone = false;
  wkShowPortalScreen();

  Serial.printf("[Portal] AP up — connect to WikiCYD_Setup, open %s\n",
                WiFi.softAPIP().toString().c_str());
}

static void wkRunPortal()   { portalDNS->processNextRequest(); portalServer->handleClient(); }

static void wkClosePortal() {
  portalServer->stop();
  portalDNS->stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(300);
  delete portalServer; portalServer = nullptr;
  delete portalDNS;    portalDNS    = nullptr;
}
