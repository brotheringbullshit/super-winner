# NES Reference: CPU & PPU

## CPU: Ricoh 2A03

- **Type:** 8-bit microprocessor (MOS 6502 variant)
- **Clock:** ~1.79 MHz (NTSC), ~1.66 MHz (PAL)
- **Registers:**
  - **A** — Accumulator
  - **X, Y** — Index registers
  - **P** — Processor status flags
  - **SP** — Stack pointer
  - **PC** — Program counter
- **Memory Map:**
  - `$0000-$07FF` — Internal RAM (mirrored up to `$1FFF`)
  - `$2000-$2007` — PPU registers
  - `$4000-$4017` — APU / I/O registers
  - `$6000-$FFFF` — Cartridge ROM / PRG-ROM
- **Instructions:** 56 base instructions (6502 subset)
- **Notes:**
  - No decimal mode on 2A03 (disabled BCD)
  - Built-in audio generation (APU)

---

## PPU: Picture Processing Unit

- **Type:** 8-bit graphics processor
- **Clock:** ~5.37 MHz (NTSC)
- **Resolution:** 256x240 pixels (logical), 262 scanlines per frame
- **Registers:**
  - `$2000` — PPUCTRL (control)
  - `$2001` — PPUMASK (mask)
  - `$2002` — PPUSTATUS (status)
  - `$2003` — OAMADDR (sprite address)
  - `$2004` — OAMDATA (sprite data)
  - `$2005` — PPUSCROLL (scroll)
  - `$2006` — PPUADDR (VRAM address)
  - `$2007` — PPUDATA (VRAM data)
- **Memory:**
  - **Pattern tables:** `$0000-$1FFF` (CHR ROM/RAM)
  - **Name tables:** `$2000-$2FFF`
  - **Palettes:** `$3F00-$3F1F`
- **Sprites:** 64 total, 8x8 or 8x16 pixels
- **Notes:**
  - Supports background and sprite layers
  - 8-color palette per tile
  - Non-linear memory access requires careful timing

---

## Quick Reference Table

| Component | Address / Registers | Notes |
|-----------|-------------------|-------|
| CPU A    | `A`               | Accumulator |
| CPU X    | `X`               | Index register |
| CPU Y    | `Y`               | Index register |
| CPU P    | `P`               | Status flags |
| CPU SP   | `SP`              | Stack pointer |
| CPU PC   | `PC`              | Program counter |
| PPU CTRL | `$2000`           | Control register |
| PPU MASK | `$2001`           | Rendering mask |
| PPU STAT | `$2002`           | Status / VBlank |
| PPU OAMADDR | `$2003`        | Sprite memory address |
| PPU OAMDATA | `$2004`        | Sprite memory data |
| PPU SCROLL | `$2005`          | Scroll X/Y |
| PPU ADDR | `$2006`           | VRAM address |
| PPU DATA | `$2007`           | VRAM read/write |
| Pattern Tables | `$0000-$1FFF` | Tile graphics |
| Name Tables | `$2000-$2FFF`  | Background maps |
| Palettes | `$3F00-$3F1F`     | Colors for sprites/bg |



# NES Cheat Sheet: CPU, PPU & Memory Map

| Component | Addr / Reg | Description |
|-----------|------------|-------------|
| **CPU A** | `A`        | Accumulator |
| **CPU X** | `X`        | Index register |
| **CPU Y** | `Y`        | Index register |
| **CPU P** | `P`        | Status flags |
| **CPU SP**| `SP`       | Stack pointer |
| **CPU PC**| `PC`       | Program counter |
| **PPU CTRL** | `$2000` | Control (NMI, VRAM increment, sprite table) |
| **PPU MASK** | `$2001` | Render mask (sprites/bg, color emphasis) |
| **PPU STAT** | `$2002` | Status, VBlank, sprite 0 hit |
| **PPU OAMADDR** | `$2003` | Sprite memory address |
| **PPU OAMDATA** | `$2004` | Sprite memory data |
| **PPU SCROLL** | `$2005` | Scroll X/Y |
| **PPU ADDR** | `$2006` | VRAM address |
| **PPU DATA** | `$2007` | VRAM read/write |
| **Pattern Tables** | `$0000-$1FFF` | Tile graphics (CHR ROM/RAM) |
| **Name Tables** | `$2000-$2FFF` | Background maps |
| **Palettes** | `$3F00-$3F1F` | Colors for sprites & BG |
| **CPU RAM** | `$0000-$07FF` | 2KB internal RAM (mirrored to `$1FFF`) |
| **PPU Registers** | `$2000-$2007` | PPU I/O registers (mirrored to `$3FFF`) |
| **APU & I/O** | `$4000-$4017` | Audio & controller I/O |
| **Expansion / SRAM** | `$6000-$7FFF` | Optional cartridge SRAM |
| **PRG-ROM** | `$8000-$FFFF` | Program ROM (from cartridge) |





# Ricoh 2A03 Legal Opcodes (6502 Subset)

| Opcode | Hex | Addressing Mode | Description |
|--------|-----|----------------|-------------|
| ADC    | 69  | Immediate      | Add with carry |
| ADC    | 65  | Zero Page      | Add with carry |
| ADC    | 75  | Zero Page,X    | Add with carry |
| ADC    | 6D  | Absolute       | Add with carry |
| ADC    | 7D  | Absolute,X     | Add with carry |
| ADC    | 79  | Absolute,Y     | Add with carry |
| ADC    | 61  | (Indirect,X)  | Add with carry |
| ADC    | 71  | (Indirect),Y  | Add with carry |
| AND    | 29  | Immediate      | AND accumulator |
| AND    | 25  | Zero Page      | AND accumulator |
| AND    | 35  | Zero Page,X    | AND accumulator |
| AND    | 2D  | Absolute       | AND accumulator |
| AND    | 3D  | Absolute,X     | AND accumulator |
| AND    | 39  | Absolute,Y     | AND accumulator |
| AND    | 21  | (Indirect,X)  | AND accumulator |
| AND    | 31  | (Indirect),Y  | AND accumulator |
| ASL    | 0A  | Accumulator    | Arithmetic shift left |
| ASL    | 06  | Zero Page      | Arithmetic shift left |
| ASL    | 16  | Zero Page,X    | Arithmetic shift left |
| ASL    | 0E  | Absolute       | Arithmetic shift left |
| ASL    | 1E  | Absolute,X     | Arithmetic shift left |
| BCC    | 90  | Relative       | Branch if carry clear |
| BCS    | B0  | Relative       | Branch if carry set |
| BEQ    | F0  | Relative       | Branch if equal (zero set) |
| BIT    | 24  | Zero Page      | Test bits |
| BIT    | 2C  | Absolute       | Test bits |
| BMI    | 30  | Relative       | Branch if minus (N set) |
| BNE    | D0  | Relative       | Branch if not equal (Z clear) |
| BPL    | 10  | Relative       | Branch if positive (N clear) |
| BRK    | 00  | Implied        | Force interrupt |
| BVC    | 50  | Relative       | Branch if overflow clear |
| BVS    | 70  | Relative       | Branch if overflow set |
| CLC    | 18  | Implied        | Clear carry |
| CLD    | D8  | Implied        | Clear decimal mode |
| CLI    | 58  | Implied        | Clear interrupt disable |
| CLV    | B8  | Implied        | Clear overflow flag |
| CMP    | C9  | Immediate      | Compare accumulator |
| CMP    | C5  | Zero Page      | Compare accumulator |
| CMP    | D5  | Zero Page,X    | Compare accumulator |
| CMP    | CD  | Absolute       | Compare accumulator |
| CMP    | DD  | Absolute,X     | Compare accumulator |
| CMP    | D9  | Absolute,Y     | Compare accumulator |
| CMP    | C1  | (Indirect,X)  | Compare accumulator |
| CMP    | D1  | (Indirect),Y  | Compare accumulator |
| CPX    | E0  | Immediate      | Compare X register |
| CPX    | E4  | Zero Page      | Compare X register |
| CPX    | EC  | Absolute       | Compare X register |
| CPY    | C0  | Immediate      | Compare Y register |
| CPY    | C4  | Zero Page      | Compare Y register |
| CPY    | CC  | Absolute       | Compare Y register |
| DEC    | C6  | Zero Page      | Decrement memory |
| DEC    | D6  | Zero Page,X    | Decrement memory |
| DEC    | CE  | Absolute       | Decrement memory |
| DEC    | DE  | Absolute,X     | Decrement memory |
| DEX    | CA  | Implied        | Decrement X |
| DEY    | 88  | Implied        | Decrement Y |
| EOR    | 49  | Immediate      | Exclusive OR |
| EOR    | 45  | Zero Page      | Exclusive OR |
| EOR    | 55  | Zero Page,X    | Exclusive OR |
| EOR    | 4D  | Absolute       | Exclusive OR |
| EOR    | 5D  | Absolute,X     | Exclusive OR |
| EOR    | 59  | Absolute,Y     | Exclusive OR |
| EOR    | 41  | (Indirect,X)  | Exclusive OR |
| EOR    | 51  | (Indirect),Y  | Exclusive OR |
| INC    | E6  | Zero Page      | Increment memory |
| INC    | F6  | Zero Page,X    | Increment memory |
| INC    | EE  | Absolute       | Increment memory |
| INC    | FE  | Absolute,X     | Increment memory |
| INX    | E8  | Implied        | Increment X |
| INY    | C8  | Implied        | Increment Y |
| JMP    | 4C  | Absolute       | Jump |
| JMP    | 6C  | Indirect       | Jump indirect |
| JSR    | 20  | Absolute       | Jump to subroutine |
| LDA    | A9  | Immediate      | Load accumulator |
| LDA    | A5  | Zero Page      | Load accumulator |
| LDA    | B5  | Zero Page,X    | Load accumulator |
| LDA    | AD  | Absolute       | Load accumulator |
| LDA    | BD  | Absolute,X     | Load accumulator |
| LDA    | B9  | Absolute,Y     | Load accumulator |
| LDA    | A1  | (Indirect,X)  | Load accumulator |
| LDA    | B1  | (Indirect),Y  | Load accumulator |
| LDX    | A2  | Immediate      | Load X register |
| LDX    | A6  | Zero Page      | Load X register |
| LDX    | B6  | Zero Page,Y    | Load X register |
| LDX    | AE  | Absolute       | Load X register |
| LDX    | BE  | Absolute,Y     | Load X register |
| LDY    | A0  | Immediate      | Load Y register |
| LDY    | A4  | Zero Page      | Load Y register |
| LDY    | B4  | Zero Page,X    | Load Y register |
| LDY    | AC  | Absolute       | Load Y register |
| LDY    | BC  | Absolute,X     | Load Y register |
| LSR    | 4A  | Accumulator    | Logical shift right |
| LSR    | 46  | Zero Page      | Logical shift right |
| LSR    | 56  | Zero Page,X    | Logical shift right |
| LSR    | 4E  | Absolute       | Logical shift right |
| LSR    | 5E  | Absolute,X     | Logical shift right |
| NOP    | EA  | Implied        | No operation |
| ORA    | 09  | Immediate      | OR accumulator |
| ORA    | 05  | Zero Page      | OR accumulator |
| ORA    | 15  | Zero Page,X    | OR accumulator |
| ORA    | 0D  | Absolute       | OR accumulator |
| ORA    | 1D  | Absolute,X     | OR accumulator |
| ORA    | 19  | Absolute,Y     | OR accumulator |
| ORA    | 01  | (Indirect,X)  | OR accumulator |
| ORA    | 11  | (Indirect),Y  | OR accumulator |
| PHA    | 48  | Implied        | Push accumulator |
| PHP    | 08  | Implied        | Push processor status |
| PLA    | 68  | Implied        | Pull accumulator |
| PLP    | 28  | Implied        | Pull processor status |
| ROL    | 2A  | Accumulator    | Rotate left |
| ROL    | 26  | Zero Page      | Rotate left |
| ROL    | 36  | Zero Page,X    | Rotate left |
| ROL    | 2E  | Absolute       | Rotate left |
| ROL    | 3E  | Absolute,X     | Rotate left |
| ROR    | 6A  | Accumulator    | Rotate right |
| ROR    | 66  | Zero Page      | Rotate right |
| ROR    | 76  | Zero Page,X    | Rotate right |
| ROR    | 6E  | Absolute       | Rotate right |
| ROR    | 7E  | Absolute,X     | Rotate right |
| RTI    | 40  | Implied        | Return from interrupt |
| RTS    | 60  | Implied        | Return from subroutine |
| SBC    | E9  | Immediate      | Subtract with carry |
| SBC    | E5  | Zero Page      | Subtract with carry |
| SBC    | F5  | Zero Page,X    | Subtract with carry |
| SBC    | ED  | Absolute       | Subtract with carry |
| SBC    | FD  | Absolute,X     | Subtract with carry |
| SBC    | F9  | Absolute,Y     | Subtract with carry |
| SBC    | E1  | (Indirect,X)  | Subtract with carry |
| SBC    | F1  | (Indirect),Y  | Subtract with carry |
| SEC    | 38  | Implied        | Set carry |
| SED    | F8  | Implied        | Set decimal mode |
| SEI    | 78  | Implied        | Set interrupt disable |
| STA    | 85  | Zero Page      | Store accumulator |
| STA    | 95  | Zero Page,X    | Store accumulator |
| STA    | 8D  | Absolute       | Store accumulator |
| STA    | 9D  | Absolute,X     | Store accumulator |
| STA    | 99  | Absolute,Y     | Store accumulator |
| STA    | 81  | (Indirect,X)  | Store accumulator |
| STA    | 91  | (Indirect),Y  | Store accumulator |
| STX    | 86  | Zero Page      | Store X |
| STX    | 96  | Zero Page,Y    | Store X |
| STX    | 8E  | Absolute       | Store X |
| STY    | 84  | Zero Page      | Store Y |
| STY    | 94  | Zero Page,X    | Store Y |
| STY    | 8C  | Absolute       | Store Y |
| TAX    | AA  | Implied        | Transfer A -> X |
| TAY    | A8  | Implied        | Transfer A -> Y |
| TSX    | BA  | Implied        | Transfer SP -> X |
| TXA    | 8A  | Implied        | Transfer X -> A |
| TXS    | 9A  | Implied        | Transfer X -> SP |
| TYA    | 98  | Implied        | Transfer Y -> A |
