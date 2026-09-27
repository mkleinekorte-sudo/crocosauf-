/* The Butcher v5.5 BLE – ESP32 Arduino Core 3.3.x
 * Bibliotheken: ESP Async WebServer, AsyncTCP, ArduinoJson 6.x
 * K1/K3/K4/K6 Relais LOW-aktiv. E-Stop GPIO32 HIGH = gesperrt.
 * E-Stop muss zusätzlich die Aktorversorgung hardwareseitig abschalten.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <memory>
#include <initializer_list>

constexpr uint8_t relayPin[7]={0,13,12,14,27,26,25};
constexpr uint8_t pwmPin[9]={0,0,4,0,17,17,0,21,22};
constexpr uint8_t START_PIN=33,RESET_PIN=18,ESTOP_PIN=32,STATUS_PIN=2;
constexpr uint8_t MAX_STEPS=96;
enum Action:uint8_t { OFF,ON,PULSE,FLICKER,KNOCK,HANDLE,RBLINK };
enum RunState:uint8_t { IDLE,RUNNING,COOLDOWN,ESTOP };
struct Step {uint32_t t_ms;uint8_t ch,ac;uint32_t dur_ms;};
struct AutoCfg {bool enabled_idle=true,enabled_run=false;uint16_t pause_min=600,pause_max=3500,on_min=50,on_max=600,duty_min=10,duty_max=1023;};
struct Flicker {uint16_t zmin=10,zmax=35,jmin=30,jmax=90,dmin=80,dmax=200;uint8_t pdark=15,pzap=40;};
struct Knock {uint16_t hit_min=40,hit_max=90,gap_min=70,gap_max=220,cgap_min=280,cgap_max=700,dmin=600,dmax=1023;uint8_t cmin=2,cmax=6,paccent=30;};
struct Handle {uint16_t pmin=280,pmax=520,hmin=120,hmax=350,rmin=240,rmax=480,peak=900,jamp=60,jint_min=50,jint_max=140,gapmin=300,gapmax=800;uint8_t repmin=1,repmax=2;};
struct Blink {uint16_t on_ms=300,off_ms=300;};
struct Channel {bool pulse=false,fx=false,output=false,manual=false;uint32_t pulseEnd=0,fxEnd=0,next=0,off=0;uint8_t phase=0,count=0;uint16_t target=0;};
Step steps[MAX_STEPS],staged[MAX_STEPS];uint8_t countSteps=0,stagedCount=0;
uint32_t totalMs=43000,cooldownMs=15000,stagedTotal=43000,stagedCooldown=15000,k1PulseMs=300;
bool k1Auto=true,loopMode=false;RunState state=IDLE;
Flicker flicker;Knock knock;Handle handleCfg;Blink blink3,blink6;AutoCfg autoCfg[9];Channel channels[9];
uint32_t showStart=0,coolEnd=0;uint8_t nextStep=0;
String apSsid="",apPass="",staSsid="",staPass="";bool useAP=true;
Preferences prefs;AsyncWebServer server(80);
uint32_t rng(uint32_t a,uint32_t b){if(a>b)std::swap(a,b);return a==b?a:random(a,b+1);}
bool due(uint32_t now,uint32_t deadline){return (int32_t)(now-deadline)>=0;}
bool emergency(){return digitalRead(ESTOP_PIN)==HIGH;}
bool isLamp(uint8_t ch){return ch==2||ch==7||ch==8;}
bool isRelay(uint8_t ch){return ch==1||ch==3||ch==4||ch==6;}
void writeK(uint8_t ch,uint16_t duty){if(ch<1||ch>8)return;channels[ch].output=duty>0;
  if(isRelay(ch))digitalWrite(relayPin[ch],duty?LOW:HIGH);
  else ledcWrite(pwmPin[ch],duty>1023?1023:duty);
}
void stopFx(uint8_t ch){channels[ch].fx=false;channels[ch].pulse=false;channels[ch].phase=0;channels[ch].manual=false;writeK(ch,0);}
void allOff(){for(uint8_t ch=1;ch<=8;ch++)stopFx(ch);}
void defaults(){totalMs=43000;cooldownMs=15000;k1PulseMs=300;k1Auto=true;loopMode=false;
  flicker=Flicker();knock=Knock();handleCfg=Handle();blink3=Blink();blink6=Blink();
  for(uint8_t ch:{uint8_t(2),uint8_t(7),uint8_t(8)})autoCfg[ch]=AutoCfg();
  countSteps=4;steps[0]={0,2,FLICKER,43000};steps[1]={6000,5,PULSE,3000};steps[2]={12000,5,PULSE,3000};steps[3]={18000,6,PULSE,500};
  stagedCount=countSteps;memcpy(staged,steps,sizeof(Step)*countSteps);stagedTotal=totalMs;stagedCooldown=cooldownMs;
}
void fillAuto(JsonObject o,const AutoCfg &a){o["enabled_idle"]=a.enabled_idle;o["enabled_run"]=a.enabled_run;
  o["pause_min"]=a.pause_min;o["pause_max"]=a.pause_max;o["on_min"]=a.on_min;o["on_max"]=a.on_max;o["duty_min"]=a.duty_min;o["duty_max"]=a.duty_max;}
void readAuto(JsonObject o,AutoCfg &a){a.enabled_idle=o["enabled_idle"]|a.enabled_idle;a.enabled_run=o["enabled_run"]|a.enabled_run;
  a.pause_min=constrain((int)(o["pause_min"]|a.pause_min),0,60000);a.pause_max=constrain((int)(o["pause_max"]|a.pause_max),0,60000);
  a.on_min=constrain((int)(o["on_min"]|a.on_min),0,10000);a.on_max=constrain((int)(o["on_max"]|a.on_max),0,10000);
  a.duty_min=constrain((int)(o["duty_min"]|a.duty_min),0,1023);a.duty_max=constrain((int)(o["duty_max"]|a.duty_max),0,1023);}
void save(){DynamicJsonDocument d(18000);d["total"]=totalMs;d["cool"]=cooldownMs;d["pulse"]=k1PulseMs;d["auto"]=k1Auto;d["loop"]=loopMode;
  JsonArray a=d.createNestedArray("steps");for(uint8_t i=0;i<countSteps;i++){JsonArray s=a.createNestedArray();s.add(steps[i].t_ms);s.add(steps[i].ch);s.add(steps[i].ac);s.add(steps[i].dur_ms);}
  JsonObject f=d.createNestedObject("f");f["zmin"]=flicker.zmin;f["zmax"]=flicker.zmax;f["jmin"]=flicker.jmin;f["jmax"]=flicker.jmax;f["dmin"]=flicker.dmin;f["dmax"]=flicker.dmax;f["pdark"]=flicker.pdark;f["pzap"]=flicker.pzap;
  JsonObject k=d.createNestedObject("k");k["hit_min"]=knock.hit_min;k["hit_max"]=knock.hit_max;k["gap_min"]=knock.gap_min;k["gap_max"]=knock.gap_max;k["cgap_min"]=knock.cgap_min;k["cgap_max"]=knock.cgap_max;k["dmin"]=knock.dmin;k["dmax"]=knock.dmax;k["cmin"]=knock.cmin;k["cmax"]=knock.cmax;k["paccent"]=knock.paccent;
  JsonObject h=d.createNestedObject("h");h["pmin"]=handleCfg.pmin;h["pmax"]=handleCfg.pmax;h["hmin"]=handleCfg.hmin;h["hmax"]=handleCfg.hmax;h["rmin"]=handleCfg.rmin;h["rmax"]=handleCfg.rmax;h["peak"]=handleCfg.peak;h["jamp"]=handleCfg.jamp;h["jint_min"]=handleCfg.jint_min;h["jint_max"]=handleCfg.jint_max;h["repmin"]=handleCfg.repmin;h["repmax"]=handleCfg.repmax;h["gapmin"]=handleCfg.gapmin;h["gapmax"]=handleCfg.gapmax;
  JsonObject b=d.createNestedObject("b");b["3on"]=blink3.on_ms;b["3off"]=blink3.off_ms;b["6on"]=blink6.on_ms;b["6off"]=blink6.off_ms;
  for(uint8_t ch:{uint8_t(2),uint8_t(7),uint8_t(8)}){String key="a"+String(ch);fillAuto(d.createNestedObject(key),autoCfg[ch]);}
  size_t len=measureJson(d);std::unique_ptr<char[]> buf(new char[len+1]);serializeJson(d,buf.get(),len+1);
  prefs.begin("butcher54",false);prefs.putBytes("settings",buf.get(),len);prefs.end();
}
void load(){defaults();prefs.begin("butcher54",true);size_t len=prefs.getBytesLength("settings");
  if(len>0&&len<18000){std::unique_ptr<char[]> buf(new char[len+1]);prefs.getBytes("settings",buf.get(),len);buf[len]=0;
    DynamicJsonDocument d(18000);if(!deserializeJson(d,buf.get(),len)){
      totalMs=d["total"]|totalMs;cooldownMs=d["cool"]|cooldownMs;k1PulseMs=d["pulse"]|k1PulseMs;k1Auto=d["auto"]|k1Auto;loopMode=d["loop"]|loopMode;
      JsonArray a=d["steps"];if(a.size()<=MAX_STEPS){countSteps=0;for(JsonArray s:a){if(s.size()!=4)continue;steps[countSteps++]={(uint32_t)(s[0]|0),(uint8_t)(s[1]|0),(uint8_t)(s[2]|0),(uint32_t)(s[3]|0)};}}
      JsonObject f=d["f"];if(!f.isNull()){flicker.zmin=f["zmin"]|flicker.zmin;flicker.zmax=f["zmax"]|flicker.zmax;flicker.jmin=f["jmin"]|flicker.jmin;flicker.jmax=f["jmax"]|flicker.jmax;flicker.dmin=f["dmin"]|flicker.dmin;flicker.dmax=f["dmax"]|flicker.dmax;flicker.pdark=f["pdark"]|flicker.pdark;flicker.pzap=f["pzap"]|flicker.pzap;}
      JsonObject k=d["k"];if(!k.isNull()){knock.hit_min=k["hit_min"]|knock.hit_min;knock.hit_max=k["hit_max"]|knock.hit_max;knock.gap_min=k["gap_min"]|knock.gap_min;knock.gap_max=k["gap_max"]|knock.gap_max;knock.cgap_min=k["cgap_min"]|knock.cgap_min;knock.cgap_max=k["cgap_max"]|knock.cgap_max;knock.dmin=k["dmin"]|knock.dmin;knock.dmax=k["dmax"]|knock.dmax;knock.cmin=k["cmin"]|knock.cmin;knock.cmax=k["cmax"]|knock.cmax;knock.paccent=k["paccent"]|knock.paccent;}
      JsonObject h=d["h"];if(!h.isNull()){handleCfg.pmin=h["pmin"]|handleCfg.pmin;handleCfg.pmax=h["pmax"]|handleCfg.pmax;handleCfg.hmin=h["hmin"]|handleCfg.hmin;handleCfg.hmax=h["hmax"]|handleCfg.hmax;handleCfg.rmin=h["rmin"]|handleCfg.rmin;handleCfg.rmax=h["rmax"]|handleCfg.rmax;handleCfg.peak=h["peak"]|handleCfg.peak;handleCfg.jamp=h["jamp"]|handleCfg.jamp;handleCfg.jint_min=h["jint_min"]|handleCfg.jint_min;handleCfg.jint_max=h["jint_max"]|handleCfg.jint_max;handleCfg.repmin=h["repmin"]|handleCfg.repmin;handleCfg.repmax=h["repmax"]|handleCfg.repmax;handleCfg.gapmin=h["gapmin"]|handleCfg.gapmin;handleCfg.gapmax=h["gapmax"]|handleCfg.gapmax;}
      JsonObject b=d["b"];blink3.on_ms=b["3on"]|blink3.on_ms;blink3.off_ms=b["3off"]|blink3.off_ms;blink6.on_ms=b["6on"]|blink6.on_ms;blink6.off_ms=b["6off"]|blink6.off_ms;
      for(uint8_t ch:{uint8_t(2),uint8_t(7),uint8_t(8)}){String key="a"+String(ch);JsonObject o=d[key];if(!o.isNull())readAuto(o,autoCfg[ch]);}
    }}prefs.end();
  totalMs=constrain(totalMs,1000UL,600000UL);cooldownMs=constrain(cooldownMs,0UL,600000UL);k1PulseMs=constrain(k1PulseMs,50UL,10000UL);
  stagedCount=countSteps;memcpy(staged,steps,sizeof(Step)*countSteps);stagedTotal=totalMs;stagedCooldown=cooldownMs;
}
void beginShow(){if(state!=IDLE||emergency())return;allOff();showStart=millis();nextStep=0;state=RUNNING;
  if(k1Auto){writeK(1,1023);channels[1].pulse=true;channels[1].pulseEnd=showStart+k1PulseMs;}}
void execute(const Step &s){uint8_t ch=s.ch;stopFx(ch);Channel &c=channels[ch];uint32_t now=millis();
  if(s.ac==OFF)return;if(s.ac==ON){writeK(ch,1023);c.manual=true;return;}
  if(s.ac==PULSE){writeK(ch,1023);c.pulse=true;c.pulseEnd=now+s.dur_ms;return;}
  c.fx=true;c.fxEnd=now+s.dur_ms;c.next=now;c.off=0;c.phase=0;c.count=0;c.target=0;
}
uint8_t activeAction[9]={};
void startEffect(const Step &s){execute(s);activeAction[s.ch]=s.ac;}
void runEffect(uint8_t ch,uint32_t now){Channel &c=channels[ch];if(c.pulse&&due(now,c.pulseEnd)){c.pulse=false;writeK(ch,0);}
  if(!c.fx)return;if(due(now,c.fxEnd)){stopFx(ch);return;}if(!due(now,c.next))return;
  switch(activeAction[ch]){
    case FLICKER:{uint8_t r=random(100);uint16_t duty;uint32_t wait;
      if(r<flicker.pdark){duty=0;wait=rng(flicker.dmin,flicker.dmax);}
      else if(r<((int)flicker.pdark+(int)flicker.pzap>100?100:(int)flicker.pdark+(int)flicker.pzap)){duty=rng(750,1023);wait=rng(flicker.zmin,flicker.zmax);}
      else{duty=rng(250,800);wait=rng(flicker.jmin,flicker.jmax);}writeK(ch,duty);c.next=now+(wait?wait:1);break;}
    case RBLINK:{Blink &b=(ch==3?blink3:blink6);bool on=!c.output;writeK(ch,on?1023:0);c.next=now+((uint32_t)(on?b.on_ms:b.off_ms)+1);break;}
    case KNOCK:{if(c.phase==0){c.count=rng(knock.cmin,knock.cmax);c.phase=1;c.next=now;}
      else if(c.phase==1){writeK(ch,1023);c.phase=2;c.next=now+((uint32_t)rng(knock.hit_min,knock.hit_max)+1);}
      else{writeK(ch,0);if(--c.count==0)c.phase=0;else c.phase=1;c.next=now+((uint32_t)rng(c.phase==0?knock.cgap_min:knock.gap_min,c.phase==0?knock.cgap_max:knock.gap_max)+1);}break;}
    case HANDLE:{ // PWM ramp, hold, release, then optional repetition.
      if(c.phase==0){c.count=rng(handleCfg.repmin,handleCfg.repmax);c.phase=1;c.off=now;c.next=now;c.target=rng(handleCfg.pmin,handleCfg.pmax);}
      uint32_t elapsed=now-c.off,span=1;uint16_t duty=0;
      if(c.phase==1){span=c.target?c.target:1;duty=(uint32_t)handleCfg.peak*(elapsed<span?elapsed:span)/span;}
      if(c.phase==2){span=c.target?c.target:1;duty=constrain((int)handleCfg.peak+(int)rng(0,handleCfg.jamp)-(int)handleCfg.jamp/2,0,1023);}
      if(c.phase==3){span=c.target?c.target:1;duty=(uint32_t)handleCfg.peak*(span-(elapsed<span?elapsed:span))/span;}
      if(c.phase==4){span=c.target?c.target:1;duty=0;}
      writeK(ch,duty);if(elapsed>=span){c.off=now;c.phase++;if(c.phase==4&&--c.count==0){stopFx(ch);break;}if(c.phase>4)c.phase=1;
        if(c.phase==1)c.target=rng(handleCfg.pmin,handleCfg.pmax);if(c.phase==2)c.target=rng(handleCfg.hmin,handleCfg.hmax);
        if(c.phase==3)c.target=rng(handleCfg.rmin,handleCfg.rmax);if(c.phase==4)c.target=rng(handleCfg.gapmin,handleCfg.gapmax);}
      c.next=now+((uint32_t)handleCfg.jint_min+10);break;}
  }
}
void autoUpdate(uint8_t ch,uint32_t now){AutoCfg &a=autoCfg[ch];Channel &c=channels[ch];bool enabled=(state==IDLE&&a.enabled_idle)||(state==RUNNING&&a.enabled_run);
  if(!enabled||c.fx||c.pulse||c.manual||emergency()){if(c.phase==10)writeK(ch,0);if(c.phase>=10)c.phase=0;return;}
  if(c.phase<10){c.phase=10;c.next=now+rng(a.pause_min,a.pause_max);}
  if(c.phase==10&&due(now,c.next)){writeK(ch,rng(a.duty_min,a.duty_max));c.phase=11;c.off=now+(rng(a.on_min,a.on_max)+1);}
  else if(c.phase==11&&due(now,c.off)){writeK(ch,0);c.phase=10;c.next=now+rng(a.pause_min,a.pause_max);}
}
String validate(){if(state!=IDLE)return "Nur im IDLE";if(stagedTotal<1000||stagedTotal>600000||stagedCooldown>600000||k1PulseMs<50||k1PulseMs>10000)return "Zeitgrenze";
  std::stable_sort(staged,staged+stagedCount,[](const Step&a,const Step&b){return a.t_ms<b.t_ms;});uint32_t busy[9]={};
  for(uint8_t i=0;i<stagedCount;i++){Step&s=staged[i];if(s.ch<1||s.ch>8||s.ac>6||s.t_ms>=stagedTotal)return "Ungueltiger Schritt";
    if((s.ac==FLICKER&&!isLamp(s.ch))||(s.ac==KNOCK&&s.ch!=4)||(s.ac==HANDLE&&s.ch!=5)||(s.ac==RBLINK&&s.ch!=3&&s.ch!=6))return "Kanal/Effekt ungueltig";
    if(s.t_ms<busy[s.ch])return "Ueberlappung K"+String(s.ch);
    if(s.ac>=PULSE){if(!s.dur_ms||s.dur_ms>stagedTotal-s.t_ms)return "Dauer ausserhalb der Show";busy[s.ch]=s.t_ms+s.dur_ms;}
  }return "";
}
String statusJson(){DynamicJsonDocument d(18000);uint32_t now=millis();d["state"]=state==IDLE?"IDLE":state==RUNNING?"RUNNING":state==COOLDOWN?"COOLDOWN":"ESTOP";d["estop"]=emergency();d["ip"]=(useAP?WiFi.softAPIP():WiFi.localIP()).toString();
  d["total_time_ms"]=totalMs;d["cooldown_ms"]=cooldownMs;d["k1_autopulse"]=k1Auto;d["k1_pulse_ms"]=k1PulseMs;d["loop"]=loopMode;
  d["t_since_ms"]=state==RUNNING?now-showStart:0;d["t_left_ms"]=state==RUNNING&&!due(now,showStart+totalMs)?showStart+totalMs-now:0;
  d["t_next_ms"]=state==RUNNING&&nextStep<countSteps&&!due(now,showStart+steps[nextStep].t_ms)?showStart+steps[nextStep].t_ms-now:0;
  d["t_cool_ms"]=state==COOLDOWN&&!due(now,coolEnd)?coolEnd-now:0;
  JsonArray arr=d.createNestedArray("steps");for(uint8_t i=0;i<countSteps;i++){JsonObject x=arr.createNestedObject();x["t_ms"]=steps[i].t_ms;x["channel"]=steps[i].ch;x["action"]=steps[i].ac;x["dur_ms"]=steps[i].dur_ms;}
  JsonObject f=d.createNestedObject("flicker");f["zmin"]=flicker.zmin;f["zmax"]=flicker.zmax;f["jmin"]=flicker.jmin;f["jmax"]=flicker.jmax;f["dmin"]=flicker.dmin;f["dmax"]=flicker.dmax;f["pdark"]=flicker.pdark;f["pzap"]=flicker.pzap;
  JsonObject k=d.createNestedObject("k4");k["hit_min"]=knock.hit_min;k["hit_max"]=knock.hit_max;k["gap_min"]=knock.gap_min;k["gap_max"]=knock.gap_max;k["cgap_min"]=knock.cgap_min;k["cgap_max"]=knock.cgap_max;k["dmin"]=knock.dmin;k["dmax"]=knock.dmax;k["cmin"]=knock.cmin;k["cmax"]=knock.cmax;k["paccent"]=knock.paccent;
  JsonObject h=d.createNestedObject("h5");h["pmin"]=handleCfg.pmin;h["pmax"]=handleCfg.pmax;h["hmin"]=handleCfg.hmin;h["hmax"]=handleCfg.hmax;h["rmin"]=handleCfg.rmin;h["rmax"]=handleCfg.rmax;h["peak"]=handleCfg.peak;h["jamp"]=handleCfg.jamp;h["jint_min"]=handleCfg.jint_min;h["jint_max"]=handleCfg.jint_max;h["repmin"]=handleCfg.repmin;h["repmax"]=handleCfg.repmax;h["gapmin"]=handleCfg.gapmin;h["gapmax"]=handleCfg.gapmax;
  JsonObject b3=d.createNestedObject("rblink3");b3["on_ms"]=blink3.on_ms;b3["off_ms"]=blink3.off_ms;JsonObject b6=d.createNestedObject("rblink6");b6["on_ms"]=blink6.on_ms;b6["off_ms"]=blink6.off_ms;
  for(uint8_t ch:{uint8_t(2),uint8_t(7),uint8_t(8)}){String key="auto"+String(ch);fillAuto(d.createNestedObject(key),autoCfg[ch]);}
  String out;serializeJson(d,out);return out;
}
void status(AsyncWebServerRequest*r){r->send(200,"application/json",statusJson());}
void jsonRoute(const char*path,void(*fn)(AsyncWebServerRequest*,JsonVariant)){server.on(path,HTTP_POST,[](AsyncWebServerRequest*){},nullptr,
  [fn](AsyncWebServerRequest*r,uint8_t*data,size_t len,size_t index,size_t total){
    if(index==0){if(total>20000){r->send(413,"text/plain","Zu gross");return;}r->_tempObject=new String();}
    String *body=(String*)r->_tempObject;if(!body)return;body->concat((const char*)data,len);
    if(index+len==total){DynamicJsonDocument d(20000);DeserializationError err=deserializeJson(d,*body);delete body;r->_tempObject=nullptr;
      if(err){r->send(400,"text/plain","JSON Fehler");return;}fn(r,d.as<JsonVariant>());}
  });}

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <mbedtls/base64.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>

static const char *BUTCHER_SERVICE="a9460001-36c5-487c-9113-b31c3894f4b1";
static const char *BUTCHER_RX="a9460002-36c5-487c-9113-b31c3894f4b1";
static const char *BUTCHER_TX="a9460003-36c5-487c-9113-b31c3894f4b1";
static BLEServer *butcherServer=nullptr;
static BLECharacteristic *butcherTx=nullptr;
static BLE2902 *butcherCccd=nullptr;
static QueueHandle_t butcherQueue=nullptr;
static std::atomic<bool> butcherConnected{false};
static std::atomic<uint32_t> butcherEpoch{0};
static uint32_t butcherSeenEpoch=0,butcherTxAt=0;
static bool butcherApproved=false;
static String butcherOutput;
static size_t butcherOutputAt=0;
struct ButcherPacket { uint32_t epoch; char *line; };

static String butcherEncode(const String& input){size_t n=4*((input.length()+2)/3)+4;
  std::unique_ptr<unsigned char[]> out(new unsigned char[n]);size_t got=0;
  if(mbedtls_base64_encode(out.get(),n,&got,(const unsigned char*)input.c_str(),input.length())!=0)return "";
  return String((const char*)out.get()).substring(0,got);
}
static bool butcherDecode(const String& input,String& output){if(input.isEmpty()){output="";return true;}
  std::unique_ptr<unsigned char[]> out(new unsigned char[(input.length()*3)/4+4]);size_t got=0;
  if(mbedtls_base64_decode(out.get(),(input.length()*3)/4+4,&got,(const unsigned char*)input.c_str(),input.length())!=0)return false;
  output=String((const char*)out.get(),got);return true;
}
class ButcherServerCallbacks:public BLEServerCallbacks {
  void onConnect(BLEServer*) override{butcherConnected.store(true);butcherEpoch.fetch_add(1);}
  void onDisconnect(BLEServer*) override{butcherConnected.store(false);butcherEpoch.fetch_add(1);}
};
class ButcherRxCallbacks:public BLECharacteristicCallbacks {
  char buffer[20000]={};size_t used=0;bool overflow=false;uint32_t epoch=0;
  void onWrite(BLECharacteristic *c) override{
    uint32_t current=butcherEpoch.load();if(epoch!=current){epoch=current;used=0;overflow=false;}
    auto bytes=c->getValue();for(size_t i=0;i<bytes.length();i++){
      char x=bytes[i];if(x=='\n'){
        if(!overflow&&used&&butcherConnected.load()){
          char *copy=(char*)malloc(used+1);if(copy){memcpy(copy,buffer,used);copy[used]=0;
            ButcherPacket p{current,copy};if(xQueueSend(butcherQueue,&p,0)!=pdTRUE)free(copy);}}
        used=0;overflow=false;
      }else if(x!='\r'){
        if(x=='\0'||used>=sizeof(buffer)-1)overflow=true;else if(!overflow)buffer[used++]=x;
      }
    }
  }
};
static void butcherBleStart(){
  butcherQueue=xQueueCreate(2,sizeof(ButcherPacket));if(!butcherQueue)return;
  BLEDevice::init("The Butcher");butcherServer=BLEDevice::createServer();butcherServer->setCallbacks(new ButcherServerCallbacks());
  BLEService *svc=butcherServer->createService(BUTCHER_SERVICE);
  butcherTx=svc->createCharacteristic(BUTCHER_TX,BLECharacteristic::PROPERTY_NOTIFY);
  butcherCccd=new BLE2902();butcherTx->addDescriptor(butcherCccd);
  BLECharacteristic *rx=svc->createCharacteristic(BUTCHER_RX,BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new ButcherRxCallbacks());svc->start();
  BLEAdvertising *adv=BLEDevice::getAdvertising();adv->addServiceUUID(BUTCHER_SERVICE);adv->setScanResponse(true);adv->start();
}
static void butcherBleApprove(){if(butcherConnected.load()&&!butcherApproved){butcherApproved=true;Serial.println("The Butcher: BLE APP OK");}}
static int butcherDispatch(const String& path,const String& payload,String& result){
  if(path=="/hello"){result="{\"product\":\"butcher\",\"protocol\":1,\"approved\":"+String(butcherApproved?"true":"false")+"}";return 200;}
  if(!butcherApproved){result="Reset-Taster am Geraet 3 Sekunden halten";return 403;}
  if(path=="/api/status"){result=statusJson();return 200;}
  if(path=="/api/alloff"){allOff();state=emergency()?ESTOP:IDLE;result="OK";return 200;}
  if(path=="/api/start"){if(emergency()||state!=IDLE){result="Busy / E-Stop";return 409;}beginShow();result="OK";return 200;}
  if(path=="/api/loop"){loopMode=!loopMode;save();result="OK";return 200;}
  if(path.startsWith("/api/test?")){
    if(state!=IDLE||emergency()){result="Gesperrt";return 409;}
    int at=path.indexOf("ch="),opAt=path.indexOf("op=");if(at<0||opAt<0){result="Argument fehlt";return 400;}
    int ch=path.substring(at+3).toInt();String op=path.substring(opAt+3);int amp=op.indexOf('&');if(amp>=0)op=op.substring(0,amp);
    if(ch<1||ch>8||(op!="on"&&op!="off"&&op!="pulse")){result="Ungueltig";return 400;}
    stopFx(ch);if(op!="off"){writeK(ch,1023);channels[ch].manual=(op=="on");if(op=="pulse"){channels[ch].pulse=true;channels[ch].pulseEnd=millis()+500;}}
    result="OK";return 200;
  }
  if(path=="/api/apply"){
    String error=validate();if(error.length()){result=error;return 400;}
    countSteps=stagedCount;memcpy(steps,staged,sizeof(Step)*countSteps);totalMs=stagedTotal;cooldownMs=stagedCooldown;save();result="OK";return 200;
  }
  if(path=="/api/defaults"){
    if(state!=IDLE){result="Nur IDLE";return 409;}
    allOff();defaults();save();useAP=true;apSsid="";apPass="";staSsid="";staPass="";
    prefs.begin("butcherwifi",false);prefs.clear();prefs.end();result="OK – WLAN nach Neustart im AP-Modus";return 200;
  }
  DynamicJsonDocument d(20000);if(deserializeJson(d,payload)){result="JSON Fehler";return 400;}
  JsonVariant v=d.as<JsonVariant>();
  if(path=="/api/wifi"){
    String mode=v["mode"]|"AP",ssid=v["ssid"]|"",a=v["pw1"]|"",b=v["pw2"]|"";
    if(a!=b||ssid.length()>32||(a.length()&&a.length()<8)){result="SSID/Passwort ungueltig";return 400;}
    if(mode=="STA"){if(ssid.isEmpty()){result="SSID fehlt";return 400;}staSsid=ssid;staPass=a;useAP=false;}
    else if(mode=="AP"){if(ssid.length())apSsid=ssid;if(a.length())apPass=a;useAP=true;}
    else{result="Modus ungueltig";return 400;}
    prefs.begin("butcherwifi",false);prefs.putBool("ap",useAP);prefs.putString("aps",apSsid);prefs.putString("app",apPass);prefs.putString("ss",staSsid);prefs.putString("sp",staPass);prefs.end();result="OK – neu starten";return 200;
  }
  if(state!=IDLE){result="Nur IDLE";return 409;}
  if(path=="/api/staged/steps"){
    if(!v.is<JsonArray>()){result="Array erwartet";return 400;}JsonArray a=v.as<JsonArray>();if(a.size()>MAX_STEPS){result="Maximal 96";return 400;}
    Step temp[MAX_STEPS];uint8_t n=0;for(JsonObject x:a){temp[n++]={(uint32_t)(x["t_ms"]|0),(uint8_t)(x["channel"]|0),(uint8_t)(x["action"]|0),(uint32_t)(x["dur_ms"]|0)};}
    memcpy(staged,temp,n*sizeof(Step));stagedCount=n;result="OK";return 200;
  }
  if(path=="/api/staged/config"){
    stagedTotal=v["total_time_ms"]|stagedTotal;stagedCooldown=v["cooldown_ms"]|stagedCooldown;
    k1Auto=v["k1_autopulse"]|k1Auto;k1PulseMs=v["k1_pulse_ms"]|k1PulseMs;result="OK";return 200;
  }
  if(path=="/api/staged/flicker"){
    flicker.zmin=constrain((int)(v["zmin"]|flicker.zmin),0,5000);flicker.zmax=constrain((int)(v["zmax"]|flicker.zmax),0,5000);
    flicker.jmin=constrain((int)(v["jmin"]|flicker.jmin),0,5000);flicker.jmax=constrain((int)(v["jmax"]|flicker.jmax),0,5000);
    flicker.dmin=constrain((int)(v["dmin"]|flicker.dmin),0,5000);flicker.dmax=constrain((int)(v["dmax"]|flicker.dmax),0,5000);
    flicker.pdark=constrain((int)(v["pdark"]|flicker.pdark),0,100);flicker.pzap=constrain((int)(v["pzap"]|flicker.pzap),0,100);save();result="OK";return 200;
  }
  if(path=="/api/staged/autofx"){
    for(uint8_t ch:{uint8_t(2),uint8_t(7),uint8_t(8)}){String key="a"+String(ch);JsonObject o=v[key];if(!o.isNull())readAuto(o,autoCfg[ch]);}
    save();result="OK";return 200;
  }
  if(path=="/api/staged/rblink"){
    for(uint8_t ch:{uint8_t(3),uint8_t(6)}){String key="rb"+String(ch);JsonObject o=v[key];if(!o.isNull()){
      Blink &b=ch==3?blink3:blink6;b.on_ms=constrain((int)(o["on_ms"]|b.on_ms),0,10000);b.off_ms=constrain((int)(o["off_ms"]|b.off_ms),0,10000);}}
    save();result="OK";return 200;
  }
  if(path=="/api/staged/motors"){
    JsonObject k=v["k4"],h=v["h5"];
    if(!k.isNull()){
      knock.hit_min=constrain((int)(k["hit_ms_min"]|knock.hit_min),0,5000);knock.hit_max=constrain((int)(k["hit_ms_max"]|knock.hit_max),0,5000);
      knock.gap_min=constrain((int)(k["gap_ms_min"]|knock.gap_min),0,10000);knock.gap_max=constrain((int)(k["gap_ms_max"]|knock.gap_max),0,10000);
      knock.cgap_min=constrain((int)(k["cluster_gap_min"]|knock.cgap_min),0,15000);knock.cgap_max=constrain((int)(k["cluster_gap_max"]|knock.cgap_max),0,15000);
      knock.cmin=constrain((int)(k["cluster_min"]|knock.cmin),1,20);knock.cmax=constrain((int)(k["cluster_max"]|knock.cmax),1,20);
      knock.dmin=constrain((int)(k["duty_min"]|knock.dmin),0,1023);knock.dmax=constrain((int)(k["duty_max"]|knock.dmax),0,1023);
      knock.paccent=constrain((int)(k["prob_accent"]|knock.paccent),0,100);
    }
    if(!h.isNull()){
      handleCfg.pmin=constrain((int)(h["pmin"]|handleCfg.pmin),0,10000);handleCfg.pmax=constrain((int)(h["pmax"]|handleCfg.pmax),0,10000);
      handleCfg.hmin=constrain((int)(h["hmin"]|handleCfg.hmin),0,10000);handleCfg.hmax=constrain((int)(h["hmax"]|handleCfg.hmax),0,10000);
      handleCfg.rmin=constrain((int)(h["rmin"]|handleCfg.rmin),0,10000);handleCfg.rmax=constrain((int)(h["rmax"]|handleCfg.rmax),0,10000);
      handleCfg.peak=constrain((int)(h["peak"]|handleCfg.peak),0,1023);handleCfg.jamp=constrain((int)(h["jamp"]|handleCfg.jamp),0,1023);
      handleCfg.jint_min=constrain((int)(h["jint_min"]|handleCfg.jint_min),0,5000);handleCfg.jint_max=constrain((int)(h["jint_max"]|handleCfg.jint_max),0,5000);
      handleCfg.repmin=constrain((int)(h["repmin"]|handleCfg.repmin),1,10);handleCfg.repmax=constrain((int)(h["repmax"]|handleCfg.repmax),1,10);
      handleCfg.gapmin=constrain((int)(h["gapmin"]|handleCfg.gapmin),0,10000);handleCfg.gapmax=constrain((int)(h["gapmax"]|handleCfg.gapmax),0,10000);
    }
    save();result="OK";return 200;
  }
  result="Unbekannter Befehl";return 404;
}
static void butcherBleProcess(const char *line){String raw(line);int first=raw.indexOf(' '),second=first<0?-1:raw.indexOf(' ',first+1);
  if(first<1||second<0)return;String id=raw.substring(0,first),path=raw.substring(first+1,second),payload;
  if(id.length()>8||path.length()>100||!path.startsWith("/")||!butcherDecode(raw.substring(second+1),payload))return;
  for(size_t i=0;i<id.length();i++)if(!isDigit(id[i]))return;
  String result;int code=butcherDispatch(path,payload,result);
  butcherOutput=id+" "+String(code)+" "+butcherEncode(result)+"\n";butcherOutputAt=0;
}
static void butcherBleTick(){
  if(!butcherQueue)return;uint32_t now=millis(),epoch=butcherEpoch.load();
  if(epoch!=butcherSeenEpoch){butcherSeenEpoch=epoch;butcherApproved=false;butcherOutput="";butcherOutputAt=0;
    if(!butcherConnected.load())BLEDevice::startAdvertising();ButcherPacket old{};while(xQueueReceive(butcherQueue,&old,0)==pdTRUE)free(old.line);}
  if(!butcherConnected.load())return;
  if(butcherOutputAt<butcherOutput.length()){
    if(due(now,butcherTxAt+15)&&butcherCccd->getNotifications()){
      butcherTxAt=now;size_t n=std::min((size_t)20,butcherOutput.length()-butcherOutputAt);
      butcherTx->setValue((uint8_t*)butcherOutput.c_str()+butcherOutputAt,n);butcherTx->notify();butcherOutputAt+=n;}
    return;
  }
  ButcherPacket p{};if(xQueueReceive(butcherQueue,&p,0)==pdTRUE){if(p.epoch==butcherSeenEpoch)butcherBleProcess(p.line);free(p.line);}
}


void setup(){Serial.begin(115200);for(uint8_t ch=1;ch<=6;ch++){digitalWrite(relayPin[ch],HIGH);pinMode(relayPin[ch],OUTPUT);}pinMode(START_PIN,INPUT_PULLUP);pinMode(RESET_PIN,INPUT_PULLUP);pinMode(ESTOP_PIN,INPUT_PULLUP);
  for(uint8_t ch:{uint8_t(2),uint8_t(5),uint8_t(7),uint8_t(8)})ledcAttach(pwmPin[ch],1000,10);ledcAttach(STATUS_PIN,5000,10);allOff();randomSeed(esp_random());load();
  prefs.begin("butcherwifi",true);useAP=prefs.getBool("ap",true);apSsid=prefs.getString("aps","");apPass=prefs.getString("app","");staSsid=prefs.getString("ss","");staPass=prefs.getString("sp","");prefs.end();
  if(apPass.length()<8){apSsid="Butcher-"+String((uint32_t)ESP.getEfuseMac(),HEX);const char abc[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";apPass="";for(int i=0;i<16;i++)apPass+=abc[esp_random()%(sizeof(abc)-1)];
    prefs.begin("butcherwifi",false);prefs.putString("aps",apSsid);prefs.putString("app",apPass);prefs.end();}
  Serial.printf("AP SSID: %s\nAP Passwort: %s\n",apSsid.c_str(),apPass.c_str());
  if(!useAP){WiFi.mode(WIFI_STA);WiFi.begin(staSsid.c_str(),staPass.c_str());if(WiFi.waitForConnectResult(10000)!=WL_CONNECTED)useAP=true;}
  if(useAP){WiFi.mode(WIFI_AP);WiFi.softAP(apSsid.c_str(),apPass.c_str());}MDNS.begin("butcher");
  server.on("/api/status",HTTP_GET,[](AsyncWebServerRequest*r){status(r);});
  server.on("/api/start",HTTP_POST,[](AsyncWebServerRequest*r){if(emergency()||state!=IDLE){r->send(409,"text/plain","Busy / E-Stop");return;}beginShow();r->send(200,"text/plain","OK");});
  server.on("/api/alloff",HTTP_POST,[](AsyncWebServerRequest*r){allOff();state=emergency()?ESTOP:IDLE;r->send(200,"text/plain","OK");});
  server.on("/api/loop",HTTP_POST,[](AsyncWebServerRequest*r){loopMode=!loopMode;save();r->send(200,"text/plain","OK");});
  server.on("/api/test",HTTP_GET,[](AsyncWebServerRequest*r){if(state!=IDLE||emergency()){r->send(409,"text/plain","Gesperrt");return;}if(!r->hasParam("ch")||!r->hasParam("op")){r->send(400,"text/plain","Argument fehlt");return;}
    int ch=r->getParam("ch")->value().toInt();String op=r->getParam("op")->value();if(ch<1||ch>8||(op!="on"&&op!="off"&&op!="pulse")){r->send(400,"text/plain","Ungueltig");return;}
    stopFx(ch);if(op!="off"){writeK(ch,1023);channels[ch].manual=(op=="on");if(op=="pulse"){channels[ch].pulse=true;channels[ch].pulseEnd=millis()+500;}}r->send(200,"text/plain","OK");});
  server.on("/api/apply",HTTP_POST,[](AsyncWebServerRequest*r){String error=validate();if(error.length()){r->send(400,"text/plain",error);return;}countSteps=stagedCount;memcpy(steps,staged,sizeof(Step)*countSteps);totalMs=stagedTotal;cooldownMs=stagedCooldown;save();r->send(200,"text/plain","OK");});
  server.on("/api/defaults",HTTP_POST,[](AsyncWebServerRequest*r){if(state!=IDLE){r->send(409,"text/plain","Nur IDLE");return;}allOff();defaults();save();useAP=true;apSsid="";apPass="";staSsid="";staPass="";
    prefs.begin("butcherwifi",false);prefs.clear();prefs.end();r->send(200,"text/plain","OK – WLAN nach Neustart im AP-Modus");});
  jsonRoute("/api/staged/steps",[](AsyncWebServerRequest*r,JsonVariant v){if(state!=IDLE||!v.is<JsonArray>()){r->send(409,"text/plain","Nur IDLE / Array");return;}JsonArray a=v.as<JsonArray>();if(a.size()>MAX_STEPS){r->send(400,"text/plain","Maximal 96");return;}
    Step temp[MAX_STEPS];uint8_t n=0;for(JsonObject x:a){temp[n++]={(uint32_t)(x["t_ms"]|0),(uint8_t)(x["channel"]|0),(uint8_t)(x["action"]|0),(uint32_t)(x["dur_ms"]|0)};}memcpy(staged,temp,n*sizeof(Step));stagedCount=n;r->send(200,"text/plain","OK");});
  jsonRoute("/api/staged/config",[](AsyncWebServerRequest*r,JsonVariant v){if(state!=IDLE){r->send(409,"text/plain","Nur IDLE");return;}stagedTotal=v["total_time_ms"]|stagedTotal;stagedCooldown=v["cooldown_ms"]|stagedCooldown;k1Auto=v["k1_autopulse"]|k1Auto;k1PulseMs=v["k1_pulse_ms"]|k1PulseMs;r->send(200,"text/plain","OK");});
  jsonRoute("/api/staged/flicker",[](AsyncWebServerRequest*r,JsonVariant v){if(state!=IDLE){r->send(409,"text/plain","Nur IDLE");return;}
    flicker.zmin=constrain((int)(v["zmin"]|flicker.zmin),0,5000);flicker.zmax=constrain((int)(v["zmax"]|flicker.zmax),0,5000);flicker.jmin=constrain((int)(v["jmin"]|flicker.jmin),0,5000);flicker.jmax=constrain((int)(v["jmax"]|flicker.jmax),0,5000);flicker.dmin=constrain((int)(v["dmin"]|flicker.dmin),0,5000);flicker.dmax=constrain((int)(v["dmax"]|flicker.dmax),0,5000);flicker.pdark=constrain((int)(v["pdark"]|flicker.pdark),0,100);flicker.pzap=constrain((int)(v["pzap"]|flicker.pzap),0,100);save();r->send(200,"text/plain","OK");});
  jsonRoute("/api/staged/autofx",[](AsyncWebServerRequest*r,JsonVariant v){if(state!=IDLE){r->send(409,"text/plain","Nur IDLE");return;}for(uint8_t ch:{uint8_t(2),uint8_t(7),uint8_t(8)}){String key="a"+String(ch);JsonObject o=v[key];if(!o.isNull())readAuto(o,autoCfg[ch]);}save();r->send(200,"text/plain","OK");});
  jsonRoute("/api/staged/rblink",[](AsyncWebServerRequest*r,JsonVariant v){if(state!=IDLE){r->send(409,"text/plain","Nur IDLE");return;}for(uint8_t ch:{uint8_t(3),uint8_t(6)}){String key="rb"+String(ch);JsonObject o=v[key];if(!o.isNull()){Blink &b=ch==3?blink3:blink6;b.on_ms=constrain((int)(o["on_ms"]|b.on_ms),0,10000);b.off_ms=constrain((int)(o["off_ms"]|b.off_ms),0,10000);}}save();r->send(200,"text/plain","OK");});
  jsonRoute("/api/staged/motors",[](AsyncWebServerRequest*r,JsonVariant v){if(state!=IDLE){r->send(409,"text/plain","Nur IDLE");return;}JsonObject k=v["k4"],h=v["h5"];
    if(!k.isNull()){knock.hit_min=constrain((int)(k["hit_ms_min"]|knock.hit_min),0,5000);knock.hit_max=constrain((int)(k["hit_ms_max"]|knock.hit_max),0,5000);knock.gap_min=constrain((int)(k["gap_ms_min"]|knock.gap_min),0,10000);knock.gap_max=constrain((int)(k["gap_ms_max"]|knock.gap_max),0,10000);knock.cgap_min=constrain((int)(k["cluster_gap_min"]|knock.cgap_min),0,15000);knock.cgap_max=constrain((int)(k["cluster_gap_max"]|knock.cgap_max),0,15000);knock.cmin=constrain((int)(k["cluster_min"]|knock.cmin),1,20);knock.cmax=constrain((int)(k["cluster_max"]|knock.cmax),1,20);knock.dmin=constrain((int)(k["duty_min"]|knock.dmin),0,1023);knock.dmax=constrain((int)(k["duty_max"]|knock.dmax),0,1023);knock.paccent=constrain((int)(k["prob_accent"]|knock.paccent),0,100);}
    if(!h.isNull()){handleCfg.pmin=constrain((int)(h["pmin"]|handleCfg.pmin),0,10000);handleCfg.pmax=constrain((int)(h["pmax"]|handleCfg.pmax),0,10000);handleCfg.hmin=constrain((int)(h["hmin"]|handleCfg.hmin),0,10000);handleCfg.hmax=constrain((int)(h["hmax"]|handleCfg.hmax),0,10000);handleCfg.rmin=constrain((int)(h["rmin"]|handleCfg.rmin),0,10000);handleCfg.rmax=constrain((int)(h["rmax"]|handleCfg.rmax),0,10000);handleCfg.peak=constrain((int)(h["peak"]|handleCfg.peak),0,1023);handleCfg.jamp=constrain((int)(h["jamp"]|handleCfg.jamp),0,1023);handleCfg.jint_min=constrain((int)(h["jint_min"]|handleCfg.jint_min),0,5000);handleCfg.jint_max=constrain((int)(h["jint_max"]|handleCfg.jint_max),0,5000);handleCfg.repmin=constrain((int)(h["repmin"]|handleCfg.repmin),1,10);handleCfg.repmax=constrain((int)(h["repmax"]|handleCfg.repmax),1,10);handleCfg.gapmin=constrain((int)(h["gapmin"]|handleCfg.gapmin),0,10000);handleCfg.gapmax=constrain((int)(h["gapmax"]|handleCfg.gapmax),0,10000);}save();r->send(200,"text/plain","OK");});
  jsonRoute("/api/wifi",[](AsyncWebServerRequest*r,JsonVariant v){String mode=v["mode"]|"AP",ssid=v["ssid"]|"",a=v["pw1"]|"",b=v["pw2"]|"";if(a!=b||ssid.length()>32||(a.length()&&a.length()<8)){r->send(400,"text/plain","SSID/Passwort ungueltig");return;}
    if(mode=="STA"){if(ssid.isEmpty()){r->send(400,"text/plain","SSID fehlt");return;}staSsid=ssid;staPass=a;useAP=false;}
    else if(mode=="AP"){if(ssid.length())apSsid=ssid;if(a.length())apPass=a;useAP=true;}
    else{r->send(400,"text/plain","Modus ungueltig");return;}
    prefs.begin("butcherwifi",false);prefs.putBool("ap",useAP);prefs.putString("aps",apSsid);prefs.putString("app",apPass);prefs.putString("ss",staSsid);prefs.putString("sp",staPass);prefs.end();r->send(200,"text/plain","OK – neu starten");});
  server.begin();
  butcherBleStart();
}
void loop(){uint32_t now=millis();butcherBleTick();if(emergency()){if(state!=ESTOP){state=ESTOP;allOff();}}else if(state==ESTOP){allOff();}
  if(state!=ESTOP){for(uint8_t ch=1;ch<=8;ch++)runEffect(ch,now);
    if(state==RUNNING){while(nextStep<countSteps&&due(now,showStart+steps[nextStep].t_ms)&&!due(now,showStart+totalMs)){startEffect(steps[nextStep++]);}
      if(due(now,showStart+totalMs)){allOff();state=COOLDOWN;coolEnd=now+cooldownMs;}}
    else if(state==COOLDOWN&&due(now,coolEnd)){state=IDLE;if(loopMode)beginShow();}
    for(uint8_t ch:{uint8_t(2),uint8_t(7),uint8_t(8)})autoUpdate(ch,now);
  }
  static bool sRaw=true,sStable=true,sArmed=true,rRaw=true,rStable=true;static uint32_t sChanged=0,sDown=0,rChanged=0,rDown=0;
  bool s=digitalRead(START_PIN);if(s!=sRaw){sRaw=s;sChanged=now;}if(due(now,sChanged+120)&&s!=sStable){sStable=s;if(!s){sDown=now;sArmed=true;}else sArmed=true;}
  if(!sStable&&sArmed&&due(now,sDown+60)){sArmed=false;beginShow();}
  bool r=digitalRead(RESET_PIN);if(r!=rRaw){rRaw=r;rChanged=now;}if(due(now,rChanged+120)&&r!=rStable){rStable=r;if(!r)rDown=now;else if(due(now,rDown+8000))ESP.restart();else if(state==ESTOP&&!emergency()){allOff();state=IDLE;}}
  if(!rStable && due(now,rDown+3000) && !due(now,rDown+8000)) butcherBleApprove();
  ledcWrite(STATUS_PIN,state==ESTOP?(now/100%2?1023:0):state==COOLDOWN?(now/10%1024):1023);
}
