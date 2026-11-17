#include <array>
#include "Microprocessor.h"
#include "ProgramConfig.h"

using namespace std;

// Plik programu używany przez obie wersje aplikacji.
const char *const programFileName = "../program.txt";

// Wersja GUI linkuje ten plik tylko po to, aby pobrać stąd nazwę programu.
// Definicja main() jest wyłączona, żeby nie kolidować z WinMain().
#ifndef ASK_GUI_BUILD
int main() {
    Microprocessor cpu;
    // array<uint8_t, 4096> program = {0}; // Cała pamięć wyzerowana

    // Przykładowy program: MOV R1, 5;  MOV R3, R1
    // program[0] = 0b00010100;
    // program[1] = 5;
    // program[2] = 0b00001101;
    // program[3] = 0b00110000;

    // cpu.loadProgram(program);
    if (!cpu.loadProgramFromFile(programFileName)) return 1;
    cpu.delay = 1;
    cpu.run();
    return 0;
}
#endif