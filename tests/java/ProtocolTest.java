import de.mkrativ.crocosauf.WireProtocol;
import java.nio.charset.StandardCharsets;
import java.util.*;
public class ProtocolTest {
 static int checks=0;
 static void check(boolean ok){checks++;if(!ok)throw new AssertionError("Check "+checks);}
 static void bad(Runnable f){boolean failed=false;try{f.run();}catch(IllegalArgumentException e){failed=true;}check(failed);}
 public static void main(String[] args){
  String response="24 200 {\"standby\":\"Grüße & Spaß\"}\n";
  for(int size=1;size<=64;size++){
   WireProtocol.Decoder decoder=new WireProtocol.Decoder();List<WireProtocol.Reply> results=new ArrayList<>();
   for(byte[] chunk:WireProtocol.chunks(response.getBytes(StandardCharsets.UTF_8),size))results.addAll(decoder.feed(chunk));
   check(results.size()==1);check(results.get(0).id==24);check(results.get(0).code==200);check(results.get(0).body.equals("{\"standby\":\"Grüße & Spaß\"}"));
  }
  WireProtocol.Decoder d=new WireProtocol.Decoder();
  List<WireProtocol.Reply> two=d.feed("1 200 OK\n2 409 Bitte Maul schliessen.\n".getBytes(StandardCharsets.UTF_8));
  check(two.size()==2);check(two.get(0).ok());check(!two.get(1).ok());
  check(WireProtocol.encode("Grüße & +%").equals("Gr%C3%BC%C3%9Fe%20%26%20%2B%25"));
  check(new String(WireProtocol.request(3,"/volume?v=22"),StandardCharsets.UTF_8).equals("3 /volume?v=22\n"));
  bad(()->WireProtocol.request(0,"/hello"));bad(()->WireProtocol.request(1,"/status\n2 /factoryReset"));
  bad(()->WireProtocol.request(1,"/saveText?txt="+"x".repeat(510)));
  bad(()->new WireProtocol.Decoder().feed(("9 200 "+"x".repeat(4097)).getBytes(StandardCharsets.UTF_8)));
  bad(()->new WireProtocol.Decoder().feed("not a response\n".getBytes(StandardCharsets.UTF_8)));
  d.feed("unfinished".getBytes(StandardCharsets.UTF_8));d.reset();check(d.feed("9 200 OK\n".getBytes(StandardCharsets.UTF_8)).size()==1);
  System.out.println("PASS Java transport: "+checks+" checks (including 64 fragment sizes)");
 }
}
