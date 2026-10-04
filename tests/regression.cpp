#include "../firmware/Crocosauf_Deluxe_v6_6_BLE_40LED_APPONLY/Crocosauf_Deluxe_v6_6_BLE_40LED_APPONLY.ino"
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<"\n";return 2;}}while(0)
static void tick(uint32_t duration) {
  uint32_t start=millis();
  while(uint32_t(millis()-start)<duration){
    if(mockPlaying&&!mockLoop&&int32_t(millis()-mockAudioEnd)>=0){mockPlaying=false;pins[33]=HIGH;}
    loop();mockMillis+=9;
  }
}
static void openMouth(){pins[27]=HIGH;tick(1000);}
static void closeMouth(){pins[27]=LOW;tick(70);}
static size_t loops(){size_t n=0;for(auto c:player.commands)if(c.name=="play")n++;return n;}
static void noWelcomeBoot(bool pin=false) {
  mockReset=ESP_RST_SW;pins.fill(HIGH);pins[27]=LOW;pins[25]=pin?LOW:HIGH;
  setup();
}
int main(int argc,char**argv) {
 if(argc!=2)return 1;
 String name=argv[1];
 if(name=="welcome") {
  pins.fill(HIGH);pins[27]=HIGH;
  setup();CHECK(welcomeActive);
  tick(1500);CHECK(stage==READY);CHECK(loops()==0);
  pins[25]=LOW;tick(80);CHECK(pinchenMode);CHECK(welcomeActive);
  handleStop();CHECK(appResponseCode==409);
  tick(9500);CHECK(welcomeActive);CHECK(loops()==0);
  tick(2200);CHECK(!welcomeActive);CHECK(stage==PLAYING);CHECK(pinchenMode);
  CHECK(loops()==1);
 } else if(name=="welcome_missing") {
  pins.fill(HIGH);pins[27]=HIGH;mockAudioEnabled=false;
  setup();tick(6200);CHECK(!welcomeActive);CHECK(stage==PLAYING);
  CHECK(audioError.length()>0);tick(5500);CHECK(gameAudioFailed);
  size_t count=loops();tick(10000);CHECK(loops()==count);
 } else if(name=="welcome_stuck") {
  pins.fill(HIGH);pins[27]=HIGH;forceBusy=LOW;
  setup();tick(181000);CHECK(!welcomeActive);CHECK(stage==PLAYING);
 } else if(name=="normal") {
  noWelcomeBoot();
  pins[27]=HIGH;tick(400);CHECK(stage==READY);
  pins[27]=LOW;tick(100);CHECK(stage==READY);
  openMouth();CHECK(stage==PLAYING);
  tick(800);CHECK(loops()==1);
  closeMouth();CHECK(stage==GAME_OVER);CHECK(dispMode==DISP_CUP_ANIM);
  tick(4100);CHECK(stage==READY);CHECK(dispMode==DISP_CUP_ANIM);
  tick(1000);CHECK(dispMode==DISP_SCROLL);
  openMouth();CHECK(stage==PLAYING);
 } else if(name=="ten_rounds") {
  noWelcomeBoot(true);
  uint16_t seen=0;
  for(int round=1;round<=10;round++){
   CHECK(rundeAktuell==round);openMouth();CHECK(stage==PLAYING);
   closeMouth();CHECK(stage==GAME_OVER);CHECK(pinnchenIndex>=0);
   CHECK(!(seen&(1U<<pinnchenIndex)));seen|=(1U<<pinnchenIndex);
   tick(4000);CHECK(stage==GAME_OVER);
   tick(4000);
   if(round<10){CHECK(stage==READY);CHECK(rundeAktuell==round+1);}
  }
  CHECK(seen==0x03FF);CHECK(stage==NEW_ROUND);
  tick(5300);CHECK(stage==READY);CHECK(rundeAktuell==1);CHECK(usedPinchenMask==0);
 } else if(name=="switch_phases") {
  noWelcomeBoot(true);
  openMouth();closeMouth();
  pins[25]=HIGH;pins[27]=HIGH;tick(1000);
  CHECK(!pinchenMode);CHECK(stage==PLAYING);
  pins[25]=LOW;tick(80);CHECK(pinchenMode);CHECK(stage==PLAYING);
  rundeAktuell=10;usedPinchenMask=0x01FF;
  closeMouth();tick(8100);CHECK(stage==NEW_ROUND);
  pins[25]=HIGH;tick(80);CHECK(stage==READY);
  openMouth();CHECK(stage==PLAYING);
  CHECK(!pinchenMode);CHECK(rundeAktuell==1);
 } else if(name=="tests_interrupt") {
  noWelcomeBoot();
  appQuery="id=3";handleTestVisual(false);
  CHECK(uiEffectTesting);openMouth();CHECK(!uiEffectTesting);CHECK(stage==PLAYING);
  closeMouth();tick(8100);
  handleTestVisual(true);CHECK(uiDispTesting);
  pins[25]=LOW;tick(100);CHECK(!uiDispTesting);CHECK(pinchenMode);
  appQuery="src=w&t=1";handlePlay();CHECK(uiPlaying);
  openMouth();CHECK(!uiPlaying);CHECK(stage==PLAYING);CHECK(!welcomeActive);
  closeMouth();tick(8100);
  appQuery="id=4";handleTestVisual(true);
  tick(60100);CHECK(!uiDispTesting);
 } else if(name=="reminder_sleep") {
  noWelcomeBoot(true);sleepMinutes=30;rundeAktuell=5;usedPinchenMask=15;
  uint32_t activity=lastUserActivity;
  tick(480100);CHECK(reminderRunning);CHECK(reminderShowCup);
  CHECK(lastUserActivity==activity);tick(1800);CHECK(!reminderShowCup);
  tick(1900);CHECK(!reminderRunning);CHECK(lastUserActivity==activity);
  bool slept=false;try{tick(1400000);}catch(Slept&){slept=true;}
  CHECK(slept);CHECK(uint32_t(millis()-activity)>=1800000);
  CHECK(rtcRound==5);CHECK(rtcUsed==15);CHECK(mockWakeMicros>0);
  CHECK(mockWakeMicros<480000000ULL);
 } else if(name=="timer_wake") {
  pins.fill(HIGH);pins[27]=LOW;pins[25]=LOW;
  mockWake=ESP_SLEEP_WAKEUP_TIMER;mockReset=ESP_RST_DEEPSLEEP;prefs.putUShort("sleepMin",30);
  rtcMagic=0xC60C6301;rtcRound=4;rtcUsed=7;rtcPinchenMode=1;
  prefs.putString("pOrder","0123456789");
  setup();CHECK(rundeAktuell==4);CHECK(usedPinchenMask==7);
  CHECK(reminderRunning);CHECK(sleepAfterReminder);
  bool slept=false;try{tick(4000);}catch(Slept&){slept=true;}CHECK(slept);
 } else if(name=="wake_interrupted") {
  pins.fill(HIGH);pins[27]=LOW;
  mockWake=ESP_SLEEP_WAKEUP_TIMER;mockReset=ESP_RST_DEEPSLEEP;prefs.putUShort("sleepMin",30);
  setup();CHECK(reminderRunning);openMouth();
  CHECK(!reminderRunning);CHECK(!sleepAfterReminder);CHECK(stage==PLAYING);
 } else if(name=="mute_volume") {
  noWelcomeBoot();openMouth();volumeLevel=25;saveVolume();
  pins[26]=LOW;tick(1100);CHECK(isMuted);CHECK(volumeLevel==25);
  pins[26]=HIGH;tick(100);CHECK(volumeLevel==25);
  CHECK(prefs.getInt("vol",0)==25);loadSettings();CHECK(isMuted);CHECK(volumeLevel==25);
  pins[26]=LOW;tick(1100);pins[26]=HIGH;tick(100);CHECK(!isMuted);CHECK(volumeLevel==25);
  pins[26]=LOW;tick(100);pins[26]=HIGH;tick(100);CHECK(volumeLevel==30);
  volumeUp();CHECK(volumeLevel==5);
 } else if(name=="audio_keepalive") {
  noWelcomeBoot();openMouth();tick(1600);CHECK(loops()==1);
  int track=currentGameMusicTrack;
  mockPlaying=false;pins[33]=HIGH;tick(400);CHECK(loops()==1);
  tick(1200);CHECK(loops()==2);CHECK(currentGameMusicTrack==track);
  tick(2500);CHECK(loops()==2);
 } else if(name=="pickers") {
  int last=0;CHECK(pickFromMask_1based(6,63,false,last)==1);
  CHECK(pickFromMask_1based(6,63,false,last)==2);
  last=1;for(int i=0;i<10000;i++){int before=last;int v=pickFromMask_1based(12,3,true,last);CHECK(v!=before);CHECK(v==1||v==2);}
  last=-1;CHECK(pickEffectFromMask_0based(20,0xFFFFF,false,last)==0);
  last=0;for(int i=0;i<10000;i++){int before=last;int v=pickEffectFromMask_0based(20,0x80001,true,last);CHECK(v!=before);}
 } else if(name=="api_export") {
  noWelcomeBoot();
  standbyText="a\"\\\n<&";audioError="x\n\"\\";
  std::ofstream("work_tests/status.json")<<statusJson();
  std::ofstream("work_tests/config.json")<<configJson();
  appQuery="src=g&t=13";handlePlay();CHECK(appResponseCode==400);
  appQuery="rmin=-20&rdur=-1";handleSaveReminderCfg();
  CHECK(reminderIntervalMs==60000);CHECK(reminderEffectDurationMs==1500);
  CHECK(!reminderEnabled);
  handleFactoryReset();CHECK(appResponseCode==200);
  CHECK(reminderIntervalMs==480000);CHECK(reminderEffectDurationMs==3500);
  CHECK(volumeLevel==20);CHECK(!isMuted);CHECK(gameTrackMask==63);
  CHECK(standbyText=="CROCOSAUF DELUXE");
 } else if(name=="animations") {
  noWelcomeBoot();
  for(int i=0;i<20;i++){
   dispSetGameAnimForced(i);
   for(int f=0;f<150;f++){mockMillis+=110;dispRender();}
   resetEffects();
   for(int f=0;f<400;f++){mockMillis+=31;effect_render(i);}
  }
 } else if(name=="wrap") {
  noWelcomeBoot();mockMillis=0xFFFFFF00;markActivity();
  showVolumeOnMatrix();tick(1400);CHECK(dispMode==DISP_SCROLL);
  CHECK(uint32_t(millis()-lastUserActivity)<2000);
  openMouth();CHECK(stage==PLAYING);closeMouth();tick(5200);
  CHECK(stage==READY);CHECK(dispMode==DISP_SCROLL);
 } else return 3;
 std::cout<<"PASS "<<name<<"\n";
}
