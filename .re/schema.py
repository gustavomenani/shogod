import pefile, capstone, struct
P=r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\schemasystem.dll"
pe=pefile.PE(P, fast_load=True)
BASE=pe.OPTIONAL_HEADER.ImageBase
data=open(P,'rb').read()
secs=[(s.Name.rstrip(b'\x00').decode(), BASE+s.VirtualAddress, s.PointerToRawData, s.Misc_VirtualSize, s.SizeOfRawData) for s in pe.sections]
print("base",hex(BASE),"sections",[(n,hex(v)) for n,v,_,_,_ in secs])
def off2va(o):
    for n,va,ptr,vsz,rsz in secs:
        if ptr<=o<ptr+rsz: return va+(o-ptr)
def va2off(va):
    for n,b,ptr,vsz,rsz in secs:
        if b<=va<b+max(vsz,rsz): return ptr+(va-b)
i=data.find(b"SchemaSystem_001\x00")
print("SchemaSystem_001 file off",hex(i),"va",hex(off2va(i)) if i>=0 else None)

import struct as st
needle=st.pack('<Q',0x180056cb0)
j=0
while True:
    k=data.find(needle,j)
    if k<0: break
    j=k+1
    va=off2va(k)
    print("ref at file",hex(k),"va",hex(va))
    start=max(0,k-8*4)
    for m in range(k-8*4,k+8*4,8):
        if m<0 or m+8>len(data): continue
        q=st.unpack('<Q',data[m:m+8])[0]
        print(f"  {off2va(m):#x}: {q:#018x}")
# exports
for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
    if e.name and (b'CreateInterface' in e.name or b'Schema' in e.name):
        print("export",e.name,hex(BASE+e.address))
