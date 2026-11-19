import pe,sys
a=int(sys.argv[1],16)
n=int(sys.argv[2]) if len(sys.argv)>2 else 80
for x,m,o,b in pe.dis(a,n):
    print(f"{x:#x} {m:10s} {o}")
