# Core accuracy tests

These standalone tests are intended for emulator-development regressions and are not part of the normal frontend build.

- `cpu_microcode_coverage.cpp` executes every 65C816 opcode value once through the cycle-stepped CPU and requires zero legacy fallbacks.
- `cpu_cycle_order.cpp` checks representative W65C816S bus-cycle ordering, including JSR stack timing and 16-bit read-modify-write write order.

They can be linked against the files in `src/core` using the same C++17 compiler settings as the emulator core.
- `cpu_addressing_cycle_order.cpp` checks cycle placement for stack push/pull, direct-page/indexed accesses, page-crossing absolute indexed reads, indexed stores, stack-relative addressing, and RTS.

## Phase 29 edge-case coverage

`cpu_edge_case_accuracy.cpp` validates W65C816S edge cases that are easy to
miss when only checking final register state:

- emulation-mode Direct Page wrapping when `DL == 0`
- native-mode Direct Page rollover into the following page
- `(direct)` pointer-byte wrapping versus `[direct]` / `PEI` linear exceptions
- hardware NMI pre-stack cycle placement
- MVN bus-cycle body ordering

## Phase 30 trace validation

`cpu_trace_validation.cpp` records completed CPU bus/internal cycles with their
master-clock spans and validates representative sequences directly against the
W65C816S Table 5-7 ordering. It currently covers JSR absolute, BRL, and
hardware NMI entry. The trace API is reusable for future differential traces
against bsnes/Mesen-S and hardware-test logs.

## Phase 31 table-driven addressing trace matrix

`cpu_addressing_trace_matrix.cpp` validates completed bus traces across representative
addressing families: absolute, direct-page indexed, `(dp,X)`, `(dp),Y` with page
crossing, stack-relative, `(stack,S),Y`, branches, stack push, indexed store,
read-modify-write, and software interrupt entry. Each row checks cycle kind,
24-bit bus address, and SNES master-clock duration.

## Phase 32 native/width trace matrix

`cpu_native_width_trace.cpp` validates native-mode 16-bit accumulator/index fetches,
24-bit long-read bank crossing, native stack push ordering, native NMI/RTI stack
sequences, and FastROM-vs-SlowROM opcode-fetch timing.

## Phase 34 ALU / decimal validation

`cpu_alu_accuracy.cpp` exhausts every 8-bit ADC/SBC input in binary and decimal
mode (including invalid BCD digits), stress-tests 16-bit binary and valid-BCD
arithmetic, and checks representative BIT/CMP/shift/rotate flag behavior. The
CPU arithmetic is factored through `include/cpu_alu.hpp` so the same tested
logic is used by every ADC/SBC addressing mode.

## Phase 35 external SingleStepTests oracle bridge

`cpu_singlestep_oracle_smoke.cpp` exercises published vectors from
SingleStepTests/65816 v1 against a test-only flat 16 MiB bus. It validates full
CPU state, relevant RAM, and RAM-visible read ordering independently of the
SNES memory map. `CPU::State` and `Bus::enableFlatTestMemory()` exist solely to
make external CPU conformance suites reproducible without changing production
SNES mapping behavior.

The upstream suite contains 20,000 cases per opcode (10,000 emulation and
10,000 native). The checked-in smoke test intentionally contains only a small
published subset; the full upstream vector corpus should remain external rather
than being copied into the emulator repository.

## Phase 36 bulk SingleStepTests runner

`cpu_singlestep_bulk.cpp` is a dependency-free C++17 runner for the upstream
SingleStepTests/65816 JSON corpus. It accepts individual JSON files or a
directory containing the v1 opcode files and compares:

- complete CPU final state
- all final RAM bytes listed by the oracle
- cycle count
- bus-visible read/write/internal cycle kind
- 24-bit bus address
- bus data value when the upstream vector exposes one

The runner intentionally does not vendor the multi-gigabyte upstream corpus.
Download/checkout `SingleStepTests/65816` separately and point the executable at
its `v1` directory. Examples:

```
cpu_singlestep_bulk --limit 100 /path/to/65816/v1/69.e.json
cpu_singlestep_bulk --limit 100 /path/to/65816/v1
cpu_singlestep_bulk /path/to/65816/v1/69.e.json /path/to/65816/v1/69.n.json
```

`--limit N` runs the first N vectors from each supplied file, which is useful
for fast CI samples. `--state-only` skips cycle-trace comparison when isolating
state/ALU failures. The full upstream corpus remains external by design.

## PPU port accuracy

`ppu_port_accuracy.cpp` checks CPU-facing PPU memory-port behavior added in Phase 37: VRAM read-latch sequencing, VMADD auto-increment even when active-display VRAM accesses are rejected, and the PPU2 open-bus bit on CGRAM high-byte reads.

## Phase 38 OAM / PPU status accuracy

`ppu_oam_status_accuracy.cpp` validates sprite priority rotation, first-sprite
selection, the 32-sprites-per-line Range Over limit, the 34-slivers-per-line
Time Over limit, STAT77 status/open-bus/version bits, and sticky overflow flags
through the end-of-VBlank frame transition. The renderer now also preserves
OBJ-vs-OBJ OAM priority independently from each sprite's BG-priority bits.

## Phase 39 PPU pipeline/CGRAM accuracy

`ppu_pipeline_cgram_accuracy.cpp` checks that OBJ range/time-over state is latched at the end of the scanline evaluation phase rather than at dot 0, and that active-display CGRAM reads/writes use the PPU-controlled palette address instead of being discarded.

## Phase 40 PPU pipeline/CGRAM half-dot accuracy

`ppu_pipeline_cgram_halfdot.cpp` extends Phase 39 with a half-dot main/sub model:

- During active display the live CGRAM address alternates between the main-screen and sub-screen palette indices for the current horizontal position (even dots prefer main, odd dots prefer sub).
- The scanline renderer caches the CGRAM indices it actually consumed so mid-scanline CPU accesses hit the same entries the output pipeline used.
- Force-blank, VBlank, and HBlank continue to use the programmed CGADD value.

Phase 65 replaced the original dot-parity approximation with explicit master-clock phase tracking inside each 4/6-clock PPU dot.

## Phase 41 PPU per-dot pipeline

`ppu_pipeline_per_dot.cpp` validates the per-dot output path:

- Each visible dot (0..255) emits its framebuffer pixel at the moment that horizontal position is reached, using the live register and CGRAM state.
- Force-blank enabled mid-scanline blacks out subsequent dots only; earlier dots keep their previously rendered color.
- Mid-scanline CGRAM writes (via the Phase 39/40 live address) affect later dots; earlier dots retain the old palette.
- OBJ layers for the line are prepared once per line (OAM is not CPU-accessible during active display) and composited with live BG/CGRAM/color-math per dot.

Phase 65 extends this pipeline with explicit master-clock phase tracking inside each 4/6-clock dot.

## Phase 42 hardware visible H window

Visible pixels are emitted only for H-counter values 22..277, mapped to
framebuffer X = H - 22. CGRAM ownership is distinct from DAC visibility:
HBlank begins at H=274, so S-CPU CGRAM MMIO uses programmed CGADD from H=274
onward even while the final four visible pixels are emitted. VBlank/force
blank and the non-visible pre-display interval also use CGADD.
`PPU::screenXFromDot()` exposes the DAC mapping for tests and tooling.

`ppu_pipeline_per_dot.cpp` covers the mapping table, mid-line force-blank and
CGRAM behaviour under the real H window, and the pre-visible CGADD path.

## Phase 43 HBlank status accuracy

`ppu_hblank_status_accuracy.cpp` validates the $4212 HBlank flag shape:

- Set at H=274
- Remains set through H=339 and H=0 of the following line
- Cleared at H=1

Previously `hblank()` was `dot >= 274` only, which incorrectly reported
H=0 as not-blank. NMI remains scheduled at H=0.5 (2 master clocks after
the VBlank line boundary) and H/V-timer IRQ offsets follow the Fullsnes
14+H*4 / previous-line 1374 formulas already in the coordinator.

## Phase 44 long-dot timing

`ppu_long_dot_timing.cpp` locks the NTSC long-dot positions:

- H=323 and H=327 are 6 master clocks on normal lines
- All other dots are 4 master clocks
- Normal line total: 1364 master clocks
- Non-interlace odd-field V=240 short line: every dot 4 clocks, total 1360

Previously the long dots were incorrectly placed at H=322 and H=326.

## Phase 45 Mode 5/6 hi-res sampling

`ppu_mode56_hires.cpp` covers Mode 5/6 tile path:

- Modes 5 and 6 always use 16-pixel-wide tiles (BGMODE 8x8 becomes 16x8)
- Background pixels are deinterleaved into main/sub: even high-res columns
  feed the sub screen, odd columns feed the main screen, doubling horizontal
  density relative to a 256-wide framebuffer
- Both main (TM) and sub (TS) must be enabled for the expected double-density
  composite look on real hardware

## Phase 46 offset-per-tile (Modes 2 / 4 / 6)

`ppu_offset_per_tile.cpp` exercises the OPT path:

- Modes 2, 4, and 6 read per-column scroll overrides from the BG3 tilemap
- Leftmost visible tile column is exempt (uses normal BGnHOFS/VOFS)
- Mode 2/6: separate H-row (at BG3VOFS) and V-row (at BG3VOFS+8)
- Mode 4: single entry, bit 15 selects horizontal vs vertical
- Bits 13/14 select which of BG1/BG2 the entry applies to

## Phase 47 color-window clip-to-black

`ppu_clip_to_black.cpp` validates CGWSEL bits 6-7:

- 00 = never clip
- 01 = clip main to black outside the color window
- 10 = clip main to black inside the color window
- 11 = always clip main to black before color math

Clipping applies to the main-screen operand before addition/subtraction;
color math can still add the fixed color or subscreen on top of black.

## Phase 48 Mode 7 EXTBG

`ppu_mode7_extbg.cpp` covers SETINI bit 6 (External Background):

- When EXTBG is set, Mode 7 pixels with bit 7 clear are BG1; bit 7 set are BG2
- Palette index uses bits 0-6 of the pixel value
- BG1/BG2 each respect TM/TS and window masks independently
- BG2 pixels use a higher priority slot than BG1

## Phase 49 pseudo-hires

`ppu_pseudo_hires.cpp` covers SETINI bit 3 and Mode 5/6 forced hi-res
main/sub behavior. Phase 66 promoted this path to a native 512-pixel output:

- Each visible SNES dot emits two physical DAC output pixels (below/sub then above/main)
- The even/below half-pixel uses the previous dot's latched main-screen color-math state
- The first even/below half-pixel of each scanline is transparent/black
- Modes 5/6 and SETINI pseudo-hires report a 512-pixel active width
- Normal display modes retain the 256-pixel active width
- Color math applies to both DAC halves according to the one-dot-delayed hardware pipeline

## Phase 50 overscan

`ppu_overscan.cpp` validates SETINI bit 2:

- Overscan off: height = 224, VBlank starts at line 225
- Overscan on: height = 239, VBlank starts at line 240
- Framebuffer is sized for 239 lines; height() selects the active region

## Phase 51 OBJ interlace

`ppu_obj_interlace.cpp` covers SETINI bit 1:

- Effective sprite height is doubled for scanline range checks
- Even field draws even sprite lines; odd field draws odd lines
- Tile sample row is localY/2 after the field filter

## Phase 52 STAT78 ($213F)

`ppu_stat78_accuracy.cpp` locks PPU2 status:

- bits 3-0 = version 1
- bit 4 = region (0=NTSC, 1=PAL) via `setPalRegion()`
- bit 6 = external latch flag (set by $2137, cleared on $213F read)
- bit 7 = interlace field (0=odd on frame 0, toggles each frame)

## Phase 53 Mode 7 mosaic

`ppu_mosaic_mode7.cpp` covers mosaic applied to Mode 7:

- When BG1 mosaic is enabled, screen X/Y are snapped to the mosaic grid
  *before* the affine transform and H/V flip
- Size encoding remains `((MOSAIC>>4)&0xF)+1` (0 → 1 pixel)

## Phase 54 BG scroll shared write latch

`ppu_scroll_latch.cpp` validates $210D-$2114 write behavior:

- A single 8-bit latch is shared by all eight BGnxOFS registers
- Each write: `register = (value << 8) | latch; latch = value`
- Only low 10 bits are used for sampling (`& 0x3FF`)
- $210D/$210E are also Mode 7 HOFS/VOFS (same registers as BG1)

## Phase 55 window edges

`ppu_window_edges.cpp` locks window geometry:

- Inside region is `[left, right]` inclusive (width = right - left + 1)
- If left > right the inside region is empty (whole screen is outside)
- Invert flips inside/outside after the empty test
- Window 1/2 combine with OR/AND/XOR/XNOR per layer

## Phase 56 / Phase 2 auto-joypad timing

`ppu_auto_joypad_timing.cpp` locks automatic joypad read:

- Starts at H=33 of the first VBlank scanline (documented window H=32.5..95.5)
- Raises latch at start, releases/clears JOY registers after 128 master clocks
- Samples both data lines on both ports 16 times at 256-master-clock intervals
- JOY1/2 receive D0 and JOY3/4 receive D1
- $4212 bit 0 remains set for the full 4224 master clocks, including long-dot accounting

## Phase 57 VRAM address translation (VMAIN)

`ppu_vram_remap.cpp` locks VMAIN bits 3-2 translation and bits 1-0 increment:

| bits 3-2 | Mapping |
|----------|---------|
| 00 | none |
| 01 | 2bpp-style: `aaaaaaaaYYYxxxxx → aaaaaaaaxxxxxYYY` |
| 10 | 4bpp-style: `aaaaaaaYYYxxxxx → aaaaaaaxxxxxYYY` |
| 11 | 8bpp-style: `aaaaaaYYYxxxxx → aaaaaaxxxxxYYY` |

| bits 1-0 | Increment |
|----------|-----------|
| 00 | +1 |
| 01 | +32 |
| 10/11 | +128 |

Bit 7 selects whether the address advances after the low or high data port.

## Phase 58 DRAM refresh timing

`ppu_dram_refresh.cpp` locks WRAM refresh:

- Fires at H=134 (~536 master clocks into each scanline)
- Pauses CPU/DMA/HDMA for 40 master clocks
- Once per scanline (~262 times per NTSC frame)

Exposed as `SNES::DRAM_REFRESH_MASTER_CLOCKS`; Phase 19 schedules refresh on the measured sub-dot 8-clock phase grid rather than a fixed PPU dot.

## Phase 59 multi-screen tilemaps

`ppu_tilemap_screens.cpp` locks BGnSC size bits and map layout:

| Size | Dimensions | Screen order |
|------|------------|--------------|
| 0 | 32×32 | [0] |
| 1 | 64×32 | [0][1] |
| 2 | 32×64 | [0] / [1] |
| 3 | 64×64 | [0][1] / [2][3] |

Each screen is 32×32 tiles (0x800 bytes). Map base = `(BGnSC & 0xFC) << 9` (byte address, 2KB steps).

## Phase 60 HDMA timing constants

`ppu_hdma_timing.cpp` locks HDMA/MDMA schedule points and overheads:

| Event | H-counter | Notes |
|-------|-----------|-------|
| HDMA init | 6 (line 0) | `PPU::HDMA_INIT_DOT` |
| HDMA line | 276 | `PPU::HDMA_START_DOT` (H=1104 master clocks) |
| VBlank disable | 0 of VBlank line | stops further line HDMA |

| Overhead | Master clocks |
|----------|---------------|
| HDMA global | 18 |
| HDMA descriptor/data read | 8 |
| MDMA global | 8 |
| MDMA per channel | 8 |
| MDMA per byte | 8 |

## Phase 61 / Phase 2 CPU multiply/divide timing

`cpu_math_timing.cpp` locks the iterative 5A22 ALU:

| Op | Trigger | Duration | Behavior |
|----|---------|----------|----------|
| MUL | write `$4203` | 8 modeled CPU-cycle ALU edges | RDMPY is cleared immediately and updated iteratively |
| DIV | write `$4206` | 16 modeled CPU-cycle ALU edges | RDMPY receives dividend immediately; quotient/remainder evolve iteratively |
| DIV0 | divisor 0 | same | quotient `$FFFF`, remainder = dividend |

Reads of `$4214`–`$4217` can observe intermediate state. A trigger written while
the ALU is already busy performs its immediate register side effect but does not
restart the operation, reproducing the documented overlap-corruption hazard.

## Phase 62 MEMSEL / FastROM timing

`cpu_memsel_timing.cpp` locks `Bus::accessMasterClocks()`:

| Region | Master clocks |
|--------|---------------|
| WRAM $7E/$7F | 8 |
| System bank $0000-$1FFF (WRAM mirror) | 8 |
| $2000-$3FFF (PPU etc.) | 6 |
| $4000-$41FF (joypads) | 12 |
| $4200-$5FFF (CPU MMIO) | 6 |
| Banks $80-$FF ROM, MEMSEL=0 | 8 (SlowROM) |
| Banks $80-$FF ROM, MEMSEL=1 | 6 (FastROM) |
| Banks $00-$3F ROM half | 8 (never FastROM) |

## Phase 63 controller serial protocol

`controller_serial.cpp` locks joypad behavior:

- Strobe (`$4016` bit 0) high: continuous latch; every read returns current B
- Strobe falling edge: freezes a 16-bit snapshot
- Serial order: B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R
- After 16 reads, further reads return `1`
- `$4016`: D1-D0 are controller-port inputs and bits 7-2 are open bus
- `$4017`: D1-D0 are controller-port inputs, bits 4-2 are forced high, bits 7-5 are open bus
- unplugged controller-port input lines read zero; standard pads drive only D0

## Phase 65 master-clock CGRAM phase

`ppu_pipeline_cgram_halfdot.cpp` now checks CGRAM ownership within the same PPU
dot using explicit master-clock phase, including the 3+3 split of 6-clock long
dots. Main/sub selection no longer depends on even/odd dot number.

## Phase 66 native 512-pixel hi-res output

Mode 5, Mode 6, and SETINI pseudo-hires now use a 512-pixel physical row
stride and emit independent sub/main pixels for every visible SNES dot. The
frontend selects the active 256/512 width dynamically.

## Phase 67 cartridge mapping accuracy

`cartridge_mapping_accuracy.cpp` validates base-cartridge address decoding and
header selection before enhancement-chip work begins:

- LoROM, HiROM, and ExHiROM internal headers are scored at `$007FC0`, `$00FFC0`,
  and `$40FFC0` respectively.
- ExHiROM mode `$25` maps the first 4 MiB through `$80-$FF` and the extended
  ROM half through `$00-$7D` while retaining the standard HiROM SRAM windows.
- Non-power-of-two ROM sizes use address-line/mask-ROM mirroring rather than a
  whole-image modulo, covering common 3 MiB and 6 MiB layouts.
- LoROM/HiROM behavior remains regression-covered alongside the new ExHiROM path.

## Phase 68 SPC700 / S-SMP conformance foundation

`apu_spc700_conformance.cpp` locks core S-SMP behavior before deeper DSP work:

- IPL ROM startup reaches the `$AA/$BB` ready handshake and initializes the user-facing stack state.
- Direct-page relocation through PSW.P is respected by load/store and ALU operations.
- ADC/SBC N/V/H/Z/C behavior is regression-covered across signed and nibble carries.
- TSET1/TCLR1 derive N/Z from `A - memory` while independently updating memory bits.
- ADDW/SUBW, DIV, and MUL results and instruction timings are covered.
- CONTROL port-clear writes affect only CPU-to-APU input latches.

`apu_timer_accuracy.cpp` corrects and locks the three-stage S-SMP timer model:

- Stage 1 is free-running even while a timer is disabled.
- A CONTROL 0->1 enable transition resets stages 2/3 but preserves stage-1 phase.
- Stage 2 uses an 8-bit post-increment equality comparator rather than `>=`.
- A target value of `$00` therefore represents 256 stage-1 ticks.
- TnOUT remains a 4-bit read-and-clear counter.


## Phase 69 S-DSP sample-path accuracy

`apu_dsp_accuracy.cpp` begins the DSP accuracy pass while retaining the existing
32-S-SMP-clock sample scheduler:

- BRR decode now follows the hardware half-amplitude predictor path, invalid
  range 13-15 behavior, signed predictor arithmetic, clamp, and final doubling.
- Envelope rates use the shared 30,720-step DSP counter (including the hardware
  rate offsets) rather than per-voice counters that restart at key-on.
- The noise LFSR is clocked by that same global rate counter and exposes its
  signed 15-bit state as the noise sample.
- Main and echo voice accumulation saturate per contribution rather than only at
  final output.
- Echo FIR history uses half-amplitude RAM samples, signed 1.7 coefficients,
  16-bit intermediate wrapping for taps 0-6, final-tap saturation, and even
  output samples.
- EDL=$00 is treated as the hardware's one-stereo-sample (4-byte) echo buffer.


## Phase 70 S-DSP Gaussian interpolation

`apu_dsp_accuracy.cpp` now also locks the hardware interpolation path:

- The exact 512-entry S-DSP Gaussian coefficient ROM replaces linear sample
  interpolation.
- The first three Gaussian products use the DSP's signed intermediate wrap;
  the fourth product is then added with final saturation and the low output bit
  cleared.
- Each voice keeps one decoded BRR block of look-ahead so the four-point
  interpolation window remains valid across compressed-block boundaries.
- BRR END/LOOP state travels with the buffered block rather than being
  overwritten by look-ahead decoding.
- Pitch modulation is suppressed on noise-enabled voices, matching the DSP
  signal path.

Per-DSP-clock register polling, KON/KOFF pipeline latency, and exact BRR group
fetch scheduling remain separate follow-on accuracy work.

## Phase 71 S-DSP key pipeline timing

`apu_dsp_key_pipeline.cpp` adds sample-pipeline coverage for the DSP key-control
path:

- KON is consumed on the DSP's alternating control-poll cadence rather than
  being applied as an unconditional per-sample level.
- A consumed KON starts five generated samples of zero output before BRR,
  pitch, and envelope processing become audible.
- KOFF is treated as a persistent level. When KOFF remains asserted alongside
  KON, the following control poll returns the voice to Release before the
  five-sample startup interval can produce audible output.
- Voice ENDX is cleared when the queued KON is consumed by the DSP pipeline;
  merely writing KON does not synchronously clear the latched ENDX bit.

The key-control behavior above is now integrated with the explicit 32-clock DSP
phase model described in Phase 72.

## Phase 72 S-DSP 32-clock control/register latches

`apu_dsp_phase_latches.cpp` establishes an explicit DSP clock phase (0..31) and
covers global register/control sampling rather than treating a generated sample
as an indivisible operation:

- KON writes feed the hidden new-KON latch; a later unconsumed write replaces
  the earlier value instead of OR-accumulating it.
- PMON is sampled on clock 27; NON/EON/DIR/FLG are sampled on clock 28; ESA is
  sampled on clock 29; KON/KOFF and the global counter/noise path run on clock 30.
- DIR changes after its clock-28 latch cannot redirect a key-on already being
  assembled for that frame.
- ENVX/OUTX remain writable DSP register RAM until their later voice-pipeline
  writeback point replaces the software value.

## Phase 73 S-DSP voice result writeback pipeline

`apu_dsp_writeback_timing.cpp` moves ENDX/OUTX/ENVX visibility out of the
sample-boundary renderer and into the hardware V5..V9 writeback slots:

- Each voice is staggered by three DSP clocks through V5, V6, V7, V8, and V9.
- ENDX is staged at V5 and published at V7; both BRR-end setting and KON
  clearing therefore have the correct delayed visibility.
- OUTX is captured at V6 and written at V8.
- ENVX is captured at V7 and written at V9.
- Existing DSP tests now distinguish internal sample completion from public
  register visibility instead of assuming all result registers update together
  at clock 31.

Exact V1..V4 source-directory/BRR byte fetch slots and group-of-four BRR decode
scheduling remain the next DSP timing work.

## Phase 74 S-DSP V1/V2 source-directory fetch timing

`apu_dsp_source_fetch_timing.cpp` moves key-on source lookup out of the
sample-boundary KON handler and into explicit per-voice V1/V2 pipeline slots:

- V1 latches each voice's SRCN and derives its source-directory address from
  the DIR value already sampled by the global DSP pipeline.
- V2 performs the ARAM start/loop pointer reads several clocks later.
- A SRCN write after V1 is too late to redirect that V1/V2 source fetch.
- Directory bytes remain live until V2, so ARAM writes between V1 and V2 are
  visible to the pending pointer read.
- The first V2 source pointer captured during KON setup is retained for that
  key-on instead of being retroactively replaced by later source-register
  activity.
- BRR block decoding is deferred until the voice actually needs sample data,
  removing the previous eager full-block decode from the KON write path.

Exact V3/V4 BRR header/data-byte fetch slots and four-sample decode scheduling
remain the next DSP timing step.

## Phase 75 S-DSP V3/V4 BRR fetch/decode timing

`apu_dsp_brr_fetch_timing.cpp` moves BRR data visibility onto explicit V3/V4
voice slots instead of decoding complete blocks synchronously:

- V3 latches the current BRR header and the first byte of a two-byte data group.
- V4 reads the second byte and commits the corresponding four decoded samples.
- ARAM writes after V3 cannot change the latched header/first byte, while a
  write to the second byte before V4 is still visible.
- Four V3/V4 groups complete one 16-sample BRR block; END/LOOP redirection is
  applied only when the final group completes.
- The decoded look-ahead half is held until the playback window promotes it,
  preventing later BRR blocks from overwriting samples that Gaussian
  interpolation has not consumed yet.

The remaining S-DSP timing work is deeper integration of V3a/V3c pitch,
envelope, and output arithmetic with the same per-clock voice pipeline, plus
cycle-timed echo RAM/FIR scheduling.

## Phase 76 S-DSP pitch ceiling and BRR-boundary advancement

`apu_dsp_pitch_timing.cpp` tightens the coupling between pitch modulation,
interpolation phase, and BRR-window promotion:

- Pitch modulation is saturated to the S-DSP's 14-bit `$3FFF` maximum rather
  than allowing the effective rate to rise toward `$7FFF`.
- A maximum positively modulated voice therefore advances at most three whole
  decoded samples plus `$FFF` fractional phase in one DSP sample.
- The same limit is verified while crossing a 16-sample BRR boundary so the
  look-ahead window is promoted exactly once instead of being skipped deeper
  into the following block.
- Noise voices continue to suppress pitch modulation and retain their base
  pitch even when PMON is enabled.

The remaining S-DSP timing work is deeper V3a/V3c envelope/output arithmetic
placement and cycle-timed echo RAM/FIR scheduling.

## Phase 77 S-DSP V2/V3a pitch-register latch timing

`apu_dsp_pitch_latch_timing.cpp` moves pitch-register visibility onto the
hardware voice pipeline instead of reading both bytes atomically at the output
sample boundary:

- V2 latches PITCHL for each voice.
- V3a latches PITCHH one clock later (with voice 0 using its split phase-22
  slot and voices 1-7 following the normal three-clock stagger).
- A PITCHL write after V2 is too late for the current voice pass, while a
  PITCHH write before V3a is still visible.
- Writes that miss either latch are picked up on the next voice pipeline pass.
- Pitch modulation and interpolation advancement consume the completed 14-bit
  latch rather than re-reading the live DSP register pair at sample end.

The remaining S-DSP timing work is moving V3c interpolation/envelope/output
calculation and V4/V5 voice mixing onto their exact per-clock slots, followed
by cycle-timed echo RAM/FIR scheduling.

## Phase 78 S-DSP V3c/V4 sample-generation timing

`apu_dsp_v3c_timing.cpp` moves voice sample generation onto the hardware V3c/V4
slots instead of completing all voice arithmetic at the end of the 32-clock
sample:

- Gaussian/noise selection, PMON, envelope multiplication, KON/KOFF handling,
  and envelope advancement occur at each voice's staggered V3c slot.
- V4 advances the interpolation/pitch position on the following clock.
- The current envelope produces the current sample; envelope advancement
  prepares the next sample rather than brightening the sample in flight.
- KON setup latency is measured from the DSP's clock-30 control consumption,
  preserving the five zero-output voice passes before audible processing.

## Phase 79 S-DSP V4/V5 mix and echo clock pipeline

`apu_dsp_echo_pipeline_timing.cpp` moves stereo voice accumulation and the echo
engine onto their hardware clock slots:

- Each voice contributes left output at V4 and right output at V5, so volume
  writes between those slots can affect only the channel not yet consumed.
- EON-controlled echo accumulation occurs alongside those V4/V5 outputs.
- Echo RAM/FIR processing is split across clocks 22 through 30: left read at
  22, right read at 23, FIR accumulation through 25, output calculation at
  26/27, FLG sampling at 28/29, and left/right echo writes at 29/30.
- DAC samples become visible at clock 27 instead of being deferred to clock 31.
- ESA affects the echo pointer on the following sample because it is latched at
  clock 29 after the current sample's pointer was selected at clock 22.
- FIR history order now matches hardware: FIR0 addresses the oldest history
  point and FIR7 the newest.
- FLG echo-write disable is re-sampled between left and right writes, allowing
  a clock-29 change to suppress the right write without retroactively
  cancelling the left write.

The next DSP timing work is exact ADSR0/GAIN register latching and the remaining
V2/V3 envelope-control visibility quirks, plus soft-reset/end-of-sample edge
cases that interact with the staged BRR header path.

## Phase 80 S-DSP register arbitration and FLG soft-reset timing

`apu_dsp_register_arbitration.cpp` tightens software-write arbitration against
shared DSP pipeline latches and makes FLG.7 a per-voice V3c control rather than
an immediate host-side shutdown:

- Software writes to ENVX and OUTX also replace the shared V7/V6 writeback
  latches, so writes shortly before V9/V8 can win over an older staged value.
- Any ENDX write clears both the visible register and the hidden V5->V7 ENDX
  latch; a stale staged ENDX value can no longer reappear after a software
  clear.
- FLG bit 7 is sampled independently at each voice's V3c slot. It forces the
  envelope to zero and Release there instead of deactivating every voice at the
  instant the S-SMP writes FLG.
- Voices whose envelope has released to zero remain in the BRR/pitch pipeline,
  allowing decoding and ENDX updates to continue while inaudible or while
  FLG.7 remains asserted.
- `apu_dsp_v3c_timing.cpp` now explicitly clears reset/mute before its private
  envelope-order test so it does not rely on the old immediate-reset behavior.

The next bounded DSP timing step is ADSR0/ADSR1/GAIN visibility and the hidden
envelope state used by bent-increase GAIN, followed by remaining BRR end/header
edge cases.

## Phase 81 S-DSP envelope-control latch timing

`apu_dsp_envelope_latch.cpp` tightens the V2/V3c envelope-control path:

- ADSR0 (`$x5`) is latched at each voice's V2 slot and that held value selects
  ADSR/GAIN behavior when the same voice reaches V3c.
- ADSR1 (`$x6`) and GAIN (`$x7`) remain live at V3c, matching the DSP's
  temporary-register ordering rather than treating all three envelope
  registers as one atomic read.
- Bent-line GAIN mode 7 now uses the DSP's hidden pre-clamp envelope state for
  its `$600` slope transition. The hidden state is updated on every envelope
  computation even when the shared rate counter does not commit the result to
  visible ENV.
- Release remains an unconditional `-$08` step and continues to run without
  rate-counter gating.

The next bounded DSP timing work is the BRR END-without-LOOP/header interaction
at V3c/V4 and remaining KON source/loop edge cases.

## Phase 82 S-DSP BRR END-only V3b/V3c timing

`apu_dsp_brr_end_timing.cpp` models the immediate-silence rule for a BRR
header with END=1 and LOOP=0:

- V3c uses the BRR header already latched at V3b; ARAM changes between the two
  slots cannot retroactively change the current END-only decision.
- The current interpolated sample is multiplied by the envelope first, then an
  END-only header forces the voice to ENV=0/Release for subsequent output.
- Envelope advancement now occurs after FLG/END immediate-silence handling and
  KOFF/KON, matching the hardware V3c ordering; a fresh KON suppresses that
  envelope step while the last KON setup pass resumes it.

The next bounded DSP work is exact ENDX assertion at the V4 block-wrap event
without making speculative look-ahead decoding publish ENDX too early.

## Phase 83 S-DSP source-directory and KON BRR pipeline timing

`apu_dsp_directory_pipeline.cpp` and the updated BRR timing coverage tighten
source selection and startup decoding around V1/V2/V4:

- V1 now uses the SRCN value latched by the previous V1 to form the shared
  directory address, then latches the current voice's SRCN for the next V1.
  This reproduces the DSP's cross-voice temporary-register dependency.
- During normal playback V2 rereads the source directory's loop pointer rather
  than freezing it at KON, so software can redirect a later END+LOOP block by
  changing the directory before the relevant V2 fetch.
- KON startup now suppresses V4 BRR decode on the first and final setup passes
  and allows it on the middle three passes.
- Eligible V4 slots decode four samples immediately instead of waiting for all
  eight BRR data bytes. The three middle KON passes therefore prefill the
  12-sample interpolation window without adding an extra audible-delay sample.
- `apu_dsp_brr_fetch_timing.cpp` now explicitly checks this startup gate while
  preserving V3b/V4 ARAM visibility and END+LOOP redirection behavior.

The next bounded DSP timing work is ENDX assertion at the actual V4 block-wrap
point and its V5/V7 writeback interaction, rather than deriving ENDX from a
later interpolation-index rollover.

## Phase 85 S-DSP reset/KON polling cadence

`apu_dsp_reset_cadence.cpp` fixes the hidden every-other-sample control phase
at DSP reset:

- The reset state begins with `every_other_sample` set, matching the accurate
  S-DSP pipeline's soft-reset state.
- Clock 29 toggles that state before clock 30 samples KON/KOFF, so the first
  phase-30 after reset deliberately does **not** consume a pending KON/KOFF.
- The second phase-30 poll consumes the pending control values, preventing a
  post-reset KON from starting one DSP sample too early.
- The regression also locks FLG=$E0, the $4000 noise seed, counter phase zero,
  and the subsequent hidden KON-clear cadence.

The next bounded APU work should move from DSP output timing into remaining
S-SMP MMIO/cycle edge cases (notably TEST $F0 and register-area execution).

## Phase 86 S-SMP TEST ($F0) core behavior

`apu_test_register.cpp` implements the well-established, compatibility-relevant
parts of the undocumented TEST register without attempting the hazardous CPU
speed/unknown modes:

- Reset starts TEST at `$0A`, the normal operating value.
- Clearing bit 1 blocks ordinary ARAM writes while `$F0-$FF` register-window
  writes still reach the underlying RAM and their MMIO side effects.
- The same RAM-write gate suppresses S-DSP echo-buffer writes.
- Timer chains run only when TEST.T/t are in the normal `1/0` combination.
- TEST hardware writes are ignored while the SPC700 P flag is set.

The high-nibble CPU-speed modes and undocumented TEST bits remain intentionally
unmodeled until a cycle-domain refactor can represent them safely.

## Phase 87 S-SMP register-area / echo-RAM arbitration

`apu_register_area_echo.cpp` covers the special `$F8/$F9` RAM bytes in the
S-SMP register window:

- `$F8/$F9` remain ordinary SPC700-readable/writable RAM bytes.
- S-DSP echo-buffer writes cannot alter `$F8/$F9`, even when echo writes and
  normal ARAM writes are enabled.
- The protection is specific to the S-DSP echo writer; SPC700 access semantics
  for the `$F0-$FF` MMIO overlay are unchanged.

The next S-SMP accuracy work should tackle memory-access timing/open-bus effects
and TEST wait-state modes only after the CPU core exposes per-access bus cycles.

## Phase 88 S-SMP DSPADDR read-protect semantics

`apu_dspaddr_readonly.cpp` covers the high bit of `$F2` (`DSPADDR`):

- Bit 7 is a write-only read-protect flag and always reads back as zero.
- The low seven bits continue to select the DSP register for `$F3` reads.
- With bit 7 set, `$F3` writes are inhibited without disturbing the selected
  DSP register or its readable value.
- Clearing bit 7 re-enables writes to the same low-seven-bit address.

This keeps the existing DSP register-memory behavior while matching the
S-SMP's documented `$F2/$F3` access semantics.

## Phase 89 headless ROM regression runner

`src/headless/main.cpp` adds a frontend-independent C++17 runner for external ROM
suites. The root `CMakeLists.txt` builds the core as `snesultimate_core` and the
runner as `snesultimate_headless`, without SDL or ImGui.

The runner supports deterministic frame/master-clock execution, visible
framebuffer hashing, PPM framebuffer dumps, raw 128 KiB WRAM dumps, CPU cycle
trace CSV output, expected-hash pass/fail exit status, and recursive batch runs.
SRAM persistence is disabled by default in the headless runner so one test run
cannot contaminate another through a neighboring `.srm` file; pass
`--persist-sram` when persistence is intentionally part of a test.

Typical use:

```
cmake -S . -B build_headless -DCMAKE_BUILD_TYPE=Release
cmake --build build_headless --config Release
build_headless/snesultimate_headless --frames 120 --json test.sfc
build_headless/snesultimate_headless --frames 120 --dump-frame final.ppm --dump-wram final.wram test.sfc
build_headless/snesultimate_headless --frames 120 --trace cpu.csv --trace-limit 10000 test.sfc
build_headless/snesultimate_headless --frames 2 --json --batch /path/to/snes-test-roms
```

`tests/headless_runner_smoke.py` verifies deterministic hashing and the dump/trace
artifacts using any supplied test ROM.

## Phase 1 independent accuracy harness

Phase 1 turns the existing ad-hoc external checks into opt-in CTest targets while
keeping third-party corpora outside the repository.

CMake cache variables:

- `SNESULTIMATE_65816_TESTS=/path/to/SingleStepTests/65816/v1` builds and registers
  the complete JSON single-step oracle as `external_65816_singlestep`.
- `SNESULTIMATE_TEST_ROM=/path/to/test.sfc` registers `headless_determinism_smoke`,
  which executes the same ROM twice and requires identical framebuffer and audio
  hashes, identical audio sample counts, valid framebuffer/WRAM/PCM dumps, and a
  bounded CPU trace.
- `SNESULTIMATE_REGRESSION_MANIFEST=/path/to/manifest.json` registers a manifest-
  driven ROM suite as `external_rom_regressions`.

`cpu_singlestep_bulk` is now always built even if no external corpus is configured.
Directory inputs are searched recursively, sorted, and deduplicated so either the
repository root or its `v1` directory can be supplied.

The headless runner now reports `audio_hash` and `audio_frames`. Audio hashing uses
64-bit FNV-1a over every generated interleaved signed-16-bit stereo sample in
little-endian byte order. `--dump-audio PATH` writes the exact hashed PCM stream,
and `--expect-audio-hash HEX` makes a mismatch fail the process. Audio is drained
once per frame (or after a master-clock run), preventing the bounded frontend audio
queue from silently discarding early samples during long regressions.

`tests/external_regression.py` consumes a JSON manifest and supports deterministic
repeat runs, per-run timeouts, framebuffer/audio golden hashes, and the headless
WRAM/SPC pass/fail oracles. See `tests/external_regression.example.json` for the
schema. ROM paths are relative to the manifest unless absolute.

Example configuration:

```
cmake -S . -B build-accuracy -DCMAKE_BUILD_TYPE=Release \
  -DSNESULTIMATE_65816_TESTS=/data/65816/v1 \
  -DSNESULTIMATE_REGRESSION_MANIFEST=/data/snes-regressions.json
cmake --build build-accuracy --target cpu_singlestep_bulk snesultimate_headless
ctest --test-dir build-accuracy -L external --output-on-failure
```

The upstream 65816 vectors intentionally remain external. For redistributable ROM
regressions, prefer suites with an explicit compatible license (for example the
MIT-licensed gilyon SNES CPU/SPC tests), or point the manifest at locally held test
ROMs without copying them into this tree.

## Phase 1B external oracle matrix

Phase 1B adds two independent CPU-oracle paths and machine-readable reporting.
Neither external corpus is vendored by the project.

### Deterministic 65816 spread sampling

`cpu_singlestep_bulk` now supports two distinct reduced-corpus modes:

- `--limit N` executes the first `N` vectors in each JSON file. Use this only
  for very fast smoke tests.
- `--sample N` parses each complete opcode file and deterministically selects
  `N` vectors using a stable hash of the filename and upstream case name. This
  is slower to parse than `--limit`, but avoids permanently biasing CI toward
  the first generated cases in every opcode file.

`--report-json PATH` writes a per-file matrix containing parsed/selected/pass
counts plus aggregate state, RAM, cycle-count, cycle-kind, cycle-address, and
cycle-value failure totals. Example:

```
cpu_singlestep_bulk --sample 64 --max-failures 50 \
  --report-json phase1-65816.json /path/to/65816/v1
```

The 65816 corpus currently has no clear upstream license. Keep it outside this
repository and supply its path through `SNESULTIMATE_65816_TESTS` or invoke the
runner manually.

### SPC700 SingleStepTests bridge

`spc700_singlestep_bulk.cpp` consumes the published SingleStepTests SPC700 v1
format (1,000 generated vectors per opcode in the archived ProcessorTests
corpus). It runs the S-SMP CPU against a test-only flat 64 KiB address space
and checks:

- complete SPC700 register state (`PC`, `A`, `X`, `Y`, `SP`, `PSW`)
- final RAM bytes listed by the oracle
- total instruction cycle count, including oracle `wait` cycles
- ordered non-wait bus reads/writes, addresses, and values

The current APU executes an instruction atomically rather than exposing each
internal wait cycle as a separate micro-step. Therefore the runner validates
where every read/write occurs and validates the *total* cycle count, but it
cannot yet prove the exact placement of `wait` cycles between bus accesses.
That remaining limitation is explicit rather than silently ignored.

Examples:

```
spc700_singlestep_bulk --limit 100 /path/to/ProcessorTests/spc700/v1/00.json
spc700_singlestep_bulk --sample 64 /path/to/ProcessorTests/spc700/v1
```

Set `SNESULTIMATE_SPC700_TESTS` at configure time to register the complete
external corpus with CTest:

```
cmake -S . -B build \
  -DSNESULTIMATE_SPC700_TESTS=/path/to/ProcessorTests/spc700/v1
```

The SPC700 test hooks (`APU::enableFlatTestMemory`, state injection, and bus
trace capture) default to disabled and do not change normal console execution.
The first published TCALL vector exposed a missing dummy program-space read;
that bus-visible behavior is now implemented while preserving the instruction's
existing final-state semantics.

### External ROM regression reports

`external_regression.py` accepts `--report-json PATH`. The report records every
manifest case, deterministic A/V hashes and timing counters for passes, and a
structured reason/result payload for failures (timeout, bad JSON, status
mismatch, nondeterminism, runner failure, or missing ROM). This is intended to
be the persistent Phase 1 failure matrix used before and after core fixes.

## Phase 1C corpus triage and failure matrices

The bulk 65816 and SPC700 runners support deterministic corpus triage without
requiring every local CTest run to execute millions of vectors.

- `--sample N` selects a stable spread of vectors from every opcode JSON file.
- `--report-json PATH` writes machine-readable totals plus per-file failure
  categories, so opcode/mode hotspots can be sorted without scraping console
  output.
- `SNESULTIMATE_EXTERNAL_SAMPLE` controls the per-file CTest sample size
  (default 100). Set it to `0` to run every supplied vector.
- `SNESULTIMATE_EXTERNAL_REPORT_DIR` optionally captures `65816-report.json`
  and `spc700-report.json` from CTest runs.

Example sampled configuration:

```
cmake -S . -B build-accuracy \
  -DSNESULTIMATE_65816_TESTS=/path/to/65816/v1 \
  -DSNESULTIMATE_SPC700_TESTS=/path/to/ProcessorTests/spc700/v1 \
  -DSNESULTIMATE_EXTERNAL_SAMPLE=250 \
  -DSNESULTIMATE_EXTERNAL_REPORT_DIR=/tmp/snes-oracle-reports
ctest --test-dir build-accuracy -L external --output-on-failure
```

For an exhaustive validation pass, reconfigure with
`-DSNESULTIMATE_EXTERNAL_SAMPLE=0`. The corpora remain external and are not
copied into this repository.


## Phase 1D ROM-level CPU/SPC conformance diagnostics

The headless runner accepts repeatable `--watch-wram-u16 OFFSET` probes. Each
probe is sampled while the ROM runs and is emitted in JSON as
`wram_u16_watches`, including the final little-endian value and the number of
observed changes. This is diagnostic-only and does not alter emulation state.

This is useful for gilyon/snes-tests because its 65C816 ROM keeps the current
16-bit `test_num` at WRAM offset `$0010`; the SPC test host keeps
`last_test_num` at WRAM offset `$0004`. The external manifest runner can map a
probe back to the suite's autogenerated `tests*.txt` catalog with:

```json
{
  "test_number_watch_offset": "0x10",
  "test_catalog": "external-roms/tests-full.txt",
  "require_catalog_complete": true
}
```

Catalog lines use the suite's `Test XXXX:` numbering. When a run stops before
the final catalog entry, `external_regression.py` reports `catalog-incomplete`
and includes the exact test number and description in the JSON report. This
turns a generic timeout/hang into an actionable CPU test location.

The SPC ROM has a stronger machine protocol and should use it directly:

```json
{
  "oracle_spc_port": "0:1:2",
  "oracle_spc_after_ipl": true,
  "stop_on_oracle": true,
  "require_pass": true,
  "status": "pass",
  "test_number_watch_offset": "0x4",
  "test_catalog": "external-roms/tests.txt"
}
```

In the upstream SPC test protocol, SPC port 0 is `1` after all tests pass and
`2` when a test fails; ports 2/3 carry the test number. The WRAM watch is kept
as a stable host-side diagnostic even after the port handshake changes.

## Phase 1E: merged accuracy defect queue

`accuracy_defect_queue.py` merges the machine-readable 65816 SingleStep,
SPC700 SingleStep, and external-ROM regression reports into one prioritized
queue. It does not invent pass/fail data: missing report files are simply
omitted from the queue.

Manual use:

```sh
python tests/accuracy_defect_queue.py \
  --65816-report build/reports/65816-report.json \
  --spc700-report build/reports/spc700-report.json \
  --rom-report build/reports/rom-report.json \
  --output-json build/triage/accuracy-defect-queue.json \
  --output-md build/triage/accuracy-defect-queue.md
```

Single-step defects are grouped per opcode JSON file and ranked primarily by
failure class: architectural state/RAM mismatches outrank cycle/bus-only
mismatches. ROM-conformance failures retain their manifest case name, failure
classification, diagnostic test number, and catalog description when present.
The JSON form is intended for tooling; the Markdown form is intended as the
human repair queue.

CMake can generate the reports and queue in one configured build:

```sh
cmake -S . -B build-accuracy \
  -DSNESULTIMATE_65816_TESTS=/path/to/65816/v1 \
  -DSNESULTIMATE_SPC700_TESTS=/path/to/spc700/v1 \
  -DSNESULTIMATE_REGRESSION_MANIFEST=/path/to/regressions.json \
  -DSNESULTIMATE_EXTERNAL_REPORT_DIR="$PWD/build-accuracy/reports" \
  -DSNESULTIMATE_ACCURACY_QUEUE_DIR="$PWD/build-accuracy/triage"
ctest --test-dir build-accuracy -L external --output-on-failure
```

`accuracy_defect_queue` is ordered after the configured external oracle tests,
so it consumes the reports they produced. `accuracy_defect_queue_smoke` is a
small self-contained test of the triage logic and requires no external ROMs or
corpora.

## Phase 1F: SPC700 bus-visible dummy fetches

Hardware timing measurements show that cycle 2 of every one-byte SPC700
instruction is a real read of the byte following the opcode, rather than a
purely internal idle cycle. `DBNZ Y,rel` is the two-byte exception: it performs
a dummy read of the relative operand before fetching the same operand for real.
These reads matter when execution reaches the SPC700 I/O page because reads can
have side effects, and they are visible to SingleStepTests-style bus oracles.

`apu_spc700_conformance` now checks the complete documented one-byte opcode set,
plus the double-read ordering of `DBNZ Y,rel`. The normal instruction cycle
counts are unchanged; this phase corrects which cycles are externally visible
as memory reads.

### Phase 1G: SPC700 multi-byte bus sequencing

The SPC700 conformance regression also checks the exceptional `MOV (X)+,A`
store sequence. Hardware timing research shows that this opcode performs a
real destination read before the write, but that read bypasses the S-SMP
internal `$F0-$FF` register overlay. The core therefore models the cycle as a
backing-ARAM read: it is visible to the external bus oracle without causing
internal-register read side effects. This is intentionally different from the
ordinary MOV-to-memory dummy reads, which can trigger timer-output read side
effects.

### Phase 1H SPC700 addressing-mode bus sequencing

The SPC700 conformance regression now locks down two logic-analyzer-confirmed bus details that are easy to miss in state-only tests. `[dp]+Y` reads the direct-page pointer low byte, reads its high byte, re-reads that high-byte address during the index-add cycle, and only then accesses the effective address. `MOVW YA,dp` similarly re-reads the direct-page low byte on its extra cycle, while `ADDW`/`SUBW` use an internal cycle there and must not gain the extra bus access. These tests intentionally validate ordered bus accesses as well as final architectural state.

### Phase 1I: SPC700 direct-relative and memory-to-memory bus ordering

The SPC700 conformance regression also checks hardware-visible ordering for the
remaining direct-relative and memory-to-memory families:

- `BBS`/`BBC` and `CBNE` read direct-page data before fetching the relative
  displacement; their intervening hardware cycles are internal and must not
  trigger MMIO read side effects.
- `DBNZ dp,rel` performs the direct-page read/write before fetching `rel`.
- `CBNE dp+X,rel` likewise defers its displacement fetch until after the indexed
  direct-page read.
- ALU `dp,dp` operations read the encoded source direct-page byte before
  fetching and reading the destination operand.
- `MOV dp,dp` uses the same source-read-before-destination-fetch order.
- `(X),(Y)` ALU operations read the Y source before the X destination
  read/modify/write cycle.

These checks model only real read/write side effects in the flat-memory trace;
internal S-SMP cycles still contribute to the instruction cycle count but are
not emitted as ordinary MMIO reads.

### Phase 1J: SPC700 word-RMW and absolute bit-test bus ordering

The SPC700 conformance regression now locks down the remaining high-confidence
word/store sequencing details from the bus audit:

- `INCW dp` and `DECW dp` are byte-serial RMW operations.  They read the low
  byte, write the adjusted low byte, then read the high byte and finally write
  the adjusted high byte.  Reading both bytes before either write is incorrect
  and can change behavior when a direct-page byte aliases S-SMP MMIO.
- `TSET1 !abs` and `TCLR1 !abs` read the absolute destination twice before the
  final write.  Flags and the modified value are derived from the first read;
  the second read is nevertheless a real bus access.
- `MOVW dp,YA` remains the store-word exception documented by Anomie/higan: it
  reads only the low destination byte, then writes low and high.  No high-byte
  destination read is inserted.
- `[dp+X]` indirect addressing keeps its index-formation cycle internal.  The
  direct-page pointer low/high bytes are each read once before the effective
  load, or before the destination read/write pair for stores.

These checks are bus-order regressions in addition to architectural-state
checks, because state-only tests cannot detect reordered reads/writes that have
identical final RAM contents.


### Phase 1K: SPC700 stack/control-flow bus sequencing

The conformance regression now locks the observable bus order for the SPC700
stack and control-flow family against current higan-style sequencing: `PUSH`,
`POP`, `BRK`, `CALL`, `PCALL`, `TCALL`, `RET`, `RETI`, and `JMP [!abs+X]`.
The existing atomic SPC700 core represents instruction elapsed time as a total
cycle count, so internal/idle cycles are deliberately not emitted as memory
accesses. This phase therefore does not guess at historically disputed idle
cycle placement; it verifies only independently supported read/write ordering,
stack byte order, return addresses, vector reads, and BRK's pre-update PSW push.

No SPC700 core change was required by this audit: the externally visible access
order already matched the reference behavior. These tests prevent later
cycle-level refactors from silently regressing that ordering.

### Phase 1L — 65C816 interrupt/control-flow lock tests

`cpu_edge_case_accuracy` now also locks the externally visible behavior of
BRK/COP, IRQ/NMI, RTI, WAI, and STP in both emulation/native contexts. It
checks software-vs-hardware status stacking, distinct native/emulation vectors,
PBR stacking/restoration, D/I entry behavior, NMI priority, the SNES WAI
12-master-clock (two I/O-cycle) wake interval, masked-IRQ inline resume after
WAI, and STP remaining halted until reset. This phase intentionally makes no
CPU-core change: the audited behavior already matches the available WDC/SNES
timing references, so these cases are regression locks for later CPU work.

### Phase 1M: 65C816 direct-page indexed-indirect wrapping

The 65C816 addressing regression now distinguishes the two emulation-mode
`(dp,X)` pointer-wrap cases defined by WDC direct-addressing caveats.  When
`DL == 0`, ordinary direct-page pointer reads wrap within the selected 256-byte
page.  When `DL != 0`, direct addressing instead spans the full bank-00
`$0000-$FFFF` range, so a `(dp,X)` pointer whose low byte lands at `$xxFF`
reads its high byte from `$xx+1:00` rather than wrapping back to `$xx00`.
Both the microcoded cycle-stepping path and the atomic/fallback addressing
helper use the same `directPointerAddress()` rule so they cannot diverge.

### Phase 1N — 65C816 indexed throwaway-read accuracy

The cycle-stepped CPU now models the W65C816 indexed-address correction cycle as a
real bus read instead of an anonymous internal cycle where the processor drives a
data address. Coverage includes:

- absolute,X / absolute,Y reads when the page-cross or 16-bit-index penalty applies;
- absolute indexed stores, which always perform the pre-carry throwaway read;
- direct-page indirect indexed-Y (`(dp),Y`) reads when the penalty applies;
- `(dp),Y` stores, which always perform the throwaway read before the write; and
- absolute,X read-modify-write instructions, which always perform the indexed
  throwaway read before the actual operand read.

The pre-carry address is formed by adding the index high byte to the base high
byte while discarding the carry from the low-byte addition. This preserves the
bus-visible incorrect-address access before the corrected effective address is
used. `cpu_addressing_trace_matrix` and `cpu_addressing_cycle_order` contain the
focused regression traces.


### Phase 1O — 65C816 bus-visible penalty/RMW cycles

The CPU cycle regressions now lock three bus-visible behaviors that must not be represented as anonymous internal time: direct-page low-byte/index formation penalties repeatedly read the instruction operand byte; taken branches read the next sequential PC and, for emulation-mode page crossings, the wrong-page target; and memory RMW uses the 6502-compatible old-value dummy write in emulation mode versus a dummy read in native mode (the high byte for 16-bit RMW). These cases are intended to catch MMIO-visible ordering regressions in addition to final-state errors.


### Phase 1P — 65C816 invalid-bus IO classification

This audit distinguishes W65C816 cycles that place an address on the pins from
cycles that actually qualify a memory transfer. SingleStepTests encodes VDA,
VPA, and VPB separately; when none are asserted the runner intentionally maps
the cycle to `PendingCycleInfo::Kind::Internal`, even if R/W is high and a
program/data address is present on the external pins.

The regression locks added here verify that these classes remain invalid-bus
IO rather than being promoted to normal Bus::read() calls:

- stack-relative `d,S` address formation;
- both extra cycles in stack-relative-indirect-indexed `(d,S),Y`;
- the equivalent `(d,S),Y` store sequence;
- REP/SEP's third cycle after the mask fetch;
- implied/register-only second cycles; and
- PER's pre-push IO cycle.

This matters because treating these as ordinary reads would incorrectly invoke
SNES MMIO or cartridge side effects. The audit also checked direct-long and
stack push/pull families; their existing qualified-transfer versus internal-IO
classification remains unchanged. No CPU-core change was required in this
phase.

### Phase 1Q — 65C816 jump/subroutine and block-move sequencing

This audit locks the cycle-visible ordering of JSL, `JSR (abs,X)`, RTL, and
MVN/MVP against the W65C816 bus tables and reference 65816 microcode. JSL and
`JSR (abs,X)` keep their documented invalid-bus IO slots between operand,
stack, and indirect-vector transfers; RTL also retains the WDC emulation-mode
multi-byte stack exception, including pulls that can cross above page 1.

The substantive CPU fix is MVN/MVP architectural commit timing. A block-move
byte performs source read, destination write, then two invalid-bus IO cycles at
the destination address. DBR, X, Y, A, and the repeat-PC decision now update
only after those two IO cycles (end of cycle 7), rather than immediately after
the destination write (cycle 5). This preserves instruction-final behavior
while making cycle-stepped state agree with the documented micro-operation
ordering and interrupt/debugger observation points.

### Phase 1R — MVN/MVP interrupt and restart boundaries

The block-move regression now verifies the instruction's unusual interrupt
boundary explicitly. MVN/MVP remains indivisible for each seven-cycle byte
transfer: IRQ/NMI requests that arrive during the source/write/post-write IO
sequence are held until the byte commits. If the accumulator has not
underflowed, the committed block-move iteration rewinds PC to the opcode before
interrupt entry, so the hardware interrupt frame contains the MVN/MVP opcode
address and RTI restarts at the next byte. If the transferred byte is the final
one, PC is not rewound and interrupt return resumes at the following
instruction.

The tests cover both NMI and an enabled maskable IRQ asserted during a transfer,
check the native-mode stacked return PC, and prove that the remaining byte is
copied exactly once after RTI. No CPU-core change is required: the current
cycle queue already defers interrupt recognition until the pending block-move
cycles finish and samples the committed PC at the next instruction boundary.
ABORT is intentionally outside this SNES-specific test path; the 5A22 vector is
unused by normal S-CPU operation.

### Phase 1S — interrupt sampling across DMA/HDMA stalls

`cpu_edge_case_accuracy` now locks the W65C816 old-I interrupt polling rule at
cycle granularity for CLI, SEI, PLP, REP, and SEP: an IRQ asserted after the
instruction has begun but before its final status commit is accepted or delayed
according to the pre-instruction I value.

`cpu_dma_interrupt_boundary` exercises the same rule through the whole-console
scheduler. MDMA must freeze the S-CPU without destroying the sampled-I state,
and an NMI asserted while HDMA owns the bus must remain latched until the CPU can
resume at an instruction boundary. DMA/HDMA never consumes a pending CPU
interrupt by itself.

### Phase 1T interrupt-source latch coverage

`cpu_irq_timer_register_reschedule` now also locks the CPU-I/O interrupt-source semantics around `$4200/$4210/$4211`: RDNMI is latched independently of NMI enable and can trigger an NMI when NMITIMEN bit 7 is enabled during VBlank; acknowledging RDNMI prevents re-triggering the old VBlank source; TIMEUP reads acknowledge only the internal H/V timer source; and `$4211` bit 7 remains set while an external cartridge/expansion IRQ is asserted. This preserves the distinction between the 5A22 timer IRQ latch and the wired external `/IRQ` source.


### Phase 1U: H/V IRQ comparator-active TIMEUP acknowledge window

`cpu_irq_timer_register_reschedule` now covers the hardware edge case where the H/V timer comparator is still active when `$4211` is read. The internal TIMEUP source remains asserted for the measured minimum four-master-clock comparator window after the IRQ edge: reads during that interval return bit 7 set but do not acknowledge the timer source. Once the active window has elapsed, a `$4211` read clears the latched internal timer IRQ normally. The timer-active deadline is serialized with save states, and the SNES coordinator now stamps the bus with the exact master clock before raising a scheduled timer edge so same-cycle reads are evaluated against the correct time.


### Phase 1V: live H/V IRQ comparator writes

The IRQ timing model now distinguishes TIMEUP from the CPU-facing timer IRQ line: TIMEUP rises one 4-master-clock circuit tick before `/IRQ`. `$4207-$420A` writes re-evaluate the currently-held H/V comparator state and can arm TIMEUP at the next circuit tick; `$4200` enables are sampled at the next circuit tick, matching the level+edge behavior measured by the byuu/KungFuFurby IRQ ROMs and the Mesen-derived circuit. H-enabled `HTIME=0` also uses the extra countdown tick instead of sharing the V-only timing point. `cpu_irq_timer_register_reschedule` locks the delayed IRQ-line propagation and mid-line VIRQ enable/VTIME-rewrite cases.

### Phase 1PW: Pilotwings / DSP-1 projection accuracy

The DSP-1 projection pipeline used by Pilotwings now keeps a shared fixed-point camera state across Parameter (`$02`), Raster (`$0A/$1A`), Project (`$06`) and Target (`$0E`).  Parameter applies the documented zenith-angle clipping boundary, projection-centre horizon adjustment, VOF correction and clipped vertical offset before Raster/Target consume the state.  Raster uses the original zenith sine together with the clipped/corrected cosine state, matching the DSP-1 command relationship instead of recomputing an independent floating-point camera for every command.  Project uses the original projection normal and ground point established by Parameter.

`dsp1_accuracy` includes a Pilotwings-critical 45-degree Parameter vector that verifies the projection centre moves along the horizon (the old HLE incorrectly returned the original Fx/Fy), a steep-zenith clipping vector that requires non-zero VOF, and streamed Raster checks after clipping.  DSP-1 remains classified as `PartialHLE`: this phase targets the projection commands visible in the supplied Pilotwings capture; the remaining DSP-1 math commands and exact internal ROM lookup/truncation behavior still need later low-level conformance work.

### Phase 1PX: Pilotwings DSP-1 command/protocol completion

The second Pilotwings capture remained visually corrupted after the first
projection-only pass, so this audit moved above the math layer and checked the
host/DSP command protocol against mature DSP-1 HLE implementations and board
research. Two compatibility defects were found that can desynchronize retail
software even when the projection equations themselves are reasonable.

Gyrate (`$14`, alias `$34`) is now recognized as a six-word command and returns
three angle words. Previously the opcode had no input length or execution path,
so all twelve parameter bytes could be reinterpreted as subsequent command
bytes. The regression uses zero/local-axis rotations and verifies the alias
consumes the complete parameter packet before the next command.

Raster (`$0A` and aliases canonicalized to it) keeps its streaming output
protocol. While an 8-byte Raster matrix remains pending, host writes consume
and discard pending output bytes instead of beginning a new command. The
regression discards a complete pending matrix with writes and immediately sends
a multiply command; successful multiplication proves the DSP command stream
remained aligned. `$07`/`$27` diagnostic aliases are also covered.

The projection state was tightened further around coefficient/exponent
normalization, reciprocal/truncation, VPlane/Les normalization and the two
clipped-zenith secants used separately by Target and Raster. These remain an
independent HLE approximation rather than bundled NEC firmware, so DSP-1 stays
classified `PartialHLE`.

PAL Pilotwings cannot safely be used to infer a physical DSP revision from the
title alone: documented PAL PCBs exist with both DSP1 and DSP1B. Exact revision
preservation therefore belongs in a later cartridge database/firmware-backed
path rather than a title heuristic in this HLE.

### Pilotwings DSP-1 cartridge port decode

`dsp1_accuracy` also validates the DSP-1 cartridge data/status split used by
Pilotwings.  On the small LoROM map, `$xx8000-$xxBFFF` is the serial command/
result data region and `$xxC000-$xxFFFF` is the read-only status region.  A
status poll must return the ready/data-request bit without consuming pending
DSP result bytes, and writes to the status region must not enter the DSP
command parser.  Equivalent map boundaries are `$4000` for large-LoROM DSP-1
boards and `$7000` for HiROM DSP-1 boards.

## Deep audit A — full destination-code PAL/NTSC classification

The cartridge region regression now covers the complete documented SNES destination-code table rather than only Europe and North America. 50 Hz PAL timing is selected for destination codes `$02-$0C` and `$11` (Australia); `$00/$01`, Korea `$0D`, Common `$0E`, Canada `$0F`, Brazil/Gateway `$10`, and `$12-$14` remain on the 60 Hz/NTSC-timing path. This fixes the previous omission where Australian releases were incorrectly treated as NTSC. Each case verifies cartridge classification, PPU region state, 262/312-line field length, nominal frame rate, STAT78 `$213F` region bit, and persistence across reset.

## Phase 3: CPU-cycle ALU, controller interference, timed OBJ fetches, S-CPU revision identity

Phase 3 moves the 5A22 multiplier/divider from fixed-master-clock advancement to completed S-CPU machine-cycle advancement. `cpu_math_timing` verifies that elapsed master time alone cannot advance the arithmetic unit, while exactly 8 multiplication or 16 division CPU cycles do, including the documented overlapping-operation corruption behavior.

`ppu_auto_joypad_timing` now verifies that manual JOYOUT and automatic polling share the same physical OUT0 latch. Holding `$4016.0` high through an automatic poll keeps the controller latched and corrupts the captured result as real hardware usage rules predict.

`ppu_oam_status_accuracy` verifies progressive next-line range evaluation and HBlank Time Over timing. `ppu_obj_fetch_pipeline` additionally proves that OBJ pixel data is captured during the preceding scanline's fetch window and is insulated from later OBJSEL/VRAM selection changes. It also covers independent row/column nibble wrapping in the 16x16-tile OBJ character table.

`cpu_revision_accuracy` introduces explicit original S-CPU/S-CPU A/S-CPU B identity and verifies the `$4210` CPU-version nibble (1 for original, 2 for A/B). More destructive revision-specific DMA/HDMA quirks remain intentionally unimplemented until backed by sufficiently precise hardware-test behavior.

## Phase 12 hardware-suite integration

For external SNES hardware ROMs, set `SNESULTIMATE_HARDWARE_SUITE_ROOT` to a directory containing independently obtained/built test assets. `tests/prepare_hardware_suites.py` recognizes the MIT-licensed `gilyon/snes-tests` layout and Zlib-licensed `undisbeliever/snes-test-roms` builds. No third-party ROM binaries are bundled.

The external ROM manifest now supports `checkpoints`: each checkpoint starts from power-on and can carry its own frame/cycle limit, framebuffer/audio hash, or oracle settings. A failure reports the first divergent checkpoint. Cases marked `optional` are skipped if the corresponding ROM is absent.

Do not promote emulator-generated framebuffer hashes to correctness goldens. Use hardware captures or a separately reviewed independent reference for PPU/DMA/HDMA visual assertions.

## Phase 13 hardware-driven SETINI/provenance work

`ppu_setini_early_read.cpp` covers the verified `$2133` early-input-bus sampling behavior. During active display the previous S-CPU data-bus byte can be observed by SETINI for one raster dot before the programmed value takes effect. The regression keeps the probabilistic OBJ corruption seen on real consoles outside the deterministic core model.

Generated hardware-suite manifests now pin ROMs with `rom_sha256` and gilyon test catalogs with `test_catalog_sha256`. `external_regression.py` verifies these before execution, preventing silently rebuilt test assets from being treated as the same hardware oracle.

## Phase 21 NMI / interrupt-recognition accuracy

`cpu_nmi_recognition_accuracy` replaces the remaining one-shot VBlank/NMI assumption with regressions for the explicit H=0.5 RDNMI source, `$4210` acknowledgement/open-bus behavior, NMITIMEN re-evaluation, late-final-cycle NMI deferral, and NMI-over-IRQ priority. `cpu_dma_interrupt_boundary` additionally verifies that an NMI edge captured during MDMA remains pending until bus ownership returns, complementing its existing HDMA case. Whole-system state version 17 serializes the new in-flight NMI/IRQ recognition state.


## Phase 22 S-SMP memory-overlay / TEST accuracy

`apu_test_register` now verifies the S-SMP TEST.R/TEST.r Audio-RAM gates, the `$5A` disconnected-RAM read pattern, the distinct `$F8/$F9` internal S-SMP latches versus backing ARAM, and separation of CPU->SMP / SMP->CPU `$F4-$F7` communication latches from the ARAM bytes visible to the S-DSP. Writes to the `$F0-$FF` register overlay still perform their I/O side effects while backing-ARAM writes obey the TEST gates. Flat SPC700 oracle memory remains intentionally raw. Whole-system state version 18 serializes the new hidden `$F8/$F9` latch state.

## Phase 23 S-SMP power/reset and timer-state accuracy

`apu_reset_power_accuracy.cpp` separates cold APU power from the console reset
line. Warm reset now restarts the SPC700/IPL-facing state without erasing the
64 KiB Audio-RAM backing store; the IPL ROM itself performs the documented
software clear of page `$00-$EF` after reset. Timer target registers `$FA-$FC`
are retained across reset while the stage-2/output state is reset, and both
directional APUIO latch banks return to zero. Cold power remains a deterministic
emulator initialization point and clears backing ARAM and timer targets.

The whole-system save-state format remains version 18 because no serialized
state fields were added or removed. The full Phase 23 suite passes 106/106.

### Phase 29 — SPC7110 clocked MCU state

`spc7110_timing_accuracy` and the expanded `spc7110_accuracy` cover the clocked SPC7110 ALU (30-clock multiply / 40-clock divide model), `$482F.7` busy lifetime, in-flight ALU checkpoint replay, `$481A` seek-side-effect behavior, `$4808`/bank-$58 read-zero port behavior, bank-$50 decompression-port mirroring, and `$4830.7` SRAM chip enable. Whole-system state version 22 and SPC7110 local version 2 serialize pending arithmetic work.

## Phase 32 cartridge RTC clock domains

`cartridge_rtc_clock_accuracy.cpp` moves Sharp S-RTC and SPC7110 RTC-4513
runtime progression off host wall time and onto deterministic cartridge clock
state. It covers exact one-second boundaries, Sharp's 1000-01-01 weekday epoch,
warm-reset divider retention, RTC-4513 HOLD/calendar controls, and checkpoint
replay of sub-second RTC phase. Whole-system state version 25, Sharp S-RTC local
version 2, and SPC7110 local version 3 serialize the newly in-flight RTC state.

## Phase 33 SPC7110 RTC-4513 serial-interface accuracy

`spc7110_rtc_interface_accuracy` locks the RTC-4513's 2.097152 MHz serial READY/busy domain: eight internal clocks per accepted mode/index/read/write transaction, non-destructive `$4842` polling, distinct Mode/Seek/Read/Write states, MDR behavior, ignored busy writes, indexed read timing, and deterministic save/load of an in-flight interface wait. Existing SPC7110 and cartridge RTC tests also account for the real elapsed time consumed by serial accesses.

## Phase 34 CX4 HG51B host controller and bus timing

`cx4_controller_timing.cpp` moves the Cx4 fallback HLE's host-facing HG51B
controller away from the old one-cycle pseudo-busy model.  The status mirrors
now expose separate RUNNING (`bit 6`) and BUSY (`bit 7`) state, `$7f47` is the
third DMA-target byte and starts the built-in DMA engine while halted, and DMA
bytes retire progressively according to the HG51B ROM wait register rather
than appearing as one atomic block copy.  `$7f48` models the 256-word program
cache preload as 512 external ROM byte fetches, and `$7fc0-$7fef` aliases the
same sixteen 24-bit GPRs as `$7f80-$7faf`.

The mathematical command bodies remain independent HLE: Phase 34 does not
invent firmware instruction counts for Mega Man X2/X3 command handlers.  The
next Cx4 step is a firmware/data-ROM-backed HG51B LLE path or equivalent
microprogram execution model; per-command HLE latency remains deliberately
unclaimed until then.

## Phase 40 — ST010 compass lattice accuracy

`st010_compass_accuracy` verifies the ST010 HLE's reverse-engineered 32×32 compass/arctangent lattice, integer coordinate normalization, quadrant rotation/post-fix behavior, and a known non-analytic lattice entry. Phase 40 removes the remaining host `atan2`/`lround` dependency from ST010 OP01/OP05 heading calculations. No save-state format changes are required.

## Phase 42: S-SMP outbound APUIO write-cycle timing

`apu_port_write_timing_accuracy` verifies that SPC700 writes to `$F4-$F7` do not become S-CPU-visible at atomic instruction dispatch. The core stages outbound writes and commits them on their bus-visible final SPC700 cycles, including consecutive `MOVW dp,YA` low/high writes and TEST-clock stretching inherited from Phase 41. This phase intentionally does not yet defer SPC700 reads of CPU-to-SMP latches; exact bidirectional collision behavior remains a later microstep.


## Phase 43 S-SMP inbound APUIO read timing

`apu_port_read_timing_accuracy` locks cycle-visible S-CPU -> SPC700 CPUIO sampling for the IPL-critical direct-page forms. It verifies cycle-3 `MOV A/X/Y,dp` and `CMP Y,dp` reads, the split cycle-3/cycle-5 `MOVW YA,dp` samples, host writes immediately before/after those boundaries, and TEST-divider stretching of the read deadline. Same-cycle electrical collision/OR behavior remains deliberately deferred.

## Phase 3 final 100% conformance gate

`final_conformance_gate.py` is deliberately stricter than the ordinary sampled
external CTest configuration. It prevents a clean-but-partial oracle run from
being mistaken for the final hardware-conformance result.

The gate requires all of the following:

- the 65816 report was produced with `--sample 0` and contains every
  `00.e.json` ... `ff.e.json` and `00.n.json` ... `ff.n.json` file, with every
  parsed vector selected and zero failures;
- the SPC700 report was produced with `--sample 0`, contains every
  `00.json` ... `ff.json` opcode file, selects every parsed vector, and has zero
  failures;
- the hardware-suite report contains passing, non-skipped results for both
  `gilyon 65C816 full conformance` and `gilyon SPC700 conformance`;
- when a golden ROM regression manifest is configured, that report must also
  have zero failures and all executed cases passing.

CMake only registers `final_conformance_gate` when both SingleStep corpora, the
hardware-suite root, an external report directory, and exhaustive
`SNESULTIMATE_EXTERNAL_SAMPLE=0` are configured. This makes the distinction
between everyday sampled CI and the literal Phase-3 promotion gate explicit.

Example final run:

```sh
cmake -S . -B build-final \
  -DSNESULTIMATE_65816_TESTS=/path/to/SingleStepTests/65816/v1 \
  -DSNESULTIMATE_SPC700_TESTS=/path/to/ProcessorTests/spc700/v1 \
  -DSNESULTIMATE_HARDWARE_SUITE_ROOT=/path/to/hardware-suites \
  -DSNESULTIMATE_EXTERNAL_SAMPLE=0 \
  -DSNESULTIMATE_EXTERNAL_REPORT_DIR="$PWD/build-final/reports"
cmake --build build-final -j
ctest --test-dir build-final -L external --output-on-failure
```

A successful `final_conformance_gate` is evidence that the configured external
oracles are exhaustive and clean; a sampled or incomplete report is rejected
even if it contains zero failures.

## Phase 44 — Cx4 HG51B execution overlay and reserved-window accuracy

`cx4_accuracy` and `cx4_controller_timing` now lock two host-facing HG51B behaviors documented from hardware research.  During the complete RUNNING interval, `$7f40-$7f7f` is overlaid onto the S-CPU `$ffc0-$ffff` area; this includes ordinary Cx4 command execution and is not restricted to the separate external-bus BUSY state used by DMA/cache operations.  The overlay is released when execution halts.

The reserved `$7fb0-$7fbf` register window and its `$7ff0-$7fff` mirror now read as zero and ignore writes rather than aliasing generic internal storage.  This phase deliberately leaves the noisy/unmapped `$6c00-$6fff` and `$7c00-$7f3f` regions and the HLE mathematical command bodies for later HG51B LLE work.
