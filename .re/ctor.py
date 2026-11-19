import pe
for a,m,o,b in pe.dis(0x1809bc6f7, 90):
    print(f"{a:#x} {m:10s} {o}")
