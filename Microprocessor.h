#ifndef PROJEKT_ASK_MICROPROCESSOR_H
#define PROJEKT_ASK_MICROPROCESSOR_H

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

using namespace std;

class Microprocessor {
public:
    // Informacje o ostatnio wykonanym rozkazie (wykorzystywane przez monitor GUI).
    struct InstructionInfo {
        uint16_t address = 0;   // adres fizyczny pierwszego bajtu rozkazu
        uint8_t  opcode  = 0;   // kod operacji (bity 7..4)
        uint8_t  args    = 0;   // argumenty (bity 3..0)
        uint8_t  operand = 0;   // pobrany bajt 2 (jeśli rozkaz go używa)
        uint8_t  length  = 1;   // długość rozkazu w bajtach (1 lub 2)

        // Komórki pamięci użyte przez ostatni rozkaz (0xFFFF = brak).
        uint16_t access[2]    = {0xFFFF, 0xFFFF};
        uint8_t  accessKind[2] = {0, 0};  // 1=dane, 2=stos, 3=MOVS źródło, 4=MOVS cel
    };

private:
    // Pamięć operacyjna: 4096 słów 8-bitowych
    static const uint16_t MEMORY_SIZE = 4096;
    array<uint8_t, MEMORY_SIZE> memory{};

    // 4 rejestry uniwersalne (R0, R1, R2, R3)
    uint8_t R[4] = {0};

    // Rejestry segmentowe
    uint8_t CS = 0;
    uint8_t DS = 0;

    // Licznik rozkazów i wskaźnik stosu
    uint16_t PC = 0;
    uint16_t SP = 4095;

    // Flagi
    struct StatusFlags {
        uint8_t ZF     : 1; // Flaga zera (1 gdy wynik operacji to 0)
        uint8_t SF     : 1; // Flaga znaku (1 gdy wynik jest ujemny)
        uint8_t CF     : 1; // Flaga przeniesienia
        uint8_t OF     : 1; // Flaga przepełnienia
        uint8_t unused : 4; // Pozostałe 4 bity do pełnego bajtu
    } Flags{};

    // Kody operacji
    enum Opcodes {
        OP_MOV_REG_REG = 0b0000,
        OP_MOV_REG_NAT = 0b0001,
        OP_MOV_REG_MEM = 0b0010,
        OP_ADD_REG_REG = 0b0100,
        OP_ADD_REG_NAT = 0b0101,
        OP_SUB_REG_REG = 0b0110,
        OP_SUB_REG_NAT = 0b0111,
        OP_CMP_REG_NAT = 0b1000,
        OP_CMP_REG_REG = 0b1001,
        OP_LOGIC       = 0b1010,
        OP_JCC         = 0b1011,
        OP_CALL        = 0b1100,
        OP_STACK       = 0b1101,
        OP_MOVS        = 0b1110,
        OP_MOV_SEG_REG = 0b1111
    };

    // Cykl rozkazu
    uint8_t fetch();
    void decodeAndExecute(uint8_t instruction);
    uint16_t calculatePhysicalAddress(uint8_t segmentReg, uint8_t offset) const;

    // Szkielety instrukcji
    void exec_MOV_RegReg(uint8_t regA, uint8_t regB);
    void exec_MOV_RegNat(uint8_t regA, uint8_t nat);
    void exec_MOV_RegMem(uint8_t regA, uint8_t segmentBit, uint8_t offset);
    void exec_MOV_SegReg(uint8_t segmentBit, uint8_t regA);

    void exec_ADD_RegReg(uint8_t regA, uint8_t regB);
    void exec_ADD_RegNat(uint8_t regA, uint8_t nat);
    void exec_SUB_RegReg(uint8_t regA, uint8_t regB);
    void exec_SUB_RegNat(uint8_t regA, uint8_t nat);

    void exec_CMP_RegNat(uint8_t regA, uint8_t nat);
    void exec_CMP_RegReg(uint8_t regA, uint8_t regB);

    void exec_LOGIC(uint8_t regA, uint8_t func, uint8_t nat);

    void exec_Jcc(uint8_t condition, uint8_t address);
    void exec_CALL(uint8_t mode, uint8_t regA, uint8_t address);

    void exec_PUSH_POP(uint8_t mode, uint8_t regA);
    void exec_MOVS();

    // Stan pomocniczy (widoczny w GUI)
    bool halted = false;
    unsigned long long cycleCount = 0;
    InstructionInfo lastInstr{};

public:
    Microprocessor();

    // ładowanie programu (ze zmiennej lub z pliku)
    void loadProgram(const array<uint8_t, MEMORY_SIZE>& program);
    bool loadProgramFromFile(string filename);

    void run();
    void step();
    // Pojedynczy krok bez opóźnienia (używany przez timer w GUI).
    void stepOnce();
    void reset();

    unsigned int delay = 0; // opóźnienie przed każdym krokiem (w sekundach, tryb CLI)
    bool trace = false;     // wypisywanie przebiegu na konsolę (domyślnie wyłączone w GUI)

    // Funkcje wyświetlające (tryb CLI)
    void displayRegs();

    // --- Gettery / akcesory dla monitora graficznego ---

    uint8_t  reg(unsigned int i) const          { return R[i]; }
    uint8_t  getCS() const                      { return CS; }
    uint8_t  getDS() const                      { return DS; }
    uint16_t getPC() const                      { return PC; }
    uint16_t getSP() const                      { return SP; }

    uint8_t  getZF() const                      { return Flags.ZF; }
    uint8_t  getSF() const                      { return Flags.SF; }
    uint8_t  getCF() const                      { return Flags.CF; }
    uint8_t  getOF() const                      { return Flags.OF; }

    uint8_t  readMem(uint16_t address) const    { return memory[address]; }
    void     writeMem(uint16_t address, uint8_t value) { memory[address] = value; }
    void     setPC(uint16_t address)            { PC = address; halted = false; }

    bool     isHalted() const                   { return halted; }
    unsigned long long cycles() const           { return cycleCount; }
    const InstructionInfo& lastInstruction() const { return lastInstr; }

    // Adres fizyczny wskazywany przez CS:PC (potrzebny do podświetlenia w pamięci)
    uint16_t currentPhysicalPC() const          { return calculatePhysicalAddress(CS, PC); }
};

#endif //PROJEKT_ASK_MICROPROCESSOR_H
