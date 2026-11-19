"""Ejecuta GOW3.exe en modo desarrollo durante N segundos y guarda el log. python tools/run_game.py logs/runN.log [segundos] [VAR=valor ...]"""
import subprocess, sys, os, time
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
log = sys.argv[1]; secs = float(sys.argv[2]) if len(sys.argv) > 2 else 30
env = dict(os.environ); [env.__setitem__(*a.split("=", 1)) for a in sys.argv[3:]]
exe = os.path.join(root, "project", "build", "GOW3.exe")
# Sesion 26: RUN_TU=1 -> ejecutable, imagen y XEX de la actualizacion del titulo #2
tu = env.get("RUN_TU") == "1"
if tu: exe = os.path.join(root, "project", "build_tu", "GOW3TU.exe")
N = "Gears of War 3 (World) (En,Fr,Pt,Zh,Ko,Pl,Ru,Cs,Hu)"
iso = os.path.join(root, "juego", N + ".iso")
with open(log, "w") as f:
    p = subprocess.Popen([exe, os.path.join(root, "xex", "tu_basefile.bin" if tu else "basefile.bin"), os.path.join(root, "tu", "tu_default.xex") if tu else os.path.join(root, "xex", "default.xex"), iso, env.get("RUN_DATA", "userdata")],
                         cwd=os.path.join(root, "run"), stdout=f, stderr=subprocess.STDOUT, env=env)
    t = time.time()
    while p.poll() is None and time.time() - t < secs: time.sleep(0.2)
    if p.poll() is None: p.kill(); print("matado tras %.0f s" % secs)
    else: print("salio con rc=%d tras %.1f s" % (p.returncode, time.time() - t))
lines = open(log, errors="replace").read().splitlines()
print(len(lines), "lineas")
