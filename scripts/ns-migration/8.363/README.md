# Sealing `OverloadId`, `SymbolID` and `ModulePath`

The edits in this commit were made by these scripts, in this order, against
the parent commit. Each rewrites by exact string or by the (file, line,
column) the compiler printed when a door was made private, and asserts every
replacement count.

1. `seal_overload.py` - `OverloadId` moves into `decl_index.cryo`, its
   position and constructor private; importers rewritten.
2. `seal_symid.py`, `seal_symid2.py`, `seal_symid3.py` - `SymbolID`'s number
   and constructor private; the one mint becomes `SymbolIds::next`; every
   read the compiler refused (`refused-symbolid.lst`, 67 lines) becomes
   `.key()`; the three rebuilds of an id from a stored number become
   key-taking bodies behind the old signatures.
3. `seal_mp.py`, `seal_mp2.py` - `ModulePath` moves into `module_graph.cryo`
   with `of` private, and the graph's three doors are added.
   `refused-modulepath.lst` is the compiler's list of the 40 sites that then
   stopped compiling.
4. `seal_mp_sites.py` - the 36 compiler sites and the editor's one rewritten
   onto the doors.
5. `probe_mp.py` - TEMPORARY, not in the commit's source: exits 97 wherever a
   rewritten site could answer differently from `ModulePath::of(text)`. Run
   over the census, the examples and the editor build with it applied; it
   never fired. `CRYO_SEALPROBE_CONTROL=1` makes it print each probe it
   reaches instead, which is the control that it is entered.

Scripts 1-3 were written against a scratch tree and carry absolute paths.
