// Exports a disassembly listing (per function, with labels and references) to listing.asm.
// @category UGH
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Symbol;

import java.io.File;
import java.io.PrintWriter;

public class ExportAsm extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outDir = getScriptArgs()[0];
        new File(outDir).mkdirs();

        try (PrintWriter out = new PrintWriter(new File(outDir, "listing.asm"))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                out.printf("%n;==== %s @ %s%n", f.getName(), f.getEntryPoint());
                for (Instruction insn : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    Symbol label = currentProgram.getSymbolTable().getPrimarySymbol(insn.getAddress());
                    if (label != null && !insn.getAddress().equals(f.getEntryPoint())) {
                        out.printf("%s:%n", label.getName());
                    }
                    out.printf("  %s  %s%n", insn.getAddress(), insn);
                }
            }
        }
    }
}
