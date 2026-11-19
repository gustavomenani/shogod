import pe, struct, capstone
G=0x18270c680
hits=[]; i=0; pat=b'\x48\x8b\x05'
while True:
    j=pe.data.find(pat,i)
    if j<0: break
    i=j+1
    va=pe.off2va(j)
    if va is None: continue
    disp=struct.unpack('<i',pe.data[j+3:j+7])[0]
    if va+7+disp==G: hits.append(va)
print("sites",len(hits))
for va in hits:
    ins=list(pe.dis(va,140))
    offs=[]
    for idx,(a,m,o,b) in enumerate(ins):
        if m=="mov" and o.startswith("qword ptr [r") and ("+ 0x" in o or o.endswith("]")):
            offs.append((a,m,o))
    show=[t for t in offs[:6]]
    print(hex(va), "|", " ; ".join(f"{o}" for _,_,o in show))
