import pe, capstone, struct
G=0x18270c680
# find all 48 8B / 48 8D / 4C 8B refs to G
hits=[]
pat=b'\x48\x8b\x05'
i=0
while True:
    j=pe.data.find(pat, i)
    if j<0: break
    i=j+1
    va=pe.off2va(j)
    if va is None: continue
    disp=struct.unpack('<i', pe.data[j+3:j+7])[0]
    tgt=va+7+disp
    if tgt==G: hits.append(va)
print("refs:",len(hits))
# print first 8 refs with context
for va in hits[:8]:
    print("=== site",hex(va))
    for a,m,o,b in pe.dis(va, 25):
        print(f"  {a:#x} {m:10s} {o}")
