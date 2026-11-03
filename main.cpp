#include <iostream>
#include <array>
#include "Microprocessor.h"

using namespace std;

int main() {
    Microprocessor cpu;
    array<uint8_t, 4096> program = {0}; // Cała pamięć wyzerowana

    // Przykładowy program: MOV R0, 5
    program[0] = 0b00010100;
    program[1] = 5;
    program[2] = 0b00001101;
    program[3] = 0b00110000;

    cpu.loadProgram(program);
    cpu.run();
    return 0;
}