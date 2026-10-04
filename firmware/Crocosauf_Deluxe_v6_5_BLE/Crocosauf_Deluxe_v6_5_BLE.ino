/*
  CROCOSAUF DELUXE v6.5 BLE - Sound- und Pinchenkorrektur fuer ESP32 WROOM.
  Basierend auf Marcels v6.2; Pinbelegung, 20 LED-Effekte und
  20 Displayanimationen beibehalten. WebUI aus beschaedigter Vorlage rekonstruiert.

  Normal: Maul 900 ms offen -> Spiel; zu -> 4 s Rot, Glas insgesamt 5 s.
  Pinchen: 10 Runden; pro Runde 4 s Rot + 4 s zufaelliges, unbenutztes Pinchen.
  Nach Runde 10: 5,2 s gruene LEDs / NEUE RUNDE / Sound aus /05.
  Reminder standardmaessig alle 8 min, 3500 ms lang; Schlaf nach 30 min Ruhe.
  WLAN crocosauf, http://192.168.4.1; individuelles Passwort im USB-Seriellmonitor.
  WLAN aus nach 5 zusammenhaengenden Minuten ohne verbundenes Geraet.
  NEU: Android-App ueber BLE; pro Verbindung Maul zu + Taste 3 s zur Freigabe.
  BLE bleibt unabhaengig vom WLAN aktiv. Im Schlaf: Maul zum Aufwecken oeffnen.
  Arduino Partition: Huge APP (3MB No OTA/1MB SPIFFS) fuer 4-MB-WROOM.

  v6.5: Zwei LEDs je Pinchen. Jeder Zehnerdurchgang wird neu gemischt;
  jedes Pinchen genau einmal, andere Position als im vorherigen Durchgang.
  Audio: feste Ordner/Titel, keine DFPlayer-Hardwareloops, Befehlsabstand 200 ms.
  DFPlayer beim Start zuruecksetzen und SD initialisieren (ca. 6,5 Sekunden).
  Gewaehlter Sound wird im Seriellmonitor mit 115200 Baud protokolliert.

  SD FAT32: Root 001..012.mp3; /02/001..012.mp3; /03/001..006.mp3;
  /04/001..006.mp3; /05/001..005.mp3.
  Root wird ueber DFPlayer-Dateiindex angesprochen: Kopierreihenfolge beachten.
  Optional GAME_MUSIC_FOLDER=1: Spielmusik in /01/001..012.mp3.

  Bibliotheken: FastLED, DFPlayerMini_Fast (+ FireTimer), Adafruit GFX,
  Adafruit LED Backpack (+ Adafruit BusIO), ESP32-Boardpaket.
  Pruefgrenze: Host-Syntax-/Ablauftests ersetzen keinen ESP32-/Hardwaretest.
*/
#define SKETCH_NAME "Crocosauf Deluxe"
#define SKETCH_VERSION "v6.5-BLE"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <FastLED.h>
#include <DFPlayerMini_Fast.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp32-hal-cpu.h"
#include "driver/rtc_io.h"

#define LED_PIN 18
#define NUM_LEDS 20
#define MAUL_SENSOR_PIN 27
#define VOLUME_TASTER 26
#define MODUS_PIN 25
#define I2C_SDA 21
#define I2C_SCL 22
#define DFPLAYER_RX_PIN 16
#define DFPLAYER_TX_PIN 17
#define DFPLAYER_BUSY_PIN 33
#define GAME_MUSIC_FOLDER 0  // 0 = bisheriger Root-Dateiindex; 1 = Ordner /01
#define FOLDER_GAMEOVER 2
#define FOLDER_SYSTEM 3
#define FOLDER_REMINDER 4
#define FOLDER_NEWRND 5

const int ROOT_GAME_TRACK_COUNT=12, GAMEOVER_TRACK_COUNT=12;
const int WELCOME_TRACK_COUNT=6, REMINDER_TRACK_COUNT=6, NEWRND_TRACK_COUNT=5;
const int EFFECT_COUNT=20, DISP_ANIM_COUNT=20, PINCHEN_TOTAL=10;
const uint32_t ALL_EFFECTS=0x000FFFFF;
const uint32_t DEFAULT_REM_EFFECTS=(1UL<<1)|(1UL<<4)|(1UL<<10)|(1UL<<11)|(1UL<<15)|(1UL<<18);
const uint16_t DEFAULT_GAME_MASK=0x003F, DEFAULT_GAMEOVER_MASK=0x003F;
const uint8_t DEFAULT_WELCOME_MASK=1, DEFAULT_REMINDER_MASK=0x3F, DEFAULT_NEWRND_MASK=1;
const uint32_t WIFI_AUTO_OFF_MS=300000, DEEP_SLEEP_AFTER_MS=1800000;
const uint32_t OPEN_CONFIRM_MS=900, RED_PHASE_DURATION=4000;
const uint32_t PINCHEN_PHASE_DURATION=4000, GRUEN_PHASE_DURATION=5200;
const uint32_t NORMAL_GAMEOVER_CUP_MS=5000, effectInterval=4000;
const uint32_t WELCOME_START_TIMEOUT_MS=5000, WELCOME_MAX_MS=180000;
const uint32_t UI_TEST_TIMEOUT_MS=60000;
const uint32_t DF_COMMAND_GAP_MS=200;
const uint16_t PINCHEN_BLINK_INTERVAL=200, PINCH_NUM_BLINK_MS=260;
const char* AP_SSID="crocosauf";
String apPassword;

WebServer server(80);
Preferences prefs;
Preferences wifiPrefs;
HardwareSerial mp3Serial(2);
DFPlayerMini_Fast player;
CRGB leds[NUM_LEDS];
Adafruit_8x8matrix matrix;

uint16_t gameTrackMask=DEFAULT_GAME_MASK, gameOverMask=DEFAULT_GAMEOVER_MASK;
uint8_t welcomeMask=1, reminderMask=0x3F, newRndMask=1;
uint32_t gameEffectMask=ALL_EFFECTS, reminderEffectMask=DEFAULT_REM_EFFECTS;
uint32_t gameDispMask=ALL_EFFECTS;
bool gameTracksRandom=true, gameOverRandom=false, welcomeRandom=false;
bool reminderSoundRandom=true, newRndRandom=false;
bool gameEffectsRandom=false, reminderEffectsRandom=true, gameDispRandom=false;
bool reminderEnabled=true, reminderSoundEnabled=true;
uint32_t reminderIntervalMs=480000;
uint16_t reminderEffectDurationMs=3500;
int volumeLevel=20;
bool isMuted=false;
String standbyText="CROCOSAUF DELUXE";

enum GameStage { READY, PLAYING, GAME_OVER, NEW_ROUND };
GameStage stage=READY;
bool pinchenMode=false, maulOffen=false;
uint32_t stageStarted=0, lastUserActivity=0, lastReminderAt=0, maulOpenSince=0;
int rundeAktuell=1, pinnchenIndex=-1;
// Bisherige Ankerpositionen bleiben erhalten. Indizes beginnen bei 0.
// Linke Haelfte: Anker + LED davor; rechte Haelfte: Anker + LED dahinter.
const uint8_t pinnchenLEDs[PINCHEN_TOTAL][2]={
  {1,0},{3,2},{5,4},{7,6},{9,8},{10,11},{12,13},{14,15},{16,17},{18,19}
};
uint8_t pinchenOrder[PINCHEN_TOTAL]={};
bool pinchenOrderValid=false;
uint16_t usedPinchenMask=0;
RTC_DATA_ATTR uint32_t rtcMagic=0;
RTC_DATA_ATTR uint16_t rtcUsed=0;
RTC_DATA_ATTR uint8_t rtcRound=1, rtcPinchenMode=0;
bool sleepAfterReminder=false;

bool wifiEnabled=false;
uint32_t wifiNoClientSince=0;
bool reminderRunning=false, reminderShowCup=true;
uint32_t reminderStarted=0;
int reminderEffectId=0;

bool uiPlaying=false, uiEffectTesting=false, uiDispTesting=false;
int uiMode=0, uiCurrentTrack=0, uiEffectId=-1, uiDispId=-1;
uint32_t uiTestStarted=0;

enum AudioRole { AUDIO_NONE, AUDIO_GAME, AUDIO_WELCOME, AUDIO_ONCE, AUDIO_TEST };
AudioRole audioRole=AUDIO_NONE;
bool dfplayerInitOk=false, welcomeActive=false, audioSawBusy=false;
bool audioIdleTracking=false, gameAudioFailed=false, volumeDirty=false;
bool audioStopPending=false, audioDisableRepeatPending=false;
uint8_t audioStep=0, audioFolder=0;
int audioTrack=0, currentGameMusicTrack=0;
uint32_t audioStepAt=0, audioStarted=0, audioIdleSince=0, lastAudioCommand=0;
String audioError="";

int lastGameTrack=0, lastGoTrack=0, lastWelcomeTrack=0, lastReminderTrack=0, lastNewTrack=0;
int lastGameEffect=-1, lastRemEffect=-1, lastGameShowPick=-1;
int currentEffect=0, effectPos=0, direction=1, hue=0, brightness=0, brightnessDelta=5;
unsigned long lastEffectSwitch=0, lastEffectUpdate=0;

enum DispMode { DISP_SCROLL, DISP_PINCH_BLINK, DISP_CUP_ANIM, DISP_VOLUME_BARS,
                DISP_MUTED_ICON, DISP_GAME_ANIM, DISP_REMINDER_ANIM };
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
  DebouncedInput(uint8_t p, uint16_t ms):pin(p),debounceMs(ms){}
  void begin() { pinMode(pin, INPUT_PULLUP); raw=stable=digitalRead(pin); changedAt=millis(); }
  bool update() {
    bool value=digitalRead(pin);
    if(value!=raw){raw=value;changedAt=millis();}
    if(uint32_t(millis()-changedAt)>=debounceMs) stable=raw;
    return stable;
  }
};
DebouncedInput reed(MAUL_SENSOR_PIN,35), modeInput(MODUS_PIN,40), volumeInput(VOLUME_TASTER,35);

static bool deadlineReached(uint32_t now,uint32_t end){return int32_t(now-end)>=0;}
static bool dfIsBusyPlaying(){return digitalRead(DFPLAYER_BUSY_PIN)==LOW;}
static void requestAudio(AudioRole role,int folder,int track);
static void restoreDisplay();
static void stopUiTests();
static void stopReminder();
static void startGame();
static void enterDeepSleepNow();
static bool bluetoothHasClient();
static void stopBluetooth();

static const char* EFFECT_NAMES[EFFECT_COUNT] = {
"Blue Scanner","Sparkle White","Green Pulse","Blue Pulse","Rainbow Train",
"Rainbow Fill","Grey Static","Double Sparkle","Mirror Yellow","Aqua Fill Up",
"Comet White","Rainbow Wave","Rainbow Breath","Color Trail","Cyan Dot",
"Confetti","Strobe White","Party Rainbow","Rainbow Glitter","Red/Blue Flip"
};

static const char* DISP_NAMES[DISP_ANIM_COUNT] = {
"Krokodil (Mund auf/zu)","Smiley pulst","Smiley zwinkert","Smiley grinst","Herz schlaegt",
"Confetti","Welle","Balken wandert","Checker Flash","Doppelpfeile rechts",
"Stern funkelt","Ring dreht","Bouncy Ball","Pacman frisst","Ausrufezeichen blinkt",
"LOL Face","Sunglasses Smiley","Question Mark","Fireworks","Mini-Cup blinkt"
};

// ============================================================
// BIG DIGITS 3x7
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
if (d > 9) return;
for (int r=0;r<7;r++){
uint8_t row = DIG3x7[d][r];
for (int c=0;c<3;c++){
if (row & (1<<(2-c))) matrix.drawPixel(x+c, y+r, LED_ON);
}
}
}
static inline void drawNumberCentered(uint8_t n) {
const int y = 0;
matrix.clear();
if (n <= 9) {
drawDigit3x7(n, 2, y);
} else {
drawDigit3x7(1, 0, y);
drawDigit3x7(0, 4, y);
}
matrix.writeDisplay();
}


// Auswahl ohne unmittelbare Wiederholung, sofern mehrere Eintraege aktiv sind.
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
static int pickFromMask_1based(int count,uint32_t mask,bool rnd,int &last) {
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
  text.replace("ä","ae"); text.replace("ö","oe"); text.replace("ü","ue");
  text.replace("Ä","Ae"); text.replace("Ö","Oe"); text.replace("Ü","Ue");
  text.replace("ß","ss");
  String result;
  for(size_t i=0;i<text.length() && result.length()<40;i++){
    uint8_t c=(uint8_t)text[i];
    if(c>=32 && c<=126) result+=(char)c;
  }
  result.trim();
  return result.length() ? result : String("CROCOSAUF DELUXE");
}
static void loadSettings() {
  gameTrackMask=checkedMask(prefs.getUShort("gMask",DEFAULT_GAME_MASK),12,DEFAULT_GAME_MASK);
  gameOverMask=checkedMask(prefs.getUShort("goMask",DEFAULT_GAMEOVER_MASK),12,DEFAULT_GAMEOVER_MASK);
  welcomeMask=checkedMask(prefs.getUChar("wMask",1),6,1);
  reminderMask=checkedMask(prefs.getUChar("rMask",0x3F),6,0x3F);
  newRndMask=checkedMask(prefs.getUChar("nrMask",1),5,1);
  gameTracksRandom=prefs.getBool("gRnd",true);
  gameOverRandom=prefs.getBool("goRnd",false);
  welcomeRandom=prefs.getBool("wRnd",false);
  reminderSoundRandom=prefs.getBool("rRnd",true);
  newRndRandom=prefs.getBool("nrRnd",false);
  gameEffectMask=checkedMask(prefs.getULong("geMask",ALL_EFFECTS),20,ALL_EFFECTS);
  reminderEffectMask=checkedMask(prefs.getULong("reMask",DEFAULT_REM_EFFECTS),20,DEFAULT_REM_EFFECTS);
  gameDispMask=checkedMask(prefs.getULong("gdMask",ALL_EFFECTS),20,ALL_EFFECTS);
  gameEffectsRandom=prefs.getBool("geRnd",false);
  reminderEffectsRandom=prefs.getBool("reRnd",true);
  gameDispRandom=prefs.getBool("gdRnd",false);
  reminderEnabled=prefs.getBool("remEn",true);
  reminderSoundEnabled=prefs.getBool("remSnd",true);
  int minutes=constrain((int)prefs.getUShort("remMin",8),1,60);
  reminderIntervalMs=(uint32_t)minutes*60000UL;
  reminderEffectDurationMs=constrain((int)prefs.getUShort("remDur",3500),1500,9000);
  volumeLevel=prefs.getInt("vol",20);
  // v6.2 speicherte bei Mute 0. Letzten Pegel, falls vorhanden, wiederherstellen.
  if(volumeLevel<5) volumeLevel=prefs.getInt("lastVol",20);
  volumeLevel=constrain(volumeLevel,5,30);
  isMuted=prefs.getBool("mut",false);
  standbyText=displaySafeText(prefs.getString("sTxt","CROCOSAUF DELUXE"));
}
static void saveSettings() {
  prefs.putUShort("gMask",gameTrackMask); prefs.putUShort("goMask",gameOverMask);
  prefs.putUChar("wMask",welcomeMask); prefs.putUChar("rMask",reminderMask);
  prefs.putUChar("nrMask",newRndMask);
  prefs.putBool("gRnd",gameTracksRandom); prefs.putBool("goRnd",gameOverRandom);
  prefs.putBool("wRnd",welcomeRandom); prefs.putBool("rRnd",reminderSoundRandom);
  prefs.putBool("nrRnd",newRndRandom);
  prefs.putULong("geMask",gameEffectMask); prefs.putULong("reMask",reminderEffectMask);
  prefs.putULong("gdMask",gameDispMask);
  prefs.putBool("geRnd",gameEffectsRandom); prefs.putBool("reRnd",reminderEffectsRandom);
  prefs.putBool("gdRnd",gameDispRandom);
  prefs.putBool("remEn",reminderEnabled); prefs.putBool("remSnd",reminderSoundEnabled);
  prefs.putUShort("remMin",reminderIntervalMs/60000UL);
  prefs.putUShort("remDur",reminderEffectDurationMs);
  prefs.putString("sTxt",standbyText);
}
static void saveVolume() {
  // Pegel und Mute sind getrennt; Mute zerstoert den letzten Pegel nicht.
  prefs.putInt("vol",volumeLevel); prefs.putInt("lastVol",volumeLevel);
  prefs.putBool("mut",isMuted);
  volumeDirty=true;
}


static void dispHardClear() { matrix.clear(); matrix.writeDisplay(); }

static int displayedPinchNumber() {
if (rundeAktuell < 1) return 1;
if (rundeAktuell > 10) return 10;
return rundeAktuell;
}

static void dispSetScroll(const String& text, unsigned long holdMs = 0) {
dispMode = DISP_SCROLL;
dispText = text;
scrollX = 8;
dispLast = 0;
dispUntil = (holdMs > 0) ? (millis() + holdMs) : 0;
}

static void dispSetCupAnim(uint8_t repeatTarget, unsigned long holdMs = 0) {
dispMode = DISP_CUP_ANIM;
cupRepeatTarget = repeatTarget;
cupLevel = 0; cupCycle = 0; cupHolding = false;
cupLastStep = millis(); cupHoldStart = 0;
dispUntil = (holdMs > 0) ? (millis() + holdMs) : 0;
}

static void dispSetReminderAnim() {
dispMode = DISP_REMINDER_ANIM;
dispUntil = 0;
remAnimFrame = 0;
remAnimLast = 0;
remBubbleX = 6;
remBubbleY = 1;
}

static void dispSetGameAnim() {
dispMode = DISP_GAME_ANIM;
dispUntil = 0;
forcedGameShow = -1;
gameShowId = (uint8_t)pickEffectFromMask_0based(DISP_ANIM_COUNT, gameDispMask, gameDispRandom, lastGameShowPick);
gameShowLastSwitch = 0;
gameFrame = 0;
gameFrameLast = 0;
}

static void dispSetGameAnimForced(int id) {
dispMode = DISP_GAME_ANIM;
dispUntil = 0;
forcedGameShow = constrain(id, 0, DISP_ANIM_COUNT-1);
gameShowId = forcedGameShow;
gameShowLastSwitch = millis();
gameFrame = 0;
gameFrameLast = 0;
}

static void showVolumeOnMatrix(unsigned long ms = 1200) {
dispMode = DISP_VOLUME_BARS;
dispUntil = millis() + ms;
}

static void showMutedOnMatrix(unsigned long ms = 1200) {
dispMode = DISP_MUTED_ICON;
dispUntil = millis() + ms;
}

static void dispSetPinchBlink() {
dispMode = DISP_PINCH_BLINK;
dispUntil = 0;
pinchBlinkOn = true;
pinchBlinkLast = millis();
pinchBlinkTogglesLeft = 6; // 3x blinken
}


static void dispRender() {
if (dispUntil != 0 && deadlineReached(millis(), dispUntil)) {
  dispUntil = 0;
  restoreDisplay();
}
if (millis() - dispLast < 70) return;
dispLast = millis();

if (dispMode == DISP_PINCH_BLINK) {
unsigned long now = millis();
if (pinchBlinkTogglesLeft > 0) {
if (now - pinchBlinkLast >= PINCH_NUM_BLINK_MS) {
pinchBlinkLast = now;
pinchBlinkOn = !pinchBlinkOn;
pinchBlinkTogglesLeft--;
if (pinchBlinkTogglesLeft == 0) pinchBlinkOn = true;
}
if (pinchBlinkOn) drawNumberCentered((uint8_t)displayedPinchNumber());
else dispHardClear();
} else {
drawNumberCentered((uint8_t)displayedPinchNumber());
}
return;
}

if (dispMode == DISP_GAME_ANIM) {
const unsigned long SWITCH_MS = 1000;
const unsigned long FRAME_MS  = 110;
unsigned long now = millis();

if (gameShowLastSwitch == 0) gameShowLastSwitch = now;
if (gameFrameLast == 0) gameFrameLast = now;

if (forcedGameShow < 0) {
  if (now - gameShowLastSwitch >= SWITCH_MS) {
    gameShowLastSwitch = now;
    gameShowId = (uint8_t)pickEffectFromMask_0based(DISP_ANIM_COUNT, (uint32_t)gameDispMask, gameDispRandom, lastGameShowPick);
    gameFrame = 0;
    gameFrameLast = now;
  }
} else {
  gameShowId = (uint8_t)forcedGameShow;
}

if (now - gameFrameLast >= FRAME_MS) { gameFrameLast = now; gameFrame++; }

matrix.clear();

auto drawSmiley = [&](bool wink, bool bigSmile){
  matrix.drawPixel(2,2,LED_ON);
  matrix.drawPixel(5,2,LED_ON);
  if (wink) { matrix.drawPixel(5,2,LED_OFF); matrix.drawPixel(5,3,LED_ON); }
  if (bigSmile) {
    matrix.drawPixel(2,5,LED_ON); matrix.drawPixel(3,6,LED_ON);
    matrix.drawPixel(4,6,LED_ON); matrix.drawPixel(5,5,LED_ON);
  } else {
    matrix.drawLine(2,6,5,6,LED_ON);
  }
};

switch (gameShowId) {
  case 0: { // Croc mouth
    bool wink = ((gameFrame % 10) == 3 || (gameFrame % 10) == 4);
    bool mouthOpen = ((gameFrame % 12) >= 6);
    matrix.drawPixel(2,2,LED_ON); matrix.drawPixel(2,3,LED_ON);
    if (!wink) { matrix.drawPixel(5,2,LED_ON); matrix.drawPixel(5,3,LED_ON); }
    else { matrix.drawPixel(5,3,LED_ON); }
    if (!mouthOpen) matrix.drawLine(2,6,5,6,LED_ON);
    else {
      matrix.drawLine(2,5,5,5,LED_ON);
      matrix.drawPixel(2,6,LED_ON); matrix.drawPixel(5,6,LED_ON);
    }
    break;
  }
  case 1: drawSmiley(false, ((gameFrame % 8) < 4)); break;
  case 2: drawSmiley(((gameFrame % 10) < 3), true); break;
  case 3: { matrix.drawPixel(2,2,LED_ON); matrix.drawPixel(5,2,LED_ON);
            bool open=((gameFrame%8)<4);
            if(open){ matrix.drawLine(2,5,5,5,LED_ON); matrix.drawPixel(2,6,LED_ON); matrix.drawPixel(5,6,LED_ON);}
            else matrix.drawLine(2,6,5,6,LED_ON);
            break; }
  case 4: { bool big=((gameFrame%10)<5);
            if(!big){ matrix.drawPixel(3,2,LED_ON); matrix.drawPixel(4,2,LED_ON);
                      matrix.drawPixel(2,3,LED_ON); matrix.drawPixel(5,3,LED_ON);
                      matrix.drawPixel(3,3,LED_ON); matrix.drawPixel(4,3,LED_ON);
                      matrix.drawPixel(3,4,LED_ON); matrix.drawPixel(4,4,LED_ON);
                      matrix.drawPixel(3,5,LED_ON); matrix.drawPixel(4,5,LED_ON); }
            else { matrix.drawPixel(2,2,LED_ON); matrix.drawPixel(5,2,LED_ON);
                   matrix.drawPixel(1,3,LED_ON); matrix.drawPixel(6,3,LED_ON);
                   matrix.drawLine(2,4,5,4,LED_ON);
                   matrix.drawPixel(3,5,LED_ON); matrix.drawPixel(4,5,LED_ON);
                   matrix.drawPixel(3,6,LED_ON); matrix.drawPixel(4,6,LED_ON); }
            break; }
  case 5: for(int i=0;i<12;i++) matrix.drawPixel(random(8),random(8),LED_ON); break;
  case 6: { uint8_t p=gameFrame%8; matrix.drawPixel(p,2,LED_ON); matrix.drawPixel((p+2)%8,3,LED_ON);
            matrix.drawPixel((p+4)%8,4,LED_ON); matrix.drawPixel((p+6)%8,5,LED_ON); break; }
  case 7: { uint8_t x=gameFrame%8; for(int y=1;y<7;y++) matrix.drawPixel(x,y,LED_ON); break; }
  case 8: { bool inv=((gameFrame%6)<3);
            for(int y=0;y<8;y++)for(int x=0;x<8;x++){ bool on=((x+y)&1); if(inv)on=!on; if(on)matrix.drawPixel(x,y,LED_ON); }
            break; }
  case 9: { int shift=gameFrame%3;
            matrix.drawPixel(1+shift,3,LED_ON);
            matrix.drawPixel(2+shift,2,LED_ON); matrix.drawPixel(2+shift,3,LED_ON); matrix.drawPixel(2+shift,4,LED_ON);
            matrix.drawPixel(3+shift,3,LED_ON);
            matrix.drawPixel(4+shift,3,LED_ON);
            matrix.drawPixel(5+shift,2,LED_ON); matrix.drawPixel(5+shift,3,LED_ON); matrix.drawPixel(5+shift,4,LED_ON);
            matrix.drawPixel(6+shift,3,LED_ON);
            break; }
  case 10:{ static const uint8_t stars[5][2]={{1,1},{6,1},{2,5},{5,6},{3,3}};
            int k=gameFrame%5; for(int i=0;i<5;i++) matrix.drawPixel(stars[i][0],stars[i][1],LED_ON);
            if((gameFrame%2)==0){ matrix.drawPixel(max(0,(int)stars[k][0]-1),stars[k][1],LED_ON); matrix.drawPixel(min(7,(int)stars[k][0]+1),stars[k][1],LED_ON); }
            break; }
  case 11:{ int f=gameFrame%8; static const uint8_t ring[8][2]={{3,1},{4,1},{6,3},{6,4},{4,6},{3,6},{1,4},{1,3}};
            for(int i=0;i<8;i+=2){ int idx=(i+f)%8; matrix.drawPixel(ring[idx][0],ring[idx][1],LED_ON); }
            break; }
  case 12:{ int x=gameFrame%8; int y=(gameFrame/2)%6; y=(y<=3)?y:(6-y); matrix.drawPixel(x,y+1,LED_ON); matrix.drawPixel(x,y+2,LED_ON); break; }
  case 13:{ int x=gameFrame%9; bool mouth=((gameFrame%6)<3); int px=min(x,7);
            matrix.drawPixel(px,3,LED_ON); matrix.drawPixel(px,4,LED_ON);
            matrix.drawPixel(max(px-1,0),3,LED_ON); matrix.drawPixel(max(px-1,0),4,LED_ON);
            if(mouth) matrix.drawPixel(px,3,LED_OFF);
            for(int i=px+1;i<8;i+=2) matrix.drawPixel(i,3,LED_ON);
            break; }
  case 14:{ bool on=((gameFrame%6)<3); if(on){ matrix.drawLine(4,1,4,5,LED_ON); matrix.drawPixel(4,7,LED_ON);} break; }
  case 15:{ matrix.drawPixel(2,2,LED_ON); matrix.drawPixel(5,2,LED_ON);
            matrix.drawPixel(1,4,LED_ON); matrix.drawPixel(6,4,LED_ON);
            bool mouth=((gameFrame%8)<4); if(mouth){ matrix.drawPixel(2,6,LED_ON); matrix.drawPixel(5,6,LED_ON); matrix.drawPixel(3,7,LED_ON); matrix.drawPixel(4,7,LED_ON);}
            else matrix.drawLine(2,6,5,6,LED_ON);
            break; }
  case 16:{ matrix.drawLine(1,2,6,2,LED_ON); matrix.drawPixel(3,2,LED_ON); matrix.drawPixel(4,2,LED_ON);
            matrix.drawPixel(2,6,LED_ON); matrix.drawPixel(3,7,LED_ON); matrix.drawPixel(4,7,LED_ON); matrix.drawPixel(5,6,LED_ON); break; }
  case 17:{ bool blink=((gameFrame%10)<7); if(blink){ matrix.drawPixel(3,1,LED_ON); matrix.drawPixel(4,1,LED_ON); matrix.drawPixel(5,2,LED_ON);
                                                      matrix.drawPixel(4,3,LED_ON); matrix.drawPixel(4,4,LED_ON); matrix.drawPixel(4,6,LED_ON);} break; }
  case 18:{ int cx=3+((gameFrame/6)%2); int cy=3; int phase=gameFrame%6;
            matrix.drawPixel(cx,cy,LED_ON);
            if(phase>=1){matrix.drawPixel(cx-1,cy,LED_ON);matrix.drawPixel(cx+1,cy,LED_ON);}
            if(phase>=2){matrix.drawPixel(cx,cy-1,LED_ON);matrix.drawPixel(cx,cy+1,LED_ON);}
            if(phase>=3){matrix.drawPixel(cx-1,cy-1,LED_ON);matrix.drawPixel(cx+1,cy+1,LED_ON);}
            if(phase>=4){matrix.drawPixel(cx+1,cy-1,LED_ON);matrix.drawPixel(cx-1,cy+1,LED_ON);}
            if(phase>=5){matrix.drawPixel(random(8),random(8),LED_ON);}
            break; }
  case 19:{ bool on=((gameFrame%8)<4); if(on){ matrix.drawRect(2,2,4,5,LED_ON); matrix.drawPixel(6,3,LED_ON); matrix.drawPixel(6,4,LED_ON);
            int lvl=(gameFrame%4); for(int y=6;y>=6-lvl;y--) matrix.drawLine(3,y,4,y,LED_ON); } break; }
}

matrix.writeDisplay();
return;

}

if (dispMode == DISP_REMINDER_ANIM) {
const unsigned long STEP_MS = 180;
unsigned long now = millis();
if (remAnimLast == 0 || (now - remAnimLast >= STEP_MS)) {
remAnimLast = now;
remAnimFrame ^= 1;
if (remBubbleY == 0) { remBubbleY = 2; remBubbleX = (uint8_t)random(5, 8); }
else remBubbleY--;
}
matrix.clear();
matrix.drawPixel(5,2,LED_ON); matrix.drawPixel(7,2,LED_ON);
matrix.drawLine(4,3,7,3,LED_ON);
matrix.drawPixel(4,4,LED_ON);
matrix.drawLine(1,4,3,4,LED_ON);
if (remAnimFrame == 0) matrix.drawLine(1,5,3,5,LED_ON);
else { matrix.drawLine(1,5,3,5,LED_ON); matrix.drawLine(1,6,3,6,LED_ON); }
matrix.drawPixel(remBubbleX, remBubbleY, LED_ON);
matrix.writeDisplay();
return;
}

// scroll / volume / mute / cup
matrix.clear();
matrix.setTextWrap(false);
matrix.setTextSize(1);
matrix.setTextColor(LED_ON);

if (dispMode == DISP_SCROLL) {
matrix.setCursor(scrollX, 1); // oben & unten je 1 Reihe Luft
matrix.print(dispText);
matrix.writeDisplay();
scrollX--;
int16_t minX = -(int16_t)dispText.length() * 6;
if (scrollX < minX) scrollX = 8;
return;
}

if (dispMode == DISP_VOLUME_BARS) {
int bars = map(volumeLevel, 0, 30, 0, 8);
bars = constrain(bars, 0, 8);
for (int x = 0; x < 8; x++) if (x < bars)
for (int y = 7; y >= 0; y--) matrix.drawPixel(x, y, LED_ON);
matrix.writeDisplay();
return;
}

if (dispMode == DISP_MUTED_ICON) {
matrix.drawPixel(1,3,LED_ON); matrix.drawPixel(1,4,LED_ON);
matrix.drawPixel(2,2,LED_ON); matrix.drawPixel(2,3,LED_ON);
matrix.drawPixel(2,4,LED_ON); matrix.drawPixel(2,5,LED_ON);
matrix.drawPixel(3,1,LED_ON); matrix.drawPixel(3,2,LED_ON);
matrix.drawPixel(3,5,LED_ON); matrix.drawPixel(3,6,LED_ON);
matrix.drawPixel(5,2,LED_ON); matrix.drawPixel(6,3,LED_ON);
matrix.drawPixel(7,4,LED_ON); matrix.drawPixel(7,3,LED_ON);
matrix.drawPixel(6,4,LED_ON); matrix.drawPixel(5,5,LED_ON);
matrix.writeDisplay();
return;
}

if (dispMode == DISP_CUP_ANIM) {
const unsigned long STEP_MS = 160;
const unsigned long HOLD_FULL_MS = 650;
unsigned long now = millis();

matrix.drawRect(2, 1, 4, 6, LED_ON);
matrix.drawPixel(6, 2, LED_ON);
matrix.drawPixel(6, 3, LED_ON);
matrix.drawPixel(6, 4, LED_ON);

bool loopForever = (cupRepeatTarget == 255);

if (!cupHolding) {
  if (now - cupLastStep >= STEP_MS) {
    cupLastStep = now;
    if (cupLevel < 4) cupLevel++;
    else { cupHolding = true; cupHoldStart = now; }
  }
} else {
  if (now - cupHoldStart >= HOLD_FULL_MS) {
    cupCycle++;
    if (loopForever) { cupLevel = 0; cupHolding = false; cupLastStep = now; cupCycle = 0; }
    else {
      if (cupCycle < cupRepeatTarget) { cupLevel = 0; cupHolding = false; cupLastStep = now; }
      else cupLevel = 4;
    }
  }
}

for (int y = 6; y >= 6 - cupLevel + 1; y--) matrix.drawLine(3, y, 4, y, LED_ON);
matrix.writeDisplay();
return;

}
}


static void resetEffects() {
effectPos = 0; direction = 1; hue = 0; brightness = 0; brightnessDelta = 5;
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


// Audio-Befehle mit Abstand senden, ohne die Spielschleife mit delay zu blockieren.
static void stopAudio(bool force=false) {
  if(welcomeActive && !force) return;
  if(dfplayerInitOk) audioStopPending=true;
  audioDisableRepeatPending=false;
  audioRole=AUDIO_NONE; audioStep=0; audioIdleTracking=false; audioSawBusy=false;
  if(force) welcomeActive=false;
}
static void requestAudio(AudioRole role,int folder,int track) {
  if(!dfplayerInitOk || isMuted || welcomeActive) return;
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
  uiPlaying=false; uiMode=0; uiCurrentTrack=0;
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
      case 2: player.volume(isMuted ? 0 : volumeLevel); volumeDirty=false; break;
      case 3: {
        // Kein loop()/0x08: dessen Verhalten unterscheidet sich bei DFPlayer-
        // Varianten. Wiederholung ausschliesslich ueber BUSY und denselben Titel.
        if(audioFolder) player.playFolder(audioFolder,audioTrack);
        else player.play(audioTrack);
        char line[100];
        if(audioFolder) snprintf(line,sizeof(line),"[AUDIO] /%02u/%03d.mp3 | Lautstaerke %d%s",
                                 (unsigned)audioFolder,audioTrack,volumeLevel,isMuted?" (Mute)":"");
        else snprintf(line,sizeof(line),"[AUDIO] Root-Dateiindex %d | Lautstaerke %d%s",
                      audioTrack,volumeLevel,isMuted?" (Mute)":"");
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
    player.volume(isMuted ? 0 : volumeLevel);
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
    welcomeActive=false; audioRole=AUDIO_NONE;audioDisableRepeatPending=false;
    if(wasTest){uiPlaying=false;uiMode=0;uiCurrentTrack=0;}
  }
}
static void startGameMusic() {
  if(isMuted || welcomeActive || !dfplayerInitOk || gameAudioFailed) return;
  requestAudio(AUDIO_GAME,GAME_MUSIC_FOLDER,currentGameMusicTrack);
}
static void playFromSelection(int folder,int count,uint32_t mask,bool rnd,int &last) {
  int track=pickFromMask_1based(count,mask,rnd,last);
  requestAudio(AUDIO_ONCE,folder,track);
}

static void restoreDisplay() {
  dispUntil=0;
  if(reminderRunning) {
    if(reminderShowCup) dispSetCupAnim(255,0); else dispSetReminderAnim();
  } else if(stage==NEW_ROUND) dispSetScroll("NEUE RUNDE");
  else if(stage==GAME_OVER) dispSetCupAnim(255,0);
  else if(stage==PLAYING) {
    if(pinchenMode) dispSetPinchBlink(); else dispSetGameAnim();
  } else if(uiDispTesting) dispSetGameAnimForced(uiDispId);
  else if(pinchenMode) dispSetPinchBlink();
  else if(normalCupUntil && !deadlineReached(millis(),normalCupUntil)) {
    dispSetCupAnim(255,0);
    dispUntil=normalCupUntil;
  } else {
    normalCupUntil=0; dispSetScroll(standbyText);
  }
}
static void stopUiTests() {
  if(uiPlaying || audioRole==AUDIO_TEST) stopAudio();
  uiPlaying=false; uiMode=0; uiCurrentTrack=0;
  uiEffectTesting=false; uiEffectId=-1; uiDispTesting=false; uiDispId=-1;
  forcedGameShow=-1;
  FastLED.clear(); FastLED.show();
}
static void stopReminder() {
  if(!reminderRunning) return;
  reminderRunning=false;
  stopAudio();
  FastLED.clear(); FastLED.show();
  lastReminderAt=millis();  // Keine Benutzeraktivitaet: Schlafzeit bleibt unberuehrt.
  restoreDisplay();
}
static void startReminder() {
  if(stage!=READY || maulOffen || welcomeActive) return;
  stopUiTests();
  stopAudio();
  reminderRunning=true; reminderStarted=millis(); reminderShowCup=true;
  reminderEffectId=pickEffectFromMask_0based(EFFECT_COUNT,reminderEffectMask,
                                            reminderEffectsRandom,lastRemEffect);
  resetEffects(); lastEffectUpdate=0;
  restoreDisplay();
  if(reminderSoundEnabled)
    playFromSelection(FOLDER_REMINDER,REMINDER_TRACK_COUNT,reminderMask,
                      reminderSoundRandom,lastReminderTrack);
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
  stopUiTests(); stopReminder(); stopAudio();
  pinchenMode=newMode;
  stage=READY; stageStarted=millis(); normalCupUntil=0;
  currentGameMusicTrack=0; gameAudioFailed=false;
  resetRounds(); markActivity(); restoreDisplay();
  // Welcome bleibt geschuetzt; die Hauptschleife startet anschliessend im neuen Modus.
}
static void startGame() {
  if(stage!=READY || !maulOffen || welcomeActive) return;
  stopUiTests(); stopReminder();
  stage=PLAYING; stageStarted=millis(); normalCupUntil=0;
  currentGameMusicTrack=pickFromMask_1based(ROOT_GAME_TRACK_COUNT,gameTrackMask,
                                           gameTracksRandom,lastGameTrack);
  gameAudioFailed=false; markActivity();
  resetEffects(); lastEffectSwitch=millis(); lastEffectUpdate=0;
  currentEffect=pickEffectFromMask_0based(EFFECT_COUNT,gameEffectMask,
                                         gameEffectsRandom,lastGameEffect);
  restoreDisplay(); startGameMusic();
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
static void volumeUp() {
  volumeLevel+=5; if(volumeLevel>30) volumeLevel=5;
  isMuted=false; saveVolume(); markActivity(); showVolumeOnMatrix();
  if(stage==PLAYING && audioRole!=AUDIO_GAME){gameAudioFailed=false;startGameMusic();}
}
static void toggleMute() {
  isMuted=!isMuted; saveVolume(); markActivity();
  if(isMuted) showMutedOnMatrix(); else showVolumeOnMatrix();
  if(!isMuted && stage==PLAYING && audioRole!=AUDIO_GAME){
    gameAudioFailed=false;startGameMusic();
  }
}
static void handleVolumeButton() {
  static bool last=HIGH, held=false, armed=false;
  static uint32_t pressed=0;
  bool value=volumeInput.update();
  if(!maulOffen){last=value;held=false;armed=false;return;}
  if(value==LOW && last==HIGH){pressed=millis();held=false;armed=true;}
  if(value==LOW && armed && !held && uint32_t(millis()-pressed)>=1000){
    held=true;toggleMute();
  }
  if(value==HIGH && last==LOW && armed){
    if(!held) volumeUp();
    armed=false;
  }
  last=value;
}
static void maybeDisableWifiIfIdle() {
  if(!wifiEnabled) return;
  if(WiFi.softAPgetStationNum()>0){wifiNoClientSince=millis();return;}
  if(uint32_t(millis()-wifiNoClientSince)<WIFI_AUTO_OFF_MS) return;
  server.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); wifiEnabled=false;
  // WLAN-Abschaltung unterbricht keine Bluetooth-Tests.
}
static void enterDeepSleepNow() {
  if(maulOffen || digitalRead(MAUL_SENSOR_PIN)==HIGH || stage!=READY || welcomeActive || bluetoothHasClient()) return;
  stopBluetooth();
  stopUiTests(); stopReminder(); stopAudio(true);
  // Vor Deep Sleep muss der vorgemerkte Stopp tatsaechlich gesendet werden.
  if(audioStopPending) {delay(DF_COMMAND_GAP_MS);updateAudio();}
  FastLED.clear(); FastLED.show(); dispHardClear();
  if(wifiEnabled){server.stop();WiFi.softAPdisconnect(true);}
  WiFi.mode(WIFI_OFF); wifiEnabled=false;
  rtcMagic=0xC60C6301; rtcRound=rundeAktuell; rtcUsed=usedPinchenMask;
  rtcPinchenMode=pinchenMode;
  rtc_gpio_init((gpio_num_t)MAUL_SENSOR_PIN);
  rtc_gpio_set_direction((gpio_num_t)MAUL_SENSOR_PIN,RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)MAUL_SENSOR_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)MAUL_SENSOR_PIN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)MAUL_SENSOR_PIN,1);
  if(reminderEnabled) {
    uint32_t since=millis()-lastReminderAt;
    uint32_t remaining=since>=reminderIntervalMs ? 1 : reminderIntervalMs-since;
    esp_sleep_enable_timer_wakeup((uint64_t)remaining*1000ULL);
  }
  esp_deep_sleep_start();
}

// WebUI: lokale HTML-Seiten, keine Internetverbindung und keine Zusatz-App noetig.
static String htmlEscape(const String& value) {
  String out;
  for(size_t i=0;i<value.length();i++){
    char c=value[i];
    if(c=='&') out+="&amp;";
    else if(c=='<') out+="&lt;";
    else if(c=='>') out+="&gt;";
    else if(c=='"') out+="&quot;";
    else if(c=='\'') out+="&#39;";
    else out+=c;
  }
  return out;
}
static String jsonEscape(const String& value) {
  String out;
  for(size_t i=0;i<value.length();i++){
    uint8_t c=(uint8_t)value[i];
    if(c=='"') out+="\\\"";
    else if(c=='\\') out+="\\\\";
    else if(c<32){char buf[7];snprintf(buf,sizeof(buf),"\\u%04x",(unsigned)c);out+=buf;}
    else out+=(char)c;
  }
  return out;
}
static String statusJson() {
  String state=stage==PLAYING ? "Spiel laeuft" : stage==GAME_OVER ? "Game Over" :
               stage==NEW_ROUND ? "Neue Runde" : "Bereit";
  if(welcomeActive) state="Welcome";
  else if(reminderRunning) state="Reminder";
  String j="{\"version\":\""+String(SKETCH_VERSION)+"\",\"state\":\""+state+"\"";
  j+=",\"pinchen\":"+String(pinchenMode?"true":"false")+",\"round\":"+String(rundeAktuell);
  j+=",\"open\":"+String(maulOffen?"true":"false")+",\"vol\":"+String(volumeLevel);
  j+=",\"muted\":"+String(isMuted?"true":"false");
  j+=",\"dfOk\":"+String(dfplayerInitOk?"true":"false")+",\"busy\":"+String(dfIsBusyPlaying()?"true":"false");
  j+=",\"clients\":"+String(WiFi.softAPgetStationNum());
  j+=",\"uiPlaying\":"+String(uiPlaying?"true":"false")+",\"uiMode\":"+String(uiMode);
  j+=",\"uiTrack\":"+String(uiCurrentTrack)+",\"testEffect\":"+String(uiEffectId);
  j+=",\"audioFolder\":"+String(audioFolder)+",\"audioTrack\":"+String(audioTrack);
  j+=",\"testDisplay\":"+String(uiDispId);
  j+=",\"standby\":\""+jsonEscape(standbyText)+"\",\"error\":\""+jsonEscape(audioError)+"\"}";
  return j;
}
static String checked(bool value){return value ? " checked" : "";}
static String actionButton(const String& text,const String& url,bool confirm=false) {
  return "<button type='button' data-cmd='"+htmlEscape(url)+"'"+
         String(confirm?" data-confirm='1'":"")+">"+htmlEscape(text)+"</button>";
}
static String modes(bool rnd) {
  return "<div class='row'><label><input type='radio' name='mode' value='rnd'"+checked(rnd)+
         "> Zufall</label><label><input type='radio' name='mode' value='seq'"+checked(!rnd)+
         "> Reihe</label></div>";
}
static String selectionForm(const String& key,const String& title,int count,
                            uint32_t mask,bool rnd,int kind) {
  String h="<section><h2>"+title+"</h2><form method='post' action='/save?group="+key+"'>";
  h+=modes(rnd);
  for(int i=0;i<count;i++){
    String label=kind==0 ? "Track "+String(i+1) :
                 kind==1 ? String(EFFECT_NAMES[i]) : String(DISP_NAMES[i]);
    String url=kind==0 ? "/play?src="+key+"&t="+String(i+1) :
               kind==1 ? "/testEffect?id="+String(i) : "/testDisp?id="+String(i);
    h+="<div class='row'><label><input type='checkbox' name='m"+String(i)+"' value='1'"+
       checked(mask&(1UL<<i))+"> "+htmlEscape(label)+"</label>";
    h+=actionButton("Test / Stop",url)+"</div>";
  }
  h+="<p><button type='submit'>Auswahl speichern</button></p></form></section>";
  return h;
}
static String renderPage(const String& tab) {
  String h; h.reserve(15000);
  h=R"HTML(<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Crocosauf Deluxe</title>
<style>
*{box-sizing:border-box}body{font:16px system-ui,sans-serif;margin:0;background:#111812;color:#ecf5ed}
main{max-width:900px;margin:auto;padding:20px}h1{color:#a6e461;margin-bottom:4px}
h2{font-size:1.2rem}section{background:#1e2a20;padding:18px;margin:16px 0;border-radius:12px}
nav{display:flex;flex-wrap:wrap;gap:9px;margin:20px 0}nav a{color:#c9f4aa;padding:10px;background:#263b29;border-radius:8px;text-decoration:none}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:7px 0;flex-wrap:wrap}
button,input[type=number],input[type=text]{font:inherit;border-radius:7px;border:1px solid #699b51;padding:9px}
button{background:#a6e461;color:#102008;cursor:pointer}input[type=checkbox],input[type=radio]{width:20px;height:20px;vertical-align:middle}
input[type=text]{width:100%}small,.hint{color:#b9c7ba}#status,#message{white-space:pre-wrap}#message{color:#ffce7c;min-height:1.5em}
</style></head><body><main><h1>CROCOSAUF DELUXE</h1>)HTML";
  h+="<small>mkrativ.de · "+String(SKETCH_VERSION)+"</small>";
  h+="<p class='hint'>WLAN: crocosauf · Individuelles Passwort im USB-Seriellmonitor (115200 Baud). Bedienung ohne Internet.</p>";
  h+="<nav><a href='/'>Musik</a><a href='/effects'>Effekte</a><a href='/system'>System</a><a href='/help'>Spielablauf</a></nav>";
  h+="<section><div id='status'>Status wird geladen …</div><p id='message' role='status'></p>";
  h+=actionButton("Tests / Reminder stoppen","/stop")+"</section>";
  if(tab=="music") {
    h+="<p>Häkchen aktivieren die Auswahl. Tests sind bei geschlossenem Maul im Bereitschaftszustand möglich. Leere Auswahl stellt die Standardauswahl wieder her.</p>";
    h+=selectionForm("g",GAME_MUSIC_FOLDER ? "Spielmusik · Ordner /01" : "Spielmusik · Root-Dateiindex",12,gameTrackMask,gameTracksRandom,0);
    h+=selectionForm("go","GameOver · Ordner /02",12,gameOverMask,gameOverRandom,0);
    h+=selectionForm("w","Welcome · Ordner /03",6,welcomeMask,welcomeRandom,0);
    h+=selectionForm("r","Reminder · Ordner /04",6,reminderMask,reminderSoundRandom,0);
    h+=selectionForm("nr","Neue Runde · Ordner /05",5,newRndMask,newRndRandom,0);
  } else if(tab=="effects") {
    h+=selectionForm("ge","LED-Effekte beim Spielen",20,gameEffectMask,gameEffectsRandom,1);
    h+=selectionForm("re","LED-Effekte beim Reminder",20,reminderEffectMask,reminderEffectsRandom,1);
    h+=selectionForm("gd","Display im Normalmodus",20,gameDispMask,gameDispRandom,2);
  } else if(tab=="system") {
    h+="<section><h2>Laufschrift</h2><form method='post' action='/saveText'><label>Text (max. 40 darstellbare Zeichen)";
    h+="<input type='text' name='txt' maxlength='80' value='"+htmlEscape(standbyText)+"'></label>";
    h+="<p><button>Speichern</button></p></form><small>Umlaute werden zu ae/oe/ue, ß zu ss. Emojis werden entfernt.</small></section>";
    h+="<section><h2>Reminder</h2><form method='post' action='/saveReminderCfg'>";
    h+="<p><label><input type='checkbox' name='ren'"+checked(reminderEnabled)+"> Reminder aktiv</label></p>";
    h+="<p><label><input type='checkbox' name='rsnd'"+checked(reminderSoundEnabled)+"> Reminder mit Sound</label></p>";
    h+="<p><label>Intervall (Minuten) <input type='number' name='rmin' min='1' max='60' value='"+String(reminderIntervalMs/60000UL)+"'></label></p>";
    h+="<p><label>Dauer (Millisekunden) <input type='number' name='rdur' min='1500' max='9000' step='100' value='"+String(reminderEffectDurationMs)+"'></label></p>";
    h+="<button>Speichern</button></form><p>"+actionButton("Reminder testen","/testReminder")+"</p></section>";
    h+="<section><h2>Lautstärke</h2><p>"+actionButton("Lauter","/volUp")+" "+actionButton("Mute an / aus","/muteToggle")+"</p>";
    h+="<p>Am Gerät bei offenem Maul: kurz drücken = +5, etwa 1 Sekunde halten = Mute. Nach 30 folgt 5.</p></section>";
    h+="<section><h2>Werkseinstellungen</h2><p>Setzt Auswahl, Texte, Lautstärke und Reminder sowie den Pinchen-Zähler zurück. SD-Dateien bleiben erhalten.</p>";
    h+=actionButton("Werkseinstellungen laden","/factoryReset",true)+"</section>";
  } else {
    h+=R"HTML(<section><h2>Normalmodus</h2><ol>
<li>Maul öffnen. Nach 0,9 Sekunden stabil offen startet das Spiel, sobald Welcome beendet ist.</li>
<li>Spielmusik, LED-Effekte und Displayanimationen laufen. Die Mitspieler drücken reihum die mechanischen Zähne.</li>
<li>Beim Zuschnappen stoppt die Spielmusik. GameOver-Sound, 4 Sekunden rote Blinkphase und etwa 5 Sekunden Trinkglas folgen.</li>
<li>Bei geschlossenem Maul folgt die Laufschrift. Ein neues Spiel kann nach der Rotphase mit offenem Maul beginnen.</li>
</ol><p>Der ESP32 erkennt nur offen/geschlossen. Den auslösenden Zahn bestimmt die Mechanik, nicht der Sketch.</p></section>
<section><h2>Pinchenmodus</h2><p>Zehn Runden. Das Display zeigt die Rundennummer.
Nach jedem Zuschnappen: 4 Sekunden rot, dann 4 Sekunden eine türkise LED am zufällig ausgewählten Pinchen.
Jedes Pinchen wird innerhalb einer Serie genau einmal verwendet. Das Display zeigt während GameOver das Glas.
Danach erscheint die nächste Rundennummer. Nach Runde 10 folgen 5,2 Sekunden grünes Blinken,
NEUE RUNDE und der Sound aus /05. Dann geht es wieder bei Runde 1 los.</p>
<p>Ein Moduswechsel bricht die aktuelle Runde ab und setzt die Serie zurück. Bei offenem Maul kann das nächste Spiel direkt starten.</p></section>
<section><h2>Reminder, WLAN und Schlaf</h2><p>Standard: alle 8 Minuten ohne Bedienung ein 3,5 Sekunden langer Reminder,
mit Glas/Krokodil-Wechsel, eigenen LED-Effekten und optionalem Sound. Er startet nur bei geschlossenem Maul.
Nach 30 Minuten ohne Bedienung kann der ESP32 schlafen. Öffnen weckt ihn;
aktivierte Reminder wecken ihn zeitgesteuert kurz. Rundenstand bleibt beim Schlaf erhalten.</p>
<p>WLAN startet nach einem Einschalt-Reset, nicht beim Aufwachen aus Deep Sleep.
Nach 5 Minuten ohne verbundenes Gerät schaltet es ab. Wieder einschalten: Strom aus/ein.
Solange ein WLAN-Gerät verbunden ist, wird der Schlafmodus aufgeschoben.</p></section>
<section><h2>Sound und Tests</h2><p>Tests enden spätestens nach 60 Sekunden. Maul öffnen oder Modus wechseln beendet sie.
Welcome ist geschützt. Bei fehlendem BUSY-Startsignal nach 5 Sekunden oder dauerhaft LOW nach 180 Sekunden
wird die Sperre aufgehoben und ein Fehler angezeigt. Normale Welcome-Dateien mit korrektem BUSY laufen vollständig.</p>
<p>Die Root-Tracks des DFPlayers richten sich nach dem Dateiindex/Kopierablauf.
Änderungen an Sounds erfolgen auf der SD-Karte; die WebUI lädt keine Musik hoch.</p></section>)HTML";
  }
  h+="<p class='hint'>Support: mkrativ.design@gmail.com</p>";
  h+=R"HTML(<script>
const statusBox=document.getElementById('status'),msg=document.getElementById('message');
async function refresh(){
 try{
  const res=await fetch('/status',{cache:'no-store'});if(!res.ok)throw Error('Status nicht erreichbar');
  const s=await res.json();
  let t=s.version+' · '+(s.pinchen?'Pinchen, Runde '+s.round:'Normalmodus')+' · '+s.state;
  t+='\nMaul '+(s.open?'offen':'geschlossen')+' · Lautstärke '+s.vol+(s.muted?' (stumm)':'');
  t+='\nDFPlayer '+(s.dfOk?'initialisiert':'nicht initialisiert')+' · BUSY '+(s.busy?'LOW':'HIGH')+' · WLAN-Geräte '+s.clients;
  if(s.uiPlaying)t+='\nSoundtest: Gruppe '+s.uiMode+', Track '+s.uiTrack;
  if(s.testEffect>=0)t+='\nLED-Test '+(s.testEffect+1);
  if(s.testDisplay>=0)t+='\nDisplaytest '+(s.testDisplay+1);
  if(s.error)t+='\nHinweis: '+s.error;
  statusBox.textContent=t;
 }catch(e){statusBox.textContent='Keine Verbindung. WLAN-Verbindung mit crocosauf prüfen.';}
}
document.addEventListener('click',async e=>{
 const b=e.target.closest('button[data-cmd]');if(!b)return;
 if(b.dataset.confirm && !confirm('Alle Einstellungen auf Standard zurücksetzen?'))return;
 b.disabled=true;
 try{
  const res=await fetch(b.dataset.cmd,{method:'POST'});
  const text=await res.text();if(!res.ok)throw Error(text);
  msg.textContent=text;
  if(b.dataset.cmd==='/factoryReset'){location.reload();return;}
  await refresh();
 }catch(err){msg.textContent=err.message;}
 finally{b.disabled=false;}
});
refresh();setInterval(refresh,1500);
</script></main></body></html>)HTML";
  return h;
}
// === ControlApi.h (fuer die Einzeldatei eingebunden) ===
// Gemeinsame Befehlsverarbeitung: HTTP und BLE verwenden dieselben Handler.
// Zugriff ausschliesslich aus loop(), niemals aus dem Bluetooth-Callback.
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
    if(c=='+') out+=' ';
    else if(c=='%' && i+2<in.length() && hexValue(in[i+1])>=0 && hexValue(in[i+2])>=0) {
      out+=(char)((hexValue(in[i+1])<<4)|hexValue(in[i+2])); i+=2;
    } else out+=c;
  }
  return out;
}
static bool queryValue(const String& query,const String& key,String& value) {
  size_t start=0;
  while(start<query.length()) {
    size_t end=start; while(end<query.length() && query[end]!='&') end++;
    size_t eq=start; while(eq<end && query[eq]!='=') eq++;
    if(urlDecode(query.substring(start,eq))==key) {
      value=eq<end ? urlDecode(query.substring(eq+1,end)) : String(""); return true;
    }
    start=end+1;
  }
  return false;
}
static String apiArg(const String& key) {
  if(!appRequest) return server.arg(key);
  String result; queryValue(appQuery,key,result); return result;
}
static bool apiHasArg(const String& key) {
  if(!appRequest) return server.hasArg(key);
  String value; return queryValue(appQuery,key,value);
}
static void apiSend(int code,const String& type,const String& body) {
  if(!appRequest) {server.send(code,type,body);return;}
  appResponseCode=code; appResponse=body;
}


static bool requireIdle() {
  if(gameIsBusyForUi()){apiSend(409,"text/plain; charset=utf-8","Bitte Spiel/Welcome abwarten und Maul schliessen.");return false;}
  return true;
}
static void ok(const char* message="OK"){apiSend(200,"text/plain; charset=utf-8",message);}
static void handleStop() {
  if(welcomeActive || stage!=READY){apiSend(409,"text/plain","Welcome oder Spiel ist aktiv.");return;}
  stopUiTests(); stopReminder(); stopAudio(); markActivity(); restoreDisplay(); ok("Tests und Reminder gestoppt.");
}
static void handlePlay() {
  if(!requireIdle()) return;
  if(!dfplayerInitOk){apiSend(503,"text/plain","DFPlayer nicht initialisiert.");return;}
  if(isMuted){apiSend(409,"text/plain","Bitte zuerst Mute ausschalten.");return;}
  String src=apiArg("src"); int count=0,folder=0,mode=0;
  if(src=="g"){count=12;folder=GAME_MUSIC_FOLDER;mode=1;}
  else if(src=="w"){count=6;folder=3;mode=2;}
  else if(src=="go"){count=12;folder=2;mode=3;}
  else if(src=="r"){count=6;folder=4;mode=4;}
  else if(src=="nr"){count=5;folder=5;mode=5;}
  int track=apiArg("t").toInt();
  if(track<1 || track>count){apiSend(400,"text/plain","Ungueltiger Track.");return;}
  bool same=uiPlaying && uiMode==mode && uiCurrentTrack==track;
  stopUiTests(); markActivity(); restoreDisplay();
  if(same){ok("Soundtest gestoppt.");return;}
  uiPlaying=true;uiMode=mode;uiCurrentTrack=track;uiTestStarted=millis();
  requestAudio(AUDIO_TEST,folder,track);ok("Soundtest gestartet.");
}
static void handleTestVisual(bool display) {
  if(!requireIdle()) return;
  int id=apiArg("id").toInt();
  if(!apiHasArg("id") || id<0 || id>=20){apiSend(400,"text/plain","Ungueltiger Effekt.");return;}
  bool same=display ? (uiDispTesting && uiDispId==id) : (uiEffectTesting && uiEffectId==id);
  stopUiTests();markActivity();
  if(!same){
    uiTestStarted=millis();
    if(display){uiDispTesting=true;uiDispId=id;}
    else{uiEffectTesting=true;uiEffectId=id;resetEffects();lastEffectUpdate=0;}
  }
  restoreDisplay();ok(same?"Test gestoppt.":"Test gestartet.");
}
static uint32_t postedMask(int count) {
  uint32_t m=0;
  for(int i=0;i<count;i++) if(apiHasArg("m"+String(i))) m|=(1UL<<i);
  return m;
}
static void redirect(const char* path) {
  if(appRequest){apiSend(200,"text/plain","Einstellungen gespeichert.");return;}
  server.sendHeader("Location",path);server.send(303,"text/plain","");
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
  standbyText=displaySafeText(apiArg("txt"));saveSettings();markActivity();
  if(stage==READY && !reminderRunning && !uiDispTesting) restoreDisplay();
  redirect("/system");
}
static void handleSaveReminderCfg() {
  reminderEnabled=apiHasArg("ren");reminderSoundEnabled=apiHasArg("rsnd");
  long minutes=apiArg("rmin").toInt(),duration=apiArg("rdur").toInt();
  minutes=constrain(minutes,1L,60L);duration=constrain(duration,1500L,9000L);
  reminderIntervalMs=(uint32_t)minutes*60000UL;reminderEffectDurationMs=(uint16_t)duration;
  if(reminderRunning) stopReminder();
  saveSettings();markActivity();redirect("/system");
}
static void handleFactoryReset() {
  if(!requireIdle()) return;
  stopUiTests();stopAudio();prefs.clear();loadSettings();
  saveSettings();saveVolume();resetRounds();
  lastGameTrack=lastGoTrack=lastWelcomeTrack=lastReminderTrack=lastNewTrack=0;
  lastGameEffect=lastRemEffect=lastGameShowPick=-1;
  normalCupUntil=0;audioError="";gameAudioFailed=false;
  markActivity();restoreDisplay();ok("Werkseinstellungen geladen.");
}
static void startWebUI() {
  if(!WiFi.mode(WIFI_AP)) return;
  // Hardware-Zufall erst bei aktivem WLAN verwenden. Kein gemeinsames Passwort im Quellcode.
  apPassword=wifiPrefs.getString("apPass","");
  if(apPassword.length()!=16) {
    static const char alphabet[]="23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    apPassword="";
    for(int i=0;i<16;i++) apPassword+=alphabet[esp_random()&31U];
    wifiPrefs.putString("apPass",apPassword);
  }
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  wifiEnabled=WiFi.softAP(AP_SSID,apPassword.c_str());
  if(!wifiEnabled){WiFi.mode(WIFI_OFF);return;}
  Serial.println("Crocosauf WLAN-Passwort: "+apPassword);
  wifiNoClientSince=millis();
  server.on("/",HTTP_GET,[]{server.send(200,"text/html; charset=utf-8",renderPage("music"));});
  server.on("/effects",HTTP_GET,[]{server.send(200,"text/html; charset=utf-8",renderPage("effects"));});
  server.on("/system",HTTP_GET,[]{server.send(200,"text/html; charset=utf-8",renderPage("system"));});
  server.on("/help",HTTP_GET,[]{server.send(200,"text/html; charset=utf-8",renderPage("help"));});
  server.on("/status",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",statusJson());});
  server.on("/stop",HTTP_POST,handleStop);
  server.on("/play",HTTP_POST,handlePlay);
  server.on("/testEffect",HTTP_POST,[]{handleTestVisual(false);});
  server.on("/testDisp",HTTP_POST,[]{handleTestVisual(true);});
  server.on("/volUp",HTTP_POST,[]{volumeUp();ok("Lautstaerke erhoeht.");});
  server.on("/muteToggle",HTTP_POST,[]{toggleMute();ok("Mute umgeschaltet.");});
  server.on("/save",HTTP_POST,handleSaveSelection);
  server.on("/saveText",HTTP_POST,handleSaveText);
  server.on("/saveReminderCfg",HTTP_POST,handleSaveReminderCfg);
  server.on("/testReminder",HTTP_POST,[]{if(requireIdle()){markActivity();startReminder();ok("Reminder gestartet.");}});
  server.on("/factoryReset",HTTP_POST,handleFactoryReset);
  server.onNotFound([]{server.send(404,"text/plain","Seite nicht gefunden.");});
  server.begin();
}

// === BluetoothControl.h (fuer die Einzeldatei eingebunden) ===
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <atomic>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// BLE ist eine Fernbedienung, kein Bluetooth-Audioempfaenger.
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
struct BleRequest { uint32_t epoch; char text[512]; };
static QueueHandle_t bleQueue=nullptr;
static String bleOutput;
static size_t bleOutputOffset=0;
static bool bleButtonArmed=false,bleButtonDown=false,bleButtonUsed=false;
static uint32_t bleButtonAt=0;

class CrocoServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer*) override {bleConnected.store(true);bleEpoch.fetch_add(1);}
  void onDisconnect(BLEServer*) override {bleConnected.store(false);bleEpoch.fetch_add(1);}
};
class CrocoRxCallbacks: public BLECharacteristicCallbacks {
  char line[512]={}; size_t used=0; bool overflow=false; uint32_t epoch=0;
  void onWrite(BLECharacteristic* characteristic) override {
    uint32_t current=bleEpoch.load();
    if(epoch!=current){epoch=current;used=0;overflow=false;}
    auto bytes=characteristic->getValue();
    for(size_t i=0;i<bytes.length();i++) {
      char c=bytes[i];
      if(c=='\n') {
        if(!overflow && used && bleConnected.load()) {
          line[used]=0; BleRequest req{};req.epoch=current;
          memcpy(req.text,line,used+1);
          // Volle Queue: kein Befehl wird teilweise ausgefuehrt. App meldet Timeout.
          xQueueSend(bleQueue,&req,0);
        }
        used=0;overflow=false;
      } else if(c!='\r') {
        if(c=='\0' || used>=sizeof(line)-1) overflow=true;
        else if(!overflow) line[used++]=c;
      }
    }
  }
};
static void startBluetooth() {
  if(bleStarted) return;
  bleQueue=xQueueCreate(4,sizeof(BleRequest));
  if(!bleQueue){Serial.println("BLE: keine Befehlsqueue verfuegbar.");return;}
  BLEDevice::init("Crocosauf Deluxe");
  bleServer=BLEDevice::createServer();
  bleServer->setCallbacks(new CrocoServerCallbacks());
  BLEService* service=bleServer->createService(BLE_SERVICE);
  bleTx=service->createCharacteristic(BLE_TX,BLECharacteristic::PROPERTY_NOTIFY);
  bleCccd=new BLE2902();bleTx->addDescriptor(bleCccd);
  BLECharacteristic* rx=service->createCharacteristic(BLE_RX,BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new CrocoRxCallbacks());
  service->start();
  BLEAdvertising* advertising=BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE);advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);advertising->setMinPreferred(0x12);
  advertising->start();bleStarted=true;
}
static bool bluetoothHasClient(){return bleStarted && bleConnected.load();}
static void stopBluetooth() {
  if(bleStarted) BLEDevice::getAdvertising()->stop();
  // Kein deinit/delete waehrend eines Callbacks. Unmittelbar danach Deep Sleep.
}
static String configJson() {
  String j="{\"protocol\":1,\"masks\":{";
  j+="\"g\":"+String(gameTrackMask)+",\"go\":"+String(gameOverMask)+",\"w\":"+String(welcomeMask);
  j+=",\"r\":"+String(reminderMask)+",\"nr\":"+String(newRndMask)+",\"ge\":"+String(gameEffectMask);
  j+=",\"re\":"+String(reminderEffectMask)+",\"gd\":"+String(gameDispMask)+"},\"random\":{";
  j+="\"g\":"+String(gameTracksRandom?"true":"false")+",\"go\":"+String(gameOverRandom?"true":"false");
  j+=",\"w\":"+String(welcomeRandom?"true":"false")+",\"r\":"+String(reminderSoundRandom?"true":"false");
  j+=",\"nr\":"+String(newRndRandom?"true":"false")+",\"ge\":"+String(gameEffectsRandom?"true":"false");
  j+=",\"re\":"+String(reminderEffectsRandom?"true":"false")+",\"gd\":"+String(gameDispRandom?"true":"false")+"}";
  j+=",\"remEn\":"+String(reminderEnabled?"true":"false")+",\"remSnd\":"+String(reminderSoundEnabled?"true":"false");
  j+=",\"remMin\":"+String(reminderIntervalMs/60000)+",\"remDur\":"+String(reminderEffectDurationMs);
  j+=",\"standby\":\""+jsonEscape(standbyText)+"\",\"gameFolder\":"+String(GAME_MUSIC_FOLDER)+"}";
  return j;
}
// Format: ID /pfad?formularparameter\n. Pro App-Verbindung ein Befehl gleichzeitig.
static void dispatchBluetooth(const String& line) {
  size_t split=0;while(split<line.length() && line[split]!=' ') split++;
  String id=line.substring(0,split);
  if(id.length()<1 || id.length()>8) return;
  for(size_t i=0;i<id.length();i++) if(id[i]<'0' || id[i]>'9') return;
  String request=split<line.length()?line.substring(split+1,line.length()):String("");
  size_t q=0;while(q<request.length() && request[q]!='?') q++;
  String path=request.substring(0,q);
  appQuery=q<request.length()?request.substring(q+1,request.length()):String("");
  appRequest=true;appResponseCode=404;appResponse="Unbekannter Befehl.";
  if(path=="/hello") {
    appResponseCode=200;
    appResponse="{\"product\":\"crocosauf\",\"protocol\":1,\"version\":\""+String(SKETCH_VERSION)+
      "\",\"approved\":"+String(bleApproved?"true":"false")+"}";
  } else if(!bleApproved) {appResponseCode=403;appResponse="Am Geraet: Maul zu, Bereitschaft abwarten, Taste 3 Sekunden halten.";}
  else if(path=="/status"){appResponseCode=200;appResponse=statusJson();}
  else if(path=="/config"){appResponseCode=200;appResponse=configJson();}
  else if(path=="/play") handlePlay();
  else if(path=="/testEffect") handleTestVisual(false);
  else if(path=="/testDisp") handleTestVisual(true);
  else if(path=="/stop") handleStop();
  else if(path=="/save") handleSaveSelection();
  else if(path=="/saveText") handleSaveText();
  else if(path=="/saveReminderCfg") handleSaveReminderCfg();
  else if(path=="/testReminder") {if(requireIdle()){markActivity();startReminder();ok("Reminder gestartet.");}}
  else if(path=="/volUp") {volumeUp();ok("Lautstaerke erhoeht.");}
  else if(path=="/muteToggle") {toggleMute();ok("Mute umgeschaltet.");}
  else if(path=="/volume") {
    String value=apiArg("v");bool valid=value.length()>0 && value.length()<=2;
    for(size_t i=0;i<value.length();i++) if(value[i]<'0' || value[i]>'9') valid=false;
    int volume=value.toInt();
    if(!valid || volume<5 || volume>30) apiSend(400,"text/plain","Lautstaerke muss 5 bis 30 sein.");
    else {volumeLevel=volume;saveVolume();markActivity();showVolumeOnMatrix();ok("Lautstaerke gespeichert.");}
  } else if(path=="/factoryReset") {
    if(apiArg("confirm")!="yes") apiSend(400,"text/plain","Bestaetigung fehlt.");
    else handleFactoryReset();
  }
  appRequest=false;
  appResponse.replace("\n"," ");appResponse.replace("\r"," ");
  bleOutput=id+" "+String(appResponseCode)+" "+appResponse+"\n";
  bleOutputOffset=0;
}
static void updateBluetooth() {
  if(!bleStarted) return;
  uint32_t now=millis(),epoch=bleEpoch.load();
  if(epoch!=bleSeenEpoch) {
    bleSeenEpoch=epoch;bleApproved=false;bleOutput="";bleOutputOffset=0;
    bleButtonArmed=false;bleButtonDown=false;bleButtonUsed=false;bleConnectedAt=now;
    if(!bleConnected.load()) BLEDevice::startAdvertising();
  }
  if(!bleConnected.load()) return;
  // Freigabe gilt nur fuer diese Verbindung. Ein neues Handy erfordert Tastendruck.
  bool canApprove=!maulOffen && stage==READY && !welcomeActive && !reminderRunning;
  bool pressed=(volumeInput.stable==LOW);
  if(!pressed){bleButtonArmed=true;bleButtonDown=false;bleButtonUsed=false;}
  if(canApprove && !bleApproved && pressed && bleButtonArmed) {
    if(!bleButtonDown){bleButtonDown=true;bleButtonAt=now;}
    if(!bleButtonUsed && uint32_t(now-bleButtonAt)>=3000) {
      bleButtonUsed=true;bleApproved=true;markActivity();dispSetScroll("APP OK",1800);
    }
  } else if(!canApprove) bleButtonDown=false;
  if(!bleApproved && uint32_t(now-bleConnectedAt)>90000) {
    bleServer->disconnect(bleServer->getConnId());return;
  }
  if(bleOutputOffset<bleOutput.length()) {
    if(uint32_t(now-bleTxAt)>=20 && bleCccd->getNotifications()) {
      bleTxAt=now;
      // Immer <= 20 Bytes: funktioniert bereits mit dem Standard-MTU 23.
      size_t count=min((size_t)20,bleOutput.length()-bleOutputOffset);
      bleTx->setValue((uint8_t*)bleOutput.c_str()+bleOutputOffset,count);bleTx->notify();
      bleOutputOffset+=count;
    }
    return;
  }
  BleRequest req{};
  if(xQueueReceive(bleQueue,&req,0)==pdTRUE && req.epoch==bleSeenEpoch)
    dispatchBluetooth(String(req.text));
}


void setup() {
  setCpuFrequencyMhz(160);
  Serial.begin(115200);
  Serial.println(String(SKETCH_NAME)+" "+SKETCH_VERSION);
  esp_sleep_wakeup_cause_t wake=esp_sleep_get_wakeup_cause();
  bool fromSleep=(wake==ESP_SLEEP_WAKEUP_EXT0 || wake==ESP_SLEEP_WAKEUP_TIMER);
  // Der EXT0-Pin ist nach dem Aufwachen noch RTC-IO: vor digitalRead freigeben.
  rtc_gpio_deinit((gpio_num_t)MAUL_SENSOR_PIN);
  reed.begin();modeInput.begin();volumeInput.begin();
  pinMode(DFPLAYER_BUSY_PIN,INPUT_PULLUP);
  maulOffen=reed.stable;pinchenMode=(modeInput.stable==LOW);
  prefs.begin("crocodoc",false);wifiPrefs.begin("crocowifi",false);loadSettings();
  lastWelcomeTrack=prefs.getUChar("wLast",0);
  if(lastWelcomeTrack>WELCOME_TRACK_COUNT) lastWelcomeTrack=0;
  loadPinchenOrder();
  Wire.begin(I2C_SDA,I2C_SCL);
  matrix.begin(0x70);matrix.setRotation(0);dispHardClear();
  FastLED.addLeds<WS2812B,LED_PIN,GRB>(leds,NUM_LEDS);
  FastLED.setBrightness(255);FastLED.clear();FastLED.show();
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
  if(powerOn) startWebUI(); else WiFi.mode(WIFI_OFF);
  if(wake!=ESP_SLEEP_WAKEUP_TIMER || maulOffen) startBluetooth();
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
  if(powerOn && dfplayerInitOk && !isMuted) {
    int track=pickFromMask_1based(WELCOME_TRACK_COUNT,welcomeMask,welcomeRandom,lastWelcomeTrack);
    requestAudio(AUDIO_WELCOME,FOLDER_SYSTEM,track);
    prefs.putUChar("wLast",track); // Reihenfolge ueber Aus-/Einschalten erhalten.
  }
  if(wake==ESP_SLEEP_WAKEUP_TIMER && !maulOffen) {
    sleepAfterReminder=true;
    if(reminderEnabled) startReminder();
    else enterDeepSleepNow();
  }
}

void loop() {
  uint32_t now=millis();
  bool wasOpen=maulOffen;
  maulOffen=reed.update();
  if(maulOffen!=wasOpen) {
    markActivity();
    if(maulOffen){
      maulOpenSince=now;stopUiTests();stopReminder();restoreDisplay();
    }
  }
  bool selectedMode=(modeInput.update()==LOW);
  if(selectedMode!=pinchenMode) switchMode(selectedMode);
  handleVolumeButton();
  if(maulOffen && !bleStarted) startBluetooth();
  updateBluetooth();

  if(wifiEnabled){server.handleClient();maybeDisableWifiIfIdle();}
  updateAudio();

  if((uiPlaying || uiEffectTesting || uiDispTesting) &&
     uint32_t(now-uiTestStarted)>=UI_TEST_TIMEOUT_MS) {
    stopUiTests();restoreDisplay();
  }
  if(reminderRunning) {
    uint32_t elapsed=millis()-reminderStarted;
    if(maulOffen || elapsed>=reminderEffectDurationMs) {
      stopReminder();
      if(sleepAfterReminder && !maulOffen) enterDeepSleepNow();
    } else {
      // In der ersten Haelfte Glas, in der zweiten Krokodil.
      bool cup=(elapsed<(uint32_t)reminderEffectDurationMs/2);
      if(cup!=reminderShowCup){reminderShowCup=cup;restoreDisplay();}
      effect_render(reminderEffectId);
    }
  }
  if(!reminderRunning) {
    updateGame();
    if(stage==READY && maulOffen && !welcomeActive &&
       uint32_t(millis()-maulOpenSince)>=OPEN_CONFIRM_MS) startGame();
    if(stage==READY && !maulOffen && !welcomeActive) {
      bool testing=uiPlaying || uiEffectTesting || uiDispTesting;
      if(uiEffectTesting) effect_render(uiEffectId);
      if(!testing) {
        bool connected=(wifiEnabled && WiFi.softAPgetStationNum()>0) || bluetoothHasClient();
        if(!connected && (sleepAfterReminder ||
            uint32_t(millis()-lastUserActivity)>=DEEP_SLEEP_AFTER_MS)) {
          enterDeepSleepNow();
        } else if(reminderEnabled && uint32_t(millis()-lastReminderAt)>=reminderIntervalMs) {
          startReminder();
        }
      }
    }
  }
  dispRender();
  delay(1);
}
