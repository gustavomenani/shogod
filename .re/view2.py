import pe,struct
# function start containing 0x180bfa14a
addr=0x180bfa14a
o=pe.va2off(addr)
start=o
while start>0 and pe.data[start-1:start+1]!=b'\xcc\xcc':
    start-=1
    if o-start>0x400: break
fstart=addr-(o-start)
print("ctor start maybe",hex(fstart))
# find callers (e8 rel32) to fstart
hits=[]
start_o=pe.TEXT[2]; end=start_o+pe.TEXT[4]
for i in range(start_o,end-5):
    if pe.data[i]==0xe8:
        rel=struct.unpack('<i',pe.data[i+1:i+5])[0]
        va=pe.off2va(i)
        if va and va+5+rel in (fstart, fstart+0x0a, 0x180bfa140):
            hits.append(va)
print("callers",[hex(h) for h in hits][:10])
for h in hits[:6]:
    print("=== caller",hex(h))
    for a,m,op,b in pe.dis(h-32, 24):
        print(f"  {a:#x} {m:9s} {op}")
