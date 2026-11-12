# 68000 disassembly helper: tools/disasm68k.py <hex-addr> [count] [rom]. Needs `pip install capstone`.
import sys,capstone
d=open(sys.argv[3] if len(sys.argv)>3 else "game/mmww.bin","rb").read()
md=capstone.Cs(capstone.CS_ARCH_M68K,capstone.CS_MODE_BIG_ENDIAN|capstone.CS_MODE_M68K_000)
a=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 40
for i in md.disasm(d[a:a+n*10],a):
    print("%06x  %-8s %s"%(i.address,i.mnemonic,i.op_str)); n-=1
    if n<=0: break
