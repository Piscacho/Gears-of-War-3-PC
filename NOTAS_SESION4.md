# Sesión 4: lo de Judgment (sesiones 7-9), DLC, actualización del título y launcher con SteamGridDB

Fecha: 2026-11-15. Sin commits.

## Fusión con el runtime de Judgment

`runtime_gowj_orig/` es el runtime de Judgment tal como se copió (sesión 2). Los cambios que hizo Judgment después (sus sesiones 7-9)
se fusionaron a tres bandas con `git merge-file`: base y Judgment se normalizaron con los renombrados `GOWJ_`→`GOW3_`, etc., y los
fines de línea se pasaron a LF (los nuestros estaban en CRLF por escribirlos con Python). El script está en `merge/resolve.py`.
La copia anterior a la fusión está en `runtime_pre_merge/`.

- `gpu_d3d12.cpp`: se tomó la versión de Judgment en los 10 conflictos (7e3 y posproceso genérico por GPU, MSAA, R/B, LUT 3D,
  constantes con memcpy, sombras con profundidad derivada, **sesgo de exponente del color**) y se volvió a poner nuestra resolución
  dinámica (`DynResFits`) en el enlace de fmt 6 y 7. Se quitaron los modos duplicados del shader.
- `gpu_cp.cpp`, `xam.cpp`: se conservan las dos partes (LUT de gamma + `g_gpuConstShadow`; registro XMsg + `XgiSessionMessage`).
- `gpu_stub.cpp`, `kernel.cpp`: se mantiene lo nuestro (vblank = 2×fps por defecto, `PortResolution`).
- Nuevos: `launcher_gears.cpp` (el launcher portado del de Dead Space; sustituye a `launcher_ui.cpp` en CMake) y
  `net_gow3.cpp` (System Link P2P de Judgment; `ws2_32` en CMake). Sin probar todavía en Gears 3.

### Resultados

- **La campaña ya no está oscura**: era el sesgo de exponente del color (RB_COLOR_INFO bits 20-25), que Judgment arregló en su
  sesión 7. El barco, Marcus y el ticker se ven iluminados.
- **memset** del CRT en **0x82CC9EA0** (0xA0 bytes, idéntico al 0x82D9F940 de Judgment): XenonRecomp lo cortaba en el primer
  `bdzlr-`. Va como entrada manual en `tools/make_functions.py`. Era la causa real de la basura en `SkelControlIndex` (nuestra
  protección `[anim]` sigue puesta, por si acaso).
- **32:9 / 21:9 Hor+**: los ganchos de Judgment sub_826427F8/sub_826428F8 equivalen en Gears 3 a **sub_826201C8 / sub_826202C8**
  (`tools/funcmatch.py`), en `hooks_gow3.cpp`. Con `GOW3_PANO=32:9` se renderiza a 2560x720 con más campo de visión a los lados.
  Pendiente: la imagen sale algo sobreexpuesta en 32:9 y el HUD un poco ensanchado.
- Campaña a 120 fps en zonas ligeras (baja a 25-55 en zonas pesadas).

## DLC

- Los zip de `DLC\` contienen paquetes STFS en `4D5308AB\00000002\...`. Se extrajeron a **`dlc_paquetes\`** (92 paquetes, 5,6 GB).
  Ojo: en Windows `dlc` = `DLC`; la primera extracción cayó dentro de la carpeta `DLC` del usuario y se movió a `dlc_paquetes`.
- El importador del runtime (`ImportDlc`, en modo usuario con `dlc=` en gow3.ini) los copia a `datos\content`. **El juego base los
  enumera (31 paquetes de contenido), los monta (`DLC0:`…`DLC30:`) y lee su `GearGame.int` y `Xbox360-GearGame.ini`**, sin errores
  y a 120 fps. Falta comprobar en los menús que aparecen los mapas, personajes y skins.
- `tools/run_game.py` acepta ahora `RUN_DATA=<carpeta de datos>`.

## Actualización del título (TU 0x60B), preparada pero no integrada

- `Update\TU_16L625B_...` (LIVE, tipo 0xB0000) → `tu/pkg/` → `tu/default.xexp`.
- Cabeceras: `lzxdelta hdr_records.bin 0x8000 base_hdr.bin 0x4000 new_hdr.bin`. Imagen:
  `lzxdelta img_records.bin 0x8000 basefile.bin 0x1620000 tu_basefile.bin` (**la imagen pasa de 0x15B0000 a 0x1620000**).
  Resultado en `xex/tu_basefile.bin` y `tu/tu_default.xex` (`tools/make_tu_xex.py`). Versión 0xB → 0x60B.
- TU: 41.411 funciones .pdata, `.text` 0x822B0000, BINK 0x82FDCC00. Helpers: savegprlr 0x82CDD030 / restgprlr 0x82CDD080,
  savefpr 0x82CDD8F0 / 0x82CDD93C, savevmx 0x82CDD9F0 / 0x82CDDC88, vmx_64 0x82CDDA84 / 0x82CDDD1C, longjmp 0x82CDFB60,
  setjmp 0x82CDFE80, memset 0x82CDD610, colas 0x82CDB940 / 0x82CDF800. XenonAnalyse: 76 switches (`config/gow3tu_switch_tables.toml`).
- Falta, como con GOWJTU.exe: `config/gow3_tu.toml`, switches de Bink y jt_finder, thunks de GMalloc (con ptr/lisaddi tras la
  primera pasada), `ppc_tu/`, compilar con `-DGOW3_TU=ON` (build_tu) y pasar a `GOW3ADDR(original, tu)` todas las direcciones fijas
  (entry, kImageEnd, thunks XAM, GNames 0x8343D148, ProcessEvent, CallFunction, SetTimer, appError, anim, perspectiva) con
  `funcmatch` entre basefile y tu_basefile. Con eso GOW3.exe le cede el paso a GOW3TU.exe si la TU está importada.

## Launcher

- `launcher_gears.cpp`: textos de Gears 3 ("SERA · LA ÚLTIMA GUERRA", 4D5308AB), FPS 30/60/120/144/165/240/sin límite (120 por
  defecto), **Brillo** (Gráficos) y **Saltar el tutorial de la Horda** (Avanzado). En línea: crear/unirse (System Link de Judgment).
- Imágenes de **SteamGridDB** (juego 36635): hero `ac627ab1ccbdb62ec96e702f07f6425b.png` → `dist_files/launcher/portada.jpg`
  (1920 de ancho) y logo `8446920106785cf38221e8fc4a13c4c0.png` → `dist_files/launcher/logo.png`. Originales en `ref/steamgriddb/`.
- `tools\make_dist.bat` copia `launcher\`. La distribución no lleva datos del juego.

## Siguiente

1. GOW3TU.exe (ver arriba) y comprobar los DLC en los menús (algunos pueden necesitar la TU).
2. 32:9: exposición y HUD.
3. System Link entre dos PCs con `net_gow3.cpp` (Judgment se quedó en "PLAYERS CONNECTING" al empezar la partida).
4. Rendimiento en zonas pesadas de la campaña.
