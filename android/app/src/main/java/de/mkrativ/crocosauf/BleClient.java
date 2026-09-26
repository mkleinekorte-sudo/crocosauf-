package de.mkrativ.crocosauf;

import android.annotation.SuppressLint;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Context;
import android.os.*;
import java.util.*;

/** One foreground BLE link, one GATT write at a time, no automatic command replay. */
@SuppressLint("MissingPermission") // Activity gates every entry with runtime permissions.
public final class BleClient {
    public interface Listener {
        void onMessage(String message);
        void onDevice(BluetoothDevice device, int rssi);
        void onTransportReady();
        void onDisconnected(String reason);
        void onScanFinished();
    }
    public interface Result { void done(WireProtocol.Reply reply); }
    private static final UUID SERVICE=UUID.fromString(WireProtocol.SERVICE),RX=UUID.fromString(WireProtocol.RX),TX=UUID.fromString(WireProtocol.TX);
    private static final UUID CCCD=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private final Context context; private final Listener listener;
    private final Handler main=new Handler(Looper.getMainLooper());
    private final BluetoothAdapter adapter;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rx;
    private boolean ready=false,scanning=false;
    private final ArrayDeque<Command> commands=new ArrayDeque<>();
    private Command active;
    private List<byte[]> packets; private int packetIndex;
    private boolean writing=false;
    private WireProtocol.Reply earlyReply;
    private final WireProtocol.Decoder decoder=new WireProtocol.Decoder();
    private int nextId=1;
    private final Runnable timeout=()->disconnect("Keine Antwort. Verbindung getrennt; der letzte Befehl wird nicht wiederholt.");
    private final Runnable connectTimeout=()->disconnect("Verbindung dauert zu lange. Gerät wecken und erneut verbinden.");
    private final Runnable scanTimeout=()->{stopScan();listener.onScanFinished();};
    private static final class Command {
        final int id; final byte[] bytes; final Result callback;
        Command(int id,byte[] bytes,Result callback){this.id=id;this.bytes=bytes;this.callback=callback;}
    }
    public BleClient(Context context,Listener listener) {
        this.context=context;this.listener=listener;
        BluetoothManager manager=(BluetoothManager)context.getSystemService(Context.BLUETOOTH_SERVICE);
        adapter=manager==null?null:manager.getAdapter();
    }
    public BluetoothAdapter adapter(){return adapter;}
    public boolean ready(){return ready;}
    public boolean busy(){return active!=null || !commands.isEmpty();}
    public void scan() {
        disconnectQuietly();
        if(adapter==null || !adapter.isEnabled()){listener.onMessage("Bitte Bluetooth einschalten.");return;}
        try {
            BluetoothLeScanner scanner=adapter.getBluetoothLeScanner();
            if(scanner==null){listener.onMessage("Bluetooth-Suche nicht verfügbar.");return;}
            scanning=true;
            ScanFilter filter=new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE)).build();
            scanner.startScan(Collections.singletonList(filter),new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scanCallback);
            main.postDelayed(scanTimeout,12000);
        } catch(SecurityException|IllegalStateException ex){scanning=false;listener.onMessage("Bluetooth-Berechtigung prüfen.");}
    }
    private final ScanCallback scanCallback=new ScanCallback() {
        @Override public void onScanResult(int type,ScanResult result) {
            main.post(()->{if(scanning)listener.onDevice(result.getDevice(),result.getRssi());});
        }
        @Override public void onScanFailed(int errorCode) {
            main.post(()->{stopScan();listener.onMessage("Suche fehlgeschlagen ("+errorCode+"). Bitte kurz warten und erneut suchen.");});
        }
    };
    public void stopScan() {
        main.removeCallbacks(scanTimeout);
        if(scanning) {
            try { if(adapter!=null && adapter.getBluetoothLeScanner()!=null)adapter.getBluetoothLeScanner().stopScan(scanCallback); }
            catch(SecurityException|IllegalStateException ignored){}
        }
        scanning=false;
    }
    public void connect(BluetoothDevice device) {
        disconnectQuietly(); listener.onMessage("Verbinde mit Crocosauf …");
        try {
            gatt=device.connectGatt(context,false,callbacks,BluetoothDevice.TRANSPORT_LE);
            if(gatt==null){disconnect("Verbindung konnte nicht gestartet werden.");return;}
            main.postDelayed(connectTimeout,20000);
        } catch(SecurityException|IllegalArgumentException ex){disconnect("Bluetooth-Zugriff nicht möglich.");}
    }
    private final BluetoothGattCallback callbacks=new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt link,int status,int state) {
            main.post(()->{
                if(link!=gatt) return;
                if(status!=BluetoothGatt.GATT_SUCCESS || state==BluetoothProfile.STATE_DISCONNECTED) {
                    disconnect("Verbindung getrennt"+(status==0?".":" (Bluetooth "+status+")."));return;
                }
                if(state==BluetoothProfile.STATE_CONNECTED && !link.discoverServices()) disconnect("Dienste konnten nicht gelesen werden.");
            });
        }
        @Override public void onServicesDiscovered(BluetoothGatt link,int status) {
            main.post(()->{
                if(link!=gatt)return;
                BluetoothGattService service=link.getService(SERVICE);
                if(status!=0 || service==null){disconnect("Passender Crocosauf-Sketch v6.4-BLE fehlt.");return;}
                rx=service.getCharacteristic(RX);
                BluetoothGattCharacteristic tx=service.getCharacteristic(TX);
                BluetoothGattDescriptor descriptor=tx==null?null:tx.getDescriptor(CCCD);
                if(rx==null || tx==null || descriptor==null || !link.setCharacteristicNotification(tx,true)) {
                    disconnect("Bluetooth-Schnittstelle unvollständig.");return;
                }
                boolean success;
                if(Build.VERSION.SDK_INT>=33) success=link.writeDescriptor(descriptor,BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)==BluetoothStatusCodes.SUCCESS;
                else {descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);success=link.writeDescriptor(descriptor);}
                if(!success) disconnect("Antwortkanal konnte nicht aktiviert werden.");
            });
        }
        @Override public void onDescriptorWrite(BluetoothGatt link,BluetoothGattDescriptor descriptor,int status) {
            main.post(()->{
                if(link!=gatt || !CCCD.equals(descriptor.getUuid())) return;
                if(status!=0){disconnect("Antwortkanal wurde abgewiesen.");return;}
                main.removeCallbacks(connectTimeout);ready=true;listener.onTransportReady();
            });
        }
        @Override public void onCharacteristicWrite(BluetoothGatt link,BluetoothGattCharacteristic characteristic,int status) {
            main.post(()->{
                if(link!=gatt || !RX.equals(characteristic.getUuid()) || active==null)return;
                if(status!=0){disconnect("Befehl konnte nicht übertragen werden ("+status+").");return;}
                writing=false;packetIndex++;
                if(packetIndex<packets.size()) writeNextPacket();
                else if(earlyReply!=null) finish(earlyReply);
            });
        }
        @Override public void onCharacteristicChanged(BluetoothGatt link,BluetoothGattCharacteristic characteristic,byte[] value) {
            if(TX.equals(characteristic.getUuid())) {byte[] copy=value.clone();main.post(()->receive(link,copy));}
        }
        @Override public void onCharacteristicChanged(BluetoothGatt link,BluetoothGattCharacteristic characteristic) {
            if(Build.VERSION.SDK_INT<33 && TX.equals(characteristic.getUuid())) {
                byte[] value=characteristic.getValue();
                if(value!=null){byte[] copy=value.clone();main.post(()->receive(link,copy));}
            }
        }
    };
    private void receive(BluetoothGatt link,byte[] value) {
        if(link!=gatt || !ready)return;
        try {
            for(WireProtocol.Reply reply:decoder.feed(value)) {
                if(active==null || reply.id!=active.id) {disconnect("Antwort passt nicht zum Befehl. Bitte neu verbinden.");return;}
                // Some ESP32 stacks notify before the final GATT write callback.
                if(writing || packetIndex<packets.size()) earlyReply=reply;
                else finish(reply);
            }
        } catch(IllegalArgumentException ex){disconnect("Bluetooth-Antwort unvollständig. Bitte neu verbinden.");}
    }
    public boolean send(String route,Result result) {
        if(!ready){listener.onMessage("Bitte zuerst verbinden und am Gerät freigeben.");return false;}
        if(commands.size()>=8){listener.onMessage("Bitte kurz warten, Befehle werden verarbeitet.");return false;}
        int id=nextId++;if(nextId>99999999)nextId=1;
        try {commands.add(new Command(id,WireProtocol.request(id,route),result));}
        catch(IllegalArgumentException ex){listener.onMessage(ex.getMessage());return false;}
        startNext();return true;
    }
    private void startNext() {
        if(active!=null || !ready)return;
        active=commands.poll();if(active==null)return;
        packets=WireProtocol.chunks(active.bytes,20);packetIndex=0;earlyReply=null;
        main.postDelayed(timeout,12000);writeNextPacket();
    }
    private void writeNextPacket() {
        if(gatt==null || rx==null || active==null)return;
        byte[] value=packets.get(packetIndex);writing=true;
        boolean success;
        try {
            if(Build.VERSION.SDK_INT>=33) success=gatt.writeCharacteristic(rx,value,BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)==BluetoothStatusCodes.SUCCESS;
            else {rx.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);rx.setValue(value);success=gatt.writeCharacteristic(rx);}
        } catch(SecurityException|IllegalStateException ex){success=false;}
        if(!success)disconnect("Bluetooth-Schreiben fehlgeschlagen. Bitte erneut verbinden.");
    }
    private void finish(WireProtocol.Reply reply) {
        main.removeCallbacks(timeout);
        Command done=active;active=null;earlyReply=null;writing=false;
        if(done!=null)done.callback.done(reply);
        startNext();
    }
    public void disconnect(String reason) {disconnectQuietly();listener.onDisconnected(reason);}
    private void disconnectQuietly() {
        stopScan();main.removeCallbacks(timeout);main.removeCallbacks(connectTimeout);
        ready=false;active=null;commands.clear();decoder.reset();earlyReply=null;writing=false;rx=null;
        BluetoothGatt old=gatt;gatt=null;
        if(old!=null){try{old.disconnect();old.close();}catch(SecurityException ignored){}}
    }
}
