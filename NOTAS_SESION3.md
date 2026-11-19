# Sesión 3 (larga): rendimiento hasta 120 fps, guiones robustos, campaña, ini en memoria y launcher

Fecha: 2026-11-15. Sin commits.

## Resultado

| | Antes | Ahora |
|---|---|---|
| Horda (calma / con enemigos) | ~10 fps | **115-120 fps / 52-73 fps** (tope 120) |
| Menús | 30 fps | 62 → tope configurable |
| Resolves por CPU por cuadro | ~21 | **0** |
| Campaña | sin probar | arranca: dificultad → carga → cinemática con subtítulos → se juega (Act 1, "Follow Anya", tutorial WALK) |
| Launcher | el de NG2 | Gears 3 (portada y logo sacados de capturas del juego), FPS, brillo, tutorial de Horda; `tools\make_dist.bat` → `dist\GearsOfWar3` |

## Rendimiento (`runtime/gpu_d3d12.cpp`)

1. **Escena HDR 7e3 por GPU**: el resolve de la superficie 3 (RGBA16F) a fmt 7 se empaqueta en un shader
   (`F7e3`, el mismo redondeo que `Float32To7e3`) sobre R10G10B10A2_UNORM (bits/1023). El tonemap lo enlaza como fmt 7/54
   (texFmt 7). Si no hay alias en bruto, el modo 6 escribe 2_10_10_10 UNORM normal. `GOW3_CPU7E3=1` = antes.
2. **Posproceso por GPU**: superficie 5 (16_16_16_16, SNORM×32) → fmt 26 (modo 4: saturar ×32×2^sesgo, R16G16B16A16_UNORM) y
   → fmt 32 (modo 5); superficie 6 (16_16_FLOAT) → fmt 31 (y fmt 28 = alias). El **sesgo de exponente** (−5 en el bloom) se pasa
   como constante `mode.z`. `GOW3_CPUPOST=1` = antes.
3. **Resolución dinámica de UE3**: se resuelven regiones (1089x529, 1280x353…) y se leen como texturas de 1280x720.
   `DynResFits` deja enlazar la copia de la GPU (con la región en la esquina) hasta 1/4 del tamaño. Antes se volcaba a memoria
   esperando a la GPU. `GOW3_NODYNRES=1` = antes.
4. **`getenv` con caché** (`runtime/env_cache.h`, incluido en gpu_d3d12/gpu_cp/gpu_shader) y **`CmdTrace` desactivado** salvo con
   `GOW3_CMDTRACE=1` (antes hacía `sprintf` en cada draw).
5. **Páginas sucias** (`runtime/texdirty.cpp`): las k_8 grandes (la caché de glifos) recalculaban la huella de hasta 4 MB en
   cada enlace (27 % del hilo de la GPU). Ahora se protegen como solo lectura en las vistas 0xA0000000 y 0xC0000000 (0xE0000000 es
   memoria aparte), y la primera escritura las marca como sucias (en el manejador de main.cpp). `GOW3_NOTEXDIRTY=1` = antes.
6. **Descriptores**: los 48 huecos de textura por draw se copian de una tabla en blanco (`CopyDescriptorsSimple`) y solo se crean
   vistas para las texturas que usa el shader. `GOW3_OLDSRVFILL=1` = antes.
7. **Tope de fotogramas**: el juego presenta con intervalo 2 sobre el vblank emulado (60 Hz → 30 fps) y UE3 limita con
   `[Engine.Engine] bSmoothFrameRate / MaxSmoothedFrameRate=62`. Ahora `GOW3_MAXFPS` (por defecto 120; 0 = sin tope) pone
   `MaxSmoothedFrameRate = fps+2` en el ini y el vblank a 2×fps (máx. 480 Hz). La lógica del juego usa el tiempo real: el contador
   de la Horda baja 10 s cada 10 s a 120 fps.

Herramientas: `tools/proflines.py` (líneas más calientes de las muestras `[prof]`), `GOW3_PROFILE=<s>:<n>` (+`GOW3_PROFILEALL=1`),
`GOW3_CPURESOLVESTATS=1`.

Pendiente: las caídas a 52-73 fps con enemigos (en el perfil manda `D3D12DrawImpl`: copia de vértices con huella, conversión de
índices, `WriteBackResolved` ~1 por cuadro de 0C4C0000 leído como fmt 54 desde un resolve 8888).

## Ini del juego en memoria (`fs.cpp: ApplyIniOverrides`, `port_settings.cpp`)

- `Coalesced_*.bin` no está comprimido: un int32 BE con el número de archivos; por archivo, nombre y número de secciones; por
  sección, nombre y número de claves; cada clave lleva nombre y valor. Las cadenas llevan un int32 BE (negativo = UTF-16LE).
  `tools/coalesced.py archivo buscar <texto>`.
- Al indexar el ISO se reescriben los `Coalesced_*` con los cambios y se sirven desde memoria (`IsoEntry::mem`). Los cambios vienen
  de `IniOverrides()` (FPS) y de `GOW3_INI="Seccion:Clave=Valor;..."`. Solo se tocan los archivos `Xbox360-*`.
- `GOW3_CMDLINE` añade argumentos a `ExLoadedCommandLine`, pero el juego ignora el mapa: siempre va al menú.

## Guiones robustos

- `ANCHOR @Clase.Funcion` espera a un **evento de UnrealScript** (ProcessEvent); se suma a `ANCHOR <archivo>`.
- `scripts/g3hordeC.txt` / `g3waveC.txt`: Horda privada en Checkout, sin depender de los fps (anclas `PlayerStorage.dat` ×2,
  `@GearPreGameGRI.PostBeginPlay` y `@GearHordePC.StartupStopLoadingMovie`). La calibración de brillo se acepta con A, y la
  segunda apertura de `PlayerStorage.dat` es justo ese momento.
- `scripts/g3camp1.txt` / `g3camp2.txt`: campaña nueva y caminar.

## Campaña

- Se juega desde el principio: Select Difficulty → pantalla de carga (Marcus) → cinemática en el motor con subtítulos → control del
  jugador ("Follow Anya", tutorial WALK). El personaje se mueve (el bloqueo de Judgment no existe aquí).
- **Muy oscura**: el barco y la cinemática se ven casi negros, con luces azules. No son superficies GAMMA (el juego no las usa) ni la
  LUT de la salida: el juego escribe una LUT de 256 entradas (0x1922 = índice, 0x1925 = 10:10:10) que es la identidad salvo durante
  la vista previa de la calibración; ahora se decodifica (`[gamma] LUT nueva`). Sospecha: falta iluminación dinámica o sombras en la
  escena. Mientras tanto hay una opción de brillo del port (`GOW3_GAMMA`, `brillo=` en gow3.ini, en el shader de presentación;
  las capturas DUMP se toman antes y no la muestran).

## 32:9 y resolución (NO hecho)

- El juego tiene 1280x720 fijo: ignora `[SystemSettings] ResX/ResY` y el modo de vídeo (`GOW3_RES=WxH` cambia ambos, pero siguió
  creando superficies de 1280 de pitch).
- El panorámico heredado de NG2 parcheaba una constante 16/9 de NG2 (direcciones de NG2): está desactivado (`GOW3_PANOADDR`).
- Plan: (1) enganchar la matriz de perspectiva de UE3 (`FPerspectiveMatrix` en `ULocalPlayer::CalcSceneView`; el PI/360 de
  0x8208FB0C es de un cuaternión, no sirve) para hacer Hor+ con recorte correcto, y renderizar las superficies con la escala
  horizontal `sx` (infraestructura de NG2, sesión 27). (2) Resolución interna: escalar ×N todas las superficies EDRAM, viewports,
  scissors, resolves y texturas resueltas.

## Launcher (`runtime/launcher_ui.cpp`, `launcher.cpp`)

- Textos de Gears 3. Inicio: FPS (30/60/120/144/165/240/sin límite), pantalla completa, contador. Gráficos: FPS, **brillo**
  (0,8–1,5), filtrado... Juego: ISO, **Saltar el tutorial de la Horda** (`horda_sin_tutorial=` → `GOW3_HORDESKIPBUILD`).
  Teclado con los nombres de los controles de Gears y ayuda nueva.
- `dist_files/`: portada.png (Marcus de la pantalla de carga, en espejo, sobre negro), logo.png (logo de esa pantalla con alfa
  por luminancia), gow3.ini de ejemplo, LEEME.txt. `tools\make_dist.bat` → `dist\GearsOfWar3` (sin archivos del juego).
- Probado: launcher → JUGAR → descifra el XEX del ISO → PRESS START. `tools/capwin.ps1` captura una ventana con PrintWindow
  (window_shot.py no sirve con escalado de pantalla).

## Siguiente

1. 32:9 Hor+ y resolución interna (ver arriba).
2. Campaña: averiguar por qué está tan oscura (comparar luces y sombras con la Horda, revisar los draws de luces de la escena).
3. Rendimiento con enemigos (52-73 fps).
4. System Link (plan en NOTAS_SESION1/2).
