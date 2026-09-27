package de.mkrativ.butcher;

import android.app.*;
import android.os.*;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.content.*;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.net.*;
import java.io.*;
import java.util.*;

public class MainActivity extends Activity {
  LinearLayout root, content, tabs; TextView status; JSONObject live; JSONArray steps=new JSONArray();
  String selectedPage="Show";
  Map<String,EditText> fields=new LinkedHashMap<>(); Map<String,Switch> switches=new LinkedHashMap<>();
  final Handler handler=new Handler(); BleClient ble; LinearLayout devices;
  final Map<String,android.bluetooth.BluetoothDevice> found=new LinkedHashMap<>();
  interface Done { void run(String s) throws Exception; }
  void request(String path,String body,Done done){
    if(ble==null||!ble.ready()){toast("Bitte Bluetooth verbinden.");return;}
    ble.send(path,body,r->{if(!r.ok()){toast(r.code+": "+r.body);return;}
      try{done.run(r.body);}catch(Exception e){toast(e.getMessage());}});
  }
  void permissionsAndScan(){
    if(android.os.Build.VERSION.SDK_INT>=31){
      if(checkSelfPermission("android.permission.BLUETOOTH_SCAN")!=android.content.pm.PackageManager.PERMISSION_GRANTED||
         checkSelfPermission("android.permission.BLUETOOTH_CONNECT")!=android.content.pm.PackageManager.PERMISSION_GRANTED){
        requestPermissions(new String[]{"android.permission.BLUETOOTH_SCAN","android.permission.BLUETOOTH_CONNECT"},7);return;}
    }else if(android.os.Build.VERSION.SDK_INT>=23&&checkSelfPermission("android.permission.ACCESS_FINE_LOCATION")!=android.content.pm.PackageManager.PERMISSION_GRANTED){
      requestPermissions(new String[]{"android.permission.ACCESS_FINE_LOCATION"},7);return;}
    found.clear();devices.removeAllViews();status.setText("Suche The Butcher …");ble.scan();
  }
  @Override public void onRequestPermissionsResult(int req,String[] p,int[] g){super.onRequestPermissionsResult(req,p,g);
    if(req==7){for(int x:g)if(x!=android.content.pm.PackageManager.PERMISSION_GRANTED){toast("Bluetooth-Berechtigung erforderlich.");return;}permissionsAndScan();}}
  void approvalCheck(){if(ble==null||!ble.ready())return;ble.send("/hello",null,r->{try{
    JSONObject o=new JSONObject(r.body);if(!o.optString("product").equals("butcher")){toast("Falsches Gerät.");return;}
    if(o.optBoolean("approved")){refresh();return;}
    status.setText("Verbunden · Reset-Taster am ESP32 3 Sekunden halten");handler.postDelayed(this::approvalCheck,1600);
  }catch(Exception e){toast(e.getMessage());}});}
  int dp(int n){return (int)(n*getResources().getDisplayMetrics().density+.5f);}
  GradientDrawable bg(int fill,int stroke,int radius){GradientDrawable d=new GradientDrawable();d.setColor(fill);d.setCornerRadius(dp(radius));d.setStroke(dp(1),stroke);return d;}
  void toast(String s){Toast.makeText(this,s,Toast.LENGTH_LONG).show();}
  TextView text(String s){TextView t=new TextView(this);t.setText(s);t.setTextColor(Color.rgb(239,226,212));t.setTextSize(15);t.setPadding(dp(4),dp(9),dp(4),dp(5));return t;}
  Button button(String title,Runnable action){Button b=new Button(this);b.setText(title);b.setAllCaps(false);b.setTextColor(Color.WHITE);b.setTextSize(15);b.setTypeface(null,Typeface.BOLD);
    b.setBackground(bg(0xD9341017,0xFFB34B39,12));LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,dp(48));lp.setMargins(0,dp(5),0,dp(5));b.setLayoutParams(lp);b.setOnClickListener(v->action.run());return b;}
  EditText input(LinearLayout box,String key,Object value){box.addView(text(key.replace('_',' ').toUpperCase(Locale.ROOT)));EditText e=new EditText(this);e.setSingleLine();e.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);e.setTextColor(Color.WHITE);e.setHintTextColor(0xFFB99F94);e.setBackground(bg(0xD9181619,0xFF745043,9));e.setPadding(dp(12),0,dp(12),0);e.setText(String.valueOf(value));box.addView(e,new LinearLayout.LayoutParams(-1,dp(46)));fields.put(key,e);return e;}
  Switch toggle(LinearLayout box,String key,boolean value){Switch s=new Switch(this);s.setText(key.replace('_',' ').toUpperCase(Locale.ROOT));s.setTextColor(Color.WHITE);s.setChecked(value);s.setPadding(dp(5),dp(12),dp(5),dp(12));box.addView(s);switches.put(key,s);return s;}
  LinearLayout panel(){LinearLayout x=new LinearLayout(this);x.setOrientation(1);x.setPadding(dp(16),dp(13),dp(16),dp(18));x.setBackground(bg(0xE7181418,0xFF754236,16));
    LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.setMargins(0,0,0,dp(12));content.addView(x,lp);return x;}
  void page(String name){selectedPage=name;fields.clear();switches.clear();content.removeAllViews();
    if(live==null){content.addView(text("Erst mit dem ESP32 verbinden."));return;}
    try{switch(name){case "Show":control();break;case "Kanäle":channels();break;case "Zeitplan":schedules();break;case "Effekte":effects();break;case "WLAN":wifi();break;}}catch(Exception e){toast(e.getMessage());}
  }
  void control()throws Exception{LinearLayout p=panel();p.addView(text("SHOW CONTROL  /  "+live.optString("state")+"  /  E-STOP "+(live.optBoolean("estop")?"AKTIV":"OK")));
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
  }
  void channels(){String[] names={"Videoplayer","Lampe 1 · PWM","Lampe 2 · Relais","Tür / Klopfen","Klinke · PWM","Pumpe / Nebel","Seitenlampe links","Seitenlampe rechts"};
    for(int ch=1;ch<=8;ch++){final int k=ch;LinearLayout q=panel();q.addView(text("K"+ch+"  /  "+names[ch-1]));for(String op:new String[]{"on","off","pulse"})q.addView(button(op.toUpperCase(Locale.ROOT),()->request("/api/test?ch="+k+"&op="+op,null,s->refresh())));}
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
        JSONArray next=new JSONArray();for(int j=0;j<steps.length();j++)if(j!=idx)next.put(steps.opt(j));steps=next;page("Zeitplan");}));}
    Spinner ch=new Spinner(this),act=new Spinner(this);ch.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"K1","K2","K3","K4","K5","K6","K7","K8"}));
    String[] actions={"AUS","AN","PULS","FLACKERN","KLOPFEN","KLINKE","BLINKEN"};act.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,actions));p.addView(ch);p.addView(act);
    EditText t=input(p,"Start in ms",0),d=input(p,"Dauer in ms",500);
    p.addView(button("Schritt hinzufügen",()->{try{if(steps.length()>=96)throw new Exception("Maximal 96 Schritte");JSONObject o=new JSONObject();o.put("channel",ch.getSelectedItemPosition()+1);o.put("action",act.getSelectedItemPosition());o.put("t_ms",Long.parseLong(t.getText().toString()));o.put("dur_ms",Long.parseLong(d.getText().toString()));steps.put(o);page("Zeitplan");}catch(Exception e){toast(e.getMessage());}}));
    p.addView(button("Zeitplan speichern",()->request("/api/staged/steps",steps.toString(),s->request("/api/apply",null,x->refresh()))));
  }
  void wifi(){LinearLayout p=panel();p.addView(text("WLAN-ZUGANG  /  ESP32"));p.addView(text("Die App bleibt über Bluetooth verbunden."));Spinner mode=new Spinner(this);mode.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"AP","STA"}));p.addView(mode);
    EditText ssid=new EditText(this);ssid.setHint("SSID");ssid.setTextColor(Color.WHITE);ssid.setHintTextColor(0xFFB99F94);p.addView(ssid);EditText pw=new EditText(this);pw.setHint("Passwort");pw.setInputType(android.text.InputType.TYPE_CLASS_TEXT|android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD);pw.setTextColor(Color.WHITE);pw.setHintTextColor(0xFFB99F94);p.addView(pw);
    p.addView(button("WLAN speichern",()->{try{JSONObject o=new JSONObject();o.put("mode",mode.getSelectedItem().toString());o.put("ssid",ssid.getText().toString());o.put("pw1",pw.getText().toString());o.put("pw2",pw.getText().toString());request("/api/wifi",o.toString(),s->toast("WLAN gespeichert"));}catch(Exception e){toast(e.getMessage());}}));
  }
  void refresh(){request("/api/status",null,s->{boolean first=live==null;live=new JSONObject(s);steps=live.getJSONArray("steps");status.setText("●  "+live.optString("state")+"  ·  "+live.optString("ip")+(live.optBoolean("estop")?"  ·  E-STOP":""));tabs.setVisibility(View.VISIBLE);if(first)page("Show");});}
  public void onCreate(Bundle b){super.onCreate(b);getWindow().setStatusBarColor(0xFF13090B);getWindow().setNavigationBarColor(0xFF13090B);
    FrameLayout frame=new FrameLayout(this);setContentView(frame);ImageView art=new ImageView(this);art.setImageResource(R.drawable.butcher_bg);art.setScaleType(ImageView.ScaleType.CENTER_CROP);frame.addView(art,new FrameLayout.LayoutParams(-1,-1));
    View shade=new View(this);shade.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,new int[]{0xAA10080A,0xD910080B,0xF5080709}));frame.addView(shade,new FrameLayout.LayoutParams(-1,-1));
    ScrollView whole=new ScrollView(this);whole.setFillViewport(true);frame.addView(whole,new FrameLayout.LayoutParams(-1,-1));
    root=new LinearLayout(this);root.setOrientation(1);root.setPadding(dp(18),dp(28),dp(18),dp(28));whole.addView(root);
    TextView brand=text("mkrativ  /  HALLOWEEN EXPERIENCE");brand.setTextColor(0xFFFFB466);brand.setTextSize(12);brand.setTypeface(null,Typeface.BOLD);brand.setLetterSpacing(.14f);root.addView(brand);
    TextView hero=text("THE\nBUTCHER");hero.setTextSize(47);hero.setLineSpacing(-dp(9),1);hero.setTypeface(Typeface.create("serif",Typeface.BOLD));hero.setTextColor(0xFFF3E3CC);hero.setShadowLayer(dp(12),0,dp(3),0xFFB11F17);root.addView(hero);
    TextView sub=text("SHOW CONTROL  •  BLUETOOTH");sub.setTextColor(0xFFEAA36F);sub.setTextSize(12);sub.setLetterSpacing(.18f);root.addView(sub);
    status=text("●  Bluetooth nicht verbunden");status.setBackground(bg(0xDB1C1116,0xFF774136,12));status.setPadding(dp(15),dp(14),dp(15),dp(14));root.addView(status);
    root.addView(button("⌁  THE BUTCHER SUCHEN",this::permissionsAndScan));devices=new LinearLayout(this);devices.setOrientation(1);root.addView(devices);
    HorizontalScrollView nav=new HorizontalScrollView(this);nav.setHorizontalScrollBarEnabled(false);tabs=new LinearLayout(this);tabs.setOrientation(0);
    for(String name:new String[]{"Show","Kanäle","Zeitplan","Effekte","WLAN"}){Button tab=button(name,()->page(name));LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(dp(108),dp(46));lp.setMargins(0,dp(8),dp(6),dp(8));tabs.addView(tab,lp);}
    nav.addView(tabs);root.addView(nav);tabs.setVisibility(View.GONE);
    content=new LinearLayout(this);content.setOrientation(1);root.addView(content);
    ble=new BleClient(this,new BleClient.Listener(){
      public void onDevice(android.bluetooth.BluetoothDevice device,int rssi){String key=device.getAddress();if(found.containsKey(key))return;found.put(key,device);
        devices.addView(button("The Butcher · "+key+" ("+rssi+" dBm)",()->{devices.removeAllViews();ble.connect(device);}));}
      public void onReady(){status.setText("Verbunden · Reset-Taster 3 Sekunden halten");approvalCheck();}
      public void onDisconnected(String reason){live=null;status.setText(reason);content.removeAllViews();tabs.setVisibility(View.GONE);}
      public void onMessage(String msg){status.setText(msg);toast(msg);}
    });
  }
  @Override protected void onDestroy(){if(ble!=null)ble.disconnect("App geschlossen");super.onDestroy();}
}
