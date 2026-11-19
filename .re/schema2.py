import pefile, capstone, struct
P=r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\schemasystem.dll"
pe=pefile.PE(P)
BASE=pe.OPTIONAL_HEADER.ImageBase
data=open(P,'rb').read()
secs=[(s.Name.rstrip(b'\x00').decode(), BASE+s.VirtualAddress, s.PointerToRawData, s.Misc_VirtualSize, s.SizeOfRawData) for s in pe.sections]
def va2off(va):
    for n,b,ptr,vsz,rsz in secs:
        if b<=va<b+max(vsz,rsz): return ptr+(va-b)
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail=True
def dis(va,n=40):
    o=va2off(va); out=[]
    for ins in md.disasm(data[o:o+n*16], va):
        out.append((ins.address,ins.mnemonic,ins.op_str))
        if len(out)>=n: break
    return out
print("=== factory 0x1800715e0")
for a,m,o in dis(0x1800715e0,40): print(f"{a:#x} {m:10s} {o}")
