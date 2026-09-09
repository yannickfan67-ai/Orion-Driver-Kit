#!/usr/bin/env python3
import argparse, pathlib, struct, sys, zlib

MAGIC=b'ODRV'
FORMAT_VERSION=1
HEADER_SIZE=128
# ODRV package IDs are stable and intentionally distinct from ODI runtime IDs.
ARCHES={'x86_64':1,'aarch64':2,'i686':3,'riscv64':4}
ARCH_NAMES={v:k for k,v in ARCHES.items()}
ODI_ARCH_MACROS={'i686':'ODI_ARCH_I686','x86_64':'ODI_ARCH_X86_64','aarch64':'ODI_ARCH_AARCH64','riscv64':'ODI_ARCH_RISCV64'}
PAYLOAD_ELF64=1
PAYLOAD_ELF32=2
HEADER=struct.Struct('<4sHHHHHHIIIIIIII80s')
CRC_OFF=44

CLASSES={'platform','network','block','display','input','audio','bus','rng','usb','firmware'}
BUSES={'platform','pci','pcie','virtio-pci','isa','usb','acpi'}

def parse_manifest(path):
    return pathlib.Path(path).read_text(encoding='utf-8').encode('utf-8')

def payload_kind_for(arch):
    return PAYLOAD_ELF32 if arch=='i686' else PAYLOAD_ELF64

def expected_payload_kind_for_arch_id(arch):
    return PAYLOAD_ELF32 if arch==ARCHES['i686'] else PAYLOAD_ELF64 if arch in ARCH_NAMES else 0

def elf_class(payload):
    if len(payload)<5 or not payload.startswith(b'\x7fELF'):
        return 0
    return payload[4]

def build_header(abi_major,abi_minor,arch,payload_kind,flags,mo,ms,po,ps,ro,rs,crc):
    return HEADER.pack(MAGIC,FORMAT_VERSION,HEADER_SIZE,abi_major,abi_minor,arch,payload_kind,flags,mo,ms,po,ps,ro,rs,crc,b'\0'*80)

def crc_bytes(blob):
    b=bytearray(blob); b[CRC_OFF:CRC_OFF+4]=b'\0\0\0\0'
    return zlib.crc32(b)&0xffffffff

def pack(args):
    manifest=parse_manifest(args.manifest)
    payload=pathlib.Path(args.payload).read_bytes()
    kind=payload_kind_for(args.arch)
    expected_class=1 if kind==PAYLOAD_ELF32 else 2
    if elf_class(payload)!=expected_class:
        raise SystemExit(f'payload ELF class does not match {args.arch}')
    resources=pathlib.Path(args.resources).read_bytes() if args.resources else b''
    mo=HEADER_SIZE; ms=len(manifest); po=mo+ms; ps=len(payload); ro=po+ps; rs=len(resources)
    h=build_header(args.abi_major,args.abi_minor,ARCHES[args.arch],kind,0,mo,ms,po,ps,ro,rs,0)
    blob=h+manifest+payload+resources
    crc=crc_bytes(blob)
    blob=build_header(args.abi_major,args.abi_minor,ARCHES[args.arch],kind,0,mo,ms,po,ps,ro,rs,crc)+manifest+payload+resources
    pathlib.Path(args.output).write_bytes(blob)
    print(f'wrote {args.output}: {len(blob)} bytes arch={args.arch} crc32={crc:08x}')

def parse(path):
    data=pathlib.Path(path).read_bytes()
    if len(data)<HEADER_SIZE: raise ValueError('file too small')
    vals=HEADER.unpack_from(data)
    magic,fmt,hs,abi_maj,abi_min,arch,payload_kind,flags,mo,ms,po,ps,ro,rs,crc,_=vals
    if magic!=MAGIC: raise ValueError('bad magic')
    if fmt!=FORMAT_VERSION or hs!=HEADER_SIZE: raise ValueError('unsupported header')
    if arch not in ARCH_NAMES: raise ValueError(f'unsupported architecture id {arch}')
    expected_kind=expected_payload_kind_for_arch_id(arch)
    if payload_kind!=expected_kind:
        raise ValueError(f'payload kind {payload_kind} does not match {ARCH_NAMES[arch]}')
    sections=[(mo,ms,'manifest'),(po,ps,'payload'),(ro,rs,'resources')]
    previous_end=HEADER_SIZE
    for off,size,name in sections:
        if off<HEADER_SIZE or off>len(data) or size>len(data)-off:
            raise ValueError(f'{name} section out of bounds')
        if off<previous_end:
            raise ValueError(f'{name} section overlaps previous section')
        previous_end=off+size
    return data,dict(format=fmt,header_size=hs,abi_major=abi_maj,abi_minor=abi_min,arch=arch,payload_kind=payload_kind,flags=flags,manifest_offset=mo,manifest_size=ms,payload_offset=po,payload_size=ps,resources_offset=ro,resources_size=rs,crc32=crc)

def inspect(args):
    data,h=parse(args.file)
    kind={PAYLOAD_ELF64:'ELF64',PAYLOAD_ELF32:'ELF32'}.get(h['payload_kind'],str(h['payload_kind']))
    print('ODRV v1')
    print(f"ABI: {h['abi_major']}.{h['abi_minor']}")
    print(f"arch: {ARCH_NAMES[h['arch']]}")
    print(f"payload kind: {kind}")
    print(f"payload: offset={h['payload_offset']} size={h['payload_size']}")
    print(f"manifest: offset={h['manifest_offset']} size={h['manifest_size']}")
    print(f"resources: offset={h['resources_offset']} size={h['resources_size']}")
    print(f"crc32: {h['crc32']:08x}")
    if args.manifest:
        print(data[h['manifest_offset']:h['manifest_offset']+h['manifest_size']].decode('utf-8','replace'))

def check(args):
    data,h=parse(args.file)
    actual=crc_bytes(data)
    if actual!=h['crc32']:
        raise SystemExit(f'CRC mismatch: header={h["crc32"]:08x} actual={actual:08x}')
    payload=data[h['payload_offset']:h['payload_offset']+h['payload_size']]
    expected=1 if h['payload_kind']==PAYLOAD_ELF32 else 2 if h['payload_kind']==PAYLOAD_ELF64 else 0
    if not expected or elf_class(payload)!=expected:
        raise SystemExit('ELF payload class mismatch')
    print('ODRV check passed')

def new(args):
    root=pathlib.Path(args.name); root.mkdir(parents=True,exist_ok=False)
    driver_class=args.driver_class
    bus=args.bus
    selected=[]
    for arch in args.arch:
        if arch not in selected:selected.append(arch)
    arches=','.join(selected)
    (root/'driver.toml').write_text(
        '[driver]\nname="'+root.name+'"\nversion="0.1.0"\nodi_abi="1.1"\nclass="'+driver_class+'"\nbus="'+bus+'"\narchitectures="'+arches+'"\n',encoding='utf-8')
    class_macro='ODI_CLASS_'+driver_class.upper()
    arch_mask='|'.join('ODI_ARCH_BIT('+ODI_ARCH_MACROS[a]+')' for a in selected)
    source='''#include <odi.h>\n\nstatic int probe(const odi_kernel_api *api,const odi_device *dev){\n    (void)api; (void)dev; return ODI_ENODEV;\n}\n\nstatic const odi_driver_descriptor driver={\n    .struct_size=sizeof(odi_driver_descriptor), .abi_major=ODI_ABI_MAJOR, .abi_minor=ODI_ABI_MINOR,\n    .driver_class='''+class_macro+''', .name="'''+root.name+'''", .version="0.1.0",\n    .probe=probe, .required_kernel_capabilities=0,\n    .supported_architectures='''+arch_mask+'''\n};\nconst odi_driver_descriptor *odi_driver_entry(void){return &driver;}\n'''
    (root/'driver.c').write_text(source,encoding='utf-8')
    print('created',root)

def main():
    p=argparse.ArgumentParser(prog='odk'); sp=p.add_subparsers(dest='cmd',required=True)
    n=sp.add_parser('new'); n.add_argument('name'); n.add_argument('--class',dest='driver_class',choices=sorted(CLASSES),default='platform'); n.add_argument('--bus',choices=sorted(BUSES),default='platform'); n.add_argument('--arch',action='append',choices=ARCHES,default=[]); n.set_defaults(fn=lambda a:(setattr(a,'arch',a.arch or ['x86_64']),new(a))[1])
    b=sp.add_parser('pack'); b.add_argument('--manifest',required=True); b.add_argument('--payload',required=True); b.add_argument('--resources'); b.add_argument('-o','--output',required=True); b.add_argument('--arch',choices=ARCHES,default='x86_64'); b.add_argument('--abi-major',type=int,default=1); b.add_argument('--abi-minor',type=int,default=1); b.set_defaults(fn=pack)
    i=sp.add_parser('inspect'); i.add_argument('file'); i.add_argument('--manifest',action='store_true'); i.set_defaults(fn=inspect)
    c=sp.add_parser('check'); c.add_argument('file'); c.set_defaults(fn=check)
    a=p.parse_args(); a.fn(a)
if __name__=='__main__': main()
