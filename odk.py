#!/usr/bin/env python3
import argparse, pathlib, struct, sys, zlib

MAGIC=b'ODRV'
FORMAT_VERSION=1
HEADER_SIZE=128
ARCHES={'x86_64':1,'aarch64':2}
PAYLOAD_ELF64=1
HEADER=struct.Struct('<4sHHHHHHIIIIIIII80s')
CRC_OFF=44

def parse_manifest(path):
    text=pathlib.Path(path).read_text(encoding='utf-8')
    return text.encode('utf-8')

def build_header(abi_major,abi_minor,arch,flags,mo,ms,po,ps,ro,rs,crc):
    return HEADER.pack(MAGIC,FORMAT_VERSION,HEADER_SIZE,abi_major,abi_minor,arch,PAYLOAD_ELF64,flags,mo,ms,po,ps,ro,rs,crc,b'\0'*80)

def crc_bytes(blob):
    b=bytearray(blob)
    b[CRC_OFF:CRC_OFF+4]=b'\0\0\0\0'
    return zlib.crc32(b)&0xffffffff

def pack(args):
    manifest=parse_manifest(args.manifest)
    payload=pathlib.Path(args.payload).read_bytes()
    if not payload.startswith(b'\x7fELF'):
        raise SystemExit('payload is not ELF')
    resources=pathlib.Path(args.resources).read_bytes() if args.resources else b''
    mo=HEADER_SIZE; ms=len(manifest); po=mo+ms; ps=len(payload); ro=po+ps; rs=len(resources)
    h=build_header(args.abi_major,args.abi_minor,ARCHES[args.arch],0,mo,ms,po,ps,ro,rs,0)
    blob=h+manifest+payload+resources
    crc=crc_bytes(blob)
    blob=build_header(args.abi_major,args.abi_minor,ARCHES[args.arch],0,mo,ms,po,ps,ro,rs,crc)+manifest+payload+resources
    pathlib.Path(args.output).write_bytes(blob)
    print(f'wrote {args.output}: {len(blob)} bytes crc32={crc:08x}')

def parse(path):
    data=pathlib.Path(path).read_bytes()
    if len(data)<HEADER_SIZE: raise ValueError('file too small')
    vals=HEADER.unpack_from(data)
    magic,fmt,hs,abi_maj,abi_min,arch,payload_kind,flags,mo,ms,po,ps,ro,rs,crc,_=vals
    if magic!=MAGIC: raise ValueError('bad magic')
    if fmt!=FORMAT_VERSION or hs!=HEADER_SIZE: raise ValueError('unsupported header')
    for off,size,name in [(mo,ms,'manifest'),(po,ps,'payload'),(ro,rs,'resources')]:
        if off<HEADER_SIZE or off+size>len(data): raise ValueError(f'{name} section out of bounds')
    return data,dict(format=fmt,header_size=hs,abi_major=abi_maj,abi_minor=abi_min,arch=arch,payload_kind=payload_kind,flags=flags,manifest_offset=mo,manifest_size=ms,payload_offset=po,payload_size=ps,resources_offset=ro,resources_size=rs,crc32=crc)

def inspect(args):
    data,h=parse(args.file)
    names={1:'x86_64',2:'aarch64'}
    print('ODRV v1')
    print(f"ABI: {h['abi_major']}.{h['abi_minor']}")
    print(f"arch: {names.get(h['arch'],h['arch'])}")
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
    if h['payload_kind']==PAYLOAD_ELF64 and not payload.startswith(b'\x7fELF'):
        raise SystemExit('ELF64 payload marker missing')
    print('ODRV check passed')

def new(args):
    root=pathlib.Path(args.name); root.mkdir(parents=True,exist_ok=False)
    (root/'driver.toml').write_text('[driver]\nname="'+root.name+'"\nversion="0.1.0"\nodi_abi="1.0"\nclass="platform"\n',encoding='utf-8')
    (root/'driver.c').write_text('#include <odi.h>\n\nstatic int probe(const odi_kernel_api *api,const odi_device *dev){(void)api;(void)dev;return ODI_ENODEV;}\n\nstatic const odi_driver_descriptor driver={sizeof(driver),ODI_ABI_MAJOR,ODI_ABI_MINOR,ODI_CLASS_PLATFORM,0,"'+root.name+'","0.1.0",probe,0,0,0,0};\nconst odi_driver_descriptor *odi_driver_entry(void){return &driver;}\n',encoding='utf-8')
    print('created',root)

def main():
    p=argparse.ArgumentParser(prog='odk'); sp=p.add_subparsers(dest='cmd',required=True)
    n=sp.add_parser('new'); n.add_argument('name'); n.set_defaults(fn=new)
    b=sp.add_parser('pack'); b.add_argument('--manifest',required=True); b.add_argument('--payload',required=True); b.add_argument('--resources'); b.add_argument('-o','--output',required=True); b.add_argument('--arch',choices=ARCHES,default='x86_64'); b.add_argument('--abi-major',type=int,default=1); b.add_argument('--abi-minor',type=int,default=0); b.set_defaults(fn=pack)
    i=sp.add_parser('inspect'); i.add_argument('file'); i.add_argument('--manifest',action='store_true'); i.set_defaults(fn=inspect)
    c=sp.add_parser('check'); c.add_argument('file'); c.set_defaults(fn=check)
    a=p.parse_args(); a.fn(a)
if __name__=='__main__': main()
