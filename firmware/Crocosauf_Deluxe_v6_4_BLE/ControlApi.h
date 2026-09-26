#pragma once
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
