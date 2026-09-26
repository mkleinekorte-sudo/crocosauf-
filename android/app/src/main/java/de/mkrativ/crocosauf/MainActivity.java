package de.mkrativ.crocosauf;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.*;
import android.bluetooth.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.provider.Settings;
import android.text.InputFilter;
import android.text.InputType;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.util.*;

@SuppressLint("SetTextI18n")
public class MainActivity extends Activity implements BleClient.Listener {
    private static final int BG=Color.rgb(16,29,23),CARD=Color.rgb(29,48,37),INK=Color.rgb(238,246,239),MUTED=Color.rgb(176,197,181),LIME=Color.rgb(181,238,114),ORANGE=Color.rgb(255,199,120);
    private static final String[] MUSIC_KEYS={"g","go","w","r","nr"};
    private static final String[] MUSIC_LABELS={"Spielmusik","GameOver","Welcome","Reminder","Neue Runde"};
    private static final String[] EFFECT_KEYS={"ge","re","gd"};
    private static final String[] EFFECT_LABELS={"LEDs beim Spielen","LEDs beim Reminder","Display im Normalmodus"};
    private static final String[] EFFECT_NAMES={"Blue Scanner","Sparkle White","Green Pulse","Blue Pulse","Rainbow Train","Rainbow Fill","Grey Static","Double Sparkle","Mirror Yellow","Aqua Fill Up","Comet White","Rainbow Wave","Rainbow Breath","Color Trail","Cyan Dot","Confetti","Strobe White","Party Rainbow","Rainbow Glitter","Red/Blue Flip"};
    private static final String[] DISPLAY_NAMES={"Krokodil - Mund auf/zu","Smiley pulst","Smiley zwinkert","Smiley grinst","Herz schlägt","Confetti","Welle","Balken wandert","Checker Flash","Doppelpfeile rechts","Stern funkelt","Ring dreht","Bouncy Ball","Pacman frisst","Ausrufezeichen blinkt","LOL Face","Sunglasses Smiley","Question Mark","Fireworks","Mini-Cup blinkt"};
    private final Handler ui=new Handler(Looper.getMainLooper());
    private BleClient ble;
    private SharedPreferences preferences;
    private boolean approved=false,configured=false,visible=false,volumeDragging=false;
    private String tab="Musik",musicGroup="g",effectGroup="ge",chosenAddress="",chosenName="Crocosauf Deluxe";
    private TextView connectionLabel,statusLabel,messageLabel;
    private LinearLayout page,root;
    private Button connectButton,volumeMute;
    private SeekBar volumeBar;
    private TextView volumeText;
    private JSONObject config=new JSONObject(),status=new JSONObject();
    private final Map<String,Selection> selections=new HashMap<>();
    private final List<View> commandViews=new ArrayList<>();
    private final Map<String,BluetoothDevice> found=new LinkedHashMap<>();
    private LinearLayout deviceList;
    private AlertDialog devicesDialog;
    private Runnable afterPermission;
    private static final class Selection {int mask;boolean random;Selection(int mask,boolean random){this.mask=mask;this.random=random;}}

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        preferences=getSharedPreferences("crocosauf",MODE_PRIVATE);ble=new BleClient(this,this);
        if(savedInstanceState!=null)tab=savedInstanceState.getString("tab","Musik");
        getWindow().setStatusBarColor(BG);getWindow().setNavigationBarColor(BG);
        root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setBackgroundColor(BG);
        root.setOnApplyWindowInsetsListener((v,insets)->{
            if(Build.VERSION.SDK_INT>=30) {
                android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.ime());
                v.setPadding(bars.left,bars.top,bars.right,bars.bottom);
            } else v.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());
            return insets.consumeSystemWindowInsets();
        });
        setContentView(root);
        LinearLayout header=new LinearLayout(this);header.setOrientation(LinearLayout.HORIZONTAL);header.setGravity(Gravity.CENTER_VERTICAL);header.setPadding(dp(20),dp(16),dp(20),dp(8));
        TextView brand=text("CROCOSAUF",24,LIME,true);header.addView(brand,new LinearLayout.LayoutParams(0,-2,1));
        Button help=button("?",false,()->new AlertDialog.Builder(this).setTitle("Crocosauf per Bluetooth").setMessage("1. Firmware v6.4-BLE auf den ESP32 laden.\n2. Gerät einschalten oder durch Öffnen wecken.\n3. In der App verbinden.\n4. Maul schließen, Bereitschaft abwarten und Seitentaster 3 Sekunden halten.\n\nDie App wählt SD-Tracks aus. Sie überträgt keine Musikdateien und keinen Handyton.\n\nTests brauchen ein geschlossenes Maul. Beim Öffnen endet der Test und das Spiel startet.\n\nNormal/Pinchen bleibt am Schalter des Geräts. Beim Verlassen der App wird Bluetooth getrennt; das Spiel läuft selbstständig weiter.").setPositiveButton("Verstanden",null).show());
        header.addView(help,new LinearLayout.LayoutParams(dp(52),dp(48)));root.addView(header);
        LinearLayout connection=card();connectionLabel=text("Nicht verbunden",15,INK,true);connection.addView(connectionLabel);
        connectButton=button("Crocosauf verbinden",true,()->{
            if(ble.ready())ble.disconnect("Verbindung getrennt.");else requestBluetooth(this::connectionChooser);
        });connection.addView(connectButton);root.addView(connection);
        statusLabel=text("Dein Crocosauf, direkt per Bluetooth.",13,MUTED,false);statusLabel.setPadding(dp(20),dp(8),dp(20),dp(5));root.addView(statusLabel);
        messageLabel=text("",13,ORANGE,false);messageLabel.setPadding(dp(20),0,dp(20),dp(8));messageLabel.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);root.addView(messageLabel);
        LinearLayout nav=new LinearLayout(this);nav.setPadding(dp(12),0,dp(12),dp(4));
        for(String name:new String[]{"Musik","Effekte","System"}) {
            Button b=button(name,false,()->{tab=name;renderPage();});nav.addView(b,new LinearLayout.LayoutParams(0,dp(48),1));
        }
        root.addView(nav);
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);
        page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);page.setPadding(dp(8),0,dp(8),dp(24));scroll.addView(page);
        root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));renderPage();
    }
    @Override protected void onResume(){super.onResume();visible=true;ui.removeCallbacks(poll);ui.postDelayed(poll,1000);}
    @Override protected void onStop(){
        super.onStop();visible=false;ui.removeCallbacks(poll);
        if(devicesDialog!=null)devicesDialog.dismiss();
        ble.disconnect("App pausiert. Zum Bedienen erneut verbinden.");
    }
    @Override protected void onDestroy(){ui.removeCallbacksAndMessages(null);super.onDestroy();}
    @Override protected void onSaveInstanceState(Bundle state){state.putString("tab",tab);super.onSaveInstanceState(state);}
    private final Runnable poll=new Runnable(){public void run(){
        if(!visible)return;
        if(ble.ready() && !ble.busy()) {
            if(!approved)hello();
            else if(!configured)loadConfig();
            else readStatus();
        }
        ui.postDelayed(this,approved?2500:1200);
    }};
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private GradientDrawable background(int color,int radius){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(radius));return d;}
    private TextView text(String value,int size,int color,boolean bold){
        TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(color);t.setLineSpacing(dp(2),1);if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);return t;
    }
    private Button button(String value,boolean primary,Runnable action){
        Button b=new Button(this);b.setText(value);b.setAllCaps(false);b.setTextSize(14);b.setMinHeight(dp(48));
        b.setTextColor(primary?BG:INK);b.setBackgroundTintList(ColorStateList.valueOf(primary?LIME:CARD));
        b.setOnClickListener(v->action.run());return b;
    }
    private LinearLayout card(){
        LinearLayout box=new LinearLayout(this);box.setOrientation(LinearLayout.VERTICAL);box.setPadding(dp(16),dp(14),dp(16),dp(14));box.setBackground(background(CARD,16));
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.setMargins(dp(10),dp(7),dp(10),dp(7));box.setLayoutParams(p);return box;
    }
    private void hint(LinearLayout box,String value){TextView t=text(value,13,MUTED,false);t.setPadding(0,dp(7),0,dp(10));box.addView(t);}
    private void heading(LinearLayout box,String title){box.addView(text(title,19,INK,true));}
    private void guarded(View view){commandViews.add(view);view.setEnabled(approved&&configured);view.setAlpha(approved&&configured?1:.45f);}
    private void updateControls(){for(View v:commandViews){v.setEnabled(approved&&configured);v.setAlpha(approved&&configured?1:.45f);}}
    private void renderPage(){
        page.removeAllViews();commandViews.clear();volumeBar=null;volumeMute=null;volumeText=null;
        if(!configured){LinearLayout intro=card();heading(intro,"Verbinden. Auswählen. Spielen.");hint(intro,"Musik, Licht und Display - alle Einstellungen bleiben auf deinem Crocosauf gespeichert. Verbinde dich, um seine aktuellen Einstellungen zu laden.");page.addView(intro);}
        if(tab.equals("Musik"))renderSelection(false);
        else if(tab.equals("Effekte"))renderSelection(true);
        else renderSystem();
    }
    private int count(String key){if(key.equals("g")||key.equals("go"))return 12;if(key.equals("w")||key.equals("r"))return 6;if(key.equals("nr"))return 5;return 20;}
    private void renderSelection(boolean effects){
        String[] keys=effects?EFFECT_KEYS:MUSIC_KEYS,labels=effects?EFFECT_LABELS:MUSIC_LABELS;
        String key=effects?effectGroup:musicGroup;
        LinearLayout box=card();heading(box,effects?"Deine Effekte":"Deine Sounds");
        Spinner group=new Spinner(this);
        ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_item,labels);adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);group.setAdapter(adapter);
        int selected=Arrays.asList(keys).indexOf(key);group.setSelection(Math.max(selected,0));box.addView(group,new LinearLayout.LayoutParams(-1,dp(52)));
        group.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){
            public void onNothingSelected(AdapterView<?> parent){}
            public void onItemSelected(AdapterView<?> parent,View view,int position,long id){
                String next=keys[position],old=effects?effectGroup:musicGroup;
                if(!next.equals(old)){if(effects)effectGroup=next;else musicGroup=next;renderPage();}
            }
        });
        Selection selection=selections.get(key);
        if(selection==null){selection=new Selection(0,false);selections.put(key,selection);}
        final Selection draft=selection;
        Switch random=new Switch(this);random.setText("Zufall statt Reihenfolge");random.setTextColor(INK);random.setTextSize(15);random.setMinHeight(dp(48));random.setChecked(draft.random);random.setOnCheckedChangeListener((v,on)->draft.random=on);box.addView(random);guarded(random);
        hint(box,effects?"Häkchen aktivieren Effekte. Im Pinchenspiel zeigt das Display die Rundennummer.":"Häkchen wählen die erlaubten SD-Tracks. „Test“ spielt einmal ab. Die Titel entsprechen den Dateinummern auf deiner Karte.");
        for(int i=0;i<count(key);i++){
            final int index=i;String title=effects?(key.equals("gd")?DISPLAY_NAMES[i]:EFFECT_NAMES[i]):String.format(Locale.GERMANY,"Track %03d",i+1);
            LinearLayout row=new LinearLayout(this);row.setGravity(Gravity.CENTER_VERTICAL);
            CheckBox check=new CheckBox(this);check.setText(title);check.setTextColor(INK);check.setTextSize(14);check.setMinHeight(dp(50));check.setChecked((draft.mask&(1<<i))!=0);
            check.setOnCheckedChangeListener((v,on)->{if(on)draft.mask|=1<<index;else draft.mask&=~(1<<index);});
            row.addView(check,new LinearLayout.LayoutParams(0,-2,1));guarded(check);
            String route=effects?(key.equals("gd")?"/testDisp?id=":"/testEffect?id=")+index:"/play?src="+key+"&t="+(i+1);
            Button test=button("Test",false,()->command(route,null));test.setContentDescription(title+" testen oder Test stoppen");row.addView(test,new LinearLayout.LayoutParams(dp(72),dp(48)));guarded(test);box.addView(row);
        }
        Button save=button("Auswahl speichern",true,()->{
            if(draft.mask==0){onMessage("Bitte mindestens einen Eintrag aktivieren.");return;}
            StringBuilder request=new StringBuilder("/save?group=").append(key).append("&mode=").append(draft.random?"rnd":"seq");
            for(int i=0;i<count(key);i++)if((draft.mask&(1<<i))!=0)request.append("&m").append(i).append("=1");
            command(request.toString(),null);
        });box.addView(save);guarded(save);
        Button stop=button("Tests / Reminder stoppen",false,()->command("/stop",null));box.addView(stop);guarded(stop);
        page.addView(box);
    }
    private EditText input(String value,int type){
        EditText e=new EditText(this);e.setText(value);e.setTextColor(INK);e.setTextSize(16);e.setSingleLine(true);e.setInputType(type);e.setMinHeight(dp(50));return e;
    }
    private void renderSystem(){
        LinearLayout volume=card();heading(volume,"Lautstärke");
        volumeText=text("Pegel "+status.optInt("vol",20)+" / 30",15,INK,false);volume.addView(volumeText);
        volumeBar=new SeekBar(this);volumeBar.setMax(25);volumeBar.setProgress(status.optInt("vol",20)-5);volumeBar.setContentDescription("Lautstärke 5 bis 30");volume.addView(volumeBar);guarded(volumeBar);
        volumeBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar bar,int progress,boolean fromUser){volumeText.setText("Pegel "+(progress+5)+" / 30");}
            public void onStartTrackingTouch(SeekBar bar){volumeDragging=true;}
            public void onStopTrackingTouch(SeekBar bar){volumeDragging=false;command("/volume?v="+(bar.getProgress()+5),null);}
        });
        volumeMute=button(status.optBoolean("muted")?"Ton einschalten":"Stummschalten",true,()->command("/muteToggle",null));volume.addView(volumeMute);guarded(volumeMute);page.addView(volume);
        LinearLayout reminder=card();heading(reminder,"Reminder");
        Switch enabled=new Switch(this);enabled.setText("Erinnerung aktiv");enabled.setTextColor(INK);enabled.setMinHeight(dp(48));enabled.setChecked(config.optBoolean("remEn",true));reminder.addView(enabled);guarded(enabled);
        Switch sound=new Switch(this);sound.setText("Mit Sound");sound.setTextColor(INK);sound.setMinHeight(dp(48));sound.setChecked(config.optBoolean("remSnd",true));reminder.addView(sound);guarded(sound);
        hint(reminder,"Intervall in Minuten (1–60)");EditText minutes=input(""+config.optInt("remMin",8),InputType.TYPE_CLASS_NUMBER);minutes.setContentDescription("Reminder-Intervall in Minuten");reminder.addView(minutes);guarded(minutes);
        hint(reminder,"Dauer in Millisekunden (1500–9000)");EditText duration=input(""+config.optInt("remDur",3500),InputType.TYPE_CLASS_NUMBER);duration.setContentDescription("Reminder-Dauer in Millisekunden");reminder.addView(duration);guarded(duration);
        Button save=button("Reminder speichern",true,()->{
            int min=parseNumber(minutes),dur=parseNumber(duration);
            if(min<1||min>60||dur<1500||dur>9000){onMessage("Bitte 1–60 Minuten und 1500–9000 ms eintragen.");return;}
            String route="/saveReminderCfg?rmin="+min+"&rdur="+dur+(enabled.isChecked()?"&ren=1":"")+(sound.isChecked()?"&rsnd=1":"");
            command(route,()->{try{config.put("remEn",enabled.isChecked());config.put("remSnd",sound.isChecked());config.put("remMin",min);config.put("remDur",dur);}catch(JSONException ignored){}});
        });reminder.addView(save);guarded(save);
        Button test=button("Reminder testen",false,()->command("/testReminder",null));reminder.addView(test);guarded(test);page.addView(reminder);
        LinearLayout scroll=card();heading(scroll,"Laufschrift");hint(scroll,"Bis zu 40 darstellbare Zeichen. Umlaute werden ausgeschrieben.");
        EditText scrollText=input(config.optString("standby","CROCOSAUF DELUXE"),InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);scrollText.setContentDescription("Text für die Laufschrift");scrollText.setFilters(new InputFilter[]{new InputFilter.LengthFilter(40)});scroll.addView(scrollText);guarded(scrollText);
        Button saveText=button("Text speichern",true,()->command("/saveText?txt="+WireProtocol.encode(scrollText.getText().toString()),()->{
            ble.send("/config",reply->{if(reply.ok())try{JSONObject latest=new JSONObject(reply.body);config.put("standby",latest.getString("standby"));scrollText.setText(config.optString("standby"));}catch(JSONException ex){onMessage("Gespeicherten Text bitte erneut laden.");}});
        }));scroll.addView(saveText);guarded(saveText);page.addView(scroll);
        LinearLayout reset=card();heading(reset,"Gerät");hint(reset,"Normal / Pinchen wählst du weiterhin am Schalter. Werkseinstellungen setzen Auswahl, Lautstärke, Text, Reminder und Rundenzähler zurück.");
        Button factory=button("Werkseinstellungen …",false,()->new AlertDialog.Builder(this).setTitle("Alles zurücksetzen?").setMessage("Die Einstellungen und der Rundenzähler werden zurückgesetzt. Deine SD-Dateien bleiben erhalten.").setNegativeButton("Abbrechen",null).setPositiveButton("Zurücksetzen",(d,w)->command("/factoryReset?confirm=yes",()->{configured=false;loadConfig();})).show());reset.addView(factory);guarded(factory);
        Button refresh=button("Einstellungen neu laden",false,()->new AlertDialog.Builder(this).setTitle("Neu laden?").setMessage("Ungespeicherte Änderungen in der App werden verworfen.").setNegativeButton("Abbrechen",null).setPositiveButton("Neu laden",(d,w)->{configured=false;loadConfig();}).show());reset.addView(refresh);guarded(refresh);
        hint(reset,"App 1.0-beta · Firmware v6.4-BLE\nVerbindung nur zur Steuerung. Keine Cloud, kein Konto, kein Musikstreaming.");page.addView(reset);
    }
    private int parseNumber(EditText view){try{return Integer.parseInt(view.getText().toString().trim());}catch(NumberFormatException e){return -1;}}
    private void command(String route,Runnable after){
        if(!approved||!configured){onMessage("Bitte erst verbinden und am Gerät freigeben.");return;}
        onMessage("Wird übertragen …");
        ble.send(route,reply->{
            if(reply.ok()){onMessage(reply.body);if(after!=null)after.run();readStatus();}
            else {onMessage(reply.body);if(reply.code==403){approved=false;configured=false;updateControls();}}
        });
    }
    private void hello(){
        ble.send("/hello",reply->{
            try {
                JSONObject value=new JSONObject(reply.body);
                if(!reply.ok()||!value.optString("product").equals("crocosauf")||value.optInt("protocol")!=1){ble.disconnect("Diese Firmware passt nicht zur App.");return;}
                approved=value.optBoolean("approved");
                connectionLabel.setText(approved?"Bluetooth verbunden":"Verbunden · Freigabe am Gerät nötig");
                connectButton.setText("Verbindung trennen");
                if(approved){onMessage("Verbunden. Einstellungen werden geladen …");loadConfig();}
                else onMessage("Maul schließen, Bereitschaft abwarten, Seitentaster 3 Sekunden halten. Auf dem Display erscheint APP OK.");
            }catch(JSONException ex){ble.disconnect("Crocosauf antwortet mit einem unbekannten Format.");}
        });
    }
    private void loadConfig(){
        ble.send("/config",reply->{
            if(!reply.ok()){onMessage(reply.body);return;}
            try {
                JSONObject incoming=new JSONObject(reply.body);JSONObject masks=incoming.getJSONObject("masks"),random=incoming.getJSONObject("random");
                Map<String,Selection> loaded=new HashMap<>();
                for(String key:new String[]{"g","go","w","r","nr","ge","re","gd"})loaded.put(key,new Selection(masks.getInt(key),random.getBoolean(key)));
                config=incoming;selections.clear();selections.putAll(loaded);configured=true;
                preferences.edit().putString("lastAddress",chosenAddress).putString("lastName",chosenName).apply();
                renderPage();onMessage("Einstellungen geladen.");readStatus();
            }catch(JSONException ex){ble.disconnect("Einstellungen unvollständig. Firmware prüfen.");}
        });
    }
    private void readStatus(){
        if(!approved||!ble.ready())return;
        ble.send("/status",reply->{
            if(!reply.ok()){onMessage(reply.body);return;}
            try {
                status=new JSONObject(reply.body);
                statusLabel.setText((status.optBoolean("pinchen")?"Pinchen · Runde "+status.optInt("round"):"Normalmodus")+"  |  "+status.optString("state")+"\nMaul "+(status.optBoolean("open")?"offen":"geschlossen")+" · Pegel "+status.optInt("vol")+(status.optBoolean("muted")?" · stumm":""));
                if(status.optString("error").length()>0)onMessage(status.optString("error"));
                if(volumeBar!=null&&!volumeDragging)volumeBar.setProgress(Math.max(0,status.optInt("vol",20)-5));
                if(volumeMute!=null)volumeMute.setText(status.optBoolean("muted")?"Ton einschalten":"Stummschalten");
            }catch(JSONException ex){onMessage("Status konnte nicht gelesen werden.");}
        });
    }
    @SuppressLint("MissingPermission")
    private void requestBluetooth(Runnable next){
        if(ble.adapter()==null){onMessage("Dieses Gerät hat kein Bluetooth.");return;}
        String[] permissions=Build.VERSION.SDK_INT>=31?new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT}:new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
        List<String> missing=new ArrayList<>();for(String permission:permissions)if(checkSelfPermission(permission)!=PackageManager.PERMISSION_GRANTED)missing.add(permission);
        afterPermission=next;
        if(!missing.isEmpty()){requestPermissions(missing.toArray(new String[0]),71);return;}
        if(!ble.adapter().isEnabled()){startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE),72);return;}
        afterPermission=null;next.run();
    }
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] results){
        super.onRequestPermissionsResult(request,permissions,results);
        if(request!=71)return;
        boolean all=results.length>0;for(int result:results)all&=result==PackageManager.PERMISSION_GRANTED;
        if(all&&afterPermission!=null)requestBluetooth(afterPermission);
        else onMessage("Für die Verbindung bitte die Berechtigung „Geräte in der Nähe“ erlauben (Android 8–11: Standort).");
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);
        if(request==72&&result==RESULT_OK&&afterPermission!=null)requestBluetooth(afterPermission);
        else if(request==72)onMessage("Bluetooth wurde nicht eingeschaltet.");
    }
    @SuppressLint("MissingPermission")
    private void connectionChooser(){
        approved=false;configured=false;updateControls();found.clear();
        LinearLayout box=new LinearLayout(this);box.setOrientation(LinearLayout.VERTICAL);box.setPadding(dp(18),dp(8),dp(18),dp(12));
        hint(box,"Crocosauf einschalten oder das Maul zum Aufwecken öffnen. Danach das Maul wieder schließen.");
        String last=preferences.getString("lastAddress","");
        if(BluetoothAdapter.checkBluetoothAddress(last))box.addView(button("Letztes Crocosauf verbinden",true,()->selectDevice(ble.adapter().getRemoteDevice(last))));
        deviceList=new LinearLayout(this);deviceList.setOrientation(LinearLayout.VERTICAL);box.addView(deviceList);
        devicesDialog=new AlertDialog.Builder(this).setTitle("Crocosauf suchen").setView(box).setNegativeButton("Abbrechen",null).create();
        devicesDialog.setOnDismissListener(d->ble.stopScan());devicesDialog.show();ble.scan();onMessage("Suche nach Crocosauf …");
    }
    @SuppressLint("MissingPermission")
    private void selectDevice(BluetoothDevice device){
        chosenAddress=device.getAddress();String name=device.getName();chosenName=name==null?"Crocosauf Deluxe":name;
        if(devicesDialog!=null)devicesDialog.dismiss();ble.connect(device);
    }
    @Override public void onMessage(String message){messageLabel.setText(message);}
    @Override @SuppressLint("MissingPermission") public void onDevice(BluetoothDevice device,int rssi){
        if(found.containsKey(device.getAddress()))return;
        found.put(device.getAddress(),device);
        String name=device.getName()==null?"Crocosauf Deluxe":device.getName();
        Button deviceButton=button(name+"\n"+device.getAddress(),false,()->selectDevice(device));
        if(deviceList!=null)deviceList.addView(deviceButton);
    }
    @Override public void onScanFinished(){onMessage(found.isEmpty()?"Nichts gefunden. Firmware v6.4-BLE prüfen und Gerät wecken. Unter Android 8–11 muss auch Standort eingeschaltet sein.":"Gerät in der Liste auswählen.");}
    @Override public void onTransportReady(){hello();}
    @Override public void onDisconnected(String reason){
        approved=false;configured=false;connectionLabel.setText("Nicht verbunden");connectButton.setText("Crocosauf verbinden");
        statusLabel.setText("Keine aktuellen Gerätedaten.");updateControls();onMessage(reason);
    }
}
