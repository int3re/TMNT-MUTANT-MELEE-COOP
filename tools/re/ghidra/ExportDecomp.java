// Ghidra post-script: writes the decompiled C of every function to one text file.
// Argument: the output path.
import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportDecomp extends GhidraScript {
  @Override
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    DecompInterface ifc = new DecompInterface();
    ifc.openProgram(currentProgram);
    int done = 0, failed = 0;
    try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
      FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
      while (it.hasNext() && !monitor.isCancelled()) {
        Function f = it.next();
        w.println("// ===== " + f.getEntryPoint() + " " + f.getName() + " size " + f.getBody().getNumAddresses());
        DecompileResults r = ifc.decompileFunction(f, 30, monitor);
        if (r != null && r.decompileCompleted()) {
          w.println(r.getDecompiledFunction().getC());
          ++done;
        } else {
          w.println("// (not decompiled)");
          ++failed;
        }
        if ((done + failed) % 1000 == 0) {
          w.flush();
          println("ExportDecomp: " + (done + failed) + " functions");
        }
      }
    }
    println("ExportDecomp: " + done + " decompiled, " + failed + " failed -> " + out);
  }
}
