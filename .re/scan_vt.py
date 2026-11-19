import pe, struct
base=0x181c29090
offs=pe.va2off(base)
slots=[struct.unpack('<Q', pe.data[offs+i*8:offs+i*8+8])[0] for i in range(23)]
for i,f in enumerate(slots):
    if not (0x180000000 <= f < 0x181aa7000): continue
    code=pe.dis(f, 120)
    txt=" ".join(f"{m} {o}" for a,m,o,b in code)
    hot=[k for k in ("0x7fff","0x200","0x1000","0x10]","shl","imul") if k in txt]
    if hot:
        print(f"slot {i} {f:#x} hot={hot}")
