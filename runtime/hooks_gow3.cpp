// Ganchos de diagnostico sobre funciones del juego (GoW3). Las funciones traducidas son "weak": definirlas aqui las envuelve.
// GOW3_HOOKLOG=1 registra cada llamada (con hilo, r3..r5 y lr) de las funciones de la lista.
#include "runtime.h"
#include <ppc_recomp_shared.h>
#include "tu_remap.h"   // sesion 26: nombres de la actualizacion del titulo
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <string>

namespace
{
const bool g_hookLog = getenv("GOW3_HOOKLOG") != nullptr;
void HookLog(const char* name, PPCContext& ctx, std::atomic<int>& n)
{
    if (!g_hookLog) return;
    int k = n++;
    if (k < 20 || (k % 1000) == 0)
        LOG("[hook] %s #%d tid %lu r3=%08X r4=%08X r5=%08X lr=%08X", name, k, GetCurrentThreadId(), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, (uint32_t)ctx.lr);
}
}

#define GOW3_TRACE_HOOK(addr, label) \
    PPC_FUNC_IMPL(__imp__sub_##addr); \
    PPC_FUNC(sub_##addr) { static std::atomic<int> n{ 0 }; HookLog(label, ctx, n); __imp__sub_##addr(ctx, base); }

// Gancho que registra el valor devuelto (r3) cada vez que cambia
#define GOW3_RET_HOOK(addr, label)     PPC_FUNC_IMPL(__imp__sub_##addr);     PPC_FUNC(sub_##addr) { static std::atomic<uint64_t> last{ ~0ull }; uint32_t a3 = ctx.r3.u32; __imp__sub_##addr(ctx, base);         if (g_hookLog && last.exchange(ctx.r3.u64) != ctx.r3.u64) LOG("[ret] %s(r3=%08X) -> %llX (t=%lu ms)", label, a3, (unsigned long long)ctx.r3.u64, GetTickCount()); }
// GoW3 sesion 2: los ganchos de Judgment estan en hooks_gowj_ref.txt (direcciones de Judgment). Para portar uno:
// python tools/funcmatch.py <sub de Judgment> da la funcion equivalente de GoW3.

// Manejador de error fatal de UE3 ("appError called: " + mensaje en r4, cadena ancha). Judgment sub_82321758 = GoW3 sub_82319958.
static std::string GuestWide(uint32_t p, size_t max = 2000)
{
    std::string s; if (!p) return s;
    for (size_t i = 0; i < max; i++) { uint16_t c = __builtin_bswap16(*(uint16_t*)(g_base + p + i * 2)); if (!c) break; s += c < 128 ? (char)c : '?'; }
    return s;
}
PPC_FUNC_IMPL(__imp__sub_82319958);
PPC_FUNC(sub_82319958)
{
    LOG("[appError] %s | %s (lr=%08X, tid %lu)", GuestWide(ctx.r4.u32).c_str(), GuestWide(ctx.r5.u32, 200).c_str(), (uint32_t)ctx.lr, GetCurrentThreadId());
    __imp__sub_82319958(ctx, base);
}

// GoW3 sesion 2: nombres de UE3. GNames (TArray de FNameEntry*) en *(0x8343D148) (FName::ToString = sub_82303460).
// FNameEntry: +0 flags (bit 0 = unicode), +8 texto. UObject: +0x2C FName (indice, numero), +0x34 clase, +0x28 Outer.
std::string UeName(uint32_t index)
{
    uint32_t arr = Load32(GOW3ADDR(0x8343D148u, 0x8344D918u));
    if (!arr || index > 2000000) return "?";
    uint32_t e = Load32(arr + index * 4);
    if (!e) return "?";
    std::string s; bool wide = Load32(e) & 1;
    for (int i = 0; i < 100; i++)
    {
        uint32_t c = wide ? __builtin_bswap16(*(uint16_t*)(g_base + e + 8 + i * 2)) : g_base[e + 8 + i];
        if (!c) break; s += c < 128 ? (char)c : '?';
    }
    return s;
}
std::string UeObjName(uint32_t obj) { return obj ? UeName(Load32(obj + 0x2C)) : "None"; }
std::string UeClassName(uint32_t obj) { return obj && Load32(obj + 0x34) ? UeName(Load32(Load32(obj + 0x34) + 0x2C)) : "None"; }
static bool GuestPtr(uint32_t p) { return p >= 0x40000000 && p < 0x7F000000 || (p >= 0x82000000 && p < kImageEnd) || (p >= 0xA0000000 && p < 0xC0000000) || (p >= 0x00100000 && p < 0x3FFF0000); }
// Estado actual de un objeto de UnrealScript: StateFrame en +0x18; se busca en el marco un puntero a un UState
std::string UeStateName(uint32_t obj)
{
    static int dumps = 0;
    if (obj && dumps < 2) { dumps++; char b[400]; int c = 0; for (int k = 0; k < 0x40; k += 4) c += snprintf(b + c, sizeof(b) - c, " %08X", Load32(obj + k)); LOG("[state] obj %08X:%s", obj, b); }
    static const uint32_t off = getenv("GOW3_SFOFF") ? strtoul(getenv("GOW3_SFOFF"), nullptr, 16) : 0x14;
    uint32_t sf = obj ? Load32(obj + off) : 0;
    if (!GuestPtr(sf)) return "-";
    for (uint32_t k = 0; k < 0x40; k += 4)
    {
        uint32_t s = Load32(sf + k);
        if (!GuestPtr(s) || !GuestPtr(Load32(s + 0x34))) continue;
        if (UeClassName(s) == "State") return UeObjName(s) + "@" + std::to_string(k);
    }
    return "?";
}

// UObject::ProcessEvent (sub_822DFBC8: r3 = objeto, r4 = UFunction, r5 = parametros). GOW3_EVENTLOG=<texto> registra los eventos
// cuyo "Clase.Funcion" contiene el texto (las 30 primeras llamadas de cada uno); GOW3_EVENTLOG=* registra cada nombre nuevo una vez.
#include <mutex>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <map>
static thread_local int g_callTrace = 0;
bool InputScriptWantsEvent();
void InputScriptNotifyOpen(const std::string& path);
PPC_FUNC_IMPL(__imp__sub_822DFBC8);
// GoW3 sesion 5: menu principal activo (GameInfo GearPartyGame) o partida (GearGame*): el resolve 16_16_16_16 -> float lo usa
std::atomic<bool> g_frontendMenu{ true };
static void TrackGameInfo(uint32_t obj)
{
    static uint32_t lastObj = 0; static int lastKind = -1;
    if (obj == lastObj) { if (lastKind >= 0) g_frontendMenu = lastKind == 1; return; }
    // solo se mira la clase de objetos nuevos (cache pequena de los ultimos vistos)
    static std::unordered_map<uint32_t, int8_t> cache;
    auto it = cache.find(obj);
    int kind;
    if (it != cache.end()) kind = it->second;
    else
    {
        std::string c = UeClassName(obj);
        kind = c.rfind("GearPartyGame", 0) == 0 ? 1 : -1;
        static const char* const modes[] = { "GearGameSP", "GearGameHorde", "GearGameBeast", "GearGameMP", "GearGameTDM", "GearGameKOTH",
            "GearGameAnnex", "GearGameCTM", "GearGameWingman", "GearGameExecution", "GearGameGuardian", "GearGameArcade" };
        if (kind < 0) for (const char* m : modes) if (c.rfind(m, 0) == 0) { kind = 0; break; }
        if (cache.size() > 4096) cache.clear();
        cache[obj] = (int8_t)kind;
    }
    lastObj = obj; lastKind = kind;
    if (kind >= 0) g_frontendMenu = kind == 1;
}
// GoW3 sesion 5 (diagnostico): GOW3_ACTORDUMP=1 vuelca como floats los primeros 0x300 bytes de la bandera de Horda y del pawn
// del jugador (para localizar Actor.Location); GOW3_TPFLAG=<offset hex> teletransporta el pawn junto a la bandera (LB+RB+Y)
std::atomic<uint32_t> g_dbgFlag{ 0 }, g_dbgPawn{ 0 }, g_dbgObjective{ 0 };
static void DebugActors(uint32_t obj)
{
    static const bool dump = getenv("GOW3_ACTORDUMP") != nullptr;
    static const uint32_t tpOff = getenv("GOW3_TPFLAG") ? (uint32_t)strtoul(getenv("GOW3_TPFLAG"), nullptr, 16) : 0;
    if (!dump && !tpOff) return;
    static uint32_t last = 0; if (obj == last) return; last = obj;
    static std::unordered_map<uint32_t, int8_t> kinds;
    auto it = kinds.find(obj); int k;
    if (it != kinds.end()) k = it->second;
    else { std::string c = UeClassName(obj); k = c == "GearFortificationMarker" ? 1 : c.rfind("GearPawn_COGMarcus", 0) == 0 ? 2 : 0; if (kinds.size() > 8192) kinds.clear(); kinds[obj] = (int8_t)k; }
    static std::vector<uint32_t> markers;
    if (k == 1) { g_dbgFlag = obj; if (std::find(markers.begin(), markers.end(), obj) == markers.end() && markers.size() < 64) markers.push_back(obj); }
    else if (k == 2) g_dbgPawn = obj;
    if (tpOff && g_dbgPawn && !markers.empty())
    {   // GOW3_TPFLAG=<offset>[:<inicio s>:<paso s>]: recorre los marcadores (posicion = 3 floats en offset)
        static DWORD tStart = GetTickCount();
        static const char* e = getenv("GOW3_TPFLAG"); static double st0 = 20, step = 3;
        static int fixedIdx = -1;
        static bool parsed = false; if (!parsed) { parsed = true; const char* c = strchr(e, ':'); if (c) { st0 = atof(c + 1); c = strchr(c + 1, ':'); if (c) { step = atof(c + 1); c = strchr(c + 1, ':'); if (c) fixedIdx = atoi(c + 1); } } }
        double t = (GetTickCount() - tStart) / 1000.0 - st0;
        static int lastIdx = -1;
        if (t >= 0)
        {
            int idx = (int)(t / step);
            if (fixedIdx >= 0)
            {   // un solo marcador: se queda ahi; a los 3 s registra draws + captura (una vez)
                static bool logged = false;
                if (!logged && t > 3) { logged = true; extern std::atomic<int> g_drawLogRequest; g_drawLogRequest = 1; extern std::atomic<int> g_dumpRequest; g_dumpRequest = 6000 + fixedIdx; }
                idx = fixedIdx < (int)markers.size() ? fixedIdx : 0;
            }
            if (idx < (int)markers.size())
            {
                uint32_t m = markers[idx], pw = g_dbgPawn;
                for (int c = 0; c < 3; c++) { uint32_t v = Load32(m + tpOff + c * 4); if (c == 2) { float f; memcpy(&f, &v, 4); f += 60.0f; memcpy(&v, &f, 4); } Store32(pw + tpOff + c * 4, v); }
                static int dumped = -1;
                if (t - idx * step > step * 0.6 && dumped != idx) { dumped = idx; extern std::atomic<int> g_dumpRequest; g_dumpRequest = 5000 + idx; }
                if (idx != lastIdx) { lastIdx = idx; float f[3]; for (int c = 0; c < 3; c++) { uint32_t v = Load32(m + tpOff + c * 4); memcpy(&f[c], &v, 4); }
                    LOG("[tp] marcador %d/%zu %08X (%s) -> %.0f %.0f %.0f", idx, markers.size(), m, UeClassName(m).c_str(), f[0], f[1], f[2]); }
            }
        }
    }
    static DWORD t0 = 0; static bool done = false;
    if (dump && !done && g_dbgFlag && g_dbgPawn) { if (!t0) t0 = GetTickCount(); if (GetTickCount() - t0 > 3000) { done = true;
        for (uint32_t o : { g_dbgFlag.load(), g_dbgPawn.load() }) { std::string line;
            for (uint32_t off = 0x40; off < 0x300; off += 4) { uint32_t v = Load32(o + off); float f; memcpy(&f, &v, 4);
                if (std::fabs(f) > 20.0f && std::fabs(f) < 200000.0f) { char b[40]; snprintf(b, sizeof(b), " %X=%.1f", off, f); line += b; } }
            LOG("[actor] %s %08X:%s", UeClassName(o).c_str(), o, line.c_str()); } } }
}
// GoW3 sesion 5 (diagnostico): GOW3_FLAGPOS=1 registra la posicion (Actor.Location = +0x54) de los actores de bandera/base
// y la del jugador; GOW3_TPPOS=x,y,z[:seg] lleva y mantiene al jugador en ese punto desde <seg> s tras verlo (captura a los 3 s).
static void DebugFlagPos(uint32_t obj)
{
    static const bool flagPos = getenv("GOW3_FLAGPOS") != nullptr;
    static const char* tp = getenv("GOW3_TPPOS");
    if (!flagPos && !tp) return;
    static uint32_t last = 0; if (obj == last) return; last = obj;
    static std::unordered_map<uint32_t, int8_t> kinds; static uint32_t pawn = 0;
    auto it = kinds.find(obj); int k;
    if (it != kinds.end()) k = it->second;
    else
    {
        std::string c = UeClassName(obj);
        k = c.rfind("GearPawn_COGMarcus", 0) == 0 ? 2 : (c.find("Flag") != std::string::npos || c.find("CommandPost") != std::string::npos || c.find("HordeBase") != std::string::npos) ? 1 : 0;
        if (kinds.size() > 16384) kinds.clear();
        kinds[obj] = (int8_t)k;
        if (c == "GearHordeObjective_BuildFlag") { extern std::atomic<uint32_t> g_dbgObjective; g_dbgObjective = obj; }
        if (k == 1 && flagPos)
        {
            auto F = [](uint32_t a) { uint32_t v = Load32(a); float f; memcpy(&f, &v, 4); return f; };
            LOG("[flagpos] %s %08X en %.1f %.1f %.1f | jugador %.1f %.1f %.1f", c.c_str(), obj, F(obj + 0x54), F(obj + 0x58), F(obj + 0x5C),
                pawn ? F(pawn + 0x54) : 0.f, pawn ? F(pawn + 0x58) : 0.f, pawn ? F(pawn + 0x5C) : 0.f);
        }
    }
    if (k == 2) pawn = obj;
    // GOW3_FLAGPOS: una vez, 8 s despues de ver al jugador, busca GObjObjects (TArray cerca de GNames que contiene al pawn)
    // y registra todos los objetos cuya clase tenga Flag/Fortification/Base/CommandPost/Hologram, con su posicion
    if (flagPos && pawn)
    {
        static DWORD tp0 = GetTickCount(); static bool scanned = false;
        if (!scanned && GetTickCount() - tp0 > 8000)
        {
            scanned = true;
            auto F = [](uint32_t a) { uint32_t v = Load32(a); float f; memcpy(&f, &v, 4); return f; };
            uint32_t gobj = 0, gnum = 0;
            {   // UObject +0x20 = indice en GObjObjects: se busca la posicion del pawn y se confirma con otro objeto conocido
                extern std::atomic<uint32_t> g_dbgObjective;
                uint32_t idx = Load32(pawn + 0x20), other = g_dbgObjective.load(), oidx = other ? Load32(other + 0x20) : 0;
                LOG("[flagpos] indice del pawn %u, objetivo %08X indice %u", idx, other, oidx);
                for (uint64_t region = 0x30000000ull; region < 0xA0000000ull && !gobj; )
                {
                    MEMORY_BASIC_INFORMATION mi{};
                    if (!VirtualQuery(g_base + region, &mi, sizeof(mi))) break;
                    uint64_t rEnd = (uint64_t)((uint8_t*)mi.BaseAddress - g_base) + mi.RegionSize;
                    if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                        for (uint64_t a = region; a + 4 <= rEnd && a < 0xA0000000ull; a += 4)
                            if (Load32((uint32_t)a) == pawn && a >= idx * 4ull)
                            {
                                uint32_t d = (uint32_t)(a - idx * 4ull);
                                if (other && Load32(d + oidx * 4) != other) continue;
                                gobj = d; LOG("[flagpos] GObjObjects (datos) en %08X", d); break;
                            }
                    region = rEnd;
                }
                gnum = 2000000;
            }
            int shown = 0;
            int nulls = 0;
            for (uint32_t i = 0; gobj && i < gnum && shown < 300 && nulls < 200000; i++)
            {
                uint32_t o = Load32(gobj + i * 4); if (!o) { nulls++; continue; } nulls = 0;
                if (o < 0x30000000u || o >= 0xA0000000u) continue;
                std::string c = UeClassName(o);
                if (c.find("Flag") == std::string::npos && c.find("Fortification") == std::string::npos && c.find("CommandPost") == std::string::npos &&
                    c.find("Holo") == std::string::npos && c.find("HordeBase") == std::string::npos) continue;
                if (c.find("Component") != std::string::npos || c == "Class") continue;
                shown++;
                LOG("[flagpos] %s %s %08X en %.1f %.1f %.1f", c.c_str(), UeObjName(o).c_str(), o, F(o + 0x54), F(o + 0x58), F(o + 0x5C));
            }
        }
    }
    if (tp && pawn)
    {
        static float pos[3] = {}; static double after = 15; static bool parsed = false;
        if (!parsed) { parsed = true; sscanf(tp, "%f,%f,%f", &pos[0], &pos[1], &pos[2]); if (const char* c = strchr(tp, ':')) after = atof(c + 1); }
        static DWORD t0 = GetTickCount(); double t = (GetTickCount() - t0) / 1000.0;
        if (t >= after)
        {
            for (int c = 0; c < 3; c++) { uint32_t v; memcpy(&v, &pos[c], 4); Store32(pawn + 0x54 + c * 4, v); }
            static bool shot = false;
            if (!shot && t >= after + 3) { shot = true; extern std::atomic<int> g_dumpRequest; g_dumpRequest = 7000; extern std::atomic<int> g_drawLogRequest; g_drawLogRequest = 1; LOG("[tppos] jugador en %.1f %.1f %.1f", pos[0], pos[1], pos[2]); }
        }
    }
}
PPC_FUNC(sub_822DFBC8)
{
    TrackGameInfo(ctx.r3.u32);
    DebugActors(ctx.r3.u32);
    DebugFlagPos(ctx.r3.u32);
    if (InputScriptWantsEvent())
        InputScriptNotifyOpen("@" + UeClassName(ctx.r3.u32) + "." + UeName(Load32(ctx.r4.u32 + 0x2C)));
    static const char* filt = getenv("GOW3_EVENTLOG");
    if (filt)
    {
        std::string fn = UeClassName(ctx.r3.u32) + "." + UeName(Load32(ctx.r4.u32 + 0x2C));
        bool all = !strcmp(filt, "*");
        if (all || fn.find(filt) != std::string::npos)
        {
            static std::mutex m; static std::map<std::string, int> seen; int n;
            { std::lock_guard l(m); n = seen[fn]++; }
            static const bool st = getenv("GOW3_EVENTSTATE") != nullptr;
            static const bool every = getenv("GOW3_EVENTALL") != nullptr;
            if ((all && n == 0) || (!all && (every || n < 30 || (st && n % 50 == 0))))
                LOG("[event] %s (obj %s %08X estado %s) #%d t=%lu", fn.c_str(), UeObjName(ctx.r3.u32).c_str(), ctx.r3.u32, st ? UeStateName(ctx.r3.u32).c_str() : "", n, GetTickCount());
        }
    }
    static const char* ct = getenv("GOW3_CALLTRACE");
    static std::atomic<int> ctLeft{ getenv("GOW3_CALLTRACEN") ? atoi(getenv("GOW3_CALLTRACEN")) : 6 };
    bool tracing = false;
    if (ct && !g_callTrace && ctLeft > 0)
    {
        std::string fn = UeClassName(ctx.r3.u32) + "." + UeName(Load32(ctx.r4.u32 + 0x2C));
        if (fn == ct) { tracing = true; g_callTrace = 1; ctLeft--; LOG("[call] >>> %s t=%lu", fn.c_str(), GetTickCount()); }
    }
    __imp__sub_822DFBC8(ctx, base);
    if (tracing) { g_callTrace = 0; LOG("[call] <<<"); }
}

// UObject::CallFunction (sub_822DF4D0: r3 = objeto, r4 = FFrame, r5 = resultado, r6 = UFunction): con GOW3_CALLTRACE activo
// registra cada llamada (de script o nativa) con su profundidad y el valor devuelto (primer dword del resultado).
PPC_FUNC_IMPL(__imp__sub_822DF4D0);
PPC_FUNC(sub_822DF4D0)
{
    if (!g_callTrace)
    {   // tambien se puede empezar la traza en una funcion llamada desde script (no solo en un evento)
        static const char* ct = getenv("GOW3_CALLTRACE");
        static std::atomic<int> left{ getenv("GOW3_CALLTRACEN") ? atoi(getenv("GOW3_CALLTRACEN")) : 6 };
        if (ct && left > 0 && UeName(Load32(ctx.r6.u32 + 0x2C)) == strchr(ct, '.') + 1 && UeClassName(ctx.r3.u32) == std::string(ct, strchr(ct, '.')))
        {
            left--; LOG("[call] >>> %s (desde script) t=%lu", ct, GetTickCount());
            g_callTrace = 1; __imp__sub_822DF4D0(ctx, base); g_callTrace = 0; LOG("[call] <<<"); return;
        }
        __imp__sub_822DF4D0(ctx, base); return;
    }
    int d = g_callTrace++;
    uint32_t obj = ctx.r3.u32, fnp = ctx.r6.u32, res = ctx.r5.u32;
    std::string fn = UeClassName(obj) + "." + UeName(Load32(fnp + 0x2C));
    LOG("[call] %*s%s", d * 2, "", fn.c_str());
    __imp__sub_822DF4D0(ctx, base);
    g_callTrace = d;
    LOG("[call] %*s= %08X", d * 2, "", res ? Load32(res) : 0);
}

// AActor::SetTimer interno (sub_82639FE0: r3 = actor, f1 = intervalo, r5 = repetir, r6 = FName de la funcion (64 bits: indice arriba),
// r7 = objeto). GOW3_TIMERLOG=<texto> registra los que contienen el texto en "Clase.Funcion" (30 por nombre).
PPC_FUNC_IMPL(__imp__sub_82639FE0);
PPC_FUNC(sub_82639FE0)
{
    static const char* filt = getenv("GOW3_TIMERLOG");
    if (filt)
    {
        std::string fn = UeClassName(ctx.r7.u32 ? ctx.r7.u32 : ctx.r3.u32) + "." + UeName((uint32_t)(ctx.r6.u64 >> 32));
        if (fn.find(filt) != std::string::npos)
        {
            static std::mutex m; static std::map<std::string, int> seen; int n;
            { std::lock_guard l(m); n = seen[fn]++; }
            if (n < 30) LOG("[timer] SetTimer %s %.2f s%s (actor %s) t=%lu", fn.c_str(), ctx.f1.f64, ctx.r5.u32 ? " repetir" : "", UeObjName(ctx.r3.u32).c_str(), GetTickCount());
        }
    }
    // GOW3_HORDESKIPBUILD=1 (prueba): no dejar que el objetivo "Build a Command Post" (tutorial de Horda) reinicie la fase de
    // despliegue cada segundo: solo se acepta la primera vez que se programa EndDeploymentPhase.
    static const bool skipBuild = getenv("GOW3_HORDESKIPBUILD") != nullptr;
    if (skipBuild && UeName((uint32_t)(ctx.r6.u64 >> 32)) == "EndDeploymentPhase")
    {
        static uint32_t armedFor = 0;
        if (armedFor == ctx.r3.u32) return;
        armedFor = ctx.r3.u32;
        LOG("[horde] EndDeploymentPhase programado una vez (GOW3_HORDESKIPBUILD)");
    }
    __imp__sub_82639FE0(ctx, base);
}

// GoW3 sesion 2: caida en la oleada de Horda (y en Judgment sub_826A8DF8): sub_82683148 recorre la lista r16 -> +0x90 sacada de
// r7+0x124 (elementos de 12 bytes) indexada por la tabla de bytes this+0x33C/0x348 [r4], y llama a vtable+0x138 de cada uno.
// Se valida la lista antes de entrar; si un objeto no tiene vtable valida se registra todo (GOW3_ANIMCHECK=0 lo desactiva).
extern "C" bool GuestHasFunction(uint32_t a);
PPC_FUNC_IMPL(__imp__sub_82683148);
PPC_FUNC(sub_82683148)
{
    static const bool on = !getenv("GOW3_ANIMCHECK") || atoi(getenv("GOW3_ANIMCHECK")) != 0;
    if (on)
    {
        uint32_t self = ctx.r3.u32, idx = ctx.r4.u32, r7 = ctx.r7.u32;
        uint32_t tab = (ctx.r5.u32 || !ctx.r6.u32) ? self + 0x33C : self + 0x348;
        int32_t num = (int32_t)Load32(tab + 4);
        if (num > 0 && GuestPtr(Load32(tab)) && GuestPtr(r7))
        {
            uint32_t b = g_base[Load32(tab) + idx];
            if (b != 0xFF)
            {
                uint32_t arr = Load32(r7 + 0x124), o = Load32(arr + b * 12 + 8);
                int32_t lists = (int32_t)Load32(r7 + 0x128);
                if ((int32_t)b >= lists)
                {   // indice de SkelControlLists fuera del AnimTree: igual que la rama 0xFF del original (no hay controles)
                    static std::atomic<int> nOut{ 0 };
                    if (nOut++ < 5) LOG("[anim] indice %u fuera de las %d listas del AnimTree %08X (malla %08X): se omite", b, lists, r7, self);
                    return;
                }
                for (int k = 0; o && k < 64; k++, o = Load32(o + 0x90))
                {
                    uint32_t vt = GuestPtr(o) ? Load32(o) : 0, f = GuestPtr(vt) ? Load32(vt + 0x138) : 0;
                    if (!GuestHasFunction(f))
                    {
                        static std::atomic<int> n{ 0 };
                        if (n++ < 5)
                            LOG("[anim] lista invalida: this %08X (%s %s) idx %u tabla %08X n %d byte %u | r7 %08X (%s %s) arr %08X num %d | nodo %d = %08X vt %08X f %08X | r5 %08X r6 %08X r8 %08X",
                                self, UeClassName(self).c_str(), UeObjName(self).c_str(), idx, Load32(tab), num, b, r7, UeClassName(r7).c_str(), UeObjName(r7).c_str(),
                                arr, (int)Load32(r7 + 0x128), k, o, vt, f, ctx.r5.u32, ctx.r6.u32, ctx.r8.u32);
                        if (getenv("GOW3_ANIMSKIP")) return;   // prueba: omitir la llamada
                        break;
                    }
                }
            }
        }
    }
    __imp__sub_82683148(ctx, base);
}


// GoW3 sesion 4 (de GoWJ sesion 9): panoramico Hor+ (GOW3_PANO / panoramico= 21:9, 32:9, auto). Judgment sub_826428F8 / sub_826427F8.
// sub_826202C8 = FPerspectiveMatrix(r3 salida, f1 medio FOV horizontal, f2 ancho, f3 alto, f4 MinZ): FOV horizontal ampliado.
// sub_826201C8 = FPerspectiveMatrix(medio FOV X, medio FOV Y, MultFOVX, MultFOVY = 16/9, ...) de la vista principal: MultFOVX / k.
float PanoAspect();
PPC_FUNC_IMPL(__imp__sub_826202C8);
PPC_FUNC(sub_826202C8)
{
    static const float k = PanoAspect() / (16.0f / 9.0f);
    static const bool off = getenv("GOW3_NOPANOFOV") != nullptr;
    if (!off && k > 1.001f && ctx.f3.f64 > 0 && std::fabs(ctx.f2.f64 / ctx.f3.f64 - 16.0 / 9.0) < 0.02)
    {
        ctx.f1.f64 = std::atan(std::tan(ctx.f1.f64) * k);
        ctx.f2.f64 *= k;
    }
    __imp__sub_826202C8(ctx, base);
}
PPC_FUNC_IMPL(__imp__sub_826201C8);
PPC_FUNC(sub_826201C8)
{
    static const float k = PanoAspect() / (16.0f / 9.0f);
    static const bool off = getenv("GOW3_NOPANOFOV") != nullptr;
    if (!off && k > 1.001f && std::fabs(ctx.f4.f64 - 16.0 / 9.0) < 0.02 && std::fabs(ctx.f3.f64 - 1.0) < 0.001) ctx.f3.f64 /= k;
    __imp__sub_826201C8(ctx, base);
}

// GoW3 sesion 5: raton nativo de UE3. sub_8273B518 = InputAxis del cliente del viewport (vtable: InputKey sub_8273B418, InputAxis
// a continuacion): r3 this, r4 controlador, r5 tecla (FName de 64 bits), f1 valor, f2 tiempo del cuadro. Al llegar el eje
// XboxTypeS_RightX (global 0x83444438) se repite la llamada con MouseX (0x834440C8) / MouseY (0x834440D0) y el movimiento
// acumulado: el motor lo trata como raton (aMouseX/aMouseY, MouseSensitivity = 60) sin el limite de velocidad del stick.
// GOW3_MOUSESTICK=1 = sistema anterior (raton -> stick derecho).
#include "input_mouse.h"
bool MouseNativeActive() { static const bool off = getenv("GOW3_MOUSESTICK") != nullptr; return !off; }
PPC_FUNC_IMPL(__imp__sub_8273B518);
PPC_FUNC(sub_8273B518)
{
    uint64_t key = ctx.r5.u64;
    const uint64_t rightX = Load64(GOW3ADDR(0x83444438u, 0x83454C08u)), mouseX = Load64(GOW3ADDR(0x834440C8u, 0x83454898u)), mouseY = Load64(GOW3ADDR(0x834440D0u, 0x834548A0u));
    static int logged = 0;
    if (getenv("GOW3_MOUSELOG") && logged < 6 && key == rightX) { logged++; LOG("[raton] InputAxis %s valor %g dt %g r4 %08X r6 %08X", UeName((uint32_t)(key >> 32)).c_str(), ctx.f1.f64, ctx.f2.f64, ctx.r4.u32, ctx.r6.u32); }
    PPCContext saved = ctx;
    __imp__sub_8273B518(ctx, base);
    if (key == rightX && MouseNativeActive() && g_mouseCaptured)
    {
        static const float sens = getenv("GOW3_MOUSESENS") ? (float)atof(getenv("GOW3_MOUSESENS")) : 1.0f;
        int dx = g_mouseNativeDX.exchange(0), dy = g_mouseNativeDY.exchange(0);
        if (dx) { PPCContext c = saved; c.r5.u64 = mouseX; c.f1.f64 = dx * 0.25 * sens; __imp__sub_8273B518(c, base); }
        if (dy) { PPCContext c = saved; c.r5.u64 = mouseY; c.f1.f64 = -dy * 0.25 * sens; __imp__sub_8273B518(c, base); }
        static int nl = 0; if (getenv("GOW3_MOUSELOG") && (dx || dy) && nl++ < 10) LOG("[raton] inyectado dx %d dy %d", dx, dy);
    }
}

// GoW3 sesion 5: diagnostico del idioma (appGetLanguageExt): registra la extension que elige el motor la primera vez
PPC_FUNC_IMPL(__imp__sub_8295B728);
PPC_FUNC(sub_8295B728)
{
    uint32_t out = ctx.r3.u32;
    __imp__sub_8295B728(ctx, base);
    static std::atomic<int> n{ 0 };
    if (n++ < 3)
    {
        uint32_t data = Load32(out), num = Load32(out + 4);
        std::string s;
        for (uint32_t i = 0; data && i + 1 < num && i < 16; i++) s += (char)Load16(data + i * 2);
        LOG("[idioma] el motor usa la extension '%s'", s.c_str());
    }
    // con la capa de idioma activa (GOW3_LANGDIR) se fuerza su extension (GOW3_LANGEXT, ESM por defecto): el ejecutable World
    // descarta ESM aunque los archivos esten. Se sobrescribe en el sitio el resultado y la copia en cache (mismo largo que INT).
    static const std::string force = [] {
        const char* d = getenv("GOW3_LANGDIR"); if (!d || !*d) return std::string();
        const char* e = getenv("GOW3_LANGEXT"); return std::string(e && strlen(e) == 3 ? e : "ESM"); }();
    if (!force.empty())
    {
        const uint32_t cache = GOW3ADDR(0x83459064u, 0x83469864u);
        for (uint32_t fs : { out, cache })
        {
            if (!fs) continue;
            uint32_t data = Load32(fs), num = Load32(fs + 4);
            if (!data || num != 4) continue;
            for (int i = 0; i < 3; i++) Store16(data + i * 2, (uint16_t)force[i]);
        }
        static std::atomic<bool> once{ false };
        if (!once.exchange(true)) LOG("[idioma] extension forzada a '%s' (capa de idioma)", force.c_str());
    }
}

// GoW3 sesion 5 (de Judgment sesion 10; direcciones con tools/funcmatch.py): XMA de XAudio. El juego lanza (kick, registro MMIO 0x650) y limpia (0x6A0) los contextos de una voz en un bucle,
// un stwbrx de un solo bit por contexto: varios seguidos del mismo grupo de 32 se pisaban antes de que el hilo del XMA los leyera
// y esos contextos no se decodificaban (sonidos y voces que faltan). Lista de la voz en r3: {u32 n, u32 flags, u32 entradas}
// con entradas de 0x60 bytes (+0x40 = contexto, +0x50 = indice u16). GOW3_XMA_NOHOOK=1 = antes.
void XmaKickContext(uint32_t id);
void XmaClearContext(uint32_t id);
static bool XmaHookOff() { static const bool v = getenv("GOW3_XMA_NOHOOK") != nullptr; return v; }
static uint32_t XmaListIds(uint8_t* base, uint32_t list, uint32_t* ids, bool* fresh, uint32_t max)
{
    uint32_t n = __builtin_bswap32(*(uint32_t*)(base + list)), ents = __builtin_bswap32(*(uint32_t*)(base + list + 8)), k = 0;
    for (uint32_t i = 0; i < n && k < max && ents; i++)
    {
        uint32_t e = ents + i * 0x60;
        ids[k] = __builtin_bswap16(*(uint16_t*)(base + e + 0x50));
        if (fresh) fresh[k] = *(uint32_t*)(base + e + 0x40) == 0;
        k++;
    }
    return k;
}
PPC_FUNC_IMPL(__imp__sub_82B1CF98);
PPC_FUNC(sub_82B1CF98)   // lanzar los contextos de una voz
{
    uint32_t list = ctx.r3.u32;
    __imp__sub_82B1CF98(ctx, base);
    if (XmaHookOff() || !list) return;
    uint32_t ids[16]; uint32_t n = XmaListIds(base, list, ids, nullptr, 16);
    for (uint32_t i = 0; i < n; i++) XmaKickContext(ids[i]);
}
PPC_FUNC_IMPL(__imp__sub_82B1C600);
PPC_FUNC(sub_82B1C600)   // crear (y limpiar) los contextos de una voz
{
    uint32_t list = ctx.r3.u32, ids[16]; bool fresh[16] = {};
    uint32_t n = (!XmaHookOff() && list) ? XmaListIds(base, list, ids, fresh, 16) : 0;
    __imp__sub_82B1C600(ctx, base);
    for (uint32_t i = 0; i < n; i++)
        if (fresh[i])
        {
            uint32_t e = __builtin_bswap32(*(uint32_t*)(base + list + 8)) + i * 0x60;
            XmaClearContext(__builtin_bswap16(*(uint16_t*)(base + e + 0x50)));   // el indice se escribe dentro de la funcion
        }
    static int nl = 0; if (n > 1 && nl++ < 5) LOG("[xma] voz con %u contextos (antes se perdian kicks/clears del mismo grupo)", n);
}
