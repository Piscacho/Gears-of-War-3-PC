"""GoW3 sesion 5: tres instancias (enlace de sistema). python tools/run_trio.py logs/A.log logs/B.log logs/C.log segundos guionA guionBC [VAR=valor ...]
A = carpeta run (crea el grupo), B = run2 y C = run3 (se unen con el mismo guion). Red como la de GH: se encuentran solos."""
import subprocess, sys, os, time
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
logs, secs, scrA, scrBC = sys.argv[1:4], float(sys.argv[4]), sys.argv[5], sys.argv[6]
extra = dict(a.split("=", 1) for a in sys.argv[7:])
exe = os.path.join(root, "project", "build", "GOW3.exe")
iso = os.path.join(root, "juego", "Gears of War 3 (World) (En,Fr,Pt,Zh,Ko,Pl,Ru,Cs,Hu).iso")
def start(log, cwd, script, name):
    env = dict(os.environ); env.update({"GOW3_INPUTSCRIPT": os.path.abspath(script), "GOW3_NET": "1", "GOW3_NET_NOINET": "1", "GOW3_GAMERTAG": name}); env.update(extra)
    return subprocess.Popen([exe, os.path.join(root, "xex", "basefile.bin"), os.path.join(root, "xex", "default.xex"), iso, "userdata"],
                            cwd=os.path.join(root, cwd), stdout=open(log, "w"), stderr=subprocess.STDOUT, env=env)
ps = [start(logs[0], "run", scrA, "Anfitrion")]
time.sleep(3); ps.append(start(logs[1], "run2", scrBC, "Invitado1"))
time.sleep(3); ps.append(start(logs[2], "run3", scrBC, "Invitado2"))
t = time.time()
while time.time() - t < secs and any(p.poll() is None for p in ps): time.sleep(0.5)
for p in ps:
    if p.poll() is None: p.kill()
print("rc", [p.returncode for p in ps])
