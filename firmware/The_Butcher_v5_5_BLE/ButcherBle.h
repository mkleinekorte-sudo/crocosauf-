#pragma once
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
