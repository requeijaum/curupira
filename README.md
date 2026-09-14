# Curupira

An emulator for the **Zeebo** — the Brazilian game console that shipped in 2009 as a
Qualcomm **MSM7201A** (ARM1136J-S @ 528 MHz + Adreno 130) running **BREW** as its
operating system. Games are native ARM `.mod` modules calling BREW's `AEE` C
interfaces plus OpenGL ES 1.0/1.1.

It runs the real game modules against a **high-level reimplementation of the BREW
runtime**. No firmware, no BIOS, no LLE of the SoC — only user-supplied game dumps.

**MIT licensed, from end to end.**

---

## The name

**Curupira** is a figure from Brazilian folklore — a creature of the forest whose
**feet point backwards**. A hunter who follows its tracks walks the wrong way.

That is the story of this project, told as a name. The codebase grew out of a fork,
and its central finding is that **the games were right and the emulator's
assumptions were wrong**, over and over:

- the `INHERIT_IBase` vtable was assumed to have three members. It has **two**, and
  every slot number in the project was off by one — the game called index 2 for
  `GetFontMetrics` and the emulator refused it;
- the module load base was an inherited convention, `0x00100000`. The literals in a
  `.mod` are **file offsets used as absolute addresses**; the correct base is
  **zero**. Fixing it took the corpus from 48 to 62 loaded modules and from 22 to 37
  applets;
- `IShell::SetTimer` was read with a BREW 4.x argument order. This SDK's is
  `(po, msecs, pfn, pUser)`. The wrong order meant **no title armed its frame loop**
  — nothing could ever reach the code that draws.

In each case the emulator was following tracks that pointed the wrong way. The
project's answer is not to trust the tracks: every claim it makes is written next to
a measurement that supports it, and the measurements are in this repository.

There is a second reason, smaller and just as true: the Zeebo is the Brazilian
console, and the folklore should match the hardware.

---

## Where it stands, measured

Everything below is produced by a command in this repository. If a number is not
measured, it does not go in.

```
$ ./build/zb2_bateria corpus62.json /path/to/mods /tmp/state.json
== 62 titles | loaded 62 | module pointer 62 | applet 37 | EVT_APP_START 35 ==
```

| Stage of a title's life | titles | of 62 |
|---|---|---|
| `.mod` image read and mapped | **62** | 100% |
| `AEEMod_Load` returned a module pointer | **62** | 100% |
| `IModule::CreateInstance` returned an applet | **37** | 60% |
| `HandleEvent(EVT_APP_START)` delivered to the applet | **35** | 56% |
| **renders a frame** | **0** | **0%** |

**That last line is the honest headline: nothing draws yet.** The games boot, build
their object graphs, and start asking for things — and the screen is still black.
The next wall has a name and a count: `rasterizador_de_GL`, requested 124 times. The
GL and EGL interfaces are wired and the calls arrive; what does not exist is the
thing that turns vertices into pixels.

```
430 unit tests (428 passing, 2 conditional skips), 9/9 ctest suites green
```

---

## What makes this project different

Most emulator projects are judged by screenshots. This one is built around a claim
you can check: **the instrument lies more often than the emulator fails.**

That is not a slogan. It is a measured result from the previous iteration: of the
bugs found in one session, **seven were defects in the measuring code**, not in the
emulation. A GL handler that silently swallowed 86,377 `glCullFace` calls. Two
constants named after the wrong function, which made three tests named `Strstr*`
test `stristr` instead. A census that measured *images* and therefore declared "no
drawing" on titles that drew.

So the rewrite is designed to make those failures impossible by construction:

- **One recorder, no raw `fprintf`.** Every event carries a common header: who,
  where, with what arguments, when. A log only enters if it can be true.
- **Slot tables are generated from the SDK headers.** `tools/gerar_slots.py` reads
  the BREW headers and resolves the inheritance chain, and ctest fails if the
  checked-in table drifts from the header by a single slot. Copying a table from
  another emulator is *forbidden by test*, not by discipline.
- **Guardrails must be proved by deliberate violation.** For every guard, someone
  breaks it on purpose and confirms the test goes red. A check that passes without
  the change is not a check — that mistake cost this project a full round, twice.
- **The instrument and the emulator are separate products.** The measuring tool was
  once larger than the engine. It is not any more.

## Findings worth the repository alone

Each of these is a measurement with the command that produced it, written next to
the code that depends on it.

**`INHERIT_IBase` has two members, not three.** Every slot number was off by one,
because the author counted three and went looking for a `QueryInterface` that does
not exist in `INHERIT_IBase`. The symptom: `pacmania` called `[vtable+8]` — index 2
— with `(po, AEE_FONT_NORMAL, &ascent, &descent)`, which is exactly
`GetFontMetrics`. *The game was right and the emulator was wrong.*

**The module base is zero.** A `.mod`'s literals are **file offsets used as absolute
addresses**. Loading at `0x00100000` — an inherited convention, never a measurement
— put them outside the mapped image, where reads return zero.

```asciidoc
base 0x00100000:  modules 48 | applets 22
base 0x00000000:  modules 62 | applets 41
```

The proof is a test, not an opinion: with base zero, **51 literals in `pacmania.mod`
land exactly on real strings**; with the old base, zero do.

**A field that measured nothing.** The battery printed `vtable NO` for all 62 titles
for several rounds. It was read as "the modules have no vtable". In fact an
assignment had been deleted and the field was always zero, so one of the four boot
stages was dead. *A field that does not measure is worse than a missing field: a
missing field is not read.*

**Two helper offsets had drifted between two copies.** `strtowstr` was wired at
`0x0a0` instead of `0x040`, and `aee_GetRand` at `0x090` instead of `0x0a8` — so a
guest calling `atoi` received **random bytes**. The cause was a second copy of the
same table, and nothing compared the two. Both sides now read a generated header.

---

## Layout

```
core/traco        instrumentation: one recorder, common header, no relative paths
core/memoria      guest address space, 4 KiB pages, one writer, write watch
core/cpu          ICpu interface + ArmInterpreter (ARM and Thumb)
core/carga        .mod loader, .bar resource reader, VFS
core/brew         the BREW runtime: shell, display, files, input, media, GL, widget
core/video        the software rasterizer target (core/brew/tela)
tools/bateria     the 62-title battery — one measured state per title
tools/comparar    diffs two runs and FAILS when a title regresses
tools/gerar_slots.py  generates slot tables from the SDK headers
docs/             the design, the plan, and the measured findings
```

---

## Build

```sh
cmake -S . -B build
cmake --build build -j"$(nproc)"

./build/zb2_tests                  # 430 total: 428 passing, 2 conditional skips
ctest --test-dir build             # the same, plus the SDK-consistency guardrails

# One run of the whole corpus. `corpus62.json` ships with this repository; the game
# directory is where your `.mod` files live, one folder per title.
./build/zb2_bateria corpus62.json /path/to/mods /tmp/state.json

./tools/regressao.sh               # re-run and diff against the recorded baseline
```

Requires a C++20 compiler and CMake ≥ 3.16.

### Two external pieces, both optional

| what | why | without it |
|---|---|---|
| the BREW SDK headers | the slot tables and CLSIDs are generated from them | the consistency tests **skip** with a message — they do not silently pass |
| game dumps | the corpus is the specification | `regressao` skips; unit tests still run |

Point at them with `ZB2_SDK_DIR` and `ZB2_MODS`. Neither is redistributed here:
there is no SDK, no firmware and no game content in this repository.

---

## The corpus

62 retail titles, each with one measured state recorded in
`tools/baseline/bateria.json` and versioned. A change that makes a title *worse*
fails `ctest` with the number that got worse:

```
$ ./tools/regressao.sh
REGRESSOES (0):
MELHORIAS (47)  by field: applet 19, cores 14, modulo 14
result: 0 regression(s), 47 improvement(s) in 62 titles -- NO REGRESSIONS
```

The comparator refuses to compare runs that are not the same configuration: it
carries the corpus's SHA-256 and the title count, and a divergent *build* is
reported, never treated as a criterion — because comparing two emulator versions is
the tool's normal use. Every field's direction is declared in one table with the
measurement that justifies it, and an **unknown field is refused** rather than
ignored. A stub that silently accepts what it does not understand is the failure
mode this project exists to avoid.

---

## Scope and honesty

- **Playability is the goal, and it is not achieved.** No title renders yet.
- **No JIT yet.** The reference interpreter is the oracle. The design calls for a
  second implementation so "is this correct?" has a computable answer.
- **No BIOS or firmware is needed or used.**
- **Clean-room.** No SDK binaries, no game content, no decompiled code.

## License

MIT, for the whole repository. See `LICENSE`.

## Where this came from

The codebase grew out of a fork of an earlier GPLv3 emulator. The rewrite shares no
code with it — that was checked mechanically, and the method is kept in
[`NOTICE.md`](NOTICE.md) so it can be falsified. The earlier tree and the third-party
research it used are **not** vendored here: `docs/DESIGN.md` and `docs/PLAN.md`
record what was learned from them and how.
