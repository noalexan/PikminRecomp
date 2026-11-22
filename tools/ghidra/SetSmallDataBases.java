// Set r13/r2 to _SDA_BASE_/_SDA2_BASE_ over all executable memory so the
// decompiler resolves small-data accesses to named globals.
// Run as an analyzeHeadless -preScript (before auto-analysis).
// @category PikminRecomp

import java.math.BigInteger;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.ProgramContext;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;

public class SetSmallDataBases extends GhidraScript {

	private long symbolValue(String name) throws Exception {
		for (Symbol s : currentProgram.getSymbolTable().getGlobalSymbols(name)) {
			return s.getAddress().getOffset();
		}
		throw new IllegalStateException("missing symbol " + name);
	}

	@Override
	protected void run() throws Exception {
		ProgramContext ctx = currentProgram.getProgramContext();
		String[][] regs = { { "r13", "_SDA_BASE_" }, { "r2", "_SDA2_BASE_" } };
		for (String[] pair : regs) {
			Register reg = currentProgram.getRegister(pair[0]);
			BigInteger value = BigInteger.valueOf(symbolValue(pair[1]));
			for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
				if (block.isExecute()) {
					ctx.setValue(reg, block.getStart(), block.getEnd(), value);
				}
			}
			println(String.format("%s = 0x%08x over executable blocks", pair[0], value));
		}
		// GQR0 is fixed at 0 (unscaled float) by the SDK/compiler convention;
		// without it paired-single loads/stores decompile as type-dispatch mazes.
		Register gqr0 = currentProgram.getRegister("GQR0");
		for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
			if (block.isExecute()) {
				ctx.setValue(gqr0, block.getStart(), block.getEnd(), BigInteger.ZERO);
			}
		}
		println("GQR0 = 0 over executable blocks");
	}
}
