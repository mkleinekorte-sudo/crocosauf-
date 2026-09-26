#pragma once
#include "host_stubs.h"
#include <cstring>
#include <deque>
class BLEServer;
class BLECharacteristic;
class BLEServerCallbacks {
public: virtual ~BLEServerCallbacks()=default; virtual void onConnect(BLEServer*){} virtual void onDisconnect(BLEServer*){}
};
class BLECharacteristicCallbacks {
public:virtual ~BLECharacteristicCallbacks()=default;virtual void onWrite(BLECharacteristic*){}
};
class BLE2902 {public:bool enabled=true;bool getNotifications(){return enabled;}};
class BLECharacteristic {
public:
 static constexpr int PROPERTY_NOTIFY=1,PROPERTY_WRITE=2;
 String value;BLECharacteristicCallbacks* callbacks=nullptr;std::vector<String> notifications;
 void addDescriptor(BLE2902*){}
 void setCallbacks(BLECharacteristicCallbacks* c){callbacks=c;}
 String getValue(){return value;}
 void setValue(uint8_t* p,size_t n){value=String(std::string((char*)p,n));}
 void notify(){notifications.push_back(value);}
 void receive(String text){value=text;callbacks->onWrite(this);}
};
class BLEService {
public:std::vector<BLECharacteristic*> chars;
 BLECharacteristic* createCharacteristic(const char*,int){auto p=new BLECharacteristic;chars.push_back(p);return p;}
 void start(){}
};
class BLEAdvertising {
public:bool active=false;void addServiceUUID(const char*){}void setScanResponse(bool){}void setMinPreferred(int){}void start(){active=true;}void stop(){active=false;}
};
class BLEServer {
public:BLEServerCallbacks* callbacks=nullptr;BLEService service;
 void setCallbacks(BLEServerCallbacks* p){callbacks=p;}
 BLEService* createService(const char*){return &service;}
 int getConnId(){return 0;}
 void disconnect(int){callbacks->onDisconnect(this);}
};
class BLEDevice {
public:inline static BLEServer server;inline static BLEAdvertising advertising;
 static void init(const char*){}static BLEServer* createServer(){return &server;}
 static BLEAdvertising* getAdvertising(){return &advertising;}
 static void startAdvertising(){advertising.start();}
};
struct MockQueue {size_t max,item;std::deque<std::vector<char>> data;};
using QueueHandle_t=MockQueue*;
#define pdTRUE 1
inline QueueHandle_t xQueueCreate(size_t max,size_t item){return new MockQueue{max,item,{}};}
inline int xQueueSend(QueueHandle_t q,const void* item,int){if(q->data.size()>=q->max)return 0;q->data.emplace_back((const char*)item,(const char*)item+q->item);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* item,int){if(q->data.empty())return 0;memcpy(item,q->data.front().data(),q->item);q->data.pop_front();return 1;}
