/*

LC3.c 

LC-3 Virtual Machine

Author: Andrew Peters
Date: 12/20/2022

*/


// PREPROCESSOR


#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <Windows.h>
#include <conio.h>

#define MEMORY_MAX (1 << 16) // 16-bit registers, 2^16 memory locations 

uint16_t memory[MEMORY_MAX]; // Array that holds all our memory addresses



// MEMORY MAPPED REGISTERS

enum
{
	MR_KBSR = 0xFE00,	// keyboard status
	MR_KBDR = 0xFE02	// keyboard data 
};




// TRAP OPCODES


enum 
{
	TRAP_GETC = 0x20,	// get char from keyboard, not echoed to terminal
	TRAP_OUT = 0x21,	// output a char
	TRAP_PUTS = 0x22,	// output a string
	TRAP_IN = 0x23,		// get char from keyboard and echo to terminal
	TRAP_PUTSP = 0x24,	// output a byte string
	TRAP_HALT = 0x25	// halt the program
	
};



// REGISTERS


enum 
{
	
	R_R0 = 0,
	R_R1,
	R_R2,
	R_R3,
	R_R4,
	R_R5,
	R_R6,
	R_R7,
	R_PC, // Program counter, holds address of next instruction in memory to execute
	R_COND,
	R_COUNT
		
};

uint16_t reg[R_COUNT]; // Array that holds our registers



// CONDITION FLAGS

enum 
{
	
	FL_POS = 1 << 0, // P
	FL_ZRO = 1 << 1, // Z
	FL_NEG = 1 << 2, // N
};



// INSTRUCTION SET -- OPCODES

enum 
{
	
	OP_BR = 0,	// branch
	OP_ADD,		// add
	OP_LD,		// load
	OP_ST,		// store
	OP_JSR,		// jump register
	OP_AND,		// bitwise and
	OP_LDR,		// load register
	OP_STR,		// store register
	OP_RTI,		// unused
	OP_NOT,		// bitwise not
	OP_LDI,		// load indirect
	OP_STI,		// store indirect
	OP_JMP,		// jump
	OP_RES,		// reserved (unused)
	OP_LEA,		// load effective address
	OP_TRAP		// execute trap
	
};


// INPUT BUFFERING

HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void disable_input_buffering()
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); /* save old mode */
    fdwMode = fdwOldMode
            ^ ENABLE_ECHO_INPUT  /* no input echo */
            ^ ENABLE_LINE_INPUT; /* return when one or
                                    more characters are available */
    SetConsoleMode(hStdin, fdwMode); /* set new mode */
    FlushConsoleInputBuffer(hStdin); /* clear buffer */
}

void restore_input_buffering()
{
    SetConsoleMode(hStdin, fdwOldMode);
}

uint16_t check_key()
{
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

// END INPUT BUFFERING





// INTERRUPT HANDLER
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}



// Extends data with bit_count bits to 16 bits for addition
uint16_t sign_extend(uint16_t x, int bit_count) 
{
	if((x >> (bit_count - 1)) & 1) 
	{
		x |= (0xFFFF << bit_count);
	}
	return x;
}


// Swap from big to little endian
uint16_t swap16(uint16_t x)
{
	return (x << 8) | (x >> 8);
}


// Need to update a value's sign when written to a register

void update_flags(uint16_t r) 
{
	if(reg[r] == 0)
	{
		reg[R_COND] = FL_ZRO;
	}
	else if(reg[r] >> 15)
	{
		reg[R_COND] = FL_NEG; // 1 in the leftmost bit means negative
	}
	else
	{
		reg[R_COND] = FL_POS;
	}
	
}




void read_image_file(FILE *file)
{
	// The origin tells us where in memory to place the image
	uint16_t origin;
	fread(&origin, sizeof(origin), 1, file);
	origin = swap16(origin);
	
	// We know the max file size so we only need one fread
	uint16_t max_read = MEMORY_MAX - origin;
	uint16_t *p = memory + origin;
	size_t read = fread(p, sizeof(uint16_t), max_read, file);
	
	// Swap to little endian
	while(read-- > 0)
	{
		*p = swap16(*p);
		++p;
	}
}



int read_image(const char *image_path)
{
	FILE* file = fopen(image_path, "rb");
	if(!file) { return 0; }
	read_image_file(file);
	fclose(file);
	return 1;
}




void mem_write(uint16_t address, uint16_t val)
{
	memory[address] = val;
}



uint16_t mem_read(uint16_t address)
{
	if(address == MR_KBSR)
	{
		if(check_key())
		{
			memory[MR_KBSR] = (1 << 15);
			memory[MR_KBDR] = getchar();
		}
		else
		{
			memory[MR_KBSR] = 0;
		}
	}
	return memory[address];
}









int main(int argc, const char **argv) {
	
	
	signal(SIGINT, handle_interrupt);
	disable_input_buffering();
	
	// LOAD ARGUMENTS
	
	if(argc < 2)
	{
		// show usage string
		printf("LC3 [image-file1] ...\n");
		exit(2);
	}
	
	for(int j = 1; j < argc; j++) 
	{
		if(!read_image(argv[j])) 
		{
			printf("Failed to load image: %s\n", argv[j]);
			exit(1);
		}
	}
	
	// SETUP
	
	// One condition flag must be set at any given time, so initialize with the Z (zero) flag
	reg[R_COND] = FL_ZRO;
	
	// Set PC to starting position, 0x3000 is start
	enum { PC_START = 0x3000 };
	reg[R_PC] = PC_START;
	
	int running = 1;
	while(running) 
	{
		// FETCH
		uint16_t instr = mem_read(reg[R_PC]++);
		uint16_t op = instr >> 12;
		
		switch(op) 
		{
			case OP_ADD: 
			{	
				// Destination register
				uint16_t r0 = (instr >> 9) & 0x7;
				// first operand SR1
				uint16_t r1 = (instr >> 6) & 0x7;
				// grab bit 5 to check for imm mode
				uint16_t imm_flag = (instr >> 5) & 0x1;
				
				if(imm_flag)
				{
					uint16_t imm5 = sign_extend(instr & 0x1F, 5);
					reg[r0] = reg[r1] + imm5;
				}
				else
				{
					uint16_t r2 = instr & 0x7;
					reg[r0] = reg[r1] + reg[r2];
				}
				update_flags(r0);
				break;
			}
			case OP_AND:
			{
				// Destination register
				uint16_t r0 = (instr >> 9) & 0x7;
				// first operand SR1
				uint16_t r1 = (instr >> 6) & 0x7;
				// check if in immediate mode
				uint16_t imm_flag = (instr >> 5) & 0x1;
				
				if(imm_flag)
				{
					uint16_t imm5 = sign_extend(instr & 0x1F, 5);
					reg[r0] = reg[r1] & imm5;
				}
				else
				{
					uint16_t r2 = instr & 0x7;
					reg[r0] = reg[r1] & reg[r2];
				}
				update_flags(r0);
				break;
			}
			case OP_NOT: 
			{
			
				// Destination register
				uint16_t r0 = (instr >> 9) & 0x7;
				// Source register
				uint16_t r1 = (instr >> 6) & 0x7;
				reg[r0] = ~reg[r1];
				break;
			}
			case OP_BR:
			{
				// Get condicion flag (negative, zero, or positive)
				uint16_t cond_flag = (instr >> 9) & 0x7;
				// Get pc offset 
				uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
				
				if(cond_flag & reg[R_COND])
				{
					reg[R_PC] += pc_offset;
				}
				break;
			}
			case OP_JMP:
			{
				// Get register to JMP to (RET occurs when r1 == 0x7)
				uint16_t r1 = (instr >> 6) & 0x7;
				reg[R_PC] = reg[r1];
				break;
			}
			case OP_JSR:
			{
				// PC is saved in R7
				reg[R_R7] = reg[R_PC];
				uint16_t long_flag = (instr >> 11) & 0x1;
				if(long_flag) // JSR
				{
					uint16_t pc_offset = sign_extend(instr & 0x7FF, 11);
					reg[R_PC] += pc_offset;
				}
				else // JSRR
				{
					uint16_t r1 = (instr >> 6) & 0x7;
					reg[R_PC] = reg[r1];
				}
				break;
			}
			case OP_LD:
			{
				// Destination register
				uint16_t r0 = (instr >> 9) & 0x7;
				// PC offset
				uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
				reg[r0] = mem_read(reg[R_PC] + pc_offset);
				update_flags(r0);
				break;
			}
			case OP_LDI:
			{
				// Destination register
				uint16_t r0 = (instr >> 9) & 0x7;
				// PC offset 9
				uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
				// add PC offset to to current PC, and read that memory address to get the final address
				reg[r0] = mem_read(reg[R_PC] + pc_offset);
				update_flags(r0);
				break;
			}
			case OP_LDR:
			{
				// Get destination register
				uint16_t r0 = (instr >> 9) & 0x7;
				// Get base register
				uint16_t r1 = (instr >> 6) & 0x7;
				// PC offset
				uint16_t pc_offset = sign_extend(instr & 0x3F, 6);
				reg[r0] = mem_read(reg[r1] + pc_offset);
				update_flags(r0);
				break;
			}
			case OP_LEA:
			{
				// Destination register
				uint16_t r0 = (instr >> 9) & 0x7;
				// PC offset
				uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
				reg[r0] = reg[R_PC] + pc_offset;
				update_flags(r0);
				break;
			}
			case OP_ST:
			{
				// Source register
				uint16_t r0 = (instr >> 9) & 0x7;
				// PC offset
				uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
				mem_write(reg[R_PC] + pc_offset, reg[r0]);
				break;
			}
			case OP_STI:
			{
				// Source register
				uint16_t r0 = (instr >> 9) & 0x7;
				// PC offset
				uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
				mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
				break;
			}
			case OP_STR:
			{
				// Source register
				uint16_t r0 = (instr >> 9) & 0x7;
				uint16_t r1 = (instr >> 6) & 0x7;
				uint16_t pc_offset = sign_extend(instr & 0x3F, 6);
				mem_write(reg[r1] + pc_offset, reg[r0]);
				break;
			}
			case OP_TRAP:
			{
				reg[R_R7] = reg[R_PC];
				switch(instr & 0xFF)
				{
					case TRAP_GETC:
					{
						reg[R_R0] = (uint16_t)getchar();
						update_flags(R_R0);
						break;
					}
					case TRAP_OUT:
					{
						putc((char)reg[R_R0], stdout);
						fflush(stdout);
						break;
					}
					case TRAP_PUTS:
					{
						// One char per word
						uint16_t *c = memory + reg[R_R0];
						while(*c)
						{
							putc((char)*c, stdout);
							++c;	
						}
						fflush(stdout);
						break;
					}
					case TRAP_IN:
					{
						printf("Enter a character:");
						char c = getchar();
						putc(c, stdout);
						fflush(stdout);
						reg[R_R0] = (uint16_t)c;
						update_flags(R_R0);
						break;
					}
					case TRAP_PUTSP:
					{
						uint16_t *c = memory + reg[R_R0];
						while(*c)
						{
							char char1 = (*c) & 0xFF;
							putc(char1, stdout);
							char char2 = (*c) >> 8;
							if(char2)
							{
								putc(char2, stdout);
							}
							++c;
						}
						fflush(stdout);
						break;
					}
					case TRAP_HALT:
					{
						puts("HALT");
						fflush(stdout);
						running = 0;
						break;
					}
				}
				break;
			}
			case OP_RES:
			case OP_RTI:
			default:
				abort();
				break;
		}
	}
	
	// SHUTDOWN
	
	
}














































