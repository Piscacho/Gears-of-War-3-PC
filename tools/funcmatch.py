"""Empareja funciones de Judgment (o de otro juego UE3 de 360) con las de Gears 3 por su patron de instrucciones.
Se enmascaran inmediatos de 16 bits (lis/addi/lwz...), desplazamientos de saltos y bl, y se compara el resto.
Uso: python tools/funcmatch.py [SUB_JUDGMENT ...]       -> imprime la direccion en GoW3 de cada una (o candidatos)
     python tools/funcmatch.py --all salida.txt          -> tabla completa de emparejamientos unicos
"""
import struct, sys, os, hashlib, collections
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
J = "G:/Gears of war Judgmanet pc/"
B = 0x82000000

def load(img_path, fn_path):
    img = open(img_path, "rb").read()
    fns = [(int(a, 16), int(s, 16)) for a, s in (l.split()[:2] for l in open(fn_path) if l.strip())]
    return img, fns

DFORM = set(range(7, 16)) | set(range(20, 30)) | set(range(32, 56)) | {2, 3, 58, 62}
def mask(x):
    op = x >> 26
    if op == 18: return op << 26 | (x & 3)
    if op == 16: return x & 0xFFFF0003
    if op in DFORM: return x & 0xFFFF0000
    return x

def sig(img, a, s):
    o = a - B
    ws = struct.unpack_from(">%dI" % (s // 4), img, o)
    return hashlib.sha1(struct.pack(">%dI" % len(ws), *map(mask, ws))).hexdigest()

def table(img, fns):
    d = collections.defaultdict(list)
    for a, s in fns:
        if s >= 16: d[(sig(img, a, s), s)].append(a)
    return d

def main():
    ji, jf = load(J + "xex/basefile.bin", J + "xex/functions_pdata.txt")
    gi, gf = load(os.path.join(root, "xex/basefile.bin"), os.path.join(root, "xex/functions_pdata.txt"))
    gt = table(gi, gf)
    jsz = dict(jf)
    if sys.argv[1:2] == ["--all"]:
        jt = table(ji, jf)
        out = open(sys.argv[2], "w"); n = 0
        for k, v in jt.items():
            if len(v) == 1 and len(gt.get(k, [])) == 1:
                out.write("%08X %08X %X\n" % (v[0], gt[k][0], k[1])); n += 1
        print(n, "funciones emparejadas de forma unica de", len(jf))
        return
    for s in sys.argv[1:]:
        a = int(s.replace("sub_", ""), 16)
        if a not in jsz: print("%08X: no esta en .pdata de Judgment" % a); continue
        c = gt.get((sig(ji, a, jsz[a]), jsz[a]), [])
        print("%08X (0x%X) -> %s" % (a, jsz[a], " ".join("%08X" % x for x in c) or "SIN PAREJA"))

main()
