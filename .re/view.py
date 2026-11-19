import pe, struct
vt=0x181c76ac8
slots=[struct.unpack('<Q', pe.data[pe.va2off(vt+i*8):pe.va2off(vt+i*8)+8])[0] for i in range(23)]
for i,f in enumerate(slots):
    if not (0x180000000<=f<0x181aa7000):
        print(i, hex(f), "non-text"); continue
    ins=pe.dis(f, 12)
    txt=" | ".join(f"{m} {o}" for a,m,o,b in ins[:6])
    print(f"{i:2d} {f:#x} {txt}")
