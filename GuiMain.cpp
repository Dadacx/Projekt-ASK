// Monitor graficzny mikroprocesora ASK-8.
// AplikacjaWin32 bez zewnętrznych zależności – korzysta wyłącznie z Windows API
// oraz z klasy Microprocessor.

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "Microprocessor.h"
#include "ProgramConfig.h"

// ---------------------------------------------------------------------------
// Ustawienia wyglądu
// ---------------------------------------------------------------------------

const int CLIENT_W = 1360;
const int CLIENT_H = 860;

const COLORREF C_BG      = RGB(13, 17, 24);
const COLORREF C_CARD    = RGB(22, 28, 40);
const COLORREF C_CARD2   = RGB(29, 36, 51);
const COLORREF C_BORDER  = RGB(45, 57, 78);
const COLORREF C_TEXT    = RGB(230, 237, 243);
const COLORREF C_MUTED   = RGB(139, 152, 169);
const COLORREF C_ACCENT  = RGB(78, 168, 255);
const COLORREF C_GREEN   = RGB(63, 214, 140);
const COLORREF C_AMBER   = RGB(255, 180, 84);
const COLORREF C_RED     = RGB(255, 107, 107);
const COLORREF C_PURPLE  = RGB(185, 131, 255);

// Układ paneli
const int PAD = 16, GAP = 12;
const int LEFT_W = 344, MID_W = 470, RIGHT_W = 470;
const int LEFT_X = PAD;
const int MID_X  = LEFT_X + LEFT_W + GAP;
const int RIGHT_X = MID_X + MID_W + GAP;

const int REG_Y = 76,  REG_H = 196;
const int SEG_Y = 284, SEG_H = 118;
const int FLG_Y = 414, FLG_H = 86;
const int CTL_Y = 512, CTL_H = 300;

const int PRG_Y = 76,  PRG_H = 470;
const int LOG_Y = 558, LOG_H = 254;
const int MEM_Y = 76,  MEM_H = 736;

enum ControlId {
    ID_BTN_LOAD = 101,
    ID_BTN_STEP,
    ID_BTN_RUN,
    ID_BTN_RESET,
    ID_SLIDER_DELAY,
    ID_LIST_PROGRAM,
    ID_LIST_MEMORY,
    ID_LIST_LOG,
    ID_TIMER_CPU = 201,
    ID_TIMER_UI
};

// ---------------------------------------------------------------------------
// Stan globalny
// ---------------------------------------------------------------------------

struct DisasmRow {
    uint16_t address;
    uint8_t  length;
    uint8_t  kind;   // 0=zwykły, 1=skok, 2=CALL, 3=HALT, 4=stos
    std::wstring bytes;
    std::wstring text;
};

struct LogEntry {
    std::wstring text;
    uint8_t kind;
};

static Microprocessor g_cpu;
static HWND g_hwnd = nullptr;
static HWND g_hBtnLoad, g_hBtnStep, g_hBtnRun, g_hBtnReset;
static HWND g_hSlider, g_hListProgram, g_hListMemory, g_hListLog;

static HFONT g_fontUI, g_fontMono, g_fontBig, g_fontTitle, g_fontSmall;

static bool g_running = false;
static int  g_delayMs = 100;
static DWORD64 g_regFlash[4] = {0, 0, 0, 0};
static DWORD64 g_segFlash[4] = {0, 0, 0, 0};  // CS, DS, PC, SP
static std::wstring g_loadedFile = L"(brak programu)";
static std::vector<DisasmRow> g_disasm;
static std::vector<LogEntry> g_log;

static void refreshProgramList();
static void stopRunning();

// Listy są kontrolkami-potomkami z własnymi HWND. Samo InvalidateRect(hwnd, ...)
// odświeża tylko okno nadrzędne (WS_CLIPCHILDREN wycina obszary dzieci), więc
// owner-draw listy nie dostają WM_DRAWITEM i kolory „zastają" poprzedni stan.
static void refreshViews() {
    if (g_hwnd)         InvalidateRect(g_hwnd, nullptr, FALSE);
    if (g_hListProgram) InvalidateRect(g_hListProgram, nullptr, FALSE);
    if (g_hListMemory)  InvalidateRect(g_hListMemory, nullptr, FALSE);
    if (g_hListLog)     InvalidateRect(g_hListLog, nullptr, FALSE);
}

// Po kroku nie potrzebujemy przemalowywać całego okna i wszystkich list.
// Zmieniają się tylko: rejestry/segmenty/flagi (flash), pasek statusu,
// nagłówek (status) oraz kolory komórek w pamięci.
// Lista programu i dziennik odświeżają się same przez LB_SETCURSEL / LB_ADDSTRING.
static void refreshAfterStep() {
    if (g_hwnd) {
        const RECT areas[] = {
            {0, 0, CLIENT_W, 56},                                            // nagłówek
            {LEFT_X, REG_Y, LEFT_X + LEFT_W, REG_Y + REG_H},                 // rejestry
            {LEFT_X, SEG_Y, LEFT_X + LEFT_W, SEG_Y + SEG_H},                 // segmenty
            {LEFT_X, FLG_Y, LEFT_X + LEFT_W, FLG_Y + FLG_H},                 // flagi
            {0, CLIENT_H - 36, CLIENT_W, CLIENT_H}                           // pasek statusu
        };
        for (const RECT &r : areas) InvalidateRect(g_hwnd, &r, FALSE);
    }
    if (g_hListMemory) InvalidateRect(g_hListMemory, nullptr, FALSE);
}

// Tylko migające pola rejestrów i segmentów (używane przez timer UI).
static void refreshFlashAreas() {
    if (!g_hwnd) return;
    RECT regs{LEFT_X, REG_Y, LEFT_X + LEFT_W, REG_Y + REG_H};
    RECT segs{LEFT_X, SEG_Y, LEFT_X + LEFT_W, SEG_Y + SEG_H};
    InvalidateRect(g_hwnd, &regs, FALSE);
    InvalidateRect(g_hwnd, &segs, FALSE);
}

// ---------------------------------------------------------------------------
// Pomocnicze funkcje tekstowe / dekodowanie
// ---------------------------------------------------------------------------

static std::wstring hex2(uint8_t v) {
    wchar_t buf[8];
    swprintf(buf, 8, L"%02X", v);
    return buf;
}

// Operandy liczbowe w mnemonikach (wartości natychmiastowe, adresy skoków,
// offsety CS/DS) wyświetlamy dziesiętnie. Bajty rozkazu zostają w hex.
static std::wstring dec(uint8_t v) {
    return std::to_wstring(static_cast<unsigned int>(v));
}

static std::wstring hex4(uint16_t v) {
    wchar_t buf[16];
    swprintf(buf, 16, L"%04X", v);
    return buf;
}

static std::wstring formatDelay(int ms) {
    if (ms <= 0) return L"0 ms (maks. prędkość)";
    if (ms < 1000) return std::to_wstring(ms) + L" ms";
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1f s", ms / 1000.0);
    return buf;
}

// WM_TIMER nie przyjmuje 0 – najszybszy możliwy krok to 1 ms.
static UINT cpuTimerInterval() {
    return static_cast<UINT>(std::max(1, g_delayMs));
}

static std::wstring regName(uint8_t r) {
    return L"R" + std::to_wstring(r);
}

static int instrLength(uint8_t op) {
    switch (op >> 4) {
        case 0x0: case 0x4: case 0x6: case 0x9:
        case 0xD: case 0xE: case 0xF:
            return 1;
        default:
            return 2;   // CALL pobiera bajt 2 także w trybie m=0
    }
}

static uint8_t opcodeKind(uint8_t opcode) {
    switch (opcode) {
        case 0x3: return 3;   // HALT
        case 0xB: return 1;   // skok
        case 0xC: return 2;   // CALL
        case 0xD: return 4;   // stos
        default:  return 0;
    }
}

// Zamienia bajt rozkazu na tekst mnemoniku.
static std::wstring decodeText(uint8_t op, uint8_t operand, uint8_t &len, uint8_t &kind) {
    uint8_t o  = (op >> 4) & 0xF;
    uint8_t a  = op & 0xF;
    uint8_t aa = (a >> 2) & 0x3;
    uint8_t bb = a & 0x3;
    len = static_cast<uint8_t>(instrLength(op));
    kind = 0;

    switch (o) {
        case 0x0:
            if (a == 0) return L"NOP";
            return L"MOV " + regName(aa) + L", " + regName(bb);
        case 0x1: return L"MOV " + regName(aa) + L", " + dec(operand);
        case 0x2: return L"MOV " + regName(aa) + L", " + ((bb & 2) ? L"DS" : L"CS")
                         + L":[" + dec(operand) + L"]";
        case 0x4: return L"ADD " + regName(aa) + L", " + regName(bb);
        case 0x5: return L"ADD " + regName(aa) + L", " + dec(operand);
        case 0x6: return L"SUB " + regName(aa) + L", " + regName(bb);
        case 0x7: return L"SUB " + regName(aa) + L", " + dec(operand);
        case 0x8: return L"CMP " + regName(aa) + L", " + dec(operand);
        case 0x9: return L"CMP " + regName(aa) + L", " + regName(bb);
        case 0xA: {
            const wchar_t *n = (bb == 0) ? L"AND" : (bb == 1) ? L"OR" : (bb == 2) ? L"XOR" : L"???";
            return std::wstring(n) + L" " + regName(aa) + L", " + dec(operand);
        }
        case 0xB: {
            const wchar_t *n = (aa == 0) ? L"JMP" : (aa == 1) ? L"JE" : (aa == 2) ? L"JG" : L"JL";
            kind = 1;
            return std::wstring(n) + L" " + dec(operand);
        }
        case 0xC:
            kind = 2;
            if ((a >> 1) & 1) return L"CALL " + dec(operand);
            return L"CALL " + regName(aa);
        case 0xD:
            kind = 4;
            return (((a >> 1) & 1) ? L"POP " : L"PUSH ") + regName(aa);
        case 0xE: return L"MOVS";
        case 0xF: return std::wstring(L"MOV ") + (((aa >> 1) & 1) ? L"DS" : L"CS")
                         + L", " + regName(bb);
        default:
            kind = 3;
            return L"HALT";
    }
}

static std::wstring formatInstruction(uint8_t opcode, uint8_t args, uint8_t operand) {
    uint8_t op = static_cast<uint8_t>((opcode << 4) | args);
    uint8_t len = 1, kind = 0;
    return decodeText(op, operand, len, kind);
}

// Dekodowanie „przez przejścia”: podążamy za skokami, dzięki czemu dane
// wplecione w kod (np. blok DANE w program2.txt) nie są mylone z rozkazami.
static void rebuildDisassembly() {
    g_disasm.clear();

    std::vector<uint8_t> visited(4096, 0);
    std::vector<uint16_t> work;
    int regConst[4] = {-1, -1, -1, -1};
    work.push_back(0);

    while (!work.empty() && g_disasm.size() < 600) {
        uint16_t addr = work.back();
        work.pop_back();
        if (addr >= 4096 || visited[addr]) continue;
        visited[addr] = 1;

        uint8_t op = g_cpu.readMem(addr);
        uint8_t o = (op >> 4) & 0xF;

        if (o == 3) {   // nieużywany kod -> HALT
            DisasmRow row;
            row.address = addr;
            row.length = 1;
            row.kind = 3;
            row.bytes = hex2(op) + L"   ";
            row.text = L"HALT";
            g_disasm.push_back(row);
            continue;
        }

        uint8_t len = 0, kind = 0;
        uint8_t operand = (instrLength(op) == 2) ? g_cpu.readMem(static_cast<uint16_t>(addr + 1)) : 0;
        std::wstring text = decodeText(op, operand, len, kind);

        DisasmRow row;
        row.address = addr;
        row.length = len;
        row.kind = kind;
        row.bytes = hex2(op) + (len == 2 ? L" " + hex2(operand) : L"   ");
        row.text = text;
        g_disasm.push_back(row);

        if (o == 0x1) regConst[(op >> 2) & 0x3] = operand;

        uint16_t next = static_cast<uint16_t>(addr + len);
        if (o == 0xB) {
            work.push_back(operand);
            if (((op >> 2) & 0x3) != 0) work.push_back(next);   // skok warunkowy
        } else if (o == 0xC) {
            work.push_back(next);
            if ((op & 0b10) == 0) {
                uint8_t r = (op >> 2) & 0x3;
                if (regConst[r] >= 0) work.push_back(static_cast<uint16_t>(regConst[r]));
            } else {
                work.push_back(operand);
            }
        } else {
            work.push_back(next);
        }
    }

    std::sort(g_disasm.begin(), g_disasm.end(),
              [](const DisasmRow &a, const DisasmRow &b) { return a.address < b.address; });
}

// ---------------------------------------------------------------------------
// Rysowanie – pomocnicy
// ---------------------------------------------------------------------------

// Widok pamięci rysuje kilkaset prostokątów na jedno odświeżenie. Tworzenie
// i niszczenie brusha dla każdej komórki jest kosztowne i potęguje migotanie,
// więc trzymamy małą pulę GDI brushów przez cały czas życia programu.
static HBRUSH cachedSolidBrush(COLORREF color) {
    static std::vector<std::pair<COLORREF, HBRUSH>> cache;
    for (const auto &entry : cache)
        if (entry.first == color) return entry.second;
    HBRUSH brush = CreateSolidBrush(color);
    cache.emplace_back(color, brush);
    return brush;
}

static void FillRectColor(HDC hdc, RECT r, COLORREF color) {
    FillRect(hdc, &r, cachedSolidBrush(color));
}

static void FillRound(HDC hdc, RECT r, int radius, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void DrawTextAt(HDC hdc, RECT r, const wchar_t *text, HFONT font, COLORREF color, UINT flags) {
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text, -1, &r, flags);
    SelectObject(hdc, old);
}

static void DrawCard(HDC hdc, int x, int y, int w, int h, const wchar_t *title, COLORREF accent = C_ACCENT) {
    RECT r{x, y, x + w, y + h};
    FillRound(hdc, r, 10, C_CARD, C_BORDER);
    RECT bar{x + 1, y + 12, x + 5, y + 34};
    FillRectColor(hdc, bar, accent);
    RECT tr{x + 14, y + 8, x + w - 10, y + 34};
    DrawTextAt(hdc, tr, title, g_fontUI, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    HPEN pen = CreatePen(PS_SOLID, 1, C_BORDER);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, x + 12, y + 37, nullptr);
    LineTo(hdc, x + w - 12, y + 37);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static void drawHeader(HDC hdc) {
    RECT bar{0, 0, CLIENT_W, 5};
    FillRectColor(hdc, bar, C_ACCENT);

    RECT title{LEFT_X, 14, LEFT_X + 620, 52};
    DrawTextAt(hdc, title, L"MONITOR MIKROPROCESORA  •  ASK-8", g_fontTitle, C_TEXT,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const wchar_t *status;
    COLORREF statusColor;
    if (g_cpu.isHalted())       { status = L"ZATRZYMANY (HALT)"; statusColor = C_RED; }
    else if (g_running)         { status = L"PRACA CIĄGŁA";      statusColor = C_GREEN; }
    else                        { status = L"PAUZA / KROK";      statusColor = C_AMBER; }

    RECT pill{CLIENT_W - 250, 18, CLIENT_W - 20, 50};
    FillRound(hdc, pill, 12, C_CARD2, statusColor);
    RECT dot{pill.left + 12, 29, pill.left + 24, 41};
    HBRUSH previous = (HBRUSH)SelectObject(hdc, CreateSolidBrush(statusColor));
    Ellipse(hdc, dot.left, dot.top, dot.right, dot.bottom);
    HBRUSH tmp = (HBRUSH)SelectObject(hdc, previous);
    DeleteObject(tmp);
    RECT sr{pill.left + 32, pill.top, pill.right - 10, pill.bottom};
    DrawTextAt(hdc, sr, status, g_fontUI, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void drawRegisters(HDC hdc) {
    DrawCard(hdc, LEFT_X, REG_Y, LEFT_W, REG_H, L"REJESTRY UNIWERSALNE");

    const int boxW = 159, boxH = 72;
    for (int i = 0; i < 4; ++i) {
        int col = i % 2, row = i / 2;
        int bx = LEFT_X + 8 + col * (boxW + 10);
        int by = REG_Y + 42 + row * (boxH + 8);
        RECT box{bx, by, bx + boxW, by + boxH};
        bool flash = GetTickCount64() < g_regFlash[i];
        FillRound(hdc, box, 8, flash ? RGB(28, 56, 88) : C_CARD2, flash ? C_ACCENT : C_BORDER);

        RECT label{bx + 12, by + 8, bx + 55, by + 26};
        DrawTextAt(hdc, label, regName(i).c_str(), g_fontUI, C_ACCENT,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        wchar_t hex[8], dec[16];
        swprintf(hex, 8, L"%02X", g_cpu.reg(i));
        swprintf(dec, 16, L"%u", g_cpu.reg(i));
        RECT vr{bx + 55, by + 6, bx + boxW - 10, by + 44};
        DrawTextAt(hdc, vr, hex, g_fontBig, flash ? C_TEXT : C_ACCENT,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        RECT dr{bx + 55, by + 44, bx + boxW - 10, by + 64};
        DrawTextAt(hdc, dr, dec, g_fontSmall, C_MUTED,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void drawSegments(HDC hdc) {
    DrawCard(hdc, LEFT_X, SEG_Y, LEFT_W, SEG_H, L"SEGMENTY I LICZNIKI", C_PURPLE);

    const int boxW = 159, boxH = 34;
    const wchar_t *names[4] = {L"CS", L"DS", L"PC", L"SP"};
    DWORD64 now = GetTickCount64();

    for (int i = 0; i < 4; ++i) {
        int col = i % 2, row = i / 2;
        int bx = LEFT_X + 8 + col * (boxW + 10);
        int by = SEG_Y + 42 + row * (boxH + 8);
        RECT box{bx, by, bx + boxW, by + boxH};
        bool flash = now < g_segFlash[i];
        FillRound(hdc, box, 7, flash ? RGB(28, 56, 88) : C_CARD2, flash ? C_ACCENT : C_BORDER);

        RECT label{bx + 10, by, bx + 50, by + boxH};
        DrawTextAt(hdc, label, names[i], g_fontUI, C_PURPLE, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        wchar_t val[16];
        if (i == 0)      swprintf(val, 16, L"%02X", g_cpu.getCS());
        else if (i == 1) swprintf(val, 16, L"%02X", g_cpu.getDS());
        else if (i == 2) swprintf(val, 16, L"%04X", g_cpu.getPC());
        else             swprintf(val, 16, L"%04X", g_cpu.getSP());

        RECT vr{bx + 50, by, bx + boxW - 8, by + boxH};
        DrawTextAt(hdc, vr, val, g_fontMono, C_TEXT, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void drawFlags(HDC hdc) {
    DrawCard(hdc, LEFT_X, FLG_Y, LEFT_W, FLG_H, L"FLAGI STANU", C_GREEN);

    const wchar_t *names[4] = {L"ZF", L"SF", L"CF", L"OF"};
    uint8_t values[4] = {g_cpu.getZF(), g_cpu.getSF(), g_cpu.getCF(), g_cpu.getOF()};
    const int w = 76, gap = 8;

    for (int i = 0; i < 4; ++i) {
        int x = LEFT_X + 8 + i * (w + gap);
        RECT box{x, FLG_Y + 40, x + w, FLG_Y + 74};
        COLORREF on = values[i] ? C_GREEN : C_BORDER;
        FillRound(hdc, box, 8, values[i] ? RGB(22, 58, 45) : C_CARD2, on);
        wchar_t text[16];
        swprintf(text, 16, L"%ls %u", names[i], values[i]);
        DrawTextAt(hdc, box, text, g_fontMono, values[i] ? C_GREEN : C_MUTED,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static void drawControls(HDC hdc) {
    DrawCard(hdc, LEFT_X, CTL_Y, LEFT_W, CTL_H, L"STEROWANIE", C_AMBER);

    std::wstring info = L"Opóźnienie: " + formatDelay(g_delayMs) + L" / krok";
    RECT lr{LEFT_X + 10, CTL_Y + 148, LEFT_X + LEFT_W - 10, CTL_Y + 172};
    DrawTextAt(hdc, lr, info.c_str(), g_fontUI, C_AMBER, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT scale{LEFT_X + 10, CTL_Y + 210, LEFT_X + LEFT_W - 10, CTL_Y + 226};
    DrawTextAt(hdc, RECT{scale.left, scale.top, scale.left + 60, scale.bottom},
               L"0", g_fontSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextAt(hdc, RECT{scale.left + 100, scale.top, scale.right - 100, scale.bottom},
               L"5 s", g_fontSmall, C_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextAt(hdc, RECT{scale.right - 60, scale.top, scale.right, scale.bottom},
               L"10 s", g_fontSmall, C_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    RECT hint{LEFT_X + 10, CTL_Y + 226, LEFT_X + LEFT_W - 10, CTL_Y + 248};
    DrawTextAt(hdc, hint, L"F5 start/pauza   •   F10 krok   •   Ctrl+O wczytaj",
               g_fontSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT hint2{LEFT_X + 10, CTL_Y + 250, LEFT_X + LEFT_W - 10, CTL_Y + 272};
    std::wstring file = L"Program: " + g_loadedFile;
    DrawTextAt(hdc, hint2, file.c_str(), g_fontSmall, C_MUTED,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT hint3{LEFT_X + 10, CTL_Y + 272, LEFT_X + LEFT_W - 10, CTL_Y + 294};
    DrawTextAt(hdc, hint3, L"PC:niebieski  SP:zielony  dane:żółty  stos:fioletowy",
               g_fontSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void drawStatusBar(HDC hdc) {
    RECT r{0, CLIENT_H - 36, CLIENT_W, CLIENT_H};
    FillRectColor(hdc, r, C_CARD);
    RECT line{0, CLIENT_H - 36, CLIENT_W, CLIENT_H - 35};
    FillRectColor(hdc, line, C_BORDER);

    wchar_t left[512], middle[256], right[256];
    swprintf(left, 512, L"  Plik: %ls", g_loadedFile.c_str());
    swprintf(middle, 256, L"Cykle: %llu", (unsigned long long)g_cpu.cycles());
    swprintf(right, 256, L"Tryb: %ls   |   Opóźnienie: %ls   |   PC: 0x%04X   SP: 0x%04X",
             g_running ? L"CIĄGŁY" : L"KROK", formatDelay(g_delayMs).c_str(),
             g_cpu.getPC(), g_cpu.getSP());

    DrawTextAt(hdc, RECT{10, CLIENT_H - 34, CLIENT_W / 2, CLIENT_H - 2}, left,
               g_fontSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextAt(hdc, RECT{CLIENT_W / 2 - 160, CLIENT_H - 34, CLIENT_W / 2 + 160, CLIENT_H - 2}, middle,
               g_fontSmall, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextAt(hdc, RECT{CLIENT_W / 2 + 160, CLIENT_H - 34, CLIENT_W - 10, CLIENT_H - 2}, right,
               g_fontSmall, C_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Rysowanie elementów list (owner-draw)
// ---------------------------------------------------------------------------

static void drawProgramItem(DRAWITEMSTRUCT &di) {
    int idx = static_cast<int>(di.itemID);
    if (idx < 0 || idx >= static_cast<int>(g_disasm.size())) return;

    const DisasmRow &row = g_disasm[idx];
    uint16_t pcPhys = g_cpu.currentPhysicalPC();
    bool current = (row.address == pcPhys);
    bool selected = (di.itemState & ODS_SELECTED) != 0;

    COLORREF bg = current ? RGB(26, 54, 86) : (selected ? RGB(38, 47, 66) : C_CARD2);
    FillRectColor(di.hDC, di.rcItem, bg);

    if (current) {
        RECT bar{di.rcItem.left, di.rcItem.top, di.rcItem.left + 4, di.rcItem.bottom};
        FillRectColor(di.hDC, bar, C_ACCENT);
    }

    COLORREF addrColor = current ? C_ACCENT : C_MUTED;
    COLORREF byteColor = current ? RGB(190, 220, 255) : RGB(120, 160, 200);
    COLORREF textColor = C_TEXT;
    if (row.kind == 1) textColor = C_AMBER;
    else if (row.kind == 2) textColor = C_PURPLE;
    else if (row.kind == 3) textColor = C_RED;
    else if (row.kind == 4) textColor = C_GREEN;

    RECT ra{di.rcItem.left + 10, di.rcItem.top, di.rcItem.left + 62, di.rcItem.bottom};
    DrawTextAt(di.hDC, ra, hex4(row.address).c_str(), g_fontMono, addrColor,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT rb{di.rcItem.left + 66, di.rcItem.top, di.rcItem.left + 150, di.rcItem.bottom};
    DrawTextAt(di.hDC, rb, row.bytes.c_str(), g_fontMono, byteColor,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT rt{di.rcItem.left + 158, di.rcItem.top, di.rcItem.right - 8, di.rcItem.bottom};
    DrawTextAt(di.hDC, rt, row.text.c_str(), g_fontMono, textColor,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (di.itemState & ODS_FOCUS) DrawFocusRect(di.hDC, &di.rcItem);
}

// Pamięć jest płaska (4096 B), ale adres fizyczny to CS/DS * 256 + offset,
// czyli 16 segmentów po 256 B. W widoku każdy segment dostaje nagłówek
// i 16 wierszy po 16 bajtów.
static const int MEM_SEGMENT_SIZE = 256;
static const int MEM_SEGMENT_COUNT = 16;
static const int MEM_ROWS_PER_SEGMENT = 16;
static const int MEM_ITEMS_PER_SEGMENT = MEM_ROWS_PER_SEGMENT + 1;
static const int MEM_TOTAL_ITEMS = MEM_SEGMENT_COUNT * MEM_ITEMS_PER_SEGMENT;

static bool isMemoryHeader(int index) {
    return index >= 0 && (index % MEM_ITEMS_PER_SEGMENT) == 0;
}

static int memorySegmentAt(int index) {
    return index / MEM_ITEMS_PER_SEGMENT;
}

static int memoryRowAt(int index) {
    int pos = index % MEM_ITEMS_PER_SEGMENT;
    return pos == 0 ? -1 : memorySegmentAt(index) * MEM_ROWS_PER_SEGMENT + (pos - 1);
}

static void drawMemoryHeader(DRAWITEMSTRUCT &di, int segment) {
    uint8_t cs = g_cpu.getCS();
    uint8_t ds = g_cpu.getDS();
    bool isCS = (segment == cs);
    bool isDS = (segment == ds);

    COLORREF bg = isCS ? RGB(24, 46, 70)
               : isDS ? RGB(22, 52, 45)
                      : RGB(18, 23, 33);
    FillRectColor(di.hDC, di.rcItem, bg);

    RECT line{di.rcItem.left, di.rcItem.top, di.rcItem.right, di.rcItem.top + 1};
    FillRectColor(di.hDC, line, isCS ? C_ACCENT : isDS ? C_GREEN : C_BORDER);

    wchar_t label[96];
    swprintf(label, 96, L"SEGMENT %X  •  %04X-%04X",
             static_cast<unsigned>(segment),
             static_cast<unsigned>(segment * MEM_SEGMENT_SIZE),
             static_cast<unsigned>(segment * MEM_SEGMENT_SIZE + MEM_SEGMENT_SIZE - 1));

    RECT rl{di.rcItem.left + 8, di.rcItem.top, di.rcItem.right - 78, di.rcItem.bottom};
    DrawTextAt(di.hDC, rl, label, g_fontMono,
               isCS ? C_ACCENT : isDS ? C_GREEN : C_MUTED,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    wchar_t tag[32] = {0};
    COLORREF tagColor = C_MUTED;
    if (isCS && isDS)      { swprintf(tag, 32, L"CS / DS"); tagColor = C_TEXT; }
    else if (isCS)         { swprintf(tag, 32, L"CS");      tagColor = C_ACCENT; }
    else if (isDS)         { swprintf(tag, 32, L"DS");      tagColor = C_GREEN; }

    if (tag[0]) {
        RECT rt{di.rcItem.right - 72, di.rcItem.top, di.rcItem.right - 8, di.rcItem.bottom};
        DrawTextAt(di.hDC, rt, tag, g_fontMono, tagColor,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void drawMemoryItem(DRAWITEMSTRUCT &di) {
    int idx = static_cast<int>(di.itemID);
    if (idx < 0 || idx >= MEM_TOTAL_ITEMS) return;

    if (isMemoryHeader(idx)) {
        drawMemoryHeader(di, memorySegmentAt(idx));
        return;
    }

    int segment = memorySegmentAt(idx);
    int row = memoryRowAt(idx);
    uint16_t base = static_cast<uint16_t>(row * 16);
    uint16_t pcPhys = g_cpu.currentPhysicalPC();
    uint16_t sp = g_cpu.getSP();
    uint8_t cs = g_cpu.getCS();
    uint8_t ds = g_cpu.getDS();

    const auto &li = g_cpu.lastInstruction();

    // Bajt 2 rozkazu, który zostanie wykonany jako następny.
    uint16_t opAddr = 0xFFFF;
    if (pcPhys < 4095 && instrLength(g_cpu.readMem(pcPhys)) == 2)
        opAddr = static_cast<uint16_t>(pcPhys + 1);

    auto activeKindAt = [&](uint16_t addr) -> uint8_t {
        for (int k = 0; k < 2; ++k) {
            if (li.accessKind[k] != 0 && li.access[k] == addr)
                return li.accessKind[k];
        }
        return 0;
    };

    bool interesting = false;
    for (int i = 0; i < 16; ++i) {
        uint16_t a = static_cast<uint16_t>(base + i);
        if (a == pcPhys || a == sp || a == opAddr || activeKindAt(a) != 0) {
            interesting = true;
            break;
        }
    }

    COLORREF bg;
    if (segment == cs)       bg = (row % 2) ? RGB(22, 40, 60) : RGB(24, 44, 66);
    else if (segment == ds)  bg = (row % 2) ? RGB(20, 44, 38) : RGB(22, 48, 42);
    else                     bg = interesting ? RGB(25, 42, 62)
                                             : ((row % 2) ? C_CARD2 : RGB(26, 32, 46));
    FillRectColor(di.hDC, di.rcItem, bg);

    if (segment == cs || segment == ds) {
        RECT bar{di.rcItem.left, di.rcItem.top, di.rcItem.left + 3, di.rcItem.bottom};
        FillRectColor(di.hDC, bar, (segment == cs) ? C_ACCENT : C_GREEN);
    }

    RECT ra{di.rcItem.left + 8, di.rcItem.top, di.rcItem.left + 52, di.rcItem.bottom};
    DrawTextAt(di.hDC, ra, hex4(base).c_str(), g_fontMono, C_MUTED,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int byteW = 21;
    int startX = di.rcItem.left + 56;
    for (int i = 0; i < 16; ++i) {
        uint16_t addr = static_cast<uint16_t>(base + i);
        uint8_t value = g_cpu.readMem(addr);
        RECT cell{startX + i * byteW, di.rcItem.top, startX + (i + 1) * byteW, di.rcItem.bottom};

        bool isPC = (addr == pcPhys);
        bool isSP = (addr == sp);
        bool isOp = (addr == opAddr);
        uint8_t act = activeKindAt(addr);

        if (isPC || isSP || isOp || act != 0) {
            COLORREF fill = isPC ? C_ACCENT
                          : isSP ? C_GREEN
                          : isOp ? RGB(34, 66, 96)
                          : (act == 2 || act == 4) ? C_PURPLE
                                                   : C_AMBER;
            FillRectColor(di.hDC, cell, fill);
        }

        COLORREF color = C_MUTED;
        if (isPC || isSP || act != 0) color = RGB(10, 16, 24);
        else if (isOp) color = RGB(190, 220, 255);
        else if (value != 0) color = C_TEXT;

        DrawTextAt(di.hDC, cell, hex2(value).c_str(), g_fontMono, color,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (di.itemState & ODS_FOCUS) DrawFocusRect(di.hDC, &di.rcItem);
}

static void drawLogItem(DRAWITEMSTRUCT &di) {
    int idx = static_cast<int>(di.itemID);
    if (idx < 0 || idx >= static_cast<int>(g_log.size())) return;

    FillRectColor(di.hDC, di.rcItem, (di.itemState & ODS_SELECTED) ? RGB(38, 47, 66) : C_CARD2);

    COLORREF color = C_TEXT;
    const LogEntry &entry = g_log[idx];
    if (entry.kind == 1) color = C_AMBER;
    else if (entry.kind == 2) color = C_PURPLE;
    else if (entry.kind == 3) color = C_RED;
    else if (entry.kind == 5) color = C_MUTED;

    RECT r{di.rcItem.left + 8, di.rcItem.top, di.rcItem.right - 6, di.rcItem.bottom};
    DrawTextAt(di.hDC, r, entry.text.c_str(), g_fontMono, color,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Obsługa akcji
// ---------------------------------------------------------------------------

static void addLog(const std::wstring &text, uint8_t kind) {
    g_log.push_back({text, kind});
    if (g_log.size() > 400) {
        g_log.erase(g_log.begin());
        // Indeksy w LB_SETITEMDATA trzeba zsynchronizować z wektorem.
        SendMessageW(g_hListLog, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < g_log.size(); ++i) {
            int p = static_cast<int>(SendMessageW(g_hListLog, LB_ADDSTRING, 0, (LPARAM)L""));
            SendMessageW(g_hListLog, LB_SETITEMDATA, p, i);
        }
    }
    int index = static_cast<int>(g_log.size()) - 1;
    int pos = static_cast<int>(SendMessageW(g_hListLog, LB_ADDSTRING, 0, (LPARAM)L""));
    SendMessageW(g_hListLog, LB_SETITEMDATA, pos, index);
    if (pos >= 0) SendMessageW(g_hListLog, LB_SETTOPINDEX, pos, 0);
}

static void clearLog() {
    g_log.clear();
    SendMessageW(g_hListLog, LB_RESETCONTENT, 0, 0);
}

static void refreshListState() {
    // Podświetlenie aktualnego rozkazu w dekodowaniu
    uint16_t pcPhys = g_cpu.currentPhysicalPC();
    int selected = LB_ERR;
    for (size_t i = 0; i < g_disasm.size(); ++i) {
        if (g_disasm[i].address == pcPhys) { selected = static_cast<int>(i); break; }
    }
    if (selected != LB_ERR) {
        // LB_SETCURSEL z tą samą wartością też wymusza przerysowanie listy.
        int cur = static_cast<int>(SendMessageW(g_hListProgram, LB_GETCURSEL, 0, 0));
        if (cur != selected) SendMessageW(g_hListProgram, LB_SETCURSEL, selected, 0);
        int top = static_cast<int>(SendMessageW(g_hListProgram, LB_GETTOPINDEX, 0, 0));
        if (selected < top || selected > top + 18)
            SendMessageW(g_hListProgram, LB_SETTOPINDEX, selected, 0);
    } else {
        SendMessageW(g_hListProgram, LB_SETCURSEL, -1, 0);
    }

    // Przewinięcie widoku pamięci do wiersza zawierającego PC.
    // Lista ma naprzemiennie nagłówek segmentu i 16 wierszy danych,
    // więc indeks listy to: numer segmentu * 17 + 1 + wiersz w segmencie.
    int memRow = pcPhys / 16;                       // 0..255 (wiersz danych)
    int memIndex = (pcPhys / MEM_SEGMENT_SIZE) * MEM_ITEMS_PER_SEGMENT
                 + 1 + (memRow % MEM_ROWS_PER_SEGMENT);
    if (memIndex >= 0 && memIndex < MEM_TOTAL_ITEMS) {
        int cur = static_cast<int>(SendMessageW(g_hListMemory, LB_GETCURSEL, 0, 0));
        if (cur != memIndex) SendMessageW(g_hListMemory, LB_SETCURSEL, memIndex, 0);
        int top = static_cast<int>(SendMessageW(g_hListMemory, LB_GETTOPINDEX, 0, 0));
        if (memIndex < top || memIndex > top + 30)
            SendMessageW(g_hListMemory, LB_SETTOPINDEX, memIndex, 0);
    }
}

static void doStep() {
    if (g_cpu.isHalted()) return;

    uint8_t beforeR[4], beforeSeg[4];
    for (int i = 0; i < 4; ++i) beforeR[i] = g_cpu.reg(i);
    beforeSeg[0] = g_cpu.getCS();
    beforeSeg[1] = g_cpu.getDS();
    beforeSeg[2] = static_cast<uint8_t>(g_cpu.getPC());
    beforeSeg[3] = static_cast<uint8_t>(g_cpu.getSP());

    g_cpu.stepOnce();

    DWORD64 now = GetTickCount64();
    for (int i = 0; i < 4; ++i)
        if (beforeR[i] != g_cpu.reg(i)) g_regFlash[i] = now + 450;

    uint8_t afterSeg[4] = {g_cpu.getCS(), g_cpu.getDS(),
                           static_cast<uint8_t>(g_cpu.getPC()),
                           static_cast<uint8_t>(g_cpu.getSP())};
    for (int i = 0; i < 4; ++i)
        if (beforeSeg[i] != afterSeg[i]) g_segFlash[i] = now + 450;

    const auto &li = g_cpu.lastInstruction();
    std::wstring text = formatInstruction(li.opcode, li.args, li.operand);

    wchar_t bytes[16];
    uint8_t opByte = static_cast<uint8_t>((li.opcode << 4) | li.args);
    if (li.length == 2)
        swprintf(bytes, 16, L"%02X %02X", opByte, li.operand);
    else
        swprintf(bytes, 16, L"%02X --", opByte);

    wchar_t entry[256];
    swprintf(entry, 256, L"[%04X] %ls  %-20ls | cykl %llu",
             li.address, bytes, text.c_str(), (unsigned long long)g_cpu.cycles());
    uint8_t kind = opcodeKind(li.opcode);
    addLog(entry, kind == 4 ? 5 : kind);

    refreshListState();
    refreshAfterStep();

    if (g_cpu.isHalted()) {
        addLog(L"HALT — program zakończony", 3);
        stopRunning();
    }
}

static void stopRunning() {
    if (!g_running) return;
    g_running = false;
    KillTimer(g_hwnd, ID_TIMER_CPU);
    SetWindowTextW(g_hBtnRun, L"Start (F5)");
    refreshViews();
}

static void startRunning() {
    if (g_running) return;
    if (g_cpu.isHalted()) {
        g_cpu.reset();
        clearLog();
        addLog(L"Automaticzny reset po HALT — start od początku", 5);
        rebuildDisassembly();
    }
    g_running = true;
    SetWindowTextW(g_hBtnRun, L"Pauza (F5)");
    SetTimer(g_hwnd, ID_TIMER_CPU, cpuTimerInterval(), nullptr);
    refreshViews();
}

static void toggleRun() {
    if (g_running) stopRunning();
    else startRunning();
}

static std::string wideToNarrow(const std::wstring &value) {
    int length = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, result.data(), length, nullptr, nullptr);
    return result;
}

static std::wstring fileNameOnly(const std::wstring &path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

static bool onLoadFileNarrow(const std::string &path, bool silent = false) {
    stopRunning();
    if (!g_cpu.loadProgramFromFile(path)) {
        if (!silent)
            MessageBoxW(g_hwnd, L"Nie udało się wczytać pliku programu.",
                        L"Błąd wczytywania", MB_OK | MB_ICONERROR);
        return false;
    }

    g_loadedFile = fileNameOnly(std::wstring(path.begin(), path.end()));
    clearLog();
    addLog(L"Wczytano program: " + g_loadedFile, 5);
    rebuildDisassembly();
    refreshProgramList();
    refreshListState();
    refreshViews();
    return true;
}

static void onLoadFile(const std::wstring &path) {
    onLoadFileNarrow(wideToNarrow(path));
}

static void onLoadDialog() {
    wchar_t file[MAX_PATH] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Pliki programów (*.txt)\0*.txt\0Wszystkie pliki (*.*)\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) onLoadFile(file);
}

static void loadInitialProgram() {
    if (!onLoadFileNarrow(programFileName, true)) {
        addLog(L"Nie znaleziono pliku programu. Użyj przycisku [Wczytaj].", 3);
    }
}

static void refreshProgramList() {
    SendMessageW(g_hListProgram, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_disasm.size(); ++i) {
        int pos = static_cast<int>(SendMessageW(g_hListProgram, LB_ADDSTRING, 0, (LPARAM)L""));
        SendMessageW(g_hListProgram, LB_SETITEMDATA, pos, i);
    }
}

// ---------------------------------------------------------------------------
// Procedura okna
// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;

            INITCOMMONCONTROLSEX icc{};
            icc.dwSize = sizeof(icc);
            icc.dwICC = ICC_BAR_CLASSES;
            InitCommonControlsEx(&icc);

            g_fontUI    = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_fontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_fontTitle = CreateFontW(-23, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_fontMono  = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
            g_fontBig   = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");

            const int btnW = 159, btnH = 40;
            int bx1 = LEFT_X + 8;
            int bx2 = bx1 + btnW + 10;

            g_hBtnLoad  = CreateWindowExW(0, L"BUTTON", L"Wczytaj program",
                                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          bx1, CTL_Y + 44, btnW, btnH, hwnd,
                                          (HMENU)ID_BTN_LOAD, nullptr, nullptr);
            g_hBtnReset = CreateWindowExW(0, L"BUTTON", L"Reset",
                                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          bx2, CTL_Y + 44, btnW, btnH, hwnd,
                                          (HMENU)ID_BTN_RESET, nullptr, nullptr);
            g_hBtnStep  = CreateWindowExW(0, L"BUTTON", L"Krok (F10)",
                                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          bx1, CTL_Y + 92, btnW, btnH, hwnd,
                                          (HMENU)ID_BTN_STEP, nullptr, nullptr);
            g_hBtnRun   = CreateWindowExW(0, L"BUTTON", L"Start (F5)",
                                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          bx2, CTL_Y + 92, btnW, btnH, hwnd,
                                          (HMENU)ID_BTN_RUN, nullptr, nullptr);

            g_hSlider = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                                        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                                        LEFT_X + 8, CTL_Y + 176, LEFT_W - 16, 34, hwnd,
                                        (HMENU)ID_SLIDER_DELAY, nullptr, nullptr);
            SendMessageW(g_hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10000));
            SendMessageW(g_hSlider, TBM_SETLINESIZE, 0, 100);   // strzałki: 100 ms
            SendMessageW(g_hSlider, TBM_SETPAGESIZE, 0, 1000);  // klik/PageUp: 1 s
            SendMessageW(g_hSlider, TBM_SETTICFREQ, 1000, 0);   // znaczniki co 1 s
            SendMessageW(g_hSlider, TBM_SETPOS, TRUE, g_delayMs);

            DWORD ls = WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED |
                       LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;
            // Pamięć ma zmienną wysokość: nagłówki segmentów są wyższe niż wiersze.
            DWORD ms = WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWVARIABLE |
                       LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;

            g_hListProgram = CreateWindowExW(0, WC_LISTBOXW, nullptr, ls,
                                             MID_X + 8, PRG_Y + 40, MID_W - 16, PRG_H - 48,
                                             hwnd, (HMENU)ID_LIST_PROGRAM, nullptr, nullptr);
            g_hListLog = CreateWindowExW(0, WC_LISTBOXW, nullptr, ls,
                                         MID_X + 8, LOG_Y + 40, MID_W - 16, LOG_H - 48,
                                         hwnd, (HMENU)ID_LIST_LOG, nullptr, nullptr);
            g_hListMemory = CreateWindowExW(0, WC_LISTBOXW, nullptr, ms,
                                            RIGHT_X + 8, MEM_Y + 40, RIGHT_W - 16, MEM_H - 48,
                                            hwnd, (HMENU)ID_LIST_MEMORY, nullptr, nullptr);

            for (int i = 0; i < MEM_TOTAL_ITEMS; ++i)
                SendMessageW(g_hListMemory, LB_ADDSTRING, 0, (LPARAM)L"");

            SetTimer(hwnd, ID_TIMER_UI, 100, nullptr);
            loadInitialProgram();
            return 0;
        }

        case WM_MEASUREITEM: {
            auto *mi = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
            if (mi->CtlID == ID_LIST_MEMORY)
                mi->itemHeight = isMemoryHeader(static_cast<int>(mi->itemID)) ? 22 : 18;
            else if (mi->CtlID == ID_LIST_PROGRAM || mi->CtlID == ID_LIST_LOG) mi->itemHeight = 21;
            return TRUE;
        }

        case WM_DRAWITEM: {
            auto *di = reinterpret_cast<DRAWITEMSTRUCT *>(lParam);
            if (di->CtlType == ODT_LISTBOX) {
                switch (di->CtlID) {
                    case ID_LIST_PROGRAM: drawProgramItem(*di); return TRUE;
                    case ID_LIST_MEMORY:  drawMemoryItem(*di);  return TRUE;
                    case ID_LIST_LOG:     drawLogItem(*di);     return TRUE;
                    default: break;
                }
            } else if (di->CtlType == ODT_BUTTON) {
                bool pressed = (di->itemState & ODS_SELECTED) != 0;
                COLORREF bg = pressed ? RGB(35, 82, 126) : C_CARD2;
                COLORREF border = pressed ? C_ACCENT : C_BORDER;
                FillRound(di->hDC, di->rcItem, 8, bg, border);
                wchar_t text[128] = {0};
                GetWindowTextW(di->hwndItem, text, 128);
                DrawTextAt(di->hDC, di->rcItem, text, g_fontUI,
                           pressed ? C_TEXT : C_ACCENT,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (di->itemState & ODS_FOCUS) DrawFocusRect(di->hDC, &di->rcItem);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);
            switch (id) {
                case ID_BTN_LOAD:  if (notification == BN_CLICKED) onLoadDialog(); break;
                case ID_BTN_STEP:  if (notification == BN_CLICKED) { stopRunning(); doStep(); } break;
                case ID_BTN_RUN:   if (notification == BN_CLICKED) toggleRun(); break;
                case ID_BTN_RESET:
                    if (notification == BN_CLICKED) {
                        stopRunning();
                        g_cpu.reset();
                        clearLog();
                        addLog(L"Reset stanu procesora", 5);
                        refreshListState();
                        refreshViews();
                    }
                    break;
                case ID_LIST_PROGRAM:
                    if (notification == LBN_DBLCLK) {
                        int sel = static_cast<int>(SendMessageW(g_hListProgram, LB_GETCURSEL, 0, 0));
                        if (sel >= 0 && sel < static_cast<int>(g_disasm.size())) {
                            stopRunning();
                            g_cpu.setPC(g_disasm[sel].address);
                            wchar_t entry[128];
                            swprintf(entry, 128, L"Ustawiono PC = 0x%04X (podwójny klik)",
                                     g_disasm[sel].address);
                            addLog(entry, 5);
                            refreshListState();
                            refreshViews();
                        }
                    }
                    break;
                default: break;
            }
            return 0;
        }

        case WM_HSCROLL: {
            if (reinterpret_cast<HWND>(lParam) == g_hSlider) {
                int pos = static_cast<int>(SendMessageW(g_hSlider, TBM_GETPOS, 0, 0));
                g_delayMs = std::clamp(((pos + 50) / 100) * 100, 0, 10000);
                SendMessageW(g_hSlider, TBM_SETPOS, TRUE, g_delayMs);
                if (g_running) {
                    KillTimer(hwnd, ID_TIMER_CPU);
                    SetTimer(hwnd, ID_TIMER_CPU, cpuTimerInterval(), nullptr);
                }
                // Zmienia się tylko tekst opóźnienia w sterowaniu i na pasku statusu.
                if (g_hwnd) {
                    RECT ctl{LEFT_X, CTL_Y, LEFT_X + LEFT_W, CTL_Y + CTL_H};
                    RECT status{0, CLIENT_H - 36, CLIENT_W, CLIENT_H};
                    InvalidateRect(g_hwnd, &ctl, FALSE);
                    InvalidateRect(g_hwnd, &status, FALSE);
                }
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == ID_TIMER_CPU) {
                if (g_running && !g_cpu.isHalted()) {
                    doStep();
                } else if (g_cpu.isHalted()) {
                    stopRunning();
                }
            } else if (wParam == ID_TIMER_UI) {
                bool flash = false;
                DWORD64 now = GetTickCount64();
                for (int i = 0; i < 4; ++i)
                    if (g_regFlash[i] > now || g_segFlash[i] > now) flash = true;
                if (flash) refreshFlashAreas();
            }
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_F5) { toggleRun(); return 0; }
            if (wParam == VK_F10) { stopRunning(); doStep(); return 0; }
            if (wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) { onLoadDialog(); return 0; }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            FillRectColor(hdc, ps.rcPaint, C_BG);

            drawHeader(hdc);
            drawRegisters(hdc);
            drawSegments(hdc);
            drawFlags(hdc);
            drawControls(hdc);
            drawStatusBar(hdc);

            DrawCard(hdc, MID_X, PRG_Y, MID_W, PRG_H, L"PROGRAM (DEKODOWANIE)  •  2× klik = PC");
            DrawCard(hdc, MID_X, LOG_Y, MID_W, LOG_H, L"DZIENNIK WYKONANIA", C_GREEN);
            DrawCard(hdc, RIGHT_X, MEM_Y, RIGHT_W, MEM_H, L"PAMIĘĆ — 16 SEGMENTÓW × 256 B", C_PURPLE);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, ID_TIMER_CPU);
            KillTimer(hwnd, ID_TIMER_UI);
            DeleteObject(g_fontUI);
            DeleteObject(g_fontSmall);
            DeleteObject(g_fontTitle);
            DeleteObject(g_fontMono);
            DeleteObject(g_fontBig);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    using SetDpiAwareFn = BOOL(WINAPI *)();
    auto setDpiAware = reinterpret_cast<SetDpiAwareFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware"));
    if (setDpiAware) setDpiAware();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"ASK8MonitorClass";
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT rc{0, 0, CLIENT_W, CLIENT_H};
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    // WS_EX_COMPOSITED = podwójne buforowanie całego okna RAZEM z kontrolkami.
    // Bez tego rodzić i lista malują się naprzemiennie, co widać jako migotanie.
    HWND hwnd = CreateWindowExW(WS_EX_COMPOSITED, L"ASK8MonitorClass", L"Monitor Mikroprocesora ASK-8",
                                style, x, y, w, h, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Nie udało się utworzyć okna.", L"Błąd", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
