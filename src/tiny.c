#include <stdio.h> 
#include <stdint.h>
#include <stdlib.h> 
#include <signal.h> 
#include <termios.h>
#include <unistd.h> 
#include <fcntl.h> 
#include <sys/time.h>
#include <sys/types.h> 
#include <sys/termios.h> 
#include <sys/mman.h> 

#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX]; // 2**16 -> 65,536 memory locations

enum {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, // program counter 
    R_COND, // condition flag
    R_COUNT,
}; // registers
uint16_t reg[R_COUNT];

enum {
    OP_BR = 0,  // branch 
    OP_ADD,     // add 
    OP_LD,      // load 
    OP_ST,      // store
    OP_JSR,     // jump register 
    OP_AND,     // bitwise and 
    OP_LDR,     // load register 
    OP_STR,     // store register 
    OP_RTI,     // unused 
    OP_NOT,     // bitwise not 
    OP_LDI,     // load indirect 
    OP_STI,     // store indirect 
    OP_JMP,     // jump 
    OP_RES,     // reserved (unused)
    OP_LEA,     // load effective address 
    OP_TRAP     // execute trap
}; // OpCode
enum {
    FL_POS = 1 << 0, // P 
    FL_ZRO = 1 << 1, // Z 
    FL_NEG = 1 << 2, // N
}; // condition flags
enum {
    TRAP_GETC  = 0x20, // get char from keyboard, not echoed to terminal 
    TRAP_OUT   = 0x21, // output a char 
    TRAP_PUTS  = 0x22, // output a word string 
    TRAP_IN    = 0x23, // get char from keyboard, echoed to terminal
    TRAP_PUTSP = 0x24, // output a byte string 
    TRAP_HALT  = 0x25, // halt the program
}; // Trap Codes
enum {
    MR_KBSR = 0xfe00,  // keyboard status, whether a key has been pressed
    MR_KBDR = 0xfe02,  // keyboard data, identifies which key was pressed
}; // Memory Mapped Registers

struct termios original_tio;

void disable_input_buffering() {
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void handle_interrupt(int signal) {
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

uint16_t sign_extend(uint16_t x, int bit_count) {
    if ((x >> (bit_count - 1)) & 1) 
        x |= (0xffff << bit_count);
    return x;
}

void update_flags(uint16_t r) {
    if (reg[r] == 0) 
        reg[R_COND] = FL_ZRO;
    else if (reg[r] >> 15) // if 1 in the left-most bit, negative
        reg[R_COND] = FL_NEG;
    else 
        reg[R_COND] = FL_POS;
}

void mem_write(uint16_t address, uint16_t val) {
    memory[address] = val;
}

uint16_t mem_read(uint16_t address) {
    if (address == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        } else {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

uint16_t swap16(uint16_t x) { 
    return (x << 8) | (x >> 8); 
}

void read_image_file(FILE* file) {
    // origin tell us where in memory to place the image 
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    // swap to little endian 
    while (read-- > 0) {
        *p = swap16(*p);
        ++p;
    }
}

int read_image(const char* image_path) {
    FILE* file = fopen(image_path, "rb");
    if (!file) return 0;
    read_image_file(file);
    fclose(file);
    return 1;
}

int main(int argc, const char* argv[]) {
    // Load Arguments 
    if (argc < 2) {
        printf("tinyvm [image-file1] ...\n");
        exit(2);
    }
    for (int i = 1; i < argc; i++) {
        if (!read_image(argv[i])) {
            printf("Failed to load image: %s\n", argv[i]);
            exit(1);
        }
    }
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    // Setup 
    reg[R_COND] = FL_ZRO;
    
    // set the pc to the starting position, (default 0x3000)
    enum { PC_START = 0x3000 }; 
    reg[R_PC] = PC_START;

    int running = 1;
    while (running) {
        // FETCH 
        uint16_t instr = mem_read(reg[R_PC]++); 
        uint16_t op = instr >> 12;

        switch (op) {
            case OP_ADD: {
                // Destination Register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                // First Operand (SR1)
                uint16_t r1 = (instr >> 6) & 0x7;
                // check if immediate mode 
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag) {
                    // ADD R2, R3, R4; R2 <- R3 + R4 
                    uint16_t imm5 = sign_extend(instr & 0x1f, 5);
                    reg[r0] = reg[r1] + imm5;
                } else {
                    // ADD R2, R3, #7; R2 <- R3 + 7
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] + reg[r2];
                }
                update_flags(r0);
            } break;
            case OP_AND: {
                // Destination Register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                // First Operand (SR1)
                uint16_t r1 = (instr >> 6) & 0x7;
                // check if immediate mode 
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag) {
                    // AND R2, R3, R4; R2 <- R3 AND R4 
                    uint16_t imm5 = instr & 0x7;
                    reg[r0] = reg[r1] & imm5;
                } else {
                    // AND R2, R3, #7; R2 <- R3 AND #7
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] & reg[r2];
                }
                update_flags(r0);
            } break;
            case OP_NOT: {
                uint16_t r0 = (instr >> 9) & 0x7; // DR 
                uint16_t r1 = (instr >> 6) & 0x7; // SR 

                reg[r0] = ~reg[r1];
                update_flags(r0);
            } break;
            case OP_BR: {
                uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
                uint16_t cond_flag = (instr >> 9) & 0x7;
                if (cond_flag & reg[R_COND]) 
                    reg[R_PC] += pc_offset;
            } break;
            case OP_JMP: {
                uint16_t r1 = (instr >> 6) & 0x7;
                reg[R_PC] = reg[r1];
            } break;
            case OP_JSR: {
                reg[R_R7] = reg[R_PC];
                uint16_t long_flag = (instr >> 11) & 1;
                if (long_flag) {
                    uint16_t pc_offset = sign_extend(instr & 0x7ff, 11);
                    reg[R_PC] += pc_offset;
                } else {
                    uint16_t r1 = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[r1];
                }
            } break;
            case OP_LD: {
                uint16_t r0 = (instr >> 9) & 0x7; // DR 
                uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
                reg[r0] = mem_read(reg[R_PC] + pc_offset);
                update_flags(r0);
            } break;
            case OP_LDI: {
                // Destination Register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                // PCoffset 9 
                uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
                // add pc_offset to the current PC, look at that memory location to get
                // the final address 
                reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                update_flags(r0);
                } break;
            case OP_LDR: {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                uint16_t offset = sign_extend(instr & 0x3f, 6);
                reg[r0] = mem_read(reg[r1] + offset);
                update_flags(r0);
            } break;
            case OP_LEA: {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t offset = sign_extend(instr & 0x1ff, 9);
                reg[r0] = reg[R_PC] + offset;
                update_flags(r0);
            } break;
            case OP_ST: {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t offset = sign_extend(instr & 0x1ff, 9);
                mem_write(reg[R_PC] + offset, reg[r0]);
            } break;
            case OP_STI: {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t offset = sign_extend(instr & 0x1ff, 9);
                mem_write(mem_read(reg[R_PC] + offset), reg[r0]);
            } break;
            case OP_STR: {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                uint16_t offset = sign_extend(instr & 0x3f, 6);
                mem_write(reg[r1] + offset, reg[r0]);
            } break;
            case OP_TRAP: {
                reg[R_R7] = reg[R_PC];
                switch (instr & 0xff) {
                    case TRAP_GETC: { // read a single ascii char 
                        reg[R_R0] = (uint16_t)getchar();
                        update_flags(R_R0);
                    } break; 
                    case TRAP_OUT: {
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                    } break;
                    case TRAP_PUTS: { // outputs a null-terminated string 
                        // one char per word
                        uint16_t* c = memory + reg[R_R0];
                        while (*c) {
                            putc((char)*c, stdout); 
                            ++c;
                        }
                        fflush(stdout);
                    } break;
                    case TRAP_IN: {
                        printf("Enter a character: ");
                        char c = getchar();
                        putc(c, stdout);
                        fflush(stdout);
                        reg[R_R0] = (uint16_t)c;
                        update_flags(R_R0);
                    } break;
                    case TRAP_PUTSP: {
                        uint16_t* c = memory + reg[R_R0];
                        while (*c) {
                            char char1 = (*c) & 0xff;
                            putc(char1, stdout);
                            char char2 = (*c) >> 8;
                            if (char2) putc(char2, stdout);
                            ++c;
                        }
                        fflush(stdout);
                    } break;
                    case TRAP_HALT: {
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                    } break;
                }
            } break;
            case OP_RES:
            case OP_RTI:
            default:
                // BAD OPCODE 
                abort();
                break;
        }
    }
    // shutdown
    restore_input_buffering();
}