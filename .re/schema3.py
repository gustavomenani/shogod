import pefile, struct
P=r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\schemasystem.dll"
pe=pefile.PE(P)
BASE=pe.OPTIONAL_HEADER.ImageBase
data=open(P,'rb').read()
secs=[(s.Name.rstrip(b'\x00').decode(), BASE+s.VirtualAddress, s.PointerToRawData, s.Misc_VirtualSize, s.SizeOfRawData) for s in pe.sections]
def va2off(va):
    for n,b,ptr,vsz,rsz in secs:
        if b<=va<b+max(vsz,rsz): return ptr+(va-b)
def q(va):
    o=va2off(va); return struct.unpack('<Q', data[o:o+8])[0], o
for addr in (0x1800715e0, 0x1800715e8, 0x1800715f8, 0x180071600, 0x1800715d8):
    try:
        v,o=q(addr); print(hex(addr),"file",hex(o),"qword",hex(v))
    except Exception as ex: print(addr, ex)
print("--- exports ---")
for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
    print(e.name, hex(BASE+e.address))
