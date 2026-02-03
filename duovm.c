#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>

#define MEM_SIZE 65536
#define SRAM_START (224 * 256)

#define SCREEN_W 36
#define SCREEN_H 24

uint8_t memory[MEM_SIZE];

/* CPU registers */
uint16_t PC = 0;
uint16_t A = 0;
uint16_t T = 0;
uint8_t  D0 = 0;
uint8_t  D1 = 0;
bool     C = false;

/* VM state */
bool running = true;
bool waiting_key = false;

/* Cursor */
uint8_t cur_x = 0;
uint8_t cur_y = 0;

/* ================= Memory ================= */

static void check_addr(uint16_t addr) {
    if (addr >= MEM_SIZE) {
        endwin();
        fprintf(stderr, "Memory OOB: %04X\n", addr);
        exit(1);
    }
}

static uint8_t mem_read(uint16_t addr) {
    check_addr(addr);
    return memory[addr];
}

static void mem_write(uint16_t addr, uint8_t v) {
    check_addr(addr);
    if (addr < SRAM_START) {
        endwin();
        fprintf(stderr, "Write to ROM: %04X\n", addr);
        exit(1);
    }
    memory[addr] = v;
}

static uint16_t mem_read16(uint16_t addr) {
    return mem_read(addr) | (mem_read(addr + 1) << 8);
}

/* ================= Display ================= */

static void clear_screen_vm(void) {
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            mvaddch(y, x, ' ');
    refresh();
}

static void put_char_vm(uint8_t ch) {
    mvaddch(cur_y, cur_x, ch ? ch : ' ');
    cur_x++;
    if (cur_x >= SCREEN_W) {
        cur_x = 0;
        cur_y++;
        if (cur_y >= SCREEN_H)
            cur_y = 0;
    }
    refresh();
}

/* ================= Input ================= */

static int read_button(void) {
    int c;
    while (1) {
        c = getch();
        switch (c) {
            case KEY_LEFT:  return 0;
            case KEY_UP:    return 1;
            case KEY_DOWN:  return 2;
            case KEY_RIGHT: return 3;
            case 'a': return 0;
            case 'w': return 1;
            case 's': return 2;
            case 'd': return 3;
            case '\n': return 3;
        }
    }
}

/* ================= CPU ================= */

static void write_alu_dest(uint8_t dest, uint8_t val) {
    if (dest == 0)
        mem_write(A, val);
    else
        D0 = val;
}

static void step(void) {
    uint8_t opcode = mem_read(PC++);

    /* COP */
    if (opcode == 0x00) { A = mem_read16(PC); PC += 2; }
    if (opcode == 0x01) { D0 = mem_read(PC++); }
    if (opcode == 0x02) { D1 = mem_read(PC++); }
    if (opcode == 0x03) { D0 = mem_read(A); }
    if (opcode == 0x04) { D1 = mem_read(A); }
    if (opcode == 0x05) { T = (T & 0xFF00) | mem_read(A); }
    if (opcode == 0x06) { T = (T & 0x00FF) | (mem_read(A) << 8); }
    if (opcode == 0x07) { A = T; }
    if (opcode == 0x08) { PC = T; }

    /* Jumps */
    if (opcode == 0x20) PC = mem_read16(PC);
    if (opcode == 0x21) {
        uint16_t t = mem_read16(PC);
        if (C) PC = t;
        else PC += 2;
    }
    if (opcode == 0x22) {
        uint16_t t = mem_read16(PC);
        if (!C) PC = t;
        else PC += 2;
    }

    /* Flags */
    if (opcode == 0x40) C = false;
    if (opcode == 0x41) C = true;

    uint8_t alu = opcode & 0xFE;
    uint8_t dst = opcode & 1;

    if (alu == 0x60) write_alu_dest(dst, D0);

    if (alu == 0x62) {
        uint16_t r = D0 + D1 + (C ? 1 : 0);
        C = r > 0xFF;
        write_alu_dest(dst, r & 0xFF);
    }

    if (alu == 0x64) {
        int r = D0 - D1 - (C ? 1 : 0);
        C = r < 0;
        write_alu_dest(dst, r & 0xFF);
    }

    if (alu == 0x66) write_alu_dest(dst, D0 & D1);
    if (alu == 0x68) write_alu_dest(dst, D0 | D1);
    if (alu == 0x6A) write_alu_dest(dst, D0 ^ D1);
    if (alu == 0x6C) write_alu_dest(dst, (~D0) & 0xFF);

    if (alu == 0x6E) {
        uint16_t r = (D0 << 1) | (C ? 1 : 0);
        C = r > 0xFF;
        write_alu_dest(dst, r & 0xFF);
    }

    if (alu == 0x70) {
        bool nextC = D0 & 1;
        uint8_t r = (D0 >> 1) | (C ? 0x80 : 0);
        C = nextC;
        write_alu_dest(dst, r);
    }

    /* I/O */
    if (opcode == 0xA0) {
        waiting_key = true;
        int b = read_button();
        mem_write(A, b);
        waiting_key = false;
    }

    if (opcode == 0xA1) put_char_vm(mem_read(A));
    if (opcode == 0xA2) cur_x = mem_read(A);
    if (opcode == 0xA3) cur_y = mem_read(A);
    if (opcode == 0xA4) clear_screen_vm();
}

/* ================= Loader ================= */

static void load_hex(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("open");
        exit(1);
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (!isxdigit(line[0])) continue;

        uint16_t addr;
        sscanf(line, "%hx:", &addr);

        char *p = strchr(line, ':') + 1;
        while (*p) {
            while (*p == ' ') p++;
            if (!isxdigit(p[0])) break;

            unsigned v;
            sscanf(p, "%02x", &v);
            memory[addr++] = v;
            p += 2;
        }
    }
    fclose(f);
}

/* ================= Main ================= */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s program.hex\n", argv[0]);
        return 1;
    }

    load_hex(argv[1]);

    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    nodelay(stdscr, FALSE);
    curs_set(0);

    clear_screen_vm();

    while (running) {
        for (int i = 0; i < 20000 && !waiting_key; i++)
            step();
    }

    endwin();
    return 0;
}
