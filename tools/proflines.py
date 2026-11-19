"""Lineas de codigo mas calientes (marco superior) de un hilo en las lineas [prof] de un log.
Uso: python tools/proflines.py logs/runNN.log [filtro_hilo=C0C0C0C0] [n=40]"""
import subprocess, sys, os, collections
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
exe = os.path.join(root, "project", "build", "GOW3.exe")
sym = "G:/Guitar Hero Warrior of rock/Recomp/toolchain/clang+llvm-23.1.4-x86_64-pc-windows-msvc/bin/llvm-symbolizer.exe"
filt = sys.argv[2] if len(sys.argv) > 2 else "C0C0C0C0"; top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
cnt = collections.Counter(); n = 0
for l in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if l.startswith("[prof]") and filt in l:
        f = l.split()[2:]; n += 1
        if f and not f[0].startswith("m"): cnt[f[0]] += 1
addrs = list(cnt)
names = {}
for i in range(0, len(addrs), 300):
    ch = addrs[i:i+300]
    out = subprocess.run([sym, "--obj=" + exe, "--functions=short", "--no-inlines"] + ["0x" + a for a in ch], capture_output=True, text=True).stdout.split("\n\n")
    for a, b in zip(ch, out):
        ls = b.strip().split("\n"); names[a] = (ls[0] + " " + os.path.basename(ls[1]) if len(ls) > 1 else ls[0])
agg = collections.Counter()
for a, c in cnt.items(): agg[names.get(a, a)] += c
print(n, "muestras")
for k, c in agg.most_common(top): print("%5.1f%%  %s" % (100 * c / n, k))
