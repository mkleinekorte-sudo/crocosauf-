#pragma once
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
