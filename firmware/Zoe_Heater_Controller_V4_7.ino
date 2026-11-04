/*
  =====================================================================
  ZOE CABIN HEATER CONTROLLER V4.7
  ESP32-C3 SUPER MINI
  =====================================================================

  SAFE CONTROL / BENCH VERSION

  V4.7 BEDIENKONZEPT:
    - Weboberflaeche als Hauptbedienung fuer Status, Betriebsart,
      Solltemperatur, Sensoren, Regler, Lernsystem und Diagnose.
    - Einstellungen koennen im Browser gespeichert werden.
    - Sensorrollen koennen im Browser den gefundenen DS18B20 zugewiesen werden.
    - Konfigurations-Backup als Textdatei und Restore im Browser.
    - OTA-Firmwareupdate bleibt dauerhaft vorhanden.
    - Vor OTA: OEM-Modus, H1/H2/H3 AUS, Reglerausgang 0.
    - Terminal bleibt vollstaendiger Service-/Fallback-Zugang.
    - Service-/Ausgangsfunktionen bleiben Niedervolt-/Bench-Funktionen.

  V4.6 BASIS:
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

const char* FW_VERSION = "ZOE Heater Controller V4.7";

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
            "*{box-sizing:border-box}"
            "body{font-family:Arial,sans-serif;max-width:980px;margin:22px auto;padding:0 16px;background:#0d1117;color:#e6edf3}"
            ".card{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:18px;margin:14px 0}"
            ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}"
            ".tile{background:#0d1117;border:1px solid #30363d;border-radius:10px;padding:14px}"
            "a,button,input,select,textarea{font-size:16px}"
            "a{color:#58a6ff;text-decoration:none}"
            "input,select,textarea{width:100%;padding:10px;margin:6px 0 12px;border-radius:8px;border:1px solid #484f58;background:#0d1117;color:#e6edf3}"
            "textarea{min-height:220px;font-family:monospace}"
            "button,.btn{display:inline-block;padding:10px 14px;border:0;border-radius:8px;cursor:pointer;background:#238636;color:white;margin:4px 4px 4px 0}"
            ".danger{background:#da3633}.secondary{background:#30363d}"
            ".ok{color:#3fb950}.warn{color:#d29922}.err{color:#f85149}"
            ".nav{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:15px}"
            ".nav a{padding:7px 9px;border-radius:6px;background:#161b22}"
            "table{width:100%;border-collapse:collapse}td,th{padding:8px;border-bottom:1px solid #30363d;text-align:left}"
            "label{font-weight:600}"
            ".small{font-size:13px;color:#8b949e}"
            "</style></head><body>");
  html += F("<h1>ZOE Climate Controller</h1>");
  html += "<div class='nav'>"
          "<a href='/'>Dashboard</a>"
          "<a href='/control'>Bedienung</a>"
          "<a href='/sensors'>Sensoren</a>"
          "<a href='/regulator'>Regler</a>"
          "<a href='/learning'>Lernen</a>"
          "<a href='/diagnostics'>Diagnose</a>"
          "<a href='/system'>System</a>"
          "<a href='/update'>Update</a>"
          "<a href='/logout'>Logout</a>"
          "</div>";

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