package de.mkrativ.crocosauf;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** UTF-8 byte framing, independent of BLE packet boundaries or negotiated MTU. */
public final class WireProtocol {
    private WireProtocol() {}
    public static final String SERVICE = "7c800001-0dc3-4fd6-9c26-a3821da872bc";
    public static final String RX = "7c800002-0dc3-4fd6-9c26-a3821da872bc";
    public static final String TX = "7c800003-0dc3-4fd6-9c26-a3821da872bc";
    public static final class Reply {
        public final int id, code; public final String body;
        Reply(int id, int code, String body) { this.id=id;this.code=code;this.body=body; }
        public boolean ok() { return code>=200 && code<300; }
    }
    public static byte[] request(int id, String route) {
        if(id<1 || id>99999999 || !route.startsWith("/") || route.contains("\n") || route.contains("\r") || route.contains("\0"))
            throw new IllegalArgumentException("Ungültiger Befehl");
        byte[] data=(id+" "+route+"\n").getBytes(StandardCharsets.UTF_8);
        if(data.length>511) throw new IllegalArgumentException("Befehl zu lang");
        return data;
    }
    public static List<byte[]> chunks(byte[] data, int size) {
        if(size<1) throw new IllegalArgumentException("Paketgröße");
        List<byte[]> result=new ArrayList<>();
        for(int i=0;i<data.length;i+=size) result.add(java.util.Arrays.copyOfRange(data,i,Math.min(i+size,data.length)));
        return result;
    }
    public static final class Decoder {
        private final ByteArrayOutputStream pending=new ByteArrayOutputStream();
        public void reset(){pending.reset();}
        public List<Reply> feed(byte[] data) {
            List<Reply> result=new ArrayList<>();
            for(byte value:data) {
                if(value=='\n') {
                    String line=new String(pending.toByteArray(),StandardCharsets.UTF_8);pending.reset();
                    String[] parts=line.split(" ",3);
                    if(parts.length!=3 || !parts[0].matches("[0-9]{1,8}") || !parts[1].matches("[0-9]{3}"))
                        throw new IllegalArgumentException("Ungültige Antwort");
                    result.add(new Reply(Integer.parseInt(parts[0]),Integer.parseInt(parts[1]),parts[2]));
                } else if(value!='\r') {
                    if(pending.size()>=4096){reset();throw new IllegalArgumentException("Antwort zu lang");}
                    pending.write(value);
                }
            }
            return result;
        }
    }
    public static String encode(String value) {
        // Form URL encoding; no plus/percent ambiguity; bounds checked before writing.
        StringBuilder s=new StringBuilder();
        for(byte b:value.getBytes(StandardCharsets.UTF_8)) {
            int c=b&255;
            if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.') s.append((char)c);
            else s.append('%').append("0123456789ABCDEF".charAt(c>>4)).append("0123456789ABCDEF".charAt(c&15));
        }
        return s.toString();
    }
}
