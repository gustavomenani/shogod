import pe
for a,m,o,b in pe.dis(0x180149710, 120):
    print(f"{a:#x} {m:10s} {o}")
