"""Sesion 26: genera config/gow3_tu.toml (XenonRecomp para la actualizacion del titulo #2) a partir de config/gow3.toml.
Funciones sin .pdata = las del original trasladadas (config/gow3_tu_functions.txt, tools/tu_functions.py) + las manuales a las
que se llega por punteros (tu/datamap.txt) + huecos con switch de la TU (config/gow3_tu_gap_functions.txt, si existe).
Uso: python -I tools/make_tu_toml.py"""
import os, re
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
cfg = open(os.path.join(root, "config", "gow3.toml")).read()
head = cfg[:cfg.find("functions = [")]
rep = {
    'file_path = "../xex/default.xex"': 'file_path = "../tu/tu_default.xex"',
    'out_directory_path = "../ppc"': 'out_directory_path = "../ppc_tu"',
    'switch_table_file_path = "gow3_switch_tables_all.toml"': 'switch_table_file_path = "gow3_tu_switch_tables_fixed.toml"',
    "restgprlr_14_address = 0x82CC9910": "restgprlr_14_address = 0x82CDD080",
    "savegprlr_14_address = 0x82CC98C0": "savegprlr_14_address = 0x82CDD030",
    "restfpr_14_address   = 0x82CCA1CC": "restfpr_14_address   = 0x82CDD93C",
    "savefpr_14_address   = 0x82CCA180": "savefpr_14_address   = 0x82CDD8F0",
    "restvmx_14_address   = 0x82CCA518": "restvmx_14_address   = 0x82CDDC88",
    "savevmx_14_address   = 0x82CCA280": "savevmx_14_address   = 0x82CDD9F0",
    "restvmx_64_address   = 0x82CCA5AC": "restvmx_64_address   = 0x82CDDD1C",
    "savevmx_64_address   = 0x82CCA314": "savevmx_64_address   = 0x82CDDA84",
    "longjmp_address = 0x82CCC3F0": "longjmp_address = 0x82CDFB60",
    "setjmp_address  = 0x82CCC710": "setjmp_address  = 0x82CDFE80",
}
for a, b in rep.items():
    assert a in head, a
    head = head.replace(a, b)
head = head.replace("# XenonRecomp - Gears of War 3 (X360, 4D5308AB, disco v0xB)", "# XenonRecomp - Gears of War 3 TU v0x60B (generado por tools/make_tu_toml.py; no editar a mano)", 1)
fs = {}
for a, s in re.findall(r"address = 0x(\w+), size = 0x(\w+)", open(os.path.join(root, "config", "gow3_tu_functions.txt")).read()):
    fs[int(a, 16)] = int(s, 16)
# hojas por punteros (sesion 4 del original) via el mapa de datos
MANUAL_PTR = {}   # GoW3: las manuales se trasladan por contenido (tu_functions.py)
dm = {int(a, 16): int(b, 16) for a, b, n in (l.split() for l in open(os.path.join(root, "tu", "datamap.txt")))}
manual = set()
for a, s in MANUAL_PTR.items():
    t = dm[a]; fs[t] = s; manual.add(t)
gp = os.path.join(root, "config", "gow3_tu_gap_functions.txt")
if os.path.exists(gp):
    for a, s in re.findall(r"address = 0x(\w+), size = 0x(\w+)", open(gp).read()):
        a = int(a, 16); s = int(s, 16)
        if a not in fs: fs[a] = s
items = sorted(fs.items())
for i in range(len(items) - 1):   # solapes: se recorta la anterior
    (a, s), (b, _) = items[i], items[i + 1]
    if a + s > b: fs[a] = b - a
items = sorted(fs.items())
out = head + "functions = [\n" + ",\n".join("{ address = 0x%08X, size = 0x%X }" % x for x in items) + "\n]\n"
open(os.path.join(root, "config", "gow3_tu.toml"), "w").write(out)
print(len(items), "funciones")
