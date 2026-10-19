// Disassembles and prints an address range (linear addresses), including code Ghidra did not
// assign to any function. Args: <start-hex> <end-hex-exclusive>
// @category UGH
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Symbol;

public class DumpRange extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Address start = toAddr(Long.parseLong(args[0], 16));
        Address end = toAddr(Long.parseLong(args[1], 16) - 1);
        AddressSet range = new AddressSet(start, end);

        // Linear sweep: start a flow disassembly at every byte that is still undefined, so code reached only
        // through pointers in data (state handlers, jump tables) is covered too.
        Address cursor = start;
        while (cursor != null && cursor.compareTo(end) <= 0) {
            if (currentProgram.getListing().getInstructionContaining(cursor) == null
                    && currentProgram.getListing().getDefinedDataContaining(cursor) == null) {
                new DisassembleCommand(cursor, range, true).applyTo(currentProgram, monitor);
            }
            CodeUnit unit = currentProgram.getListing().getCodeUnitContaining(cursor);
            cursor = unit == null ? cursor.next() : unit.getMaxAddress().next();
        }

        for (CodeUnit cu : currentProgram.getListing().getCodeUnits(range, true)) {
            Symbol label = currentProgram.getSymbolTable().getPrimarySymbol(cu.getAddress());
            if (label != null) {
                println(label.getName() + ":");
            }
            String kind = cu instanceof Instruction ? "" : "db ";
            println(String.format("  %s  %s%s", cu.getAddress(), kind, cu));
        }
    }
}
