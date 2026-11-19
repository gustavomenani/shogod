import pe
addrs=[0x1809cb620,0x1809cb6a0,0x1809cb160,0x1809cb050,0x1809cb0d0,0x1809cb800,0x1809ec880,0x1809f1c80,0x1809c32f0]
for a in addrs:
    print("=== vfunc",hex(a))
    for x,m,o,b in pe.dis(a, 18):
        print(f"  {x:#x} {m:10s} {o}")
