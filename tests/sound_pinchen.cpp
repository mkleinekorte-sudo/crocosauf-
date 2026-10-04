#include "../firmware/Crocosauf_Deluxe_v6_5_BLE/Crocosauf_Deluxe_v6_5_BLE.ino"
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<"\n";return 2;}}while(0)
static void tick(uint32_t duration) {
  uint32_t start=millis();
  while(uint32_t(millis()-start)<duration) {
    if(mockPlaying && !mockLoop && int32_t(millis()-mockAudioEnd)>=0){mockPlaying=false;pins[33]=HIGH;}
    loop();mockMillis+=9;
  }
}
static void boot(bool pin=false,bool welcome=false) {
  pins.fill(HIGH);pins[27]=LOW;pins[25]=pin?LOW:HIGH;
  mockReset=welcome?ESP_RST_POWERON:ESP_RST_SW;setup();
}
static std::vector<int> tracks(int folder) {
  std::vector<int> values;
  for(auto c:player.commands) if(c.name=="folder" && c.a==folder) values.push_back(c.b);
  return values;
}
static bool legacyModesAbsent() {
  for(auto c:player.commands) if(c.name=="loop" || c.name=="stopRepeatPlay") return false;
  return true;
}
static void startRound(){pins[27]=HIGH;tick(1800);pins[27]=LOW;tick(80);}
static std::vector<uint8_t> order(){return {pinchenOrder,pinchenOrder+PINCHEN_TOTAL};}
int main(int argc,char**argv) {
  if(argc!=2) return 1;
  String name=argv[1];
  if(name=="welcome_selection") {
    prefs.putUChar("wMask",1U<<4);prefs.putBool("wRnd",false);
    boot(false,true);CHECK(welcomeActive);tick(1500);
    CHECK(tracks(3)==std::vector<int>({5}));CHECK(tracks(2).empty());
    CHECK(player.commands[0].name=="reset");
    CHECK(player.commands[1].name=="source" && player.commands[1].a==2);
    CHECK(player.commands[1].at-player.commands[0].at>=3000);
    int selected=audioTrack;
    bleApproved=true;dispatchBluetooth("1 /volume?v=25");tick(500);
    CHECK(audioTrack==selected);CHECK(welcomeActive);CHECK(tracks(3).size()==1);
    CHECK(legacyModesAbsent());
  } else if(name=="welcome_reboot_sequence") {
    prefs.putUChar("wMask",(1U<<1)|(1U<<4));prefs.putBool("wRnd",false);
    prefs.putUChar("wLast",2);
    boot(false,true);tick(1000);CHECK(tracks(3)==std::vector<int>({5}));
    CHECK(prefs.getUChar("wLast",0)==5);
    // Die Auswahl fuer den naechsten Kaltstart nutzt denselben gespeicherten Cursor.
    int last=prefs.getUChar("wLast",0);
    CHECK(pickFromMask_1based(6,welcomeMask,false,last)==2);
  } else if(name=="gameover_sequence" || name=="gameover_random") {
    boot();bleApproved=true;
    bool rnd=name=="gameover_random";
    dispatchBluetooth("1 /save?group=go&mode="+String(rnd?"rnd":"seq")+"&m0=1&m2=1&m5=1");
    CHECK(appResponseCode==200);CHECK(gameOverMask==37);
    int previous=0;const int sequence[3]={1,3,6};
    for(int i=0;i<18;i++) {
      startRound();CHECK(stage==GAME_OVER);tick(1000);
      auto played=tracks(2);CHECK(played.size()==(size_t)i+1);
      int actual=played.back();CHECK(actual==1 || actual==3 || actual==6);
      if(rnd) CHECK(actual!=previous);else CHECK(actual==sequence[i%3]);
      previous=actual;
      if(i==2 || i==7 || i==10) {
        int cursor=lastGoTrack;
        dispatchBluetooth("2 /volume?v="+String(i%2?10:25));tick(400);
        CHECK(lastGoTrack==cursor);CHECK(tracks(2).size()==played.size());
      }
      tick(3200);CHECK(stage==READY);
    }
    CHECK(legacyModesAbsent());
    // Speichern einer anderen Gruppe veraendert den GO-Cursor nicht.
    int cursor=lastGoTrack;
    dispatchBluetooth("3 /save?group=w&mode=seq&m4=1");CHECK(lastGoTrack==cursor);
    dispatchBluetooth("4 /save?group=go&mode=seq&m1=1&m3=1");CHECK(lastGoTrack==0);
    startRound();tick(1000);CHECK(tracks(2).back()==2);
  } else if(name=="pinchen_pairs_shuffle") {
    boot(true);reminderEnabled=false;
    std::vector<uint8_t> previous;
    for(int set=0;set<12;set++) {
      auto current=order();uint16_t seen=0;
      if(!previous.empty()) {
        for(int i=0;i<10;i++) CHECK(current[i]!=previous[i]);
        CHECK(current[0]!=previous[9]);
      }
      for(int round=0;round<10;round++) {
        startRound();CHECK(stage==GAME_OVER);CHECK(pinnchenIndex==current[round]);
        CHECK(!(seen&(1U<<pinnchenIndex)));seen|=1U<<pinnchenIndex;
        tick(RED_PHASE_DURATION+30);
        int cyan=0;
        for(int i=0;i<NUM_LEDS;i++) if(leds[i]==CRGB::Cyan) cyan++;
        CHECK(cyan==2);
        CHECK(leds[pinnchenLEDs[pinnchenIndex][0]]==CRGB::Cyan);
        CHECK(leds[pinnchenLEDs[pinnchenIndex][1]]==CRGB::Cyan);
        tick(PINCHEN_BLINK_INTERVAL);for(auto led:leds) CHECK(led==CRGB::Black);
        tick(PINCHEN_PHASE_DURATION);
      }
      CHECK(seen==0x03FF);CHECK(stage==NEW_ROUND);
      tick(800);CHECK(tracks(5).size()==(size_t)set+1);
      tick(GRUEN_PHASE_DURATION);CHECK(stage==READY);CHECK(rundeAktuell==1);CHECK(usedPinchenMask==0);
      previous=current;
    }
    // Weitere Durchgaenge pruefen die Mischregeln ohne lange Animationszeiten.
    for(int n=0;n<1000;n++) {
      auto before=order();resetRounds();uint16_t seen=0;
      for(int i=0;i<10;i++){CHECK(pinchenOrder[i]!=before[i]);seen|=1U<<pinchenOrder[i];}
      CHECK(seen==0x03FF);CHECK(pinchenOrder[0]!=before[9]);
    }
    auto before=order();pinchenOrderValid=false;loadPinchenOrder();CHECK(order()==before);
    resetRounds();for(int i=0;i<10;i++) CHECK(pinchenOrder[i]!=before[i]);
  } else if(name=="pinchen_resume") {
    boot(true);auto original=order();
    for(int i=0;i<4;i++){startRound();tick(8100);}
    uint16_t used=usedPinchenMask;CHECK(rundeAktuell==5);
    bool slept=false;try{enterDeepSleepNow();}catch(Slept&){slept=true;}
    CHECK(slept);CHECK(player.commands.back().name=="stop");
    mockWake=ESP_SLEEP_WAKEUP_TIMER;mockReset=ESP_RST_DEEPSLEEP;
    pinchenOrderValid=false;for(auto &p:pinchenOrder)p=0;
    setup();CHECK(order()==original);CHECK(rundeAktuell==5);CHECK(usedPinchenMask==used);
    CHECK(reminderRunning);startRound();CHECK(pinnchenIndex==original[4]);
  } else if(name=="audio_spacing") {
    boot();bleApproved=true;
    dispatchBluetooth("1 /play?src=w&t=3");tick(10);
    dispatchBluetooth("2 /volume?v=30");tick(10);
    dispatchBluetooth("3 /play?src=go&t=4");tick(700);
    dispatchBluetooth("4 /volume?v=10");tick(1000);
    CHECK(tracks(3).empty());CHECK(tracks(2)==std::vector<int>({4}));
    dispatchBluetooth("5 /stop");tick(300);
    for(size_t i=1;i<player.commands.size();i++) {
      CHECK(uint32_t(player.commands[i].at-player.commands[i-1].at)>=DF_COMMAND_GAP_MS);
      if(player.commands[i].name=="stopRepeat") CHECK(player.commands[i-1].name=="folder");
    }
    CHECK(!mockPlaying);CHECK(legacyModesAbsent());
  } else return 3;
  std::cout<<"PASS v6.5 "<<name<<"\n";
}
