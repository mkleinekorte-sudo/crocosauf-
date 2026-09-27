package de.mkrativ.butcher;
import android.util.Base64;
import java.nio.charset.StandardCharsets;
import java.io.ByteArrayOutputStream;
import java.util.*;

public final class WireProtocol {
  private WireProtocol(){}
  public static final UUID SERVICE=UUID.fromString("a9460001-36c5-487c-9113-b31c3894f4b1");
  public static final UUID RX=UUID.fromString("a9460002-36c5-487c-9113-b31c3894f4b1");
  public static final UUID TX=UUID.fromString("a9460003-36c5-487c-9113-b31c3894f4b1");
  public static byte[] command(int id,String path,String payload){
    if(id<1||id>99999999||!path.startsWith("/")||path.contains(" ")||path.contains("\n"))throw new IllegalArgumentException("Ungültiger Befehl");
    String body=Base64.encodeToString(payload.getBytes(StandardCharsets.UTF_8),Base64.NO_WRAP);
    byte[] bytes=(id+" "+path+" "+body+"\n").getBytes(StandardCharsets.UTF_8);
    if(bytes.length>19000)throw new IllegalArgumentException("Zeitplan zu groß");return bytes;
  }
  public static final class Reply {public final int id,code;public final String body;
    Reply(int id,int code,String body){this.id=id;this.code=code;this.body=body;}
    public boolean ok(){return code>=200&&code<300;}
  }
  public static final class Decoder {
    private final ByteArrayOutputStream pending=new ByteArrayOutputStream();
    public void reset(){pending.reset();}
    public List<Reply> feed(byte[] packet){List<Reply> replies=new ArrayList<>();
      for(byte b:packet){if(b=='\n'){
          String line=new String(pending.toByteArray(),StandardCharsets.UTF_8);pending.reset();String[] parts=line.split(" ",3);
          if(parts.length!=3||!parts[0].matches("[0-9]{1,8}")||!parts[1].matches("[0-9]{3}"))throw new IllegalArgumentException("Ungültige Antwort");
          byte[] bytes=Base64.decode(parts[2],Base64.DEFAULT);
          replies.add(new Reply(Integer.parseInt(parts[0]),Integer.parseInt(parts[1]),new String(bytes,StandardCharsets.UTF_8)));
        }else if(b!='\r'){if(pending.size()>=28000){reset();throw new IllegalArgumentException("Antwort zu lang");}pending.write(b);}}
      return replies;
    }
  }
}
