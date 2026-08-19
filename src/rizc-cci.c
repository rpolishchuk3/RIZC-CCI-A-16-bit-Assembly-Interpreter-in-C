#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define STACK_SIZE 1024
#define NUM_REGISTERS 16

#define REG_IP 12
#define REG_OP 13
#define REG_PC 14
#define REG_SP 15

#define OPCODE_R 0b00
#define OPCODE_B 0b01
#define OPCODE_I 0b10
#define OPCODE_L 0b11

#define FUNC_ADD 0b00
#define FUNC_AND 0b01
#define FUNC_OR  0b10
#define FUNC_SLL 0b11

#define FUNC_LD 0b00
#define FUNC_LB 0b01
#define FUNC_SD 0b10
#define FUNC_SB 0b11

uint64_t registers[NUM_REGISTERS];    // stores values for all 16 CPU registers
uint8_t stack[STACK_SIZE];    // 1024-byte memory space for program data
uint16_t *program;    // dynamically allocated storage for program instructions
int program_size;    // number of instructions in the loaded program

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
    if (argc != 4) {    // verify exactly 3 arguments plus program name provided

        return 1;
    }
    
    for (int i = 0; i < NUM_REGISTERS; i++) {    // iterate over each register
        registers[i] = 0;    // set register to zero
    }
    
    FILE *input_file = fopen(argv[1], "r");    // attempt to open input data file
    if (!input_file) {    // verify file was opened successfully

        return 1;
    }
    
    int input_size = 0;    // counter for bytes loaded from input
    uint8_t byte;    // holds single byte during reading
    while (fscanf(input_file, " 0x%hhx", &byte) == 1) {    // read hexadecimal bytes one at a time
        stack[input_size] = byte;    // place byte into stack memory
        input_size++;    // advance to next stack position
    }
    fclose(input_file);    // release input file resources
    
    registers[REG_IP] = 0;    // point input register to beginning of stack
    if (input_size > 0) {    // verify input data exists
        registers[REG_SP] = input_size - 1;    // point stack register to final input byte
    } else {
        registers[REG_SP] = (uint64_t)-1;    // use maximum value to indicate empty stack
    }
    
    FILE *instruction_file = fopen(argv[2], "r");    // attempt to open program file
    if (!instruction_file) {    // verify file was opened successfully

        return 1;
    }
    
    program_size = 0;    // reset instruction counter
    uint16_t temp;    // holds instruction during counting phase
    while (fscanf(instruction_file, " 0x%hx", &temp) == 1) {    // read and count all instructions
        program_size++;    // increment total instruction count
    }
    
    program = (uint16_t*)malloc(program_size * sizeof(uint16_t));    // reserve memory for all instructions
    if (!program) {    // verify memory was allocated successfully
        fclose(instruction_file);    // release file before exit

        return 1;
    }
    
    rewind(instruction_file);    // move back to start of instruction file
    for (int i = 0; i < program_size; i++) {    // iterate through instruction count
        if (fscanf(instruction_file, " 0x%hx", &program[i]) != 1) {    // load each instruction into array
            free(program);    // release instruction memory
            fclose(instruction_file);    // release file before exit

            return 1;
        }
    }
    fclose(instruction_file);    // release instruction file resources
    
    registers[REG_PC] = 0;    // set program counter to first instruction
    while (registers[REG_PC] < (uint64_t)program_size) {    // continue while instructions remain
        uint16_t instruction = program[registers[REG_PC]];    // load next instruction to execute
        if (instruction == 0xFFFF) {    // detect program termination marker

            break;
        }
        registers[REG_PC]++;    // advance to next instruction
        execute_instruction(instruction);    // perform instruction operation
    }
    
    FILE *output_file = fopen(argv[3], "w");    // attempt to create output file
    if (!output_file) {    // verify file was opened successfully
        free(program);    // release instruction memory

        return 1;
    }
    
    uint64_t output_addr = registers[REG_OP];    // retrieve output string location
    while (output_addr < STACK_SIZE && stack[output_addr] != 0) {    // continue until string terminator or boundary
        fputc(stack[output_addr], output_file);    // write single character to file
        output_addr++;    // advance to next character
    }
    
    fflush(output_file);    // force write of buffered data
    fclose(output_file);    // release output file resources
    free(program);    // release instruction memory
    
    return 0;
}

int is_valid_address(uint64_t address, int width) {
    if (address >= STACK_SIZE) return 0;
    return (STACK_SIZE - address) >= (uint64_t)width;
}

uint16_t extract_bits(uint16_t instruction, int start, int end) {
    int length = end - start + 1;    // compute bit field width

    return (instruction >> start) & ((1 << length) - 1);    // isolate and return requested bits
}


int64_t sign_extend_8(uint8_t value) {
    if (value & 0x80) {    // test if negative (bit 7 set)

        return (int64_t)value | 0xFFFFFFFFFFFFFF00;    // fill upper bits with ones for negative
    }

    return (int64_t)value;    // convert positive to 64-bit
}


uint64_t load_doubleword(uint64_t address) {
    uint64_t value = 0;    // start with zero
    for (int i = 0; i < 8; i++) {    // process each of 8 bytes
        value |= ((uint64_t)stack[address + i]) << (i * 8);    // combine byte at correct position
    }

    return value;    // return complete 64-bit value
}


void store_doubleword(uint64_t address, uint64_t value) {
    for (int i = 0; i < 8; i++) {    // process each of 8 bytes
        stack[address + i] = (value >> (i * 8)) & 0xFF;    // isolate and store single byte
    }
}


void execute_r_type(uint16_t instruction) {
    uint16_t func2 = extract_bits(instruction, 2, 3);    // get operation specifier
    uint16_t source2 = extract_bits(instruction, 4, 7);    // get second operand register
    uint16_t source1 = extract_bits(instruction, 8, 11);    // get first operand register
    uint16_t destination = extract_bits(instruction, 12, 15);    // get result register
    
    uint64_t result = 0;    // prepare result storage
    
    switch (func2) {    // select operation based on function code
        case FUNC_ADD:
            result = registers[source1] + registers[source2];    // compute sum of operands
            #ifdef DEBUG
            printf("0x%04X : add x%-2d x%-2d x%d\n", instruction, destination, source1, source2);
            #endif

            break;
        case FUNC_AND:
            result = registers[source1] & registers[source2];    // compute logical AND
            #ifdef DEBUG
            printf("0x%04X : and x%-2d x%-2d x%d\n", instruction, destination, source1, source2);
            #endif

            break;
        case FUNC_OR:
            result = registers[source1] | registers[source2];    // compute logical OR
            #ifdef DEBUG
            printf("0x%04X : or  x%-2d x%-2d x%d\n", instruction, destination, source1, source2);
            #endif

            break;
        case FUNC_SLL:
            result = registers[source1] << registers[source2];    // shift bits left by specified amount
            #ifdef DEBUG
            printf("0x%04X : sll x%-2d x%-2d x%d\n", instruction, destination, source1, source2);
            #endif

            break;
    }
    
    if (destination != 0) {    // verify not writing to hardwired zero register
        registers[destination] = result;    // save computed result
    }
}


void execute_b_type(uint16_t instruction) {
    uint16_t destination = extract_bits(instruction, 12, 15);    // get first comparison register
    uint16_t source1 = extract_bits(instruction, 8, 11);    // get second comparison register
    uint16_t literal_6_1 = extract_bits(instruction, 2, 7);    // get encoded offset bits
    
    uint8_t literal_7bit = literal_6_1 << 1;    // restore missing low bit
    
    int16_t offset;    // will hold signed jump distance
    if (literal_7bit & 0x40) {    // test if offset is negative
        offset = literal_7bit | 0xFF80;    // extend sign to 16 bits
    } else {
        offset = literal_7bit;    // keep positive value as-is
    }
    
    #ifdef DEBUG
    printf("0x%04X : beq x%-2d x%-2d %d\n", instruction, destination, source1, offset);
    #endif
    
    if (registers[destination] == registers[source1]) {    // test if values match
        registers[REG_PC] = registers[REG_PC] - 1 + (offset / 2);    // jump to target instruction
    }
}


void execute_i_type(uint16_t instruction) {
    uint16_t func2 = extract_bits(instruction, 2, 3);    // get memory operation type
    uint16_t source1 = extract_bits(instruction, 8, 11);    // get address register
    uint16_t destination = extract_bits(instruction, 12, 15);    // get data register
    
    uint64_t address = registers[source1];    // retrieve memory address value

    int width = (func2 == FUNC_LD || func2 == FUNC_SD) ? 8 : 1;
    if (!is_valid_address(address, width)) {
        fprintf(stderr, "Memory access out of bounds: address 0x%llx\n", (unsigned long long)address);
        exit(1);
    }
    
    switch (func2) {    // select memory operation
        case FUNC_LD:
            #ifdef DEBUG
            printf("0x%04X : ld  x%-2d x%d\n", instruction, destination, source1);
            #endif
            if (destination != 0) {    // verify not writing to zero register
                registers[destination] = load_doubleword(address);    // read 8 bytes from memory
            }

            break;
        case FUNC_LB:
            #ifdef DEBUG
            printf("0x%04X : lb  x%-2d x%d\n", instruction, destination, source1);
            #endif
            if (destination != 0) {    // verify not writing to zero register
                registers[destination] = (uint64_t)stack[address];    // read single byte with zero padding
            }

            break;
        case FUNC_SD:
            #ifdef DEBUG
            printf("0x%04X : sd  x%-2d x%d\n", instruction, destination, source1);
            #endif
            store_doubleword(address, registers[destination]);    // write 8 bytes to memory

            break;
        case FUNC_SB:
            #ifdef DEBUG
            printf("0x%04X : sb  x%-2d x%d\n", instruction, destination, source1);
            #endif
            stack[address] = registers[destination] & 0xFF;    // write low byte only

            break;
    }
}


void execute_l_type(uint16_t instruction) {
    uint16_t destination = extract_bits(instruction, 12, 15);    // get target register
    uint8_t literal = extract_bits(instruction, 4, 11);    // get constant value
    
    int64_t value = sign_extend_8(literal);    // extend to full register width
    
    #ifdef DEBUG
    printf("0x%04X : li  x%-2d %d\n", instruction, destination, (int)value);
    #endif
    
    if (destination != 0) {    // verify not writing to zero register
        registers[destination] = (uint64_t)value;    // store constant in register
    }
}


void execute_instruction(uint16_t instruction) {
    uint16_t opcode = extract_bits(instruction, 0, 1);    // determine instruction category
    
    switch (opcode) {    // route to appropriate handler
        case OPCODE_R:
            execute_r_type(instruction);    // handle register operation

            break;
        case OPCODE_B:
            execute_b_type(instruction);    // handle branch operation

            break;
        case OPCODE_I:
            execute_i_type(instruction);    // handle memory operation

            break;
        case OPCODE_L:
            execute_l_type(instruction);    // handle load immediate

            break;
    }
}
