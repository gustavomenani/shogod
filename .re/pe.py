import pefile, capstone, struct
P=r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
pe=pefile.PE(P, fast_load=True)
BASE=0x180000000
secs=[]
data=open(P,'rb').read()
for s in pe.sections:
    name=s.Name.rstrip(b'\x00').decode()
    secs.append((name, BASE+s.VirtualAddress, s.PointerToRawData, s.Misc_VirtualSize, s.SizeOfRawData))
TEXT=[s for s in secs if s[0]=='.text'][0]
def off2va(o):
    for n,va,ptr,vsz,rsz in secs:
        if ptr<=o<ptr+rsz: return va+(o-ptr)
    return None
def va2off(va):
    for n,b,ptr,vsz,rsz in secs:
        if b<=va<b+max(vsz,rsz): return ptr+(va-b)
    return None
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail=True
def dis(va,n=60):
    o=va2off(va)
    out=[]
    for ins in md.disasm(data[o:o+n*16], va):
        out.append((ins.address, ins.mnemonic, ins.op_str, ins.bytes.hex()))
        if len(out)>=n: break
    return out
