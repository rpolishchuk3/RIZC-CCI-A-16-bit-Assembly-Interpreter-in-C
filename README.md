# RIZC-CCI - A 16-bit Assembly Interpreter in C

A software emulator for a custom 16-bit Instruction Set Architecture (ISA), written in C. RIZC-CCI decodes and executes binary machine code, simulating a minimal CPU with 16 registers, a 1 KB memory stack, a program counter, and four instruction types - all implemented from scratch using bitwise operations and a fetch-decode-execute loop.

---

## Overview

This project implements a complete fetch-decode-execute pipeline for a custom ISA called **RIZC-CCI**, designed around fixed-width 16-bit instructions. The interpreter reads a binary program (encoded as hexadecimal), loads input data into a simulated memory stack, executes each instruction, and writes the program's output to a file.

The goal was to understand, at the lowest level, how a CPU actually works - how binary instructions are encoded, how registers interact with memory, and how branch instructions break sequential flow to enable loops and conditionals.

---

## Features

- **Full ISA implementation** - 10 instructions across 4 instruction types (R, B, I, L)
- **Fetch-decode-execute loop** driven by a simulated program counter (`pc`)
- **16 × 64-bit registers**, including special-purpose `ip`, `op`, `sp`, and `pc` registers
- **1 KB byte-addressable memory stack** with `uint8_t` granularity
- **Bitwise instruction decoding** - all fields extracted via bit masking and shifting
- **8-bit sign extension** for signed immediate values in L-type and B-type instructions
- **Branch instruction** with encoded 7-bit signed PC-relative offset (ghost-bit encoding)
- **Debug trace mode** - compile-time `DEBUG` flag enables per-instruction stdout logging showing hex encoding and human-readable disassembly
- **Zero-register protection** - writes to `x0` are silently discarded, matching real ISA conventions
- **Memory-safe execution** - no leaks; all heap-allocated program memory is freed after execution

---

## ISA Reference

RIZC-CCI uses **16-bit fixed-width instructions**. The two least-significant bits always encode the opcode (instruction type). The remaining bits carry operands, function codes, or immediates depending on type.

### Registers

| Name | Number | Purpose |
|------|--------|---------|
| `x0` | 0 | Hardwired zero - reads return 0, writes are ignored |
| `x1`–`x11` | 1–11 | General purpose |
| `ip` | 12 | Input pointer - initialized to base of input data in stack |
| `op` | 13 | Output pointer - set by program; interpreter reads output from here |
| `pc` | 14 | Program counter - index of the next instruction to execute |
| `sp` | 15 | Stack pointer - initialized to last byte of input data |

### Instruction Types

**R-type** (opcode `0b00`) - Register-to-register arithmetic/logic

| Bits 15–12 | Bits 11–8 | Bits 7–4 | Bits 3–2 | Bits 1–0 |
|-----------|----------|---------|---------|---------|
| r1 (dst) | r2 (src1) | r3 (src2) | func2 | op |

| Instruction | func2 | Operation |
|-------------|-------|-----------|
| `add` | `00` | `r1 = r2 + r3` |
| `and` | `01` | `r1 = r2 & r3` |
| `or` | `10` | `r1 = r2 \| r3` |
| `sll` | `11` | `r1 = r2 << r3` |

**B-type** (opcode `0b01`) - Conditional branch

| Bits 15–12 | Bits 11–8 | Bits 7–2 | Bits 1–0 |
|-----------|----------|---------|---------|
| r1 | r2 | imm[6:1] | op |

`beq r1, r2, offset` - if `r1 == r2`, jump: `pc += offset / 2`. The 7-bit signed offset is encoded without its LSB (the "ghost bit"), which is always `0` since all instructions are 2-byte aligned.

**I-type** (opcode `0b10`) - Memory load/store

| Bits 15–12 | Bits 11–8 | Bits 7–4 | Bits 3–2 | Bits 1–0 |
|-----------|----------|---------|---------|---------|
| r1 | r2 | `0000` | func2 | op |

| Instruction | func2 | Operation |
|-------------|-------|-----------|
| `ld` | `00` | `r1 = M[r2]` (8 bytes, little-endian) |
| `lb` | `01` | `r1 = zero_extend(M[r2])` (1 byte) |
| `sd` | `10` | `M[r2] = r1` (8 bytes, little-endian) |
| `sb` | `11` | `M[r2] = r1 & 0xFF` (1 byte) |

**L-type** (opcode `0b11`) - Load immediate

| Bits 15–12 | Bits 11–4 | Bits 3–2 | Bits 1–0 |
|-----------|----------|---------|---------|
| r1 (dst) | imm[7:0] | `00` | op |

`li r1, imm` - loads an 8-bit sign-extended immediate into `r1`. Range: `[-128, 127]`.

---

## Implementation Details

### Instruction Decoding

All fields are extracted using a generic `extract_bits(instruction, start, end)` helper that isolates any bit range via masking and shifting. This keeps each decode path clean - a single call per field rather than hand-rolled masks scattered across the codebase.

### Branch Offset Reconstruction

B-type instructions encode bits `[6:1]` of the offset (the ghost bit `[0]` is implicit zero). The interpreter reconstructs the full 7-bit value by shifting the stored field left by 1, then sign-extends it to a 16-bit signed integer for PC-relative arithmetic:

```c
uint8_t literal_7bit = literal_6_1 << 1;
int16_t offset = (literal_7bit & 0x40) ? (literal_7bit | 0xFF80) : literal_7bit;
registers[REG_PC] = registers[REG_PC] - 1 + (offset / 2);
```

The `-1` corrects for the PC having already been incremented before the branch is evaluated.

### Memory Layout

Input bytes are loaded directly into the base of the `uint8_t stack[1024]` array at startup. `ip` is set to `0` (base), and `sp` is set to the index of the last input byte. The RIZC-CCI program then manipulates the stack freely - using it for both data storage and string construction. After execution, the interpreter reads the null-terminated string from the address in `op` and writes it to the output file.

### Debug Mode

Compiling with `-DDEBUG` enables per-instruction trace output to `stdout`:

```
0x4320 : add x4  x3  x2
0x0009 : beq x0  x0  4
0xFF10 : add x15 x15 x1
```

Because branches alter control flow, the trace reflects actual execution order - instructions that are jumped over do not appear.

---

## Example

**Input file** (`input.txt`) - bytes of "Hello World!\0" in hex:
```
0x48 0x65 0x6C 0x6C 0x6F 0x20 0x57 0x6F 0x72 0x6C 0x64 0x21 0x00
```

**Instruction file** (`instructions.txt`):
```
0xDC00   // add op, ip, x0  - set output pointer to input base
0xFFFF   // halt sentinel
```

**Output file** (`output.txt`):
```
Hello World!
```

**Debug trace** (when compiled with `-DDEBUG`):
```
0xDC00 : add x13 x12 x0
```

---

## Building & Running

### Prerequisites

- GCC (or any C99-compatible compiler)
- Python 3 (optional - for the included assembler)

### Compile

```bash
# Standard build
gcc -o rizc-cci src/ex12q1.c

# Debug build (enables per-instruction trace to stdout)
gcc -DDEBUG -o rizc-cci-debug src/ex12q1.c
```

Or use the included `Makefile`:

```bash
make         # builds both rizc-cci and rizc-cci_debug
```

### Run

```bash
./rizc-cci <input_file> <instructions_file> <output_file>
```

| Argument | Description |
|----------|-------------|
| `input_file` | Hex bytes loaded into the stack as program input (one `0xNN` per line) |
| `instructions_file` | Hex-encoded RIZC-CCI program (one `0xNNNN` per line, terminated by `0xFFFF`) |
| `output_file` | Path where the interpreter writes the program's output string |

### Using the Assembler

The included `assembler.py` compiles human-readable RIZC-CCI assembly into the hex instruction format the interpreter expects:

```bash
python3 assembler.py program.rizc-cci -o instructions.txt
./rizc-cci input.txt instructions.txt output.txt
```

**Example program** (`program.rizc-cci`):
```asm
// Compute 60 + 5 = 65 ('A') and print it
li  x1, 60
li  x2, 5
add x1, x1, x2       // x1 = 65 = ASCII 'A'
li  x2, 1
add op, sp, x0        // op = base of stack
sb  x1, op            // stack[op] = 'A'
add op, op, x2
li  x3, 10
sb  x3, op            // stack[op] = '\n'
add op, op, x2
sb  x0, op            // stack[op] = '\0'
add op, sp, x0        // restore op to base
```

---

## Project Structure

```
.
├── src/
│   └── ex12q1.c          # Interpreter - decoder, executor, I/O
├── assets/
│   ├── R-type.png        # Instruction encoding diagrams
│   ├── B-type.png
│   ├── I-type.png
│   └── L-type.png
├── Testcases/            # Input/instruction/expected-output triples
├── assembler.py          # RIZC-CCI assembler (assembly → hex)
├── test.rizc-cci         # Sample assembly program
├── Makefile
└── README.md
```

---

## Future Improvements

- **Expanded ISA** - add subtract, compare, jump-and-link, and shift-right instructions to approach a more realistic RISC-style register file
- **Heap simulation** - extend memory beyond the fixed 1 KB stack to support dynamic allocation
- **ELF-style binary format** - replace the plain hex instruction file with a structured binary format (magic bytes, header, instruction section)
- **Disassembler** - standalone tool to reverse hex instruction files back into human-readable RIZC-CCI assembly
- **Step debugger** - interactive mode to step through instructions one at a time, inspect register state, and dump memory
- **Overflow and trap handling** - detect and report misaligned access, out-of-bounds PC, and arithmetic overflow rather than silently continuing
