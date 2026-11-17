#ifndef PROJEKT_ASK_MICROPROCESSOR_H
#define PROJEKT_ASK_MICROPROCESSOR_H

#include <array>
#include <cstdint>
#include <fstream>

using namespace std;

class Microprocessor {
private:
    // Pamięć operacyjna: 4096 słów 8-bitowych
    static const uint16_t MEMORY_SIZE = 4096;
    array<uint8_t, MEMORY_SIZE> memory;

    // 4 rejestry uniwersalne (R0, R1, R2, R3)
    uint8_t R[4] = {0};

    // Rejestry segmentowe
    uint8_t CS;
    uint8_t DS;

    // Licznik rozkazów i wskaźnik stosu
    uint16_t PC;
    uint16_t SP;

    // Flagi
    struct StatusFlags {
        uint8_t ZF     : 1; // Flaga zera (1 gdy wynik operacji to 0)
        uint8_t SF     : 1; // Flaga znaku (1 gdy wynik jest ujemny)
        uint8_t CF     : 1; // Flaga przeniesienia
        uint8_t OF     : 1; // Flaga przepełnienia
        uint8_t unused : 4; // Pozostałe 4 bity do pełnego bajtu
    } Flags;

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
    uint16_t calculatePhysicalAddress(uint8_t segmentReg, uint8_t offset);

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

public:
    Microprocessor();

    // ładowanie programu (ze zmiennej lub z pliku)
    void loadProgram(const array<uint8_t, MEMORY_SIZE>& program);
    bool loadProgramFromFile(string filename);

    void run();
    void step();
    void reset();

    unsigned int delay = 0; // opóźnienie przed każdym krokiem (w sekundach)

    // Funkcje wyświetlające
    void displayRegs();
};

#endif //PROJEKT_ASK_MICROPROCESSOR_H