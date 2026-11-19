# Sesión 2 (larga): de XenonRecomp a una partida de Horda jugable

Fecha: 2026-11-14/15. Sin commits. Usamos nuestro propio método: XenonRecomp + runtime de Judgment (el cual viene de NG2/GH).

## Resultado

- **GOW3.exe** compila y enlaza a la primera con el runtime de Judgment adaptado. Se llega a todo esto:
  logos Bink → PRESS START → calibración de brillo (se acepta con **A**) → **menú principal** → Horde → Private →
  opciones → "Start Match" → lobby → Map Selection (Checkout, se confirma con **START**) → Character Setup →
  **partida de Horda en Checkout**: el personaje se mueve, apunta y dispara, llegan los enemigos, hay combate y gore, y
  "WAVE 1 FAILED!" muestra las opciones Restart / Main Menu. La campaña también empieza a cargar.
- El rendimiento es de **~10 fps** en partida (se mira en "Siguiente").

## Recompilación

- `ppc/`: **83.894 funciones**, 0 switches con error y 0 instrucciones sin reconocer.
- Switches en `config/gow3_switch_tables_all.toml` (108 en total): 76 de XenonAnalyse, 29 de jt_finder y 3 de Bink sin
  comprobación de rango (los de Judgment desplazados −0xD3000: 0x82FDE358, 0x82FDFC9C, 0x82FDCF04).
  Corregidos con `jt_check_regs`: 0x823A9550 r4→r5 (igual que en Judgment) y 0x82CAD0E4 r8→r7.
- `functions` (`tools/make_functions.py`): 9 funciones de hueco (`gap_functions.py`; mismos tamaños que en Judgment), setjmp,
  los thunks de GMalloc 0x82D680A0/C0/F0, 0x82D68118 y 0x82D68140 (Judgment 0x82E38080…), y 0x82CC81D0/0x82CCC090 (0xC).
- **Gears 3 y Judgment comparten casi todo el código.** `tools/funcmatch.py` (nuevo) empareja funciones por su patrón de
  instrucciones, con inmediatos y saltos enmascarados: **19.667** parejas únicas (`xex/judgment_to_gow3.txt`).
  `python tools/funcmatch.py <sub de Judgment>` da la función equivalente de GoW3.

## Runtime (copiado de Judgment; la copia sin tocar está en `runtime_gowj_orig/`)

- `GOWJ_` → `GOW3_`, `kernel_gow3.cpp`, entry 0x82CE8EB0, kImageEnd 0x835B0000, thunks XAM 0x82CCC704/0x82CCC708 (relleno
  entre longjmp y setjmp). Imports regenerados (`tools/gen_import_stubs.py`): 280 funciones, todas ya conocidas por Judgment.
- Los ganchos de Judgment quedan como referencia en `runtime/hooks_gowj_ref.txt` (direcciones de Judgment).
  `ApplyPanoCamera` está desactivado (la constante 16/9 no existe en GoW3; `GOW3_PANOADDR` para probar).
- `[fs] abrir` lleva ahora el tiempo desde el arranque (útil para ajustar guiones).
- XMsgStartIORequest(Ex) registra las 3 primeras llamadas de cada (app, mensaje). Solo aparecen 0xFB/0xB0006, 0xB0007 y
  0xFA/0x7001A: **el juego no llama a XSessionCreate** en la Horda privada.

## Herramientas de UnrealScript nuevas (`runtime/hooks_gow3.cpp`)

| Qué | Dirección GoW3 |
|---|---|
| GNames (TArray de FNameEntry*) | `*(0x8343D148)`; entrada: +0 flags (bit 0 = unicode), +8 texto |
| UObject | +0x14 StateFrame (estado en +0x10 del marco), +0x28 Outer, +0x2C FName, +0x34 Class |
| FName::ToString | sub_82303460 |
| FindFunction / FindFunctionChecked | sub_8230C718 / sub_8230C7F0 |
| UObject::ProcessEvent (vtable+0xFC) | sub_822DFBC8 (AActor::ProcessEvent sub_82438F50) |
| UObject::CallFunction | sub_822DF4D0 (r6 = UFunction) |
| execVirtualFunction | sub_822CD110 |
| AActor::SetTimer (interno) | sub_82639FE0 (f1 intervalo, r5 repetir, r6 FName de 64 bits, r7 objeto); execSetTimer sub_825260B0 |
| appError | sub_82319958 |
| Tabla de nativas con nombre | pares {"AClaseexecFuncion", función} en .data (p. ej. AActorexecSetTimer 0x83395B90) |

Variables:
- `GOW3_EVENTLOG=<texto>|*`: eventos ("Clase.Función"). `GOW3_EVENTALL=1` los registra todos y `GOW3_EVENTSTATE=1` añade el estado.
- `GOW3_CALLTRACE=Clase.Funcion` (+ `GOW3_CALLTRACEN`): árbol de llamadas de script y nativas dentro de esa función (evento o llamada).
- `GOW3_TIMERLOG=<texto>`: llamadas a SetTimer.

## Dos problemas de la Horda resueltos

1. **El contador "0:30" no bajaba.** `GearHordeObjective_BuildFlag.Timer` reprograma `GearGameHorde.EndDeploymentPhase` (30,5 s)
   cada segundo mientras el objetivo **"CREATE A BASE: Build a Command Post"** siga pendiente. Es el tutorial de Horda de
   los perfiles nuevos, no un fallo: hay que construir el puesto de mando (X en su marcador). `GOW3_HORDESKIPBUILD=1` (prueba)
   solo deja programarlo una vez, y así la oleada empieza a los 30 s.
2. **Caída al llegar los enemigos** (`llamada indirecta a 0x42800000`, lr 0x82683390; es la misma que la de Judgment
   sub_826A8DF8). En sub_82683148 (SkelControls de un `SkeletalMeshComponent`), la tabla hueso → lista (`this+0x33C/0x348`, bytes)
   da índices 16 y 202, pero el `AnimTree` (r7) solo tiene **1** lista en `+0x124/+0x128`, así que recorría basura
   (0x0000000E, 0xDEADBEEF). Ahora el gancho trata un índice ≥ número de listas igual que 0xFF (no hace nada) y lo registra
   como `[anim]`. Queda pendiente averiguar por qué la tabla de la malla no corresponde a su AnimTree, que sería la causa de raíz.
   **Este arreglo vale también para el Survival de Judgment.**

## Guiones de entrada (`scripts/`)

- `g3horde7.txt`: hasta la partida de Horda (ANCHOR `PlayerStorage.dat`). `g3wave3.txt`: además, disparar y girar durante la oleada.
- En los menús, DOWN/A llegan bien, pero solo cuando el menú ya acepta entrada (por eso el ANCHOR). START confirma el mapa y la partida.
- `run/jugar.bat` lanza el juego para jugar a mano.

## Siguiente

1. **Rendimiento** (~10 fps; cuadro de ~120 ms: 28 ms de texturas resubidas (~30 MB por cuadro), 30-40 ms de vaciados y ~21
   resolves por CPU por cuadro). Los que quedan en la CPU (`GOW3_CPURESOLVESTATS=1`) son:
   - escena HDR 1280x720 **fmt 7 desde superficie 3** (7e3) → `0C4C0000`: hacer el empaquetado 7e3 en un shader;
   - posproceso 328x182 / 640x360 / 192x96 **fmt 26 desde superficie 5**, **fmt 31 desde 6** y **fmt 32 desde 5**.
2. **System Link real** (objetivo de la Horda online): NetDll_* → Winsock (sockets VDP 254 = UDP), XNADDR con la IP del
   adaptador (LAN o Radmin/ZeroTier), difusión de LAN, `XNetGetEthernetLinkStatus` = conectado, XUID distinto por PC
   (`kXuid` está fijo en xam.cpp). Para probar en un solo PC: instancias en 127.0.0.1/127.0.0.2 con la difusión convertida a unicast.
3. Que la calibración de brillo no salga en cada arranque, y revisar el guardado del perfil.
