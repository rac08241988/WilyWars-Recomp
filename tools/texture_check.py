"""Match a native stage frame against a reference map (VGMaps rips) and report
tile/colour agreement. Colours are compared as 3-bit Genesis channel indices
(ours: hardware DAC ladder; map: emulator linear ladder), so palette *choice*
errors show up while ladder differences don't."""
import sys, json, numpy as np, cv2
from PIL import Image
OURS = np.array([0,49,87,119,146,174,206,255])
def q_ours(a): return np.abs(a[...,None]-OURS).argmin(-1)
# VGMaps rips use their own ladder (~16,60,95,128,162,195,236,248); learned
# per channel from Cut/Elec/Guts start screens matched against our frames.
MAPLAD = np.array([16,60,95,128,162,195,236,248])
def q_map(a):  return np.abs(a[...,None]-MAPLAD).argmin(-1)
def run(frame, mapf, out):
    F=np.asarray(Image.open(frame).convert('RGB')).astype(np.int32)
    M=np.asarray(Image.open(mapf).convert('RGB')).astype(np.int32)
    Fq=q_ours(F); Mq=q_map(M)
    fi=(Fq[...,0]*64+Fq[...,1]*8+Fq[...,2]).astype(np.float32)
    mi=(Mq[...,0]*64+Mq[...,1]*8+Mq[...,2]).astype(np.float32)
    # template: lower 2/3 of the frame minus HUD columns; sprites are a minority
    ty0,ty1,tx0,tx1=40,216,24,232
    T=np.stack([Fq[ty0:ty1,tx0:tx1,c] for c in range(3)],-1).astype(np.float32)
    Mf=Mq.astype(np.float32)
    r=cv2.matchTemplate(Mf,T,cv2.TM_SQDIFF)
    _,_,loc,_=cv2.minMaxLoc(r)
    mx,my=loc[0]-tx0,loc[1]-ty0
    H,W=F.shape[:2]
    crop=np.zeros_like(F); cq=np.full_like(Fq,-1)
    sy0,sx0=max(0,-my),max(0,-mx); ey,ex=min(H,M.shape[0]-my),min(W,M.shape[1]-mx)
    crop[sy0:ey,sx0:ex]=M[my+sy0:my+ey,mx+sx0:mx+ex]; cq[sy0:ey,sx0:ex]=Mq[my+sy0:my+ey,mx+sx0:mx+ex]
    valid=(cq[...,0]>=0)&~((crop==255).all(-1))   # white = outside the ripped level
    same=(cq==Fq).all(-1)&valid
    near=(np.abs(cq-Fq)<=1).all(-1)&valid
    # 8x8 cells
    cells=bad=0; badmask=np.zeros((H//8,W//8),bool)
    for cy in range(H//8):
        for cx in range(W//8):
            v=valid[cy*8:cy*8+8,cx*8:cx*8+8]
            if v.sum()<48: continue
            cells+=1
            if (~near[cy*8:cy*8+8,cx*8:cx*8+8][v]).mean()>0.25: bad+=1; badmask[cy,cx]=True
    # sheet: ours | map | diff
    diff=np.zeros_like(F); diff[valid&~near]=(255,0,0); diff[valid&near&~same]=(255,200,0); diff[valid&same]=(F[valid&same]//3)
    S=np.concatenate([F,np.full((H,4,3),255),crop,np.full((H,4,3),255),diff],1).astype(np.uint8)
    Image.fromarray(S).resize((S.shape[1]*2,S.shape[0]*2),Image.NEAREST).save(out)
    res=dict(frame=frame,map=mapf,at=[int(mx),int(my)],compared_px=int(valid.sum()),
             exact_pct=round(100*same.sum()/max(1,valid.sum()),1),within1_pct=round(100*near.sum()/max(1,valid.sum()),1),
             cells=cells,bad_cells=bad)
    print(json.dumps(res)); return res
if __name__=='__main__': run(*sys.argv[1:4])
