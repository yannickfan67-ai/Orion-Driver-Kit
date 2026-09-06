# Orion Driver Kit (ODK)

ODK is the development toolkit for drivers targeting the Orion Driver Interface (ODI).

Current tool version: **0.1**.

## Commands

```bash
python3 odk.py new my-driver
python3 odk.py pack --manifest my-driver/driver.toml --payload my-driver/driver.elf -o my-driver.odrv
python3 odk.py inspect my-driver.odrv
python3 odk.py check my-driver.odrv
```

`pack` creates an ODRV v1 container with a 128-byte fixed header and CRC32 integrity check.

## Reference drivers

- `examples/qemu-edu` — PCI/MMIO reference driver for QEMU's educational device (`1234:11e8`)
- `examples/fw_cfg` — x86 PIO reference driver for QEMU fw_cfg (`0x510/0x511`)

The goal is to keep the driver projects independent from UN_Orion's private kernel symbols. They target the ABI published in the separate `Orion-Driver-Interface` repository.

## Roadmap

1. ODRV packaging and validation
2. freestanding x86_64 driver build helper
3. QEMU EDU liveness/IRQ/DMA tests
4. fw_cfg signature/file-directory tests
5. VirtIO PCI transport template
6. UN_Orion dynamic `.odrv` loader
