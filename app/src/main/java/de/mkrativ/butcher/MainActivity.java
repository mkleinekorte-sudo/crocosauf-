package de.mkrativ.butcher;

import android.app.*;
import android.os.*;
import android.graphics.Color;
import android.content.*;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.net.*;
import java.io.*;
import java.util.*;

public class MainActivity extends Activity {
  LinearLayout root, content; TextView status; EditText host; JSONObject live; JSONArray steps=new JSONArray();
  Map<String,EditText> fields=new LinkedHashMap<>(); Map<String,Switch> switches=new LinkedHashMap<>();
  final Handler handler=new Handler(); String base="http://192.168.4.1";
  interface Done { void run(String s) throws Exception; }
  void request(String path,String body,Done done){
    String url=base+path;
    new Thread(()->{try{
      HttpURLConnection c=(HttpURLConnection)new URL(url).openConnection(); c.setConnectTimeout(4000);c.setReadTimeout(5000);
      if(body!=null){c.setRequestMethod("POST");c.setDoOutput(true);c.setRequestProperty("Content-Type","application/json");c.getOutputStream().write(body.getBytes("UTF-8"));}
      else if(!path.equals("/api/status") && !path.startsWith("/api/test?")){c.setRequestMethod("POST");}
      int code=c.getResponseCode(); InputStream in=code<400?c.getInputStream():c.getErrorStream();
      ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] b=new byte[4096];int n;while((n=in.read(b))!=-1)out.write(b,0,n);
      String response=out.toString("UTF-8");c.disconnect();
      if(code>=400)throw new IOException(code+": "+response);
      handler.post(()->{try{done.run(response);}catch(Exception e){toast(e.getMessage());}});
    }catch(Exception e){handler.post(()->toast("Verbindung: "+e.getMessage()));}}).start();
  }
  void toast(String s){Toast.makeText(this,s,Toast.LENGTH_LONG).show();}
  TextView text(String s){TextView t=new TextView(this);t.setText(s);t.setTextSize(17);t.setPadding(14,14,14,8);return t;}
  Button button(String title,Runnable action){Button b=new Button(this);b.setText(title);b.setOnClickListener(v->action.run());return b;}
  EditText input(LinearLayout box,String key,Object value){box.addView(text(key));EditText e=new EditText(this);e.setSingleLine();e.setInputType(android.text.InputType.TYPE_CLASS_NUMBER|android.text.InputType.TYPE_NUMBER_FLAG_SIGNED);e.setText(String.valueOf(value));box.addView(e);fields.put(key,e);return e;}
  Switch toggle(LinearLayout box,String key,boolean value){Switch s=new Switch(this);s.setText(key);s.setChecked(value);box.addView(s);switches.put(key,s);return s;}
  LinearLayout panel(){LinearLayout x=new LinearLayout(this);x.setOrientation(1);x.setPadding(12,12,12,18);content.addView(x);return x;}
  void page(String name){fields.clear();switches.clear();content.removeAllViews();
    if(live==null){content.addView(text("Erst mit dem ESP32 verbinden."));return;}
    try{switch(name){case "Steuerung":control();break;case "Zeitpläne":schedules();break;case "Effekte":effects();break;case "WLAN":wifi();break;}}catch(Exception e){toast(e.getMessage());}
  }
  void control()throws Exception{LinearLayout p=panel();p.addView(text("Status: "+live.optString("state")+" · E-Stop: "+(live.optBoolean("estop")?"AKTIV":"OK")));
    p.addView(text("Restzeit: "+live.optInt("t_left_ms")+" ms · Sperrzeit: "+live.optInt("t_cool_ms")+" ms"));
    p.addView(button("START",()->request("/api/start",null,s->refresh())));
    p.addView(button("ALLES AUS",()->request("/api/alloff",null,s->refresh())));
    p.addView(button("Loop: "+(live.optBoolean("loop")?"AN":"AUS"),()->request("/api/loop",null,s->refresh())));
    input(p,"total_time_ms",live.optInt("total_time_ms")); input(p,"cooldown_ms",live.optInt("cooldown_ms"));
    toggle(p,"k1_autopulse",live.optBoolean("k1_autopulse"));input(p,"k1_pulse_ms",live.optInt("k1_pulse_ms"));
    p.addView(button("Grundzeiten speichern",()->{try{JSONObject cfg=values("total_time_ms","cooldown_ms","k1_pulse_ms");cfg.put("k1_autopulse",switches.get("k1_autopulse").isChecked());
      request("/api/staged/config",cfg.toString(),s->request("/api/apply",null,t->refresh()));}catch(Exception e){toast(e.getMessage());}}));
    p.addView(button("WERKSEINSTELLUNGEN",()->new AlertDialog.Builder(this).setMessage("Alle Einstellungen einschließlich WLAN zurücksetzen?")
      .setPositiveButton("Zurücksetzen",(d,w)->request("/api/defaults",null,s->refresh())).setNegativeButton("Abbrechen",null).show()));
    for(int ch=1;ch<=8;ch++){final int k=ch;LinearLayout q=panel();q.addView(text("K"+ch));for(String op:new String[]{"on","off","pulse"})q.addView(button(op.toUpperCase(),()->request("/api/test?ch="+k+"&op="+op,null,s->refresh())));}
  }
  JSONObject values(String... keys)throws Exception{JSONObject o=new JSONObject();for(String k:keys){String v=fields.get(k).getText().toString();o.put(k,Long.parseLong(v));}return o;}
  void group(String title,String source,String endpoint,String[] keys)throws Exception{
    JSONObject data=live.getJSONObject(source);LinearLayout p=panel();p.addView(text(title));
    Map<String,EditText> localFields=new LinkedHashMap<>();Map<String,Switch> localSwitches=new LinkedHashMap<>();
    for(String key:keys){Object v=data.opt(key);if(v instanceof Boolean)localSwitches.put(key,toggle(p,key,(Boolean)v));else localFields.put(key,input(p,key,v));}
    p.addView(button("Speichern",()->{try{JSONObject o=new JSONObject();for(String k:keys){if(localSwitches.containsKey(k))o.put(k,localSwitches.get(k).isChecked());else o.put(k,Long.parseLong(localFields.get(k).getText().toString()));}
      JSONObject payload=new JSONObject();if(endpoint.equals("/api/staged/autofx"))payload.put(source.replace("auto","a"),o);
      else if(endpoint.equals("/api/staged/rblink"))payload.put(source.replace("rblink","rb"),o);
      else if(endpoint.equals("/api/staged/motors")){if(source.equals("k4")){
        String[][] aliases={{"hit_min","hit_ms_min"},{"hit_max","hit_ms_max"},{"gap_min","gap_ms_min"},{"gap_max","gap_ms_max"},{"cmin","cluster_min"},{"cmax","cluster_max"},{"cgap_min","cluster_gap_min"},{"cgap_max","cluster_gap_max"},{"dmin","duty_min"},{"dmax","duty_max"},{"paccent","prob_accent"}};
        JSONObject mapped=new JSONObject();for(String[] a:aliases)mapped.put(a[1],o.get(a[0]));o=mapped;}payload.put(source,o);}
      else payload=o;
      request(endpoint,payload.toString(),s->refresh());
    }catch(Exception e){toast("Eingabe prüfen: "+e.getMessage());}}));
  }
  void effects()throws Exception{
    group("Flackern K2/K7/K8","flicker","/api/staged/flicker",new String[]{"zmin","zmax","jmin","jmax","dmin","dmax","pdark","pzap"});
    for(int ch:new int[]{2,7,8})group("K"+ch+" AutoFX","auto"+ch,"/api/staged/autofx",new String[]{"enabled_idle","enabled_run","pause_min","pause_max","on_min","on_max","duty_min","duty_max"});
    for(int ch:new int[]{3,6})group("K"+ch+" Blinken","rblink"+ch,"/api/staged/rblink",new String[]{"on_ms","off_ms"});
    group("K4 Klopfen","k4","/api/staged/motors",new String[]{"hit_min","hit_max","gap_min","gap_max","cgap_min","cgap_max","dmin","dmax","cmin","cmax","paccent"});
    group("K5 Klinke","h5","/api/staged/motors",new String[]{"pmin","pmax","hmin","hmax","rmin","rmax","peak","jamp","jint_min","jint_max","repmin","repmax","gapmin","gapmax"});
  }
  void schedules()throws Exception{
    LinearLayout p=panel();p.addView(text("Zeitplan (max. 96 Schritte)"));
    for(int i=0;i<steps.length();i++){JSONObject step=steps.getJSONObject(i);final int idx=i;
      p.addView(button("K"+step.optInt("channel")+" · "+step.optInt("t_ms")+" ms · "+step.optInt("action")+" · "+step.optInt("dur_ms")+" ms  ✕",()->{
        JSONArray next=new JSONArray();for(int j=0;j<steps.length();j++)if(j!=idx)next.put(steps.opt(j));steps=next;page("Zeitpläne");}));}
    Spinner ch=new Spinner(this),act=new Spinner(this);ch.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"K1","K2","K3","K4","K5","K6","K7","K8"}));
    String[] actions={"AUS","AN","PULS","FLACKERN","KLOPFEN","KLINKE","BLINKEN"};act.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,actions));p.addView(ch);p.addView(act);
    EditText t=input(p,"Start in ms",0),d=input(p,"Dauer in ms",500);
    p.addView(button("Schritt hinzufügen",()->{try{if(steps.length()>=96)throw new Exception("Maximal 96 Schritte");JSONObject o=new JSONObject();o.put("channel",ch.getSelectedItemPosition()+1);o.put("action",act.getSelectedItemPosition());o.put("t_ms",Long.parseLong(t.getText().toString()));o.put("dur_ms",Long.parseLong(d.getText().toString()));steps.put(o);page("Zeitpläne");}catch(Exception e){toast(e.getMessage());}}));
    p.addView(button("Zeitplan speichern",()->request("/api/staged/steps",steps.toString(),s->request("/api/apply",null,x->refresh()))));
  }
  void wifi(){LinearLayout p=panel();p.addView(text("WLAN des ESP32 ändern (danach Neustart)"));Spinner mode=new Spinner(this);mode.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"AP","STA"}));p.addView(mode);
    EditText ssid=new EditText(this);ssid.setHint("SSID");p.addView(ssid);EditText pw=new EditText(this);pw.setHint("Passwort");p.addView(pw);
    p.addView(button("WLAN speichern",()->{try{JSONObject o=new JSONObject();o.put("mode",mode.getSelectedItem().toString());o.put("ssid",ssid.getText().toString());o.put("pw1",pw.getText().toString());o.put("pw2",pw.getText().toString());request("/api/wifi",o.toString(),s->toast("ESP32 startet neu"));}catch(Exception e){toast(e.getMessage());}}));
  }
  void refresh(){request("/api/status",null,s->{live=new JSONObject(s);steps=live.getJSONArray("steps");status.setText(live.optString("state")+" · "+live.optString("ip")+(live.optBoolean("estop")?" · E-STOP":""));});}
  public void onCreate(Bundle b){super.onCreate(b);root=new LinearLayout(this);root.setOrientation(1);root.setPadding(12,12,12,0);root.setBackgroundColor(Color.rgb(250,250,250));setContentView(root);
    host=new EditText(this);host.setSingleLine();host.setText(getPreferences(0).getString("host","192.168.4.1"));host.setHint("ESP32 IP-Adresse");root.addView(host);
    root.addView(button("Verbinden / Aktualisieren",()->{base="http://"+host.getText().toString().trim().replace("http://","").replaceAll("/$","");getPreferences(0).edit().putString("host",host.getText().toString()).apply();refresh();}));
    status=text("Nicht verbunden");root.addView(status);HorizontalScrollView nav=new HorizontalScrollView(this);LinearLayout tabs=new LinearLayout(this);for(String name:new String[]{"Steuerung","Zeitpläne","Effekte","WLAN"})tabs.addView(button(name,()->page(name)));nav.addView(tabs);root.addView(nav);
    ScrollView scroll=new ScrollView(this);content=new LinearLayout(this);content.setOrientation(1);scroll.addView(content);root.addView(scroll);refresh();
  }
}
