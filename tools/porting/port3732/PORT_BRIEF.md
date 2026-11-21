# Port of the Crimson Desert co-op mod to game build 1.0.0.3732 — shared brief for each module agent

Repo: `D:\SteamLibrary\steamapps\common\Crimson Desert\multi` (C++20, MSVC x64, a git checkout on `main`). Read
`CLAUDE.md` first (engineering rules; research docs in Spanish, player-facing text and code comments in English).
The game exe on disk is **1.0.0.3732** (PE timestamp `0x6AFDC0CF`, image size `0x13C7E000`,
`D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`, fixed base `0x140000000`). Steam replaced
1.0.0.3663 with it on 2026-11-19 and **the 3663 exe is gone** (no copy anywhere). Until ported, the mod switches
itself off on 3732.

## The decision: retarget 3663's values IN PLACE
Nobody can run 3663 any more, so we do not add new tables. **Every value the code uses when
`game::current_build() == Build::v3663` / `game::is_3663()` becomes its 3732 value, in place.** Identifiers keep their
`3663` names for now (the lead renames them all to 3732 at the end, mechanically). Do NOT add `3732` identifiers, do
NOT touch gates, switches or logic, do NOT touch the tables of other builds (2976, 3527, 3549, 3576).
Already done by the lead: `build_profile.h` identity (`kPeTimestamp3663`/`kImageSize3663` now hold 3732's) and the
Troc singleton block range.

"Every value" means: constants named `...3663...`, byte arrays named `...3663...`, rows of tables selected for 3663
(`kAddrs3663`, `rva3663()`, `current3663{...}`, `case Build::v3663:` blocks, `if (... v3663 ...)` branches), and
arrays/constants without a build in their name that the 3663 path uses. Find them all in your files; the lead will
cross-check with a script that lists every address-like literal on a 3663 path that did not change.

## What changed between 3663 and 3732 (measured)
- **Troc catalogue: identical** (1179 messages, same names, same native numbers, kinds and sizes:
  `tools/porting/data/opcode_mapping_3663_3732.json`). Do not touch any opcode number, logical or native.
- **Troc singleton block** moved by exactly +0x6000: `0x6F14E10..0x6F48840` -> `0x6F1AE10..0x6F4E840`. A singleton
  instance inside the old block (e.g. a `...Req/Ack` object address) is +0x6000; confirm with one code reference.
- **Code** (the R-X section, `0x1000 + 0x5617000`): the same functions, recompiled; shifts grow with the address
  from about +0x370 (near 0x67xxxx) to about +0x50xx (near 0x2C7xxxx), about +0x45xx near 0x3FCxxxx. NOT monotone
  locally and not constant between two anchors: every address needs its own evidence.
- **vtables**: by RTTI name (`vt3732()` below); they moved by different amounts (+0x4Bxx .. +0x55xx typical).
- **Globals** (RW data): about +0x5000 .. +0x6200 (the world system `0x72451C0` -> `0x724B350`, 3197 references).
- **Protected RWX section**: 3663 `0x7AC4000 + 0xC581064` -> 3732 `0x7ACB000 + 0xBD0F7FC`, re-encoded. An address
  inside it cannot be found by shift: re-derive it the way its 3663 comment says it was found (usually the rel32 of a
  thunk/jump in the real code, or a return address after a call found in the RWX bytes on disk).
- Struct layouts: unknown until checked. Where the 3663 comment says how an offset was read, re-check it.

## Tools (read-only; python with capstone installed; run with `python -I yourscript.py`)
`tools/porting/p3732lib.py` — `sys.path.insert(0, r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi/tools/porting')`
then `from p3732lib import *`:
- `rd(rva, n)` bytes, `q(rva)` a 64-bit value, `dis(rva, n)` capstone tuples `(rva, size, mnemonic, ops, bytes)`,
  `show(rva, n)` prints a listing (stops at ret/int3 unless `stop_ret=False`).
- `func(rva)` -> `(begin, end)` of the .pdata function holding rva (a function may have several chunks);
  `is_start(rva)`; `starts_between(a, b)`.
- `shift_est(old)` -> the shifts of the nearest anchors below/above (code and data have their own lists; 156 anchors
  in `tools/porting/data/anchors_3732.json`); `guess(old)` -> `([old+shift...], [function starts between them])`.
- `near(old, data)` -> `(matches nearest first, shifts)`: a 3663 byte array searched around `old + shift` with its
  relative operands masked (window 0x8000; `exact=True` for raw bytes); `whole(data, old)` over the whole code section.
- `operands(rva, n)` -> the relative operands of the code at rva: `(insn rva, 'call'|'jump'|'rip', target rva)`.
- `vt3732(vt3663)` -> the 3732 vtable of the same RTTI class (None if the old address was not a vtable start);
  `vt_name(vt3732)`; `slot(vt, k)` -> the k-th virtual function.
- `refs(global)` -> code sites with a RIP-relative operand on it (about 2 s per call; `count_refs`);
  `callers(fn)` -> sites of `call/jmp rel32` to fn.
Other data: `tools/porting/data/rtti_3663.json` / `rtti_3732.json` (class -> vtables), `troc_mapping_3663_3732.json`
(each Troc message's vtable, handler slots, singleton), `docs/research/port-3663.md` (how each 3663 value was found:
read the part about your modules FIRST, it tells you the method that worked).
CAUTION: the bash tool mangles backslashes and quotes in heredocs. Write python scripts with the Write tool into
your own folder `C:\Users\Admin\AppData\Local\Temp\claude\b9a40c34b2952844\aded0e0e-c64a-4734-8a78-f21a9b51aca7\scratchpad\p3732\<your group>\`.

## Method for each value
1. **A hooked/called function or a mid-function site with a byte array in the source:** `near(old, array)`; expect ONE
   match close to `old + shift`. Confirm with `show()` that it is the same code (same instructions; only relative
   fields may differ). A function head should satisfy `is_start(new)`.
2. **A code address without bytes in the source:** use what the code/comment says about it: a virtual function ->
   `slot(vt3732(old vtable), k)`; the target of a call at a site you already mapped -> `operands(site)`; a function
   that references a known global or calls a known function -> `refs()` / `callers()` and the neighbourhood;
   otherwise `guess(old)` gives the candidates (function starts between the two shifts): read each with `show()` and
   decide from what the module's own code expects of that function (arguments, struct offsets it touches, constants,
   the calls it makes to functions you already mapped). If you cannot decide with certainty, do NOT guess: leave the
   old value, mark it `UNRESOLVED` in your map and report.
3. **Globals:** from the instruction in 3732 that reads/writes them inside a function you mapped (`operands()`),
   cross-checked with `count_refs` when the 3663 comment gives a count (counts change by a few).
4. **vtables:** `vt3732(old)`; say the class name (`vt_name`).
5. **Byte arrays:** after retargeting, an array must equal `rd(new, len)` EXACTLY (the mod compares before hooking).
   If only relative fields differ, write 3732's bytes. If any non-relative byte differs, the function CHANGED: stop on
   that item and report it prominently (what differs, with both listings if you have the old bytes). If an array is
   shared with another build's table and needs different bytes for 3732, add a `...3663` copy for the 3663 path (the
   one allowed structural change) and say so.
6. **Struct offsets and registers the hook relies on:** read the hooked function in 3732 around the site and check
   that what the module assumes still holds (which register holds what, the `[reg+off]` it reads). Say which you
   checked and which you could not.
7. **Evidence in the source:** next to each retargeted value add
   `// 3732 (static, 2026-11-21; never run): was 0x<old>; <how you know>` (one line; keep the older comments, they
   describe 3663 with 3663's addresses).

## Deliverables in your scratch folder
- `map.json`: a list of `{"file", "name", "kind": "function|site|global|vtable|rwx|array|offset", "old": "0x..",
  "new": "0x..", "evidence"}` for EVERY value you looked at (unchanged ones too, with `"new"` = `"old"` and why;
  unresolved ones with `"new": null`).
- `verify.py`: re-reads every byte array of your files' 3663 path against the exe at its new address and checks every
  function address with `is_start` (or says why it is a mid-function site); it must print `0 mismatches`.

## Rules
- Edit ONLY your assigned files. Do not edit tests, `CMakeLists.txt`, `native_opcode*`, docs, or another group's
  files; if one needs a change, describe it in your report.
- Do NOT build (`scripts/build.ps1`), do NOT launch or close the game, do NOT touch `bin64`, no git command that
  changes anything (`git diff -- <file>` and `git show HEAD:<file>` are fine to see the original).
- Keep each file's line endings and style; keep the C++ compiling (same types, same array lengths unless the code
  uses `sizeof`).
- No inline hooks mid-function; never identify an exe section by name.

## Report (your final message; it is all the lead sees besides your files)
Per file: how many values retargeted (a compact table old -> new is welcome for the important ones), everything
UNRESOLVED and why, every function whose body changed, every struct offset/register assumption you could not verify,
any change needed in a file you may not edit (tests included). Facts only, compact.
