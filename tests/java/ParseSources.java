import javax.tools.*;
import com.sun.source.util.JavacTask;
import java.io.*;
import java.util.*;
public class ParseSources {
 public static void main(String[] args)throws Exception{
  JavaCompiler compiler=ToolProvider.getSystemJavaCompiler();
  DiagnosticCollector<JavaFileObject> diagnostics=new DiagnosticCollector<>();
  try(StandardJavaFileManager manager=compiler.getStandardFileManager(diagnostics,null,null)){
   JavacTask task=(JavacTask)compiler.getTask(null,manager,diagnostics,Arrays.asList("-proc:none"),null,manager.getJavaFileObjects(args));
   task.parse(); // Parse only: Android SDK types are intentionally not substituted.
   for(Diagnostic<?> d:diagnostics.getDiagnostics())if(d.getKind()==Diagnostic.Kind.ERROR)throw new AssertionError(d.toString());
  }
  System.out.println("PASS Java syntax: "+args.length+" source files; not an Android build");
 }
}
