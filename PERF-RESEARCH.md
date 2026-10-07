# nPDF speed optimisation — research, patches, and verification

**Fork:** https://github.com/luimu64/nPDF
**Branch:** `perf/blit-and-lazy-search` (commit `ff130b8`, pushed)
**Target:** TI-Nspire, ARM926EJ-S (ARMv5TEJ), ~132–150 MHz, no FPU, no NEON, 16 KB I-cache / 8 KB D-cache, 64 MB RAM

---

## 1. Method, and what is honestly NOT verified

I could not build a `.tns` binary: the Ndless SDK (`nspire-g++`, `genzehn`, `make-prg`) is not
installed in this container and there was no root to install it. So **no claim below is based on
running nPDF on hardware.**

What I did instead, so the numbers are real rather than plausible:

- Downloaded an ARM cross-toolchain (GCC 13.2, `arm-none-linux-gnueabihf`) into the scratch dir.
- Compiled the **actual** nPDF `Screen.cpp` blit loops (and the original upstream versions, pulled
  from `git show abe29f5:Screen.cpp`) for `-mcpu=arm926ej-s`, and read the generated assembly with
  `objdump`. Instruction counts below come from real disassembly of the real source.
- Ran a differential benchmark on both x86 and emulated ARM (`qemu-arm-static`), with an exact
  pixel-for-pixel equality check against the original implementation.
- Read MuPDF's own source in the fork for the AA and pixmap facts.

Caveat that matters: qemu's cycle model is not an ARM926EJ-S pipeline model, so **qemu wall-clock
ratios are indicative, not authoritative.** I therefore quote instruction counts (architecture-
independent, from real codegen) as the primary evidence and treat timings as corroboration.

---

## 2. Corrections to my own earlier analysis

The user asked me to check my own findings. Two of my first-pass claims were **wrong**, and the
correction changes the recommendation:

**WRONG (first pass):** *"`setPixel` is not `inline` and is called 76,800 times per frame, so
inlining it will give 3–5×."*
**Reality:** GCC already inlines it. Disassembly of the original `showImgRGBA` shows **zero `bl`
instructions** in the hot path. There is no per-pixel call overhead to remove.

**WRONG (first pass):** *"Add `-mcpu=arm926ej-s -mtune=arm926ej-s` to the Makefile — free 10–20%."*
**Reality:** two problems. (a) `nspire-g++` is a much older GCC and its acceptance of these flags
must be verified on the actual toolchain — precisely the failure mode below. (b) I proved on my
toolchain that `-mcpu=arm926ej-s` *combined with* an explicit `-mfpu=vfp` overrides the core and
lets the compiler emit **VFP/NEON instructions**, which do not exist on ARM926EJ-S and would
`SIGILL` on real hardware. A silently-ignored `-mcpu` would let GCC fall back to a newer default
arch with the same result. So I deliberately **did not add** these flags, and documented the
one-line check to run before enabling them.

The genuine blit win is smaller than I claimed — but it is still real, and I measured what it
actually is.

---

## 3. Verified patches

### 3.1 `Screen.cpp` — row-pointer blit for 16-bit colour (the main change)

The original computes `pos = n*(wImg*i + j)` per pixel and calls `setPixel`, which re-derives
`y*SCREEN_WIDTH + x` and runs a **two-comparison bounds guard** (`x < 320 && y < 240`) on every
pixel. That guard is data-dependent on the loop indices, so GCC could not hoist it — confirmed in
the disassembly, which carries a `cmp`/`movhi`/`movls` chain per pixel.

The rewrite keeps a destination row pointer, advances the source pointer, and drops the guard
because `Viewer::display()` already clamps `w`/`h` to the screen.

**Measured on real arm926ej-s codegen (`-O2`):**

| function | original | patched | reduction |
|---|---|---|---|
| `showImgRGBA` | 68 instructions | 43 | **−37%** |
| `showImgGrayA` (`-O3`) | 94 instructions | 71 | **−24%** |
| `fillRect` (letterbox) | per-pixel helper | pointer walk | **8.7×** (x86, steady-state) |

Correctness: **0 differing pixels** vs the original on all three paths, verified across the full
320×240 framebuffer.

`fillRect` / `drawVert` / `drawHoriz` get the same treatment. These are easy to overlook but they
paint the letterbox background up to twice per `display()` call, and upstream walked them with the
per-pixel helper too.

The 4-bit grayscale path (`SCR_320x240_4`, classic Nspires) is left on the original helper, since
it uses a different bit-packing scheme.

### 3.2 `Viewer.cpp` — build the text layout on demand

`drawPage()` called `fz_new_stext_page_from_page()` **and freed it again in the same function**.
That walks the entire page content stream a second time and builds a `fz_stext_page` in the
document arena — so the cost was paid on **every page turn and every zoom step even when the user
never pressed Ctrl+F**.

`ensurePageText()` now builds it on first search and keeps it for the page's lifetime, so repeated
Ctrl+G search-next doesn't rebuild layout. It's dropped only when the page underneath changes.

**Honest scope limit:** this does **not** speed up Ctrl+F itself. `fz_search_stext_page()` still
allocates a buffer of the whole page text and scans it, as I verified by reading
`mupdf/source/fitz/stext-search.c`. What improves is **page turn and zoom latency on the
non-search path**. I'm not going to dress that up as "search is now fast".

### 3.3 `Makefile` — make the existing `--gc-sections` actually work

`-Wl,--gc-sections` was already passed, but **no `-ffunction-sections`/`-fdata-sections`**, so the
linker could only discard whole input sections and collected almost nothing from `libmupdf.a`.

Verified with a two-function test object: without the flags both functions land in a single
`.text`; with them you get `.text.used_fn` and `.text.unused_fn` separately, so the unused one
becomes collectable. This is a **binary-size** win, which on a device with 64 MB RAM and a slow
flash read is a real startup win, not just cosmetics.

### 3.4 `Makefile` — expose MuPDF's `AA_BITS` (new finding, biggest lever)

This is the largest single lever I found anywhere in the stack, and upstream nPDF has never set it.

Anti-aliasing quality in MuPDF is a **compile-time constant**, not a runtime setting. From
`mupdf/source/fitz/draw-imp.h`, the `AA_BITS` value selects the rasterizer's supersampling grid:

| `AA_BITS` | bits | grid | coverage cells per run |
|---|---|---|---|
| **unset (nPDF's current build)** | 8 | 17×15 | **255** |
| 6 | 6 | 8×8 | 64 |
| 4 | 4 | 5×3 | 15 |
| 2 | 2 | 2×2 | 4 |
| 0 | 0 | 1×1 | 1 |

nPDF has always built with `AA_BITS` unset, so it is evaluating **255 coverage cells per
coverage run** on a 132 MHz core with no FPU.

Critically, this **cannot** be worked around at run time: `draw-imp.h` only defines
`fz_rasterizer_aa_*` as mutable `ctx` values when `AA_BITS` is *not* defined. So
`fz_set_aa_level()` in the viewer is a no-op when the library is built with a fixed `AA_BITS`.

I added it as an **opt-in knob** (`make AA_BITS=4`) rather than a default, because it changes
visual output — aliased text and vector edges. That's the user's call, not mine to make silently.
Verified with `make -n` that the define reaches both the MuPDF and nPDF compile lines, and only
when passed.

---

## 4. External research: what fits, what doesn't

I searched for existing prior art and compared each technique against this specific target.

| Technique | Source | Verdict for nPDF |
|---|---|---|
| Lower the resource-store cache (`FZ_STORE_UNLIMITED` → bounded) | MuPDF `fz_new_context` docs; sioyek issue #954 (measured 800 MB → 220 MB working set) | **Applicable but low priority.** nPDF uses `FZ_STORE_UNLIMITED` (Viewer.cpp:35). On a 64 MB device this invites eviction thrash, but nPDF renders one page at a time so the working set stays small. Worth bounding as a safety net, not a speed win. |
| Reduce `AA_BITS` | MuPDF `draw-imp.h` (read in-repo) | **Most applicable.** See §3.4. |
| Allocate the pixmap with `alpha=0` | MuPDF `fz_new_pixmap_with_bbox` docs — signature's last parameter is literally `int alpha` | **Applicable, needs care.** nPDF passes `alpha=1`, so MuPDF allocates and composites a 4th channel (`pix->n == 4`) that nPDF then discards. Real memory/bandwidth saving, but the draw device's blend behaviour must be re-tested before shipping — I did **not** make this change. |
| Render only the visible region instead of the whole page | General MuPDF/embedded practice; `fz_new_pixmap_with_bbox` sets x/y origin | **Applicable, largest architectural win.** nPDF rasterizes the full page on every zoom, so a 2× zoom quadruples the pixmap. Switching to a clipped viewport keeps cost bounded at 320×240. This is a real refactor, not a one-liner — flagged, not attempted. |
| `-mcpu` / `-mtune` / `-marm` tuning | GCC ARM Options | **Trap.** See §2. `-marm` is safe and I use it; the `-mcpu`/`-mfpu` combination can silently emit unexecutable code. |
| `-ffast-math` | GCC docs | **Not applicable here.** I grepped: `Screen.cpp` contains no float at all, and the `Viewer` float work is per-page, not per-pixel. It would buy nothing on the hot path while risking rendering differences in MuPDF's own math. |
| `-flto` across the `libmupdf.a` boundary | GCC docs | **Speculative.** Plausible, but nspire-g+++the Ndless link step is exactly where LTO support is least certain. Recommend only as an experiment. |
| Disable more MuPDF features (`TOFU_*`, `NO_ICC`) to shrink the binary | MuPDF `config.h` | **Already done.** The fork's `config.h` sets `TOFU`, `TOFU_CJK`, `FZ_ENABLE_XPS=0`, `FZ_PLOTTERS_* = 0`, `FZ_ENABLE_JS=0`. `-DNO_ICC` is already reaching the compile line (verified via `make -n`). Nothing left to win cheaply. |
| Existing nPDF forks with speed work | GitHub | **None found.** No prior art to reuse. |

---

## 5. What I'd do next, in priority order

1. **Try `make AA_BITS=4`** — highest expected payoff, one flag, trivially reversible. Judge the
   visual result yourself; it's a quality/speed trade, not a free win.
2. **Viewport-clipped rendering** (§4) — the only change that stops zoom from scaling cost.
3. **Pixmap `alpha`** — verify blending, then flip to the non-alpha path.
4. **Bound `FZ_STORE_UNLIMITED`** — hygiene, protects against pathological documents.

## 6. Reproducing the measurements

Toolchain and benchmarks live in `/opt/data/cache/scratch/`:

```bash
# real ARM codegen of the actual source, no toolchain from the TNS SDK needed
TC=/opt/data/cache/scratch/tc/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-linux-gnueabihf
$TC/bin/arm-none-linux-gnueabihf-gcc -O2 -mcpu=arm926ej-s -marm -mfpu=vfp \
    -c -nostdlib -ffreestanding -x c++ -std=gnu++14 blit_pair.cpp -o bp.o
$TC/bin/arm-none-linux-gnueabihf-objdump -d bp.o --no-show-raw-insn | less

# differential + correctness benchmark (x86 and ARM)
g++ -O2 -o bench_ab bench_ab.cpp && ./bench_ab
./qemu-arm-static ./bench_ab_arm
```

## 7. Before trusting any of this on hardware

```bash
make && ls -la nPDF.tns
```

Then confirm the toolchain's arch support **before** enabling the optional flags:

```bash
nspire-g++ -mcpu=arm926ej-s -Q --help=target | grep '^  -march='
# expect: armv5tej+fp  -> honoured
# if it prints something newer, the flag is being ignored and must not be used
```

And verify no VFP/NEON slipped into the object:

```bash
arm-*-objdump -d nPDF.elf | grep -cE '\bv(ldr|str|mov|shl|mla)'
# must be 0
```
