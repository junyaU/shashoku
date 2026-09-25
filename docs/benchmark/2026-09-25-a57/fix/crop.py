import zlib,struct,sys
def readpng(p):
    d=open(p,'rb').read(); pos=8; idat=b''; w=h=ct=bd=None
    while pos<len(d):
        ln=struct.unpack('>I',d[pos:pos+4])[0]; typ=d[pos+4:pos+8]; data=d[pos+8:pos+8+ln]
        if typ==b'IHDR': w,h,bd,ct=struct.unpack('>IIBB',data[:10])
        if typ==b'IDAT': idat+=data
        pos+=12+ln
    raw=zlib.decompress(idat)
    bpp={0:1,2:3,4:2,6:4}[ct]
    stride=w*bpp+1
    prev=bytearray(w*bpp); rows=[]
    for y in range(h):
        f=raw[y*stride]; line=bytearray(raw[y*stride+1:(y+1)*stride])
        if f==1:
            for i in range(bpp,len(line)): line[i]=(line[i]+line[i-bpp])&255
        elif f==2:
            for i in range(len(line)): line[i]=(line[i]+prev[i])&255
        elif f==3:
            for i in range(len(line)):
                a=line[i-bpp] if i>=bpp else 0
                line[i]=(line[i]+(a+prev[i])//2)&255
        elif f==4:
            for i in range(len(line)):
                a=line[i-bpp] if i>=bpp else 0; b=prev[i]; c=prev[i-bpp] if i>=bpp else 0
                p=a+b-c; pa=abs(p-a); pb=abs(p-b); pc=abs(p-c)
                pr=a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[i]=(line[i]+pr)&255
        prev=line; rows.append(bytes(line))
    return w,h,bpp,rows
def writepng(path,w,h,rgb):
    raw=b''.join(b'\x00'+bytes(r) for r in rgb)
    def chunk(t,d):
        c=struct.pack('>I',len(d))+t+d
        return c+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    out=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,6))+chunk(b'IEND',b'')
    open(path,'wb').write(out)
if __name__=='__main__':
    src,dst,x0,y0,cw,ch=sys.argv[1],sys.argv[2],*map(int,sys.argv[3:7])
    zoom=int(sys.argv[7]) if len(sys.argv)>7 else 1
    w,h,bpp,rows=readpng(src)
    cw=min(cw,w-x0); ch=min(ch,h-y0)
    out=[]
    for y in range(y0,y0+ch):
        line=bytearray()
        for x in range(x0,x0+cw):
            px=rows[y][x*bpp:x*bpp+3]
            line.extend(px*zoom)
        for _ in range(zoom): out.append(bytes(line))
    writepng(dst,cw*zoom,ch*zoom,out)
    print('wrote',dst,cw*zoom,ch*zoom)
