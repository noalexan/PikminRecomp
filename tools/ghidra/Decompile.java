// Print decompiled C for functions named in script args (read-only use).
// Usage: analyzeHeadless ghidra PikminGPIP01 -process main.elf -noanalysis -readOnly \
//          -scriptPath tools/ghidra -postScript Decompile.java main PSMTXConcat
// @category PikminRecomp

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;

public class Decompile extends GhidraScript {
	@Override
	protected void run() throws Exception {
		DecompInterface ifc = new DecompInterface();
		ifc.openProgram(currentProgram);
		for (String name : getScriptArgs()) {
			for (Symbol s : currentProgram.getSymbolTable().getGlobalSymbols(name)) {
				Function f = getFunctionAt(s.getAddress());
				if (f == null) {
					continue;
				}
				DecompileResults r = ifc.decompileFunction(f, 60, monitor);
				println("=== " + name + " @ " + f.getEntryPoint() + "\n"
					+ (r.decompileCompleted() ? r.getDecompiledFunction().getC() : r.getErrorMessage()));
			}
		}
		ifc.dispose();
	}
}
