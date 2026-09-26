#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
using std::min;using std::max;
class String: public std::string {
public:
 using std::string::string;
 String()=default;
 String(const std::string& s):std::string(s){}
 template<class T,typename std::enable_if<std::is_arithmetic<T>::value,int>::type=0>
 String(T value):std::string(std::to_string(value)){}
 void trim(){auto a=find_first_not_of(" \r\n\t");if(a==npos){clear();return;}auto b=find_last_not_of(" \r\n\t");*this=substr(a,b-a+1);}
 void replace(const String& a,const String& b){size_t i=0;while((i=find(a,i))!=npos){std::string::replace(i,a.size(),b);i+=b.size();}}
 long toInt()const{try{return std::stol(*this);}catch(...){return 0;}}
 String substring(size_t a,size_t b)const{return substr(a,b-a);}
};
template<class T> T constrain(T v,T lo,T hi){return min(hi,max(lo,v));}
inline long map(long x,long a,long b,long c,long d){return (x-a)*(d-c)/(b-a)+c;}
inline uint32_t mockMillis=10000;
inline uint32_t millis(){return mockMillis;}
inline uint32_t micros(){return mockMillis*1000;}
inline bool setCpuFrequencyMhz(int){return true;}
inline void delay(uint32_t ms){mockMillis+=ms;}
inline void randomSeed(uint32_t n){std::srand(n);}
inline long random(long n){return n?std::rand()%n:0;}
inline long random(long a,long b){return a+random(b-a);}
inline uint8_t random8(){return random(256);}
inline uint32_t esp_random(){return (uint32_t)std::rand();}
inline uint8_t random8(int m){return random(m);}
inline uint8_t random8(int a,int b){return random(a,b);}
inline uint8_t sin8(uint8_t n){return 128+127*std::sin(n/255.0*6.28);}
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2
#define RTC_DATA_ATTR
#define SERIAL_8N1 0
inline std::array<int,40> pins{};
inline int forceBusy=-1;
inline void pinMode(int,int){}
inline int digitalRead(int pin){return pin==33 && forceBusy>=0?forceBusy:pins[pin];}
struct Stream{};
struct HardwareSerial:Stream{
 HardwareSerial(int=0){}
 void begin(int,int=0,int=0,int=0){}
 void println(const String& s){(void)s;}
};
inline HardwareSerial Serial;
struct Preferences{
 std::map<std::string,uint64_t> n;std::map<std::string,String> s;
 void begin(const char*,bool){}
 void clear(){n.clear();s.clear();}
 uint64_t get(const char* k,uint64_t v){return n.count(k)?n[k]:v;}
 uint16_t getUShort(const char*k,uint16_t v){return get(k,v);}
 uint8_t getUChar(const char*k,uint8_t v){return get(k,v);}
 uint32_t getULong(const char*k,uint32_t v){return get(k,v);}
 int getInt(const char*k,int v){return get(k,v);}
 bool getBool(const char*k,bool v){return get(k,v);}
 String getString(const char*k,const char*v){return s.count(k)?s[k]:String(v);}
 void putUShort(const char*k,uint16_t v){n[k]=v;}
 void putUChar(const char*k,uint8_t v){n[k]=v;}
 void putULong(const char*k,uint32_t v){n[k]=v;}
 void putInt(const char*k,int v){n[k]=v;}
 void putBool(const char*k,bool v){n[k]=v;}
 void putString(const char*k,String v){s[k]=v;}
};
struct IPAddress{IPAddress(int,int,int,int){}};
#define WIFI_OFF 0
#define WIFI_AP 1
struct WiFiClass{
 int clients=0;bool enabled=false;
 bool mode(int m){enabled=m!=0;return true;}
 bool softAP(const char*,const char*){enabled=true;return true;}
 bool softAPConfig(IPAddress,IPAddress,IPAddress){return true;}
 int softAPgetStationNum(){return clients;}
 void softAPdisconnect(bool){enabled=false;}
};
inline WiFiClass WiFi;
#define HTTP_GET 0
#define HTTP_POST 1
struct WebServer {
 std::map<String,String> args,headers;
 std::map<String,std::function<void()>> routes;
 int code=0;String response,type;
 WebServer(int){}
 void on(String path,int method,std::function<void()> handler){routes[path+":"+String(method)]=handler;}
 void onNotFound(std::function<void()>){}
 void begin(){}
 void stop(){}
 void handleClient(){}
 bool hasArg(String k){return args.count(k);}
 String arg(String k){return args.count(k)?args[k]:String("");}
 void sendHeader(String k,String v){headers[k]=v;}
 void send(int c,String t="",String body=""){code=c;type=t;response=body;}
};
struct CHSV{int h,s,v;CHSV(int a,int b,int c):h(a),s(b),v(c){}};
struct CRGB{
 int r=0,g=0,b=0;
 CRGB()=default;CRGB(int a,int d,int c):r(a),g(d),b(c){}
 CRGB(CHSV c):r(c.h),g(c.s),b(c.v){}
 CRGB& operator+=(CRGB c){r+=c.r;g+=c.g;b+=c.b;return *this;}
 bool operator==(CRGB c)const{return r==c.r&&g==c.g&&b==c.b;}
 static const CRGB Blue,White,Yellow,Aqua,Cyan,Red,Green,Black;
};
inline const CRGB CRGB::Blue{0,0,255},CRGB::White{255,255,255},CRGB::Yellow{255,255,0},
 CRGB::Aqua{0,255,255},CRGB::Cyan{0,255,255},CRGB::Red{255,0,0},CRGB::Green{0,128,0},CRGB::Black{0,0,0};
struct WS2812B{};
#define GRB 0
struct FastLEDClass{
 CRGB* data=nullptr;int count=0;
 template<class T,int P,int O>void addLeds(CRGB* p,int n){data=p;count=n;}
 void setBrightness(int){}
 void clear(){if(data)std::fill(data,data+count,CRGB::Black);}
 void show(){}
};
inline FastLEDClass FastLED;
inline void fill_solid(CRGB* p,int n,CRGB c){std::fill(p,p+n,c);}
inline void fadeToBlackBy(CRGB* p,int n,int){for(int i=0;i<n;i++)p[i]=CRGB(p[i].r/2,p[i].g/2,p[i].b/2);}
#define LED_ON 1
#define LED_OFF 0
struct Adafruit_8x8matrix{
 std::array<int,64> pixels{};String lastText;
 void begin(int){}
 void setRotation(int){}
 void clear(){pixels.fill(0);}
 void writeDisplay(){}
 void drawPixel(int x,int y,int v){if(x>=0&&y>=0&&x<8&&y<8)pixels[y*8+x]=v;}
 void drawLine(int a,int b,int c,int d,int v){if(a==c){for(int y=min(b,d);y<=max(b,d);y++)drawPixel(a,y,v);}else for(int x=min(a,c);x<=max(a,c);x++)drawPixel(x,b,v);}
 void drawRect(int x,int y,int w,int h,int v){drawLine(x,y,x+w-1,y,v);drawLine(x,y+h-1,x+w-1,y+h-1,v);drawLine(x,y,x,y+h-1,v);drawLine(x+w-1,y,x+w-1,y+h-1,v);}
 void setTextWrap(bool){}
 void setTextSize(int){}
 void setTextColor(int){}
 void setCursor(int,int){}
 void print(String v){lastText=v;}
};
struct WireClass{void begin(int,int){}};
inline WireClass Wire;
inline bool mockAudioEnabled=true;
inline uint32_t mockAudioEnd=0;
inline bool mockPlaying=false,mockLoop=false;
inline uint32_t mockTrackDuration=12000;
struct DFPlayerMini_Fast{
 struct Command{String name;int a,b;uint32_t at;};
 std::vector<Command> commands;
 bool begin(Stream&,bool=false,unsigned long=100){return true;}
 void add(String name,int a=0,int b=0){commands.push_back({name,a,b,millis()});}
 void stop(){add("stop");mockPlaying=false;pins[33]=HIGH;}
 void stopRepeat(){add("stopRepeat");mockLoop=false;}
 void stopRepeatPlay(){add("stopRepeatPlay");}
 void volume(uint8_t v){add("volume",v);}
 void start(bool loop){mockLoop=loop;mockPlaying=mockAudioEnabled;pins[33]=mockAudioEnabled?LOW:HIGH;mockAudioEnd=millis()+mockTrackDuration;}
 void play(uint16_t n){add("play",n);start(false);}
 void loop(uint16_t n){add("loop",n);start(true);}
 void playFolder(uint8_t f,uint8_t n){add("folder",f,n);start(false);}
};
enum esp_sleep_wakeup_cause_t{ESP_SLEEP_WAKEUP_UNDEFINED,ESP_SLEEP_WAKEUP_EXT0,ESP_SLEEP_WAKEUP_TIMER};
enum esp_reset_reason_t{ESP_RST_POWERON,ESP_RST_DEEPSLEEP,ESP_RST_SW};
inline esp_sleep_wakeup_cause_t mockWake=ESP_SLEEP_WAKEUP_UNDEFINED;
inline esp_reset_reason_t mockReset=ESP_RST_POWERON;
inline auto esp_sleep_get_wakeup_cause(){return mockWake;}
inline auto esp_reset_reason(){return mockReset;}
using gpio_num_t=int;
#define RTC_GPIO_MODE_INPUT_ONLY 0
inline void rtc_gpio_init(gpio_num_t){}
inline void rtc_gpio_deinit(gpio_num_t){}
inline void rtc_gpio_set_direction(gpio_num_t,int){}
inline void rtc_gpio_pullup_en(gpio_num_t){}
inline void rtc_gpio_pulldown_dis(gpio_num_t){}
inline uint64_t mockWakeMicros=0;
inline void esp_sleep_enable_ext0_wakeup(gpio_num_t,int){}
inline void esp_sleep_enable_timer_wakeup(uint64_t t){mockWakeMicros=t;}
struct Slept{};
inline void esp_deep_sleep_start(){throw Slept{};}
