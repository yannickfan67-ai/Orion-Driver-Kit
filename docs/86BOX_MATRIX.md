# 86Box / legacy-PC ODI compatibility matrix

This matrix turns 86Box into a hardware-regression target for Orion drivers. The emulator is **not** part of ODI; every test communicates with the emulated device through normal ISA/PCI I/O, MMIO, IRQ or DMA.

Status vocabulary: `working-source` = low-level device communication is implemented in ODK; `kernel-existing` = UN_Orion already has an in-kernel implementation to migrate to ODI; `planned` = ABI/test slot exists but device implementation remains.

| Class | Device family | Bus | ODI target | Status | Test objective |
|---|---|---|---|---|---|
| network | NE2000 / DP8390 compatible | ISA | `examples/86box-ne2000` | working-source | reset, PROM/MAC read, packet remote-DMA path |
| network | RTL8139 | PCI | ODI migration | kernel-existing | PCI BAR, RX/TX DMA, IRQ |
| network | AMD PCnet-PCI | PCI | `pcnet` | planned | init block, descriptors, IRQ |
| network | DEC 21040 Tulip | PCI | `tulip` | planned | descriptor rings, IRQ |
| block | ATA/IDE PIO | ISA-compatible legacy IDE | `examples/86box-ide-pio` | working-source | IDENTIFY, LBA28 sector read/write, cache flush |
| block | ATAPI CD-ROM | IDE | `atapi` | planned | PACKET, READ(10), removable media |
| block | Adaptec AHA-154x | ISA/SCSI | `aha154x` | planned | mailbox command path, DMA |
| display | VGA baseline | ISA/PCI | `vga` | planned | mode/probe, linearized console fallback |
| display | VBE/Bochs-compatible path | firmware/display | `vbe` | planned | mode enumeration, framebuffer publish |
| display | S3/Cirrus SVGA | PCI | `s3` / `cirrus` | planned | native linear framebuffer modes |
| input | i8042 PS/2 keyboard/mouse | platform | ODI migration | kernel-existing | shared input service |
| input | 16550 serial | ISA/platform | `uart16550` | planned | console/input fallback |
| audio | Sound Blaster 16 | ISA | `sb16` | planned | DSP reset/version, DMA PCM |
| audio | AC'97 | PCI | `ac97` | planned | codec reset, PCM bus-master |
| bus | PCI configuration mechanism #1 | platform | ODI bus layer | kernel-existing | enumerate PCI functions independent of NIC driver |
| firmware | ACPI tables | platform | `acpi` | planned | RSDP/XSDT discovery, device resources |

## Recommended 86Box machine profiles

### Legacy ISA profile
- late 486 / early Pentium-class machine
- IDE controller enabled
- NE2000-compatible ISA NIC at a known resource tuple (for example I/O `0x300`, IRQ 10)
- PS/2 or AT keyboard path
- VGA/SVGA adapter
- optional Sound Blaster 16

This profile exercises ISA PIO, IRQ and legacy DMA.

### PCI transition profile
- Pentium/Pentium II-class PCI machine
- RTL8139 or AMD PCnet-PCI
- PCI VGA/SVGA
- IDE/ATAPI storage
- optional AC'97-era audio where the selected 86Box machine/card supports it

This profile exercises PCI config/BARs, bus-master DMA and shared IRQ behavior.

## Image compatibility

UN_Orion boot media should be tested in two representations:
- raw whole-disk image (`.img`) for firmware/drive boot;
- ISO for optical install/live boot where the selected machine firmware supports the required boot mode.

Current UN_Orion 0.0.5 is x86_64 UEFI-first, so many historically accurate 86Box machines cannot boot it yet. The driver matrix is still useful before BIOS/legacy boot support exists: drivers can be tested in dedicated ODI probes or in a future i686/BIOS Orion image. Do not misinterpret a firmware boot failure as a device-driver failure.

## Expansion order

1. NE2000 + IDE PIO
2. migrate RTL8139 and i8042 behind ODI services
3. PCI bus core + PCnet/Tulip
4. VGA/VBE display service
5. SB16 audio
6. ATAPI/SCSI
7. ACPI/USB and newer PCI devices
