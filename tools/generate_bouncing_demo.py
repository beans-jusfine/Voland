#!/usr/bin/env python3
"""Generate Voland's animated 96x54 VFB2 bouncing-square NRO."""
import struct
from pathlib import Path
W,H,S,N,INS=96,54,8,16,9000
T,HS=0x1000,20
def rgba(r,g,b): return (255<<24)|(b<<16)|(g<<8)|r
def movz(v): return 0x52800000|((v&65535)<<5)
def movk(v): return 0x72a00000|((v&65535)<<5)
def adrp(r,p):
 p&=0x1fffff; return 0x90000000|((p&3)<<29)|(((p>>2)&0x7ffff)<<5)|r
def add(d,n,i,s=False): return 0x91000000|(int(s)<<22)|((i&4095)<<10)|(n<<5)|d
def st(i,b): return 0xb9000000|((i&4095)<<10)|(b<<5)
pos=[];x,y,dx,dy=8,8,5,3
for _ in range(N):
 pos.append((x,y));x+=dx;y+=dy
 if x<=0 or x>=W-S: dx=-dx;x+=2*dx
 if y<=0 or y>=H-S: dy=-dy;y+=2*dy
data_off=(T+INS*4+0xfff)&~0xfff
code=[adrp(20,(data_off-T)//4096),add(20,20,0),add(1,20,HS),add(2,1,4,True)]
bg,color=rgba(10,14,27),rgba(0,235,255)
def pixel(i,c):
 code.extend((movz(c),movk(c>>16)));code.append(st(i-4096,2) if i>=4096 else st(i,1))
def square(p,c):
 for yy in range(S):
  for xx in range(S): pixel((p[1]+yy)*W+p[0]+xx,c)
square(pos[0],color);code.extend((movz(1),0xb9001280))
ldr=len(code);code.append(0xb9400000|(4<<10)|(20<<5)|9)
wait=len(code);code.append(0x35000000|(((ldr-wait)&0x7ffff)<<5)|9)
for i in range(1,N):
 square(pos[i-1],bg);square(pos[i],color);code.extend((movz(1),0xb9001280))
 b=len(code);code.append(0x14000000|((ldr-b)&0x3ffffff))
if len(code)>INS: raise RuntimeError(f"code overflow: {len(code)}")
# After the final frame's handshake, skip padding and restart at the LDR.
# Branching to the CBNZ itself would reuse stale W9 and eventually fall off.
code.extend([0xd503201f]*(INS-len(code)))
code[-1] = 0x14000000 | ((ldr-(INS-1)) & 0x3ffffff)
ds=HS+W*H*4;image=bytearray(data_off+ds)
def put(o,v): struct.pack_into("<I",image,o,v&0xffffffff)
put(0x10,0x304f524e);put(0x18,len(image));put(0x20,T);put(0x24,INS*4);put(0x30,data_off);put(0x34,ds)
put(data_off,0x32424656);put(data_off+4,W);put(data_off+8,H);put(data_off+12,1);put(data_off+16,0)
for yy in range(H):
 for xx in range(W): put(data_off+HS+(yy*W+xx)*4,rgba(22,30,48) if xx%12==0 or yy%12==0 else bg)
for i,v in enumerate(code): put(T+i*4,v)
Path("platform/web/public/homebrew/voland-bouncing.nro").write_bytes(image)
print(f"Wrote {len(image):,} bytes: {W}x{H} animated bouncing square.")
