package de.mkrativ.butcher;
import android.annotation.SuppressLint;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Context;
import android.os.*;
import java.util.*;

@SuppressLint("MissingPermission")
public final class BleClient {
  public interface Listener {void onDevice(BluetoothDevice d,int rssi);void onReady();void onDisconnected(String reason);void onMessage(String text);}
  public interface Result {void done(WireProtocol.Reply reply);}
  private static final UUID CCCD=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
  private final Context context;private final Listener listener;private final Handler main=new Handler(Looper.getMainLooper());
  private final BluetoothAdapter adapter;private BluetoothGatt gatt;private BluetoothGattCharacteristic rx;private boolean scanning=false,ready=false;
  private final WireProtocol.Decoder decoder=new WireProtocol.Decoder();private final ArrayDeque<Command> queue=new ArrayDeque<>();
  private Command active;private int nextId=1,offset=0;private boolean writing=false;private WireProtocol.Reply early;
  private final Runnable timeout=()->disconnect("Bluetooth-Antwort ausgeblieben. Bitte neu verbinden.");
  private final Runnable scanTimeout=()->stopScan();
  private final Runnable connectTimeout=()->disconnect("Verbindung dauert zu lange.");
  private static final class Command {int id;byte[] bytes;Result result;Command(int i,byte[] b,Result r){id=i;bytes=b;result=r;}}
  public BleClient(Context c,Listener l){context=c;listener=l;BluetoothManager m=(BluetoothManager)c.getSystemService(Context.BLUETOOTH_SERVICE);adapter=m==null?null:m.getAdapter();}
  public BluetoothAdapter adapter(){return adapter;}public boolean ready(){return ready;}
  private final ScanCallback scanCallback=new ScanCallback(){@Override public void onScanResult(int t,ScanResult r){main.post(()->{if(scanning)listener.onDevice(r.getDevice(),r.getRssi());});}
    @Override public void onScanFailed(int code){main.post(()->{stopScan();listener.onMessage("Suche fehlgeschlagen: "+code);});}};
  public void scan(){disconnectQuiet();if(adapter==null||!adapter.isEnabled()){listener.onMessage("Bluetooth einschalten.");return;}
    BluetoothLeScanner s=adapter.getBluetoothLeScanner();if(s==null){listener.onMessage("Bluetooth-Suche nicht verfügbar.");return;}
    scanning=true;s.startScan(Collections.singletonList(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(WireProtocol.SERVICE)).build()),
      new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scanCallback);main.postDelayed(scanTimeout,12000);
  }
  public void stopScan(){main.removeCallbacks(scanTimeout);if(scanning&&adapter!=null&&adapter.getBluetoothLeScanner()!=null)try{adapter.getBluetoothLeScanner().stopScan(scanCallback);}catch(SecurityException ignored){}scanning=false;}
  public void connect(BluetoothDevice device){disconnectQuiet();listener.onMessage("Verbinde mit The Butcher …");gatt=device.connectGatt(context,false,callbacks,BluetoothDevice.TRANSPORT_LE);main.postDelayed(connectTimeout,20000);}
  private final BluetoothGattCallback callbacks=new BluetoothGattCallback(){
    @Override public void onConnectionStateChange(BluetoothGatt link,int code,int state){main.post(()->{if(link!=gatt)return;
      if(code!=0||state==BluetoothProfile.STATE_DISCONNECTED){disconnect("Verbindung getrennt ("+code+").");return;}
      if(state==BluetoothProfile.STATE_CONNECTED&&!link.discoverServices())disconnect("BLE-Dienste fehlen.");});}
    @Override public void onServicesDiscovered(BluetoothGatt link,int code){main.post(()->{if(link!=gatt)return;
      BluetoothGattService svc=link.getService(WireProtocol.SERVICE);if(code!=0||svc==null){disconnect("Butcher-BLE-Sketch fehlt.");return;}
      rx=svc.getCharacteristic(WireProtocol.RX);BluetoothGattCharacteristic tx=svc.getCharacteristic(WireProtocol.TX);
      BluetoothGattDescriptor c=tx==null?null:tx.getDescriptor(CCCD);if(rx==null||c==null||!link.setCharacteristicNotification(tx,true)){disconnect("BLE-Kanäle fehlen.");return;}
      boolean ok;if(Build.VERSION.SDK_INT>=33)ok=link.writeDescriptor(c,BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)==BluetoothStatusCodes.SUCCESS;
      else{c.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);ok=link.writeDescriptor(c);}if(!ok)disconnect("Antwortkanal nicht aktiv.");});}
    @Override public void onDescriptorWrite(BluetoothGatt link,BluetoothGattDescriptor c,int code){main.post(()->{if(link!=gatt)return;if(code!=0){disconnect("Antwortkanal abgewiesen.");return;}
      main.removeCallbacks(connectTimeout);ready=true;listener.onReady();});}
    @Override public void onCharacteristicWrite(BluetoothGatt link,BluetoothGattCharacteristic c,int code){main.post(()->{if(link!=gatt||active==null)return;
      if(code!=0){disconnect("Schreibfehler "+code);return;}writing=false;offset+=Math.min(20,active.bytes.length-offset);
      if(offset<active.bytes.length)writeNext();else if(early!=null)finish(early);});}
    @Override public void onCharacteristicChanged(BluetoothGatt link,BluetoothGattCharacteristic c,byte[] data){if(WireProtocol.TX.equals(c.getUuid())){byte[] copy=data.clone();main.post(()->receive(link,copy));}}
    @Override public void onCharacteristicChanged(BluetoothGatt link,BluetoothGattCharacteristic c){if(Build.VERSION.SDK_INT<33&&WireProtocol.TX.equals(c.getUuid())){
      byte[] data=c.getValue();if(data!=null){byte[] copy=data.clone();main.post(()->receive(link,copy));}}}
  };
  private void receive(BluetoothGatt link,byte[] bytes){if(link!=gatt||!ready)return;try{for(WireProtocol.Reply r:decoder.feed(bytes)){
      if(active==null||r.id!=active.id){disconnect("Antwort passt nicht zum Befehl.");return;}
      if(writing||offset<active.bytes.length)early=r;else finish(r);
    }}catch(Exception ex){disconnect("Bluetooth-Antwort beschädigt.");}}
  public boolean send(String path,String body,Result cb){if(!ready){listener.onMessage("Bitte erst verbinden.");return false;}
    if(queue.size()>=8){listener.onMessage("Bitte warten.");return false;}
    int id=nextId++;if(nextId>99999999)nextId=1;
    try{queue.add(new Command(id,WireProtocol.command(id,path,body==null?"":body),cb));}catch(Exception e){listener.onMessage(e.getMessage());return false;}
    startNext();return true;
  }
  private void startNext(){if(active!=null||!ready)return;active=queue.poll();if(active==null)return;offset=0;early=null;main.postDelayed(timeout,90000);writeNext();}
  private void writeNext(){if(active==null||gatt==null)return;byte[] bytes=Arrays.copyOfRange(active.bytes,offset,Math.min(offset+20,active.bytes.length));writing=true;
    boolean ok;if(Build.VERSION.SDK_INT>=33)ok=gatt.writeCharacteristic(rx,bytes,BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)==BluetoothStatusCodes.SUCCESS;
    else{rx.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);rx.setValue(bytes);ok=gatt.writeCharacteristic(rx);}if(!ok)disconnect("Senden fehlgeschlagen.");}
  private void finish(WireProtocol.Reply r){main.removeCallbacks(timeout);Command done=active;active=null;early=null;writing=false;if(done!=null)done.result.done(r);startNext();}
  public void disconnect(String reason){disconnectQuiet();listener.onDisconnected(reason);}
  private void disconnectQuiet(){stopScan();main.removeCallbacks(timeout);main.removeCallbacks(connectTimeout);ready=false;active=null;queue.clear();decoder.reset();early=null;writing=false;rx=null;
    BluetoothGatt old=gatt;gatt=null;if(old!=null)try{old.disconnect();old.close();}catch(SecurityException ignored){}
  }
}
