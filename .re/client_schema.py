import pe, struct
import pefile, capstone
P=r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
# find string "client.dll"
i=0; hits=[]
while True:
    j=pe.data.find(b"client.dll\x00",i)
    if j<0: break
    i=j+1; va=pe.off2va(j); hits.append((va,j))
print("client.dll strings", [(hex(a)) for a,_ in hits][:10])
# refs to each
for sva,_ in hits[:6]:
    needle=struct.pack('<I', sva & 0xFFFFFFFF)  # not used for rip; scan lea/mov with disp
    refs=[]
    start=TEXT_PTR=pe.TEXT[2]; end=TEXT_PTR+pe.TEXT[4]
    for k in range(start,end-7):
        if pe.data[k] in (0x48,0x4c) and pe.data[k+1] in (0x8d,0x8b,0x89):
            disp=struct.unpack('<i',pe.data[k+3:k+7])[0]
            va=pe.off2va(k)
            if va and va+7+disp==sva: refs.append(va)
    if refs:
        print("string",hex(sva),"refs",len(refs),[hex(r) for r in refs][:6])
