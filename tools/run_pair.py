"""GoW3 sesion 5 (de Judgment s.9): dos instancias a la vez (enlace de sistema). python tools/run_pair.py logs/A.log logs/B.log segundos guionA guionB [VAR=valor ...]
A = anfitrion (127.0.0.1, carpeta run), B = invitado (127.0.0.2, carpeta run2, par 127.0.0.1)."""
import subprocess, sys, os, time
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
logA, logB, secs, scrA, scrB = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5]
extra = dict(a.split("=", 1) for a in sys.argv[6:])
exe = os.path.join(root, "project", "build", "GOW3.exe")
N = "Gears of War 3 (World) (En,Fr,Pt,Zh,Ko,Pl,Ru,Cs,Hu)"
iso = os.path.join(root, "juego", N + ".iso")
def start(log, cwd, script, net):
    env = dict(os.environ); env.update({"GOW3_INPUTSCRIPT": os.path.abspath(script)}); env.update(extra); env.update(net)
    return subprocess.Popen([exe, os.path.join(root, "xex", "basefile.bin"), os.path.join(root, "xex", "default.xex"), iso, "userdata"],
                            cwd=os.path.join(root, cwd), stdout=open(log, "w"), stderr=subprocess.STDOUT, env=env)
a = start(logA, "run", scrA, {"GOW3_NET": "crear", "GOW3_NET_IP": "127.0.0.1", "GOW3_GAMERTAG": "Anfitrion"})
time.sleep(3)
b = start(logB, "run2", scrB, {"GOW3_NET": "unirse", "GOW3_NET_IP": "127.0.0.2", "GOW3_NET_PEER": "127.0.0.1", "GOW3_GAMERTAG": "Invitado"})
t = time.time()
while time.time() - t < secs and (a.poll() is None or b.poll() is None): time.sleep(0.5)
for p in (a, b):
    if p.poll() is None: p.kill()
print("A rc", a.returncode, "B rc", b.returncode)
