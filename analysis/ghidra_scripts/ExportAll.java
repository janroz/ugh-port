// Exports decompiled C for every function, plus a function index and defined strings.
// @category UGH
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;

public class ExportAll extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outDir = getScriptArgs()[0];
        new File(outDir).mkdirs();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter code = new PrintWriter(new File(outDir, "decompiled.c"));
             PrintWriter index = new PrintWriter(new File(outDir, "functions.tsv"))) {
            index.println("address\tname\tsize\tcallees");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                StringBuilder callees = new StringBuilder();
                for (Function c : f.getCalledFunctions(monitor)) {
                    callees.append(c.getName()).append(' ');
                }
                index.printf("%s\t%s\t%d\t%s%n", f.getEntryPoint(), f.getName(),
                        f.getBody().getNumAddresses(), callees.toString().trim());

                DecompileResults res = decomp.decompileFunction(f, 120, monitor);
                code.printf("// ==== %s @ %s%n", f.getName(), f.getEntryPoint());
                if (res.decompileCompleted()) {
                    code.println(res.getDecompiledFunction().getC());
                } else {
                    code.println("// decompile failed: " + res.getErrorMessage());
                }
            }
        }

        try (PrintWriter strings = new PrintWriter(new File(outDir, "strings.tsv"))) {
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                StringDataInstance s = StringDataInstance.getStringDataInstance(d);
                if (s != StringDataInstance.NULL_INSTANCE) {
                    String v = s.getStringValue();
                    if (v != null) {
                        strings.printf("%s\t%s%n", d.getAddress(), v.replace("\n", "\\n").replace("\r", "\\r"));
                    }
                }
            }
        }
    }
}
