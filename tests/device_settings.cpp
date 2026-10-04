#include "../firmware/Crocosauf_Deluxe_v6_6_BLE_40LED_APPONLY/Crocosauf_Deluxe_v6_6_BLE_40LED_APPONLY.ino"
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<"\n";return 2;}}while(0)
static void tick(uint32_t duration,uint32_t step=10){
 uint32_t start=millis();
 while(uint32_t(millis()-start)<duration){
  if(mockPlaying&&!mockLoop&&int32_t(millis()-mockAudioEnd)>=0){mockPlaying=false;pins[33]=HIGH;}
  loop();mockMillis+=step-1;
 }
}
static void boot(bool welcome=false){pins.fill(HIGH);pins[27]=LOW;mockReset=welcome?ESP_RST_POWERON:ESP_RST_SW;setup();}
static int command(const String& r){bleApproved=true;dispatchBluetooth("1 "+r);return appResponseCode;}
int main(int argc,char**argv){
 if(argc!=2)return 1;
 String name=argv[1];
 if(name=="settings_migrate"){
  prefs.putUChar("lBr",37);prefs.putUChar("dBr",60);prefs.putUChar("dRot",2);
  prefs.putBool("aEn",false);prefs.putInt("wVol",11);prefs.putInt("vol",25);prefs.putUInt("remLed",24000);
  boot();CHECK(NUM_LEDS==40);CHECK(ledBrightness==37);CHECK(displayBrightness==60);CHECK(matrix.rotation==2);
  CHECK(FastLED.globalBrightness==94);CHECK(matrix.brightness==9);CHECK(!audioEnabled);CHECK(welcomeVolumeLevel==11);
  CHECK(reminderLedDurationMs==24000);CHECK(startVolumeLevel==25);CHECK(sleepMinutes==0);
  const int expected[10][2]={{2,3},{6,7},{11,12},{14,15},{17,18},{21,22},{24,25},{27,28},{31,32},{36,37}};
  for(int i=0;i<10;i++)for(int j=0;j<2;j++)CHECK(pinnchenLEDs[i][j]==expected[i][j]);
 }else if(name=="visual_and_reset"){
  boot();CHECK(command("/saveDisplayCfg?lbr=42&dbr=73&rot=3")==200);
  CHECK(FastLED.globalBrightness==107);CHECK(matrix.rotation==3);CHECK(matrix.brightness==10);
  CHECK(command("/saveDisplayCfg?lbr=99&dbr=101&rot=0")==400);CHECK(ledBrightness==42);CHECK(displayRotation==3);
  CHECK(command("/saveDisplayCfg?lbr=-1&dbr=50&rot=2")==400);
  CHECK(command("/saveDisplayCfg?lbr=50&dbr=50&rot=2x")==400);
  loadSettings();applyVisualSettings();CHECK(ledBrightness==42);CHECK(displayBrightness==73);CHECK(matrix.rotation==3);
  CHECK(command("/testOrientation")==200);tick(200);CHECK(matrix.lastText=="F");CHECK(uiDispTesting);
  pins[27]=HIGH;tick(1200);CHECK(stage==PLAYING);CHECK(!uiDispTesting);
  pins[27]=LOW;tick(5500);CHECK(command("/factoryReset?confirm=yes")==200);
  CHECK(ledBrightness==100);CHECK(displayBrightness==100);CHECK(matrix.rotation==0);CHECK(sleepMinutes==0);
 }else if(name=="audio_settings"){
  prefs.putInt("vStart",0);prefs.putBool("mut",true);prefs.putInt("wVol",13);boot(true);tick(900);
  CHECK(welcomeActive);CHECK(isMuted);CHECK(audioFolder==3);CHECK(requestedAudioVolume()==13);
  bool welcomeVolumeSent=false;for(auto c:player.commands)if(c.name=="volume"&&c.a==13)welcomeVolumeSent=true;CHECK(welcomeVolumeSent);
  tick(13000);CHECK(!welcomeActive);tick(500);CHECK(player.commands.back().name=="volume");CHECK(player.commands.back().a==0);
  CHECK(command("/saveAudioCfg?aen=0&vstart=15&vwelcome=7")==200);tick(500);CHECK(!audioEnabled);CHECK(!mockPlaying);
  pins[27]=HIGH;tick(2000);CHECK(stage==PLAYING);CHECK(audioRole==AUDIO_NONE);
  CHECK(command("/saveAudioCfg?aen=1&vstart=20&vwelcome=8")==200);tick(900);CHECK(audioRole==AUDIO_GAME);CHECK(mockPlaying);
  CHECK(startVolumeLevel==20);CHECK(welcomeVolumeLevel==8);CHECK(command("/saveAudioCfg?aen=1&vstart=2&vwelcome=30")==400);
 }else if(name=="two_hour_idle"){
  boot();CHECK(sleepMinutes==0);CHECK(loopWatchdogReady);CHECK(mockWdtConfig.trigger_panic);CHECK(mockWdtConfig.timeout_ms==12000);
  int before=matrix.writes;int reminders=0;bool was=false;
  for(int i=0;i<72000;i++){
   tick(100,100);if(reminderRunning&&!was)reminders++;was=reminderRunning;
  }
  CHECK(reminders>=14);CHECK(matrix.writes>before+60000);CHECK(stage==READY);CHECK(mockWdtFeeds>70000);
  pins[27]=HIGH;tick(1900);CHECK(stage==PLAYING);CHECK(audioRole==AUDIO_GAME);
 }else if(name=="display_failure"){
  Wire.present=false;boot();CHECK(!matrixReady);CHECK(Wire.timeout==25);
  pins[27]=HIGH;tick(2000);CHECK(stage==PLAYING);CHECK(audioRole==AUDIO_GAME);
  pins[27]=LOW;tick(5500);CHECK(stage==READY);
  Wire.present=true;tick(5100);CHECK(matrixReady);CHECK(matrix.writes>0);CHECK(matrix.rotation==0);
  Wire.present=false;tick(5100);CHECK(!matrixReady);int writes=matrix.writes;tick(60000,100);CHECK(matrix.writes==writes);
  CHECK(command("/saveDisplayCfg?lbr=25&dbr=30&rot=1")==200);
  Wire.present=true;tick(5100);CHECK(matrixReady);CHECK(matrix.rotation==1);CHECK(matrix.brightness==4);
 }else if(name=="sleep_failure"){
  boot();CHECK(command("/savePower?sleep=30")==200);mockWakeError=1;
  enterDeepSleepNow();CHECK(sleepMinutes==0);CHECK(!systemError.empty());CHECK(BLEDevice::advertising.active);
  pins[27]=HIGH;tick(1800);CHECK(stage==PLAYING);
 }else if(name=="timer_fallback"){
  prefs.putUShort("sleepMin",30);mockMillis=0;boot();
  // Model separate timer boots and retained RTC time without requiring real sleep.
  lastReminderAt=millis();bool slept=false;try{enterDeepSleepNow();}catch(Slept&){slept=true;}CHECK(slept);
  CHECK(mockWakeMicros==60000000ULL);CHECK(rtcReminderRemainingMs==480000);
  int wakes=0;uint32_t elapsed=0;
  while(!reminderRunning && wakes<10){
   elapsed+=rtcSleepSliceMs;mockMillis=0;mockWake=ESP_SLEEP_WAKEUP_TIMER;mockReset=ESP_RST_DEEPSLEEP;
   // globals normally zeroed by reset; only RTC and prefs survive.
   bleStarted=false;audioStopPending=false;audioRole=AUDIO_NONE;audioStep=0;sleepAfterReminder=false;
   try{setup();}catch(Slept&){}elapsed+=millis();wakes++;
  }
  CHECK(reminderRunning);CHECK(wakes>=7&&wakes<=8);CHECK(elapsed>=480000&&elapsed<500000);
  pins[27]=HIGH;tick(1800);CHECK(stage==PLAYING);CHECK(!sleepAfterReminder);CHECK(bleStarted);
 }else if(name=="extended_reminder"){
  boot();CHECK(command("/saveReminderCfg?rmin=8&rdur=9000&rled=30000&ren=1&rsnd=1")==200);
  startReminder();tick(14000);CHECK(reminderRunning);CHECK(reminderShowCup);tick(2000);CHECK(!reminderShowCup);
  tick(14500);CHECK(!reminderRunning);loadSettings();CHECK(reminderLedDurationMs==30000);
 }else return 3;
 std::cout<<"PASS device "<<name<<"\n";
}
