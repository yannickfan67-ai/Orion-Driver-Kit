# Orion Driver Kit roadmap

- `odk build`: freestanding compiler/linker wrapper for ODI drivers
- `odk lint`: manifest, ABI, capability and unsafe-I/O validation
- `odk test`: host/QEMU reference-device test harness
- `odk sign` and signature verification tooling
- symbol/export checks so drivers use only ODI services
- templates for PCI, MMIO, PIO, IRQ and DMA devices
- reference drivers for QEMU EDU and fw_cfg first
- VirtIO PCI transport template after ODI DMA/interrupt contracts stabilize
- CI compatibility matrix against supported ODI minor versions
- package/repository index generation for `.odrv` distribution
