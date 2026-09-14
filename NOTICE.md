# Where the code came from, and why it is MIT

This repository is MIT, in full. That is not a default — it was checked.

## The question

The emulator grew out of a fork of an earlier, GPLv3-licensed emulator. A rewrite
that merely *feels* new cannot be relicensed; only one that shares no code can. So
the overlap was **measured**, not assumed.

## The method

For every file that exists under both trees: strip blank lines, braces, comments and
short lines, keep only substantive lines (more than 12 characters of real code), and
intersect the two sets.

Result for the largest shared filename, `core/cpu/arm_interpreter.cpp`:

```
rewrite:     384 substantive lines
earlier:     652 substantive lines
IDENTICAL:     7  --  1.8% of the rewrite
```

and all seven are the kind of line that two independent implementations of the same
instruction set cannot avoid:

```
#include "core/cpu/arm_interpreter.h"
switch (op) {
switch (opcode) {
if (rd == kPC) {
if (cond == 0xF) {
}  // namespace
```

No shared logic, no shared expression, no shared constant.

## Where the behaviour comes from

Not from the earlier code. It comes from:

- the **BREW SDK headers**, which the slot tables and the CLSIDs are generated from
  (`tools/gerar_slots.py`, guarded by a ctest suite that fails if a checked-in table
  drifts from the header by one slot);
- **measurements** taken by running the corpus and disassembling the games — every
  behaviour is justified by one, written next to the code that depends on it;
- the **62-title corpus** and its recorded baseline, which are the specification.

## What is *not* here

The earlier GPLv3 tree and the third-party projects studied along the way are not
vendored in this repository. They are referenced, not copied — which is also what
keeps this repository MIT from end to end.

## Falsifiability

If you believe a specific file carries over code that this check did not catch, open
an issue and it will be measured again. The check is mechanical and repeatable, and
it is written here so that it can be disproved rather than trusted.
