/*   TODO
. XX-Web button to clear WiFi credentials
. XX-Add first time startup (AP mode)
. XX-Add table for logs
. XX-Display system information
. XX-Disallow duplicate light schedule entries
. XX-Random colors
. XX-Schedule for changing color/brighness
. XX-Change to websockets
. XX-Change PILLS to all buttons
. XX-Web button to clear logs
*/
/* Version doc (find out how to use github!)
  3.01 - update oled display with real data
  3.03 - add code to restart when in AP mode. If power fails and comes back up, the wireless may not be
    available for a few minutes. So, periodically restart.
  3.04 - add save minute to come back automatically after power failure
  3.05 - fix bug make sure g_lastMinute is updated
*/
#pragma region Includes
#include <Arduino.h>
#include <AccelStepper.h>
#include <time.h>
#include <WiFi.h>
// #include "ESPDateTime.h"
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include "SPIFFS.h"
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#pragma endregion

#pragma region Variables
int g_ver_major = 3;
int g_ver_minor = 65;

AsyncWebServer server(80); // Create AsyncWebServer object on g_stepperports 80
AsyncWebSocket ws("/ws");
bool g_isSTAMode = false; // True if in WiFi station mode
int g_apTimeoutMin = 5;

// stepper motor control
// int g_stepperports[4] = {19, 18, 4, 15}; // ports used to control the stepper motor
int g_stepperports[4] = {18, 19, 23, 33}; // ports used to control the stepper motor
int g_steppersequence[8][4] = {
    {LOW, HIGH, HIGH, LOW},
    {LOW, LOW, HIGH, LOW},
    {LOW, LOW, HIGH, HIGH},
    {LOW, LOW, LOW, HIGH},
    {HIGH, LOW, LOW, HIGH},
    {HIGH, LOW, LOW, LOW},
    {HIGH, HIGH, LOW, LOW},
    {LOW, HIGH, LOW, LOW}};
int g_stepperDelaytime = 1; // Stepper motor delay between steps

const long wifiTimeout = 10000; // WiFi connection attemp limit - 10 seconds
// #define LED 2

#define STEPS_PER_ROTATION 12288 // steps for a full turn of minute ring (1 hour)
int g_stepsHour;
int g_stepTestCycles;
// Next three are used in the move minute routine. Dependent on g_stepsHour
int g_stepsMinute = 0;  // motor steps / minute
int g_addStepsFive = 0; // add motor steps every fifth minute
int g_addStepsHour = 0; // add motor steps at the end of each hour
int g_lastMinute = 0;   // used to trigger each minute (move minute hand)
int g_lastSecond = 0;   // used in random light setting

// Web page drop-down-lists (select) with defaulted value selected
//   For Timezon, Color, and Level
//   These are passed up using the processor() function
String g_selectTimeZones[10][2] = {
    {"AKST9AKDT,M3.2.0,M11.1.0", "America/Anchorage"},
    {"MST7MDT,M3.2.0,M11.1.0", "America/Boise"},
    {"CST6CDT,M3.2.0,M11.1.0", "America/Chicago"},
    {"MST7MDT,M3.2.0,M11.1.0", "America/Denver"},
    {"EST5EDT,M3.2.0,M11.1.0", "America/Detroit"},
    {"EST5EDT,M3.2.0,M11.1.0", "America/Louisville"},
    {"PST8PDT,M3.2.0,M11.1.0", "America/Los Angeles"},
    {"CST6CDT,M4.1.0,M10.5.0", "America/Mexico City"},
    {"EST5EDT,M3.2.0,M11.1.0", "America/New York"},
    {"HST10", "America/Puerto Rico"}};
String g_selectColor[10][2] = {
    {"Red", "Red"},
    {"Orange", "Orange"},
    {"Yellow", "Yellow"},
    {"Green", "Green"},
    {"Blue", "Blue"},
    {"Indigo", "Indigo"},
    {"Violet", "Violet"},
    {"White", "White"},
    {"Random", "Random"},
    {"Sparkle", "Sparkle"}};
String g_selectLevel[11][2] = {
    {"255", "10"},
    {"200", "9"},
    {"150", "8"},
    {"100", "7"},
    {"80", "6"},
    {"60", "5"},
    {"25", "4"},
    {"8", "3"},
    {"3", "2"},
    {"1", "1"},
    {"0", "Off"}};

hw_timer_t *g_SparkleTimer = NULL;
hw_timer_t *g_RandomTimer = NULL;

String g_clockName = "";            // Clock name
bool g_prepConfigStepping = false;  // True if preparing for configuration g_stepTestCycles
bool g_startConfigStepping = false; // True if perfoming configuration g_stepTestCycles
bool g_isSettingClock = false;      // True if setting the clock to real time
bool g_isRandomColor = false;       // True if random color selected
// int g_clockHour;                    // Keep track of clock hour postion
// int g_clockMinute;                  // Keep track of clock minute position
String g_clockTime = ""; // What we think is the clock time. Format: HH:MM
String g_timezone;       // Timezone
String g_ntpPool;        // NTP pool
String g_ssid;           // WiFi network name
String g_pass;           // WiFi network passowrd
String g_lighting;       // The lighting parameters

// File paths for permanent EEPROM values
const char *e_ssidPath = "/ssid.txt";
const char *e_passPath = "/pass.txt";
const char *e_stepsHourPath = "/stephour.txt";
const char *e_testCyclesPath = "/testcycs.txt";
const char *e_timezonePath = "/timezone.txt";
const char *e_ntpPoolPath = "/ntppool.txt";
// const char *e_changeTZPath = "/changetz.txt";
const char *e_ledColorPath = "/ledcolor.txt";
const char *e_ledLevelPath = "/ledlevel.txt";
const char *e_logfilePath = "/logfile.txt";
const char *e_schedulePath = "/schedule.txt";
const char *e_clocknmPath = "/clocknm.txt";
const char *e_clockTimePath = "/clocktm.txt";

#define LED_PIN 4
#define NUM_LEDS 24
#define BRIGHTNESS 240
#define LED_TYPE WS2811
#define COLOR_ORDER GRB
enum LedMode
{
  Normal,
  Random,
  Sparkle
};
CRGB g_leds[NUM_LEDS];
String g_ledColor = "";
int g_ledLevel = 0;
LedMode g_ledMode = Normal;
typedef struct
{
  String sTime;
  String sColor;
  int iLevel;
} sched_type;
sched_type g_schedules[25];
int g_scheduleCount = 0;

bool g_sparkleTimerTriggered = false;
bool g_randomTimerTriggered = false;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#define SSD1306_NO_SPLASH

#define SWITCH_PIN 26

ulong g_steps_plus_completed;
ulong g_steps_minus_completed;

#pragma endregion

#pragma region Function declarations
#pragma region Date/Time
String getTime(const char *format);
int getSecond();
int getMinute();
int getHour();
int getYear();
void setTimezone(String timezone);
void setupDateTime(String, String);
void determineMinuteSteps();
String formatTime(int, int);
#pragma endregion
#pragma region File
void initSPIFFS();
String readFile(fs::FS &fs, const char *path);
String readFile(fs::FS &fs, const char *path, String);
void writeFile(fs::FS &fs, const char *path, const char *message);
#pragma endregion
#pragma region Hardware timers
void IRAM_ATTR onTimerRandom();
void IRAM_ATTR onTimerSparkle();
#pragma endregion

int cmpSched(const void *a, const void *b);
void deleteSchedule(int idx);
String formatSchedule();
long hstol(String recv);
String formatLogs(String logs);
void listDir(const char *dir);
void moveMinute();
String processor(const String &var);
void readLightSchedule();
void rotate(int step);
void setupLighting();
bool setupWiFi();
void writeLightSchedule();
void writeLog(String title, String content);
String formatSysinfo();
String ledLevelNumber(int lvl);
void writeLineDisplay(int line, int col, String txt);
String lightBrightnessToLevel(int);
#pragma region WebSocket
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len);
void initWebSocket();
#pragma endregion
#pragma endregion

void setup()
{
  Serial.begin(115200);
  pinMode(SWITCH_PIN, INPUT_PULLUP);

#pragma region Get values from permanent storage
  initSPIFFS();
  g_timezone = readFile(SPIFFS, e_timezonePath, "CST6CDT,M3.2.0,M11.1.0");
  g_ntpPool = readFile(SPIFFS, e_ntpPoolPath, "us.pool.ntp.org");

  g_stepsHour = readFile(SPIFFS, e_stepsHourPath, "36870").toInt();
  g_stepTestCycles = readFile(SPIFFS, e_testCyclesPath, "100").toInt();

  g_ssid = readFile(SPIFFS, e_ssidPath);
  g_pass = readFile(SPIFFS, e_passPath);

  g_ledColor = readFile(SPIFFS, e_ledColorPath, "White");
  g_ledLevel = readFile(SPIFFS, e_ledLevelPath, "0").toInt();

  g_clockName = readFile(SPIFFS, e_clocknmPath, "Clock Name Here");
  g_clockTime = readFile(SPIFFS, e_clockTimePath, "0");

  readLightSchedule();

  // listDir("/");

#pragma endregion
#pragma region Setup the OLED display
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  // Serial.println("Setup Display...");
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("SSD1306 allocation failed"));
    // for (;;)
    //   ; // Don't proceed, loop forever
  }
  display.display();
  display.clearDisplay();
  display.setTextSize(1.2);           // Normal 1:1 pixel scale
  display.setTextColor(WHITE, BLACK); // Draw white text
  display.cp437(true);                // Use full 256 char 'Code Page 437' font
#pragma endregion
#pragma region Setup Web services
  if (setupWiFi()) // if the WiFi is available - Setup web server and pages
  {
    g_isSTAMode = true;
    initWebSocket();

    // Setup the home page -------------------------------------------
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html", "text/html", false, processor); });
    server.serveStatic("/", SPIFFS, "/");

    server.begin();
  }
  else // Setup the AP mode to get WiFi credentials
  {
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point

    String networkName = WiFi.macAddress().c_str();
    networkName = networkName.substring(6);
    networkName = "Clock_" + networkName; // Add MAC address to name

    WiFi.softAP(networkName.c_str(), NULL);
    Serial.println(networkName);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    writeLineDisplay(0, 0, "Connect to network:");
    writeLineDisplay(0, 1, networkName);
    writeLineDisplay(0, 2, "Then: " + IP.toString());

    // Set the timeout tics for the main AP loop. Minutes * seconds * 250ms delay (in main loop)
    g_apTimeoutMin = g_apTimeoutMin * 60 * 4;

    // Setup the home page -------------------------------------------
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/wifimanager.html", "text/html"); });
    server.serveStatic("/", SPIFFS, "/");

    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
              {
      int params = request->params();
      for (int i = 0; i < params; i++)
      {
        AsyncWebParameter *p = request->getParam(i);
        if (p->isPost())
        {
          // HTTP POST ssid value
          if (p->name() == "ssid")
          {
            g_ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(g_ssid);
            // Write file to save value
            writeFile(SPIFFS, e_ssidPath, g_ssid.c_str());
          }
          // HTTP POST pw value
          if (p->name() == "pw")
          {
            g_pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(g_pass);
            // Write file to save value
            writeFile(SPIFFS, e_passPath, g_pass.c_str());
          }
        }
      }     
      //request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      ESP.restart(); });
    server.begin();
  };
#pragma endregion
#pragma region Finish setup if we have successfully connected to WiFi
  if (g_isSTAMode) // Finish setup if we have successfully connected to WiFi
  {
    String sIP = "IP:" + String(WiFi.localIP().toString());
    writeLineDisplay(0, 0, sIP);
    String sLighting = g_ledColor + ", " + ledLevelNumber(g_ledLevel);
    writeLineDisplay(0, 1, g_ledColor + ", " + ledLevelNumber(g_ledLevel));

    writeLineDisplay(0, 2, getTime("%H:%M:%S"));
    display.display();

#pragma region Setup Timers
    g_SparkleTimer = timerBegin(0, 80, true);
    timerAttachInterrupt(g_SparkleTimer, &onTimerSparkle, true);
    timerAlarmWrite(g_SparkleTimer, 50000, true);
    timerAlarmEnable(g_SparkleTimer);

    g_RandomTimer = timerBegin(1, 80, true);
    timerAttachInterrupt(g_RandomTimer, &onTimerRandom, true);
    timerAlarmWrite(g_RandomTimer, 2000000, true);
    timerAlarmEnable(g_RandomTimer);
#pragma endregion

#pragma region Setup stepper motor pins
    pinMode(g_stepperports[0], OUTPUT);
    pinMode(g_stepperports[1], OUTPUT);
    pinMode(g_stepperports[2], OUTPUT);
    pinMode(g_stepperports[3], OUTPUT);
#pragma endregion

#pragma region Setup Date/Time
    setupDateTime(g_ntpPool, g_timezone);
    determineMinuteSteps();

    Serial.printf("Startup. Clock time: %s\n", g_clockTime.c_str());
    if (g_clockTime != "0")
    {
      String sRealTime = formatTime(getHour(), getMinute());
      if (g_clockTime != sRealTime)
      {
        Serial.printf("Time mismatch. Real time: %s\n", sRealTime);
        g_isSettingClock = true;
      }
    }

    ws.textAll("timezone##" + g_timezone);
#pragma endregion

#pragma region Setup the clock lighting
    // Setup lighting and assume 'Off'
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(g_leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(0);

    setupLighting();

    ws.textAll("lighting##" + g_ledColor + "&" + ledLevelNumber(g_ledLevel));
#pragma endregion

    writeLog("STARTUP", "&nbsp;");
  }
#pragma endregion
}

void loop()
{
  if (g_isSTAMode) // No loop processed unless we successfully connect to WiFi
  {
#pragma region Test steps/hour for specified g_stepTestCycles.
    if (g_prepConfigStepping)
    {
      if (g_startConfigStepping) // Start running hour/step for iteration times.
      {
        Serial.printf("Starting stepping process. steps/hour: %d, g_stepTestCycles: %d\n", g_stepsHour, g_stepTestCycles);
        for (int i = 0; i < g_stepTestCycles; i++)
        {
          Serial.printf("   Iteration: %d\r", i + 1);
          ws.textAll("cycleCount##" + String(i + 1));

          rotate(g_stepsHour);
          // Serial.printf("Steps: %d\n", cnt);
          if (!g_startConfigStepping)
            break;
          delay(1000);
        }
        determineMinuteSteps();
        g_startConfigStepping = false;
        // ws.textAll("configFinished##done");
        Serial.printf("\nFinished configuring steps/hour.\n");
      }
    }
#pragma endregion
#pragma region Set the clock
    else if (g_isSettingClock) // Set the clock time starting from observed (clock) time.
    {
      Serial.printf("Setting time from: %s to: %s\n", g_clockTime, formatTime(getHour(), getMinute()));

      // Parse the clock time string and
      // Get the clock time total minutes past 12:00
      // Serial.println("Starting time set...");
      int iColon = g_clockTime.indexOf(':');
      int g_CurrentClockHour = g_clockTime.substring(0, iColon).toInt();
      int iClockMin = g_clockTime.substring(iColon + 1).toInt();
      int iClockTotalMinutes = g_CurrentClockHour * 60 + iClockMin;
      // Serial.printf("   Clock time: %s, Hour: %d, Minute: %d, Total Mins: %d\n", g_clockTime, g_CurrentClockHour, iClockMin, iClockTotalMinutes);

      // Get the local time total minutes past 12:00
      int iLocalHour = getHour();
      int iLocalMin = getMinute();
      int iLocalTotalMinutes = iLocalHour * 60 + iLocalMin;
      // Serial.printf("   Real time: Hour: %d, Minute: %d, Total mins: %d\n", iLocalHour, iLocalMin, iLocalTotalMinutes);

      // Use the difference between the two total minutes to determine how many (and which direction) steps to move
      int iMinutesDifference = iLocalTotalMinutes - iClockTotalMinutes;
      int iMoveMinutes = iMinutesDifference;
      if (abs(iMinutesDifference) > 360)
        iMoveMinutes = (iMinutesDifference > 0) ? iMinutesDifference - 720 : iMinutesDifference + 720;
      int iMoveSteps = iMoveMinutes * (g_stepsHour / 60);
      // Serial.printf("   Minute difference: %d, Move minutes: %d, Move steps: %d\n", iMinutesDifference, iMoveMinutes, iMoveSteps);

      // Do the move and compensate for backlash if moving backwards
      if (iMoveSteps > 0)
      {
        rotate(iMoveSteps);
      }
      else
      {
        iMoveSteps -= 200;
        rotate(iMoveSteps);
        rotate(200);
      }

      // Set precise time. See if a minute(s) passed during clock move
      int iMinNow = getMinute();
      int iFixMin = iMinNow - iLocalMin;
      if (iFixMin != 0)
      {
        Serial.printf("   Tweaking time. iLocalMin: %d, iMinNow: %d iFixMin: %d\n", iLocalMin, iMinNow, iFixMin);
        rotate(iFixMin * (g_stepsHour / 60));
      }

      String sMsg = "Local: " + getTime("%r") + ", Clock: " + g_clockTime;
      writeLog("CLOCKSET", sMsg);
      ws.textAll("localTime##" + getTime("%r"));

      g_clockTime = formatTime(getHour(), getMinute());
      g_lastMinute = getMinute();
      writeFile(SPIFFS, e_clockTimePath, g_clockTime.c_str());

      g_isSettingClock = false;
    }
#pragma endregion
#pragma region Normal operation
    else
    {
      // Normal operation. Move minute hand every minute. Check for color changes.

      // Process special colors triggered by timers
      if (g_sparkleTimerTriggered) // Set a random led to a random color
      {
        int setColor = random(256) + (random(256) << 8) + (random(256) << 16);
        long led = random(NUM_LEDS);
        g_leds[led] = setColor;
        FastLED.show();

        g_sparkleTimerTriggered = false;
      }
      else if (g_randomTimerTriggered) // Set all leds to a random color
      {
        long newColor = random(256) + (random(256) << 8) + (random(256) << 16);
        for (int dot = 0; dot < NUM_LEDS; dot++)
        {
          g_leds[dot] = newColor;
          FastLED.show();
        }
        g_randomTimerTriggered = false;
      }

      // If a minute has passed
      int localMinute = getMinute();
      if (localMinute != g_lastMinute)
      {
        g_lastMinute = localMinute;
        int localHour = getHour();
        g_clockTime = formatTime(localHour, localMinute);

        ws.cleanupClients(); // may need to be handled more often (see ESAsyncWebServer GitHub page)

        writeLineDisplay(0, 2, getTime("%r"));                   // Display time on clock LED
        writeFile(SPIFFS, e_clockTimePath, g_clockTime.c_str()); // Save time to EEPROM

        moveMinute(); // Move the clock one minute forward.
        // Serial.printf("Minute passed. Clock time: %s\n", g_clockTime.c_str());

        // if ((localHour == 3) && (localMinute == 0) ) // test to trigger every hour
        // {
        //   // At 3:00 AM...
        //   //    Check for time discrepancies, reset clock if difference.
        //   //    This could be processor clock drift or seasonal time change.
        //   setupDateTime(g_ntpPool, g_timezone); // Update the time from the Internet

        //   // Move the clock if local time is not equal to the perceived clock time
        //   // Should only happen during seasonal time changes
        //   if ((localHour != getHour()) || (localMinute != getMinute()))
        //   {
        //     char msg[50];
        //     sprintf(msg, "Local: %s Clock: %s", formatTime(getHour(), getMinute()), g_clockTime);
        //     writeLog("CLOCKFIX", msg);
        //     g_isSettingClock = true;
        //   }
        // }
        // Serial.printf("Time check. Local: %s", getTime("%r"));
        // setupDateTime(g_ntpPool, g_timezone);
        // Serial.printf(", Updated: %s\n", getTime("%r"));
        // Update the web page with times
        ws.textAll("localTime##" + getTime("%r"));
        ws.textAll("clockTime##" + g_clockTime);

        // Look for lighting schedule trigger
        for (int i = 0; i < g_scheduleCount; i++)
        {
          // Serial.printf("Check schedule. localtm: %s, time: %s, color: %s\n", localtm, g_schedules[i].sTime, g_schedules[i].sColor);
          if (g_schedules[i].sTime == g_clockTime)
          {
            g_ledLevel = g_schedules[i].iLevel;
            g_ledColor = g_schedules[i].sColor;
            writeFile(SPIFFS, e_ledLevelPath, ledLevelNumber(g_ledLevel).c_str());
            writeFile(SPIFFS, e_ledColorPath, g_ledColor.c_str());
            g_lighting = "Color: " + g_ledColor + " Level: " + ledLevelNumber(g_ledLevel);
            writeLineDisplay(0, 1, g_ledColor + ", " + ledLevelNumber(g_ledLevel));
            writeLog("SETLIGHT", g_lighting);

            setupLighting();
          }
        }
      }
    }
#pragma endregion
  }
  else
  {
    // AP mode will timeout after
    delay(250);
    g_apTimeoutMin--;
    if (g_apTimeoutMin < 0)
    {
      ESP.restart();
    }
  }
}

#pragma region Function definitions

#pragma region Date/Time
String getTime(const char *format)
{
  // Get the the current time in the requested 'format'
  struct tm timeinfo;
  char text[64];
  const char *f = format;
  if (!f)
  {
    f = "%c"; // default to standard date time in locale format
  }
  getLocalTime(&timeinfo);
  size_t txtSize = strftime(text, sizeof(text), f, &timeinfo);
  if (txtSize == 0)
  {
    return "";
  }
  return String(text);
}
int getSecond()
{
  return getTime("%S").toInt();
}
int getMinute()
{
  return getTime("%M").toInt();
}
int getHour()
{
  return getTime("%H").toInt();
}
int getYear()
{
  return getTime("%Y").toInt();
}
void setTimezone(String timezone)
{
  // Serial.printf("  Setting Timezone to %s\n", timezone.c_str());
  setenv("TZ", timezone.c_str(), 1); //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
}
void setupDateTime(String ntpPool, String timezone)
{
  configTime(0, 0, ntpPool.c_str()); // First connect to NTP server, with 0 TZ offset

  struct tm timeInfo;
  if (!getLocalTime(&timeInfo))
  {
    Serial.println("Failed to get time from server.");
    // Reboot system
    ESP.restart();
  }
  else
  {
    setTimezone(timezone);
    // Serial.printf("Check internet time: %s\n", getTime("%r"));
    // Serial.print("Local time is: ");
    // Serial.println(&timeInfo, "%A, %B %d %Y %H:%M:%S");
    // Serial.printf("%r format: %s\n", getTime("%r"));
  }
}
void determineMinuteSteps()
{
  g_stepsMinute = g_stepsHour / 60;

  // if the steps/hour is not evenly divisible by 60
  //   then we need to compensate during the hour
  int remainder = g_stepsHour % 60;
  g_addStepsFive = (remainder >= 12) ? remainder / 12 : 0;
  g_addStepsHour = remainder % 12;
  // Serial.printf("g_stepsHour: %d, g_stepsMinute: %d\n", g_stepsHour, g_stepsMinute);
  // Serial.printf("g_addStepsFive: %d, g_addStepsHour: %d\n", g_addStepsFive, g_addStepsHour);
}
String formatTime(int hr, int min)
{
  char clockTime[20];
  sprintf(clockTime, "%02d:%02d", hr, min);
  return clockTime;
}
#pragma endregion
#pragma region Hardware timers
void IRAM_ATTR onTimerSparkle()
{
  g_sparkleTimerTriggered = true;
}
void IRAM_ATTR onTimerRandom()
{
  g_randomTimerTriggered = true;
}
#pragma endregion
#pragma region File
void initSPIFFS()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}
String readFile(fs::FS &fs, const char *path, String defValue)
{
  // Read File from SPIFFS write defValue if empty
  // Serial.print("Reading file with default... ");

  String fileContent;

  fileContent = readFile(fs, path);

  if (fileContent == "")
  {
    writeFile(SPIFFS, path, defValue.c_str());
    // Serial.println();
    return defValue;
  }

  return fileContent;
}
String readFile(fs::FS &fs, const char *path)
{
  // Read File from SPIFFS
  // Serial.printf("reading: %s ", path);

  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.printf("Error reading file: %s\n", path);
    return "";
  }

  String fileContent;
  while (file.available())
  {
    String sTmp = file.readString();
    fileContent += sTmp;
  }

  // Serial.println();
  return fileContent;
}
void writeFile(fs::FS &fs, const char *path, const char *message)
{
  // Write file to SPIFFS
  // Serial.printf("Writing file: %s ", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.printf("Error writing file: %s\n", path);
    return;
  }
  file.print(message);
  // {
  //   Serial.println("- file written");
  // }
  // else
  // {
  //   Serial.println("- write failed");
  // }
  file.close();
}
#pragma endregion

int cmpSched(const void *a, const void *b)
{
  const sched_type *schedA = (const sched_type *)a;
  const sched_type *schedB = (const sched_type *)b;
  String sA = schedA->sTime;
  String sB = schedB->sTime;
  // Serial.printf("sA: [%s], sB: [%s]\n", sA, sB);
  sA.remove(sA.indexOf(":"), 1);
  sB.remove(sB.indexOf(":"), 1);
  // Serial.printf("sA: [%s], sB: [%s]\n", sA, sB);
  // Serial.printf("sA.int: [%d], sB.int: [%d]\n", sA.toInt(), sB.toInt());

  // return (sA.toInt() > sB.toInt());
  int ia = sA.toInt();
  int ib = sB.toInt();

  return (ia > ib) - (ia < ib);
}
bool setupWiFi()
{
  if ((g_ssid == "") || g_pass == "")
  {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());
  Serial.print("WiFi Connecting...");
  unsigned long currentTick = millis();
  unsigned long previousTick = currentTick;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    currentTick = millis();
    if (currentTick - previousTick >= wifiTimeout)
    {
      Serial.println("WiFi failed to connect");
      return false;
    }
  }
  Serial.println(WiFi.localIP());
  return true;
}
String processor(const String &var)
{
  // Replaces the text between %match% in spiffs index.html on upload with actual variables

  if (var == "STEPSPERHOUR")
  {
    // String sRet = g_stepsHour.c_str();
    return String(g_stepsHour);
  }
  if (var == "CYCLES")
  {
    return String(g_stepTestCycles);
  }
  if (var == "LOCALTIME")
  {
    return getTime("%r").c_str();
  }
  if (var == "SCHEDULEDISPLAYXX")
  {
    return formatSchedule();
  }
  if (var == "TIMEZONESELECT")
  {
    String options = "";
    for (int i = 0; i < 10; i++)
    {
      options += "<option ";
      if (g_selectTimeZones[i][0] == g_timezone)
        options += "selected ";
      options += "value='" + g_selectTimeZones[i][0] + "'>" + g_selectTimeZones[i][1] + "</option>";
    }
    return options;
  }
  if (var == "COLORSELECT")
  {
    String options = "";
    for (int i = 0; i < 10; i++)
    {
      options += "<option ";
      if (g_selectColor[i][0] == g_ledColor)
        options += "selected ";
      options += "value='" + g_selectColor[i][0] + "'>" + g_selectColor[i][1] + "</option>";
    }
    return options;
  }
  if (var == "LEVELSELECT")
  {
    String options = "";
    for (int i = 0; i < 11; i++)
    {
      options += "<option ";
      if (g_selectLevel[i][0] == String(g_ledLevel))
        options += "selected ";
      options += "value='" + g_selectLevel[i][0] + "'>" + g_selectLevel[i][1] + "</option>";
    }
    return options;
  }
  if (var == "LOGFILEXX")
  {
    String logs = readFile(SPIFFS, e_logfilePath);
    return formatLogs(logs);
  }
  if (var == "SYSINFOXX")
  {
    return formatSysinfo();
  }
  if (var == "CLOCKNAME")
  {
    return readFile(SPIFFS, e_clocknmPath);
  }
  return String();
}
void rotate(int iStep)
{
  // Serial.printf("Entering rotate()...steps: %d\n", step);
  int iLastSec = 0;
  if (iStep > 0)
    g_steps_plus_completed += iStep;
  else
    g_steps_minus_completed += iStep;

  static int phase = 0;
  int i, j;
  int delta = (iStep > 0) ? 1 : 7;

  iStep = (iStep > 0) ? iStep : -iStep;
  for (j = 0; j < iStep; j++)
  {
    phase = (phase + delta) % 8;
    for (i = 0; i < 4; i++)
    {
      digitalWrite(g_stepperports[i], g_steppersequence[phase][i]);
    }
    delay(g_stepperDelaytime);
    // int iSecNow = getSecond();
    // if(iLastSec != iSecNow){
    //   Serial.printf("Switch: %d\n", digitalRead(SWITCH_PIN));
    //   iLastSec = iSecNow;
    // }
  }
  // power cut
  for (i = 0; i < 4; i++)
  {
    digitalWrite(g_stepperports[i], LOW);
  }
  // Serial.println("Exiting rotate()!");
}
void moveMinute()
{
  int localHour = getHour();
  int localMinute = getMinute();
  int localSecond = getSecond();

  // Determine how many motor steps to take.
  //   The number of steps/revolution may not be evenly divisible by 60.
  //   So we divide any remainder by 12 to add every 5 minutes.
  //   Any remainder from that will be added at the hour.
  //   See the determinMinuteSteps() function.
  int steps = g_stepsMinute;
  // Add 5 minute count offset
  if ((localMinute % 5) == 0)
  {
    steps += g_addStepsFive;
  }
  // Add hour count offset
  if (localMinute == 0)
  {
    steps += g_addStepsHour;
  }

  rotate(steps);
}
long hstol(String sHex) // Hex string to long
{
  char c[sHex.length() + 1];
  sHex.toCharArray(c, sHex.length() + 1);
  return strtol(c, NULL, 16);
}
long coltolong(String sColor) // Color to long value
{
  long lRet = 0;
  if (sColor == "White")
    lRet = 0xFFFFFF;
  else if (sColor == "Red")
    lRet = 0xFF0000;
  else if (sColor == "Orange")
    // lRet = 0xFFA500;
    lRet = 0xF55500;
  else if (sColor == "Yellow")
    lRet = 0xFFFF00;
  else if (sColor == "Green")
    lRet = 0x00FF00;
  else if (sColor == "Blue")
    lRet = 0x0000FF;
  else if (sColor == "Indigo")
    lRet = 0x4B0082;
  else if (sColor == "Violet")
    lRet = 0xEE82EE;

  return lRet;
}
void writeLog(String sTitle, String sContent)
{
  String sDate = getTime("%D %T");
  String sLogEntry = sDate + "#" + sTitle + "#" + sContent;

  String sLogs = readFile(SPIFFS, e_logfilePath);
  String sNewLogs = sLogEntry + "##" + sLogs;
  writeFile(SPIFFS, e_logfilePath, sNewLogs.c_str());
  ws.textAll("showLogs##" + formatLogs(sNewLogs));
}
void setupLighting()
{
  FastLED.setBrightness(0);

  if (g_ledColor != "") // If we are in any color mode (not off)
  {
    // Set the brighness level
    FastLED.setBrightness(g_ledLevel);

    // Default to a single 'normal' color
    timerStop(g_SparkleTimer);
    timerStop(g_RandomTimer);
    g_sparkleTimerTriggered = false;
    g_randomTimerTriggered = false;
    g_ledMode = Normal;

    // We are in 'Random' color mode
    if (g_ledColor == "Random")
    {
      // Serial.println("Trying to start Random");
      g_ledMode = Random;
      g_randomTimerTriggered = false;
      timerStart(g_RandomTimer);
    }
    // We are in 'Sparkle' color mode
    else if (g_ledColor == "Sparkle")
    {
      // Serial.println("Trying to start Sparkle");
      g_ledMode = Sparkle;
      g_sparkleTimerTriggered = false;
      timerStart(g_SparkleTimer);
    }
    // We are in single color mode
    else // set the 'normal' color and brightness
    {
      long col = coltolong(g_ledColor);
      for (int dot = 0; dot < NUM_LEDS; dot++)
      {
        g_leds[dot] = col;
        FastLED.show();
      }
    }
  }
}
String format12hr(String sTime)
{
  if (sTime == "00:00")
    return "12:00 AM";
  int hr = sTime.substring(0, 2).toInt();
  String min = sTime.substring(2);
  // Serial.printf("min: [%s]\n", min.c_str());
  String meridiem = " AM";
  // Serial.printf("Raw: [%s], hr: [%d], min: [%s]\n", t.c_str(), hr, min.c_str());
  if (hr > 11)
    meridiem = " PM";
  hr = hr % 12;
  if (hr == 0)
    hr = 12;
  return String(hr) + min + meridiem;
}
String formatLogs(String logs)
{
  // Serial.printf("Logs: %s\n", logs.c_str());
  String sRet = "";
  sRet += "<table id='logsTable'><tr><th>Date</th><th>Code</th><th>Value</th></tr>";
  String sLine = "";

  do
  {
    int lineIdx = logs.indexOf("##");

    sLine = logs.substring(0, lineIdx);
    int valIdx = sLine.indexOf("#");
    String sVal1 = sLine.substring(0, valIdx);

    sLine = sLine.substring(valIdx + 1);
    valIdx = sLine.indexOf("#");
    String sVal2 = sLine.substring(0, valIdx);

    sLine = sLine.substring(valIdx + 1);
    valIdx = sLine.indexOf("#");
    String sVal3 = sLine.substring(0, valIdx);

    sRet += "<tr><td>" + sVal1 + "</td><td>" + sVal2 + "</td><td>" + sVal3 + "</td></tr>";

    logs = logs.substring(lineIdx + 2);
    lineIdx = logs.indexOf("##");
    sLine = logs.substring(0, lineIdx);
  } while (sLine != "");

  sRet += "</table>";

  // Serial.printf("Logs table: %s\n", sRet.c_str());
  return sRet;
}
String formatSchedule()
{

  // for (int x = 0; x < g_scheduleCount; x++)
  // {
  //   Serial.printf("%s, %s, %d\n", g_schedules[x].sTime, g_schedules[x].sColor, g_schedules[x].iLevel);
  // }

  qsort(g_schedules, g_scheduleCount, sizeof(sched_type), cmpSched);

  String sched = "";
  sched += "<table id='scheduleTable'><tr><th>Time</th><th>Color</th><th>Level</th><th>Remove</th></tr>";
  for (int i = 0; i < g_scheduleCount; i++)
  {
    // Serial.printf("time: [%s]\n", g_schedules[i].sTime.c_str());
    sched += "<tr>";
    sched += "<td>" + format12hr(g_schedules[i].sTime) + "</td>";
    sched += "<td>" + g_schedules[i].sColor + "</td>";
    sched += "<td>" + lightBrightnessToLevel(g_schedules[i].iLevel) + "</td>";
    sched += "<td><button type='button' class='btn btn-outline-danger btn-sm' id='rem" + String(i) + "' onclick='remSched(this)'>Delete</button></td>";
    sched += "</tr>";
  }
  sched += "</table>";
  return sched;
}
String formatSysinfo()
{
  String sTZ = "";
  for (int i = 0; i < 10; i++)
    if (g_selectTimeZones[i][0] == g_timezone)
    {
      sTZ = g_selectTimeZones[i][1];
      break;
    }

  String sSMP = "";
  for (int i = 0; i < 4; i++)
    sSMP += "[" + String(g_stepperports[i]) + "] ";

  char sClockTime[10] = "";
  sprintf(sClockTime, "%02d:%02d", getHour(), getMinute());
  char sVersion[15];
  sprintf(sVersion, "%02d.%02d", g_ver_major, g_ver_minor);

  String sLighting = "Color: " + g_ledColor + ", Level: " + ledLevelNumber(g_ledLevel);

  String sRet = "";
  sRet += "<table id='sysInfoTable'><tr><th>Description</th><th>Value</th></tr>";
  sRet += "<tr><td>Version</td><td>" + String(sVersion) + "</td></tr>";
  sRet += "<tr><td>Time zone</td><td>" + sTZ + "</td></tr>";
  sRet += "<tr><td>Clock time</td><td>" + String(sClockTime) + "</td></tr>";
  sRet += "<tr><td>Local time</td><td>" + getTime("%r") + "</td></tr>";
  sRet += "<tr><td>WiFi ssid</td><td>" + g_ssid + "</td></tr>";
  sRet += "<tr><td>Current lighting</td><td>" + sLighting + "</td></tr>";
  sRet += "<tr><td>Config steps/hr</td><td>" + String(g_stepsHour) + "</td></tr>";
  sRet += "<tr><td>Config cycle count</td><td>" + String(g_stepTestCycles) + "</td></tr>";
  sRet += "<tr><td>Motor steps/minute</td><td>" + String(g_stepsMinute) + "</td></tr>";
  sRet += "<tr><td>Motor steps added every 5 minutes</td><td>" + String(g_addStepsFive) + "</td></tr>";
  sRet += "<tr><td>Motor steps added every hour</td><td>" + String(g_addStepsHour) + "</td></tr>";
  sRet += "<tr><td>Stepper motor pins</td><td>" + sSMP + "</td></tr>";
  sRet += "<tr><td>Lighting pin</td><td>[" + String(LED_PIN) + "]</td></tr>";
  sRet += "<tr><td>Display pins</td><td>SDA:[" + String(OLED_SDA) + "] SCL:[" + String(OLED_SCL) + "]</td></tr>";
  sRet += "<tr><td>Chip model</td><td>" + String(ESP.getChipModel()) + "</td></tr>";
  sRet += "<tr><td>Chip revision</td><td>" + String(ESP.getChipRevision()) + "</td></tr>";
  sRet += "<tr><td>Chip cores</td><td>" + String(ESP.getChipCores()) + "</td></tr>";
  sRet += "<tr><td>CPU frequency</td><td>" + String(ESP.getCpuFreqMHz()) + " Mhz</td></tr>";
  sRet += "<tr><td>Heap size</td><td>" + String(ESP.getHeapSize()) + "</td></tr>";
  sRet += "<tr><td>Heap free</td><td>" + String(ESP.getFreeHeap()) + "</td></tr>";
  sRet += "<tr><td>Min free heap</td><td>" + String(ESP.getMinFreeHeap()) + "</td></tr>";
  sRet += "<tr><td>Max alloc heap</td><td>" + String(ESP.getMaxAllocHeap()) + "</td></tr>";
  sRet += "<tr><td>SDK version</td><td>" + String(ESP.getSdkVersion()) + "</td></tr>";
  sRet += "<tr><td>Flash chip size</td><td>" + String(ESP.getFlashChipSize()) + "</td></tr>";
  sRet += "<tr><td>Sketch size</td><td>" + String(ESP.getSketchSize()) + "</td></tr>";
  sRet += "<tr><td>Sketch free</td><td>" + String(ESP.getFreeSketchSpace()) + "</td></tr>";
  sRet += "</table>";
  return sRet;
}
#pragma region
void deleteSchedule(int iIdx)
{
  for (int i = 0; i < g_scheduleCount; i++)
  {
    if (i >= iIdx)
    {
      g_schedules[i].sTime = g_schedules[i + 1].sTime;
      g_schedules[i].sColor = g_schedules[i + 1].sColor;
      g_schedules[i].iLevel = g_schedules[i + 1].iLevel;
    }
  }
  g_scheduleCount--;
}
void writeLightSchedule()
{
  String sched = "";
  for (int i = 0; i < g_scheduleCount; i++)
  {
    sched += g_schedules[i].sTime + "&";
    sched += g_schedules[i].sColor + "&";
    sched += String(g_schedules[i].iLevel) + "|";
  }
  // Serial.printf("Write scshed file: %s\n", sched.c_str());
  writeFile(SPIFFS, e_schedulePath, sched.c_str());
}
void readLightSchedule()
{
  String sched = readFile(SPIFFS, e_schedulePath);
  // Serial.printf("Read sched file: [%s]\n", sched.c_str());
  int iCount = 0;
  int idx = 0;

  idx = sched.indexOf("|");
  String rec = sched.substring(0, idx);
  while (rec.length() > 0)
  {
    // Serial.printf("sched: [%s]\n", sched.c_str());
    // Serial.printf("rec: [%s]\n", rec.c_str());

    int i = rec.indexOf("&");
    g_schedules[iCount].sTime = rec.substring(0, i);

    rec = rec.substring(i + 1);
    i = rec.indexOf("&");
    g_schedules[iCount].sColor = rec.substring(0, i);

    rec = rec.substring(i + 1);
    g_schedules[iCount].iLevel = rec.substring(0, i).toInt();

    // Serial.printf("time: [%s], color: [%s], level: [%d]\n", g_schedules[iCount].sTime.c_str(), g_schedules[iCount].sColor.c_str(), g_schedules[iCount].iLevel);

    iCount++;
    sched = sched.substring(idx + 1);
    idx = sched.indexOf("|");
    rec = sched.substring(0, idx);
  }

  g_scheduleCount = iCount;
}
#pragma endregion
void listDir(const char *sDir)
{
  File root = SPIFFS.open(sDir);
  File file = root.openNextFile();
  while (file)
  {
    Serial.printf("FILE: %s - size: %d\n", file.name(), file.size());
    file = root.openNextFile();
  }
}
String ledLevelNumber(int lvl)
{
  String sRet = "";
  for (int i = 0; i < 11; i++)
  {
    if (String(lvl) == g_selectLevel[i][0])
    {
      sRet = g_selectLevel[i][1];
      break;
    }
  }
  return sRet;
}
void writeLineDisplay(int col, int line, String txt)
{
  // limit 3 lines, space between lines
  int x = col * 5;
  int y = line * 10; // not sure about this but must be font with set width
  display.setCursor(x, y);
  txt.trim();
  display.print(txt);
  for (int i = 0; i < (21 - txt.length()); i++)
    display.print(" ");
  display.display();
}
#pragma region Utils
String lightBrightnessToLevel(int iLevel)
{
  switch (iLevel)
  {
  case 255:
    return "10";
    break;
  case 200:
    return "9";
    break;
  case 150:
    return "8";
    break;
  case 100:
    return "7";
    break;
  case 80:
    return "6";
    break;
  case 60:
    return "5";
    break;
  case 25:
    return "4";
    break;
  case 8:
    return "3";
    break;
  case 3:
    return "2";
    break;
  case 1:
    return "1";
    break;
  case 0:
    return "Off";
    break;
  default:
    return "ERR";
  }
}
#pragma endregion
#pragma region Web socket
void notifyClients()
{
  // ws.textAll(String(ledState));
  ws.textAll("Hello");
}
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;

    String msg = (char *)data;
    int idx = msg.indexOf("##");
    String cmd = msg.substring(0, idx);
    String val = msg.substring(idx + 2);

    // Serial.printf("cmd: %s, val: %s\n", cmd.c_str(), val.c_str());
    if (cmd == "refreshTime") // Refresh the local time display
    {
      String sTemp = getTime("%r");
      ws.textAll("localTime##" + sTemp);
    }
    else if (cmd == "prepConfig")
    {
      g_prepConfigStepping = true;
    }
    else if (cmd == "startConfig") // Start a step/hour test
    {
      idx = val.indexOf("&");
      String steps = val.substring(0, idx);
      String cycles = val.substring(idx + 1);
      g_stepsHour = steps.toInt();
      writeFile(SPIFFS, e_stepsHourPath, steps.c_str());
      g_stepTestCycles = cycles.toInt();
      writeFile(SPIFFS, e_testCyclesPath, cycles.c_str());
      g_startConfigStepping = true;
    }
    else if (cmd == "stopConfig") // Stop the step/hor test
    {
      g_prepConfigStepping = false;
      g_startConfigStepping = false;
    }
    else if (cmd == "cw")
    {
      rotate(20);
    }
    else if (cmd == "ccw")
    {
      rotate(-20);
    }
    else if (cmd == "setClock")
    {
      g_clockTime = val;
      g_isSettingClock = true;
    }
    else if (cmd == "timezone")
    {
      if (val.startsWith("'"))
      {
        val = val.substring(1, val.length() - 1);
      }
      writeLog("TIMEZONE", "From:" + g_timezone + " To:" + val);
      g_timezone = val;
      writeFile(SPIFFS, e_timezonePath, g_timezone.c_str());
      g_clockTime = String(getHour()) + ":" + String(getMinute());
      // writeFile(SPIFFS, e_changeTZPath, g_clockTime.c_str());
      setupDateTime(g_ntpPool, g_timezone);
      g_isSettingClock = true;

      // Wait until next minute to give full minute for reboot
      // while (getSecond != 0)
      // {
      // }
      // moveMinute();
      // delay(5000); // Let async complete
      // ESP.restart();
    }
    else if (cmd == "color")
    {
      idx = val.indexOf("&");
      String col = val.substring(0, idx);
      String lvl = val.substring(idx + 1);

      g_ledColor = col;
      g_ledLevel = lvl.toInt();

      writeFile(SPIFFS, e_ledColorPath, g_ledColor.c_str());
      char sLevel[10];
      itoa(g_ledLevel, sLevel, 10);
      writeFile(SPIFFS, e_ledLevelPath, sLevel);

      // Serial.printf("New color: [%s], level: [%d]\n", g_ledColor, g_ledLevel);
      g_lighting = "Color: " + g_ledColor + " Level: " + ledLevelNumber(g_ledLevel);
      writeLineDisplay(0, 1, g_ledColor + ", " + ledLevelNumber(g_ledLevel));
      writeLog("SETLIGHT", g_lighting);

      setupLighting();
    }
    else if (cmd == "showlogs")
    {
      String logs = readFile(SPIFFS, e_logfilePath);
      // Serial.printf("Listing logfiles...%s\n", logs);
      ws.textAll("showLogs##" + formatLogs(logs));
    }
    else if (cmd == "addSchedule")
    {
      idx = val.indexOf("&");
      String time = val.substring(0, idx);
      val = val.substring(val.indexOf("&") + 1);
      idx = val.indexOf("&");
      String color = val.substring(0, idx);
      val = val.substring(val.indexOf("&") + 1);
      idx = val.indexOf("&");
      String level = val.substring(0, idx);
      // Serial.printf("time: %s, color: %s, level: %s\n", time, color, level);
      bool isDuplicate = false;
      for (int i = 0; i < g_scheduleCount; i++)
      {
        if (g_schedules[i].sTime == time)
        {
          isDuplicate = true;
          break;
        }
      }
      if (!isDuplicate)
      {
        g_schedules[g_scheduleCount++] = (sched_type){
            time, color, level.toInt()};
        // Serial.printf("schedule: %s\n", sched.c_str());
        writeLog("ADDSCHED", time + "," + color + ", " + level);
        writeLightSchedule();
      }
      // Serial.printf("isDuplicate: %d, time: %s\n", isDuplicate, time);
      ws.textAll("schedule##" + formatSchedule());
    }
    else if (cmd == "remSchedule")
    {
      int idx = val.toInt();
      String time = g_schedules[idx].sTime;
      String color = g_schedules[idx].sColor;
      String level = String(g_schedules[idx].iLevel);
      writeLog("REMSCHED", time + "," + color + ", " + level);

      deleteSchedule(val.toInt());
      writeLightSchedule();
      ws.textAll("schedule##" + formatSchedule());
    }
    else if (cmd == "refreshSchedule")
    {
      ws.textAll("schedule##" + formatSchedule());
    }
    else if (cmd == "delLogs")
    {
      SPIFFS.remove(e_logfilePath);
      ws.textAll("showLogs##No Logs");
    }
    else if (cmd == "refreshSysinfo")
    {
      // Serial.println("trying to refresh system info");
      String sysInfo = formatSysinfo();
      ws.textAll("refreshSysinfo##" + sysInfo);
    }
    else if (cmd == "clearWiFi")
    {
      SPIFFS.remove(e_passPath);
      SPIFFS.remove(e_ssidPath);
      delay(3000);
      ESP.restart();
    }
    else if (cmd == "clockName")
    {
      writeFile(SPIFFS, e_clocknmPath, val.c_str());
      ws.textAll("clockName##" + val);
    }
  }
}
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}
void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}
#pragma endregion
#pragma endregion

// Half step example
// const int IN1 = 2;
// const int IN2 = 3;
// const int IN3 = 4;
// const int IN4 = 5;
// const int totalSteps = 4096;
// int i = 0;
// void setup()
// {
//   pinMode(IN1, OUTPUT);
//   pinMode(IN2, OUTPUT);
//   pinMode(IN3, OUTPUT);
//   pinMode(IN4, OUTPUT);
// }
// void loop()
// {
//   for (int stepCount = 0; stepCount < totalSteps; stepCount++)
//   {
//     setHalfStep();
//     delayMicroseconds(1200);
//   }
// }
// void setHalfStep()
// {
//   int steps[] = {B1000, B1100, B0100, B0110, B0010, B0011, B0001, B1001};
//   digitalWrite(IN1, bitRead(steps[i], 0));
//   digitalWrite(IN2, bitRead(steps[i], 1));
//   digitalWrite(IN3, bitRead(steps[i], 2));
//   digitalWrite(IN4, bitRead(steps[i], 3));
//   i = (i + 1) % 8;
// }