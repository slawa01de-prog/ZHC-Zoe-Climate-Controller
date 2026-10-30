/*
  =====================================================================
  ZOE CABIN HEATER CONTROLLER V4.6
  ESP32-C3 SUPER MINI
  =====================================================================

  SAFE CONTROL / BENCH VERSION

  V4.6 BEDIENKONZEPT:
    - Echtes hierarchisches Terminal-Menue mit Untermenues.
    - WLAN Access Point mit fester SSID: ZHC
    - WLAN im Terminal ein-/ausschaltbar.
    - WLAN-Startverhalten dauerhaft einstellbar.
    - Geschuetztes Webinterface mit Login.
    - Initialpasswort nur lokal im Terminal sichtbar.
    - Erster Login erzwingt eigenes Passwort.
    - Eigenes Passwort wird nur als gesalzener SHA-256 Hash gespeichert.
    - Session-Timeout und Login-Limit.
    - Lokaler Credential-Recovery ueber Terminal oder Boot-Taster.
    - Geschuetztes OTA-Firmwareupdate.
    - OTA schaltet eigene Heizanforderung vorher sicher AUS.
    - Beim Start erscheint nur das kompakte Hauptmenue.
    - Zahlenwahl fuer normale Bedienung.
    - 'b' = zurueck, 'm' = Hauptmenue.
    - Alte Textbefehle bleiben als Expertenmodus erhalten.
    - Fehler sperren nur die eigene Heizfreigabe.
    - Terminal, Sensor-Setup und Analyse bleiben IMMER bedienbar.
    - Nach dem Flashen ohne Sensorzuordnung kein FAILSAFE.
    - 'analyse' zeigt/markiert einen neuen Fehler als gelesen.
    - 'fault reset' hebt den Safety-Lock erst nach Fehlerbehebung auf.

  SENSORROLLEN:
    CABIN_LOW   = Fussraum
    CABIN_HIGH  = Oberkoerper/Kopfhoehe
    OUTSIDE     = Aussentemperatur
    SAFETY      = optionaler Kontrollsensor

  WICHTIG:
  HEATER1_OUT / HEATER2_OUT / HEATER3_OUT sind ausschliesslich
  3.3-V LOGIK-/BENCH-AUSGAENGE.
  Keine direkte Verbindung zu HV/PTC-Leistungspfaden.

  Terminal (115200 Baud):
    menu
    status
    analyse
    analyse history
    analyse last
    diag
    thermal

    sensor scan
    sensor setup
    sensor status

    assign low 0
    assign low none

    assign high 1
    assign high none

    assign outside 2
    assign outside none

    assign safety 3
    assign safety none

    offset low 0.0
    offset high 0.0
    offset outside 0.0
    offset safety 0.0

    weight low 40
    weight high 60

    safetydiff 3.0
    safetydelay 30

    mode oem
    mode auto
    mode boost
    mode manual

    stage 0..3

    target 21.0
    deadband 0.25
    restart 0.50

    kp 22
    ki 0.12
    trendgain 25

    threshold1 20
    threshold2 50
    threshold3 80

    mintime 60
    stagedelay 30
    boosttime 300

    learning on
    learning off
    learning reset
    learnrate 0.05
    learntime 120

    emergency 35
    sensortimeout 10
    tempjump 4

    fault
    fault history
    fault last
    fault reset

    save
    defaults
    reboot
  =====================================================================
*/

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <mbedtls/sha256.h>

// =====================================================================
// VERSION
// =====================================================================

const char* FW_VERSION = "ZOE Heater Controller V4.6";

// =====================================================================
// GPIO
// =====================================================================

constexpr uint8_t PIN_BUTTON  = 3;
constexpr uint8_t PIN_RGB_R   = 4;
constexpr uint8_t PIN_RGB_G   = 5;
constexpr uint8_t PIN_RGB_B   = 6;
constexpr uint8_t PIN_ONEWIRE = 7;

// NUR LOGIK / BENCH
constexpr uint8_t HEATER1_OUT = 8;
constexpr uint8_t HEATER2_OUT = 9;
constexpr uint8_t HEATER3_OUT = 10;

constexpr bool RGB_COMMON_ANODE = false;

// =====================================================================
// ZEITEN
// =====================================================================

constexpr uint32_t SENSOR_INTERVAL_MS     = 1000;
constexpr uint32_t CONTROL_INTERVAL_MS    = 5000;
constexpr uint32_t BUTTON_DEBOUNCE_MS     = 40;
constexpr uint32_t BUTTON_LONGPRESS_MS    = 1500;

constexpr uint8_t MAX_DISCOVERED_SENSORS = 8;
constexpr uint8_t FAULT_HISTORY_SIZE      = 10;

// =====================================================================
// ENUMS
// =====================================================================

enum ControllerMode
{
  MODE_OEM = 0,
  MODE_AUTO,
  MODE_BOOST,
  MODE_MANUAL,
  MODE_FAILSAFE
};

enum SensorRoleId
{
  ROLE_LOW = 0,
  ROLE_HIGH,
  ROLE_OUTSIDE,
  ROLE_SAFETY
};

enum FaultCode
{
  FAULT_NONE = 0,
  FAULT_NO_CABIN_SENSOR = 1,
  FAULT_CABIN_IMPLAUSIBLE = 2,
  FAULT_TEMP_JUMP = 3,
  FAULT_CABIN_TIMEOUT = 4,
  FAULT_CONTROLLER_INVALID = 5,
  FAULT_CONFIG_INVALID = 6,
  FAULT_OVERTEMP = 7,
  FAULT_CONTROL_TIMEOUT = 8,
  FAULT_OUTPUT_INVALID = 9,
  FAULT_SAFETY_DIFFERENCE = 10,
  FAULT_SAFETY_SENSOR = 11
};

enum WarningCode
{
  WARNING_NONE = 0,
  WARNING_OUTSIDE_MISSING = 1,
  WARNING_ONE_CABIN_SENSOR_MISSING = 2,
  WARNING_LEARNING_IMPLAUSIBLE = 3,
  WARNING_TEMP_RISE_LOW = 4,
  WARNING_SETUP_REQUIRED = 5
};

// =====================================================================
// SETTINGS
// =====================================================================

struct Settings
{
  float targetTemp = 21.0f;
  float deadBand = 0.25f;
  float restartDelta = 0.50f;

  // Gewichtung virtuelle Innenraumtemperatur
  float weightLow = 40.0f;
  float weightHigh = 60.0f;

  // PI + Trend
  float kp = 22.0f;
  float ki = 0.12f;
  float trendGain = 25.0f;
  float integralLimit = 100.0f;

  // Schichtungs-/Komfortkorrektur
  float highZoneLimitOffset = 0.50f;   // HIGH > Soll + 0.5 K -> Leistung reduzieren
  float highZoneReduceGain  = 15.0f;   // %-Punkte pro K
  float highZoneMaxReduction = 30.0f;  // max. Reduktion

  // Stufen
  float stage1Threshold = 20.0f;
  float stage2Threshold = 50.0f;
  float stage3Threshold = 80.0f;

  uint32_t minStageTimeSec = 60;
  uint32_t stageDelaySec = 30;

  // Boost
  uint32_t boostTimeSec = 300;

  // Lernen
  bool learningEnabled = true;
  float learningRate = 0.05f;
  uint32_t learningMinTimeSec = 120;
  float maxLearningTrend = 1.5f;

  // Sicherheit
  float minValidCabinTemp = -20.0f;
  float maxValidCabinTemp = 55.0f;
  float emergencyMaxTemp = 35.0f;
  float maxTempJump = 4.0f;
  uint32_t sensorTimeoutSec = 10;

  // SAFETY Sensor (nur wenn zugewiesen)
  float safetyMaxDifference = 3.0f;
  uint32_t safetyDifferenceDelaySec = 30;

  // Diagnose
  uint32_t noHeatRiseWarningSec = 600;
};

struct ThermalModel
{
  float gainStage1 = 0.08f;
  float gainStage2 = 0.18f;
  float gainStage3 = 0.32f;
  float coastTemp = 0.30f;

  uint32_t samplesStage1 = 0;
  uint32_t samplesStage2 = 0;
  uint32_t samplesStage3 = 0;
};

struct SensorRole
{
  bool assigned = false;
  bool present = false;
  bool valid = false;

  DeviceAddress address = {0,0,0,0,0,0,0,0};

  float offset = 0.0f;
  float temperature = NAN;

  uint32_t lastValidMillis = 0;
};

struct FaultRecord
{
  FaultCode code = FAULT_NONE;
  uint32_t timestamp = 0;

  float lowTemp = NAN;
  float highTemp = NAN;
  float outsideTemp = NAN;
  float safetyTemp = NAN;
  float controlTemp = NAN;

  uint8_t stage = 0;
};

// =====================================================================
// GLOBALS
// =====================================================================

Settings cfg;
ThermalModel model;

ControllerMode currentMode = MODE_OEM;

SensorRole sensorLow;
SensorRole sensorHigh;
SensorRole sensorOutside;
SensorRole sensorSafety;

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

DeviceAddress discoveredSensors[MAX_DISCOVERED_SENSORS];
uint8_t discoveredSensorCount = 0;


Preferences preferences;

// =====================================================================
// WLAN / WEB / OTA
// =====================================================================

constexpr const char* ZHC_AP_SSID = "ZHC";
constexpr uint32_t WEB_SESSION_TIMEOUT_MS = 15UL * 60UL * 1000UL;
constexpr uint8_t MAX_LOGIN_FAILURES = 5;
constexpr uint32_t LOGIN_LOCKOUT_MS = 60UL * 1000UL;
constexpr uint32_t BOOT_RECOVERY_HOLD_MS = 8000UL;

WebServer webServer(80);

bool wifiEnabled = false;
bool wifiStartupEnabled = false;
bool webServerRunning = false;

String apPassword;

String authSalt;
String passwordHash;
String temporaryPassword;
bool forcePasswordChange = true;

String sessionToken;
uint32_t sessionLastActivityMillis = 0;

uint8_t failedLoginCount = 0;
uint32_t loginLockoutUntilMillis = 0;

bool otaInProgress = false;
bool otaUploadAuthorized = false;


FaultCode activeFault = FAULT_NONE;
WarningCode activeWarning = WARNING_NONE;
bool faultLatched = false;

// Neuer Fehler wurde noch nicht ueber "analyse" angesehen.
bool faultUnread = false;

FaultRecord faultHistory[FAULT_HISTORY_SIZE];
uint8_t faultHistoryCount = 0;
uint8_t faultHistoryIndex = 0;

// Regelung
float controlTemp = NAN;
float previousControlTemp = NAN;
float stratification = NAN;

float tempTrendPerMinute = 0.0f;
float temperatureError = 0.0f;
float proportionalTerm = 0.0f;
float integralTerm = 0.0f;
float trendCorrection = 0.0f;
float stratificationCorrection = 0.0f;
float predictedTemp = NAN;
float controllerOutput = 0.0f;
bool targetHoldActive = false;

// Heizstufen
uint8_t requestedStage = 0;
uint8_t activeStage = 0;
uint8_t manualStage = 0;
uint32_t lastStageChangeMillis = 0;

// Boost
uint32_t boostStartMillis = 0;

// Lernen
uint8_t learningStage = 0;
float learningStartTemp = NAN;
float learningStartOutsideTemp = NAN;
uint32_t learningStartMillis = 0;

// Heating response
float heatRiseStartTemp = NAN;
uint32_t heatRiseStartMillis = 0;

// Safety
uint32_t safetyDifferenceStartMillis = 0;

// Takt
uint32_t lastSensorCycleMillis = 0;
uint32_t lastControlCycleMillis = 0;

// Button
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
uint32_t buttonChangeMillis = 0;
uint32_t buttonPressMillis = 0;
bool longPressHandled = false;

// Serial
String terminalBuffer;

// =====================================================================
// HIERARCHISCHES TERMINAL-MENUE
// =====================================================================

enum MenuState
{
  MENU_MAIN = 0,
  MENU_STATUS,
  MENU_SENSORS,
  MENU_MODE,
  MENU_TEMPERATURE,
  MENU_CONTROLLER,
  MENU_STAGES,
  MENU_LEARNING,
  MENU_ANALYSIS,
  MENU_SYSTEM,
  MENU_WIFI,
  MENU_SECURITY
};

MenuState currentMenu = MENU_MAIN;

void printCurrentMenu();


// =====================================================================
// HELPER
// =====================================================================

const char* modeName()
{
  switch (currentMode)
  {
    case MODE_OEM: return "OEM";
    case MODE_AUTO: return "AUTO";
    case MODE_BOOST: return "BOOST";
    case MODE_MANUAL: return "MANUAL";
    case MODE_FAILSAFE: return "FAILSAFE";
  }
  return "UNKNOWN";
}

const char* faultName(FaultCode code)
{
  switch (code)
  {
    case FAULT_NONE: return "Kein Fehler";
    case FAULT_NO_CABIN_SENSOR: return "Kein gueltiger Innenraumsensor";
    case FAULT_CABIN_IMPLAUSIBLE: return "Innenraumtemperatur unplausibel";
    case FAULT_TEMP_JUMP: return "Unplausibler Temperatursprung";
    case FAULT_CABIN_TIMEOUT: return "Innenraumsensor Timeout";
    case FAULT_CONTROLLER_INVALID: return "Reglerwert ungueltig";
    case FAULT_CONFIG_INVALID: return "Konfiguration ungueltig";
    case FAULT_OVERTEMP: return "Innenraum-Uebertemperatur";
    case FAULT_CONTROL_TIMEOUT: return "Regelzyklus Timeout";
    case FAULT_OUTPUT_INVALID: return "Ausgangszustand unplausibel";
    case FAULT_SAFETY_DIFFERENCE: return "SAFETY-Abweichung zu gross";
    case FAULT_SAFETY_SENSOR: return "SAFETY Sensor fehlt/ungueltig";
  }
  return "Unbekannter Fehler";
}

const char* warningName(WarningCode code)
{
  switch (code)
  {
    case WARNING_NONE: return "Keine Warnung";
    case WARNING_OUTSIDE_MISSING: return "OUTSIDE zugewiesen aber nicht verfuegbar";
    case WARNING_ONE_CABIN_SENSOR_MISSING: return "Nur ein Innenraumsensor verfuegbar";
    case WARNING_LEARNING_IMPLAUSIBLE: return "Lernwert unplausibel";
    case WARNING_TEMP_RISE_LOW: return "Temperaturanstieg trotz Heizung gering";
    case WARNING_SETUP_REQUIRED: return "Sensor-Setup erforderlich";
  }
  return "Unbekannte Warnung";
}

String roleName(SensorRoleId role)
{
  switch (role)
  {
    case ROLE_LOW: return "CABIN_LOW";
    case ROLE_HIGH: return "CABIN_HIGH";
    case ROLE_OUTSIDE: return "OUTSIDE";
    case ROLE_SAFETY: return "SAFETY";
  }
  return "UNKNOWN";
}

SensorRole* getRole(SensorRoleId role)
{
  switch (role)
  {
    case ROLE_LOW: return &sensorLow;
    case ROLE_HIGH: return &sensorHigh;
    case ROLE_OUTSIDE: return &sensorOutside;
    case ROLE_SAFETY: return &sensorSafety;
  }
  return nullptr;
}

// =====================================================================
// ADDRESS
// =====================================================================

String addressToString(const DeviceAddress address)
{
  String result;

  for (uint8_t i = 0; i < 8; i++)
  {
    if (address[i] < 16) result += "0";
    result += String(address[i], HEX);
  }

  result.toUpperCase();
  return result;
}

int hexValue(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  return -1;
}

bool stringToAddress(String text, DeviceAddress address)
{
  text.trim();

  if (text.length() != 16)
    return false;

  for (uint8_t i = 0; i < 8; i++)
  {
    int h = hexValue(text[i * 2]);
    int l = hexValue(text[i * 2 + 1]);

    if (h < 0 || l < 0)
      return false;

    address[i] = (h << 4) | l;
  }

  return true;
}

bool addressEqual(const DeviceAddress a, const DeviceAddress b)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (a[i] != b[i])
      return false;
  }

  return true;
}

// =====================================================================
// RGB
// =====================================================================

void writeRGBPin(uint8_t pin, bool state)
{
  digitalWrite(pin, RGB_COMMON_ANODE ? !state : state);
}

void setRGB(bool r, bool g, bool b)
{
  writeRGBPin(PIN_RGB_R, r);
  writeRGBPin(PIN_RGB_G, g);
  writeRGBPin(PIN_RGB_B, b);
}

void rgbOff()     { setRGB(false, false, false); }
void rgbBlue()    { setRGB(false, false, true); }
void rgbGreen()   { setRGB(false, true, false); }
void rgbRed()     { setRGB(true, false, false); }
void rgbYellow()  { setRGB(true, true, false); }
void rgbCyan()    { setRGB(false, true, true); }
void rgbMagenta() { setRGB(true, false, true); }

// =====================================================================
// OUTPUTS
// =====================================================================

void forceOutputsOff()
{
  digitalWrite(HEATER1_OUT, LOW);
  digitalWrite(HEATER2_OUT, LOW);
  digitalWrite(HEATER3_OUT, LOW);
}

void applyBenchOutputs()
{
  if (faultLatched || currentMode == MODE_FAILSAFE)
  {
    forceOutputsOff();
    return;
  }

  digitalWrite(HEATER1_OUT, activeStage >= 1 ? HIGH : LOW);
  digitalWrite(HEATER2_OUT, activeStage >= 2 ? HIGH : LOW);
  digitalWrite(HEATER3_OUT, activeStage >= 3 ? HIGH : LOW);
}


// =====================================================================
// SECURITY / RANDOM / HASH
// =====================================================================

String randomToken(size_t length)
{
  static const char alphabet[] =
    "ABCDEFGHJKLMNPQRSTUVWXYZ"
    "abcdefghijkmnopqrstuvwxyz"
    "23456789";

  String result;
  result.reserve(length);

  for (size_t i = 0; i < length; i++)
  {
    uint32_t r = esp_random();
    result += alphabet[r % (sizeof(alphabet) - 1)];
  }

  return result;
}

String randomHex(size_t bytes)
{
  static const char hex[] = "0123456789ABCDEF";

  String result;
  result.reserve(bytes * 2);

  for (size_t i = 0; i < bytes; i++)
  {
    uint8_t b = (uint8_t)(esp_random() & 0xFF);
    result += hex[(b >> 4) & 0x0F];
    result += hex[b & 0x0F];
  }

  return result;
}

String sha256Hex(const String &input)
{
  unsigned char digest[32];

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);

  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(
    &ctx,
    reinterpret_cast<const unsigned char*>(input.c_str()),
    input.length()
  );
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);

  static const char hex[] = "0123456789abcdef";
  String out;
  out.reserve(64);

  for (uint8_t i = 0; i < 32; i++)
  {
    out += hex[(digest[i] >> 4) & 0x0F];
    out += hex[digest[i] & 0x0F];
  }

  return out;
}

String makePasswordHash(const String &password)
{
  return sha256Hex(authSalt + ":" + password);
}

bool constantTimeStringEqual(const String &a, const String &b)
{
  if (a.length() != b.length())
    return false;

  uint8_t diff = 0;

  for (size_t i = 0; i < a.length(); i++)
    diff |= (uint8_t)(a[i] ^ b[i]);

  return diff == 0;
}

void invalidateWebSession()
{
  sessionToken = "";
  sessionLastActivityMillis = 0;
}

void generateTemporaryWebPassword()
{
  temporaryPassword = "ZHC-" + randomToken(8);
  passwordHash = makePasswordHash(temporaryPassword);
  forcePasswordChange = true;
  invalidateWebSession();

  preferences.begin("zhcauth", false);
  preferences.putString("salt", authSalt);
  preferences.putString("hash", passwordHash);
  preferences.putString("temp", temporaryPassword);
  preferences.putBool("force", true);
  preferences.end();
}

void loadSecuritySettings()
{
  preferences.begin("zhcauth", true);

  authSalt = preferences.getString("salt", "");
  passwordHash = preferences.getString("hash", "");
  temporaryPassword = preferences.getString("temp", "");
  forcePasswordChange = preferences.getBool("force", true);

  preferences.end();

  if (authSalt.length() < 16)
  {
    authSalt = randomHex(16);

    preferences.begin("zhcauth", false);
    preferences.putString("salt", authSalt);
    preferences.end();
  }

  if (passwordHash.length() != 64)
  {
    generateTemporaryWebPassword();
  }
}

void saveNewAdminPassword(const String &newPassword)
{
  passwordHash = makePasswordHash(newPassword);
  temporaryPassword = "";
  forcePasswordChange = false;
  invalidateWebSession();

  preferences.begin("zhcauth", false);
  preferences.putString("salt", authSalt);
  preferences.putString("hash", passwordHash);
  preferences.remove("temp");
  preferences.putBool("force", false);
  preferences.end();
}

void resetWebCredentials()
{
  generateTemporaryWebPassword();

  Serial.println();
  Serial.println("========================================");
  Serial.println(" WEB-ZUGANG ZURUECKGESETZT");
  Serial.println("========================================");
  Serial.println("Benutzer       : admin");
  Serial.print("Initialpasswort: ");
  Serial.println(temporaryPassword);
  Serial.println();
  Serial.println("Beim ersten Login muss ein neues");
  Serial.println("Administrator-Passwort gesetzt werden.");
  Serial.println("========================================");
}

bool isLoginLocked()
{
  if (loginLockoutUntilMillis == 0)
    return false;

  if ((int32_t)(millis() - loginLockoutUntilMillis) >= 0)
  {
    loginLockoutUntilMillis = 0;
    failedLoginCount = 0;
    return false;
  }

  return true;
}

String htmlEscape(const String &value)
{
  String out;
  out.reserve(value.length() + 16);

  for (size_t i = 0; i < value.length(); i++)
  {
    switch (value[i])
    {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += value[i]; break;
    }
  }

  return out;
}

String pageHeader(const String &title)
{
  String html;

  html += F("<!doctype html><html><head>"
            "<meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>ZHC - ");
  html += htmlEscape(title);
  html += F("</title>"
            "<style>"
            "body{font-family:Arial,sans-serif;max-width:760px;margin:30px auto;padding:0 18px;background:#111;color:#eee}"
            ".card{background:#1d1d1d;border:1px solid #333;border-radius:12px;padding:18px;margin:14px 0}"
            "a,button,input{font-size:16px}"
            "a{color:#8ecbff;text-decoration:none}"
            "input{width:100%;box-sizing:border-box;padding:10px;margin:7px 0 14px;border-radius:8px;border:1px solid #555;background:#0d0d0d;color:#fff}"
            "button{padding:10px 16px;border:0;border-radius:8px;cursor:pointer}"
            ".ok{color:#8ce99a}.warn{color:#ffd43b}.err{color:#ff8787}"
            ".nav a{margin-right:14px}"
            "table{width:100%;border-collapse:collapse}td{padding:7px;border-bottom:1px solid #333}"
            "</style></head><body>");
  html += F("<h1>ZOE Climate Controller</h1>");
  html += "<div class='nav'><a href='/'>Status</a><a href='/sensors'>Sensoren</a><a href='/system'>System</a><a href='/update'>Update</a><a href='/logout'>Logout</a></div>";

  return html;
}

String pageFooter()
{
  return F("</body></html>");
}

String getCookieValue(const String &name)
{
  if (!webServer.hasHeader("Cookie"))
    return "";

  String cookie = webServer.header("Cookie");
  String key = name + "=";

  int start = cookie.indexOf(key);
  if (start < 0)
    return "";

  start += key.length();

  int end = cookie.indexOf(';', start);
  if (end < 0)
    end = cookie.length();

  return cookie.substring(start, end);
}

bool isAuthenticated()
{
  if (sessionToken.length() == 0)
    return false;

  if (millis() - sessionLastActivityMillis > WEB_SESSION_TIMEOUT_MS)
  {
    invalidateWebSession();
    return false;
  }

  String cookieToken = getCookieValue("ZHCSESSION");

  if (!constantTimeStringEqual(cookieToken, sessionToken))
    return false;

  sessionLastActivityMillis = millis();
  return true;
}

bool requireAuthentication(bool allowForcedPasswordPage = false)
{
  if (!isAuthenticated())
  {
    webServer.sendHeader("Location", "/login");
    webServer.send(302, "text/plain", "");
    return false;
  }

  if (forcePasswordChange && !allowForcedPasswordPage)
  {
    webServer.sendHeader("Location", "/password");
    webServer.send(302, "text/plain", "");
    return false;
  }

  return true;
}

// =====================================================================
// WEB PAGES
// =====================================================================

void handleLoginPage()
{
  if (webServer.method() == HTTP_GET)
  {
    String html =
      F("<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ZHC Login</title>"
        "<style>body{font-family:Arial;max-width:420px;margin:60px auto;padding:20px;background:#111;color:#eee}"
        ".card{background:#1d1d1d;padding:24px;border-radius:12px}"
        "input{width:100%;box-sizing:border-box;padding:11px;margin:6px 0 16px;background:#0d0d0d;color:#fff;border:1px solid #555;border-radius:8px}"
        "button{padding:10px 16px}</style></head><body>"
        "<div class='card'><h2>ZOE Climate Controller</h2>"
        "<form method='post' action='/login'>"
        "<label>Benutzer</label><input name='user' value='admin' autocomplete='username'>"
        "<label>Passwort</label><input type='password' name='pass' autocomplete='current-password'>"
        "<button type='submit'>Anmelden</button>"
        "</form></div></body></html>");

    webServer.send(200, "text/html; charset=utf-8", html);
    return;
  }

  if (isLoginLocked())
  {
    webServer.send(429, "text/plain; charset=utf-8",
                   "Zu viele Fehlversuche. Bitte spaeter erneut versuchen.");
    return;
  }

  String user = webServer.arg("user");
  String pass = webServer.arg("pass");

  bool userOkay = (user == "admin");
  bool passOkay =
    constantTimeStringEqual(makePasswordHash(pass), passwordHash);

  if (!userOkay || !passOkay)
  {
    failedLoginCount++;

    if (failedLoginCount >= MAX_LOGIN_FAILURES)
    {
      loginLockoutUntilMillis = millis() + LOGIN_LOCKOUT_MS;
      failedLoginCount = 0;
    }

    webServer.send(401, "text/plain; charset=utf-8",
                   "Login fehlgeschlagen.");
    return;
  }

  failedLoginCount = 0;
  loginLockoutUntilMillis = 0;

  sessionToken = randomHex(24);
  sessionLastActivityMillis = millis();

  webServer.sendHeader(
    "Set-Cookie",
    "ZHCSESSION=" + sessionToken +
      "; HttpOnly; SameSite=Strict; Max-Age=900; Path=/"
  );

  webServer.sendHeader(
    "Location",
    forcePasswordChange ? "/password" : "/"  );

  webServer.send(302, "text/plain", "");
}

void handlePasswordPage()
{
  if (!requireAuthentication(true))
    return;

  if (webServer.method() == HTTP_GET)
  {
    String html = pageHeader("Passwort");

    html += F("<div class='card'><h2>Administrator-Passwort</h2>");

    if (forcePasswordChange)
      html += F("<p class='warn'>Ersteinrichtung: Bitte eigenes Passwort setzen.</p>");

    html += F("<form method='post' action='/password'>"
              "<label>Neues Passwort (mindestens 8 Zeichen)</label>"
              "<input type='password' name='p1' autocomplete='new-password'>"
              "<label>Wiederholen</label>"
              "<input type='password' name='p2' autocomplete='new-password'>"
              "<button type='submit'>Passwort speichern</button>"
              "</form></div>");

    html += pageFooter();

    webServer.send(200, "text/html; charset=utf-8", html);
    return;
  }

  String p1 = webServer.arg("p1");
  String p2 = webServer.arg("p2");

  if (p1.length() < 8)
  {
    webServer.send(400, "text/plain; charset=utf-8",
                   "Passwort muss mindestens 8 Zeichen haben.");
    return;
  }

  if (p1 != p2)
  {
    webServer.send(400, "text/plain; charset=utf-8",
                   "Passwoerter stimmen nicht ueberein.");
    return;
  }

  saveNewAdminPassword(p1);

  webServer.sendHeader("Location", "/login");
  webServer.send(302, "text/plain", "");
}

void handleRootPage()
{
  if (!requireAuthentication())
    return;

  String html = pageHeader("Status");

  html += F("<div class='card'><h2>Status</h2><table>");

  html += "<tr><td>Firmware</td><td>" + String(FW_VERSION) + "</td></tr>";
  html += "<tr><td>Modus</td><td>" + String(modeName()) + "</td></tr>";

  html += "<tr><td>Safety-Lock</td><td>";
  html += faultLatched ? "<span class='err'>AKTIV</span>" : "<span class='ok'>AUS</span>";
  html += "</td></tr>";

  html += "<tr><td>Innenraum</td><td>";
  if (isfinite(controlTemp))
    html += String(controlTemp, 2) + " &deg;C";
  else
    html += "---";
  html += "</td></tr>";

  html += "<tr><td>Soll</td><td>" + String(cfg.targetTemp, 2) + " &deg;C</td></tr>";
  html += "<tr><td>Heizstufe</td><td>" + String(activeStage) + "</td></tr>";
  html += "<tr><td>Regler</td><td>" + String(controllerOutput, 1) + " %</td></tr>";

  html += F("</table></div>");

  html += F("<div class='card'><h2>Sicherheit</h2>"
            "<p>Eigene Heizfreigabe und OTA sind getrennt abgesichert.</p>"
            "<p>Firmware-Update schaltet die Logikausgaenge vorher AUS und setzt den Controller auf OEM.</p>"
            "</div>");

  html += pageFooter();

  webServer.send(200, "text/html; charset=utf-8", html);
}

String sensorWebValue(SensorRole &role)
{
  if (!role.assigned)
    return "nicht zugewiesen";

  if (!role.present)
    return "nicht gefunden";

  if (!role.valid)
    return "ungueltig";

  return String(role.temperature, 2) + " &deg;C";
}

void handleSensorsPage()
{
  if (!requireAuthentication())
    return;

  String html = pageHeader("Sensoren");

  html += F("<div class='card'><h2>Sensoren</h2><table>");

  html += "<tr><td>CABIN_LOW</td><td>" + sensorWebValue(sensorLow) + "</td></tr>";
  html += "<tr><td>CABIN_HIGH</td><td>" + sensorWebValue(sensorHigh) + "</td></tr>";
  html += "<tr><td>OUTSIDE</td><td>" + sensorWebValue(sensorOutside) + "</td></tr>";
  html += "<tr><td>SAFETY</td><td>" + sensorWebValue(sensorSafety) + "</td></tr>";

  html += "<tr><td>Virtual Cabin</td><td>";
  if (isfinite(controlTemp))
    html += String(controlTemp, 2) + " &deg;C";
  else
    html += "---";
  html += "</td></tr>";

  html += "<tr><td>Schichtung</td><td>";
  if (isfinite(stratification))
    html += String(stratification, 2) + " K";
  else
    html += "---";
  html += "</td></tr>";

  html += F("</table></div>");
  html += F("<div class='card'><p>Sensor-Zuordnung bleibt in V4.6 bewusst eine lokale Servicefunktion im Terminal.</p></div>");

  html += pageFooter();

  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleSystemPage()
{
  if (!requireAuthentication())
    return;

  String html = pageHeader("System");

  html += F("<div class='card'><h2>System</h2><table>");
  html += "<tr><td>SSID</td><td>ZHC</td></tr>";
  html += "<tr><td>WLAN</td><td>" + String(wifiEnabled ? "AKTIV" : "AUS") + "</td></tr>";
  html += "<tr><td>IP</td><td>" + WiFi.softAPIP().toString() + "</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String(millis() / 1000UL) + " s</td></tr>";
  html += "<tr><td>Passwortstatus</td><td>" + String(forcePasswordChange ? "ERSTEINRICHTUNG" : "GESETZT") + "</td></tr>";
  html += F("</table></div>");

  html += F("<div class='card'><h2>Administration</h2>"
            "<p><a href='/password'>Administrator-Passwort aendern</a></p>"
            "<p><a href='/update'>Firmware-Update</a></p>"
            "</div>");

  html += pageFooter();

  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleUpdatePage()
{
  if (!requireAuthentication())
    return;

  String html = pageHeader("Firmware Update");

  html += F("<div class='card'><h2>Firmware Update</h2>"
            "<p>Nur eine fuer diesen ESP32-C3 erstellte Firmware-Datei verwenden.</p>"
            "<p>Vor dem Schreiben werden eigene Heizanforderung und H1/H2/H3 sicher abgeschaltet.</p>"
            "<form method='POST' action='/update' enctype='multipart/form-data'>"
            "<input type='file' name='firmware' accept='.bin'>"
            "<button type='submit'>Update starten</button>"
            "</form></div>");

  html += pageFooter();

  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleUpdateUpload()
{
  if (!isAuthenticated() || forcePasswordChange)
  {
    otaUploadAuthorized = false;
    return;
  }

  otaUploadAuthorized = true;

  HTTPUpload &upload = webServer.upload();

  if (upload.status == UPLOAD_FILE_START)
  {
    otaInProgress = true;

    // Vor OTA: eigene Regelung in sicheren Zustand.
    currentMode = MODE_OEM;
    requestedStage = 0;
    activeStage = 0;
    controllerOutput = 0.0f;
    integralTerm = 0.0f;
    forceOutputsOff();

    Serial.println();
    Serial.println("[OTA] Update gestartet.");
    Serial.println("[OTA] Eigene Heizanforderung AUS / OEM.");

    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (Update.end(true))
    {
      Serial.print("[OTA] Erfolgreich. Bytes: ");
      Serial.println(upload.totalSize);
    }
    else
    {
      Update.printError(Serial);
    }

    otaInProgress = false;
  }
  else if (upload.status == UPLOAD_FILE_ABORTED)
  {
    Update.abort();
    otaInProgress = false;
    Serial.println("[OTA] Abgebrochen.");
  }
}

void handleUpdateFinish()
{
  if (!otaUploadAuthorized)
  {
    webServer.send(401, "text/plain; charset=utf-8",
                   "Nicht autorisiert.");
    return;
  }

  if (Update.hasError())
  {
    webServer.send(500, "text/plain; charset=utf-8",
                   "Firmware-Update fehlgeschlagen.");
    return;
  }

  webServer.send(
    200,
    "text/html; charset=utf-8",
    "<!doctype html><html><body><h2>Update erfolgreich</h2>"
    "<p>Controller startet neu.</p></body></html>"
  );

  delay(500);
  forceOutputsOff();
  ESP.restart();
}

void handleLogout()
{
  invalidateWebSession();

  webServer.sendHeader(
    "Set-Cookie",
    "ZHCSESSION=; HttpOnly; SameSite=Strict; Max-Age=0; Path=/"
  );

  webServer.sendHeader("Location", "/login");
  webServer.send(302, "text/plain", "");
}

void configureWebServer()
{
  const char* headerKeys[] = { "Cookie" };
  webServer.collectHeaders(headerKeys, 1);

  webServer.on("/login", HTTP_GET, handleLoginPage);
  webServer.on("/login", HTTP_POST, handleLoginPage);

  webServer.on("/password", HTTP_GET, handlePasswordPage);
  webServer.on("/password", HTTP_POST, handlePasswordPage);

  webServer.on("/", HTTP_GET, handleRootPage);
  webServer.on("/sensors", HTTP_GET, handleSensorsPage);
  webServer.on("/system", HTTP_GET, handleSystemPage);

  webServer.on("/update", HTTP_GET, handleUpdatePage);
  webServer.on(
    "/update",
    HTTP_POST,
    handleUpdateFinish,
    handleUpdateUpload
  );

  webServer.on("/logout", HTTP_GET, handleLogout);

  webServer.onNotFound([]()
  {
    if (!isAuthenticated())
    {
      webServer.sendHeader("Location", "/login");
      webServer.send(302, "text/plain", "");
      return;
    }

    webServer.send(404, "text/plain; charset=utf-8", "Nicht gefunden.");
  });
}

// =====================================================================
// WLAN
// =====================================================================

void saveWiFiSettings()
{
  preferences.begin("zhcwifi", false);
  preferences.putBool("startup", wifiStartupEnabled);
  preferences.putString("apPwd", apPassword);
  preferences.end();
}

void loadWiFiSettings()
{
  preferences.begin("zhcwifi", true);

  wifiStartupEnabled =
    preferences.getBool("startup", false);

  apPassword =
    preferences.getString("apPwd", "");

  preferences.end();

  if (apPassword.length() < 8)
  {
    apPassword = "ZHC-" + randomToken(8);
    saveWiFiSettings();
  }
}

void printWiFiStatus()
{
  Serial.println();
  Serial.println("============== WLAN STATUS ====================");

  Serial.print("WLAN           : ");
  Serial.println(wifiEnabled ? "EIN" : "AUS");

  Serial.print("Startup        : ");
  Serial.println(wifiStartupEnabled ? "EIN" : "AUS");

  Serial.print("SSID           : ");
  Serial.println(ZHC_AP_SSID);

  Serial.print("AP-Passwort    : ");
  Serial.println(apPassword);

  Serial.print("Webserver      : ");
  Serial.println(webServerRunning ? "AKTIV" : "AUS");

  if (wifiEnabled)
  {
    Serial.print("IP             : ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Web            : http://192.168.4.1");
  }

  Serial.println("================================================");
}

bool startZHCWiFi()
{
  if (wifiEnabled)
    return true;

  WiFi.mode(WIFI_AP);

  bool ok =
    WiFi.softAP(
      ZHC_AP_SSID,
      apPassword.c_str()
    );

  if (!ok)
  {
    Serial.println("WLAN Start fehlgeschlagen.");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  configureWebServer();
  webServer.begin();

  wifiEnabled = true;
  webServerRunning = true;

  Serial.println();
  Serial.println("WLAN gestartet.");
  Serial.println("SSID: ZHC");
  Serial.print("AP-Passwort: ");
  Serial.println(apPassword);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Web: http://192.168.4.1");

  if (forcePasswordChange)
  {
    Serial.println();
    Serial.println("WEB ERSTEINRICHTUNG");
    Serial.println("Benutzer: admin");
    Serial.print("Initialpasswort: ");
    Serial.println(temporaryPassword);
  }

  return true;
}

void stopZHCWiFi()
{
  invalidateWebSession();

  if (webServerRunning)
  {
    webServer.stop();
    webServerRunning = false;
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  wifiEnabled = false;

  Serial.println("WLAN / Webserver / OTA AUS.");
}

void serviceWebServer()
{
  if (wifiEnabled && webServerRunning)
    webServer.handleClient();
}

// =====================================================================
// BOOT RECOVERY
// =====================================================================

void checkBootCredentialRecovery()
{
  if (digitalRead(PIN_BUTTON) != LOW)
    return;

  Serial.println();
  Serial.println("Taster beim Boot erkannt.");
  Serial.println("Fuer Web-Recovery weiter gedrueckt halten...");

  uint32_t start = millis();

  while (digitalRead(PIN_BUTTON) == LOW)
  {
    if (millis() - start >= BOOT_RECOVERY_HOLD_MS)
    {
      resetWebCredentials();

      Serial.println("Recovery abgeschlossen.");
      Serial.println("Taster loslassen.");

      while (digitalRead(PIN_BUTTON) == LOW)
        delay(20);

      return;
    }

    delay(20);
  }

  Serial.println("Recovery nicht ausgeloest.");
}

// =====================================================================
// CONFIG VALIDATION
// =====================================================================

bool configValid()
{
  if (!isfinite(cfg.targetTemp)) return false;

  if (cfg.targetTemp < 15.0f || cfg.targetTemp > 28.0f) return false;
  if (cfg.deadBand < 0.10f || cfg.deadBand > 2.0f) return false;
  if (cfg.restartDelta < cfg.deadBand || cfg.restartDelta > 5.0f) return false;

  if (cfg.weightLow < 0.0f || cfg.weightHigh < 0.0f) return false;
  if ((cfg.weightLow + cfg.weightHigh) <= 0.0f) return false;

  if (cfg.kp < 0.0f || cfg.kp > 100.0f) return false;
  if (cfg.ki < 0.0f || cfg.ki > 10.0f) return false;
  if (cfg.trendGain < 0.0f || cfg.trendGain > 100.0f) return false;

  if (cfg.stage1Threshold < 0.0f) return false;
  if (cfg.stage2Threshold <= cfg.stage1Threshold) return false;
  if (cfg.stage3Threshold <= cfg.stage2Threshold) return false;
  if (cfg.stage3Threshold > 100.0f) return false;

  if (cfg.emergencyMaxTemp <= cfg.targetTemp) return false;
  if (cfg.sensorTimeoutSec < 2 || cfg.sensorTimeoutSec > 120) return false;

  if (cfg.safetyMaxDifference < 0.5f || cfg.safetyMaxDifference > 15.0f) return false;
  if (cfg.safetyDifferenceDelaySec < 5 || cfg.safetyDifferenceDelaySec > 600) return false;

  return true;
}

// =====================================================================
// SENSOR DISCOVERY
// =====================================================================

void scanSensors()
{
  discoveredSensorCount = 0;

  uint8_t count = sensors.getDeviceCount();
  if (count > MAX_DISCOVERED_SENSORS)
    count = MAX_DISCOVERED_SENSORS;

  for (uint8_t i = 0; i < count; i++)
  {
    DeviceAddress adr;

    if (sensors.getAddress(adr, i))
    {
      memcpy(discoveredSensors[discoveredSensorCount], adr, 8);
      discoveredSensorCount++;
    }
  }

  // present Flags aktualisieren
  SensorRole* roles[] = { &sensorLow, &sensorHigh, &sensorOutside, &sensorSafety };

  for (SensorRole* role : roles)
  {
    role->present = false;

    if (!role->assigned)
      continue;

    for (uint8_t i = 0; i < discoveredSensorCount; i++)
    {
      if (addressEqual(role->address, discoveredSensors[i]))
      {
        role->present = true;
        break;
      }
    }
  }
}

int findSensorIndexByAddress(const DeviceAddress address)
{
  for (uint8_t i = 0; i < discoveredSensorCount; i++)
  {
    if (addressEqual(address, discoveredSensors[i]))
      return i;
  }

  return -1;
}

bool indexAlreadyAssignedToOtherRole(int index, SensorRoleId targetRole)
{
  if (index < 0 || index >= discoveredSensorCount)
    return false;

  for (int r = ROLE_LOW; r <= ROLE_SAFETY; r++)
  {
    if (r == targetRole)
      continue;

    SensorRole* role = getRole((SensorRoleId)r);

    if (role && role->assigned &&
        addressEqual(role->address, discoveredSensors[index]))
    {
      Serial.print("Sensor [");
      Serial.print(index);
      Serial.print("] ist bereits ");
      Serial.print(roleName((SensorRoleId)r));
      Serial.println(" zugeordnet.");
      return true;
    }
  }

  return false;
}

bool assignSensor(SensorRoleId targetRole, int index)
{
  scanSensors();

  if (index < 0 || index >= discoveredSensorCount)
  {
    Serial.println("Ungueltiger Sensorindex.");
    return false;
  }

  if (indexAlreadyAssignedToOtherRole(index, targetRole))
  {
    Serial.println("Zuweisung nicht geaendert.");
    return false;
  }

  SensorRole* role = getRole(targetRole);
  if (!role) return false;

  memcpy(role->address, discoveredSensors[index], 8);
  role->assigned = true;
  role->present = true;
  role->valid = false;

  Serial.print(roleName(targetRole));
  Serial.print(" <- Sensor [");
  Serial.print(index);
  Serial.print("] ");
  Serial.println(addressToString(role->address));

  return true;
}

void unassignSensor(SensorRoleId roleId)
{
  SensorRole* role = getRole(roleId);
  if (!role) return;

  role->assigned = false;
  role->present = false;
  role->valid = false;
  role->temperature = NAN;
  role->lastValidMillis = 0;

  memset(role->address, 0, 8);

  if (roleId == ROLE_SAFETY)
    safetyDifferenceStartMillis = 0;

  Serial.print(roleName(roleId));
  Serial.println(" = NICHT ZUGEWIESEN / DEAKTIVIERT");
}

// =====================================================================
// SENSOR STATUS / SETUP
// =====================================================================

void printSensorScan()
{
  scanSensors();

  Serial.println();
  Serial.println("============== SENSOR SCAN ====================");

  Serial.print("Gefundene DS18B20: ");
  Serial.println(discoveredSensorCount);

  for (uint8_t i = 0; i < discoveredSensorCount; i++)
  {
    Serial.print("[");
    Serial.print(i);
    Serial.print("] ");
    Serial.print(addressToString(discoveredSensors[i]));

    // Zuordnung anzeigen
    String assigned = "FREI";

    if (sensorLow.assigned && addressEqual(sensorLow.address, discoveredSensors[i]))
      assigned = "CABIN_LOW";
    else if (sensorHigh.assigned && addressEqual(sensorHigh.address, discoveredSensors[i]))
      assigned = "CABIN_HIGH";
    else if (sensorOutside.assigned && addressEqual(sensorOutside.address, discoveredSensors[i]))
      assigned = "OUTSIDE";
    else if (sensorSafety.assigned && addressEqual(sensorSafety.address, discoveredSensors[i]))
      assigned = "SAFETY";

    Serial.print("   -> ");
    Serial.println(assigned);
  }

  Serial.println("================================================");
}

void printRoleLine(const char* label, SensorRole &role)
{
  Serial.print(label);
  Serial.print(" : ");

  if (!role.assigned)
  {
    Serial.println("NICHT ZUGEWIESEN / DEAKTIVIERT");
    return;
  }

  int idx = findSensorIndexByAddress(role.address);

  Serial.print("[");
  if (idx >= 0) Serial.print(idx);
  else Serial.print("-");
  Serial.print("] ");

  Serial.print(addressToString(role.address));
  Serial.print("  ");

  if (!role.present)
  {
    Serial.println("NICHT GEFUNDEN");
    return;
  }

  if (!role.valid)
  {
    Serial.println("INVALID");
    return;
  }

  Serial.print(role.temperature, 2);
  Serial.println(" C");
}

void printSensorStatus()
{
  scanSensors();

  Serial.println();
  Serial.println("============== SENSOR STATUS ==================");

  printRoleLine("CABIN_LOW ", sensorLow);
  printRoleLine("CABIN_HIGH", sensorHigh);
  printRoleLine("OUTSIDE   ", sensorOutside);
  printRoleLine("SAFETY    ", sensorSafety);

  Serial.println();

  Serial.print("Weight LOW         : ");
  Serial.print(cfg.weightLow, 1);
  Serial.println(" %");

  Serial.print("Weight HIGH        : ");
  Serial.print(cfg.weightHigh, 1);
  Serial.println(" %");

  Serial.print("Virtual Cabin      : ");
  if (isfinite(controlTemp))
  {
    Serial.print(controlTemp, 2);
    Serial.println(" C");
  }
  else
  {
    Serial.println("---");
  }

  Serial.print("Stratification     : ");
  if (isfinite(stratification))
  {
    Serial.print(stratification, 2);
    Serial.println(" K");
  }
  else
  {
    Serial.println("---");
  }

  Serial.println("================================================");
}

void printSensorSetup()
{
  printSensorScan();

  Serial.println();
  Serial.println("============== SENSOR SETUP ===================");

  printRoleLine("CABIN_LOW ", sensorLow);
  printRoleLine("CABIN_HIGH", sensorHigh);
  printRoleLine("OUTSIDE   ", sensorOutside);
  printRoleLine("SAFETY    ", sensorSafety);

  Serial.println();
  Serial.println("Zuordnung:");
  Serial.println(" assign low INDEX");
  Serial.println(" assign low none");
  Serial.println(" assign high INDEX");
  Serial.println(" assign high none");
  Serial.println(" assign outside INDEX");
  Serial.println(" assign outside none");
  Serial.println(" assign safety INDEX");
  Serial.println(" assign safety none");
  Serial.println();
  Serial.println("Nicht zugewiesen = automatisch deaktiviert.");
  Serial.println("================================================");
}

// =====================================================================
// SENSOR READ
// =====================================================================

void readOneSensor(SensorRole &role)
{
  role.valid = false;

  if (!role.assigned || !role.present)
    return;

  float t = sensors.getTempC(role.address);

  if (t == DEVICE_DISCONNECTED_C || !isfinite(t))
    return;

  t += role.offset;

  role.temperature = t;
  role.valid = true;
  role.lastValidMillis = millis();
}

void calculateControlTemperature()
{
  bool lowOK = sensorLow.assigned && sensorLow.valid;
  bool highOK = sensorHigh.assigned && sensorHigh.valid;

  stratification = NAN;

  if (lowOK && highOK)
  {
    float sumWeights = cfg.weightLow + cfg.weightHigh;

    controlTemp =
      (
        sensorLow.temperature * cfg.weightLow +
        sensorHigh.temperature * cfg.weightHigh
      ) / sumWeights;

    stratification =
      sensorHigh.temperature -
      sensorLow.temperature;
  }
  else if (lowOK)
  {
    controlTemp = sensorLow.temperature;
  }
  else if (highOK)
  {
    controlTemp = sensorHigh.temperature;
  }
  else
  {
    controlTemp = NAN;
  }
}

bool anyCabinRoleAssigned()
{
  return sensorLow.assigned || sensorHigh.assigned;
}

bool anyCabinSensorValid()
{
  return (sensorLow.assigned && sensorLow.valid) ||
         (sensorHigh.assigned && sensorHigh.valid);
}

void updateWarnings()
{
  activeWarning = WARNING_NONE;

  if (sensorOutside.assigned && !sensorOutside.present)
  {
    activeWarning = WARNING_OUTSIDE_MISSING;
    return;
  }

  // Wenn beide Innenraumsensoren zugewiesen sind, aber nur einer funktioniert:
  if (sensorLow.assigned && sensorHigh.assigned)
  {
    bool lowOK = sensorLow.valid;
    bool highOK = sensorHigh.valid;

    if (lowOK != highOK)
    {
      activeWarning = WARNING_ONE_CABIN_SENSOR_MISSING;
      return;
    }
  }
}

void readSensors()
{
  uint32_t now = millis();

  if (now - lastSensorCycleMillis < SENSOR_INTERVAL_MS)
    return;

  float dtMinutes = 0.0f;

  if (lastSensorCycleMillis > 0)
  {
    dtMinutes = (now - lastSensorCycleMillis) / 60000.0f;
  }

  lastSensorCycleMillis = now;

  scanSensors();

  sensors.requestTemperatures();

  previousControlTemp = controlTemp;

  readOneSensor(sensorLow);
  readOneSensor(sensorHigh);
  readOneSensor(sensorOutside);
  readOneSensor(sensorSafety);

  calculateControlTemperature();
  updateWarnings();

  // -----------------------------------------------------------
  // Es muss mindestens LOW oder HIGH zugewiesen sein
  // -----------------------------------------------------------

  if (!anyCabinRoleAssigned())
  {
    // Nach Flash / Erstinbetriebnahme ist das KEIN verriegelter Fehler.
    // Der Controller bleibt sicher im OEM-/Setup-Zustand und das Terminal
    // bleibt voll bedienbar.
    activeWarning = WARNING_SETUP_REQUIRED;

    requestedStage = 0;
    activeStage = 0;
    controllerOutput = 0.0f;
    forceOutputsOff();

    // Nur falls waehrend einer aktiven Eigenregelung die Zuordnung
    // verschwindet, wird daraus ein echter Safety-Lock.
    if (currentMode == MODE_AUTO ||
        currentMode == MODE_BOOST ||
        currentMode == MODE_MANUAL)
    {
      enterFailsafe(FAULT_NO_CABIN_SENSOR);
    }

    return;
  }

  // -----------------------------------------------------------
  // Wenn keiner der zugewiesenen Innenraumsensoren gueltig ist:
  // Timeout abwarten, dann FAILSAFE
  // -----------------------------------------------------------

  if (!anyCabinSensorValid())
  {
    uint32_t newestLastValid = max(sensorLow.lastValidMillis, sensorHigh.lastValidMillis);

    requestedStage = 0;
    activeStage = 0;
    controllerOutput = 0.0f;
    forceOutputsOff();

    // Im OEM-/Setup-Zustand nur Diagnosehinweis.
    // Erst waehrend AUTO/BOOST/MANUAL wird ein Sensorverlust zum FAILSAFE.
    if ((currentMode == MODE_AUTO ||
         currentMode == MODE_BOOST ||
         currentMode == MODE_MANUAL) &&
        now - newestLastValid > cfg.sensorTimeoutSec * 1000UL)
    {
      enterFailsafe(FAULT_CABIN_TIMEOUT);
    }
    else
    {
      activeWarning = WARNING_SETUP_REQUIRED;
    }

    return;
  }
  // -----------------------------------------------------------
  // Plausibilitaet
  // -----------------------------------------------------------

  if (!isfinite(controlTemp) ||
      controlTemp < cfg.minValidCabinTemp ||
      controlTemp > cfg.maxValidCabinTemp)
  {
    enterFailsafe(FAULT_CABIN_IMPLAUSIBLE);
    return;
  }

  // -----------------------------------------------------------
  // Sprung der virtuellen Innenraumtemperatur
  // -----------------------------------------------------------

  if (isfinite(previousControlTemp))
  {
    float jump = fabs(controlTemp - previousControlTemp);

    if (jump > cfg.maxTempJump)
    {
      enterFailsafe(FAULT_TEMP_JUMP);
      return;
    }
  }

  // -----------------------------------------------------------
  // Trend
  // -----------------------------------------------------------

  if (isfinite(previousControlTemp) && dtMinutes > 0.0f)
  {
    float rawTrend =
      (controlTemp - previousControlTemp) / dtMinutes;

    tempTrendPerMinute =
      0.90f * tempTrendPerMinute +
      0.10f * rawTrend;
  }

  // -----------------------------------------------------------
  // Uebertemperatur:
  // Jede vorhandene Innenraumzone wird geprueft
  // -----------------------------------------------------------

  if ((sensorLow.valid  && sensorLow.temperature  >= cfg.emergencyMaxTemp) ||
      (sensorHigh.valid && sensorHigh.temperature >= cfg.emergencyMaxTemp) ||
      controlTemp >= cfg.emergencyMaxTemp)
  {
    enterFailsafe(FAULT_OVERTEMP);
    return;
  }

  // -----------------------------------------------------------
  // SAFETY:
  // Nur wenn explizit zugewiesen.
  // Referenz ist virtuelle Innenraumtemperatur.
  // -----------------------------------------------------------

  if (sensorSafety.assigned)
  {
    if (!sensorSafety.present || !sensorSafety.valid)
    {
      if (now - sensorSafety.lastValidMillis >
          cfg.sensorTimeoutSec * 1000UL)
      {
        enterFailsafe(FAULT_SAFETY_SENSOR);
        return;
      }
    }
    else
    {
      float diff =
        fabs(sensorSafety.temperature - controlTemp);

      if (diff > cfg.safetyMaxDifference)
      {
        if (safetyDifferenceStartMillis == 0)
          safetyDifferenceStartMillis = now;

        if (now - safetyDifferenceStartMillis >=
            cfg.safetyDifferenceDelaySec * 1000UL)
        {
          enterFailsafe(FAULT_SAFETY_DIFFERENCE);
          return;
        }
      }
      else
      {
        safetyDifferenceStartMillis = 0;
      }
    }
  }
  else
  {
    safetyDifferenceStartMillis = 0;
  }
}

// =====================================================================
// FAULT HISTORY / PERSISTENCE
// =====================================================================

void addFaultHistory(FaultCode code)
{
  FaultRecord &f = faultHistory[faultHistoryIndex];

  f.code = code;
  f.timestamp = millis();
  f.lowTemp = sensorLow.temperature;
  f.highTemp = sensorHigh.temperature;
  f.outsideTemp = sensorOutside.temperature;
  f.safetyTemp = sensorSafety.temperature;
  f.controlTemp = controlTemp;
  f.stage = activeStage;

  faultHistoryIndex++;

  if (faultHistoryIndex >= FAULT_HISTORY_SIZE)
    faultHistoryIndex = 0;

  if (faultHistoryCount < FAULT_HISTORY_SIZE)
    faultHistoryCount++;
}

void saveLastFault(FaultCode code)
{
  preferences.begin("zoefault", false);

  preferences.putUInt("code", (uint32_t)code);
  preferences.putFloat("low", sensorLow.temperature);
  preferences.putFloat("high", sensorHigh.temperature);
  preferences.putFloat("out", sensorOutside.temperature);
  preferences.putFloat("safe", sensorSafety.temperature);
  preferences.putFloat("ctrl", controlTemp);
  preferences.putUInt("stage", activeStage);
  preferences.putUInt("uptime", millis() / 1000UL);

  preferences.end();
}

void enterFailsafe(FaultCode code)
{
  if (faultLatched)
    return;

  addFaultHistory(code);
  saveLastFault(code);

  activeFault = code;
  faultLatched = true;
  faultUnread = true;
  currentMode = MODE_FAILSAFE;

  requestedStage = 0;
  activeStage = 0;

  controllerOutput = 0.0f;
  proportionalTerm = 0.0f;
  integralTerm = 0.0f;
  trendCorrection = 0.0f;
  stratificationCorrection = 0.0f;

  forceOutputsOff();

  // Absichtlich nur eine kurze Meldung.
  // Das Terminal und alle Setup-/Diagnosebefehle bleiben bedienbar.
  Serial.println();
  Serial.println("[SAFETY] Eigene Heizfreigabe wurde gesperrt.");
  Serial.println("[SAFETY] Menue bleibt aktiv. Befehl: analyse");
}

// =====================================================================
// SETTINGS LOAD/SAVE
// =====================================================================

void saveRole(Preferences &p, const char* key, SensorRole &role)
{
  if (role.assigned)
    p.putString(key, addressToString(role.address));
  else
    p.remove(key);
}

void loadRole(Preferences &p, const char* key, SensorRole &role)
{
  String text = p.getString(key, "");

  role.assigned = false;

  if (text.length() == 16 &&
      stringToAddress(text, role.address))
  {
    role.assigned = true;
  }
}

void saveSettings()
{
  preferences.begin("zoeheat", false);

  preferences.putFloat("target", cfg.targetTemp);
  preferences.putFloat("deadband", cfg.deadBand);
  preferences.putFloat("restart", cfg.restartDelta);

  preferences.putFloat("wLow", cfg.weightLow);
  preferences.putFloat("wHigh", cfg.weightHigh);

  preferences.putFloat("kp", cfg.kp);
  preferences.putFloat("ki", cfg.ki);
  preferences.putFloat("trend", cfg.trendGain);

  preferences.putFloat("st1", cfg.stage1Threshold);
  preferences.putFloat("st2", cfg.stage2Threshold);
  preferences.putFloat("st3", cfg.stage3Threshold);

  preferences.putUInt("mintime", cfg.minStageTimeSec);
  preferences.putUInt("delay", cfg.stageDelaySec);

  preferences.putUInt("boost", cfg.boostTimeSec);

  preferences.putBool("learning", cfg.learningEnabled);
  preferences.putFloat("learnrate", cfg.learningRate);
  preferences.putUInt("learntime", cfg.learningMinTimeSec);

  preferences.putFloat("gain1", model.gainStage1);
  preferences.putFloat("gain2", model.gainStage2);
  preferences.putFloat("gain3", model.gainStage3);
  preferences.putFloat("coast", model.coastTemp);

  preferences.putFloat("emergency", cfg.emergencyMaxTemp);
  preferences.putUInt("sensTO", cfg.sensorTimeoutSec);
  preferences.putFloat("tempjump", cfg.maxTempJump);

  preferences.putFloat("safeDiff", cfg.safetyMaxDifference);
  preferences.putUInt("safeDelay", cfg.safetyDifferenceDelaySec);

  preferences.end();

  preferences.begin("zoesensors", false);

  saveRole(preferences, "lowAdr", sensorLow);
  saveRole(preferences, "highAdr", sensorHigh);
  saveRole(preferences, "outAdr", sensorOutside);
  saveRole(preferences, "safeAdr", sensorSafety);

  preferences.putFloat("lowOff", sensorLow.offset);
  preferences.putFloat("highOff", sensorHigh.offset);
  preferences.putFloat("outOff", sensorOutside.offset);
  preferences.putFloat("safeOff", sensorSafety.offset);

  preferences.end();

  Serial.println("Alle Einstellungen gespeichert.");
}

void loadSettings()
{
  preferences.begin("zoeheat", true);

  cfg.targetTemp = preferences.getFloat("target", 21.0f);
  cfg.deadBand = preferences.getFloat("deadband", 0.25f);
  cfg.restartDelta = preferences.getFloat("restart", 0.50f);

  cfg.weightLow = preferences.getFloat("wLow", 40.0f);
  cfg.weightHigh = preferences.getFloat("wHigh", 60.0f);

  cfg.kp = preferences.getFloat("kp", 22.0f);
  cfg.ki = preferences.getFloat("ki", 0.12f);
  cfg.trendGain = preferences.getFloat("trend", 25.0f);

  cfg.stage1Threshold = preferences.getFloat("st1", 20.0f);
  cfg.stage2Threshold = preferences.getFloat("st2", 50.0f);
  cfg.stage3Threshold = preferences.getFloat("st3", 80.0f);

  cfg.minStageTimeSec = preferences.getUInt("mintime", 60);
  cfg.stageDelaySec = preferences.getUInt("delay", 30);

  cfg.boostTimeSec = preferences.getUInt("boost", 300);

  cfg.learningEnabled = preferences.getBool("learning", true);
  cfg.learningRate = preferences.getFloat("learnrate", 0.05f);
  cfg.learningMinTimeSec = preferences.getUInt("learntime", 120);

  model.gainStage1 = preferences.getFloat("gain1", 0.08f);
  model.gainStage2 = preferences.getFloat("gain2", 0.18f);
  model.gainStage3 = preferences.getFloat("gain3", 0.32f);
  model.coastTemp = preferences.getFloat("coast", 0.30f);

  cfg.emergencyMaxTemp = preferences.getFloat("emergency", 35.0f);
  cfg.sensorTimeoutSec = preferences.getUInt("sensTO", 10);
  cfg.maxTempJump = preferences.getFloat("tempjump", 4.0f);

  cfg.safetyMaxDifference = preferences.getFloat("safeDiff", 3.0f);
  cfg.safetyDifferenceDelaySec = preferences.getUInt("safeDelay", 30);

  preferences.end();

  preferences.begin("zoesensors", true);

  loadRole(preferences, "lowAdr", sensorLow);
  loadRole(preferences, "highAdr", sensorHigh);
  loadRole(preferences, "outAdr", sensorOutside);
  loadRole(preferences, "safeAdr", sensorSafety);

  sensorLow.offset = preferences.getFloat("lowOff", 0.0f);
  sensorHigh.offset = preferences.getFloat("highOff", 0.0f);
  sensorOutside.offset = preferences.getFloat("outOff", 0.0f);
  sensorSafety.offset = preferences.getFloat("safeOff", 0.0f);

  preferences.end();
}

void loadDefaults()
{
  cfg = Settings();
  model = ThermalModel();

  integralTerm = 0.0f;
  targetHoldActive = false;

  Serial.println("Regler-/System-Defaultwerte geladen.");
  Serial.println("Sensor-Zuweisungen bleiben erhalten.");
}

// =====================================================================
// CONTROL
// =====================================================================

void calculateController()
{
  if (!isfinite(controlTemp))
  {
    requestedStage = 0;
    controllerOutput = 0.0f;
    return;
  }

  temperatureError =
    cfg.targetTemp - controlTemp;

  // Zielbereich erreicht
  if (controlTemp >= cfg.targetTemp - cfg.deadBand)
  {
    targetHoldActive = true;
  }

  // Hysterese / Wiedereinschaltung
  if (targetHoldActive &&
      controlTemp > cfg.targetTemp - cfg.restartDelta)
  {
    requestedStage = 0;
    controllerOutput = 0.0f;
    integralTerm *= 0.90f;
    return;
  }

  if (controlTemp <= cfg.targetTemp - cfg.restartDelta)
  {
    targetHoldActive = false;
  }

  proportionalTerm =
    temperatureError * cfg.kp;

  integralTerm +=
    temperatureError * cfg.ki;

  integralTerm =
    constrain(
      integralTerm,
      0.0f,
      cfg.integralLimit
    );

  trendCorrection = 0.0f;

  if (tempTrendPerMinute > 0.0f)
  {
    trendCorrection =
      tempTrendPerMinute * cfg.trendGain;
  }

  predictedTemp =
    controlTemp +
    model.coastTemp +
    tempTrendPerMinute;

  if (predictedTemp >= cfg.targetTemp)
  {
    trendCorrection += 15.0f;
  }

  // -----------------------------------------------------------
  // Schichtungs-/HIGH-Zone-Korrektur
  // Wenn oben bereits waermer als Soll, Leistung reduzieren.
  // LOW/HIGH-Differenz allein ist KEIN Fehler.
  // -----------------------------------------------------------

  stratificationCorrection = 0.0f;

  if (sensorHigh.valid)
  {
    float highOver =
      sensorHigh.temperature -
      (cfg.targetTemp + cfg.highZoneLimitOffset);

    if (highOver > 0.0f)
    {
      stratificationCorrection =
        min(
          highOver * cfg.highZoneReduceGain,
          cfg.highZoneMaxReduction
        );
    }
  }

  controllerOutput =
    proportionalTerm +
    integralTerm -
    trendCorrection -
    stratificationCorrection;

  if (!isfinite(controllerOutput))
  {
    enterFailsafe(FAULT_CONTROLLER_INVALID);
    return;
  }

  controllerOutput =
    constrain(
      controllerOutput,
      0.0f,
      100.0f
    );

  if (controllerOutput < cfg.stage1Threshold)
    requestedStage = 0;
  else if (controllerOutput < cfg.stage2Threshold)
    requestedStage = 1;
  else if (controllerOutput < cfg.stage3Threshold)
    requestedStage = 2;
  else
    requestedStage = 3;
}

void processStageManager()
{
  if (faultLatched)
  {
    activeStage = 0;
    return;
  }

  if (requestedStage > 3 || activeStage > 3)
  {
    enterFailsafe(FAULT_OUTPUT_INVALID);
    return;
  }

  if (requestedStage == activeStage)
    return;

  uint32_t elapsedSec =
    (millis() - lastStageChangeMillis) / 1000UL;

  uint32_t requiredDelay =
    max(cfg.minStageTimeSec, cfg.stageDelaySec);

  if (elapsedSec < requiredDelay)
    return;

  // Immer nur eine Stufe je Wechsel
  if (requestedStage > activeStage)
    activeStage++;
  else
    activeStage--;

  lastStageChangeMillis = millis();

  Serial.print("Heizstufe -> ");
  Serial.println(activeStage);
}

// =====================================================================
// LEARNING
// =====================================================================

void stopLearningMeasurement()
{
  learningStage = 0;
  learningStartTemp = NAN;
  learningStartOutsideTemp = NAN;
  learningStartMillis = 0;
}

void startLearningMeasurement(uint8_t stage)
{
  if (!isfinite(controlTemp))
    return;

  learningStage = stage;
  learningStartTemp = controlTemp;

  learningStartOutsideTemp =
    sensorOutside.valid
      ? sensorOutside.temperature
      : NAN;

  learningStartMillis = millis();
}

void updateLearning()
{
  if (!cfg.learningEnabled) return;
  if (faultLatched) return;
  if (currentMode != MODE_AUTO) return;
  if (!isfinite(controlTemp)) return;

  if (activeStage == 0)
  {
    stopLearningMeasurement();
    return;
  }

  if (learningStage != activeStage)
  {
    startLearningMeasurement(activeStage);
    return;
  }

  uint32_t elapsedSec =
    (millis() - learningStartMillis) / 1000UL;

  if (elapsedSec < cfg.learningMinTimeSec)
    return;

  float elapsedMin =
    elapsedSec / 60.0f;

  float measuredGain =
    (controlTemp - learningStartTemp) / elapsedMin;

  if (!isfinite(measuredGain) ||
      fabs(measuredGain) > cfg.maxLearningTrend)
  {
    activeWarning = WARNING_LEARNING_IMPLAUSIBLE;
    startLearningMeasurement(activeStage);
    return;
  }

  float lr = cfg.learningRate;

  switch (activeStage)
  {
    case 1:
      model.gainStage1 =
        model.gainStage1 * (1.0f - lr) +
        measuredGain * lr;
      model.samplesStage1++;
      break;

    case 2:
      model.gainStage2 =
        model.gainStage2 * (1.0f - lr) +
        measuredGain * lr;
      model.samplesStage2++;
      break;

    case 3:
      model.gainStage3 =
        model.gainStage3 * (1.0f - lr) +
        measuredGain * lr;
      model.samplesStage3++;
      break;
  }

  startLearningMeasurement(activeStage);
}

// =====================================================================
// HEATING RESPONSE
// =====================================================================

void monitorHeatingResponse()
{
  if (activeStage == 0 || !isfinite(controlTemp))
  {
    heatRiseStartMillis = 0;
    heatRiseStartTemp = NAN;
    return;
  }

  if (heatRiseStartMillis == 0)
  {
    heatRiseStartMillis = millis();
    heatRiseStartTemp = controlTemp;
    return;
  }

  uint32_t elapsedSec =
    (millis() - heatRiseStartMillis) / 1000UL;

  if (elapsedSec >= cfg.noHeatRiseWarningSec)
  {
    float rise =
      controlTemp - heatRiseStartTemp;

    if (rise < 0.2f)
      activeWarning = WARNING_TEMP_RISE_LOW;

    heatRiseStartMillis = millis();
    heatRiseStartTemp = controlTemp;
  }
}


// =====================================================================
// CONTROL-START FREIGABE
// =====================================================================

bool controlStartReady()
{
  scanSensors();

  // Mindestens LOW oder HIGH muss zugewiesen sein.
  if (!anyCabinRoleAssigned())
  {
    Serial.println();
    Serial.println("AUTO/BOOST/MANUAL nicht gestartet:");
    Serial.println("Kein Innenraumsensor zugewiesen.");
    Serial.println("Bitte 'sensor scan' und danach 'sensor setup' verwenden.");
    return false;
  }

  // Mindestens einer der zugewiesenen Innenraumsensoren muss aktuell
  // vorhanden und gueltig sein.
  if (!anyCabinSensorValid())
  {
    Serial.println();
    Serial.println("AUTO/BOOST/MANUAL nicht gestartet:");
    Serial.println("Kein gueltiger Innenraumsensor verfuegbar.");
    Serial.println("Bitte 'sensor status' oder 'analyse' verwenden.");
    return false;
  }

  return true;
}

// =====================================================================
// MODES
// =====================================================================

void setMode(ControllerMode newMode)
{
  // Das Terminal bleibt auch bei FAILSAFE voll bedienbar.
  // Gesperrt wird nur die eigene Heizfreigabe.
  if (faultLatched &&
      newMode != MODE_FAILSAFE &&
      newMode != MODE_OEM)
  {
    Serial.println("Heizmodus gesperrt: Safety-Lock aktiv.");
    Serial.println("Befehl 'analyse' zeigt die Ursache.");
    return;
  }

  // Vor Start einer eigenen Regelung erst Sensorik pruefen.
  if ((newMode == MODE_AUTO ||
       newMode == MODE_BOOST ||
       newMode == MODE_MANUAL) &&
      !controlStartReady())
  {
    currentMode = MODE_OEM;
    requestedStage = 0;
    activeStage = 0;
    controllerOutput = 0.0f;
    forceOutputsOff();
    activeWarning = WARNING_SETUP_REQUIRED;
    return;
  }

  currentMode = newMode;
  integralTerm = 0.0f;
  targetHoldActive = false;

  if (currentMode == MODE_BOOST)
  {
    boostStartMillis = millis();
  }

  Serial.print("MODE -> ");
  Serial.println(modeName());
}

void processMode()
{
  if (faultLatched)
  {
    currentMode = MODE_FAILSAFE;
    requestedStage = 0;
    controllerOutput = 0.0f;
    return;
  }

  switch (currentMode)
  {
    case MODE_OEM:
      requestedStage = 0;
      controllerOutput = 0.0f;
      integralTerm = 0.0f;
      break;

    case MODE_AUTO:
      calculateController();
      break;

    case MODE_BOOST:
      requestedStage = 3;
      controllerOutput = 100.0f;

      if ((millis() - boostStartMillis) / 1000UL >= cfg.boostTimeSec)
      {
        Serial.println("BOOST beendet.");
        setMode(MODE_AUTO);
      }
      break;

    case MODE_MANUAL:
      requestedStage = manualStage;
      controllerOutput = manualStage * 33.33f;
      break;

    case MODE_FAILSAFE:
      requestedStage = 0;
      activeStage = 0;
      controllerOutput = 0.0f;
      forceOutputsOff();
      break;
  }
}

// =====================================================================
// SAFETY SUPERVISOR
// =====================================================================

void safetySupervisor()
{
  if (faultLatched)
  {
    requestedStage = 0;
    activeStage = 0;
    forceOutputsOff();
    return;
  }

  if (!configValid())
  {
    enterFailsafe(FAULT_CONFIG_INVALID);
    return;
  }

  if (requestedStage > 3 || activeStage > 3)
  {
    enterFailsafe(FAULT_OUTPUT_INVALID);
    return;
  }

  if (!isfinite(controllerOutput) ||
      !isfinite(integralTerm))
  {
    enterFailsafe(FAULT_CONTROLLER_INVALID);
    return;
  }
}

void checkControlCycleTimeout()
{
  if (lastControlCycleMillis == 0)
    return;

  uint32_t timeoutMs =
    CONTROL_INTERVAL_MS * 4;

  if (millis() - lastControlCycleMillis > timeoutMs)
  {
    enterFailsafe(FAULT_CONTROL_TIMEOUT);
  }
}

// =====================================================================
// BUTTON
// =====================================================================

void shortButtonPress()
{
  if (faultLatched)
  {
    Serial.println("Taster ignoriert: FAILSAFE aktiv.");
    return;
  }

  switch (currentMode)
  {
    case MODE_OEM:
      setMode(MODE_AUTO);
      break;

    case MODE_AUTO:
      setMode(MODE_BOOST);
      break;

    case MODE_BOOST:
    case MODE_MANUAL:
      setMode(MODE_OEM);
      break;

    case MODE_FAILSAFE:
      break;
  }
}

void longButtonPress()
{
  requestedStage = 0;
  activeStage = 0;
  forceOutputsOff();

  if (faultLatched)
  {
    currentMode = MODE_FAILSAFE;
    Serial.println("Longpress: FAILSAFE bleibt aktiv.");
  }
  else
  {
    setMode(MODE_OEM);
    Serial.println("Longpress -> OEM");
  }
}

void processButton()
{
  bool reading =
    digitalRead(PIN_BUTTON);

  if (reading != lastButtonReading)
  {
    buttonChangeMillis = millis();
    lastButtonReading = reading;
  }

  if (millis() - buttonChangeMillis > BUTTON_DEBOUNCE_MS)
  {
    if (reading != stableButtonState)
    {
      stableButtonState = reading;

      if (stableButtonState == LOW)
      {
        buttonPressMillis = millis();
        longPressHandled = false;
      }
      else
      {
        if (!longPressHandled)
          shortButtonPress();
      }
    }
  }

  if (stableButtonState == LOW &&
      !longPressHandled &&
      millis() - buttonPressMillis >= BUTTON_LONGPRESS_MS)
  {
    longPressHandled = true;
    longButtonPress();
  }
}

// =====================================================================
// RGB STATUS
// =====================================================================

void updateRGB()
{
  static bool blinkState = false;
  static uint32_t blinkMillis = 0;

  if (faultLatched)
  {
    if (millis() - blinkMillis >= 300)
    {
      blinkMillis = millis();
      blinkState = !blinkState;
    }

    if (blinkState) rgbRed();
    else rgbOff();

    return;
  }

  switch (currentMode)
  {
    case MODE_OEM:
      rgbBlue();
      break;

    case MODE_AUTO:
      if (activeStage == 0) rgbGreen();
      else if (activeStage == 1) rgbCyan();
      else if (activeStage == 2) rgbYellow();
      else rgbRed();
      break;

    case MODE_BOOST:
      rgbRed();
      break;

    case MODE_MANUAL:
      rgbMagenta();
      break;

    case MODE_FAILSAFE:
      rgbRed();
      break;
  }
}

// =====================================================================
// DIAGNOSTICS
// =====================================================================

void printStatus()
{
  Serial.println();
  Serial.println("============== ZOE HEATER STATUS ==============");

  Serial.print("Firmware           : ");
  Serial.println(FW_VERSION);

  Serial.print("Mode               : ");
  Serial.println(modeName());

  Serial.print("Safety-Lock        : ");
  Serial.println(faultLatched ? "AKTIV" : "NEIN");

  Serial.print("Neue Diagnose      : ");
  Serial.println(faultUnread ? "JA" : "NEIN");

  Serial.println();

  Serial.print("LOW                : ");
  if (sensorLow.valid)
  {
    Serial.print(sensorLow.temperature, 2);
    Serial.println(" C");
  }
  else Serial.println("---");

  Serial.print("HIGH               : ");
  if (sensorHigh.valid)
  {
    Serial.print(sensorHigh.temperature, 2);
    Serial.println(" C");
  }
  else Serial.println("---");

  Serial.print("OUTSIDE            : ");
  if (sensorOutside.valid)
  {
    Serial.print(sensorOutside.temperature, 2);
    Serial.println(" C");
  }
  else Serial.println("---");

  Serial.print("SAFETY             : ");
  if (sensorSafety.valid)
  {
    Serial.print(sensorSafety.temperature, 2);
    Serial.println(" C");
  }
  else Serial.println("---");

  Serial.println();

  Serial.print("Virtual Cabin      : ");
  if (isfinite(controlTemp))
  {
    Serial.print(controlTemp, 2);
    Serial.println(" C");
  }
  else Serial.println("---");

  Serial.print("Target             : ");
  Serial.print(cfg.targetTemp, 2);
  Serial.println(" C");

  Serial.print("Stratification     : ");
  if (isfinite(stratification))  {
    Serial.print(stratification, 2);
    Serial.println(" K");
  }
  else Serial.println("---");

  Serial.print("Trend              : ");
  Serial.print(tempTrendPerMinute, 3);
  Serial.println(" K/min");

  Serial.println();

  Serial.print("P-Anteil           : ");
  Serial.println(proportionalTerm, 2);

  Serial.print("I-Anteil           : ");
  Serial.println(integralTerm, 2);

  Serial.print("Trendkorrektur     : ");
  Serial.println(trendCorrection, 2);

  Serial.print("Schichtungskorr.   : ");
  Serial.println(stratificationCorrection, 2);

  Serial.print("Reglerausgang      : ");
  Serial.print(controllerOutput, 1);
  Serial.println(" %");

  Serial.print("Requested Stage    : ");
  Serial.println(requestedStage);

  Serial.print("Active Stage       : ");
  Serial.println(activeStage);

  Serial.println();

  Serial.print("Warnung            : ");
  Serial.println(warningName(activeWarning));

  Serial.print("Fehler             : ");
  Serial.println(faultName(activeFault));

  Serial.println("================================================");
}

void printDiagnostics()
{
  Serial.println();
  Serial.println("============== SYSTEM DIAGNOSTICS =============");

  Serial.print("Konfiguration      : ");
  Serial.println(configValid() ? "OK" : "FEHLER");

  Serial.print("CABIN_LOW          : ");
  if (!sensorLow.assigned) Serial.println("DEAKTIVIERT");
  else if (!sensorLow.present) Serial.println("NICHT GEFUNDEN");
  else Serial.println(sensorLow.valid ? "OK" : "FEHLER");

  Serial.print("CABIN_HIGH         : ");
  if (!sensorHigh.assigned) Serial.println("DEAKTIVIERT");
  else if (!sensorHigh.present) Serial.println("NICHT GEFUNDEN");
  else Serial.println(sensorHigh.valid ? "OK" : "FEHLER");

  Serial.print("OUTSIDE            : ");
  if (!sensorOutside.assigned) Serial.println("DEAKTIVIERT");
  else if (!sensorOutside.present) Serial.println("NICHT GEFUNDEN / OPTIONAL");
  else Serial.println(sensorOutside.valid ? "OK" : "FEHLER");

  Serial.print("SAFETY             : ");
  if (!sensorSafety.assigned) Serial.println("DEAKTIVIERT");
  else if (!sensorSafety.present) Serial.println("NICHT GEFUNDEN");
  else Serial.println(sensorSafety.valid ? "OK" : "FEHLER");

  Serial.println();

  Serial.print("Virtual Cabin      : ");
  if (isfinite(controlTemp))
  {
    Serial.print(controlTemp, 2);
    Serial.println(" C");
  }
  else Serial.println("INVALID");

  Serial.print("Regler             : ");
  Serial.println(isfinite(controllerOutput) ? "OK" : "FEHLER");

  Serial.print("H1 Logikausgang    : ");
  Serial.println(digitalRead(HEATER1_OUT) ? "ON" : "OFF");

  Serial.print("H2 Logikausgang    : ");
  Serial.println(digitalRead(HEATER2_OUT) ? "ON" : "OFF");

  Serial.print("H3 Logikausgang    : ");
  Serial.println(digitalRead(HEATER3_OUT) ? "ON" : "OFF");

  Serial.println();

  Serial.print("Warnung            : ");
  Serial.println(warningName(activeWarning));

  Serial.print("Aktueller Fehler   : ");
  Serial.println(faultName(activeFault));

  Serial.print("Failsafe           : ");
  Serial.println(faultLatched ? "AKTIV" : "NEIN");

  Serial.print("GESAMTSTATUS       : ");

  if (faultLatched || !configValid())
    Serial.println("FEHLER");
  else if (activeWarning != WARNING_NONE)
    Serial.println("WARNUNG");
  else
    Serial.println("OK");

  Serial.println("================================================");
}

void printThermal()
{
  Serial.println();
  Serial.println("============== THERMAL MODEL ==================");

  Serial.print("Stage 1 Gain       : ");
  Serial.print(model.gainStage1, 4);
  Serial.print(" K/min  Samples=");
  Serial.println(model.samplesStage1);

  Serial.print("Stage 2 Gain       : ");
  Serial.print(model.gainStage2, 4);
  Serial.print(" K/min  Samples=");
  Serial.println(model.samplesStage2);

  Serial.print("Stage 3 Gain       : ");
  Serial.print(model.gainStage3, 4);
  Serial.print(" K/min  Samples=");
  Serial.println(model.samplesStage3);

  Serial.print("Nachlauf           : ");
  Serial.print(model.coastTemp, 3);
  Serial.println(" K");

  Serial.print("Learning           : ");
  Serial.println(cfg.learningEnabled ? "ON" : "OFF");

  Serial.print("Aussentemp aktuell : ");
  if (sensorOutside.valid)
  {
    Serial.print(sensorOutside.temperature, 2);
    Serial.println(" C");
  }
  else Serial.println("---");

  Serial.println("================================================");
}

void printFault()
{
  Serial.println();
  Serial.println("============== FEHLERANALYSE ==================");

  Serial.print("Systemstatus       : ");
  Serial.println(faultLatched ? "SAFETY-LOCK" : "OK / SETUP");

  Serial.print("Fehler             : ");
  Serial.println(faultName(activeFault));

  Serial.print("Neue Meldung       : ");
  Serial.println(faultUnread ? "JA" : "NEIN");

  Serial.print("Heizfreigabe       : ");
  Serial.println(faultLatched ? "GESPERRT" : "FREI");

  Serial.println();
  Serial.println("Das Terminal/Setup bleibt immer bedienbar.");
  Serial.println("Nur AUTO/BOOST/MANUAL wird bei Safety-Lock gesperrt.");
  Serial.println();

  printSensorStatus();

  // Einmaliges Auslesen / Quittieren der Diagnoseanzeige.
  // Der Safety-Lock selbst wird dadurch NICHT geloescht.
  faultUnread = false;

  Serial.println("Diagnose als gelesen markiert.");
  Serial.println("Zum Entsperren nach Fehlerbehebung: fault reset");
  Serial.println("================================================");
}

void printAnalysis()
{
  Serial.println();
  Serial.println("################################################");
  Serial.println("                    ANALYSE");
  Serial.println("################################################");

  printFault();
  printDiagnostics();

  Serial.println();
  Serial.println("Weitere Befehle:");
  Serial.println(" analyse history");
  Serial.println(" analyse last");
  Serial.println(" sensor setup");
  Serial.println(" fault reset");
  Serial.println("################################################");
}

void printFaultHistory()
{
  Serial.println();
  Serial.println("============== FEHLERHISTORIE =================");

  if (faultHistoryCount == 0)
  {
    Serial.println("Keine Fehler gespeichert.");
    Serial.println("================================================");
    return;
  }

  for (uint8_t n = 0; n < faultHistoryCount; n++)
  {
    int index =
      faultHistoryIndex - 1 - n;

    while (index < 0)
      index += FAULT_HISTORY_SIZE;

    FaultRecord &f =
      faultHistory[index];

    Serial.print("#");
    Serial.print(n + 1);

    Serial.print(" ");
    Serial.print(faultName(f.code));

    Serial.print(" | t=");
    Serial.print(f.timestamp / 1000UL);
    Serial.print("s");

    Serial.print(" | LOW=");
    Serial.print(f.lowTemp);

    Serial.print(" | HIGH=");
    Serial.print(f.highTemp);

    Serial.print(" | CTRL=");
    Serial.print(f.controlTemp);

    Serial.print(" | OUT=");
    Serial.print(f.outsideTemp);

    Serial.print(" | SAFE=");
    Serial.print(f.safetyTemp);

    Serial.print(" | STAGE=");
    Serial.println(f.stage);
  }

  Serial.println("================================================");
}

void printLastStoredFault()
{
  preferences.begin("zoefault", true);

  uint32_t code =
    preferences.getUInt("code", 0);

  if (code == 0)
  {
    preferences.end();
    Serial.println("Kein persistenter Fehler gespeichert.");
    return;
  }

  float low =
    preferences.getFloat("low", NAN);

  float high =
    preferences.getFloat("high", NAN);

  float out =
    preferences.getFloat("out", NAN);

  float safe =
    preferences.getFloat("safe", NAN);

  float ctrl =
    preferences.getFloat("ctrl", NAN);

  uint32_t stage =
    preferences.getUInt("stage", 0);

  uint32_t uptime =
    preferences.getUInt("uptime", 0);

  preferences.end();

  Serial.println();
  Serial.println("========= LETZTER GESPEICHERTER FEHLER =========");

  Serial.print("Fehler             : ");
  Serial.println(faultName((FaultCode)code));

  Serial.print("LOW                : ");
  Serial.println(low);

  Serial.print("HIGH               : ");
  Serial.println(high);

  Serial.print("CONTROL            : ");
  Serial.println(ctrl);

  Serial.print("OUTSIDE            : ");
  Serial.println(out);

  Serial.print("SAFETY             : ");
  Serial.println(safe);

  Serial.print("Stage              : ");
  Serial.println(stage);

  Serial.print("Uptime damals      : ");
  Serial.print(uptime);
  Serial.println(" s");

  Serial.println("================================================");
}

// =====================================================================
// FAULT RESET
// =====================================================================

bool currentSafetyConditionsOkay()
{
  scanSensors();

  if (!configValid())
    return false;

  if (!anyCabinRoleAssigned())
    return false;

  // Mindestens einer muss aktuell vorhanden sein.
  bool lowPresent =
    sensorLow.assigned && sensorLow.present;

  bool highPresent =
    sensorHigh.assigned && sensorHigh.present;

  if (!lowPresent && !highPresent)
    return false;

  // Wenn SAFETY zugewiesen ist, muss er vorhanden sein.
  if (sensorSafety.assigned && !sensorSafety.present)
    return false;

  return true;
}

void resetFault()
{
  if (!faultLatched)
  {
    Serial.println("Kein verriegelter Fehler vorhanden.");
    return;
  }

  if (!currentSafetyConditionsOkay())
  {
    Serial.println("FAULT RESET NICHT MOEGLICH.");
    Serial.println("Fehlerursache / Sensorzuordnung noch ungueltig.");
    return;
  }

  forceOutputsOff();

  activeFault = FAULT_NONE;
  faultLatched = false;
  faultUnread = false;

  requestedStage = 0;
  activeStage = 0;

  integralTerm = 0.0f;
  targetHoldActive = false;

  currentMode = MODE_OEM;

  Serial.println("Fehler geloescht.");
  Serial.println("System bleibt im OEM-Modus.");
}

// =====================================================================
// MENU
// =====================================================================

void processCommand(String cmd);

void printMainMenu()
{
  currentMenu = MENU_MAIN;

  Serial.println();
  Serial.println("================================================");
  Serial.println("       ZOE CLIMATE CONTROLLER V4.6");
  Serial.println("================================================");
  Serial.println(" 1  Status");
  Serial.println(" 2  Sensoren");
  Serial.println(" 3  Betriebsart");
  Serial.println(" 4  Temperatur / Komfort");
  Serial.println(" 5  Regler");
  Serial.println(" 6  Heizstufen");
  Serial.println(" 7  Lernsystem");
  Serial.println(" 8  Analyse / Fehler");
  Serial.println(" 9  System");
  Serial.println();
  Serial.println(" 0  Schnellstatus");
  Serial.println(" h  Expertenbefehle anzeigen");
  Serial.println("================================================");
  Serial.print("> ");
}

void printStatusMenu()
{
  currentMenu = MENU_STATUS;

  Serial.println();
  Serial.println("--------------- STATUS ----------------");
  Serial.println(" 1  Gesamtstatus");
  Serial.println(" 2  Sensordaten");
  Serial.println(" 3  Thermisches Modell");
  Serial.println(" 4  Systemdiagnose");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printSensorMenu()
{
  currentMenu = MENU_SENSORS;

  Serial.println();
  Serial.println("-------------- SENSOREN ---------------");
  Serial.println(" 1  Sensorstatus");
  Serial.println(" 2  Sensoren suchen");
  Serial.println(" 3  Sensor-Zuordnung anzeigen");
  Serial.println(" 4  CABIN_LOW zuweisen");
  Serial.println(" 5  CABIN_HIGH zuweisen");
  Serial.println(" 6  OUTSIDE zuweisen");
  Serial.println(" 7  SAFETY zuweisen");
  Serial.println(" 8  Sensor-Zuweisung loeschen");
  Serial.println(" 9  Gewichtung LOW/HIGH");
  Serial.println("10  Sensor-Offsets");
  Serial.println("11  Sensor-Einstellungen speichern");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printModeMenu()
{
  currentMenu = MENU_MODE;

  Serial.println();
  Serial.println("------------- BETRIEBSART -------------");
  Serial.print("Aktuell: ");
  Serial.println(modeName());
  Serial.println();
  Serial.println(" 1  OEM");
  Serial.println(" 2  AUTO");
  Serial.println(" 3  BOOST");
  Serial.println(" 4  MANUAL");
  Serial.println(" 5  Manuelle Stufe 0");
  Serial.println(" 6  Manuelle Stufe 1");
  Serial.println(" 7  Manuelle Stufe 2");
  Serial.println(" 8  Manuelle Stufe 3");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printTemperatureMenu()
{
  currentMenu = MENU_TEMPERATURE;

  Serial.println();
  Serial.println("--------- TEMPERATUR / KOMFORT --------");
  Serial.print("Solltemperatur : ");
  Serial.print(cfg.targetTemp, 2);
  Serial.println(" C");
  Serial.print("Deadband       : ");
  Serial.print(cfg.deadBand, 2);
  Serial.println(" K");
  Serial.print("Restart Delta  : ");
  Serial.print(cfg.restartDelta, 2);
  Serial.println(" K");
  Serial.print("Weight LOW     : ");
  Serial.print(cfg.weightLow, 1);
  Serial.println(" %");
  Serial.print("Weight HIGH    : ");
  Serial.print(cfg.weightHigh, 1);
  Serial.println(" %");
  Serial.println();
  Serial.println(" 1  Solltemperatur aendern");
  Serial.println(" 2  Deadband aendern");
  Serial.println(" 3  Restart Delta aendern");
  Serial.println(" 4  Weight LOW aendern");
  Serial.println(" 5  Weight HIGH aendern");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printControllerMenu()
{
  currentMenu = MENU_CONTROLLER;

  Serial.println();
  Serial.println("--------------- REGLER ----------------");
  Serial.print("Kp         : "); Serial.println(cfg.kp);
  Serial.print("Ki         : "); Serial.println(cfg.ki);
  Serial.print("Trend Gain : "); Serial.println(cfg.trendGain);
  Serial.println();
  Serial.println(" 1  Kp aendern");
  Serial.println(" 2  Ki aendern");
  Serial.println(" 3  Trend Gain aendern");
  Serial.println(" 4  Reglerstatus anzeigen");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printStageMenu()
{
  currentMenu = MENU_STAGES;

  Serial.println();
  Serial.println("------------- HEIZSTUFEN --------------");
  Serial.print("Threshold 1 : "); Serial.println(cfg.stage1Threshold);
  Serial.print("Threshold 2 : "); Serial.println(cfg.stage2Threshold);
  Serial.print("Threshold 3 : "); Serial.println(cfg.stage3Threshold);
  Serial.print("Min. Zeit   : "); Serial.print(cfg.minStageTimeSec); Serial.println(" s");
  Serial.print("Stage Delay : "); Serial.print(cfg.stageDelaySec); Serial.println(" s");
  Serial.print("Boost Zeit  : "); Serial.print(cfg.boostTimeSec); Serial.println(" s");
  Serial.println();
  Serial.println(" 1  Threshold 1 aendern");
  Serial.println(" 2  Threshold 2 aendern");
  Serial.println(" 3  Threshold 3 aendern");
  Serial.println(" 4  Mindestzeit aendern");
  Serial.println(" 5  Stage Delay aendern");
  Serial.println(" 6  Boost-Zeit aendern");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printLearningMenu()
{
  currentMenu = MENU_LEARNING;

  Serial.println();
  Serial.println("------------- LERNSYSTEM --------------");
  Serial.print("Learning : ");
  Serial.println(cfg.learningEnabled ? "EIN" : "AUS");
  Serial.print("Rate     : ");
  Serial.println(cfg.learningRate);
  Serial.print("Messzeit : ");
  Serial.print(cfg.learningMinTimeSec);
  Serial.println(" s");
  Serial.println();
  Serial.println(" 1  Learning EIN");
  Serial.println(" 2  Learning AUS");
  Serial.println(" 3  Lernmodell anzeigen");
  Serial.println(" 4  Lernmodell zuruecksetzen");
  Serial.println(" 5  Learning Rate aendern");
  Serial.println(" 6  Learning Messzeit aendern");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printAnalysisMenu()
{
  currentMenu = MENU_ANALYSIS;

  Serial.println();
  Serial.println("----------- ANALYSE / FEHLER -----------");
  Serial.print("Safety-Lock : ");
  Serial.println(faultLatched ? "AKTIV" : "NEIN");
  Serial.print("Neue Meldung: ");
  Serial.println(faultUnread ? "JA" : "NEIN");
  Serial.println();
  Serial.println(" 1  Aktuelle Fehleranalyse");
  Serial.println(" 2  Systemdiagnose");
  Serial.println(" 3  Fehlerhistorie");
  Serial.println(" 4  Letzten gespeicherten Fehler");
  Serial.println(" 5  Safety-Lock zuruecksetzen");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printSystemMenu()
{
  currentMenu = MENU_SYSTEM;

  Serial.println();
  Serial.println("--------------- SYSTEM ----------------");
  Serial.println(" 1  WLAN / Web");
  Serial.println(" 2  Sicherheit / Login");
  Serial.println(" 3  Alles speichern");
  Serial.println(" 4  Defaultwerte laden");
  Serial.println(" 5  Neustart");
  Serial.println(" 6  Firmware / Status");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printWiFiMenu()
{
  currentMenu = MENU_WIFI;

  Serial.println();
  Serial.println("------------- WLAN / WEB --------------");
  Serial.print("WLAN    : ");
  Serial.println(wifiEnabled ? "EIN" : "AUS");
  Serial.print("Startup : ");
  Serial.println(wifiStartupEnabled ? "EIN" : "AUS");
  Serial.println("SSID    : ZHC");
  Serial.println();
  Serial.println(" 1  WLAN einschalten");
  Serial.println(" 2  WLAN ausschalten");
  Serial.println(" 3  WLAN-Status");
  Serial.println(" 4  WLAN beim Start EIN");
  Serial.println(" 5  WLAN beim Start AUS");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printSecurityMenu()
{
  currentMenu = MENU_SECURITY;

  Serial.println();
  Serial.println("---------- SICHERHEIT / LOGIN ----------");
  Serial.println("Benutzer : admin");
  Serial.print("Web Login: ");
  Serial.println(passwordHash.length() == 64 ? "EINGERICHTET" : "NICHT EINGERICHTET");
  Serial.print("Erstlogin: ");
  Serial.println(forcePasswordChange ? "JA" : "NEIN");
  Serial.print("Session  : ");
  Serial.println(sessionToken.length() > 0 ? "AKTIV" : "KEINE");
  Serial.println();
  Serial.println(" 1  Login-Status");
  Serial.println(" 2  Initialpasswort anzeigen");
  Serial.println(" 3  Web-Session abmelden");
  Serial.println(" 4  Web-Zugang lokal zuruecksetzen");
  Serial.println(" 5  Recovery-Hinweis");
  Serial.println();
  Serial.println(" b  Zurueck");
  Serial.println(" m  Hauptmenue");
  Serial.println("---------------------------------------");
  Serial.print("> ");
}

void printExpertCommands()
{
  Serial.println();
  Serial.println("============= EXPERTENBEFEHLE =============");
  Serial.println("Alle bisherigen Textbefehle funktionieren weiter.");
  Serial.println("Beispiele:");
  Serial.println(" status");
  Serial.println(" analyse");
  Serial.println(" sensor scan");
  Serial.println(" sensor setup");
  Serial.println(" assign low 0");
  Serial.println(" assign outside none");
  Serial.println(" target 21.0");
  Serial.println(" kp 22");
  Serial.println(" mode auto");
  Serial.println(" fault reset");
  Serial.println(" wifi on");
  Serial.println(" wifi off");
  Serial.println(" wifi status");
  Serial.println(" wifi startup on");
  Serial.println(" wifi startup off");
  Serial.println(" security status");
  Serial.println(" security reset");
  Serial.println(" save");
  Serial.println();
  Serial.println("Fuer normale Bedienung: m");
  Serial.println("============================================");
  Serial.print("> ");
}

void printCurrentMenu()
{
  switch (currentMenu)
  {
    case MENU_MAIN:        printMainMenu(); break;
    case MENU_STATUS:      printStatusMenu(); break;
    case MENU_SENSORS:     printSensorMenu(); break;
    case MENU_MODE:        printModeMenu(); break;
    case MENU_TEMPERATURE: printTemperatureMenu(); break;
    case MENU_CONTROLLER:  printControllerMenu(); break;
    case MENU_STAGES:      printStageMenu(); break;
    case MENU_LEARNING:    printLearningMenu(); break;
    case MENU_ANALYSIS:    printAnalysisMenu(); break;
    case MENU_SYSTEM:      printSystemMenu(); break;
    case MENU_WIFI:        printWiFiMenu(); break;
    case MENU_SECURITY:    printSecurityMenu(); break;
  }
}

// Kleine Hilfsfunktion: Wert interaktiv abfragen.
// Die Eingabe selbst wird weiterhin zeilenweise vom Terminal gelesen.
// Wir verwenden dafuer einen "pending command".
String pendingMenuCommand = "";

void requestValue(const String &commandPrefix, const String &prompt)
{
  pendingMenuCommand = commandPrefix;

  Serial.println();
  Serial.print(prompt);
  Serial.print(": ");
}

// Assignment helper
void requestSensorAssignment(const String &role)
{
  printSensorScan();
  Serial.println();
  Serial.print("Index fuer ");
  Serial.print(role);
  Serial.println(" eingeben.");
  Serial.println("Oder 'none' zum Deaktivieren.");
  Serial.print("> ");

  pendingMenuCommand = "assign " + role + " ";
}

void requestSensorUnassign()
{
  Serial.println();
  Serial.println("Welche Rolle deaktivieren?");
  Serial.println(" 1 LOW");
  Serial.println(" 2 HIGH");
  Serial.println(" 3 OUTSIDE");
  Serial.println(" 4 SAFETY");
  Serial.print("> ");

  pendingMenuCommand = "__unassign__";
}

void requestOffset()
{
  Serial.println();
  Serial.println("Welcher Offset?");
  Serial.println(" 1 LOW");
  Serial.println(" 2 HIGH");
  Serial.println(" 3 OUTSIDE");
  Serial.println(" 4 SAFETY");
  Serial.print("> ");

  pendingMenuCommand = "__offset_select__";
}

void processMenuInput(String input)
{
  input.trim();

  if (input.length() == 0)
  {
    printCurrentMenu();
    return;
  }

  // ----------------------------------------------------------
  // Laufende Wert-/Wizard-Eingabe
  // ----------------------------------------------------------

  if (pendingMenuCommand.length() > 0)
  {
    String pending = pendingMenuCommand;
    pendingMenuCommand = "";

    if (pending == "__unassign__")
    {
      if (input == "1") processCommand("assign low none");
      else if (input == "2") processCommand("assign high none");
      else if (input == "3") processCommand("assign outside none");
      else if (input == "4") processCommand("assign safety none");
      else Serial.println("Ungueltige Auswahl.");

      printSensorMenu();
      return;
    }

    if (pending == "__offset_select__")
    {
      if (input == "1") requestValue("offset low ", "Offset LOW in K");
      else if (input == "2") requestValue("offset high ", "Offset HIGH in K");
      else if (input == "3") requestValue("offset outside ", "Offset OUTSIDE in K");
      else if (input == "4") requestValue("offset safety ", "Offset SAFETY in K");
      else
      {
        Serial.println("Ungueltige Auswahl.");
        printSensorMenu();
      }

      return;
    }

    if (pending == "__weight_select__")
    {
      if (input == "1") requestValue("weight low ", "Weight LOW in %");
      else if (input == "2") requestValue("weight high ", "Weight HIGH in %");
      else
      {
        Serial.println("Ungueltige Auswahl.");
        printSensorMenu();
      }

      return;
    }

    // Normale pending command:
    processCommand(pending + input);
    printCurrentMenu();
    return;
  }

  // ----------------------------------------------------------
  // Navigation jederzeit
  // ----------------------------------------------------------

  String low = input;
  low.toLowerCase();

  if (low == "m")
  {
    printMainMenu();
    return;
  }

  if (low == "b")
  {
    printMainMenu();
    return;
  }

  // Textbefehle bleiben als Expertenmodus erhalten.
  // Wenn Eingabe keine reine Zahl ist, an alten Parser durchreichen.
  bool numeric = true;

  for (size_t i = 0; i < input.length(); i++)
  {
    if (!isDigit(input[i]))
    {
      numeric = false;
      break;
    }
  }

  if (!numeric)
  {
    if (low == "h")
    {
      printExpertCommands();
      return;
    }

    processCommand(input);
    Serial.println();
    Serial.println("(m = Hauptmenue)");
    Serial.print("> ");
    return;
  }

  int choice = input.toInt();

  // ----------------------------------------------------------
  // Hauptmenue
  // ----------------------------------------------------------

  if (currentMenu == MENU_MAIN)
  {
    switch (choice)
    {
      case 0: printStatus(); printMainMenu(); break;
      case 1: printStatusMenu(); break;
      case 2: printSensorMenu(); break;
      case 3: printModeMenu(); break;
      case 4: printTemperatureMenu(); break;
      case 5: printControllerMenu(); break;
      case 6: printStageMenu(); break;
      case 7: printLearningMenu(); break;
      case 8: printAnalysisMenu(); break;
      case 9: printSystemMenu(); break;
      default:
        Serial.println("Ungueltige Auswahl.");
        printMainMenu();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // Status
  // ----------------------------------------------------------

  if (currentMenu == MENU_STATUS)
  {
    switch (choice)
    {
      case 1: printStatus(); break;
      case 2: printSensorStatus(); break;
      case 3: printThermal(); break;
      case 4: printDiagnostics(); break;
      default: Serial.println("Ungueltige Auswahl."); break;
    }

    printStatusMenu();
    return;
  }

  // ----------------------------------------------------------
  // Sensoren
  // ----------------------------------------------------------

  if (currentMenu == MENU_SENSORS)
  {
    switch (choice)
    {
      case 1:
        printSensorStatus();
        printSensorMenu();
        break;

      case 2:
        printSensorScan();
        printSensorMenu();
        break;

      case 3:
        printSensorSetup();
        printSensorMenu();
        break;

      case 4:
        requestSensorAssignment("low");
        break;

      case 5:
        requestSensorAssignment("high");
        break;

      case 6:
        requestSensorAssignment("outside");
        break;

      case 7:
        requestSensorAssignment("safety");
        break;

      case 8:
        requestSensorUnassign();
        break;

      case 9:
        Serial.println();
        Serial.println("Aktuelle Gewichtung:");
        Serial.print("LOW  = "); Serial.println(cfg.weightLow);        Serial.print("HIGH = "); Serial.println(cfg.weightHigh);
        Serial.println();
        Serial.println(" 1 LOW aendern");
        Serial.println(" 2 HIGH aendern");
        Serial.print("> ");
        pendingMenuCommand = "__weight_select__";
        break;

      case 10:
        requestOffset();
        break;

      case 11:
        saveSettings();
        printSensorMenu();
        break;

      default:
        Serial.println("Ungueltige Auswahl.");
        printSensorMenu();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // Mode
  // ----------------------------------------------------------

  if (currentMenu == MENU_MODE)
  {
    switch (choice)
    {
      case 1: setMode(MODE_OEM); break;
      case 2: setMode(MODE_AUTO); break;
      case 3: setMode(MODE_BOOST); break;
      case 4: setMode(MODE_MANUAL); break;
      case 5: manualStage = 0; setMode(MODE_MANUAL); break;
      case 6: manualStage = 1; setMode(MODE_MANUAL); break;
      case 7: manualStage = 2; setMode(MODE_MANUAL); break;
      case 8: manualStage = 3; setMode(MODE_MANUAL); break;
      default: Serial.println("Ungueltige Auswahl."); break;
    }

    printModeMenu();
    return;
  }

  // ----------------------------------------------------------
  // Temperatur
  // ----------------------------------------------------------

  if (currentMenu == MENU_TEMPERATURE)
  {
    switch (choice)
    {
      case 1: requestValue("target ", "Neue Solltemperatur in C"); break;
      case 2: requestValue("deadband ", "Neue Deadband in K"); break;
      case 3: requestValue("restart ", "Neues Restart Delta in K"); break;
      case 4: requestValue("weight low ", "Weight LOW in %"); break;
      case 5: requestValue("weight high ", "Weight HIGH in %"); break;
      default:
        Serial.println("Ungueltige Auswahl.");
        printTemperatureMenu();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // Regler
  // ----------------------------------------------------------

  if (currentMenu == MENU_CONTROLLER)
  {
    switch (choice)
    {
      case 1: requestValue("kp ", "Neuer Kp"); break;
      case 2: requestValue("ki ", "Neuer Ki"); break;
      case 3: requestValue("trendgain ", "Neuer Trend Gain"); break;
      case 4: printStatus(); printControllerMenu(); break;
      default:
        Serial.println("Ungueltige Auswahl.");
        printControllerMenu();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // Heizstufen
  // ----------------------------------------------------------

  if (currentMenu == MENU_STAGES)
  {
    switch (choice)
    {
      case 1: requestValue("threshold1 ", "Neuer Threshold 1"); break;
      case 2: requestValue("threshold2 ", "Neuer Threshold 2"); break;
      case 3: requestValue("threshold3 ", "Neuer Threshold 3"); break;
      case 4: requestValue("mintime ", "Neue Mindestzeit in s"); break;
      case 5: requestValue("stagedelay ", "Neuer Stage Delay in s"); break;
      case 6: requestValue("boosttime ", "Neue Boost-Zeit in s"); break;
      default:
        Serial.println("Ungueltige Auswahl.");
        printStageMenu();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // Lernen
  // ----------------------------------------------------------

  if (currentMenu == MENU_LEARNING)
  {
    switch (choice)
    {
      case 1:
        cfg.learningEnabled = true;
        Serial.println("Learning EIN");
        printLearningMenu();
        break;

      case 2:
        cfg.learningEnabled = false;
        stopLearningMeasurement();
        Serial.println("Learning AUS");
        printLearningMenu();
        break;

      case 3:
        printThermal();
        printLearningMenu();
        break;

      case 4:
        model = ThermalModel();
        stopLearningMeasurement();
        Serial.println("Lernmodell zurueckgesetzt.");
        printLearningMenu();
        break;

      case 5:
        requestValue("learnrate ", "Neue Learning Rate");
        break;

      case 6:
        requestValue("learntime ", "Neue Learning Messzeit in s");
        break;

      default:
        Serial.println("Ungueltige Auswahl.");
        printLearningMenu();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // Analyse
  // ----------------------------------------------------------

  if (currentMenu == MENU_ANALYSIS)
  {
    switch (choice)
    {
      case 1: printAnalysis(); break;
      case 2: printDiagnostics(); break;
      case 3: printFaultHistory(); break;
      case 4: printLastStoredFault(); break;
      case 5: resetFault(); break;
      default: Serial.println("Ungueltige Auswahl."); break;
    }

    printAnalysisMenu();
    return;
  }

  // ----------------------------------------------------------
  // System
  // ----------------------------------------------------------

  if (currentMenu == MENU_SYSTEM)
  {
    switch (choice)
    {
      case 1:
        printWiFiMenu();
        break;

      case 2:
        printSecurityMenu();
        break;

      case 3:
        saveSettings();
        printSystemMenu();
        break;

      case 4:
        loadDefaults();
        printSystemMenu();
        break;

      case 5:
        forceOutputsOff();
        Serial.println("Neustart...");
        delay(300);
        ESP.restart();
        break;

      case 6:
        printStatus();
        printSystemMenu();
        break;

      default:
        Serial.println("Ungueltige Auswahl.");
        printSystemMenu();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // WLAN
  // ----------------------------------------------------------

  if (currentMenu == MENU_WIFI)
  {
    switch (choice)
    {
      case 1:
        startZHCWiFi();
        printWiFiMenu();
        break;

      case 2:
        stopZHCWiFi();
        printWiFiMenu();
        break;

      case 3:
        printWiFiStatus();
        printWiFiMenu();
        break;

      case 4:
        wifiStartupEnabled = true;
        saveWiFiSettings();
        Serial.println("WLAN Startup = EIN");
        printWiFiMenu();
        break;

      case 5:
        wifiStartupEnabled = false;
        saveWiFiSettings();
        Serial.println("WLAN Startup = AUS");
        printWiFiMenu();
        break;

      default:
        Serial.println("Ungueltige Auswahl.");
        printWiFiMenu();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // Sicherheit
  // ----------------------------------------------------------

  if (currentMenu == MENU_SECURITY)
  {
    switch (choice)
    {
      case 1:
        Serial.println();
        Serial.println("Login-Status:");
        Serial.println(" Benutzer: admin");
        Serial.print(" Erstlogin erforderlich: ");
        Serial.println(forcePasswordChange ? "JA" : "NEIN");
        Serial.print(" Session aktiv: ");
        Serial.println(sessionToken.length() > 0 ? "JA" : "NEIN");
        printSecurityMenu();
        break;

      case 2:
        Serial.println();
        if (forcePasswordChange && temporaryPassword.length() > 0)
        {
          Serial.println("Benutzer       : admin");
          Serial.print("Initialpasswort: ");
          Serial.println(temporaryPassword);
        }
        else
        {
          Serial.println("Kein Initialpasswort aktiv.");
          Serial.println("Das gesetzte Admin-Passwort wird nie angezeigt.");
        }
        printSecurityMenu();
        break;

      case 3:
        invalidateWebSession();
        Serial.println("Web-Session abgemeldet.");
        printSecurityMenu();
        break;

      case 4:
        resetWebCredentials();
        printSecurityMenu();
        break;

      case 5:
        Serial.println();
        Serial.println("Lokaler Recovery:");
        Serial.println("- Beim Einschalten den lokalen Taster ca. 8 s halten.");
        Serial.println("- Es werden NUR Web-Zugangsdaten zurueckgesetzt.");
        Serial.println("- Sensor-/Regler-/Lernwerte bleiben erhalten.");
        Serial.println("- Neues Initialpasswort erscheint im Terminal.");
        printSecurityMenu();
        break;

      default:
        Serial.println("Ungueltige Auswahl.");
        printSecurityMenu();
        break;
    }

    return;
  }
}

// =====================================================================
// COMMAND PARSER
// =====================================================================

void processCommand(String cmd)
{
  cmd.trim();
  cmd.toLowerCase();

  // -----------------------------------------------------------
  // Allgemein
  // -----------------------------------------------------------

  if (cmd == "menu")
  {
    printMainMenu();
  }
  else if (cmd == "help")
  {
    printExpertCommands();
  }
  else if (cmd == "status")
  {
    printStatus();
  }
  else if (cmd == "analyse")
  {
    printAnalysis();
  }
  else if (cmd == "analyse history")
  {
    printFaultHistory();
  }
  else if (cmd == "analyse last")
  {
    printLastStoredFault();
  }
  else if (cmd == "diag")
  {
    printDiagnostics();
  }
  else if (cmd == "thermal")
  {
    printThermal();
  }
  else if (cmd == "fault")
  {
    printFault();
  }
  else if (cmd == "fault history")
  {
    printFaultHistory();
  }
  else if (cmd == "fault last")
  {
    printLastStoredFault();
  }
  else if (cmd == "fault reset")
  {
    resetFault();
  }

  // -----------------------------------------------------------
  // Sensoren
  // -----------------------------------------------------------

  else if (cmd == "sensor scan")
  {
    printSensorScan();
  }
  else if (cmd == "sensor setup")
  {
    printSensorSetup();
  }
  else if (cmd == "sensor status")
  {
    printSensorStatus();
  }
  else if (cmd == "sensor save")
  {
    saveSettings();
  }

  // Assign
  else if (cmd == "assign low none")
  {
    unassignSensor(ROLE_LOW);
  }
  else if (cmd == "assign high none")
  {
    unassignSensor(ROLE_HIGH);
  }
  else if (cmd == "assign outside none")
  {
    unassignSensor(ROLE_OUTSIDE);
  }
  else if (cmd == "assign safety none")
  {
    unassignSensor(ROLE_SAFETY);
  }
  else if (cmd.startsWith("assign low "))
  {
    int index = cmd.substring(11).toInt();
    assignSensor(ROLE_LOW, index);
  }
  else if (cmd.startsWith("assign high "))
  {
    int index = cmd.substring(12).toInt();
    assignSensor(ROLE_HIGH, index);
  }
  else if (cmd.startsWith("assign outside "))
  {
    int index = cmd.substring(15).toInt();
    assignSensor(ROLE_OUTSIDE, index);
  }
  else if (cmd.startsWith("assign safety "))
  {
    int index = cmd.substring(14).toInt();
    assignSensor(ROLE_SAFETY, index);
  }

  // Offsets
  else if (cmd.startsWith("offset low "))
  {
    sensorLow.offset = cmd.substring(11).toFloat();
    Serial.print("LOW Offset = ");
    Serial.println(sensorLow.offset);
  }
  else if (cmd.startsWith("offset high "))
  {
    sensorHigh.offset = cmd.substring(12).toFloat();
    Serial.print("HIGH Offset = ");
    Serial.println(sensorHigh.offset);
  }
  else if (cmd.startsWith("offset outside "))
  {
    sensorOutside.offset = cmd.substring(15).toFloat();
    Serial.print("OUTSIDE Offset = ");
    Serial.println(sensorOutside.offset);
  }
  else if (cmd.startsWith("offset safety "))
  {
    sensorSafety.offset = cmd.substring(14).toFloat();
    Serial.print("SAFETY Offset = ");
    Serial.println(sensorSafety.offset);
  }

  // Gewichtung
  else if (cmd.startsWith("weight low "))
  {
    float v = cmd.substring(11).toFloat();

    if (v >= 0.0f && v <= 100.0f)
    {
      cfg.weightLow = v;
      Serial.print("Weight LOW = ");
      Serial.println(v);
    }
  }
  else if (cmd.startsWith("weight high "))
  {
    float v = cmd.substring(12).toFloat();

    if (v >= 0.0f && v <= 100.0f)
    {
      cfg.weightHigh = v;
      Serial.print("Weight HIGH = ");
      Serial.println(v);
    }
  }

  // Safety
  else if (cmd.startsWith("safetydiff "))
  {
    float v = cmd.substring(11).toFloat();

    if (v >= 0.5f && v <= 15.0f)
    {
      cfg.safetyMaxDifference = v;
      Serial.print("Safety Differenz = ");
      Serial.print(v);
      Serial.println(" K");
    }
  }
  else if (cmd.startsWith("safetydelay "))
  {
    int v = cmd.substring(12).toInt();

    if (v >= 5 && v <= 600)
    {
      cfg.safetyDifferenceDelaySec = v;
      Serial.print("Safety Delay = ");
      Serial.print(v);
      Serial.println(" s");
    }
  }

  // -----------------------------------------------------------
  // Modus
  // -----------------------------------------------------------

  else if (cmd == "mode oem")
  {
    setMode(MODE_OEM);
  }
  else if (cmd == "mode auto")
  {
    setMode(MODE_AUTO);
  }
  else if (cmd == "mode boost")
  {
    setMode(MODE_BOOST);
  }
  else if (cmd == "mode manual")
  {
    setMode(MODE_MANUAL);
  }

  // -----------------------------------------------------------
  // Manual Stage
  // -----------------------------------------------------------

  else if (cmd.startsWith("stage "))
  {
    int v = cmd.substring(6).toInt();

    if (v >= 0 && v <= 3)
    {
      manualStage = v;
      Serial.print("Manual Stage = ");
      Serial.println(v);
    }
    else
    {
      Serial.println("Bereich: 0...3");
    }
  }

  // -----------------------------------------------------------
  // Temperatur
  // -----------------------------------------------------------

  else if (cmd.startsWith("target "))
  {
    float v = cmd.substring(7).toFloat();

    if (v >= 15.0f && v <= 28.0f)
    {
      cfg.targetTemp = v;
      integralTerm = 0.0f;
      targetHoldActive = false;

      Serial.print("Solltemperatur = ");
      Serial.println(v);
    }
  }
  else if (cmd.startsWith("deadband "))
  {
    float v = cmd.substring(9).toFloat();

    if (v >= 0.10f && v <= 2.0f)
    {
      cfg.deadBand = v;
      Serial.print("Deadband = ");
      Serial.println(v);
    }
  }
  else if (cmd.startsWith("restart "))
  {
    float v = cmd.substring(8).toFloat();

    if (v >= 0.20f && v <= 5.0f)
    {
      cfg.restartDelta = v;
      Serial.print("Restart Delta = ");
      Serial.println(v);
    }
  }

  // -----------------------------------------------------------
  // PI
  // -----------------------------------------------------------

  else if (cmd.startsWith("kp "))
  {
    float v = cmd.substring(3).toFloat();

    if (v >= 0.0f && v <= 100.0f)
      cfg.kp = v;
  }
  else if (cmd.startsWith("ki "))
  {
    float v = cmd.substring(3).toFloat();

    if (v >= 0.0f && v <= 10.0f)
      cfg.ki = v;
  }
  else if (cmd.startsWith("trendgain "))
  {
    float v = cmd.substring(10).toFloat();

    if (v >= 0.0f && v <= 100.0f)
      cfg.trendGain = v;
  }

  // -----------------------------------------------------------
  // Stufen
  // -----------------------------------------------------------

  else if (cmd.startsWith("threshold1 "))
  {
    cfg.stage1Threshold = cmd.substring(11).toFloat();
  }
  else if (cmd.startsWith("threshold2 "))
  {
    cfg.stage2Threshold = cmd.substring(11).toFloat();
  }
  else if (cmd.startsWith("threshold3 "))
  {
    cfg.stage3Threshold = cmd.substring(11).toFloat();
  }
  else if (cmd.startsWith("mintime "))
  {
    int v = cmd.substring(8).toInt();

    if (v >= 10 && v <= 1800)
      cfg.minStageTimeSec = v;
  }
  else if (cmd.startsWith("stagedelay "))
  {
    int v = cmd.substring(11).toInt();

    if (v >= 5 && v <= 600)
      cfg.stageDelaySec = v;
  }
  else if (cmd.startsWith("boosttime "))
  {
    int v = cmd.substring(10).toInt();

    if (v >= 10 && v <= 3600)
      cfg.boostTimeSec = v;
  }

  // -----------------------------------------------------------
  // Learning
  // -----------------------------------------------------------

  else if (cmd == "learning on")
  {
    cfg.learningEnabled = true;
    Serial.println("Learning ON");
  }
  else if (cmd == "learning off")
  {
    cfg.learningEnabled = false;
    stopLearningMeasurement();
    Serial.println("Learning OFF");
  }
  else if (cmd == "learning reset")
  {
    model = ThermalModel();
    stopLearningMeasurement();
    Serial.println("Thermisches Lernmodell zurueckgesetzt.");
  }
  else if (cmd.startsWith("learnrate "))
  {
    float v = cmd.substring(10).toFloat();

    if (v >= 0.001f && v <= 0.5f)
      cfg.learningRate = v;
  }
  else if (cmd.startsWith("learntime "))
  {
    int v = cmd.substring(10).toInt();

    if (v >= 30 && v <= 3600)
      cfg.learningMinTimeSec = v;
  }

  // -----------------------------------------------------------
  // Sicherheit
  // -----------------------------------------------------------

  else if (cmd.startsWith("emergency "))
  {
    float v = cmd.substring(10).toFloat();

    if (v >= 25.0f && v <= 50.0f)
      cfg.emergencyMaxTemp = v;
  }
  else if (cmd.startsWith("sensortimeout "))
  {
    int v = cmd.substring(14).toInt();

    if (v >= 2 && v <= 120)
      cfg.sensorTimeoutSec = v;
  }
  else if (cmd.startsWith("tempjump "))
  {
    float v = cmd.substring(9).toFloat();

    if (v >= 0.5f && v <= 20.0f)
      cfg.maxTempJump = v;
  }

  // -----------------------------------------------------------
  // WLAN / WEB
  // -----------------------------------------------------------

  else if (cmd == "wifi on")
  {
    startZHCWiFi();
  }
  else if (cmd == "wifi off")
  {
    stopZHCWiFi();
  }
  else if (cmd == "wifi status")
  {
    printWiFiStatus();
  }
  else if (cmd == "wifi startup on")
  {
    wifiStartupEnabled = true;
    saveWiFiSettings();
    Serial.println("WLAN Startup = EIN");
  }
  else if (cmd == "wifi startup off")
  {
    wifiStartupEnabled = false;
    saveWiFiSettings();
    Serial.println("WLAN Startup = AUS");
  }
  else if (cmd == "security status")
  {
    Serial.println();
    Serial.println("============== SECURITY STATUS ================");
    Serial.println("Benutzer: admin");
    Serial.print("Erstlogin erforderlich: ");
    Serial.println(forcePasswordChange ? "JA" : "NEIN");
    Serial.print("Session aktiv: ");
    Serial.println(sessionToken.length() > 0 ? "JA" : "NEIN");

    if (forcePasswordChange && temporaryPassword.length() > 0)
    {
      Serial.print("Initialpasswort: ");
      Serial.println(temporaryPassword);
    }

    Serial.println("================================================");
  }
  else if (cmd == "security reset")
  {
    resetWebCredentials();
  }

  // -----------------------------------------------------------
  // System
  // -----------------------------------------------------------

  else if (cmd == "save")
  {
    if (configValid())
      saveSettings();
    else
      Serial.println("Konfiguration ungueltig - nicht gespeichert.");
  }
  else if (cmd == "defaults")
  {
    loadDefaults();
  }
  else if (cmd == "reboot")
  {
    forceOutputsOff();
    Serial.println("Neustart...");
    delay(300);
    ESP.restart();
  }
  else if (cmd.length() > 0)
  {
    Serial.print("Unbekannter Befehl: ");
    Serial.println(cmd);
    Serial.println("menu eingeben.");
  }
}

void processSerial()
{
  while (Serial.available())
  {
    char c = Serial.read();

    if (c == '\r' || c == '\n')
    {
      if (terminalBuffer.length() > 0)
      {
        processMenuInput(terminalBuffer);
        terminalBuffer = "";
      }
    }
    else
    {
      terminalBuffer += c;

      if (terminalBuffer.length() > 160)
        terminalBuffer = "";
    }
  }
}

// =====================================================================
// SETUP
// =====================================================================

void setup()
{
  // ESP32-C3 USB Serial
  Serial.begin(115200);

  uint32_t serialStart = millis();

  while (!Serial &&
         millis() - serialStart < 5000)
  {
    delay(10);
  }

  delay(200);

  // Ausgaenge zuerst sicher AUS
  pinMode(HEATER1_OUT, OUTPUT);
  pinMode(HEATER2_OUT, OUTPUT);
  pinMode(HEATER3_OUT, OUTPUT);
  forceOutputsOff();

  // Button
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Security/WLAN-Konfiguration laden.
  loadSecuritySettings();
  loadWiFiSettings();

  // Lokaler Recovery: Taster beim Boot ca. 8 Sekunden halten.
  checkBootCredentialRecovery();

  // RGB
  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_RGB_G, OUTPUT);
  pinMode(PIN_RGB_B, OUTPUT);
  rgbOff();

  // Einstellungen
  loadSettings();

  // DS18B20
  sensors.begin();
  sensors.setResolution(11);

  scanSensors();

  // Safe Boot
  currentMode = MODE_OEM;
  requestedStage = 0;
  activeStage = 0;
  integralTerm = 0.0f;

  faultLatched = false;
  activeFault = FAULT_NONE;

  lastControlCycleMillis = millis();

  Serial.println();
  Serial.println("================================================");
  Serial.println(FW_VERSION);
  Serial.println("ESP32-C3 SUPER MINI");
  Serial.println("SAFE CONTROL / BENCH VERSION");
  Serial.println("================================================");
  Serial.println();
  Serial.println("BOOT-SICHERHEIT:");
  Serial.println("Alle Heiz-Ausgaenge = AUS");
  Serial.println("Startmodus = OEM");
  Serial.println("Automatische Heizfreigabe = NEIN");
  Serial.println("Terminal/Setup bleibt auch bei Fehlern bedienbar.");
  Serial.println("Ohne Sensorzuordnung: sicherer SETUP/OEM-Zustand, kein FAILSAFE.");
  Serial.println();

  Serial.print("DS18B20 gefunden: ");
  Serial.println(discoveredSensorCount);

  if (forcePasswordChange)
  {
    Serial.println();
    Serial.println("WEB ERSTEINRICHTUNG:");
    Serial.println(" Benutzer       : admin");
    Serial.print(" Initialpasswort: ");
    Serial.println(temporaryPassword);
    Serial.println(" Passwort muss nach erstem Login geaendert werden.");
  }

  if (wifiStartupEnabled)
  {
    startZHCWiFi();
  }

  if (!anyCabinRoleAssigned())
  {
    Serial.println();
    Serial.println("Hinweis: Noch kein Innenraumsensor zugewiesen.");
    Serial.println("Menue 2 -> Sensoren fuer die Einrichtung verwenden.");
  }

  printMainMenu();
}

// =====================================================================
// LOOP
// =====================================================================

void loop()
{
  static uint32_t lastControlMillis = 0;

  processSerial();
  serviceWebServer();

  if (otaInProgress)
  {
    requestedStage = 0;
    activeStage = 0;
    controllerOutput = 0.0f;
    forceOutputsOff();
    updateRGB();
    delay(2);
    return;
  }

  processButton();
  readSensors();

  safetySupervisor();
  checkControlCycleTimeout();

  if (millis() - lastControlMillis >= CONTROL_INTERVAL_MS)
  {
    lastControlMillis = millis();
    lastControlCycleMillis = millis();

    processMode();

    safetySupervisor();

    processStageManager();

    safetySupervisor();

    applyBenchOutputs();
    updateLearning();
    monitorHeatingResponse();
  }

  updateRGB();

  delay(5);
}