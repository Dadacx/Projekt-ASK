#include <iostream>
#include <bitset>
#include <limits>
#include "Microprocessor.h"

Microprocessor::Microprocessor() {
    // ustawianie wartości domyślnych
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

    // wyzerowanie pamięci przed wczytaniem nowego programu
    memory.fill(0);

    unsigned int address = 0;
    string line;

    // Pobieranie kolejnych wartości do pamięci
    while (file >> line && address < MEMORY_SIZE) {
        // printf("%s, ",line.c_str());
        if (line.starts_with(">")) { // jeśli wykryje '>' (komentarz) ignoruje reszte bieżącej lini
            // Ignoruje wszystkie znaki w strumieniu aż do znaku nowej linii
            file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } else { // zamienia tekst na 8-bitową przekonwertowaną liczbe i  zapisuje do pamięci
            bitset<8> byteValue(line);
            memory[address] = static_cast<uint8_t>(byteValue.to_ulong());
            address++;
        }
    }

    PC = 0; // zeruje licznik rozkazów
    file.close();
    return true;  // uznaczenie poprawnego wykonania funkcji
}

void Microprocessor::run() {
    bool isRunning = true;

    // wyswietlenie stanu rejestrów
    displayRegs();

    while (isRunning) {
        // zabezpieczenie przed wyjściem poza dostepną pamięć
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
        uint8_t instruction = fetch(); // pobranie kolejnego rozkazy

        decodeAndExecute(instruction); // dekodowanie i wykonanie rozkazu
    }
}

void Microprocessor::displayRegs() {
    // cout << "R[0] = " << (int)R[0] << endl;
    // cout << "R[1] = " << (int)R[1] << endl;
    // cout << "R[2] = " << (int)R[2] << endl;
    // cout << "R[3] = " << (int)R[3] << endl;
    for (int i = 0; i < 4; i++) cout << "R[" << i << "] = " << static_cast<int>(R[i]) << "\t";
    cout << endl;
}

uint8_t Microprocessor::fetch() {
    uint16_t physicalAddress = calculatePhysicalAddress(CS, PC); // liczenie fizycznego adresu
    uint8_t byte = memory[physicalAddress]; // pobranie 1 bajta z pamięci

    PC++;
    return byte;
}

void Microprocessor::decodeAndExecute(uint8_t instruction) {
    uint8_t opcode = (instruction >> 4) & 0b1111;   // wyodrębnienie opcode z przekazanego słowa
    uint8_t args = instruction & 0b1111;            // wyodrebnienie argumentów z przekzanego słowa

    // wspolne wyłuaskanie dla wiekszosci rozkazów
    uint8_t aa = (args >> 2) & 0b11;
    uint8_t bb = args & 0b11;

    cout << "opcode: " << bitset<4>(opcode) << "\t args: " << bitset<4>(args) << endl;

    // dekodowanie instrukcji
    switch (opcode) {
        case OP_MOV_REG_REG:
            // cout << "Przesyłanie danych między rejestrami." << endl;
            exec_MOV_RegReg(aa, bb);
            break;
        case OP_MOV_REG_NAT:
            // cout << "Ładowanie wartości natychmiastowej do rejestru." << endl;
            exec_MOV_RegNat(aa, fetch());
            break;
        case OP_MOV_REG_MEM:
            cout << "Odczyt z pamięci do rejestru. Bit s wybiera segment: 0=CS, 1=DS" << endl;
            break;
        case OP_ADD_REG_REG:
            cout << "Dodawanie dwóch rejestrów. Aktualizuje Flagi." << endl;
            break;
        case OP_ADD_REG_NAT:
            cout << "Dodawanie wartości natychmiastowej. Aktualizuje Flagi." << endl;
            break;
        case OP_SUB_REG_REG:
            cout << "Odejmowanie rejestrów. Aktualizuje Flagi." << endl;
            break;
        case OP_SUB_REG_NAT:
            cout << "Odejmowanie wartości natychmiastowej. Aktualizuje Flagi." << endl;
            break;
        case OP_CMP_REG_NAT:
            cout << "Porównanie rej-nat. Zmienia Flagi." << endl;
            break;
        case OP_CMP_REG_REG:
            cout << "Porównanie rej-rej. Zmienia Flagi." << endl;
            break;
        case OP_LOGIC:
            cout << "Funkcje logiczne. Bity ff wybierają operację: 00= AND, 01=OR, 10= XOR." << endl;
            break;
        case OP_JCC:
            cout <<
                    "Skoki. Bity cc wybierają warunek: 00=JMP (Bezwarunkowy); 01=JE (Gdy Równe); 10=JG (Gdy Większe); 11=JL (Gdy Mniejsze)."
                    << endl;
            break;
        case OP_CALL:
            cout <<
                    "Wywołanie podprogramu. Bit m decyduje o trybie: 0 = CALL Raa (adres w Raa, dł. 1 bajt); 1= CALL [adres] (adres 8-bit, dł. 2 bajty)."
                    << endl;
            break;
        case OP_STACK:
            cout << "Obsługa stosu. Bit p wybiera tryb: 0= PUSH Raa: 1= POP Raa." << endl;
            break;
        case OP_MOVS:
            cout <<
                    "Operacja łańcuchowa z adresowaniem indeksowym (domyślnie używa R1 jako indeksu źródła, R2 jako indeksu celu)."
                    << endl;
            break;
        case OP_MOV_SEG_REG:
            cout << "Ładowanie segmentu. Bit s wybiera segment: 0= CS; 1=DS." << endl;
            break;
        default:
            cout << "Nieznany rozkaz" << endl;
            exit(-1);
            break;
    }
}

uint16_t Microprocessor::calculatePhysicalAddress(uint8_t segmentReg, uint8_t offset) {
    return (segmentReg * 256) + offset; // licznei fizycznego adresu
}

// funkcje wykonujące instrukcje
void Microprocessor::exec_MOV_RegReg(uint8_t regA, uint8_t regB) {
    R[regA] = R[regB];
}

void Microprocessor::exec_MOV_RegNat(uint8_t regA, uint8_t nat) {
    R[regA] = nat;
}
