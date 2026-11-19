"""Reescribe la lista functions de config/gow3.toml = config/gow3_gap_functions.txt + entradas manuales.
python -I tools/make_functions.py"""
import re, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANUAL = {
    0x82CCC710: 0x2CC,  # setjmp (GoW3)
    0x82CC9EA0: 0xA0,   # memset del CRT (Judgment 0x82D9F940): XenonRecomp la cortaba en el primer bdzlr- (colas sin escribir)
    # thunks de GMalloc tras el switch de la funcion de hueco 0x82D67E40 (Judgment 0x82E38080...)
    0x82D680A0: 0x20, 0x82D680C0: 0x30, 0x82D680F0: 0x28, 0x82D68118: 0x24, 0x82D68140: 0x1C,
    0x82CC81D0: 0xC, 0x82CCC090: 0xC,   # colas de funcion con 'b' a las que se llega por lis/addi (Judgment 0x82D9D930, 0x82DA1BE8)
}
cfg = os.path.join(root, "config", "gow3.toml")
gap = dict((int(a, 16), int(s, 16)) for a, s in re.findall(r"address = 0x(\w+), size = 0x(\w+)", open(os.path.join(root, "config", "gow3_gap_functions.txt")).read()))
gap.update(MANUAL)
# una funcion manual dentro de una de hueco estimada: la de hueco se recorta (comprobado que no deja fuera etiquetas de switch)
items = sorted(gap.items())
for i in range(len(items) - 1):
    (a, s_), (b, _) = items[i], items[i + 1]
    if a + s_ > b and b in MANUAL and a not in MANUAL: gap[a] = b - a
fs = sorted(gap.items())
bad = [(hex(a), hex(b)) for (a, s), (b, _) in zip(fs, fs[1:]) if a + s > b]
assert not bad, bad
t = open(cfg).read()
i = t.find("functions = [")
t = (t[:i] if i >= 0 else t.rstrip() + "\n") + "functions = [\n" + ",\n".join("{ address = 0x%08X, size = 0x%X }" % x for x in fs) + "\n]\n"
open(cfg, "w").write(t)
print(len(fs), "funciones")
