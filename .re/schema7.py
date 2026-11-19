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
TEXT=[s for s in secs if s[0]=='.text'][0]
targets=[0x1800572b9,0x180057339,0x1800573a9]  # error messages
for t in targets:
    refs=[]
    start,end=TEXT[2],TEXT[2]+TEXT[4]
    for k in range(start,end-7):
        if data[k] in (0x48,0x4c) and data[k+1] in (0x8d,0x8b):
            disp=struct.unpack('<i',data[k+3:k+7])[0]
            va=off2va(k)
            if va and va+7+disp==t: refs.append(va)
    print("msg",hex(t),"refs",[hex(r) for r in refs])
