#include <iostream>
#include <bitset>
#include <limits>
#include "Microprocessor.h"

Microprocessor::Microprocessor() {
    memory.fill(0);
    CS = 0;
    DS = 0;
    PC = 0;
    SP = 4095;
}

void Microprocessor::loadProgram(const array<uint8_t, MEMORY_SIZE> &program) {
    memory = program;
    PC = 0;
}

bool Microprocessor::loadProgramFromFile(string filename) {
    ifstream file(filename);

    if (!file.is_open()) {
        cerr << "Blad: Nie mozna otworzyc pliku " << filename << endl;
        return false;
    }

    memory.fill(0);
    unsigned int address = 0;
    string line;

    while (file >> line && address < MEMORY_SIZE) {
        if (line.starts_with(">")) {
            file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } else {
            bitset<8> byteValue(line);
            memory[address] = static_cast<uint8_t>(byteValue.to_ulong());
            address++;
        }
    }

    PC = 0;
    file.close();
    return true;
}

void Microprocessor::run() {
    bool isRunning = true;
    displayRegs();

    while (isRunning) {
        if (PC >= MEMORY_SIZE) {
            isRunning = false;
            break;
        }
        step();
        displayRegs();
    }
}

void Microprocessor::step() {
    if (PC < MEMORY_SIZE) {
        uint8_t instruction = fetch();
        decodeAndExecute(instruction);
    }
}

void Microprocessor::displayRegs() {
    for (int i = 0; i < 4; i++) cout << "R[" << i << "] = " << static_cast<int>(R[i]) << "\t";
    cout << endl;
}

uint8_t Microprocessor::fetch() {
    uint16_t physicalAddress = calculatePhysicalAddress(CS, PC);
    uint8_t byte = memory[physicalAddress];
    PC++;
    return byte;
}

void Microprocessor::decodeAndExecute(uint8_t instruction) {
    uint8_t opcode = (instruction >> 4) & 0b1111;
    uint8_t args = instruction & 0b1111;

    uint8_t aa = (args >> 2) & 0b11;
    uint8_t bb = args & 0b11;

    cout << "opcode: " << bitset<4>(opcode) << "\t args: " << bitset<4>(args) << endl;

    switch (opcode) {
        case OP_MOV_REG_REG:
            exec_MOV_RegReg(aa, bb);
            break;
        case OP_MOV_REG_NAT:
            exec_MOV_RegNat(aa, fetch());
            break;
        case OP_MOV_REG_MEM:
            cout << "Odczyt z pamieci (jeszcze do zrobienia)" << endl;
            break;
        case OP_ADD_REG_REG:
            exec_ADD_RegReg(aa, bb);
            break;
        case OP_ADD_REG_NAT:
            exec_ADD_RegNat(aa, fetch());
            break;
        case OP_SUB_REG_REG:
            exec_SUB_RegReg(aa, bb);
            break;
        case OP_SUB_REG_NAT:
            exec_SUB_RegNat(aa, fetch());
            break;
        case OP_CMP_REG_NAT:
            exec_CMP_RegNat(aa, fetch());
            break;
        case OP_CMP_REG_REG:
            exec_CMP_RegReg(aa, bb);
            break;
        case OP_LOGIC:
            exec_LOGIC(aa, bb, fetch());
            break;
        case OP_JCC:
            exec_Jcc(aa, fetch());
            break;
        case OP_CALL:
            // bit m (wyciągnięty z args) decyduje skąd bierzemy adres skoku
            exec_CALL((args >> 1) & 1, aa, fetch());
            break;
        case OP_STACK:
            // bit p (wyciągnięty z args) mówi czy to PUSH czy POP
            exec_PUSH_POP((args >> 1) & 1, aa);
            break;
        case OP_MOVS:
            cout << "Operacje lancuchowe (jeszcze do zrobienia)" << endl;
            break;
        case OP_MOV_SEG_REG:
            cout << "Ladowanie segmentu (jeszcze do zrobienia)" << endl;
            break;
        default:
            cout << "Nieznany rozkaz" << endl;
            exit(-1);
            break;
    }
}

uint16_t Microprocessor::calculatePhysicalAddress(uint8_t segmentReg, uint8_t offset) {
    return (segmentReg * 256) + offset;
}

// --- FUNKCJE WYKONUJĄCE KONKRETNE ROZKAZY ---

void Microprocessor::exec_MOV_RegReg(uint8_t regA, uint8_t regB) {
    R[regA] = R[regB];
}

void Microprocessor::exec_MOV_RegNat(uint8_t regA, uint8_t nat) {
    R[regA] = nat;
}

void Microprocessor::exec_ADD_RegReg(uint8_t regA, uint8_t regB) {
    uint16_t result = R[regA] + R[regB];

    Flags.ZF = ((result & 0xFF) == 0) ? 1 : 0;
    Flags.SF = ((result >> 7) & 1);
    Flags.CF = (result > 255) ? 1 : 0;

    bool aSign = (R[regA] >> 7) & 1;
    bool bSign = (R[regB] >> 7) & 1;
    bool rSign = (result >> 7) & 1;
    Flags.OF = (aSign == bSign && aSign != rSign) ? 1 : 0;

    R[regA] = static_cast<uint8_t>(result & 0xFF);
}

void Microprocessor::exec_ADD_RegNat(uint8_t regA, uint8_t nat) {
    uint16_t result = R[regA] + nat;

    Flags.ZF = ((result & 0xFF) == 0) ? 1 : 0;
    Flags.SF = ((result >> 7) & 1);
    Flags.CF = (result > 255) ? 1 : 0;

    bool aSign = (R[regA] >> 7) & 1;
    bool bSign = (nat >> 7) & 1;
    bool rSign = (result >> 7) & 1;
    Flags.OF = (aSign == bSign && aSign != rSign) ? 1 : 0;

    R[regA] = static_cast<uint8_t>(result & 0xFF);
}

void Microprocessor::exec_SUB_RegReg(uint8_t regA, uint8_t regB) {
    int16_t result = R[regA] - R[regB];

    Flags.ZF = ((result & 0xFF) == 0) ? 1 : 0;
    Flags.SF = ((result >> 7) & 1);
    Flags.CF = (R[regA] < R[regB]) ? 1 : 0;

    bool aSign = (R[regA] >> 7) & 1;
    bool bSign = (R[regB] >> 7) & 1;
    bool rSign = (result >> 7) & 1;
    Flags.OF = (aSign != bSign && aSign != rSign) ? 1 : 0;

    R[regA] = static_cast<uint8_t>(result & 0xFF);
}

void Microprocessor::exec_SUB_RegNat(uint8_t regA, uint8_t nat) {
    int16_t result = R[regA] - nat;

    Flags.ZF = ((result & 0xFF) == 0) ? 1 : 0;
    Flags.SF = ((result >> 7) & 1);
    Flags.CF = (R[regA] < nat) ? 1 : 0;

    bool aSign = (R[regA] >> 7) & 1;
    bool bSign = (nat >> 7) & 1;
    bool rSign = (result >> 7) & 1;
    Flags.OF = (aSign != bSign && aSign != rSign) ? 1 : 0;

    R[regA] = static_cast<uint8_t>(result & 0xFF);
}

void Microprocessor::exec_CMP_RegReg(uint8_t regA, uint8_t regB) {
    int16_t result = R[regA] - R[regB];

    Flags.ZF = ((result & 0xFF) == 0) ? 1 : 0;
    Flags.SF = ((result >> 7) & 1);
    Flags.CF = (R[regA] < R[regB]) ? 1 : 0;

    bool aSign = (R[regA] >> 7) & 1;
    bool bSign = (R[regB] >> 7) & 1;
    bool rSign = (result >> 7) & 1;
    Flags.OF = (aSign != bSign && aSign != rSign) ? 1 : 0;
}

void Microprocessor::exec_CMP_RegNat(uint8_t regA, uint8_t nat) {
    int16_t result = R[regA] - nat;

    Flags.ZF = ((result & 0xFF) == 0) ? 1 : 0;
    Flags.SF = ((result >> 7) & 1);
    Flags.CF = (R[regA] < nat) ? 1 : 0;

    bool aSign = (R[regA] >> 7) & 1;
    bool bSign = (nat >> 7) & 1;
    bool rSign = (result >> 7) & 1;
    Flags.OF = (aSign != bSign && aSign != rSign) ? 1 : 0;
}

void Microprocessor::exec_LOGIC(uint8_t regA, uint8_t func, uint8_t nat) {
    switch (func) {
        case 0b00: R[regA] = R[regA] & nat; break;
        case 0b01: R[regA] = R[regA] | nat; break;
        case 0b10: R[regA] = R[regA] ^ nat; break;
        default: break;
    }

    Flags.ZF = (R[regA] == 0) ? 1 : 0;
    Flags.SF = ((R[regA] >> 7) & 1);
    Flags.CF = 0;
    Flags.OF = 0;
}

void Microprocessor::exec_Jcc(uint8_t condition, uint8_t address) {
    bool shouldJump = false;

    switch (condition) {
        case 0b00: // JMP bezwarunkowy
            shouldJump = true;
            break;
        case 0b01: // JE równe
            if (Flags.ZF == 1) shouldJump = true;
            break;
        case 0b10: // JG większe
            if (Flags.ZF == 0 && Flags.SF == Flags.OF) shouldJump = true;
            break;
        case 0b11: // JL mniejsze
            if (Flags.SF != Flags.OF) shouldJump = true;
            break;
        default: break;
    }

    if (shouldJump) {
        PC = address;
    }
}

void Microprocessor::exec_PUSH_POP(uint8_t mode, uint8_t regA) {
    if (mode == 0) {
        // PUSH - zmniejszamy wskaźnik SP i odkładamy wartość z rejestru do pamięci
        SP--;
        memory[SP] = R[regA];
    } else {
        // POP - czytamy wartość z pamięci do rejestru i podnosimy wskaźnik SP
        R[regA] = memory[SP];
        SP++;
    }
}

void Microprocessor::exec_CALL(uint8_t mode, uint8_t regA, uint8_t address) {
    // Odłożenie adresu powrotnego (PC) na stos przed skokiem do podprogramu
    // PC jest 16-bitowe, a pamięć ma 8 bitów, więc dzielimy adres na dwie połówki
    SP--;
    memory[SP] = (PC >> 8) & 0xFF; // Starszy bajt
    SP--;
    memory[SP] = PC & 0xFF;        // Młodszy bajt

    // Fizyczny skok pod wskazany adres
    if (mode == 0) {
        PC = R[regA];
    } else {
        PC = address;
    }
}