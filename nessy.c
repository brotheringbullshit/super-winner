#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define NES_RAM_SIZE 0x800
#define PRG_ROM_BANK_SIZE 0x4000
#define CHR_ROM_BANK_SIZE 0x2000
#define PPU_VRAM_SIZE 0x800
#define PPU_PALETTE_SIZE 0x20
#define OAM_SIZE 0x100
#define FRAME_WIDTH 256
#define FRAME_HEIGHT 240

enum {
    FLAG_CARRY = 1 << 0,
    FLAG_ZERO = 1 << 1,
    FLAG_INTERRUPT = 1 << 2,
    FLAG_DECIMAL = 1 << 3,
    FLAG_BREAK = 1 << 4,
    FLAG_UNUSED = 1 << 5,
    FLAG_OVERFLOW = 1 << 6,
    FLAG_NEGATIVE = 1 << 7
};

typedef struct {
    uint8_t prg_rom[PRG_ROM_BANK_SIZE * 2];
    uint8_t chr_rom[CHR_ROM_BANK_SIZE];
    size_t prg_rom_size;
    size_t chr_rom_size;
} Cartridge;

typedef struct {
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    uint8_t oam_addr;
    uint8_t scroll_latch;
    uint8_t addr_latch;
    uint16_t vram_addr;
    uint16_t temp_addr;
    uint8_t fine_x;
    uint8_t read_buffer;
    uint8_t vram[PPU_VRAM_SIZE];
    uint8_t palette[PPU_PALETTE_SIZE];
    uint8_t oam[OAM_SIZE];
    uint8_t framebuffer[FRAME_WIDTH * FRAME_HEIGHT];
    int scanline;
    int cycle;
    bool nmi_triggered;
} PPU;

typedef struct {
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint16_t pc;
    uint64_t cycles;
} CPU;

typedef struct {
    CPU cpu;
    PPU ppu;
    Cartridge cart;
    uint8_t ram[NES_RAM_SIZE];
} NES;

typedef struct {
    const uint8_t *rom_data;
    size_t rom_size;
} RomImage;

static void ppu_reset(PPU *ppu) {
    memset(ppu, 0, sizeof(*ppu));
    ppu->status = 0xA0;
}

static void cpu_reset(CPU *cpu, uint16_t reset_vector) {
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0xFD;
    cpu->p = FLAG_INTERRUPT | FLAG_UNUSED;
    cpu->pc = reset_vector;
    cpu->cycles = 7;
}

static uint8_t cpu_read(NES *nes, uint16_t addr);
static void cpu_write(NES *nes, uint16_t addr, uint8_t value);

static uint16_t ppu_mirror_vram_addr(uint16_t addr) {
    addr &= 0x2FFF;
    if (addr >= 0x3000 && addr <= 0x3EFF) {
        addr -= 0x1000;
    }
    return addr;
}

static uint8_t ppu_read(NES *nes, uint16_t addr) {
    PPU *ppu = &nes->ppu;
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        if (nes->cart.chr_rom_size == 0) {
            return 0;
        }
        return nes->cart.chr_rom[addr % nes->cart.chr_rom_size];
    }
    if (addr < 0x3F00) {
        addr = ppu_mirror_vram_addr(addr);
        return ppu->vram[addr & 0x7FF];
    }
    addr = 0x3F00 | (addr & 0x1F);
    return ppu->palette[addr & 0x1F];
}

static void ppu_write(NES *nes, uint16_t addr, uint8_t value) {
    PPU *ppu = &nes->ppu;
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        if (nes->cart.chr_rom_size == 0) {
            return;
        }
        nes->cart.chr_rom[addr % nes->cart.chr_rom_size] = value;
        return;
    }
    if (addr < 0x3F00) {
        addr = ppu_mirror_vram_addr(addr);
        ppu->vram[addr & 0x7FF] = value;
        return;
    }
    addr = 0x3F00 | (addr & 0x1F);
    ppu->palette[addr & 0x1F] = value;
}

static uint8_t ppu_read_register(NES *nes, uint16_t addr) {
    PPU *ppu = &nes->ppu;
    switch (addr & 0x7) {
        case 2: {
            uint8_t value = ppu->status;
            ppu->status &= ~0x80;
            ppu->addr_latch = 0;
            return value;
        }
        case 4:
            return ppu->oam[ppu->oam_addr];
        case 7: {
            uint16_t vram_addr = ppu->vram_addr;
            uint8_t value = ppu_read(nes, vram_addr);
            if (vram_addr < 0x3F00) {
                uint8_t buffered = ppu->read_buffer;
                ppu->read_buffer = value;
                value = buffered;
            } else {
                ppu->read_buffer = ppu_read(nes, vram_addr - 0x1000);
            }
            ppu->vram_addr += (ppu->ctrl & 0x04) ? 32 : 1;
            return value;
        }
        default:
            return 0;
    }
}

static void ppu_write_register(NES *nes, uint16_t addr, uint8_t value) {
    PPU *ppu = &nes->ppu;
    switch (addr & 0x7) {
        case 0:
            ppu->ctrl = value;
            ppu->temp_addr = (ppu->temp_addr & 0xF3FF) | ((value & 0x03) << 10);
            break;
        case 1:
            ppu->mask = value;
            break;
        case 3:
            ppu->oam_addr = value;
            break;
        case 4:
            ppu->oam[ppu->oam_addr++] = value;
            break;
        case 5:
            if (!ppu->scroll_latch) {
                ppu->fine_x = value & 0x7;
                ppu->temp_addr = (ppu->temp_addr & 0xFFE0) | (value >> 3);
                ppu->scroll_latch = 1;
            } else {
                ppu->temp_addr = (ppu->temp_addr & 0x8FFF) | ((value & 0x07) << 12);
                ppu->temp_addr = (ppu->temp_addr & 0xFC1F) | ((value & 0xF8) << 2);
                ppu->scroll_latch = 0;
            }
            break;
        case 6:
            if (!ppu->addr_latch) {
                ppu->temp_addr = (ppu->temp_addr & 0x00FF) | ((value & 0x3F) << 8);
                ppu->addr_latch = 1;
            } else {
                ppu->temp_addr = (ppu->temp_addr & 0xFF00) | value;
                ppu->vram_addr = ppu->temp_addr;
                ppu->addr_latch = 0;
            }
            break;
        case 7:
            ppu_write(nes, ppu->vram_addr, value);
            ppu->vram_addr += (ppu->ctrl & 0x04) ? 32 : 1;
            break;
        default:
            break;
    }
}

static void ppu_step(PPU *ppu) {
    ppu->cycle++;
    if (ppu->cycle >= 341) {
        ppu->cycle = 0;
        ppu->scanline++;
        if (ppu->scanline == 241) {
            ppu->status |= 0x80;
            if (ppu->ctrl & 0x80) {
                ppu->nmi_triggered = true;
            }
        }
        if (ppu->scanline >= 262) {
            ppu->scanline = 0;
            ppu->status &= ~0x80;
            ppu->nmi_triggered = false;
        }
    }
}

static void cpu_push(NES *nes, uint8_t value) {
    nes->ram[0x100 + nes->cpu.sp--] = value;
}

static uint8_t cpu_pop(NES *nes) {
    return nes->ram[0x100 + ++nes->cpu.sp];
}

static void cpu_set_zn(CPU *cpu, uint8_t value) {
    if (value == 0) {
        cpu->p |= FLAG_ZERO;
    } else {
        cpu->p &= ~FLAG_ZERO;
    }
    if (value & 0x80) {
        cpu->p |= FLAG_NEGATIVE;
    } else {
        cpu->p &= ~FLAG_NEGATIVE;
    }
}

static uint8_t cpu_read(NES *nes, uint16_t addr) {
    if (addr < 0x2000) {
        return nes->ram[addr & 0x7FF];
    }
    if (addr < 0x4000) {
        return ppu_read_register(nes, addr);
    }
    if (addr >= 0x8000) {
        size_t prg_size = nes->cart.prg_rom_size;
        if (prg_size == PRG_ROM_BANK_SIZE) {
            return nes->cart.prg_rom[(addr - 0x8000) % PRG_ROM_BANK_SIZE];
        }
        return nes->cart.prg_rom[(addr - 0x8000) % prg_size];
    }
    return 0;
}

static void cpu_write(NES *nes, uint16_t addr, uint8_t value) {
    if (addr < 0x2000) {
        nes->ram[addr & 0x7FF] = value;
        return;
    }
    if (addr < 0x4000) {
        ppu_write_register(nes, addr, value);
        return;
    }
}

static void cpu_nmi(NES *nes) {
    CPU *cpu = &nes->cpu;
    cpu_push(nes, (uint8_t)(cpu->pc >> 8));
    cpu_push(nes, (uint8_t)(cpu->pc & 0xFF));
    cpu_push(nes, cpu->p & ~FLAG_BREAK);
    cpu->p |= FLAG_INTERRUPT;
    uint16_t lo = cpu_read(nes, 0xFFFA);
    uint16_t hi = cpu_read(nes, 0xFFFB);
    cpu->pc = (hi << 8) | lo;
}

typedef struct {
    uint16_t addr;
    uint8_t value;
    bool page_crossed;
} AddrResult;

static AddrResult addr_immediate(CPU *cpu) {
    AddrResult result = { cpu->pc++, 0, false };
    return result;
}

static AddrResult addr_zero_page(NES *nes) {
    uint8_t addr = cpu_read(nes, nes->cpu.pc++);
    AddrResult result = { addr, 0, false };
    return result;
}

static AddrResult addr_zero_page_x(NES *nes) {
    uint8_t base = cpu_read(nes, nes->cpu.pc++);
    uint8_t addr = base + nes->cpu.x;
    AddrResult result = { addr, 0, false };
    return result;
}

static AddrResult addr_zero_page_y(NES *nes) {
    uint8_t base = cpu_read(nes, nes->cpu.pc++);
    uint8_t addr = base + nes->cpu.y;
    AddrResult result = { addr, 0, false };
    return result;
}

static AddrResult addr_absolute(NES *nes) {
    uint16_t lo = cpu_read(nes, nes->cpu.pc++);
    uint16_t hi = cpu_read(nes, nes->cpu.pc++);
    AddrResult result = { (hi << 8) | lo, 0, false };
    return result;
}

static AddrResult addr_absolute_x(NES *nes) {
    uint16_t lo = cpu_read(nes, nes->cpu.pc++);
    uint16_t hi = cpu_read(nes, nes->cpu.pc++);
    uint16_t base = (hi << 8) | lo;
    uint16_t addr = base + nes->cpu.x;
    AddrResult result = { addr, 0, (addr & 0xFF00) != (base & 0xFF00) };
    return result;
}

static AddrResult addr_absolute_y(NES *nes) {
    uint16_t lo = cpu_read(nes, nes->cpu.pc++);
    uint16_t hi = cpu_read(nes, nes->cpu.pc++);
    uint16_t base = (hi << 8) | lo;
    uint16_t addr = base + nes->cpu.y;
    AddrResult result = { addr, 0, (addr & 0xFF00) != (base & 0xFF00) };
    return result;
}

static AddrResult addr_indirect(NES *nes) {
    uint16_t lo = cpu_read(nes, nes->cpu.pc++);
    uint16_t hi = cpu_read(nes, nes->cpu.pc++);
    uint16_t ptr = (hi << 8) | lo;
    uint16_t addr = cpu_read(nes, ptr) | (cpu_read(nes, (ptr & 0xFF00) | ((ptr + 1) & 0xFF)) << 8);
    AddrResult result = { addr, 0, false };
    return result;
}

static AddrResult addr_indexed_indirect(NES *nes) {
    uint8_t base = cpu_read(nes, nes->cpu.pc++);
    uint8_t ptr = base + nes->cpu.x;
    uint16_t addr = cpu_read(nes, ptr) | (cpu_read(nes, (uint8_t)(ptr + 1)) << 8);
    AddrResult result = { addr, 0, false };
    return result;
}

static AddrResult addr_indirect_indexed(NES *nes) {
    uint8_t base = cpu_read(nes, nes->cpu.pc++);
    uint16_t addr = cpu_read(nes, base) | (cpu_read(nes, (uint8_t)(base + 1)) << 8);
    uint16_t final_addr = addr + nes->cpu.y;
    AddrResult result = { final_addr, 0, (final_addr & 0xFF00) != (addr & 0xFF00) };
    return result;
}

static void cpu_branch(NES *nes, bool condition) {
    CPU *cpu = &nes->cpu;
    int8_t offset = (int8_t)cpu_read(nes, cpu->pc++);
    if (condition) {
        uint16_t prev_pc = cpu->pc;
        cpu->pc = (uint16_t)(cpu->pc + offset);
        cpu->cycles++;
        if ((prev_pc & 0xFF00) != (cpu->pc & 0xFF00)) {
            cpu->cycles++;
        }
    }
}

static void cpu_adc(CPU *cpu, uint8_t value) {
    uint16_t sum = cpu->a + value + ((cpu->p & FLAG_CARRY) ? 1 : 0);
    if (sum > 0xFF) {
        cpu->p |= FLAG_CARRY;
    } else {
        cpu->p &= ~FLAG_CARRY;
    }
    uint8_t result = (uint8_t)sum;
    if (~(cpu->a ^ value) & (cpu->a ^ result) & 0x80) {
        cpu->p |= FLAG_OVERFLOW;
    } else {
        cpu->p &= ~FLAG_OVERFLOW;
    }
    cpu->a = result;
    cpu_set_zn(cpu, cpu->a);
}

static void cpu_sbc(CPU *cpu, uint8_t value) {
    cpu_adc(cpu, (uint8_t)(~value));
}

static void cpu_compare(CPU *cpu, uint8_t reg, uint8_t value) {
    uint16_t diff = (uint16_t)reg - value;
    if (reg >= value) {
        cpu->p |= FLAG_CARRY;
    } else {
        cpu->p &= ~FLAG_CARRY;
    }
    cpu_set_zn(cpu, (uint8_t)diff);
}

static void cpu_step(NES *nes) {
    CPU *cpu = &nes->cpu;
    uint8_t opcode = cpu_read(nes, cpu->pc++);
    switch (opcode) {
        case 0x00:
            cpu->pc++;
            cpu_push(nes, (uint8_t)(cpu->pc >> 8));
            cpu_push(nes, (uint8_t)(cpu->pc & 0xFF));
            cpu_push(nes, cpu->p | FLAG_BREAK | FLAG_UNUSED);
            cpu->p |= FLAG_INTERRUPT;
            cpu->pc = cpu_read(nes, 0xFFFE) | (cpu_read(nes, 0xFFFF) << 8);
            cpu->cycles += 7;
            break;
        case 0x01: {
            AddrResult addr = addr_indexed_indirect(nes);
            cpu->a |= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x05: {
            AddrResult addr = addr_zero_page(nes);
            cpu->a |= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0x06: {
            AddrResult addr = addr_zero_page(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((value >> 7) & FLAG_CARRY);
            value <<= 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 5;
            break;
        }
        case 0x08:
            cpu_push(nes, cpu->p | FLAG_BREAK | FLAG_UNUSED);
            cpu->cycles += 3;
            break;
        case 0x09: {
            AddrResult addr = addr_immediate(cpu);
            cpu->a |= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x0A:
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((cpu->a >> 7) & FLAG_CARRY);
            cpu->a <<= 1;
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        case 0x0D: {
            AddrResult addr = addr_absolute(nes);
            cpu->a |= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x0E: {
            AddrResult addr = addr_absolute(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((value >> 7) & FLAG_CARRY);
            value <<= 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0x10:
            cpu_branch(nes, !(cpu->p & FLAG_NEGATIVE));
            cpu->cycles += 2;
            break;
        case 0x11: {
            AddrResult addr = addr_indirect_indexed(nes);
            cpu->a |= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 5 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x15: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu->a |= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x16: {
            AddrResult addr = addr_zero_page_x(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((value >> 7) & FLAG_CARRY);
            value <<= 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0x18:
            cpu->p &= ~FLAG_CARRY;
            cpu->cycles += 2;
            break;
        case 0x19: {
            AddrResult addr = addr_absolute_y(nes);
            cpu->a |= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x1D: {
            AddrResult addr = addr_absolute_x(nes);
            cpu->a |= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x1E: {
            AddrResult addr = addr_absolute_x(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((value >> 7) & FLAG_CARRY);
            value <<= 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 7;
            break;
        }
        case 0x20: {
            AddrResult addr = addr_absolute(nes);
            cpu_push(nes, (uint8_t)((cpu->pc - 1) >> 8));
            cpu_push(nes, (uint8_t)((cpu->pc - 1) & 0xFF));
            cpu->pc = addr.addr;
            cpu->cycles += 6;
            break;
        }
        case 0x21: {
            AddrResult addr = addr_indexed_indirect(nes);
            cpu->a &= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x24: {
            AddrResult addr = addr_zero_page(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~(FLAG_ZERO | FLAG_NEGATIVE | FLAG_OVERFLOW)) |
                     (value & FLAG_NEGATIVE) |
                     ((value & 0x40) ? FLAG_OVERFLOW : 0);
            if ((cpu->a & value) == 0) {
                cpu->p |= FLAG_ZERO;
            }
            cpu->cycles += 3;
            break;
        }
        case 0x25: {
            AddrResult addr = addr_zero_page(nes);
            cpu->a &= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0x26: {
            AddrResult addr = addr_zero_page(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            uint8_t carry = cpu->p & FLAG_CARRY ? 1 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((value >> 7) & FLAG_CARRY);
            value = (value << 1) | carry;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 5;
            break;
        }
        case 0x28:
            cpu->p = cpu_pop(nes);
            cpu->p |= FLAG_UNUSED;
            cpu->cycles += 4;
            break;
        case 0x29: {
            AddrResult addr = addr_immediate(cpu);
            cpu->a &= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x2A: {
            uint8_t carry = cpu->p & FLAG_CARRY ? 1 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((cpu->a >> 7) & FLAG_CARRY);
            cpu->a = (cpu->a << 1) | carry;
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x2C: {
            AddrResult addr = addr_absolute(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~(FLAG_ZERO | FLAG_NEGATIVE | FLAG_OVERFLOW)) |
                     (value & FLAG_NEGATIVE) |
                     ((value & 0x40) ? FLAG_OVERFLOW : 0);
            if ((cpu->a & value) == 0) {
                cpu->p |= FLAG_ZERO;
            }
            cpu->cycles += 4;
            break;
        }
        case 0x2D: {
            AddrResult addr = addr_absolute(nes);
            cpu->a &= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x2E: {
            AddrResult addr = addr_absolute(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            uint8_t carry = cpu->p & FLAG_CARRY ? 1 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((value >> 7) & FLAG_CARRY);
            value = (value << 1) | carry;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0x30:
            cpu_branch(nes, cpu->p & FLAG_NEGATIVE);
            cpu->cycles += 2;
            break;
        case 0x31: {
            AddrResult addr = addr_indirect_indexed(nes);
            cpu->a &= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 5 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x35: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu->a &= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x36: {
            AddrResult addr = addr_zero_page_x(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            uint8_t carry = cpu->p & FLAG_CARRY ? 1 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((value >> 7) & FLAG_CARRY);
            value = (value << 1) | carry;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0x38:
            cpu->p |= FLAG_CARRY;
            cpu->cycles += 2;
            break;
        case 0x39: {
            AddrResult addr = addr_absolute_y(nes);
            cpu->a &= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x3D: {
            AddrResult addr = addr_absolute_x(nes);
            cpu->a &= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x3E: {
            AddrResult addr = addr_absolute_x(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            uint8_t carry = cpu->p & FLAG_CARRY ? 1 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | ((value >> 7) & FLAG_CARRY);
            value = (value << 1) | carry;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 7;
            break;
        }
        case 0x40:
            cpu->p = cpu_pop(nes);
            cpu->pc = cpu_pop(nes);
            cpu->pc |= (uint16_t)cpu_pop(nes) << 8;
            cpu->cycles += 6;
            break;
        case 0x41: {
            AddrResult addr = addr_indexed_indirect(nes);
            cpu->a ^= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x45: {
            AddrResult addr = addr_zero_page(nes);
            cpu->a ^= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0x46: {
            AddrResult addr = addr_zero_page(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~FLAG_CARRY) | (value & 1);
            value >>= 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 5;
            break;
        }
        case 0x48:
            cpu_push(nes, cpu->a);
            cpu->cycles += 3;
            break;
        case 0x49: {
            AddrResult addr = addr_immediate(cpu);
            cpu->a ^= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x4A:
            cpu->p = (cpu->p & ~FLAG_CARRY) | (cpu->a & 1);
            cpu->a >>= 1;
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        case 0x4C: {
            AddrResult addr = addr_absolute(nes);
            cpu->pc = addr.addr;
            cpu->cycles += 3;
            break;
        }
        case 0x4D: {
            AddrResult addr = addr_absolute(nes);
            cpu->a ^= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x4E: {
            AddrResult addr = addr_absolute(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~FLAG_CARRY) | (value & 1);
            value >>= 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0x50:
            cpu_branch(nes, !(cpu->p & FLAG_OVERFLOW));
            cpu->cycles += 2;
            break;
        case 0x51: {
            AddrResult addr = addr_indirect_indexed(nes);
            cpu->a ^= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 5 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x55: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu->a ^= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x56: {
            AddrResult addr = addr_zero_page_x(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~FLAG_CARRY) | (value & 1);
            value >>= 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0x58:
            cpu->p &= ~FLAG_INTERRUPT;
            cpu->cycles += 2;
            break;
        case 0x59: {
            AddrResult addr = addr_absolute_y(nes);
            cpu->a ^= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x5D: {
            AddrResult addr = addr_absolute_x(nes);
            cpu->a ^= cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x5E: {
            AddrResult addr = addr_absolute_x(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            cpu->p = (cpu->p & ~FLAG_CARRY) | (value & 1);
            value >>= 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 7;
            break;
        }
        case 0x60:
            cpu->pc = cpu_pop(nes);
            cpu->pc |= (uint16_t)cpu_pop(nes) << 8;
            cpu->pc++;
            cpu->cycles += 6;
            break;
        case 0x61: {
            AddrResult addr = addr_indexed_indirect(nes);
            cpu_adc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 6;
            break;
        }
        case 0x65: {
            AddrResult addr = addr_zero_page(nes);
            cpu_adc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 3;
            break;
        }
        case 0x66: {
            AddrResult addr = addr_zero_page(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            uint8_t carry = cpu->p & FLAG_CARRY ? 0x80 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | (value & 1);
            value = (value >> 1) | carry;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 5;
            break;
        }
        case 0x68:
            cpu->a = cpu_pop(nes);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        case 0x69: {
            AddrResult addr = addr_immediate(cpu);
            cpu_adc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 2;
            break;
        }
        case 0x6A: {
            uint8_t carry = cpu->p & FLAG_CARRY ? 0x80 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | (cpu->a & 1);
            cpu->a = (cpu->a >> 1) | carry;
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x6C: {
            AddrResult addr = addr_indirect(nes);
            cpu->pc = addr.addr;
            cpu->cycles += 5;
            break;
        }
        case 0x6D: {
            AddrResult addr = addr_absolute(nes);
            cpu_adc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 4;
            break;
        }
        case 0x6E: {
            AddrResult addr = addr_absolute(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            uint8_t carry = cpu->p & FLAG_CARRY ? 0x80 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | (value & 1);
            value = (value >> 1) | carry;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0x70:
            cpu_branch(nes, cpu->p & FLAG_OVERFLOW);
            cpu->cycles += 2;
            break;
        case 0x71: {
            AddrResult addr = addr_indirect_indexed(nes);
            cpu_adc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 5 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x75: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu_adc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 4;
            break;
        }
        case 0x76: {
            AddrResult addr = addr_zero_page_x(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            uint8_t carry = cpu->p & FLAG_CARRY ? 0x80 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | (value & 1);
            value = (value >> 1) | carry;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0x78:
            cpu->p |= FLAG_INTERRUPT;
            cpu->cycles += 2;
            break;
        case 0x79: {
            AddrResult addr = addr_absolute_y(nes);
            cpu_adc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x7D: {
            AddrResult addr = addr_absolute_x(nes);
            cpu_adc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0x7E: {
            AddrResult addr = addr_absolute_x(nes);
            uint8_t value = cpu_read(nes, addr.addr);
            uint8_t carry = cpu->p & FLAG_CARRY ? 0x80 : 0;
            cpu->p = (cpu->p & ~FLAG_CARRY) | (value & 1);
            value = (value >> 1) | carry;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 7;
            break;
        }
        case 0x81: {
            AddrResult addr = addr_indexed_indirect(nes);
            cpu_write(nes, addr.addr, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x84: {
            AddrResult addr = addr_zero_page(nes);
            cpu_write(nes, addr.addr, cpu->y);
            cpu->cycles += 3;
            break;
        }
        case 0x85: {
            AddrResult addr = addr_zero_page(nes);
            cpu_write(nes, addr.addr, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0x86: {
            AddrResult addr = addr_zero_page(nes);
            cpu_write(nes, addr.addr, cpu->x);
            cpu->cycles += 3;
            break;
        }
        case 0x88:
            cpu->y--;
            cpu_set_zn(cpu, cpu->y);
            cpu->cycles += 2;
            break;
        case 0x8A:
            cpu->a = cpu->x;
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        case 0x8C: {
            AddrResult addr = addr_absolute(nes);
            cpu_write(nes, addr.addr, cpu->y);
            cpu->cycles += 4;
            break;
        }
        case 0x8D: {
            AddrResult addr = addr_absolute(nes);
            cpu_write(nes, addr.addr, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x8E: {
            AddrResult addr = addr_absolute(nes);
            cpu_write(nes, addr.addr, cpu->x);
            cpu->cycles += 4;
            break;
        }
        case 0x90:
            cpu_branch(nes, !(cpu->p & FLAG_CARRY));
            cpu->cycles += 2;
            break;
        case 0x91: {
            AddrResult addr = addr_indirect_indexed(nes);
            cpu_write(nes, addr.addr, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x94: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu_write(nes, addr.addr, cpu->y);
            cpu->cycles += 4;
            break;
        }
        case 0x95: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu_write(nes, addr.addr, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x96: {
            AddrResult addr = addr_zero_page_y(nes);
            cpu_write(nes, addr.addr, cpu->x);
            cpu->cycles += 4;
            break;
        }
        case 0x98:
            cpu->a = cpu->y;
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        case 0x99: {
            AddrResult addr = addr_absolute_y(nes);
            cpu_write(nes, addr.addr, cpu->a);
            cpu->cycles += 5;
            break;
        }
        case 0x9A:
            cpu->sp = cpu->x;
            cpu->cycles += 2;
            break;
        case 0x9D: {
            AddrResult addr = addr_absolute_x(nes);
            cpu_write(nes, addr.addr, cpu->a);
            cpu->cycles += 5;
            break;
        }
        case 0xA0: {
            AddrResult addr = addr_immediate(cpu);
            cpu->y = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->y);
            cpu->cycles += 2;
            break;
        }
        case 0xA1: {
            AddrResult addr = addr_indexed_indirect(nes);
            cpu->a = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0xA2: {
            AddrResult addr = addr_immediate(cpu);
            cpu->x = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        }
        case 0xA4: {
            AddrResult addr = addr_zero_page(nes);
            cpu->y = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->y);
            cpu->cycles += 3;
            break;
        }
        case 0xA5: {
            AddrResult addr = addr_zero_page(nes);
            cpu->a = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0xA6: {
            AddrResult addr = addr_zero_page(nes);
            cpu->x = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->x);
            cpu->cycles += 3;
            break;
        }
        case 0xA8:
            cpu->y = cpu->a;
            cpu_set_zn(cpu, cpu->y);
            cpu->cycles += 2;
            break;
        case 0xA9: {
            AddrResult addr = addr_immediate(cpu);
            cpu->a = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0xAA:
            cpu->x = cpu->a;
            cpu_set_zn(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        case 0xAC: {
            AddrResult addr = addr_absolute(nes);
            cpu->y = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->y);
            cpu->cycles += 4;
            break;
        }
        case 0xAD: {
            AddrResult addr = addr_absolute(nes);
            cpu->a = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0xAE: {
            AddrResult addr = addr_absolute(nes);
            cpu->x = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->x);
            cpu->cycles += 4;
            break;
        }
        case 0xB0:
            cpu_branch(nes, cpu->p & FLAG_CARRY);
            cpu->cycles += 2;
            break;
        case 0xB1: {
            AddrResult addr = addr_indirect_indexed(nes);
            cpu->a = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 5 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xB4: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu->y = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->y);
            cpu->cycles += 4;
            break;
        }
        case 0xB5: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu->a = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0xB6: {
            AddrResult addr = addr_zero_page_y(nes);
            cpu->x = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->x);
            cpu->cycles += 4;
            break;
        }
        case 0xB8:
            cpu->p &= ~FLAG_OVERFLOW;
            cpu->cycles += 2;
            break;
        case 0xB9: {
            AddrResult addr = addr_absolute_y(nes);
            cpu->a = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xBA:
            cpu->x = cpu->sp;
            cpu_set_zn(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        case 0xBC: {
            AddrResult addr = addr_absolute_x(nes);
            cpu->y = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->y);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xBD: {
            AddrResult addr = addr_absolute_x(nes);
            cpu->a = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->a);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xBE: {
            AddrResult addr = addr_absolute_y(nes);
            cpu->x = cpu_read(nes, addr.addr);
            cpu_set_zn(cpu, cpu->x);
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xC0: {
            AddrResult addr = addr_immediate(cpu);
            cpu_compare(cpu, cpu->y, cpu_read(nes, addr.addr));
            cpu->cycles += 2;
            break;
        }
        case 0xC1: {
            AddrResult addr = addr_indexed_indirect(nes);
            cpu_compare(cpu, cpu->a, cpu_read(nes, addr.addr));
            cpu->cycles += 6;
            break;
        }
        case 0xC4: {
            AddrResult addr = addr_zero_page(nes);
            cpu_compare(cpu, cpu->y, cpu_read(nes, addr.addr));
            cpu->cycles += 3;
            break;
        }
        case 0xC5: {
            AddrResult addr = addr_zero_page(nes);
            cpu_compare(cpu, cpu->a, cpu_read(nes, addr.addr));
            cpu->cycles += 3;
            break;
        }
        case 0xC6: {
            AddrResult addr = addr_zero_page(nes);
            uint8_t value = cpu_read(nes, addr.addr) - 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 5;
            break;
        }
        case 0xC8:
            cpu->y++;
            cpu_set_zn(cpu, cpu->y);
            cpu->cycles += 2;
            break;
        case 0xC9: {
            AddrResult addr = addr_immediate(cpu);
            cpu_compare(cpu, cpu->a, cpu_read(nes, addr.addr));
            cpu->cycles += 2;
            break;
        }
        case 0xCA:
            cpu->x--;
            cpu_set_zn(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        case 0xCC: {
            AddrResult addr = addr_absolute(nes);
            cpu_compare(cpu, cpu->y, cpu_read(nes, addr.addr));
            cpu->cycles += 4;
            break;
        }
        case 0xCD: {
            AddrResult addr = addr_absolute(nes);
            cpu_compare(cpu, cpu->a, cpu_read(nes, addr.addr));
            cpu->cycles += 4;
            break;
        }
        case 0xCE: {
            AddrResult addr = addr_absolute(nes);
            uint8_t value = cpu_read(nes, addr.addr) - 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0xD0:
            cpu_branch(nes, !(cpu->p & FLAG_ZERO));
            cpu->cycles += 2;
            break;
        case 0xD1: {
            AddrResult addr = addr_indirect_indexed(nes);
            cpu_compare(cpu, cpu->a, cpu_read(nes, addr.addr));
            cpu->cycles += 5 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xD5: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu_compare(cpu, cpu->a, cpu_read(nes, addr.addr));
            cpu->cycles += 4;
            break;
        }
        case 0xD6: {
            AddrResult addr = addr_zero_page_x(nes);
            uint8_t value = cpu_read(nes, addr.addr) - 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0xD8:
            cpu->p &= ~FLAG_DECIMAL;
            cpu->cycles += 2;
            break;
        case 0xD9: {
            AddrResult addr = addr_absolute_y(nes);
            cpu_compare(cpu, cpu->a, cpu_read(nes, addr.addr));
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xDD: {
            AddrResult addr = addr_absolute_x(nes);
            cpu_compare(cpu, cpu->a, cpu_read(nes, addr.addr));
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xDE: {
            AddrResult addr = addr_absolute_x(nes);
            uint8_t value = cpu_read(nes, addr.addr) - 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 7;
            break;
        }
        case 0xE0: {
            AddrResult addr = addr_immediate(cpu);
            cpu_compare(cpu, cpu->x, cpu_read(nes, addr.addr));
            cpu->cycles += 2;
            break;
        }
        case 0xE1: {
            AddrResult addr = addr_indexed_indirect(nes);
            cpu_sbc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 6;
            break;
        }
        case 0xE4: {
            AddrResult addr = addr_zero_page(nes);
            cpu_compare(cpu, cpu->x, cpu_read(nes, addr.addr));
            cpu->cycles += 3;
            break;
        }
        case 0xE5: {
            AddrResult addr = addr_zero_page(nes);
            cpu_sbc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 3;
            break;
        }
        case 0xE6: {
            AddrResult addr = addr_zero_page(nes);
            uint8_t value = cpu_read(nes, addr.addr) + 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 5;
            break;
        }
        case 0xE8:
            cpu->x++;
            cpu_set_zn(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        case 0xE9: {
            AddrResult addr = addr_immediate(cpu);
            cpu_sbc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 2;
            break;
        }
        case 0xEA:
            cpu->cycles += 2;
            break;
        case 0xEC: {
            AddrResult addr = addr_absolute(nes);
            cpu_compare(cpu, cpu->x, cpu_read(nes, addr.addr));
            cpu->cycles += 4;
            break;
        }
        case 0xED: {
            AddrResult addr = addr_absolute(nes);
            cpu_sbc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 4;
            break;
        }
        case 0xEE: {
            AddrResult addr = addr_absolute(nes);
            uint8_t value = cpu_read(nes, addr.addr) + 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0xF0:
            cpu_branch(nes, cpu->p & FLAG_ZERO);
            cpu->cycles += 2;
            break;
        case 0xF1: {
            AddrResult addr = addr_indirect_indexed(nes);
            cpu_sbc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 5 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xF5: {
            AddrResult addr = addr_zero_page_x(nes);
            cpu_sbc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 4;
            break;
        }
        case 0xF6: {
            AddrResult addr = addr_zero_page_x(nes);
            uint8_t value = cpu_read(nes, addr.addr) + 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 6;
            break;
        }
        case 0xF8:
            cpu->p |= FLAG_DECIMAL;
            cpu->cycles += 2;
            break;
        case 0xF9: {
            AddrResult addr = addr_absolute_y(nes);
            cpu_sbc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xFD: {
            AddrResult addr = addr_absolute_x(nes);
            cpu_sbc(cpu, cpu_read(nes, addr.addr));
            cpu->cycles += 4 + (addr.page_crossed ? 1 : 0);
            break;
        }
        case 0xFE: {
            AddrResult addr = addr_absolute_x(nes);
            uint8_t value = cpu_read(nes, addr.addr) + 1;
            cpu_write(nes, addr.addr, value);
            cpu_set_zn(cpu, value);
            cpu->cycles += 7;
            break;
        }
        default:
            cpu->cycles += 2;
            break;
    }
}

static bool load_rom(const char *path, RomImage *image) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *buffer = malloc((size_t)size);
    if (!buffer) {
        fclose(file);
        return false;
    }
    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(buffer);
        return false;
    }
    fclose(file);
    image->rom_data = buffer;
    image->rom_size = (size_t)size;
    return true;
}

static bool parse_ines(const RomImage *image, Cartridge *cart) {
    if (image->rom_size < 16) {
        return false;
    }
    const uint8_t *header = image->rom_data;
    if (memcmp(header, "NES\x1A", 4) != 0) {
        return false;
    }
    uint8_t prg_banks = header[4];
    uint8_t chr_banks = header[5];
    uint8_t flags6 = header[6];
    uint8_t mapper = (flags6 >> 4);
    if (mapper != 0) {
        fprintf(stderr, "Only mapper 0 is supported.\n");
        return false;
    }
    size_t offset = 16;
    if (flags6 & 0x04) {
        offset += 512;
    }
    size_t prg_size = prg_banks * PRG_ROM_BANK_SIZE;
    size_t chr_size = chr_banks * CHR_ROM_BANK_SIZE;
    if (image->rom_size < offset + prg_size + chr_size) {
        return false;
    }
    memset(cart, 0, sizeof(*cart));
    cart->prg_rom_size = prg_size ? prg_size : PRG_ROM_BANK_SIZE;
    memcpy(cart->prg_rom, image->rom_data + offset, prg_size);
    offset += prg_size;
    cart->chr_rom_size = chr_size;
    if (chr_size > 0) {
        memcpy(cart->chr_rom, image->rom_data + offset, chr_size);
    }
    return true;
}

static void free_rom(RomImage *image) {
    free((void *)image->rom_data);
    image->rom_data = NULL;
    image->rom_size = 0;
}

static void render_frame(const NES *nes, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", FRAME_WIDTH, FRAME_HEIGHT);
    for (int i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; i++) {
        uint8_t shade = nes->ppu.framebuffer[i];
        uint8_t rgb[3] = { shade, shade, shade };
        fwrite(rgb, 1, sizeof(rgb), file);
    }
    fclose(file);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom.nes> [frames]\n", argv[0]);
        return 1;
    }

    RomImage image = {0};
    if (!load_rom(argv[1], &image)) {
        fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
        return 1;
    }

    NES nes;
    memset(&nes, 0, sizeof(nes));
    if (!parse_ines(&image, &nes.cart)) {
        fprintf(stderr, "Invalid iNES ROM or unsupported mapper.\n");
        free_rom(&image);
        return 1;
    }

    ppu_reset(&nes.ppu);
    uint16_t reset_vector = cpu_read(&nes, 0xFFFC) | (cpu_read(&nes, 0xFFFD) << 8);
    cpu_reset(&nes.cpu, reset_vector);

    int frames = 1;
    if (argc > 2) {
        frames = atoi(argv[2]);
        if (frames <= 0) {
            frames = 1;
        }
    }

    int rendered = 0;
    int last_scanline = nes.ppu.scanline;
    while (rendered < frames) {
        if (nes.ppu.nmi_triggered) {
            cpu_nmi(&nes);
            nes.ppu.nmi_triggered = false;
        }
        cpu_step(&nes);
        for (int i = 0; i < 3; i++) {
            ppu_step(&nes.ppu);
        }
        if (nes.ppu.scanline == 0 && last_scanline == 261) {
            char path[64];
            snprintf(path, sizeof(path), "frame_%03d.ppm", rendered);
            render_frame(&nes, path);
            rendered++;
        }
        last_scanline = nes.ppu.scanline;
    }

    free_rom(&image);
    return 0;
}
