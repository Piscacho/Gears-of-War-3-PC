"""Lee / modifica Coalesced_*.bin de Gears 3 (UE3: int32 BE de archivos; por archivo: nombre, n secciones; seccion: nombre, n claves;
clave: nombre y valor). Cadenas: int32 BE (negativo = UTF-16LE con -n caracteres incluido el nulo; positivo = ANSI).
Uso: python tools/coalesced.py archivo.bin buscar <texto>"""
import struct, sys
def rd(d, o):
    n = struct.unpack_from(">i", d, o)[0]; o += 4
    if n < 0: s = d[o:o - 2 * n].decode("utf-16-le").rstrip("\0"); o += -2 * n
    else: s = d[o:o + n].decode("latin-1").rstrip("\0"); o += n
    return s, o
def parse(d):
    o = 0; nf = struct.unpack_from(">i", d, o)[0]; o += 4; out = []
    for _ in range(nf):
        fn, o = rd(d, o); ns = struct.unpack_from(">i", d, o)[0]; o += 4
        for _ in range(ns):
            sn, o = rd(d, o); nk = struct.unpack_from(">i", d, o)[0]; o += 4
            for _ in range(nk):
                ko = o; k, o = rd(d, o); vo = o; v, o = rd(d, o); out.append((fn, sn, k, v, ko, vo))
    return out
if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read(); e = parse(d)
    if sys.argv[2] == "buscar":
        for fn, sn, k, v, ko, vo in e:
            if sys.argv[3].lower() in (k + "=" + v + " " + sn).lower(): print("%s [%s] %s=%s  @%X" % (fn, sn, k, v, vo))
