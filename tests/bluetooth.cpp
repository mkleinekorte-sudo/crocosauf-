#include "../firmware/Crocosauf_Deluxe_v6_6_BLE_40LED_APPONLY/Crocosauf_Deluxe_v6_6_BLE_40LED_APPONLY.ino"
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<"\n";return 2;}}while(0)
static void tick(uint32_t duration){uint32_t start=millis();while(uint32_t(millis()-start)<duration){loop();mockMillis+=9;}}
static String response(){String r;for(const auto& c:bleTx->notifications)r+=c;bleTx->notifications.clear();return r;}
static String cmd(String request){BLEDevice::server.service.chars[1]->receive(request+"\n");tick(2400);return response();}
int main(int argc,char** argv){
 if(argc!=2)return 1;
 String test=argv[1];
 pins.fill(HIGH);pins[27]=LOW;mockReset=ESP_RST_SW;setup();
 CHECK(bleStarted);CHECK(!bluetoothHasClient());
 BLEDevice::server.callbacks->onConnect(&BLEDevice::server);tick(100);
 CHECK(bluetoothHasClient());CHECK(!bleApproved);
 if(test=="approval") {
   CHECK(cmd("1 /hello").find("\"approved\":false")!=String::npos);
   CHECK(cmd("2 /volume?v=25").find("403")!=String::npos);CHECK(volumeLevel==20);
   pins[26]=LOW;tick(2900);CHECK(!bleApproved);tick(300);CHECK(bleApproved);CHECK(!isMuted);CHECK(volumeLevel==20);
   CHECK(cmd("3 /volume?v=27").find("200")!=String::npos);CHECK(volumeLevel==27);
   CHECK(cmd("4 /volume?v=999").find("400")!=String::npos);CHECK(volumeLevel==27);
   BLEDevice::server.callbacks->onDisconnect(&BLEDevice::server);tick(20);CHECK(!bleApproved);
   BLEDevice::server.callbacks->onConnect(&BLEDevice::server);tick(100);CHECK(!bleApproved);
   pins[26]=LOW;tick(4000);CHECK(!bleApproved); // Release required before new approval.
 } else if(test=="settings") {
   bleApproved=true;
   CHECK(cmd("1 /save?group=g&mode=seq&m2=1&m8=1").find("200")!=String::npos);
   CHECK(gameTrackMask==260);CHECK(!gameTracksRandom);
   CHECK(cmd("2 /save?group=ge&mode=rnd&m19=1").find("200")!=String::npos);
   CHECK(gameEffectMask==(1UL<<19));CHECK(gameEffectsRandom);
   CHECK(cmd("3 /saveText?txt=Gr%C3%BC%C3%9Fe%20%26%20Spa%C3%9F").find("200")!=String::npos);
   CHECK(standbyText=="Gruesse & Spass");
   CHECK(cmd("4 /saveReminderCfg?rmin=12&rdur=5000&ren=1").find("200")!=String::npos);
   CHECK(reminderIntervalMs==720000);CHECK(reminderEffectDurationMs==5000);CHECK(!reminderSoundEnabled);
   String conf=cmd("5 /config");CHECK(conf.find("\"g\":260")!=String::npos);CHECK(conf.find("\"remMin\":12")!=String::npos);
   CHECK(cmd("6 /factoryReset").find("400")!=String::npos);CHECK(reminderIntervalMs==720000);
   CHECK(cmd("7 /factoryReset?confirm=yes").find("200")!=String::npos);CHECK(reminderIntervalMs==480000);
 } else if(test=="framing") {
   bleApproved=true;auto rx=BLEDevice::server.service.chars[1];
   String request="12 /saveText?txt=AA%26BB%2BCC\n";
   for(char c:request){rx->receive(String(1,c));tick(10);}
   tick(2000);CHECK(response().find("12 200 ")==0);CHECK(standbyText=="AA&BB+CC");
   rx->receive(String(600,'x')+"\n");tick(1000);CHECK(response().empty());
   rx->receive("13 /volume?v=22\n");
   BLEDevice::server.callbacks->onDisconnect(&BLEDevice::server);tick(50);
   BLEDevice::server.callbacks->onConnect(&BLEDevice::server);tick(100);CHECK(volumeLevel==20);CHECK(response().empty());
   CHECK(cmd("14 /hello").find("14 200 ")==0);
 } else if(test=="lifecycle") {
   tick(90100);CHECK(!bluetoothHasClient());CHECK(!bleApproved);
   BLEDevice::server.callbacks->onConnect(&BLEDevice::server);tick(30);bleApproved=true;
   bool slept=false;try{tick(1900000);}catch(Slept&){slept=true;}CHECK(!slept);
   pins[27]=LOW;stage=READY;welcomeActive=false;reminderRunning=false;
   cmd("1 /testEffect?id=4");CHECK(uiEffectTesting);
   CHECK(uiEffectTesting);
   pins[27]=HIGH;tick(1000);CHECK(!uiEffectTesting);CHECK(stage==PLAYING);
 } else return 3;
 std::cout<<"PASS BLE "<<test<<"\n";
}
