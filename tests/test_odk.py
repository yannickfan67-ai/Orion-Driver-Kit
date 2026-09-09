import os
import pathlib
import tempfile
import unittest
from types import SimpleNamespace

import odk


def fake_elf(cls):
    b=bytearray(128)
    b[:4]=b'\x7fELF'
    b[4]=cls
    return bytes(b)


class OdkTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.root=pathlib.Path(self.tmp.name)
        self.manifest=self.root/'driver.toml'
        self.manifest.write_text('[driver]\nname="test"\nversion="0.1.0"\nodi_abi="1.1"\n',encoding='utf-8')

    def tearDown(self):
        self.tmp.cleanup()

    def pack(self,arch):
        payload=self.root/(arch+'.elf')
        payload.write_bytes(fake_elf(1 if arch=='i686' else 2))
        out=self.root/(arch+'.odrv')
        odk.pack(SimpleNamespace(manifest=str(self.manifest),payload=str(payload),resources=None,output=str(out),arch=arch,abi_major=1,abi_minor=1))
        return out

    def test_all_architectures_roundtrip(self):
        for arch in odk.ARCHES:
            with self.subTest(arch=arch):
                out=self.pack(arch)
                data,h=odk.parse(out)
                self.assertEqual(odk.ARCH_NAMES[h['arch']],arch)
                self.assertEqual(h['payload_kind'],odk.payload_kind_for(arch))
                odk.check(SimpleNamespace(file=str(out)))
                self.assertEqual(odk.crc_bytes(data),h['crc32'])

    def test_rejects_arch_payload_kind_mismatch(self):
        out=self.pack('i686')
        data=bytearray(out.read_bytes())
        vals=list(odk.HEADER.unpack_from(data))
        vals[6]=odk.PAYLOAD_ELF64
        data[:odk.HEADER_SIZE]=odk.HEADER.pack(*vals)
        out.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'payload kind'):
            odk.parse(out)

    def test_rejects_overlapping_sections(self):
        out=self.pack('x86_64')
        data=bytearray(out.read_bytes())
        vals=list(odk.HEADER.unpack_from(data))
        vals[10]=vals[8]  # payload starts at manifest start
        data[:odk.HEADER_SIZE]=odk.HEADER.pack(*vals)
        out.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'overlaps'):
            odk.parse(out)

    def test_crc_corruption_is_detected(self):
        out=self.pack('x86_64')
        data=bytearray(out.read_bytes())
        data[-1]^=0x5a
        out.write_bytes(data)
        with self.assertRaisesRegex(SystemExit,'CRC mismatch'):
            odk.check(SimpleNamespace(file=str(out)))

    def test_new_uses_requested_architecture_mask(self):
        old=os.getcwd()
        os.chdir(self.root)
        try:
            args=SimpleNamespace(name='demo',driver_class='network',bus='pci',arch=['i686','aarch64','i686'])
            odk.new(args)
        finally:
            os.chdir(old)
        source=(self.root/'demo'/'driver.c').read_text(encoding='utf-8')
        manifest=(self.root/'demo'/'driver.toml').read_text(encoding='utf-8')
        self.assertIn('ODI_ARCH_BIT(ODI_ARCH_I686)|ODI_ARCH_BIT(ODI_ARCH_AARCH64)',source)
        self.assertNotIn('ODI_ARCH_X86_64',source)
        self.assertNotIn('ODI_ARCH_RISCV64',source)
        self.assertIn('architectures="i686,aarch64"',manifest)


if __name__=='__main__':
    unittest.main()
