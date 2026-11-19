import pefile, struct, capstone
P=r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\schemasystem.dll"
pe=pefile.PE(P); BASE=pe.OPTIONAL_HEADER.ImageBase; data=open(P,'rb').read()
secs=[(s.Name.rstrip(b'\x00').decode(), BASE+s.VirtualAddress, s.PointerToRawData, s.Misc_VirtualSize, s.SizeOfRawData) for s in pe.sections]
def off2va(o):
    for n,va,ptr,vsz,rsz in secs:
        if ptr<=o<ptr+rsz: return va+(o-ptr)
def va2off(va):
    for n,b,ptr,vsz,rsz in secs:
        if b<=va<b+max(vsz,rsz): return ptr+(va-b)
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail=True
def dis(va,n=30):
    o=va2off(va); out=[]
    for ins in md.disasm(data[o:o+n*16], va):
        out.append((ins.address,ins.mnemonic,ins.op_str))
        if len(out)>=n: break
    return out
# search text for lea/mov rip refs that resolve to the BSS slot
target=0x1800715e0
o_text=[s for s in secs if s[0]=='.text'][0]
start,end=o_text[2], o_text[2]+o_text[4]
hits=[]
for i in range(start,end-7):
    if data[i] in (0x48,0x4c) and data[i+1] in (0x8d,0x8b,0x89):
        disp=struct.unpack('<i',data[i+3:i+7])[0]
        va=off2va(i)
        if va and va+7+disp==target:
            hits.append(va)
print("refs to slot",len(hits),[hex(h) for h in hits][:10])
for h in hits[:6]:
    print("=== site",hex(h))
    for a,m,op in dis(h,20): print(f"  {a:#x} {m:10s} {op}")
