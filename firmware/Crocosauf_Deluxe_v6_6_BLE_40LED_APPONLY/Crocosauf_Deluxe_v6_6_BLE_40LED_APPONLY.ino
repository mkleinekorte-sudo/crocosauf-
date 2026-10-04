/*
  CROCOSAUF DELUXE v6.6-BLE-40LED-APPONLY
  Grundlage: Marcels Crocosauf_Deluxe_v6_4_BLE(1).ino (40 LEDs).
  Originale LED-Paare und 20 Lichteffekte; 20 Displayanimationen.
  Android-App 1.1; keine WebUI und kein WLAN.

  NEU: App fuer Helligkeiten 0..100%, Rotation, Audio an/aus,
  Start- und Welcome-Lautstaerke, Reminder-Dauer bis 30 Sekunden,
  Schlaf Aus/15/30/60 Minuten. Standard: Schlaf AUS.
  Zweifach-LED-Pinchen, zehn unterschiedliche Ziehungen; je Serie neu gemischt.
  Getrennte Soundzaehler und serialisierte DFPlayer-Befehle.
  Fehlende SD/BUSY blockiert das Spiel nicht dauerhaft.
  I2C-Timeout, Display-Wiederanlauf, 12-Sekunden-Loop-Watchdog.
  Optionaler Schlaf mit Sensor + Timer und Freigabe des RTC-Pins beim Aufwachen.

  Pins: LEDs 18; Maul 27 HIGH=offen; Modus 25 LOW=Pinchen; Taste 26.
  DFPlayer: RX des ESP32=16, TX=17, BUSY=33 LOW=spielt.
  Matrix HT16K33: SDA=21, SCL=22, Adresse=0x70.
  SD FAT32: Root 001..012.mp3; /02/ GameOver; /03/ Welcome;
  /04/ Reminder; /05/ Neue Runde. Dateinummern dreistellig.
  Root benutzt den globalen DFPlayer-Dateiindex. Optional GAME_MUSIC_FOLDER=1
  und Spielmusik in /01/001.mp3 usw.
  BLE: Crocosauf Deluxe; pro Verbindung Maul zu, Taste 3 Sekunden halten.
  Arduino: ESP32 Dev Module, Core 3.3.7, Huge APP (3MB No OTA/1MB SPIFFS).
  Libraries: FastLED, DFPlayerMini_Fast, FireTimer, Adafruit GFX,
  Adafruit LED Backpack, Adafruit BusIO.
*/

#define SKETCH_NAME "Crocosauf Deluxe"
#define SKETCH_VERSION "v6.6-BLE-40LED-APPONLY"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <FastLED.h>
#include <DFPlayerMini_Fast.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp32-hal-cpu.h"
#include "driver/rtc_io.h"

#define LED_PIN 18
#define NUM_LEDS 40
#define MAUL_SENSOR_PIN 27
#define VOLUME_TASTER 26
#define MODUS_PIN 25
#define I2C_SDA 21
#define I2C_SCL 22
#define DFPLAYER_RX_PIN 16
#define DFPLAYER_TX_PIN 17
#define DFPLAYER_BUSY_PIN 33

#define GAME_MUSIC_FOLDER 0
#define FOLDER_GAMEOVER 2
#define FOLDER_SYSTEM 3
#define FOLDER_REMINDER 4
#define FOLDER_NEWRND 5

const int ROOT_GAME_TRACK_COUNT=12, GAMEOVER_TRACK_COUNT=12;
const int WELCOME_TRACK_COUNT=6, REMINDER_TRACK_COUNT=6, NEWRND_TRACK_COUNT=5;
const int EFFECT_COUNT=20, DISP_ANIM_COUNT=20, PINCHEN_TOTAL=10;

const uint32_t ALL_EFFECTS=0x000FFFFF;
const uint32_t DEFAULT_REM_EFFECTS=
  (1UL<<1)|(1UL<<4)|(1UL<<10)|(1UL<<11)|(1UL<<15)|(1UL<<18);

const uint16_t DEFAULT_GAME_MASK=0x003F;
const uint16_t DEFAULT_GAMEOVER_MASK=0x003F;
const uint8_t DEFAULT_WELCOME_MASK=1;
const uint8_t DEFAULT_REMINDER_MASK=0x3F;
const uint8_t DEFAULT_NEWRND_MASK=1;

const uint32_t WIFI_AUTO_OFF_MS=300000;
const uint32_t DEEP_SLEEP_AFTER_MS=1800000; // legacy reference for tests / documentation
const uint32_t DF_COMMAND_GAP_MS=200;
const uint32_t SLEEP_CHECK_MS=60000;
const uint16_t I2C_TIMEOUT_MS=25;
const uint32_t OPEN_CONFIRM_MS=900;
const uint32_t RED_PHASE_DURATION=4000;
const uint32_t PINCHEN_PHASE_DURATION=4000;
const uint32_t GRUEN_PHASE_DURATION=5200;
const uint32_t NORMAL_GAMEOVER_CUP_MS=5000;
const uint32_t effectInterval=4000;
const uint32_t WELCOME_START_TIMEOUT_MS=5000;
const uint32_t WELCOME_MAX_MS=180000;
const uint32_t UI_TEST_TIMEOUT_MS=60000;
const uint16_t PINCHEN_BLINK_INTERVAL=200;
const uint16_t PINCH_NUM_BLINK_MS=260;


Preferences prefs;
HardwareSerial mp3Serial(2);
DFPlayerMini_Fast player;
CRGB leds[NUM_LEDS];
Adafruit_8x8matrix matrix;

uint16_t gameTrackMask=DEFAULT_GAME_MASK;
uint16_t gameOverMask=DEFAULT_GAMEOVER_MASK;
uint8_t welcomeMask=1, reminderMask=0x3F, newRndMask=1;
uint32_t gameEffectMask=ALL_EFFECTS;
uint32_t reminderEffectMask=DEFAULT_REM_EFFECTS;
uint32_t gameDispMask=ALL_EFFECTS;

bool gameTracksRandom=true;
bool gameOverRandom=false;
bool welcomeRandom=false;
bool reminderSoundRandom=true;
bool newRndRandom=false;
bool gameEffectsRandom=false;
bool reminderEffectsRandom=true;
bool gameDispRandom=false;
bool reminderEnabled=true;
bool reminderSoundEnabled=true;

uint32_t reminderIntervalMs=480000;
uint16_t reminderEffectDurationMs=3500;
int volumeLevel=20;
bool isMuted=false;
String standbyText="CROCOSAUF DELUXE";
uint8_t ledBrightness=100, displayBrightness=100, displayRotation=0;
bool audioEnabled=true;
int startVolumeLevel=20, welcomeVolumeLevel=20;
uint32_t reminderLedDurationMs=3500;
uint16_t sleepMinutes=0; // Migration from v6.4 deliberately keeps the device awake.
bool matrixReady=false, loopWatchdogReady=false;
uint32_t matrixCheckedAt=0, matrixRecoveries=0;
String systemError="";
uint32_t bootResetReason=0;
RTC_DATA_ATTR uint32_t rtcReminderRemainingMs=0, rtcSleepSliceMs=0;

enum GameStage {
  READY,
  PLAYING,
  GAME_OVER,
  NEW_ROUND
};

GameStage stage=READY;
bool pinchenMode=false, maulOffen=false;
uint32_t stageStarted=0;
uint32_t lastUserActivity=0;
uint32_t lastReminderAt=0;
uint32_t maulOpenSince=0;

int rundeAktuell=1, pinnchenIndex=-1;
// Unveraenderte LED-Paare aus Marcels 40-LED-Upload; Indizes ab 0.
const uint8_t pinnchenLEDs[PINCHEN_TOTAL][2]={
  {2,3},{6,7},{11,12},{14,15},{17,18},{21,22},{24,25},{27,28},{31,32},{36,37}
};
uint8_t pinchenOrder[PINCHEN_TOTAL]={};
bool pinchenOrderValid=false;
uint16_t usedPinchenMask=0;

RTC_DATA_ATTR uint32_t rtcMagic=0;
RTC_DATA_ATTR uint16_t rtcUsed=0;
RTC_DATA_ATTR uint8_t rtcRound=1, rtcPinchenMode=0;

bool sleepAfterReminder=false;

bool reminderRunning=false, reminderShowCup=true;
uint32_t reminderStarted=0;
int reminderEffectId=0;

bool uiPlaying=false, uiEffectTesting=false, uiDispTesting=false;
int uiMode=0, uiCurrentTrack=0;
int uiEffectId=-1, uiDispId=-1;
uint32_t uiTestStarted=0;

enum AudioRole {
  AUDIO_NONE,
  AUDIO_GAME,
  AUDIO_WELCOME,
  AUDIO_ONCE,
  AUDIO_TEST
};

AudioRole audioRole=AUDIO_NONE;
bool dfplayerInitOk=false;
bool welcomeActive=false;
bool audioSawBusy=false;
bool audioIdleTracking=false;
bool gameAudioFailed=false;
bool volumeDirty=false;
bool audioStopPending=false, audioDisableRepeatPending=false;

uint8_t audioStep=0, audioFolder=0;
int audioTrack=0, currentGameMusicTrack=0;
uint32_t audioStepAt=0;
uint32_t audioStarted=0;
uint32_t audioIdleSince=0;
uint32_t lastAudioCommand=0;
String audioError="";

int lastGameTrack=0;
int lastGoTrack=0;
int lastWelcomeTrack=0;
int lastReminderTrack=0;
int lastNewTrack=0;
int lastGameEffect=-1;
int lastRemEffect=-1;
int lastGameShowPick=-1;

int currentEffect=0;
int effectPos=0;
int direction=1;
int hue=0;
int brightness=0;
int brightnessDelta=5;

unsigned long lastEffectSwitch=0;
unsigned long lastEffectUpdate=0;

enum DispMode {
  DISP_SCROLL,
  DISP_PINCH_BLINK,
  DISP_CUP_ANIM,
  DISP_VOLUME_BARS,
  DISP_MUTED_ICON,
  DISP_GAME_ANIM,
  DISP_REMINDER_ANIM,
  DISP_ORIENTATION
};

DispMode dispMode=DISP_SCROLL;
String dispText="CROCOSAUF DELUXE";
int16_t scrollX=8;
unsigned long dispLast=0, dispUntil=0;

uint8_t cupRepeatTarget=255, cupLevel=0, cupCycle=0;
bool cupHolding=false;
unsigned long cupLastStep=0, cupHoldStart=0;

uint8_t gameShowId=0, gameFrame=0;
unsigned long gameShowLastSwitch=0, gameFrameLast=0;
int forcedGameShow=-1;

uint8_t remAnimFrame=0, remBubbleX=6, remBubbleY=1;
unsigned long remAnimLast=0;

bool pinchBlinkOn=true;
unsigned long pinchBlinkLast=0;
int pinchBlinkTogglesLeft=0;
uint32_t normalCupUntil=0;

struct DebouncedInput {
  uint8_t pin;
  bool raw=HIGH, stable=HIGH;
  uint32_t changedAt=0;
  uint16_t debounceMs;

  DebouncedInput(uint8_t p, uint16_t ms):
    pin(p), debounceMs(ms) {}

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    raw=stable=digitalRead(pin);
    changedAt=millis();
  }

  bool update() {
    bool value=digitalRead(pin);
    if(value!=raw) {
      raw=value;
      changedAt=millis();
    }
    if(uint32_t(millis()-changedAt)>=debounceMs) stable=raw;
    return stable;
  }
};

DebouncedInput reed(MAUL_SENSOR_PIN,35);
DebouncedInput modeInput(MODUS_PIN,40);
DebouncedInput volumeInput(VOLUME_TASTER,35);

static bool deadlineReached(uint32_t now,uint32_t end) {
  return int32_t(now-end)>=0;
}

static bool dfIsBusyPlaying() {
  return digitalRead(DFPLAYER_BUSY_PIN)==LOW;
}

static void requestAudio(AudioRole role,int folder,int track);
static void restoreDisplay();
static void stopUiTests();
static void stopReminder();
static void startGame();
static void enterDeepSleepNow();
static bool bluetoothHasClient();
static void stopBluetooth();
static void applyVisualSettings();
static void writeMatrixDisplay();



// ============================================================
// GROSSE ZIFFERN 3x7
// ============================================================

static const uint8_t DIG3x7[10][7] = {
  {0b111,0b101,0b101,0b101,0b101,0b101,0b111},
  {0b010,0b110,0b010,0b010,0b010,0b010,0b111},
  {0b111,0b001,0b001,0b111,0b100,0b100,0b111},
  {0b111,0b001,0b001,0b111,0b001,0b001,0b111},
  {0b101,0b101,0b101,0b111,0b001,0b001,0b001},
  {0b111,0b100,0b100,0b111,0b001,0b001,0b111},
  {0b111,0b100,0b100,0b111,0b101,0b101,0b111},
  {0b111,0b001,0b001,0b001,0b001,0b001,0b001},
  {0b111,0b101,0b101,0b111,0b101,0b101,0b111},
  {0b111,0b101,0b101,0b111,0b001,0b001,0b111}
};

static inline void drawDigit3x7(uint8_t d, int x, int y) {
  if(d>9) return;
  for(int r=0;r<7;r++) {
    uint8_t row=DIG3x7[d][r];
    for(int c=0;c<3;c++) {
      if(row & (1<<(2-c))) matrix.drawPixel(x+c,y+r,LED_ON);
    }
  }
}

static inline void drawNumberCentered(uint8_t n) {
  const int y=0;
  matrix.clear();
  if(n<=9) {
    drawDigit3x7(n,2,y);
  } else {
    drawDigit3x7(1,0,y);
    drawDigit3x7(0,4,y);
  }
  writeMatrixDisplay();
}

// ============================================================
// AUSWAHL UND EINSTELLUNGEN
// ============================================================

// Keine unmittelbare Wiederholung, sofern mehrere Eintraege aktiv sind.
static uint32_t gameRandomBelow(uint32_t limit) {
  if(limit<2) return 0;
  // Hardwarequelle, unabhaengig vom Animations-Zufall und von millis().
  const uint32_t threshold=(0U-limit)%limit;
  uint32_t value;
  do { value=esp_random(); } while(value<threshold);
  return value%limit;
}

static int pickEffectFromMask_0based(int count,uint32_t mask,bool rnd,int &last) {
  if(count<1 || count>32) return 0;
  const uint32_t valid=count==32 ? 0xFFFFFFFFUL : ((1UL<<count)-1);
  mask &= valid;
  if(!mask) mask=valid;
  if(!rnd) {
    for(int step=1;step<=count;step++){
      int id=(last+step+count)%count;
      if(mask&(1UL<<id)){last=id;return id;}
    }
  }
  int candidates[32], n=0;
  for(int i=0;i<count;i++) if((mask&(1UL<<i)) && i!=last) candidates[n++]=i;
  if(!n) for(int i=0;i<count;i++) if(mask&(1UL<<i)) candidates[n++]=i;
  last=candidates[gameRandomBelow(n)];
  return last;
}

static int pickFromMask_1based(
  int count,uint32_t mask,bool rnd,int &last
) {
  int zero=last-1;
  int result=pickEffectFromMask_0based(count,mask,rnd,zero)+1;
  last=result;
  return result;
}

static void markActivity() {
  lastUserActivity=lastReminderAt=millis();
  sleepAfterReminder=false;
}

static bool gameIsBusyForUi() {
  return maulOffen || digitalRead(MAUL_SENSOR_PIN)==HIGH ||
         stage!=READY || welcomeActive || reminderRunning;
}

static uint32_t checkedMask(uint32_t value,int count,uint32_t fallback) {
  value &= ((1UL<<count)-1);
  return value ? value : fallback;
}

static String displaySafeText(String text) {
  text.replace("ä","ae");
  text.replace("ö","oe");
  text.replace("ü","ue");
  text.replace("Ä","Ae");
  text.replace("Ö","Oe");
  text.replace("Ü","Ue");
  text.replace("ß","ss");

  String result;
  for(size_t i=0;i<text.length() && result.length()<40;i++) {
    uint8_t c=(uint8_t)text[i];
    if(c>=32 && c<=126) result+=(char)c;
  }
  result.trim();
  return result.length() ? result : String("CROCOSAUF DELUXE");
}

static void loadSettings() {
  gameTrackMask=checkedMask(
    prefs.getUShort("gMask",DEFAULT_GAME_MASK),12,DEFAULT_GAME_MASK
  );
  gameOverMask=checkedMask(
    prefs.getUShort("goMask",DEFAULT_GAMEOVER_MASK),12,DEFAULT_GAMEOVER_MASK
  );
  welcomeMask=checkedMask(prefs.getUChar("wMask",1),6,1);
  reminderMask=checkedMask(prefs.getUChar("rMask",0x3F),6,0x3F);
  newRndMask=checkedMask(prefs.getUChar("nrMask",1),5,1);

  gameTracksRandom=prefs.getBool("gRnd",true);
  gameOverRandom=prefs.getBool("goRnd",false);
  welcomeRandom=prefs.getBool("wRnd",false);
  reminderSoundRandom=prefs.getBool("rRnd",true);
  newRndRandom=prefs.getBool("nrRnd",false);

  gameEffectMask=checkedMask(
    prefs.getULong("geMask",ALL_EFFECTS),20,ALL_EFFECTS
  );
  reminderEffectMask=checkedMask(
    prefs.getULong("reMask",DEFAULT_REM_EFFECTS),20,DEFAULT_REM_EFFECTS
  );
  gameDispMask=checkedMask(
    prefs.getULong("gdMask",ALL_EFFECTS),20,ALL_EFFECTS
  );

  gameEffectsRandom=prefs.getBool("geRnd",false);
  reminderEffectsRandom=prefs.getBool("reRnd",true);
  gameDispRandom=prefs.getBool("gdRnd",false);

  reminderEnabled=prefs.getBool("remEn",true);
  reminderSoundEnabled=prefs.getBool("remSnd",true);

  int minutes=constrain((int)prefs.getUShort("remMin",8),1,60);
  reminderIntervalMs=(uint32_t)minutes*60000UL;
  reminderEffectDurationMs=
    constrain((int)prefs.getUShort("remDur",3500),1500,9000);

  volumeLevel=prefs.getInt("vol",20);
  // v6.2 speicherte bei Mute 0.
  if(volumeLevel<5) volumeLevel=prefs.getInt("lastVol",20);
  volumeLevel=constrain(volumeLevel,5,30);
  isMuted=prefs.getBool("mut",false);

  standbyText=displaySafeText(
    prefs.getString("sTxt","CROCOSAUF DELUXE")
  );
  ledBrightness=constrain((int)prefs.getUChar("lBr",100),0,100);
  displayBrightness=constrain((int)prefs.getUChar("dBr",100),0,100);
  displayRotation=constrain((int)prefs.getUChar("dRot",0),0,3);
  sleepMinutes=prefs.getUShort("sleepMin",0);
  if(sleepMinutes!=15 && sleepMinutes!=30 && sleepMinutes!=60) sleepMinutes=0;
  audioEnabled=prefs.getBool("aEn",true);
  welcomeVolumeLevel=constrain(prefs.getInt("wVol",20),0,30);
  startVolumeLevel=constrain(prefs.getInt("vStart",isMuted?0:volumeLevel),0,30);
  if(startVolumeLevel>0 && startVolumeLevel<5) startVolumeLevel=5;
  reminderLedDurationMs=constrain((uint32_t)prefs.getUInt("remLed",reminderEffectDurationMs),(uint32_t)1500,(uint32_t)30000);
}

static void saveSettings() {
  prefs.putUShort("gMask",gameTrackMask);
  prefs.putUShort("goMask",gameOverMask);
  prefs.putUChar("wMask",welcomeMask);
  prefs.putUChar("rMask",reminderMask);
  prefs.putUChar("nrMask",newRndMask);

  prefs.putBool("gRnd",gameTracksRandom);
  prefs.putBool("goRnd",gameOverRandom);
  prefs.putBool("wRnd",welcomeRandom);
  prefs.putBool("rRnd",reminderSoundRandom);
  prefs.putBool("nrRnd",newRndRandom);

  prefs.putULong("geMask",gameEffectMask);
  prefs.putULong("reMask",reminderEffectMask);
  prefs.putULong("gdMask",gameDispMask);
  prefs.putBool("geRnd",gameEffectsRandom);
  prefs.putBool("reRnd",reminderEffectsRandom);
  prefs.putBool("gdRnd",gameDispRandom);

  prefs.putBool("remEn",reminderEnabled);
  prefs.putBool("remSnd",reminderSoundEnabled);
  prefs.putUShort("remMin",reminderIntervalMs/60000UL);
  prefs.putUShort("remDur",reminderEffectDurationMs);
  prefs.putString("sTxt",standbyText);
  prefs.putUChar("lBr",ledBrightness);
  prefs.putUChar("dBr",displayBrightness);
  prefs.putUChar("dRot",displayRotation);
  prefs.putUShort("sleepMin",sleepMinutes);
  prefs.putBool("aEn",audioEnabled);
  prefs.putInt("wVol",welcomeVolumeLevel);
  prefs.putInt("vStart",startVolumeLevel);
  prefs.putUInt("remLed",reminderLedDurationMs);
}

static void saveVolume() {
  // Pegel und Mute getrennt speichern.
  prefs.putInt("vol",volumeLevel);
  prefs.putInt("lastVol",volumeLevel);
  prefs.putBool("mut",isMuted);
  volumeDirty=true;
}

// ============================================================
// DISPLAY
// ============================================================

static void configureLoopWatchdog() {
  esp_task_wdt_config_t config={};
  config.timeout_ms=12000;
  config.idle_core_mask=0;
  config.trigger_panic=true;
  esp_err_t error=esp_task_wdt_init(&config);
  if(error==ESP_ERR_INVALID_STATE) error=esp_task_wdt_reconfigure(&config);
  if(error==ESP_OK) {
    loopWatchdogReady=(esp_task_wdt_status(nullptr)==ESP_OK ||
                       esp_task_wdt_add(nullptr)==ESP_OK);
  }
  if(!loopWatchdogReady) systemError="Watchdog konnte nicht aktiviert werden.";
}

static void applyVisualSettings() {
  FastLED.setBrightness((uint8_t)map(ledBrightness,0,100,0,255));
  FastLED.show();
  matrix.setRotation(displayRotation);
  if(matrixReady) matrix.setBrightness((uint8_t)map(displayBrightness,0,100,0,15));
}

static void writeMatrixDisplay() {
  if(matrixReady) matrix.writeDisplay();
}

static void updateMatrixHealth(bool force=false) {
  if(!force && uint32_t(millis()-matrixCheckedAt)<5000) return;
  matrixCheckedAt=millis();
  Wire.beginTransmission(0x70);
  bool responds=(Wire.endTransmission()==0);
  if(!responds) { matrixReady=false; return; }
  if(!matrixReady) {
    matrixReady=matrix.begin(0x70);
    if(matrixReady) {
      matrixRecoveries++;
      matrix.setRotation(displayRotation);
      matrix.setBrightness((uint8_t)map(displayBrightness,0,100,0,15));
      matrix.clear();
      writeMatrixDisplay();
      dispLast=0;
    }
  }
}

static void dispHardClear() {
  matrix.clear();
  writeMatrixDisplay();
}

static int displayedPinchNumber() {
  if(rundeAktuell<1) return 1;
  if(rundeAktuell>10) return 10;
  return rundeAktuell;
}

static void dispSetScroll(const String& text,unsigned long holdMs=0) {
  dispMode=DISP_SCROLL;
  dispText=text;
  scrollX=8;
  dispLast=0;
  dispUntil=(holdMs>0) ? (millis()+holdMs) : 0;
}

static void dispSetCupAnim(uint8_t repeatTarget,unsigned long holdMs=0) {
  dispMode=DISP_CUP_ANIM;
  cupRepeatTarget=repeatTarget;
  cupLevel=0;
  cupCycle=0;
  cupHolding=false;
  cupLastStep=millis();
  cupHoldStart=0;
  dispUntil=(holdMs>0) ? (millis()+holdMs) : 0;
}

static void dispSetReminderAnim() {
  dispMode=DISP_REMINDER_ANIM;
  dispUntil=0;
  remAnimFrame=0;
  remAnimLast=0;
  remBubbleX=6;
  remBubbleY=1;
}

static void dispSetGameAnim() {
  dispMode=DISP_GAME_ANIM;
  dispUntil=0;
  forcedGameShow=-1;
  gameShowId=(uint8_t)pickEffectFromMask_0based(
    DISP_ANIM_COUNT,gameDispMask,gameDispRandom,lastGameShowPick
  );
  gameShowLastSwitch=0;
  gameFrame=0;
  gameFrameLast=0;
}

static void dispSetGameAnimForced(int id) {
  dispMode=DISP_GAME_ANIM;
  dispUntil=0;
  forcedGameShow=constrain(id,0,DISP_ANIM_COUNT-1);
  gameShowId=forcedGameShow;
  gameShowLastSwitch=millis();
  gameFrame=0;
  gameFrameLast=0;
}

static void showVolumeOnMatrix(unsigned long ms=1200) {
  dispMode=DISP_VOLUME_BARS;
  dispUntil=millis()+ms;
}

static void showMutedOnMatrix(unsigned long ms=1200) {
  dispMode=DISP_MUTED_ICON;
  dispUntil=millis()+ms;
}

static void dispSetPinchBlink() {
  dispMode=DISP_PINCH_BLINK;
  dispUntil=0;
  pinchBlinkOn=true;
  pinchBlinkLast=millis();
  pinchBlinkTogglesLeft=6;
}

static void dispRender() {
  if(!matrixReady) return;
  if(dispMode==DISP_ORIENTATION) {
    if(uint32_t(millis()-dispLast)<100) return;
    dispLast=millis();
    matrix.clear();matrix.setTextWrap(false);matrix.setTextSize(1);
    matrix.setTextColor(LED_ON);matrix.setCursor(1,0);matrix.print("F");
    writeMatrixDisplay();return;
  }
  if(dispUntil!=0 && deadlineReached(millis(),dispUntil)) {
    dispUntil=0;
    restoreDisplay();
  }
  if(millis()-dispLast<70) return;
  dispLast=millis();

  if(dispMode==DISP_PINCH_BLINK) {
    unsigned long now=millis();
    if(pinchBlinkTogglesLeft>0) {
      if(now-pinchBlinkLast>=PINCH_NUM_BLINK_MS) {
        pinchBlinkLast=now;
        pinchBlinkOn=!pinchBlinkOn;
        pinchBlinkTogglesLeft--;
        if(pinchBlinkTogglesLeft==0) pinchBlinkOn=true;
      }
      if(pinchBlinkOn) {
        drawNumberCentered((uint8_t)displayedPinchNumber());
      } else {
        dispHardClear();
      }
    } else {
      drawNumberCentered((uint8_t)displayedPinchNumber());
    }
    return;
  }

  if(dispMode==DISP_GAME_ANIM) {
    const unsigned long SWITCH_MS=1000;
    const unsigned long FRAME_MS=110;
    unsigned long now=millis();

    if(gameShowLastSwitch==0) gameShowLastSwitch=now;
    if(gameFrameLast==0) gameFrameLast=now;

    if(forcedGameShow<0) {
      if(now-gameShowLastSwitch>=SWITCH_MS) {
        gameShowLastSwitch=now;
        gameShowId=(uint8_t)pickEffectFromMask_0based(
          DISP_ANIM_COUNT,(uint32_t)gameDispMask,
          gameDispRandom,lastGameShowPick
        );
        gameFrame=0;
        gameFrameLast=now;
      }
    } else {
      gameShowId=(uint8_t)forcedGameShow;
    }

    if(now-gameFrameLast>=FRAME_MS) {
      gameFrameLast=now;
      gameFrame++;
    }

    matrix.clear();

    auto drawSmiley=[&](bool wink,bool bigSmile) {
      matrix.drawPixel(2,2,LED_ON);
      matrix.drawPixel(5,2,LED_ON);
      if(wink) {
        matrix.drawPixel(5,2,LED_OFF);
        matrix.drawPixel(5,3,LED_ON);
      }
      if(bigSmile) {
        matrix.drawPixel(2,5,LED_ON);
        matrix.drawPixel(3,6,LED_ON);
        matrix.drawPixel(4,6,LED_ON);
        matrix.drawPixel(5,5,LED_ON);
      } else {
        matrix.drawLine(2,6,5,6,LED_ON);
      }
    };

    switch(gameShowId) {
      case 0: {
        bool wink=((gameFrame%10)==3 || (gameFrame%10)==4);
        bool mouthOpen=((gameFrame%12)>=6);

        matrix.drawPixel(2,2,LED_ON);
        matrix.drawPixel(2,3,LED_ON);

        if(!wink) {
          matrix.drawPixel(5,2,LED_ON);
          matrix.drawPixel(5,3,LED_ON);
        } else {
          matrix.drawPixel(5,3,LED_ON);
        }

        if(!mouthOpen) {
          matrix.drawLine(2,6,5,6,LED_ON);
        } else {
          matrix.drawLine(2,5,5,5,LED_ON);
          matrix.drawPixel(2,6,LED_ON);
          matrix.drawPixel(5,6,LED_ON);
        }
        break;
      }

      case 1:
        drawSmiley(false,((gameFrame%8)<4));
        break;

      case 2:
        drawSmiley(((gameFrame%10)<3),true);
        break;

      case 3: {
        matrix.drawPixel(2,2,LED_ON);
        matrix.drawPixel(5,2,LED_ON);
        bool open=((gameFrame%8)<4);
        if(open) {
          matrix.drawLine(2,5,5,5,LED_ON);
          matrix.drawPixel(2,6,LED_ON);
          matrix.drawPixel(5,6,LED_ON);
        } else {
          matrix.drawLine(2,6,5,6,LED_ON);
        }
        break;
      }

      case 4: {
        bool big=((gameFrame%10)<5);
        if(!big) {
          matrix.drawPixel(3,2,LED_ON);
          matrix.drawPixel(4,2,LED_ON);
          matrix.drawPixel(2,3,LED_ON);
          matrix.drawPixel(5,3,LED_ON);
          matrix.drawPixel(3,3,LED_ON);
          matrix.drawPixel(4,3,LED_ON);
          matrix.drawPixel(3,4,LED_ON);
          matrix.drawPixel(4,4,LED_ON);
          matrix.drawPixel(3,5,LED_ON);
          matrix.drawPixel(4,5,LED_ON);
        } else {
          matrix.drawPixel(2,2,LED_ON);
          matrix.drawPixel(5,2,LED_ON);
          matrix.drawPixel(1,3,LED_ON);
          matrix.drawPixel(6,3,LED_ON);
          matrix.drawLine(2,4,5,4,LED_ON);
          matrix.drawPixel(3,5,LED_ON);
          matrix.drawPixel(4,5,LED_ON);
          matrix.drawPixel(3,6,LED_ON);
          matrix.drawPixel(4,6,LED_ON);
        }
        break;
      }

      case 5:
        for(int i=0;i<12;i++) {
          matrix.drawPixel(random(8),random(8),LED_ON);
        }
        break;

      case 6: {
        uint8_t p=gameFrame%8;
        matrix.drawPixel(p,2,LED_ON);
        matrix.drawPixel((p+2)%8,3,LED_ON);
        matrix.drawPixel((p+4)%8,4,LED_ON);
        matrix.drawPixel((p+6)%8,5,LED_ON);
        break;
      }

      case 7: {
        uint8_t x=gameFrame%8;
        for(int y=1;y<7;y++) matrix.drawPixel(x,y,LED_ON);
        break;
      }

      case 8: {
        bool inv=((gameFrame%6)<3);
        for(int y=0;y<8;y++) {
          for(int x=0;x<8;x++) {
            bool on=((x+y)&1);
            if(inv) on=!on;
            if(on) matrix.drawPixel(x,y,LED_ON);
          }
        }
        break;
      }

      case 9: {
        int shift=gameFrame%3;
        matrix.drawPixel(1+shift,3,LED_ON);
        matrix.drawPixel(2+shift,2,LED_ON);
        matrix.drawPixel(2+shift,3,LED_ON);
        matrix.drawPixel(2+shift,4,LED_ON);
        matrix.drawPixel(3+shift,3,LED_ON);
        matrix.drawPixel(4+shift,3,LED_ON);
        matrix.drawPixel(5+shift,2,LED_ON);
        matrix.drawPixel(5+shift,3,LED_ON);
        matrix.drawPixel(5+shift,4,LED_ON);
        matrix.drawPixel(6+shift,3,LED_ON);
        break;
      }

      case 10: {
        static const uint8_t stars[5][2]={
          {1,1},{6,1},{2,5},{5,6},{3,3}
        };
        int k=gameFrame%5;
        for(int i=0;i<5;i++) {
          matrix.drawPixel(stars[i][0],stars[i][1],LED_ON);
        }
        if((gameFrame%2)==0) {
          matrix.drawPixel(
            max(0,(int)stars[k][0]-1),stars[k][1],LED_ON
          );
          matrix.drawPixel(
            min(7,(int)stars[k][0]+1),stars[k][1],LED_ON
          );
        }
        break;
      }

      case 11: {
        int f=gameFrame%8;
        static const uint8_t ring[8][2]={
          {3,1},{4,1},{6,3},{6,4},{4,6},{3,6},{1,4},{1,3}
        };
        for(int i=0;i<8;i+=2) {
          int idx=(i+f)%8;
          matrix.drawPixel(ring[idx][0],ring[idx][1],LED_ON);
        }
        break;
      }

      case 12: {
        int x=gameFrame%8;
        int y=(gameFrame/2)%6;
        y=(y<=3)?y:(6-y);
        matrix.drawPixel(x,y+1,LED_ON);
        matrix.drawPixel(x,y+2,LED_ON);
        break;
      }

      case 13: {
        int x=gameFrame%9;
        bool mouth=((gameFrame%6)<3);
        int px=min(x,7);
        matrix.drawPixel(px,3,LED_ON);
        matrix.drawPixel(px,4,LED_ON);
        matrix.drawPixel(max(px-1,0),3,LED_ON);
        matrix.drawPixel(max(px-1,0),4,LED_ON);
        if(mouth) matrix.drawPixel(px,3,LED_OFF);
        for(int i=px+1;i<8;i+=2) matrix.drawPixel(i,3,LED_ON);
        break;
      }

      case 14: {
        bool on=((gameFrame%6)<3);
        if(on) {
          matrix.drawLine(4,1,4,5,LED_ON);
          matrix.drawPixel(4,7,LED_ON);
        }
        break;
      }

      case 15: {
        matrix.drawPixel(2,2,LED_ON);
        matrix.drawPixel(5,2,LED_ON);
        matrix.drawPixel(1,4,LED_ON);
        matrix.drawPixel(6,4,LED_ON);
        bool mouth=((gameFrame%8)<4);
        if(mouth) {
          matrix.drawPixel(2,6,LED_ON);
          matrix.drawPixel(5,6,LED_ON);
          matrix.drawPixel(3,7,LED_ON);
          matrix.drawPixel(4,7,LED_ON);
        } else {
          matrix.drawLine(2,6,5,6,LED_ON);
        }
        break;
      }

      case 16: {
        matrix.drawLine(1,2,6,2,LED_ON);
        matrix.drawPixel(3,2,LED_ON);
        matrix.drawPixel(4,2,LED_ON);
        matrix.drawPixel(2,6,LED_ON);
        matrix.drawPixel(3,7,LED_ON);
        matrix.drawPixel(4,7,LED_ON);
        matrix.drawPixel(5,6,LED_ON);
        break;
      }

      case 17: {
        bool blink=((gameFrame%10)<7);
        if(blink) {
          matrix.drawPixel(3,1,LED_ON);
          matrix.drawPixel(4,1,LED_ON);
          matrix.drawPixel(5,2,LED_ON);
          matrix.drawPixel(4,3,LED_ON);
          matrix.drawPixel(4,4,LED_ON);
          matrix.drawPixel(4,6,LED_ON);
        }
        break;
      }

      case 18: {
        int cx=3+((gameFrame/6)%2);
        int cy=3;
        int phase=gameFrame%6;
        matrix.drawPixel(cx,cy,LED_ON);
        if(phase>=1) {
          matrix.drawPixel(cx-1,cy,LED_ON);
          matrix.drawPixel(cx+1,cy,LED_ON);
        }
        if(phase>=2) {
          matrix.drawPixel(cx,cy-1,LED_ON);
          matrix.drawPixel(cx,cy+1,LED_ON);
        }
        if(phase>=3) {
          matrix.drawPixel(cx-1,cy-1,LED_ON);
          matrix.drawPixel(cx+1,cy+1,LED_ON);
        }
        if(phase>=4) {
          matrix.drawPixel(cx+1,cy-1,LED_ON);
          matrix.drawPixel(cx-1,cy+1,LED_ON);
        }
        if(phase>=5) {
          matrix.drawPixel(random(8),random(8),LED_ON);
        }
        break;
      }

      case 19: {
        bool on=((gameFrame%8)<4);
        if(on) {
          matrix.drawRect(2,2,4,5,LED_ON);
          matrix.drawPixel(6,3,LED_ON);
          matrix.drawPixel(6,4,LED_ON);
          int lvl=(gameFrame%4);
          for(int y=6;y>=6-lvl;y--) {
            matrix.drawLine(3,y,4,y,LED_ON);
          }
        }
        break;
      }
    }

    writeMatrixDisplay();
    return;
  }

  if(dispMode==DISP_REMINDER_ANIM) {
    const unsigned long STEP_MS=180;
    unsigned long now=millis();

    if(remAnimLast==0 || (now-remAnimLast>=STEP_MS)) {
      remAnimLast=now;
      remAnimFrame^=1;
      if(remBubbleY==0) {
        remBubbleY=2;
        remBubbleX=(uint8_t)random(5,8);
      } else {
        remBubbleY--;
      }
    }

    matrix.clear();
    matrix.drawPixel(5,2,LED_ON);
    matrix.drawPixel(7,2,LED_ON);
    matrix.drawLine(4,3,7,3,LED_ON);
    matrix.drawPixel(4,4,LED_ON);
    matrix.drawLine(1,4,3,4,LED_ON);

    if(remAnimFrame==0) {
      matrix.drawLine(1,5,3,5,LED_ON);
    } else {
      matrix.drawLine(1,5,3,5,LED_ON);
      matrix.drawLine(1,6,3,6,LED_ON);
    }

    matrix.drawPixel(remBubbleX,remBubbleY,LED_ON);
    writeMatrixDisplay();
    return;
  }

  matrix.clear();
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  matrix.setTextColor(LED_ON);

  if(dispMode==DISP_SCROLL) {
    matrix.setCursor(scrollX,1);
    matrix.print(dispText);
    writeMatrixDisplay();
    scrollX--;
    int16_t minX=-(int16_t)dispText.length()*6;
    if(scrollX<minX) scrollX=8;
    return;
  }

  if(dispMode==DISP_VOLUME_BARS) {
    int bars=map(volumeLevel,0,30,0,8);
    bars=constrain(bars,0,8);
    for(int x=0;x<8;x++) {
      if(x<bars) {
        for(int y=7;y>=0;y--) matrix.drawPixel(x,y,LED_ON);
      }
    }
    writeMatrixDisplay();
    return;
  }

  if(dispMode==DISP_MUTED_ICON) {
    matrix.drawPixel(1,3,LED_ON);
    matrix.drawPixel(1,4,LED_ON);
    matrix.drawPixel(2,2,LED_ON);
    matrix.drawPixel(2,3,LED_ON);
    matrix.drawPixel(2,4,LED_ON);
    matrix.drawPixel(2,5,LED_ON);
    matrix.drawPixel(3,1,LED_ON);
    matrix.drawPixel(3,2,LED_ON);
    matrix.drawPixel(3,5,LED_ON);
    matrix.drawPixel(3,6,LED_ON);
    matrix.drawPixel(5,2,LED_ON);
    matrix.drawPixel(6,3,LED_ON);
    matrix.drawPixel(7,4,LED_ON);
    matrix.drawPixel(7,3,LED_ON);
    matrix.drawPixel(6,4,LED_ON);
    matrix.drawPixel(5,5,LED_ON);
    writeMatrixDisplay();
    return;
  }

  if(dispMode==DISP_CUP_ANIM) {
    const unsigned long STEP_MS=160;
    const unsigned long HOLD_FULL_MS=650;
    unsigned long now=millis();

    matrix.drawRect(2,1,4,6,LED_ON);
    matrix.drawPixel(6,2,LED_ON);
    matrix.drawPixel(6,3,LED_ON);
    matrix.drawPixel(6,4,LED_ON);

    bool loopForever=(cupRepeatTarget==255);

    if(!cupHolding) {
      if(now-cupLastStep>=STEP_MS) {
        cupLastStep=now;
        if(cupLevel<4) {
          cupLevel++;
        } else {
          cupHolding=true;
          cupHoldStart=now;
        }
      }
    } else {
      if(now-cupHoldStart>=HOLD_FULL_MS) {
        cupCycle++;
        if(loopForever) {
          cupLevel=0;
          cupHolding=false;
          cupLastStep=now;
          cupCycle=0;
        } else {
          if(cupCycle<cupRepeatTarget) {
            cupLevel=0;
            cupHolding=false;
            cupLastStep=now;
          } else {
            cupLevel=4;
          }
        }
      }
    }

    for(int y=6;y>=6-cupLevel+1;y--) {
      matrix.drawLine(3,y,4,y,LED_ON);
    }
    writeMatrixDisplay();
    return;
  }
}

// ============================================================
// LED-EFFEKTE
// ============================================================

static void resetEffects() {
  effectPos=0;
  direction=1;
  hue=0;
  brightness=0;
  brightnessDelta=5;
}

static void effect_render(int id) {
  if (millis() - lastEffectUpdate < 30) return;
  lastEffectUpdate = millis();

  switch (id) { case 10: case 13: case 15: case 16: case 17: case 18: break; default: FastLED.clear(); break; }

  switch (id) {
    case 0: { leds[effectPos] = CRGB::Blue; effectPos += direction; if (effectPos == NUM_LEDS - 1 || effectPos == 0) direction = -direction; break; }
    case 1: leds[random(NUM_LEDS)] = CRGB::White; break;
    case 2: { brightness += brightnessDelta; if (brightness <= 0 || brightness >= 255) brightnessDelta = -brightnessDelta; fill_solid(leds, NUM_LEDS, CRGB(0, brightness, 0)); break; }
    case 3: { brightness += brightnessDelta; if (brightness <= 0 || brightness >= 255) brightnessDelta = -brightnessDelta; fill_solid(leds, NUM_LEDS, CRGB(0, 0, brightness)); break; }
    case 4: { for (int i = 0; i < NUM_LEDS; i++) leds[(effectPos + i) % NUM_LEDS] = CHSV(i * (255 / NUM_LEDS), 255, 255); effectPos = (effectPos + 1) % NUM_LEDS; break; }
    case 5: { for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV((i * 20 + hue) % 255, 255, 255); hue++; break; }
    case 6: for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(0, 0, random8(80, 255)); break;
    case 7: leds[random(NUM_LEDS)] = CRGB::White; leds[random(NUM_LEDS)] = CRGB::White; break;
    case 8: { static int b=0; leds[b]=CRGB::Yellow; leds[NUM_LEDS-1-b]=CRGB::Yellow; b=(b+1)%NUM_LEDS; break; }
    case 9: { static int f=0; for(int j=0;j<=f && j<NUM_LEDS;j++) leds[j]=CRGB::Aqua; f=(f+1)%(NUM_LEDS+1); break; }
    case 10:{ fadeToBlackBy(leds,NUM_LEDS,50); static int m=0; leds[m]=CRGB::White; m=(m+1)%NUM_LEDS; break; }
    case 11:{ static uint8_t s=0; s+=2; for(int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(s+i*10,255,255); break; }
    case 12:{ static uint8_t fx=0; fx+=2; for(int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(fx+i*12,255,sin8((millis()/5)+i*20)); break; }
    case 13:{ fadeToBlackBy(leds,NUM_LEDS,60); static int p=0; static uint8_t sh=0; sh+=4; leds[p]=CHSV(sh,255,255); p=(p+1)%NUM_LEDS; break; }
    case 14:{ static int u=0; leds[u]=CRGB::Cyan; u=(u+1)%NUM_LEDS; break; }
    case 15:{ fadeToBlackBy(leds,NUM_LEDS,20); int pos=random(NUM_LEDS); leds[pos]+=CHSV(random8(),200,255); break; }
    case 16:{ fadeToBlackBy(leds,NUM_LEDS,80); static int t=0; for(int i=0;i<NUM_LEDS;i+=3){int idx=(i+t)%NUM_LEDS; leds[idx]=CRGB::White;} t=(t+1)%3; break; }
    case 17:{ static uint8_t base=0; base+=4; for(int i=0;i<NUM_LEDS;i++){ uint8_t wave=sin8((uint8_t)(base+i*18)); leds[i]=CHSV((uint8_t)(base+i*9),255,wave);} if(random8()<120) leds[random(NUM_LEDS)]=CRGB::White; if(random8()<80) leds[random(NUM_LEDS)]+=CHSV(random8(),200,255); break; }
    case 18:{ fadeToBlackBy(leds,NUM_LEDS,40); static uint8_t base=0; base+=2; int pos=random(NUM_LEDS); leds[pos]=CHSV(base+random8(64),255,255); break; }
    case 19:{ static bool flip=false; flip=!flip; fill_solid(leds,NUM_LEDS, flip?CRGB::Red:CRGB::Blue); break; }
  }

  FastLED.show();
}

// ============================================================
// AUDIO
// ============================================================

// Audio-Befehle mit Abstand senden, ohne die Spielschleife zu blockieren.
static void stopAudio(bool force=false) {
  if(welcomeActive && !force) return;
  if(dfplayerInitOk) audioStopPending=true;
  audioDisableRepeatPending=false;
  audioRole=AUDIO_NONE; audioStep=0; audioIdleTracking=false; audioSawBusy=false;
  if(force) welcomeActive=false;
}

static int requestedAudioVolume() {
  if(!audioEnabled) return 0;
  if(audioRole==AUDIO_WELCOME || (audioRole==AUDIO_TEST && audioFolder==FOLDER_SYSTEM)) return welcomeVolumeLevel;
  return isMuted?0:volumeLevel;
}

static void requestAudio(AudioRole role,int folder,int track) {
  if(!dfplayerInitOk || !audioEnabled || welcomeActive) return;
  bool welcomeSound=(role==AUDIO_WELCOME || (role==AUDIO_TEST && folder==FOLDER_SYSTEM));
  if(welcomeSound ? welcomeVolumeLevel==0 : isMuted) return;
  if(track<1 || folder<0 || folder>99) return;
  stopAudio();
  audioStopPending=false; // Stopp ist Schritt 1 des neuen Auftrags.
  audioRole=role; audioFolder=folder; audioTrack=track;
  audioStep=1; audioStepAt=millis();
  audioSawBusy=false; audioIdleTracking=false;
  if(role==AUDIO_WELCOME) welcomeActive=true;
}

static void audioFault(const char* message) {
  audioError=message;
  Serial.println(message);

  if(audioRole==AUDIO_GAME) gameAudioFailed=true;

  stopAudio(true);
  uiPlaying=false;
  uiMode=0;
  uiCurrentTrack=0;
}

static void updateAudio() {
  uint32_t now=millis();
  bool canSend=uint32_t(now-lastAudioCommand)>=DF_COMMAND_GAP_MS;
  if(audioStopPending) {
    if(!canSend) return;
    player.stop(); mp3Serial.flush();
    lastAudioCommand=millis();audioStopPending=false;
    return;
  }
  if(audioStep) {
    if(!canSend) return;
    switch(audioStep++) {
      case 1: player.stop(); break;
      case 2: player.volume(requestedAudioVolume()); volumeDirty=false; break;
      case 3: {
        // Kein loop()/0x08: dessen Verhalten unterscheidet sich bei DFPlayer-
        // Varianten. Wiederholung ausschliesslich ueber BUSY und denselben Titel.
        if(audioFolder) player.playFolder(audioFolder,audioTrack);
        else player.play(audioTrack);
        char line[100];
        if(audioFolder) snprintf(line,sizeof(line),"[AUDIO] /%02u/%03d.mp3 | Lautstaerke %d%s",
                                 (unsigned)audioFolder,audioTrack,requestedAudioVolume(),requestedAudioVolume()==0?" (stumm)":"");
        else snprintf(line,sizeof(line),"[AUDIO] Root-Dateiindex %d | Lautstaerke %d%s",
                      audioTrack,requestedAudioVolume(),requestedAudioVolume()==0?" (stumm)":"");
        Serial.println(line);
        audioStep=0; audioStarted=now;audioDisableRepeatPending=true;
        break;
      }
    }
    mp3Serial.flush();audioStepAt=lastAudioCommand=millis();
    return;
  }
  // Einzelwiederholung erst WAEHREND der Wiedergabe abschalten. Im Stoppzustand
  // ignorieren einige Module 0x19. Kein 0x11 (alle Titel) zwischen zwei Sounds.
  if(audioDisableRepeatPending && dfIsBusyPlaying() && canSend) {
    player.stopRepeat();mp3Serial.flush();lastAudioCommand=millis();
    audioDisableRepeatPending=false;
  } else if(volumeDirty && dfplayerInitOk && canSend) {
    player.volume(requestedAudioVolume());
    mp3Serial.flush();volumeDirty=false;lastAudioCommand=millis();
  }
  if(audioRole==AUDIO_NONE) return;
  if(dfIsBusyPlaying()) {
    if(!audioSawBusy) audioError="";
    audioSawBusy=true; audioIdleTracking=false;
    if(audioRole==AUDIO_WELCOME && uint32_t(now-audioStarted)>=WELCOME_MAX_MS)
      audioFault("Welcome: BUSY zu lange LOW; Wiedergabe freigegeben.");
    return;
  }
  if(!audioSawBusy) {
    if(uint32_t(now-audioStarted)>=WELCOME_START_TIMEOUT_MS)
      audioFault("Kein Wiedergabestart erkannt: BUSY, SD-Karte und Datei pruefen.");
    return;
  }
  if(!audioIdleTracking){audioIdleTracking=true;audioIdleSince=now;}
  uint32_t idleMs=now-audioIdleSince;
  if(uint32_t(now-audioStarted)<1200) return;
  if(audioRole==AUDIO_GAME) {
    if(idleMs>=800 && stage==PLAYING && maulOffen && !isMuted)
      requestAudio(AUDIO_GAME,GAME_MUSIC_FOLDER,currentGameMusicTrack);
  } else if(idleMs>=250) {
    bool wasTest=audioRole==AUDIO_TEST;
    if(audioRole==AUDIO_WELCOME) volumeDirty=true;
    welcomeActive=false; audioRole=AUDIO_NONE;audioDisableRepeatPending=false;
    if(wasTest){uiPlaying=false;uiMode=0;uiCurrentTrack=0;}
  }
}

static void startGameMusic() {
  if(!audioEnabled || isMuted || welcomeActive || !dfplayerInitOk || gameAudioFailed) return;
  requestAudio(AUDIO_GAME,GAME_MUSIC_FOLDER,currentGameMusicTrack);
}

static void playFromSelection(
  int folder,int count,uint32_t mask,bool rnd,int &last
) {
  int track=pickFromMask_1based(count,mask,rnd,last);
  requestAudio(AUDIO_ONCE,folder,track);
}

// ============================================================
// SPIEL UND REMINDER
// ============================================================

static void restoreDisplay() {
  dispUntil=0;

  if(reminderRunning) {
    if(reminderShowCup) {
      dispSetCupAnim(255,0);
    } else {
      dispSetReminderAnim();
    }
  } else if(stage==NEW_ROUND) {
    dispSetScroll("NEUE RUNDE");
  } else if(stage==GAME_OVER) {
    dispSetCupAnim(255,0);
  } else if(stage==PLAYING) {
    if(pinchenMode) dispSetPinchBlink();
    else dispSetGameAnim();
  } else if(uiDispTesting) {
    if(uiDispId==DISP_ANIM_COUNT) dispMode=DISP_ORIENTATION;
    else dispSetGameAnimForced(uiDispId);
  } else if(pinchenMode) {
    dispSetPinchBlink();
  } else if(normalCupUntil && !deadlineReached(millis(),normalCupUntil)) {
    dispSetCupAnim(255,0);
    dispUntil=normalCupUntil;
  } else {
    normalCupUntil=0;
    dispSetScroll(standbyText);
  }
}

static void stopUiTests() {
  if(uiPlaying || audioRole==AUDIO_TEST) stopAudio();

  uiPlaying=false;
  uiMode=0;
  uiCurrentTrack=0;
  uiEffectTesting=false;
  uiEffectId=-1;
  uiDispTesting=false;
  uiDispId=-1;
  forcedGameShow=-1;

  FastLED.clear();
  FastLED.show();
}

static void stopReminder() {
  if(!reminderRunning) return;

  reminderRunning=false;
  stopAudio();
  FastLED.clear();
  FastLED.show();

  // Keine Benutzeraktivitaet: Schlafzeit bleibt unberuehrt.
  lastReminderAt=millis();
  restoreDisplay();
}

static void startReminder() {
  if(stage!=READY || maulOffen || welcomeActive) return;

  stopUiTests();
  stopAudio();
  reminderRunning=true;
  reminderStarted=millis();
  reminderShowCup=true;

  reminderEffectId=pickEffectFromMask_0based(
    EFFECT_COUNT,reminderEffectMask,reminderEffectsRandom,lastRemEffect
  );

  resetEffects();
  lastEffectUpdate=0;
  restoreDisplay();

  if(reminderSoundEnabled) {
    playFromSelection(
      FOLDER_REMINDER,REMINDER_TRACK_COUNT,reminderMask,
      reminderSoundRandom,lastReminderTrack
    );
  }
}

static void loadPinchenOrder() {
  String saved=prefs.getString("pOrder","");
  pinchenOrderValid=false;
  if(saved.length()!=PINCHEN_TOTAL) return;
  uint16_t mask=0;
  for(int i=0;i<PINCHEN_TOTAL;i++) {
    int id=saved[i]-'0';
    if(id<0 || id>=PINCHEN_TOTAL || (mask&(1U<<id))) return;
    pinchenOrder[i]=id;mask|=(1U<<id);
  }
  pinchenOrderValid=(mask==0x03FF);
}

static void shufflePinchenOrder() {
  uint8_t previous[PINCHEN_TOTAL];
  for(int i=0;i<PINCHEN_TOTAL;i++) previous[i]=pinchenOrder[i];
  bool accepted=false;
  // Fisher-Yates, mit begrenzter Wiederholung. Jeder Platz unterscheidet sich
  // vom vorigen Durchgang; auch letztes -> erstes Pinchen wiederholt sich nicht.
  for(int attempt=0;attempt<64 && !accepted;attempt++) {
    for(int i=0;i<PINCHEN_TOTAL;i++) pinchenOrder[i]=i;
    for(int i=PINCHEN_TOTAL-1;i>0;i--) {
      int j=gameRandomBelow(i+1);
      uint8_t tmp=pinchenOrder[i];pinchenOrder[i]=pinchenOrder[j];pinchenOrder[j]=tmp;
    }
    accepted=true;
    if(pinchenOrderValid) {
      for(int i=0;i<PINCHEN_TOTAL;i++) if(pinchenOrder[i]==previous[i]) accepted=false;
      if(pinchenOrder[0]==previous[PINCHEN_TOTAL-1]) accepted=false;
    }
  }
  if(!accepted) {
    int shift=1+gameRandomBelow(PINCHEN_TOTAL-2);
    for(int i=0;i<PINCHEN_TOTAL;i++) pinchenOrder[i]=previous[(i+shift)%PINCHEN_TOTAL];
  }
  pinchenOrderValid=true;
  String saved;
  for(int i=0;i<PINCHEN_TOTAL;i++) saved+=(char)('0'+pinchenOrder[i]);
  // Ein Schreibvorgang pro neuem Durchgang, nicht pro Loop/Pinchen.
  // Damit unterscheidet sich die Folge auch nach Aus-/Einschalten.
  prefs.putString("pOrder",saved);
}

static void resetRounds() {
  rundeAktuell=1; usedPinchenMask=0; pinnchenIndex=-1;
  if(pinchenMode) shufflePinchenOrder();
}

static void switchMode(bool newMode) {
  stopUiTests();
  stopReminder();
  stopAudio();

  pinchenMode=newMode;
  stage=READY;
  stageStarted=millis();
  normalCupUntil=0;
  currentGameMusicTrack=0;
  gameAudioFailed=false;

  resetRounds();
  markActivity();
  restoreDisplay();

  // Welcome bleibt geschuetzt.
  // Die Hauptschleife startet danach im neuen Modus.
}

static void startGame() {
  if(stage!=READY || !maulOffen || welcomeActive) return;

  stopUiTests();
  stopReminder();

  stage=PLAYING;
  stageStarted=millis();
  normalCupUntil=0;

  currentGameMusicTrack=pickFromMask_1based(
    ROOT_GAME_TRACK_COUNT,gameTrackMask,gameTracksRandom,lastGameTrack
  );

  gameAudioFailed=false;
  markActivity();
  resetEffects();
  lastEffectSwitch=millis();
  lastEffectUpdate=0;

  currentEffect=pickEffectFromMask_0based(
    EFFECT_COUNT,gameEffectMask,gameEffectsRandom,lastGameEffect
  );

  restoreDisplay();
  startGameMusic();
}

static void beginGameOver() {
  stage=GAME_OVER; stageStarted=millis(); pinnchenIndex=-1;
  // Bereits jetzt reservieren: auch ein spaeter Schleifendurchlauf darf
  // keine Pinchen-Auswahl ueberspringen. Sichtbar wird sie erst nach 4 s.
  if(pinchenMode) {
    if(!pinchenOrderValid) shufflePinchenOrder();
    for(int i=0;i<PINCHEN_TOTAL;i++) {
      int id=pinchenOrder[i];
      if(!(usedPinchenMask&(1U<<id))) {
        pinnchenIndex=id;usedPinchenMask|=(1U<<id);break;
      }
    }
  }
  normalCupUntil=pinchenMode ? 0 : (millis()+NORMAL_GAMEOVER_CUP_MS);
  markActivity();
  playFromSelection(FOLDER_GAMEOVER,GAMEOVER_TRACK_COUNT,gameOverMask,
                    gameOverRandom,lastGoTrack);
  // Auch im Mute muss ein bereits laufender Musikloop gestoppt werden.
  if(isMuted) stopAudio();
  restoreDisplay();
}

static void updateGame() {
  uint32_t now=millis(), elapsed=now-stageStarted;
  if(stage==PLAYING) {
    if(!maulOffen){beginGameOver();return;}
    if(uint32_t(now-lastEffectSwitch)>=effectInterval) {
      currentEffect=pickEffectFromMask_0based(EFFECT_COUNT,gameEffectMask,
                                             gameEffectsRandom,lastGameEffect);
      resetEffects(); lastEffectSwitch=now;
    }
    effect_render(currentEffect);
    return;
  }
  if(stage==GAME_OVER) {
    FastLED.clear();
    bool on=((elapsed/PINCHEN_BLINK_INTERVAL)%2)==0;
    if(elapsed<RED_PHASE_DURATION) {
      if(on) fill_solid(leds,NUM_LEDS,CRGB::Red);
    } else if(pinchenMode && elapsed<RED_PHASE_DURATION+PINCHEN_PHASE_DURATION) {
      if(on && pinnchenIndex>=0 && pinnchenIndex<PINCHEN_TOTAL) {
        for(int pair=0;pair<2;pair++) leds[pinnchenLEDs[pinnchenIndex][pair]]=CRGB::Cyan;
      }
    }
    FastLED.show();
    uint32_t duration=RED_PHASE_DURATION+(pinchenMode?PINCHEN_PHASE_DURATION:0);
    if(elapsed>=duration) {
      FastLED.clear(); FastLED.show();
      if(pinchenMode && rundeAktuell>=PINCHEN_TOTAL) {
        stage=NEW_ROUND; stageStarted=now;
        playFromSelection(FOLDER_NEWRND,NEWRND_TRACK_COUNT,newRndMask,newRndRandom,lastNewTrack);
      } else {
        if(pinchenMode) rundeAktuell++;
        stage=READY;
      }
      restoreDisplay();
    }
    return;
  }
  if(stage==NEW_ROUND) {
    if(elapsed<GRUEN_PHASE_DURATION) {
      bool on=((elapsed/PINCHEN_BLINK_INTERVAL)%2)==0;
      fill_solid(leds,NUM_LEDS,on?CRGB::Green:CRGB::Black); FastLED.show();
    } else {
      resetRounds(); stage=READY;
      FastLED.clear(); FastLED.show(); restoreDisplay();
    }
  }
}

// ============================================================
// LAUTSTAERKE UND TASTER
// ============================================================

static void volumeUp() {
  volumeLevel+=5;
  if(volumeLevel>30) volumeLevel=5;

  isMuted=false;
  saveVolume();
  markActivity();
  showVolumeOnMatrix();

  if(stage==PLAYING && audioRole!=AUDIO_GAME) {
    gameAudioFailed=false;
    startGameMusic();
  }
}

static void toggleMute() {
  isMuted=!isMuted;
  saveVolume();
  markActivity();

  if(isMuted) showMutedOnMatrix();
  else showVolumeOnMatrix();

  if(!isMuted && stage==PLAYING && audioRole!=AUDIO_GAME) {
    gameAudioFailed=false;
    startGameMusic();
  }
}

static void handleVolumeButton() {
  static bool last=HIGH,held=false,armed=false;
  static uint32_t pressed=0;
  bool value=volumeInput.update();

  if(!maulOffen) {
    last=value;
    held=false;
    armed=false;
    return;
  }

  if(value==LOW && last==HIGH) {
    pressed=millis();
    held=false;
    armed=true;
  }

  if(value==LOW && armed && !held &&
     uint32_t(millis()-pressed)>=1000) {
    held=true;
    toggleMute();
  }

  if(value==HIGH && last==LOW && armed) {
    if(!held) volumeUp();
    armed=false;
  }

  last=value;
}

// ============================================================
// WLAN UND DEEP SLEEP
// ============================================================



static void enterDeepSleepNow() {
  if(!sleepMinutes || maulOffen || digitalRead(MAUL_SENSOR_PIN)==HIGH ||
     stage!=READY || welcomeActive || bluetoothHasClient()) return;

  uint32_t since=millis()-lastReminderAt;
  uint32_t remaining=since>=reminderIntervalMs ? 0 : reminderIntervalMs-since;
  uint32_t slice=reminderEnabled ? min(SLEEP_CHECK_MS,max((uint32_t)1,remaining)) : SLEEP_CHECK_MS;
  esp_err_t rtcOk=rtc_gpio_init((gpio_num_t)MAUL_SENSOR_PIN);
  if(rtcOk==ESP_OK) rtcOk=rtc_gpio_set_direction((gpio_num_t)MAUL_SENSOR_PIN,RTC_GPIO_MODE_INPUT_ONLY);
  if(rtcOk==ESP_OK) rtcOk=rtc_gpio_pullup_en((gpio_num_t)MAUL_SENSOR_PIN);
  if(rtcOk==ESP_OK) rtcOk=rtc_gpio_pulldown_dis((gpio_num_t)MAUL_SENSOR_PIN);
  esp_err_t extOk=esp_sleep_enable_ext0_wakeup((gpio_num_t)MAUL_SENSOR_PIN,1);
  esp_err_t timerOk=esp_sleep_enable_timer_wakeup((uint64_t)slice*1000ULL);
  if(rtcOk!=ESP_OK || extOk!=ESP_OK || timerOk!=ESP_OK) {
    rtc_gpio_deinit((gpio_num_t)MAUL_SENSOR_PIN);reed.begin();
    // Remain awake until a new deliberate setting change or reboot.
    sleepMinutes=0;sleepAfterReminder=false;
    systemError="Schlaf deaktiviert: Aufwachquelle konnte nicht eingerichtet werden.";
    Serial.println(systemError);return;
  }
  // The jaw may have opened while commands were prepared.
  if(rtc_gpio_get_level((gpio_num_t)MAUL_SENSOR_PIN)==HIGH) {
    rtc_gpio_deinit((gpio_num_t)MAUL_SENSOR_PIN);reed.begin();
    maulOffen=reed.stable;maulOpenSince=millis();markActivity();return;
  }
  rtcMagic=0xC60C6301;rtcRound=rundeAktuell;rtcUsed=usedPinchenMask;
  rtcPinchenMode=pinchenMode;
  rtcReminderRemainingMs=remaining;rtcSleepSliceMs=slice;

  stopUiTests();stopReminder();stopAudio(true);
  if(audioStopPending) {delay(DF_COMMAND_GAP_MS);updateAudio();}
  FastLED.clear();FastLED.show();dispHardClear();
  Serial.println("[SLEEP] Sensor und Timer aktiv; naechste Kontrolle in "+String(slice/1000)+" s");
  stopBluetooth();
  esp_deep_sleep_start();
}

// ============================================================
// WEBUI: HTML UND JSON
// ============================================================



static String jsonEscape(const String& value) {
  String out;
  for(size_t i=0;i<value.length();i++) {
    uint8_t c=(uint8_t)value[i];
    if(c=='"') {
      out+="\\\"";
    } else if(c=='\\') {
      out+="\\\\";
    } else if(c<32) {
      char buf[7];
      snprintf(buf,sizeof(buf),"\\u%04x",(unsigned)c);
      out+=buf;
    } else {
      out+=(char)c;
    }
  }
  return out;
}

static String statusJson() {
  String state=stage==PLAYING ? "Spiel laeuft" :
               stage==GAME_OVER ? "Game Over" :
               stage==NEW_ROUND ? "Neue Runde" : "Bereit";

  if(welcomeActive) state="Welcome";
  else if(reminderRunning) state="Reminder";

  String j="{\"version\":\""+String(SKETCH_VERSION)+"\",\"state\":\""+state+"\"";
  j+=",\"pinchen\":"+String(pinchenMode?"true":"false");
  j+=",\"round\":"+String(rundeAktuell);
  j+=",\"open\":"+String(maulOffen?"true":"false");
  j+=",\"vol\":"+String(volumeLevel);
  j+=",\"muted\":"+String(isMuted?"true":"false");
  j+=",\"dfOk\":"+String(dfplayerInitOk?"true":"false");
  j+=",\"busy\":"+String(dfIsBusyPlaying()?"true":"false");
  j+=",\"clients\":"+String(bluetoothHasClient()?1:0);
  j+=",\"uiPlaying\":"+String(uiPlaying?"true":"false");
  j+=",\"uiMode\":"+String(uiMode);
  j+=",\"uiTrack\":"+String(uiCurrentTrack);
  j+=",\"testEffect\":"+String(uiEffectId);
  j+=",\"testDisplay\":"+String(uiDispId);
  j+=",\"standby\":\""+jsonEscape(standbyText);
  j+="\",\"error\":\""+jsonEscape(audioError)+"\"}";
  j=j.substring(0,j.length()-1);
  j+=",\"audioFolder\":"+String(audioFolder)+",\"audioTrack\":"+String(audioTrack);
  j+=",\"uptimeSec\":"+String(millis()/1000);
  j+=",\"resetReason\":"+String(bootResetReason);
  j+=",\"freeHeap\":"+String(ESP.getFreeHeap());
  j+=",\"matrixOk\":"+String(matrixReady?"true":"false");
  j+=",\"matrixRecoveries\":"+String(matrixRecoveries);
  j+=",\"watchdogOk\":"+String(loopWatchdogReady?"true":"false");
  j+=",\"sleepMinutes\":"+String(sleepMinutes);
  j+=",\"systemError\":\""+jsonEscape(systemError)+"\"}";
  return j;
}











// ============================================================
// GEMEINSAME BEFEHLSVERARBEITUNG FUER WEB UND BLUETOOTH
// ============================================================

// Zugriff ausschliesslich aus loop(), nicht aus Bluetooth-Callbacks.
static bool appRequest=false;
static String appQuery;
static int appResponseCode=500;
static String appResponse;

static int hexValue(char c) {
  if(c>='0' && c<='9') return c-'0';
  if(c>='a' && c<='f') return c-'a'+10;
  if(c>='A' && c<='F') return c-'A'+10;
  return -1;
}

static String urlDecode(const String& in) {
  String out;
  for(size_t i=0;i<in.length();i++) {
    char c=in[i];
    if(c=='+') {
      out+=' ';
    } else if(c=='%' && i+2<in.length() &&
              hexValue(in[i+1])>=0 && hexValue(in[i+2])>=0) {
      out+=(char)((hexValue(in[i+1])<<4)|hexValue(in[i+2]));
      i+=2;
    } else {
      out+=c;
    }
  }
  return out;
}

static bool queryValue(
  const String& query,const String& key,String& value
) {
  size_t start=0;
  while(start<query.length()) {
    size_t end=start;
    while(end<query.length() && query[end]!='&') end++;

    size_t eq=start;
    while(eq<end && query[eq]!='=') eq++;

    if(urlDecode(query.substring(start,eq))==key) {
      value=eq<end ? urlDecode(query.substring(eq+1,end)) : String("");
      return true;
    }
    start=end+1;
  }
  return false;
}

static String apiArg(const String& key) {
  String result;queryValue(appQuery,key,result);return result;
}

static bool apiHasArg(const String& key) {
  String value;return queryValue(appQuery,key,value);
}

static void apiSend(int code,const String&,const String& body) {
  appResponseCode=code;appResponse=body;
}

// ============================================================
// BEFEHLS-HANDLER
// ============================================================

static bool requireIdle() {
  if(gameIsBusyForUi()) {
    apiSend(
      409,"text/plain; charset=utf-8",
      "Bitte Spiel/Welcome abwarten und Maul schliessen."
    );
    return false;
  }
  return true;
}

static void ok(const char* message="OK") {
  apiSend(200,"text/plain; charset=utf-8",message);
}

static void handleStop() {
  if(welcomeActive || stage!=READY) {
    apiSend(409,"text/plain","Welcome oder Spiel ist aktiv.");
    return;
  }

  stopUiTests();
  stopReminder();
  stopAudio();
  markActivity();
  restoreDisplay();
  ok("Tests und Reminder gestoppt.");
}

static void handlePlay() {
  if(!requireIdle()) return;

  if(!audioEnabled){apiSend(409,"text/plain","Audio ist ausgeschaltet.");return;}
  if(!dfplayerInitOk) {
    apiSend(503,"text/plain","DFPlayer nicht initialisiert.");
    return;
  }

  if(isMuted) {
    apiSend(409,"text/plain","Bitte zuerst Mute ausschalten.");
    return;
  }

  String src=apiArg("src");
  int count=0,folder=0,mode=0;

  if(src=="g") {
    count=12;
    folder=GAME_MUSIC_FOLDER;
    mode=1;
  } else if(src=="w") {
    count=6;
    folder=3;
    mode=2;
  } else if(src=="go") {
    count=12;
    folder=2;
    mode=3;
  } else if(src=="r") {
    count=6;
    folder=4;
    mode=4;
  } else if(src=="nr") {
    count=5;
    folder=5;
    mode=5;
  }

  int track=apiArg("t").toInt();
  if(track<1 || track>count) {
    apiSend(400,"text/plain","Ungueltiger Track.");
    return;
  }

  bool same=uiPlaying && uiMode==mode && uiCurrentTrack==track;

  stopUiTests();
  markActivity();
  restoreDisplay();

  if(same) {
    ok("Soundtest gestoppt.");
    return;
  }

  uiPlaying=true;
  uiMode=mode;
  uiCurrentTrack=track;
  uiTestStarted=millis();

  requestAudio(AUDIO_TEST,folder,track);
  ok("Soundtest gestartet.");
}

static void handleTestVisual(bool display) {
  if(!requireIdle()) return;

  int id=apiArg("id").toInt();
  if(!apiHasArg("id") || id<0 || id>=20) {
    apiSend(400,"text/plain","Ungueltiger Effekt.");
    return;
  }

  bool same=display ?
    (uiDispTesting && uiDispId==id) :
    (uiEffectTesting && uiEffectId==id);

  stopUiTests();
  markActivity();

  if(!same) {
    uiTestStarted=millis();
    if(display) {
      uiDispTesting=true;
      uiDispId=id;
    } else {
      uiEffectTesting=true;
      uiEffectId=id;
      resetEffects();
      lastEffectUpdate=0;
    }
  }

  restoreDisplay();
  ok(same?"Test gestoppt.":"Test gestartet.");
}

static uint32_t postedMask(int count) {
  uint32_t m=0;
  for(int i=0;i<count;i++) {
    if(apiHasArg("m"+String(i))) m|=(1UL<<i);
  }
  return m;
}

static void redirect(const char*) {
  apiSend(200,"text/plain","Einstellungen gespeichert.");
}

static void handleSaveSelection() {
  String g=apiArg("group");bool rnd=apiArg("mode")=="rnd";
  if(g=="g"){
    uint16_t mask=checkedMask(postedMask(12),12,DEFAULT_GAME_MASK);
    if(mask!=gameTrackMask || rnd!=gameTracksRandom) lastGameTrack=0;
    gameTrackMask=mask;gameTracksRandom=rnd;
  } else if(g=="go"){
    uint16_t mask=checkedMask(postedMask(12),12,DEFAULT_GAMEOVER_MASK);
    if(mask!=gameOverMask || rnd!=gameOverRandom) lastGoTrack=0;
    gameOverMask=mask;gameOverRandom=rnd;
  } else if(g=="w"){
    uint8_t mask=checkedMask(postedMask(6),6,1);
    if(mask!=welcomeMask || rnd!=welcomeRandom){lastWelcomeTrack=0;prefs.putUChar("wLast",0);}
    welcomeMask=mask;welcomeRandom=rnd;
  } else if(g=="r"){
    uint8_t mask=checkedMask(postedMask(6),6,0x3F);
    if(mask!=reminderMask || rnd!=reminderSoundRandom) lastReminderTrack=0;
    reminderMask=mask;reminderSoundRandom=rnd;
  } else if(g=="nr"){
    uint8_t mask=checkedMask(postedMask(5),5,1);
    if(mask!=newRndMask || rnd!=newRndRandom) lastNewTrack=0;
    newRndMask=mask;newRndRandom=rnd;
  }
  else if(g=="ge"){gameEffectMask=checkedMask(postedMask(20),20,ALL_EFFECTS);gameEffectsRandom=rnd;}
  else if(g=="re"){reminderEffectMask=checkedMask(postedMask(20),20,DEFAULT_REM_EFFECTS);reminderEffectsRandom=rnd;}
  else if(g=="gd"){gameDispMask=checkedMask(postedMask(20),20,ALL_EFFECTS);gameDispRandom=rnd;}
  else{apiSend(400,"text/plain","Unbekannte Gruppe.");return;}
  saveSettings();markActivity();
  redirect((g=="ge" || g=="re" || g=="gd")?"/effects":"/");
}

static void handleSaveText() {
  standbyText=displaySafeText(apiArg("txt"));
  saveSettings();
  markActivity();

  if(stage==READY && !reminderRunning && !uiDispTesting) {
    restoreDisplay();
  }
  redirect("/system");
}

static void handleSaveReminderCfg() {
  reminderEnabled=apiHasArg("ren");
  reminderSoundEnabled=apiHasArg("rsnd");

  long minutes=apiArg("rmin").toInt();
  long duration=apiArg("rdur").toInt();

  minutes=constrain(minutes,1L,60L);
  duration=constrain(duration,1500L,9000L);

  reminderIntervalMs=(uint32_t)minutes*60000UL;
  reminderEffectDurationMs=(uint16_t)duration;

  uint32_t requestedLed=apiHasArg("rled") ? (uint32_t)apiArg("rled").toInt() : (uint32_t)duration;
  reminderLedDurationMs=constrain(requestedLed,(uint32_t)1500,(uint32_t)30000);
  if(reminderRunning) stopReminder();

  saveSettings();
  markActivity();
  redirect("/system");
}

static bool apiNumber(const char* key,int lower,int upper,int &number) {
  String value=apiArg(key);
  if(value.length()==0 || value.length()>3) return false;
  for(size_t i=0;i<value.length();i++) if(value[i]<'0' || value[i]>'9') return false;
  number=value.toInt();return number>=lower && number<=upper;
}

static void handleSaveVisual() {
  int led,display,rotation;
  if(!apiNumber("lbr",0,100,led) || !apiNumber("dbr",0,100,display) ||
     !apiNumber("rot",0,3,rotation)) {
    apiSend(400,"text/plain","Ungueltige Helligkeit oder Displaydrehung.");return;
  }
  ledBrightness=led;displayBrightness=display;displayRotation=rotation;
  prefs.putUChar("lBr",ledBrightness);prefs.putUChar("dBr",displayBrightness);
  prefs.putUChar("dRot",displayRotation);
  applyVisualSettings();markActivity();ok("Licht und Display gespeichert.");
}

static void handleSaveAudioCfg() {
  int start,welcome,enabled;
  if(!apiNumber("vstart",0,30,start) || (start>0 && start<5) ||
     !apiNumber("vwelcome",0,30,welcome) || !apiNumber("aen",0,1,enabled)) {
    apiSend(400,"text/plain","Audio: Start 0 oder 5..30, Welcome 0..30.");return;
  }
  bool wasEnabled=audioEnabled;
  audioEnabled=(enabled==1);startVolumeLevel=start;welcomeVolumeLevel=welcome;
  isMuted=(start==0);if(start>0) volumeLevel=start;
  prefs.putBool("aEn",audioEnabled);prefs.putInt("vStart",startVolumeLevel);
  prefs.putInt("wVol",welcomeVolumeLevel);saveVolume();markActivity();
  if(!audioEnabled) stopAudio(true);
  else if(stage==PLAYING && (!wasEnabled || audioRole!=AUDIO_GAME)) {
    gameAudioFailed=false;startGameMusic();
  }
  ok("Audioeinstellungen gespeichert.");
}

static void handleSavePower() {
  int minutes;
  if(!apiNumber("sleep",0,60,minutes) ||
     (minutes!=0 && minutes!=15 && minutes!=30 && minutes!=60)) {
    apiSend(400,"text/plain","Schlafzeit: 0, 15, 30 oder 60 Minuten.");return;
  }
  sleepMinutes=minutes;prefs.putUShort("sleepMin",sleepMinutes);
  systemError="";markActivity();ok("Schlafeinstellung gespeichert.");
}

static void handleTestOrientation() {
  if(!requireIdle()) return;
  bool same=uiDispTesting && uiDispId==DISP_ANIM_COUNT;
  stopUiTests();markActivity();
  if(!same) {uiDispTesting=true;uiDispId=DISP_ANIM_COUNT;uiTestStarted=millis();}
  restoreDisplay();ok(same ? "Displaytest gestoppt." : "Display zeigt F zur Ausrichtung.");
}

static void handleFactoryReset() {
  if(!requireIdle()) return;

  stopUiTests();
  stopAudio();
  prefs.clear();
  loadSettings();
  applyVisualSettings();
  saveSettings();
  saveVolume();
  resetRounds();

  lastGameTrack=lastGoTrack=lastWelcomeTrack=lastReminderTrack=lastNewTrack=0;
  lastGameEffect=lastRemEffect=lastGameShowPick=-1;

  normalCupUntil=0;
  audioError="";
  gameAudioFailed=false;

  markActivity();
  restoreDisplay();
  ok("Werkseinstellungen geladen.");
}



// ============================================================
// BLUETOOTH LOW ENERGY
// ============================================================

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <atomic>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// BLE-Fernbedienung, kein Bluetooth-Audioempfaenger.
static const char* BLE_SERVICE="7c800001-0dc3-4fd6-9c26-a3821da872bc";
static const char* BLE_RX="7c800002-0dc3-4fd6-9c26-a3821da872bc";
static const char* BLE_TX="7c800003-0dc3-4fd6-9c26-a3821da872bc";

static std::atomic<bool> bleConnected{false};
static std::atomic<uint32_t> bleEpoch{0};

static bool bleStarted=false,bleApproved=false;
static uint32_t bleSeenEpoch=0,bleConnectedAt=0,bleTxAt=0;
static BLEServer* bleServer=nullptr;
static BLECharacteristic* bleTx=nullptr;
static BLE2902* bleCccd=nullptr;

struct BleRequest {
  uint32_t epoch;
  char text[512];
};

static QueueHandle_t bleQueue=nullptr;
static String bleOutput;
static size_t bleOutputOffset=0;

static bool bleButtonArmed=false;
static bool bleButtonDown=false;
static bool bleButtonUsed=false;
static uint32_t bleButtonAt=0;

class CrocoServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected.store(true);
    bleEpoch.fetch_add(1);
  }

  void onDisconnect(BLEServer*) override {
    bleConnected.store(false);
    bleEpoch.fetch_add(1);
  }
};

class CrocoRxCallbacks: public BLECharacteristicCallbacks {
  char line[512]={};
  size_t used=0;
  bool overflow=false;
  uint32_t epoch=0;

  void onWrite(BLECharacteristic* characteristic) override {
    uint32_t current=bleEpoch.load();
    if(epoch!=current) {
      epoch=current;
      used=0;
      overflow=false;
    }

    auto bytes=characteristic->getValue();
    for(size_t i=0;i<bytes.length();i++) {
      char c=bytes[i];

      if(c=='\n') {
        if(!overflow && used && bleConnected.load()) {
          line[used]=0;
          BleRequest req{};
          req.epoch=current;
          memcpy(req.text,line,used+1);

          // Bei voller Queue meldet die App Timeout.
          // Kein Befehl wird teilweise ausgefuehrt.
          xQueueSend(bleQueue,&req,0);
        }

        used=0;
        overflow=false;

      } else if(c!='\r') {
        if(c=='\0' || used>=sizeof(line)-1) {
          overflow=true;
        } else if(!overflow) {
          line[used++]=c;
        }
      }
    }
  }
};

static void startBluetooth() {
  if(bleStarted) return;

  bleQueue=xQueueCreate(4,sizeof(BleRequest));
  if(!bleQueue) {
    Serial.println("BLE: keine Befehlsqueue verfuegbar.");
    return;
  }

  BLEDevice::init("Crocosauf Deluxe");
  bleServer=BLEDevice::createServer();
  bleServer->setCallbacks(new CrocoServerCallbacks());

  BLEService* service=bleServer->createService(BLE_SERVICE);

  bleTx=service->createCharacteristic(
    BLE_TX,BLECharacteristic::PROPERTY_NOTIFY
  );
  bleCccd=new BLE2902();
  bleTx->addDescriptor(bleCccd);

  BLECharacteristic* rx=service->createCharacteristic(
    BLE_RX,BLECharacteristic::PROPERTY_WRITE
  );
  rx->setCallbacks(new CrocoRxCallbacks());

  service->start();

  BLEAdvertising* advertising=BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  advertising->start();

  bleStarted=true;
}

static bool bluetoothHasClient() {
  return bleStarted && bleConnected.load();
}

static void stopBluetooth() {
  if(bleStarted) BLEDevice::getAdvertising()->stop();
  // Kein deinit/delete waehrend eines Callbacks.
  // Unmittelbar danach folgt Deep Sleep.
}

static String configJson() {
  String j="{\"protocol\":1,\"masks\":{";

  j+="\"g\":"+String(gameTrackMask);
  j+=",\"go\":"+String(gameOverMask);
  j+=",\"w\":"+String(welcomeMask);
  j+=",\"r\":"+String(reminderMask);
  j+=",\"nr\":"+String(newRndMask);
  j+=",\"ge\":"+String(gameEffectMask);
  j+=",\"re\":"+String(reminderEffectMask);
  j+=",\"gd\":"+String(gameDispMask)+"},\"random\":{";

  j+="\"g\":"+String(gameTracksRandom?"true":"false");
  j+=",\"go\":"+String(gameOverRandom?"true":"false");
  j+=",\"w\":"+String(welcomeRandom?"true":"false");
  j+=",\"r\":"+String(reminderSoundRandom?"true":"false");
  j+=",\"nr\":"+String(newRndRandom?"true":"false");
  j+=",\"ge\":"+String(gameEffectsRandom?"true":"false");
  j+=",\"re\":"+String(reminderEffectsRandom?"true":"false");
  j+=",\"gd\":"+String(gameDispRandom?"true":"false")+"}";

  j+=",\"remEn\":"+String(reminderEnabled?"true":"false");
  j+=",\"remSnd\":"+String(reminderSoundEnabled?"true":"false");
  j+=",\"remMin\":"+String(reminderIntervalMs/60000);
  j+=",\"remDur\":"+String(reminderEffectDurationMs);
  j+=",\"standby\":\""+jsonEscape(standbyText)+"\"";
  j+=",\"gameFolder\":"+String(GAME_MUSIC_FOLDER)+"}";

  j=j.substring(0,j.length()-1);
  j+=",\"features\":{\"visualSettings\":true,\"sleepSettings\":true,\"diagnostics\":true,\"audioSettings\":true}";
  j+=",\"ledBrightness\":"+String(ledBrightness);
  j+=",\"displayBrightness\":"+String(displayBrightness);
  j+=",\"rotation\":"+String(displayRotation);
  j+=",\"sleepMinutes\":"+String(sleepMinutes);
  j+=",\"numLeds\":"+String(NUM_LEDS)+"}";
  j=j.substring(0,j.length()-1);
  j+=",\"audioEnabled\":"+String(audioEnabled?"true":"false");
  j+=",\"startVolume\":"+String(startVolumeLevel);
  j+=",\"welcomeVolume\":"+String(welcomeVolumeLevel);
  j+=",\"remLed\":"+String(reminderLedDurationMs)+"}";
  return j;
}

// Format: ID /pfad?formularparameter\n
// Pro App-Verbindung ein Befehl gleichzeitig.
static void dispatchBluetooth(const String& line) {
  size_t split=0;
  while(split<line.length() && line[split]!=' ') split++;

  String id=line.substring(0,split);
  if(id.length()<1 || id.length()>8) return;

  for(size_t i=0;i<id.length();i++) {
    if(id[i]<'0' || id[i]>'9') return;
  }

  String request=split<line.length() ?
    line.substring(split+1,line.length()) : String("");

  size_t q=0;
  while(q<request.length() && request[q]!='?') q++;

  String path=request.substring(0,q);
  appQuery=q<request.length() ?
    request.substring(q+1,request.length()) : String("");

  appRequest=true;
  appResponseCode=404;
  appResponse="Unbekannter Befehl.";

  if(path=="/hello") {
    appResponseCode=200;
    appResponse=
      "{\"product\":\"crocosauf\",\"protocol\":1,\"version\":\""+
      String(SKETCH_VERSION)+"\",\"approved\":"+
      String(bleApproved?"true":"false")+"}";

  } else if(!bleApproved) {
    appResponseCode=403;
    appResponse=
      "Am Geraet: Maul zu, Bereitschaft abwarten, Taste 3 Sekunden halten.";

  } else if(path=="/status") {
    appResponseCode=200;
    appResponse=statusJson();

  } else if(path=="/config") {
    appResponseCode=200;
    appResponse=configJson();

  } else if(path=="/play") {
    handlePlay();

  } else if(path=="/testEffect") {
    handleTestVisual(false);

  } else if(path=="/testDisp") {
    handleTestVisual(true);

  } else if(path=="/stop") {
    handleStop();

  } else if(path=="/save") {
    handleSaveSelection();

  } else if(path=="/saveText") {
    handleSaveText();

  } else if(path=="/saveDisplayCfg") {
    handleSaveVisual();
  } else if(path=="/saveAudioCfg") {
    handleSaveAudioCfg();
  } else if(path=="/savePower") {
    handleSavePower();
  } else if(path=="/testOrientation") {
    handleTestOrientation();
  } else if(path=="/saveReminderCfg") {
    handleSaveReminderCfg();

  } else if(path=="/testReminder") {
    if(requireIdle()) {
      markActivity();
      startReminder();
      ok("Reminder gestartet.");
    }

  } else if(path=="/volUp") {
    volumeUp();
    ok("Lautstaerke erhoeht.");

  } else if(path=="/muteToggle") {
    toggleMute();
    ok("Mute umgeschaltet.");

  } else if(path=="/volume") {
    String value=apiArg("v");
    bool valid=value.length()>0 && value.length()<=2;

    for(size_t i=0;i<value.length();i++) {
      if(value[i]<'0' || value[i]>'9') valid=false;
    }

    int volume=value.toInt();

    if(!valid || volume<5 || volume>30) {
      apiSend(400,"text/plain","Lautstaerke muss 5 bis 30 sein.");
    } else {
      volumeLevel=volume;
      saveVolume();
      markActivity();
      showVolumeOnMatrix();
      ok("Lautstaerke gespeichert.");
    }

  } else if(path=="/factoryReset") {
    if(apiArg("confirm")!="yes") {
      apiSend(400,"text/plain","Bestaetigung fehlt.");
    } else {
      handleFactoryReset();
    }
  }

  appRequest=false;
  appResponse.replace("\n"," ");
  appResponse.replace("\r"," ");

  bleOutput=id+" "+String(appResponseCode)+" "+appResponse+"\n";
  bleOutputOffset=0;
}

static void updateBluetooth() {
  if(!bleStarted) return;

  uint32_t now=millis();
  uint32_t epoch=bleEpoch.load();

  if(epoch!=bleSeenEpoch) {
    bleSeenEpoch=epoch;
    bleApproved=false;
    bleOutput="";
    bleOutputOffset=0;
    bleButtonArmed=false;
    bleButtonDown=false;
    bleButtonUsed=false;
    bleConnectedAt=now;

    if(!bleConnected.load()) BLEDevice::startAdvertising();
  }

  if(!bleConnected.load()) return;

  // Freigabe gilt nur fuer diese Verbindung.
  bool canApprove=
    !maulOffen && stage==READY && !welcomeActive && !reminderRunning;

  bool pressed=(volumeInput.stable==LOW);

  if(!pressed) {
    bleButtonArmed=true;
    bleButtonDown=false;
    bleButtonUsed=false;
  }

  if(canApprove && !bleApproved && pressed && bleButtonArmed) {
    if(!bleButtonDown) {
      bleButtonDown=true;
      bleButtonAt=now;
    }

    if(!bleButtonUsed && uint32_t(now-bleButtonAt)>=3000) {
      bleButtonUsed=true;
      bleApproved=true;
      markActivity();
      dispSetScroll("APP OK",1800);
    }

  } else if(!canApprove) {
    bleButtonDown=false;
  }

  if(!bleApproved && uint32_t(now-bleConnectedAt)>90000) {
    bleServer->disconnect(bleServer->getConnId());
    return;
  }

  if(bleOutputOffset<bleOutput.length()) {
    if(uint32_t(now-bleTxAt)>=20 && bleCccd->getNotifications()) {
      bleTxAt=now;

      // Maximal 20 Bytes: funktioniert mit Standard-MTU 23.
      size_t count=min((size_t)20,bleOutput.length()-bleOutputOffset);

      bleTx->setValue(
        (uint8_t*)bleOutput.c_str()+bleOutputOffset,count
      );
      bleTx->notify();
      bleOutputOffset+=count;
    }
    return;
  }

  BleRequest req{};
  if(xQueueReceive(bleQueue,&req,0)==pdTRUE &&
     req.epoch==bleSeenEpoch) {
    dispatchBluetooth(String(req.text));
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  setCpuFrequencyMhz(160);
  Serial.begin(115200);
  Serial.println(String(SKETCH_NAME)+" "+SKETCH_VERSION);
  bootResetReason=(uint32_t)esp_reset_reason();
  Serial.println("[BOOT] Resetgrund "+String(bootResetReason));
  configureLoopWatchdog();
  esp_sleep_wakeup_cause_t wake=esp_sleep_get_wakeup_cause();
  bool fromSleep=(wake==ESP_SLEEP_WAKEUP_EXT0 || wake==ESP_SLEEP_WAKEUP_TIMER);
  // Der EXT0-Pin ist nach dem Aufwachen noch RTC-IO: vor digitalRead freigeben.
  rtc_gpio_deinit((gpio_num_t)MAUL_SENSOR_PIN);
  reed.begin();modeInput.begin();volumeInput.begin();
  pinMode(DFPLAYER_BUSY_PIN,INPUT_PULLUP);
  maulOffen=reed.stable;pinchenMode=(modeInput.stable==LOW);
  prefs.begin("crocodoc",false);loadSettings();
  isMuted=(startVolumeLevel==0);
  if(startVolumeLevel>0) volumeLevel=startVolumeLevel;
  lastWelcomeTrack=prefs.getUChar("wLast",0);
  if(lastWelcomeTrack>WELCOME_TRACK_COUNT) lastWelcomeTrack=0;
  loadPinchenOrder();
  Wire.begin(I2C_SDA,I2C_SCL);Wire.setTimeOut(I2C_TIMEOUT_MS);
  updateMatrixHealth(true);dispHardClear();
  FastLED.addLeds<WS2812B,LED_PIN,GRB>(leds,NUM_LEDS);
  FastLED.clear();applyVisualSettings();
  mp3Serial.begin(9600,SERIAL_8N1,DFPLAYER_RX_PIN,DFPLAYER_TX_PIN);
  dfplayerInitOk=player.begin(mp3Serial,false);
  if(dfplayerInitOk) {
    // Die Bibliothek initialisiert nur UART. Die SD kann 1,5-3 s benoetigen.
    // Reset loescht auch Wiedergabemodi, die eine alte Firmware gesetzt hat.
    delay(3000);
    player.reset();mp3Serial.flush();delay(3000);
    player.playbackSource(2);mp3Serial.flush();delay(300); // TF / microSD
    player.stop();mp3Serial.flush();delay(DF_COMMAND_GAP_MS);
    player.volume(isMuted?0:volumeLevel);mp3Serial.flush();lastAudioCommand=millis();
  }
  else audioError="DFPlayer-Initialisierung fehlgeschlagen.";
  markActivity();maulOpenSince=millis();
  // POWERON kann hardwarebedingt auch einen EN-/Reset-Taster einschliessen.
  bool powerOn=(esp_reset_reason()==ESP_RST_POWERON && !fromSleep);
  if(wake!=ESP_SLEEP_WAKEUP_TIMER || maulOffen || !sleepMinutes) startBluetooth();
  // Animationszufall ebenfalls nicht mit einer fast gleichen Bootzeit starten.
  randomSeed(esp_random());
  if(fromSleep && rtcMagic==0xC60C6301 && bool(rtcPinchenMode)==pinchenMode && pinchenOrderValid) {
    rundeAktuell=constrain((int)rtcRound,1,PINCHEN_TOTAL);
    usedPinchenMask=rtcUsed&0x03FF;
    uint16_t expected=0;
    for(int i=0;i<rundeAktuell-1;i++) expected|=(1U<<pinchenOrder[i]);
    if(usedPinchenMask!=expected) resetRounds();
  } else resetRounds();
  restoreDisplay();
  if(powerOn && dfplayerInitOk && audioEnabled && welcomeVolumeLevel>0) {
    int track=pickFromMask_1based(WELCOME_TRACK_COUNT,welcomeMask,welcomeRandom,lastWelcomeTrack);
    requestAudio(AUDIO_WELCOME,FOLDER_SYSTEM,track);
    prefs.putUChar("wLast",track); // Reihenfolge ueber Aus-/Einschalten erhalten.
  }
  if(wake==ESP_SLEEP_WAKEUP_TIMER && !maulOffen && sleepMinutes) {
    uint32_t elapsed=rtcSleepSliceMs+millis();
    uint32_t remaining=rtcReminderRemainingMs>elapsed ? rtcReminderRemainingMs-elapsed : 0;
    remaining=min(remaining,reminderIntervalMs);
    lastReminderAt=millis()-(reminderIntervalMs-remaining);
    sleepAfterReminder=true;
    if(reminderEnabled && remaining==0) startReminder();
    else enterDeepSleepNow();
  }
}

// ============================================================
// HAUPTSCHLEIFE
// ============================================================

void loop() {
  if(loopWatchdogReady) esp_task_wdt_reset();
  updateMatrixHealth();
  uint32_t now=millis();
  bool wasOpen=maulOffen;
  maulOffen=reed.update();

  if(maulOffen!=wasOpen) {
    markActivity();

    if(maulOffen) {
      maulOpenSince=now;
      stopUiTests();
      stopReminder();
      restoreDisplay();
    }
  }

  bool selectedMode=(modeInput.update()==LOW);
  if(selectedMode!=pinchenMode) switchMode(selectedMode);

  handleVolumeButton();

  if(maulOffen && !bleStarted) startBluetooth();
  updateBluetooth();


  updateAudio();

  if((uiPlaying || uiEffectTesting || uiDispTesting) &&
     uint32_t(now-uiTestStarted)>=UI_TEST_TIMEOUT_MS) {
    stopUiTests();
    restoreDisplay();
  }

  if(reminderRunning) {
    uint32_t elapsed=millis()-reminderStarted;

    if(maulOffen || elapsed>=reminderLedDurationMs) {
      stopReminder();

      if(sleepAfterReminder && !maulOffen) {
        enterDeepSleepNow();
      }
    } else {
      // Erste Haelfte Glas, zweite Haelfte Krokodil.
      bool cup=(elapsed<reminderLedDurationMs/2);

      if(cup!=reminderShowCup) {
        reminderShowCup=cup;
        restoreDisplay();
      }
      effect_render(reminderEffectId);
    }
  }

  if(!reminderRunning) {
    updateGame();

    if(stage==READY && maulOffen && !welcomeActive &&
       uint32_t(millis()-maulOpenSince)>=OPEN_CONFIRM_MS) {
      startGame();
    }

    if(stage==READY && !maulOffen && !welcomeActive) {
      bool testing=uiPlaying || uiEffectTesting || uiDispTesting;

      if(uiEffectTesting) effect_render(uiEffectId);

      if(!testing) {
        bool connected=bluetoothHasClient();

        if(sleepMinutes && !connected &&
           (sleepAfterReminder ||
            uint32_t(millis()-lastUserActivity)>=(uint32_t)sleepMinutes*60000UL)) {
          enterDeepSleepNow();

        } else if(reminderEnabled &&
                  uint32_t(millis()-lastReminderAt)>=reminderIntervalMs) {
          startReminder();
        }
      }
    }
  }

  dispRender();
  delay(1);
}