package de.mkrativ.crocosauf;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.widget.*;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

/** Real Android UI smoke test. No BLE hardware is simulated as a successful link. */
public final class SettingsInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    private void require(boolean value,String message){if(!value)throw new AssertionError(message);}
    private void set(Object target,String name,Object value)throws Exception{Field f=MainActivity.class.getDeclaredField(name);f.setAccessible(true);f.set(target,value);}
    private void render(MainActivity activity)throws Exception{Method m=MainActivity.class.getDeclaredMethod("renderPage");m.setAccessible(true);m.invoke(activity);}
    private <T> List<T> views(View root,Class<T> type){List<T> result=new ArrayList<>();if(type.isInstance(root))result.add(type.cast(root));if(root instanceof ViewGroup){ViewGroup group=(ViewGroup)root;for(int i=0;i<group.getChildCount();i++)result.addAll(views(group.getChildAt(i),type));}return result;}
    private boolean text(View root,String value){for(TextView v:views(root,TextView.class))if(v.getText().toString().contains(value))return true;return false;}
    private void screenshot(String name)throws Exception{
        waitForIdleSync();android.os.SystemClock.sleep(400);waitForIdleSync();
        Bitmap bitmap=getUiAutomation().takeScreenshot();require(bitmap!=null,"Screenshot fehlt");
        File dir=new File(getTargetContext().getExternalFilesDir(null),"screenshots");require(dir.exists()||dir.mkdirs(),"Screenshot-Ordner fehlt");
        try(FileOutputStream out=new FileOutputStream(new File(dir,name+".png"))){require(bitmap.compress(Bitmap.CompressFormat.PNG,100,out),"Screenshot fehlgeschlagen");}bitmap.recycle();
    }
    @Override public void onStart(){
        Bundle result=new Bundle();
        try{
            Intent launch=new Intent(getTargetContext(),MainActivity.class);launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            MainActivity activity=(MainActivity)startActivitySync(launch);waitForIdleSync();
            runOnMainSync(()->{
                try{
                    JSONObject config=new JSONObject("{\"features\":{\"visualSettings\":true,\"sleepSettings\":true,\"audioSettings\":true,\"diagnostics\":true},\"numLeds\":40,\"ledBrightness\":42,\"displayBrightness\":73,\"rotation\":2,\"sleepMinutes\":0,\"audioEnabled\":true,\"startVolume\":20,\"welcomeVolume\":12,\"remEn\":true,\"remSnd\":true,\"remMin\":8,\"remDur\":3500,\"remLed\":3500}");
                    set(activity,"config",config);set(activity,"approved",true);set(activity,"configured",true);set(activity,"tab","System");
                    set(activity,"status",new JSONObject("{\"version\":\"v6.6-BLE-40LED-APPONLY\",\"uptimeSec\":7260,\"matrixOk\":true,\"watchdogOk\":true,\"resetReason\":1,\"vol\":20}"));render(activity);
                    View root=activity.getWindow().getDecorView();
                    require(text(root,"Licht & Display"),"Lichtkarte fehlt");require(text(root,"42 %"),"LED-Prozent falsch");require(text(root,"73 %"),"Display-Prozent falsch");
                    require(text(root,"Audio beim Einschalten"),"Audiokarte fehlt");require(text(root,"Automatischer Schlaf"),"Schlafkarte fehlt");require(text(root,"40 LEDs"),"Falsches Gerät");
                    List<Spinner> spinners=views(root,Spinner.class);require(spinners.size()==2,"Drehung und Schlafauswahl fehlen");require(spinners.get(0).getSelectedItemPosition()==2,"Drehung nicht geladen");
                    for(Button b:views(root,Button.class))if(b.getText().toString().equals("Licht & Display speichern"))require(b.isEnabled(),"Speichern blockiert");
                    List<SeekBar> sliders=views(root,SeekBar.class);require(sliders.size()==5,"Helligkeiten, Pegel, Start und Welcome erwartet");
                    require(sliders.get(0).getMax()==100&&sliders.get(1).getMax()==100,"Prozentwerte falsch");
                }catch(Exception e){throw new RuntimeException(e);}
            });
            screenshot("01-light-display");
            runOnMainSync(()->views(activity.getWindow().getDecorView(),ScrollView.class).get(0).scrollTo(0,900));screenshot("02-audio-reminder");
            runOnMainSync(()->views(activity.getWindow().getDecorView(),ScrollView.class).get(0).fullScroll(View.FOCUS_DOWN));screenshot("03-device-status");
            runOnMainSync(()->{try{
                // Older 40-LED firmware must offer its existing controls but no unsupported sleep command.
                JSONObject config=new JSONObject("{\"rotation\":1,\"ledBrightness\":50,\"displayBrightness\":50,\"audioEnabled\":true,\"welcomeVolume\":20}");
                set(activity,"config",config);render(activity);View root=activity.getWindow().getDecorView();
                require(text(root,"Licht & Display speichern"),"Alte 40-LED-Firmware nicht unterstützt");
                require(!text(root,"Schlafeinstellung speichern"),"Nicht unterstützter Befehl sichtbar");
            }catch(Exception e){throw new RuntimeException(e);}});
            result.putString("stream","CROCOSAUF_UI_PASS: 40 LED settings, ranges, migration, diagnostics\n");finish(Activity.RESULT_OK,result);
        }catch(Throwable failure){result.putString("stream","CROCOSAUF_UI_FAIL: "+failure+"\n");finish(Activity.RESULT_CANCELED,result);}
    }
}
