#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define STACK_SIZE 1024
#define NUM_REGISTERS 16

// Special registers (x12-x15)
#define REG_IP 12
#define REG_OP 13
#define REG_PC 14
#define REG_SP 15

// Opcode and func values are 2-bit fields.
// Hex is used because 0b literals are not C99.
#define OPCODE_R 0x0
#define OPCODE_B 0x1
#define OPCODE_I 0x2
#define OPCODE_L 0x3

// func values for R-type
#define FUNC_ADD 0x0
#define FUNC_AND 0x1
#define FUNC_OR  0x2
#define FUNC_SLL 0x3

// func values for I-type
#define FUNC_LD 0x0
#define FUNC_LB 0x1
#define FUNC_SD 0x2
#define FUNC_SB 0x3

// Machine state
uint64_t registers[NUM_REGISTERS];   // 16 registers, 64 bits each
uint8_t stack[STACK_SIZE];           // 1 KB of byte-addressed memory
uint16_t *program;                   // instructions, allocated after counting
int program_size;                    // number of instructions loaded

uint16_t extract_bits(uint16_t instruction, int start, int end);
int64_t sign_extend_8(uint8_t value);
uint64_t load_doubleword(uint64_t address);
int is_valid_address(uint64_t address, int width);

void store_doubleword(uint64_t address, uint64_t value);

void execute_r_type(uint16_t instruction);
void execute_b_type(uint16_t instruction);
void execute_i_type(uint16_t instruction);
void execute_l_type(uint16_t instruction);
void execute_instruction(uint16_t instruction);


int main(int argc, char *argv[]) {
    // Need exactly 3 file arguments
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input.txt> <instructions.txt> <output.txt>\n", argv[0]);
        return 1;
    }

    // Start with every register at zero
    for (int i = 0; i < NUM_REGISTERS; i++) {
        registers[i] = 0;
    }

    // Step 1: load input bytes into memory

    FILE *input_file = fopen(argv[1], "r");
    if (!input_file) {
        perror(argv[1]);
        return 1;
    }

    // Read one hex byte at a time into stack[0], stack[1], ...
    int input_size = 0;
    uint8_t byte;
    while (fscanf(input_file, " 0x%hhx", &byte) == 1) {
        // Stop before writing past the end of the 1 KB array
        if (input_size >= STACK_SIZE) {
            fprintf(stderr, "%s: input larger than %d bytes\n", argv[1], STACK_SIZE);
            fclose(input_file);
            return 1;
        }
        stack[input_size] = byte;
        input_size++;
    }
    fclose(input_file);

    // Step 2: set up the special registers

    // ip points to the start of the input
    registers[REG_IP] = 0;

    // sp points to the last input byte
    // (all ones means "no input")
    if (input_size > 0) {
        registers[REG_SP] = input_size - 1;
    } else {
        registers[REG_SP] = (uint64_t)-1;
    }

    // Step 3: load the program

    FILE *instruction_file = fopen(argv[2], "r");
    if (!instruction_file) {
        perror(argv[2]);
        return 1;
    }

    // First pass: count instructions so we know how much to allocate
    program_size = 0;
    uint16_t temp;
    while (fscanf(instruction_file, " 0x%hx", &temp) == 1) {
        program_size++;
    }

    program = malloc(program_size * sizeof(uint16_t));
    if (!program) {
        fclose(instruction_file);
        fprintf(stderr, "Out of memory loading program\n");
        return 1;
    }

    // Second pass: go back to the start and store each instruction
    rewind(instruction_file);
    for (int i = 0; i < program_size; i++) {
        if (fscanf(instruction_file, " 0x%hx", &program[i]) != 1) {
            free(program);
            fclose(instruction_file);
            fprintf(stderr, "%s: malformed instruction #%d\n", argv[2], i + 1);
            return 1;
        }
    }
    fclose(instruction_file);

    // Step 4: fetch-decode-execute loop

    registers[REG_PC] = 0;
    while (registers[REG_PC] < (uint64_t)program_size) {
        // Fetch
        uint16_t instruction = program[registers[REG_PC]];

        // 0xFFFF is the halt marker
        if (instruction == 0xFFFF) {
            break;
        }

        // Advance pc BEFORE executing, so a branch
        // can overwrite it with the jump target
        registers[REG_PC]++;
        execute_instruction(instruction);
    }

    // Step 5: write the output string

    FILE *output_file = fopen(argv[3], "w");
    if (!output_file) {
        free(program);
        perror(argv[3]);
        return 1;
    }

    // op holds the address of a null-terminated string in memory.
    // Print bytes until the 0 terminator or the end of memory.
    uint64_t output_addr = registers[REG_OP];
    while (output_addr < STACK_SIZE && stack[output_addr] != 0) {
        fputc(stack[output_addr], output_file);
        output_addr++;
    }

    fflush(output_file);
    fclose(output_file);
    free(program);

    return 0;
}


// Returns 1 if bytes [address, address + width) all fit in memory.
// Written as STACK_SIZE - address >= width instead of
// address + width <= STACK_SIZE, because address + width
// could overflow and wrap around to a small number.
int is_valid_address(uint64_t address, int width) {
    if (address >= STACK_SIZE) return 0;
    return (STACK_SIZE - address) >= (uint64_t)width;
}


// Returns bits start..end (inclusive) of the instruction,
// shifted down to bit 0.
uint16_t extract_bits(uint16_t instruction, int start, int end) {
    int length = end - start + 1;

    // Shift the field down, then mask off everything above it
    return (instruction >> start) & ((1 << length) - 1);
}


// Widens an 8-bit two's complement value to 64 bits.
int64_t sign_extend_8(uint8_t value) {
    // Bit 7 set means negative: fill the upper 56 bits with 1s
    if (value & 0x80) {
        return (int64_t)value | 0xFFFFFFFFFFFFFF00;
    }

    // Positive: upper bits stay 0
    return (int64_t)value;
}


// Reads 8 bytes starting at address, little-endian
// (lowest byte at the lowest address).
uint64_t load_doubleword(uint64_t address) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        // Byte i goes into bits 8*i .. 8*i+7
        value |= ((uint64_t)stack[address + i]) << (i * 8);
    }

    return value;
}


// Writes a 64-bit value as 8 bytes, little-endian.
void store_doubleword(uint64_t address, uint64_t value) {
    for (int i = 0; i < 8; i++) {
        // Shift byte i down to the bottom and keep only those 8 bits
        stack[address + i] = (value >> (i * 8)) & 0xFF;
    }
}


// R-type: rd = rs1 <op> rs2
// Layout: | rd 15-12 | rs1 11-8 | rs2 7-4 | func 3-2 | opcode 1-0 |
void execute_r_type(uint16_t instruction) {
    uint16_t func2 = extract_bits(instruction, 2, 3);
    uint16_t source2 = extract_bits(instruction, 4, 7);
    uint16_t source1 = extract_bits(instruction, 8, 11);
    uint16_t destination = extract_bits(instruction, 12, 15);

    uint64_t result = 0;

    switch (func2) {
        case FUNC_ADD:
            result = registers[source1] + registers[source2];
            #ifdef DEBUG
            printf("0x%04X : add x%-2d x%-2d x%d\n", instruction, destination, source1, source2);
            #endif
            break;

        case FUNC_AND:
            result = registers[source1] & registers[source2];
            #ifdef DEBUG
            printf("0x%04X : and x%-2d x%-2d x%d\n", instruction, destination, source1, source2);
            #endif
            break;

        case FUNC_OR:
            result = registers[source1] | registers[source2];
            #ifdef DEBUG
            printf("0x%04X : or  x%-2d x%-2d x%d\n", instruction, destination, source1, source2);
            #endif
            break;

        case FUNC_SLL:
            result = registers[source1] << (registers[source2] & 63);
            #ifdef DEBUG
            printf("0x%04X : sll x%-2d x%-2d x%d\n", instruction, destination, source1, source2);
            #endif
            break;
    }

    // x0 is hardwired to zero, so writes to it are ignored
    if (destination != 0) {
        registers[destination] = result;
    }
}


// B-type: if rs1 == rs2, jump by offset
// Layout: | rs1 15-12 | rs2 11-8 | offset[6:1] 7-2 | opcode 1-0 |
void execute_b_type(uint16_t instruction) {
    uint16_t destination = extract_bits(instruction, 12, 15);
    uint16_t source1 = extract_bits(instruction, 8, 11);
    uint16_t literal_6_1 = extract_bits(instruction, 2, 7);

    // The offset is in bytes and always even, so bit 0 isn't stored.
    // Shift left by 1 to put it back.
    uint8_t literal_7bit = literal_6_1 << 1;

    // Sign-extend the 7-bit offset (bit 6 is the sign) to 16 bits
    int16_t offset;
    if (literal_7bit & 0x40) {
        offset = literal_7bit | 0xFF80;
    } else {
        offset = literal_7bit;
    }

    #ifdef DEBUG
    printf("0x%04X : beq x%-2d x%-2d %d\n", instruction, destination, source1, offset);
    #endif

    if (registers[destination] == registers[source1]) {
        // pc was already advanced in main, so undo that with -1.
        // offset is in bytes and each instruction is 2 bytes, so divide by 2.
        registers[REG_PC] = registers[REG_PC] - 1 + (offset / 2);
    }
} 


// I-type: load or store between a register and memory
// Layout: | rd 15-12 | rs1 (address) 11-8 | unused 7-4 | func 3-2 | opcode 1-0 |
void execute_i_type(uint16_t instruction) {
    uint16_t func2 = extract_bits(instruction, 2, 3);
    uint16_t source1 = extract_bits(instruction, 8, 11);
    uint16_t destination = extract_bits(instruction, 12, 15);

    uint64_t address = registers[source1];

    // ld/sd touch 8 bytes, lb/sb touch 1 byte.
    // Check the whole range is inside memory before accessing it.
    int width = (func2 == FUNC_LD || func2 == FUNC_SD) ? 8 : 1;
    if (!is_valid_address(address, width)) {
        fprintf(stderr, "Memory access out of bounds: address 0x%llx\n", (unsigned long long)address);
        exit(1);
    }

    switch (func2) {
        case FUNC_LD:
            #ifdef DEBUG
            printf("0x%04X : ld  x%-2d x%d\n", instruction, destination, source1);
            #endif
            // Load 8 bytes (skip if the destination is x0)
            if (destination != 0) {
                registers[destination] = load_doubleword(address);
            }
            break;

        case FUNC_LB:
            #ifdef DEBUG
            printf("0x%04X : lb  x%-2d x%d\n", instruction, destination, source1);
            #endif
            // Load 1 byte, zero-extended to 64 bits (skip if x0)
            if (destination != 0) {
                registers[destination] = (uint64_t)stack[address];
            }
            break;

        case FUNC_SD:
            #ifdef DEBUG
            printf("0x%04X : sd  x%-2d x%d\n", instruction, destination, source1);
            #endif
            // Store all 8 bytes of the register
            store_doubleword(address, registers[destination]);
            break;

        case FUNC_SB:
            #ifdef DEBUG
            printf("0x%04X : sb  x%-2d x%d\n", instruction, destination, source1);
            #endif
            // Store only the lowest byte of the register
            stack[address] = registers[destination] & 0xFF;
            break;
    }
}


// L-type: load an 8-bit signed constant into a register
// Layout: | rd 15-12 | imm[7:0] 11-4 | unused 3-2 | opcode 1-0 |
void execute_l_type(uint16_t instruction) {
    uint16_t destination = extract_bits(instruction, 12, 15);
    uint8_t literal = extract_bits(instruction, 4, 11);

    // Sign-extend so that e.g. 0xFF becomes -1, not 255
    int64_t value = sign_extend_8(literal);

    #ifdef DEBUG
    printf("0x%04X : li  x%-2d %d\n", instruction, destination, (int)value);
    #endif

    // Writes to x0 are ignored
    if (destination != 0) {
        registers[destination] = (uint64_t)value;
    }
}


// Decode: the low 2 bits pick the instruction type,
// then hand off to that type's handler.
void execute_instruction(uint16_t instruction) {
    uint16_t opcode = extract_bits(instruction, 0, 1);

    switch (opcode) {
        case OPCODE_R:
            execute_r_type(instruction);
            break;

        case OPCODE_B:
            execute_b_type(instruction);
            break;

        case OPCODE_I:
            execute_i_type(instruction);
            break;

        case OPCODE_L:
            execute_l_type(instruction);
            break;
    }
}