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
def cstr(va):
    o=va2off(va); 
    if o is None: return None
    e=data.find(b'\0',o); return data[o:e].decode('latin1')
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail=True
def dis(va,n=25):
    o=va2off(va); out=[]
    for ins in md.disasm(data[o:o+n*16], va):
        out.append((ins.address,ins.mnemonic,ins.op_str))
        if len(out)>=n: break
    return out
# strings near schema-ish names
for term in (b"SchemaSystem", b"TypeScope", b"DeclaredClass", b"FindTypeScope"):
    i=0; cnt=0
    while cnt<8:
        j=data.find(term,i)
        if j<0: break
        i=j+1; va=off2va(j)
        print(term, hex(va), cstr(va)[:60] if cstr(va) else None); cnt+=1
# print sites mapping vfuncs
for site in (0x180027062, 0x1800299cc, 0x180029a90, 0x180029ac0, 0x180029b27, 0x180029f7d, 0x1800276f8, 0x18002770b):
    print("=== site",hex(site))
    for a,m,o in dis(site,14): print(f"  {a:#x} {m:10s} {o}")
