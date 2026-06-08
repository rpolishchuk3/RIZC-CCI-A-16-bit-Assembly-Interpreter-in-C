# RIZC-CCI - Custom ISA Emulator & Assembler

A fully working CPU emulator for a custom 16-bit instruction set architecture, written in C. RIZC-CCI simulates a minimal processor - register file, byte-addressable memory, program counter, and a complete fetch-decode-execute pipeline - paired with a Python assembler that compiles human-readable assembly into binary machine code the emulator runs.

---

## Motivation

I wanted to understand what actually happens when a CPU executes a program. Not at the operating system level, not at the compiler level - but at the point where binary instruction bits enter a processor and registers change as a result. Building an emulator forces that understanding: you have to define every instruction encoding, implement every operation, and get the sequencing exactly right or nothing works.

RIZC-CCI gave me a complete picture of how software meets hardware. Writing programs in assembly for my own ISA, then watching the emulator execute them instruction by instruction, made the fetch-decode-execute cycle tangible in a way that reading about it never could.

---

## Real-World Relevance

This project serves as a capstone-style implementation that consolidates core concepts from low-level programming, memory management, and system design into a cohesive system.

### Why This Matters

- **Systems Programming & Performance Engineering** - understanding how memory, data structures, and execution flow interact at a low level is essential for building high-performance applications such as databases, operating systems, and real-time systems.

- **Data Processing & Backend Systems** - many large-scale systems rely on efficient data handling and transformation. The techniques used in this project mirror patterns found in backend services, data pipelines, and analytics engines.

- **Reliability & Debugging Skills** - working close to the hardware forces precise control over memory and logic, which develops strong debugging and problem-solving skills—critical for production-level engineering.

- **Foundation for Advanced Fields** - concepts reinforced here are directly applicable to:
  - Machine learning infrastructure (efficient data handling)
  - Robotics systems (real-time processing constraints)
  - High-frequency trading systems (latency-sensitive computations)

### Key Takeaway

This project represents a transition from academic exercises to practical engineering, demonstrating the ability to design, implement, and reason about systems at a level required in real-world software development.

It reflects not just isolated knowledge, but the integration of core computer science fundamentals into a working system.

---

## Overview

The project has two components that work together:

**The emulator** (`rizc-cci.c`) loads a hex-encoded binary program and an input data file, executes the program on a simulated CPU, and writes the output. It implements a 16-register file, 1 KB of byte-addressable memory, and a program counter driving a full execution loop.

**The assembler** (`assembler.py`) compiles `.rizc-cci` assembly source files into the hex instruction format the emulator expects. It handles label resolution, PC-relative branch offsets, and syntax validation.

---

## How It Works

```
┌─────────────┐     ┌──────────────┐     ┌──────────────────────┐
│  .rizc-cci  │────▶│  assembler   │────▶│  hex instruction file │
│  (assembly) │     │  (Python)    │     │  (0xNNNN per line)   │
└─────────────┘     └──────────────┘     └──────────┬───────────┘
                                                     │
                    ┌────────────────────────────────▼────────────┐
                    │              Emulator (C)                    │
                    │                                              │
                    │  1. Load program into instruction array      │
                    │  2. Load input bytes into memory stack       │
                    │  3. LOOP:                                    │
                    │     a. Fetch instruction at PC               │
                    │     b. Halt if 0xFFFF sentinel               │
                    │     c. Increment PC                          │
                    │     d. Decode opcode → dispatch handler      │
                    │     e. Execute → update registers / memory   │
                    │  4. Write null-terminated string at op       │
                    └──────────────────────────────────────────────┘
```

---

## ISA Design

All instructions are **16 bits wide**. The two least-significant bits encode the instruction type (opcode). The remaining bits carry operands, function codes, or immediates.

### Register File

| Register | Number | Role |
|----------|--------|------|
| `x0` | 0 | Hardwired zero - reads always return 0, writes are discarded |
| `x1`–`x11` | 1–11 | General purpose |
| `ip` | 12 | Input pointer - initialized to start of input data in memory |
| `op` | 13 | Output pointer - emulator reads output string from here after halt |
| `pc` | 14 | Program counter - index of the next instruction to fetch |
| `sp` | 15 | Stack pointer - initialized to last byte of input data |

All registers are 64-bit (`uint64_t`).

### Instruction Encoding

**R-type** - register arithmetic and logic (opcode `00`)

```
15      12 | 11      8 | 7       4 | 3    2 | 1    0
  r1 (dst) |  r2 (src) |  r3 (src) |  func  |  op
```

| Mnemonic | func | Operation |
|----------|------|-----------|
| `add` | `00` | `r1 = r2 + r3` |
| `and` | `01` | `r1 = r2 & r3` |
| `or`  | `10` | `r1 = r2 \| r3` |
| `sll` | `11` | `r1 = r2 << r3` |

**B-type** - conditional branch (opcode `01`)

```
15      12 | 11      8 | 7              2 | 1    0
    r1     |     r2    |    imm[6:1]      |  op
```

`beq r1, r2, label` - if `r1 == r2`, jump to `label`. The 7-bit signed offset is PC-relative. Bit 0 is always 0 (all instructions are 2-byte aligned, so the assembler stores only bits [6:1]).

**I-type** - memory load / store (opcode `10`)

```
15      12 | 11      8 | 7       4 | 3    2 | 1    0
  r1       |    r2     |   0000    |  func  |  op
```

| Mnemonic | func | Operation |
|----------|------|-----------|
| `ld` | `00` | `r1 = M[r2]` - load 8 bytes, little-endian |
| `lb` | `01` | `r1 = M[r2]` - load 1 byte, zero-extended |
| `sd` | `10` | `M[r2] = r1` - store 8 bytes, little-endian |
| `sb` | `11` | `M[r2] = r1 & 0xFF` - store low byte only |

**L-type** - load immediate (opcode `11`)

```
15      12 | 11             4 | 3    2 | 1    0
  r1 (dst) |    imm[7:0]      |   00   |  op
```

`li r1, imm` - sign-extends an 8-bit immediate to 64 bits and loads it into `r1`. Range: [−128, 127].

---

## Key Implementation Details

### Unified bit extractor

Rather than hand-rolling masks for every field in every instruction type, a single helper isolates any bit range:

```c
uint16_t extract_bits(uint16_t instruction, int start, int end) {
    int length = end - start + 1;
    return (instruction >> start) & ((1 << length) - 1);
}
```

Every decode path calls this - one line per field, no scattered magic numbers.

### Branch offset: the ghost bit

B-type instructions can only jump to instruction-aligned addresses, so the offset's LSB is always 0. The assembler drops it. The emulator reconstructs it with a left shift, then sign-extends to get a signed 16-bit jump distance:

```c
uint8_t literal_7bit = literal_6_1 << 1;          // restore the implicit 0 bit
int16_t offset = (literal_7bit & 0x40)
    ? (literal_7bit | 0xFF80)                       // negative: sign-extend
    : literal_7bit;                                 // positive: use as-is
registers[REG_PC] = registers[REG_PC] - 1 + (offset / 2);
```

The `- 1` corrects for PC already having advanced past the branch before it executes.

### Memory model

The 1 KB `uint8_t stack[1024]` serves as all program memory. Input data is loaded at address 0 on startup. `ip` points to byte 0, `sp` points to the last input byte. Programs build their output anywhere in that space and leave `op` pointing to the result string. After the halt sentinel, the emulator walks from `op` until it hits a null byte, writing each character to the output file.

### 8-byte little-endian memory access

`ld` and `sd` read and write 8 bytes in little-endian order, assembling or disassembling a full `uint64_t` byte by byte:

```c
uint64_t load_doubleword(uint64_t address) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value |= ((uint64_t)stack[address + i]) << (i * 8);
    return value;
}
```

---

## Debug Trace Mode

Compile with `-DDEBUG` to enable per-instruction stdout logging. Each executed instruction prints its hex encoding and a human-readable disassembly, in actual execution order (instructions inside un-taken branches are silently skipped):

```bash
gcc -DDEBUG -o rizc-cci-debug rizc-cci.c
./rizc-cci-debug input.txt instructions.txt output.txt
```

Example trace:

```
0x1FF3 : li  x1  69
0xFD02 : sb  x1  sp
0x1003 : li  x1  0
0xF802 : lb  x1  sp
0x0002 : sb  x0  sp
0x2FF3 : li  x2  1
0xDC00 : add op  sp  x0
...
```

---

## Example: Writing a Character to Output

This is the full `test.rizc-cci` - loads ASCII `'E'` (69) into memory, reads it back, then builds a one-character output string:

```asm
// Load a value and round-trip it through memory
li  x1, 69          // x1 = 69 = ASCII 'E'
sb  x1, sp          // store byte at stack pointer
li  x1, 0           // clear x1
lb  x1, sp          // reload the byte from memory → x1 = 69
sb  x0, sp          // write zero back (cleanup)

// Build the output string starting at sp
li  x2, 1
add op, sp, x0      // op → base of output region
sb  x1, op          // write 'E'
add op, op, x2      // advance op
li  x3, 10
sb  x3, op          // write '\n'
add op, op, x2      // advance op
sb  x0, op          // write '\0' (null terminator)
add op, sp, x0      // reset op to base so emulator reads from there
```

Assemble and run:

```bash
python3 assembler.py test.rizc-cci -o instructions.txt
./rizc-cci input.txt instructions.txt output.txt
cat output.txt      # → E
```

---

## Build & Run

**Requirements:** GCC (C99), Python 3

```bash
# Build standard and debug binaries
make

# Or manually
gcc -std=c99 -o rizc-cci rizc-cci.c
gcc -std=c99 -DDEBUG -o rizc-cci-debug rizc-cci.c
```

```bash
# Assemble a program
python3 assembler.py program.rizc-cci -o instructions.txt

# Run the emulator
./rizc-cci input.txt instructions.txt output.txt

# Run with instruction trace
./rizc-cci-debug input.txt instructions.txt output.txt
```

| Argument | Description |
|----------|-------------|
| `input.txt` | Hex bytes preloaded into memory (`0xNN` per entry) |
| `instructions.txt` | Assembled hex program (`0xNNNN` per line, ends with `0xFFFF`) |
| `output.txt` | File the emulator writes the output string to |

---

## Project Structure

```
.
├── rizc-cci.c          # Emulator - fetch/decode/execute loop, register file, memory
├── assembler.py        # Assembler - lexer, label resolver, instruction encoder
├── test.rizc-cci       # Example assembly program
├── Testcases/          # 30 input/instruction/expected-output triples
├── assets/             # Instruction encoding diagrams (R/B/I/L-type)
├── Makefile
└── README.md
```

---

## Limitations

- **Fixed 1 KB memory** - no heap, no memory protection, no address translation
- **No pipeline simulation** - instructions execute sequentially with no hazard detection or stall modelling
- **Immediate range capped at 8 bits** - L-type load immediate is limited to [−128, 127], larger constants require multiple instructions
- **Branch range capped at ±32 instructions** - the 7-bit signed offset limits how far a `beq` can jump
- **No interrupt or exception model** - illegal instructions and out-of-bounds accesses fail silently

---

## Future Improvements

- **Expanded instruction set** - subtract, compare-and-set, jump-and-link (for function calls), and arithmetic shift right
- **Pipeline model** - simulate fetch, decode, execute, and writeback as separate stages with hazard detection
- **Memory hierarchy** - add a cache layer between the register file and the flat memory array to model cache hit/miss behavior
- **ELF-style binary format** - replace plain hex files with a structured binary format (magic bytes, header, separate code and data sections)
- **Disassembler** - reverse the assembler: read a hex instruction file and produce human-readable assembly
- **Interactive debugger** - step through instructions one at a time, inspect register state, dump memory regions

---

## Key Takeaways

- **Instruction encoding is a design problem.** Every bit in a 16-bit instruction is a resource - deciding how to split them between opcode, register fields, and immediates involves real tradeoffs between instruction count, operand range, and decoder complexity.
- **The fetch-decode-execute loop is the CPU.** Everything else - registers, memory, the program counter - is infrastructure that the loop drives. Once that mental model clicks, how real processors work becomes much clearer.
- **Stateful systems require disciplined debugging.** When an emulator produces wrong output, the bug could be in the decoder, the executor, the memory model, or the assembler. The debug trace mode was essential - being able to see exactly which instructions executed in which order made every bug locatable.
- **The assembler and emulator must agree on every encoding detail.** The ghost-bit convention in branch offsets, the endianness of doubleword loads, the behavior of writes to `x0` - every one of these has to be consistent across both tools or programs silently produce wrong results.
