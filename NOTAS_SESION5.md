# Sesión 5 — TU, DLC, ratón/teclado, DualSense, voces y español latino

## Hecho
- **Actualización del título v0x60B** → `project/build_tu/GOW3TU.exe` (`tools/build_game_tu.bat`, código en `ppc_tu/`).
  Mapas `tu/funcmap.txt` / `tu/datamap.txt`; enganches remapeados en `runtime/tu_remap.h`
  (añadidos esta sesión: InputAxis `8273B518→8274CC98`, appGetLanguageExt `8295B728→8296C768`).
  El arranque con TU espera una pulsación antes del título: los guiones necesitan tiempos propios (`scripts/g3dlcmaps_tu.txt`).
- **DLC en menús** (Horda: mapa por defecto ACADEMY del Horde Command Pack).
- **Gráficos**: bias de color solo en formatos 5/7; resolve fijo→float sin ×32 (`GOW3_FIXEDTOFLOAT32=1` = antes).
  Arregla menú quemado y cinemáticas borrosas.
- **Ratón**: inyección nativa en `PlayerInput.InputAxis` (MouseX/MouseY de UE3), sin aceleración de vista
  (`bViewAccelEnabled=FALSE`). `GOW3_MOUSESTICK=1` = sistema anterior (ratón→stick). F10 captura/libera.
- **Teclado**: disposición de Gears (ver LEEME). **DualSense** por HID, mezclado con XInput por eje.
- **Voces**: downmix 5.1→estéreo con canal central a `GOW3_VOICEGAIN` (launcher: Volumen de las voces).
- **Español latino** (capa de idioma, `runtime/fs.cpp`):
  - `GOW3_LANGDIR` = carpeta de otro disco (busca `GearGame\CookedXbox360` hasta 3 niveles). Se sirven encima del ISO
    los archivos `*_ESM.*`, la carpeta `CookedXbox360\ESM\` y `Movies\*_LOC.*` que ya existan en el ISO (683 archivos, 18 vídeos).
  - El ejecutable World descarta ESM en appGetLanguageExt (lista de idiomas conocidos); el enganche de `sub_8295B728`
    fuerza la extensión `GOW3_LANGEXT` (ESM) en el resultado y en la caché `0x83459064` (TU `0x83469864`).
  - Launcher: Idioma → "Español latino" (busca "juego en latino"/"latino" junto al launcher o al ISO, o `carpeta_latino=`).
  - Verificado: menús en español, campaña (Nightmare/Prólogo) carga `ESM\SP_*_LOC_ESM.xxx` y se juega; TU también.
- **Arreglo de fs**: la enumeración de subcarpetas del ISO nunca funcionó (hijos guardados con mayúsculas) y el patrón
  solo llegaba en la primera llamada; ahora se guarda en el `FileObj`. `GOW3_DIRLOG=1` registra la enumeración.

- **Halo negro alrededor de los personajes y cristales en negativo (arreglado)**: la reducción de escena del DOF
  (PS `834C89F7` → superficie 16_16_16_16 `00050658` → resolve fmt 32 a `0D100000`) necesita el ×32 del fijo→float.
  Sin él, color y profundidad llegaban 32 veces más bajos: el DOF (`319DEC9A`) daba enfoque 0 en los bordes y la
  composición final (`2FA440D5`: `lerp(desenfocado, escena, T4.a)`) mezclaba ahí un color 32 veces más oscuro.
  El menú principal (GameInfo `GearPartyGame`) se quema con ×32 → ahí se mantiene sin ×32 (`g_frontendMenu`, detectado
  en el gancho de ProcessEvent). `GOW3_FIXEDTOFLOAT32=1` siempre ×32, `=2` nunca.
  Con el ×32, Marcus pegado a la cámara sale algo suave (DOF cercano del motor).
- Herramientas nuevas de diagnóstico: `GOW3_PSREPL=<hash>:<texto>=><texto>|...` (sustituye texto del HLSL),
  `GOW3_NOKILL=<hashes>` (quita solo los kill), `GOW3_DISCARDLOG=1` (primer uso de cada PS con kill), `tf4` en
  `[drawst]`, volcados EDRAM con el formato en el nombre (`_f<dxgi>`).
- Banderas de Horda: el contenido existe (`GearFortification_Flag_Content` recibe Bump/Touch) y con `GOW3_NODISCARD=1`
  se ven estandartes e icono holográfico → los oculta algún `kill`. Falta aislar el shader (152 candidatos con kill
  en el mapa; la bisección visual es lenta porque el punto de aparición varía).

- Banderas de Horda (investigación, sin arreglar):
  - Detección de kills corregida (antes contaba también `if (!ap) discard;` de la prueba de alfa).
  - `GOW3_KILLVIS=<dígito>:<hashes>` pinta lo que mataría cada kill con un color por shader (base 7, 3 pasadas);
    `GOW3_ATVIS=1` pinta en magenta lo que no pasa la prueba de alfa; `GOW3_DISCARDLOGPS=<hash>` registra cada draw
    (fetch constants t0..t5 y c252/c253).
  - `GOW3_TPFLAG=54:<inicio s>:<paso s>` teletransporta a Marcus por los 64 `GearFortificationMarker`
    (Actor.Location = +0x54, 3 floats BE) y saca `shot_50NN.bmp` en cada uno. `GOW3_ACTORDUMP=1` vuelca floats.
  - Resultado: en los 64 marcadores no hay ningún holograma recortado por kill ni por prueba de alfa. Las marcas del
    suelo que recorta `77790E8B` son calcomanías (máscara DXT1 en verde, umbral 0,333 normal).
    `GearHordeObjective_BuildFlag` no tiene posición; `GearFortification_Flag_Content` solo aparece cuando el
    jugador está encima del punto de la base (en esa partida, con `GOW3_NODISCARD=1`, sí se veían estandartes e
    icono). Siguiente paso: localizar ese punto (junto al maniquí de Checkout) y teletransportar ahí con KILLVIS.

- Vídeo de referencia (YouTube bF4Gx_lNlAk, Checkout Normal): la bandera es una **silueta verde brillante** sólida
  (estilo Tac-Com), visible desde lejos (p. ej. bajo el cartel "Pharmacy"), y el contador de despliegue baja (0:22);
  en el nuestro se queda en 0:30. La imagen de referencia es bastante más oscura que la nuestra en Checkout.
- Las siluetas (jugador, compañeros, bandera) se dibujan en una máscara 640×360 (PS `9D857238`/`08B170BF`/`57F822FE`,
  superficie `0A000280`/`00000658`) → resolve `0D3D0000` (8888) → la leen `834C89F7` (t2) y `F6B94D12`. La máscara está
  bien alineada. Teletransportado frente a "Pharmacy" (`GOW3_TPFLAG=54:25:3:6`, 4.º campo = marcador fijo; registra
  draws y captura a los 3 s) la máscara **solo contiene a Marcus**: el juego no envía la bandera a dibujar.
- `GearFortification_Flag_Content` solo se crea en algunas partidas (en una, encima del jugador). Kismet funciona
  (ToggleHidden, MPRoundStart...). `BuildFlag.PostBeginPlay` no llama a nada. Siguiente: ver qué activa/oculta las
  banderas (marcadores de tipo bandera, `SetHidden`) — probablemente hay que leer el bytecode de `GearHordeObjective_BuildFlag`
  / `GearFortificationMarker` en memoria.
- Comando de guion nuevo `DRAWLOG` (registra los draws de 2 frames en ese momento).

## Banderas de Horda: ARREGLADO (y gráficos generales)
- La bandera es la malla translúcida del PS `D1C04A3ACF8367AC` (verde, "depth-biased alpha"): alfa = fundido con la
  profundidad de la escena, que lee como **k_24_8_FLOAT (formato 23)** en `0C120000`. El runtime solo servía la profundidad
  resuelta en GPU para 22/6; con 23 cargaba la memoria (ceros) → alfa 0 → invisible. Ahora 23 = 22 (`zfmt`, `GOW3_NODEPTH23=1`
  = antes). En el menú principal (GearPartyGame) se mantiene la lectura antigua (el humo de la calavera salía negro).
  Judgment llegó a lo mismo en su sesión 10 (niebla/partículas con fundido por profundidad).
- Con la profundidad buena, el halo negro ya no aparece sin el ×32: el ×32 del resolve fijo→float vuelve a estar
  **desactivado por defecto** (`GOW3_FIXEDTOFLOAT32=1` siempre ×32, `=3` regla del menú, `=4` color ×32 y alfa sin ×32).
- Fusión de la sesión 10/11 de Judgment (a tres bandas, base `runtime_gowj_orig`, copia previa en `runtime_pre_merge10/`):
  **signo gamma de texturas** (`XTexSign`, PWL de Xenia; `GOW3_NOTEXGAMMA=1`) → la Horda ya tiene el brillo/contraste del
  vídeo de referencia; páginas sucias con firmas (`texdirty.cpp` de Judgment entero), `ResolveMemOk`, VGT_INDX_OFFSET en
  memexport, recorte 7e3. Conservado lo de Gears 3: sesgo de color, LUT gamma de salida, relleno de SRV, superficies 1120
  (`ScreenPitch` incluye 1120/560/280: aquí la escena va en 1120). `gpu_window.cpp` NO se fusionó (ratón propio).
- Desenfoque de movimiento apagado por defecto (como Judgment s.11): `SystemSettings:MotionBlur=False` en el ini en memoria;
  `desenfoque_movimiento=1` / `GOW3_MOTIONBLUR=1` lo activa.
- Verificado: menú con su fondo, Horda con bandera verde, "Buy Command Post: 0" y contador que baja, Marcus nítido, sin halo.
- Guiones: el perfil carga solo al arrancar; los guiones anclan ahora en `@UIScene_FrontEndContent.Callback_OnTitleScreen` y
  `...OnReadSaveGameDataComplete` (`g3flags3`, `g3tp`, `g3dl`). Nuevos diagnósticos: `GOW3_FLAGPOS`, `GOW3_TPPOS=x,y,z[:s]`.

## Pendiente
- Comprobar comprar el puesto de mando (Y) y una oleada completa jugando.
- System Link (Horda online).
- Sobreexposición momentánea al empezar la Horda (adaptación de exposición); revisar con el ×32 nuevo.
- System Link (Horda online entre dos instancias: el anfitrión se queda en "PLAYERS CONNECTING").
- 32:9 (exposición/HUD) y rendimiento en zonas pesadas.
- Los textos ESM vienen de un disco v7: textos añadidos por la TU podrían faltar (comprobar menús de DLC).
- Confirmar con el usuario sensación del ratón, DualSense y que las voces latinas se oyen.

## Última tanda: online, voces, nitidez, 32:9 y rendimiento
- **Horda online por System Link: funciona** (dos instancias en este PC, `tools/run_pair.py` + `scripts/net_host.txt` /
  `net_join.txt`). `net_gow3.cpp` = el de Judgment (reenlace del invitado en puerto +100×generación, sesión 10 de Judgment).
  Flujo: BACK → System Link; anfitrión Y (crear grupo); invitado X (refrescar) + A (unirse); anfitrión Horde → PRIVATE → A … →
  lobby → START (elegir mapa). STANDARD pide Xbox LIVE Gold. Ambos entran en Checkout con su HUD. Sin probar entre PCs reales.
- **Voces/sonidos que faltaban** (latino incluido): arreglo XMA de Judgment s.10. `xma.cpp` de Judgment y ganchos XAudio en
  `sub_82B1CF98` (lanzar contextos de una voz) y `sub_82B1C600` (crear/limpiar), emparejados con `tools/funcmatch.py`
  (Judgment 82C0D7F0/82C0CE58); TU 82B302D0/82B2F938 en `tu_remap.h`. Muestras decodificadas en el mismo tramo: 6,1 M → 24 M.
  `GOW3_XMA_NOHOOK=1` = antes.
- **Nitidez del escalado** (CAS, de Judgment s.11): `nitidez=` en gow3.ini / `GOW3_NITIDEZ` (0..1, por defecto 0,5).
  Ojo: el preprocesador de HLSL no admite decimales en `#if` (se usa `SHARP_ON`); el primer intento rompió el shader de
  presentación y no se veía nada.
- **32:9**: Hor+ y HUD centrado bien. FPS (Horda, caché caliente): 16:9 ~80 → **~109** tras vectorizar `XTexSign`
  (gamma RGB de una vez, `XGammaToLin3`); la versión por componente costaba ~30 %. 32:9 (2560×720) ~69 antes del cambio.
  `GOW3_NODEPTH23` da +9 % (sombras/partículas reales ahora): se deja activado.

## Paquete para la ROG Ally
- Launcher (AVANZADO): filas compactas para ISO, **carpeta de DLC** (`dlc=`), **carpeta de la actualización**
  (`actualizacion_dir=`, nueva; `ImportDlc` la importa como otra carpeta de paquetes) y **carpeta del parche latino**
  (`carpeta_latino=`), con selector de carpetas (`IFileOpenDialog` + `FOS_PICKFOLDERS`).
- `GearsOfWar3_ROGAlly\` = GearsOfWar3 (dist) + Juego (ISO) + DLC (dlc_paquetes) + Update + Latino (solo lo que usa la capa:
  683 archivos, 1,8 GB). ISO/DLC/Update/Latino son **enlaces duros** (no ocupan disco aquí; al copiarlos salen enteros).
  gow3.ini con rutas relativas (`..\Juego\...`, `..\DLC`, …) e `idioma=latino`. Generado con el script make_ally.py
  (scratchpad). Probado desde el paquete: ISO, 92 DLC + TU importados (TU aplicada 0x60B) y capa ESM desde `..\Latino`.

## Actualizador del launcher (como Guitar Hero sesión 24)
- `runtime/updater.cpp` (de Guitar Hero, renombrado): GitHub API `releases/latest` de `actualizar_repo`
  (por defecto `Piscacho/Gears-of-War-3-PC`), compara `tag_name` con `AppVersion()` ("1.0.0"), descarga el primer `.zip`,
  lo descomprime con tar.exe y lo instala junto al exe conservando gow3.ini, userdata, dlc, shadercache, shaders, gow3.log y
  la carpeta `datos=`; exe/DLL en uso → `*.old` (se borran al arrancar: `UpdaterCleanup` en main.cpp); reabre el launcher.
  CMake: updater.cpp + winhttp.
- Launcher, INICIO → ACTUALIZACIONES: estado, botón BUSCAR ACTUALIZACIONES / INSTALAR vX y opción "Buscar actualizaciones al
  abrir" (`actualizar_al_abrir=1`, por defecto sí; en segundo plano y solo avisa si hay versión nueva). La versión de la barra
  lateral sale de `AppVersion()`.
- Pruebas: `GOW3_UPDATE_API=<url>` (servidor local) y `GOW3_UPDATE_AUTOYES=1` (instala sin preguntar, una sola vez).
  Probado de punta a punta con un servidor local (v1.0.0 → v1.0.1): instaló, conservó gow3.ini y la partida, se reabrió y
  borró los `.old`.
- `tools/make_release.ps1`: compila (salvo -NoBuild), make_dist y crea `release\GearsOfWar3-PC-v<ver>-windows.zip` + `.sha256`
  (sin userdata, shaders\*.bin del juego, gow3.ini personal; falla si hay .iso/.xex/.bin/.log). Para cada versión nueva:
  subir `AppVersion()`, make_release y publicar la release `v<ver>` con el zip. Publicado 2026-11-18: https://github.com/Piscacho/Gears-of-War-3-PC (release v1.0.0). El código del repo se armó desde una lista (runtime, tools, config, compat, dist_files, scripts, project/CMakeLists.txt, notas, xenonrecomp_gow3.diff) en una copia aparte: esta carpeta NO es un repo git; para subir cambios de código hay que repetir esa copia o clonar el repo.

## Pantalla negra con la TU, consola, icono y controles de Reloaded (2026-11-18, partida con un amigo)
- **Pantalla negra al arrancar con la TU** (anfitrion y amigo): no era el online ni un cuelgue. GOW3TU espera una pulsacion
  con la pantalla en negro (swaps se queda en 2; con START sigue). `xam.cpp` CurrentButtons: mientras `GpuGetStats().swaps <= 2`
  y han pasado 3 s, pulsa START 200 ms cada 2 s (`GOW3_NOBOOTSTART` lo quita). Probado: TU sin pulsar nada 2 -> 2671 swaps.
- **Sin ventana de consola**: CMake `/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup` (el registro ya iba a gow3.log por freopen;
  run_game.py sigue capturando stdout/stderr). `project(... CXX RC)`.
- **Icono**: `runtime/gow3.ico` (emblema rojo del logo de SteamGridDB, mitad izquierda reflejada para quitar la "G") +
  `runtime/gow3.rc` (icono 1); la ventana del juego lo usa (`gpu_window.cpp` wc.hIcon) y el launcher ya cargaba el icono 1.
- **Teclado como Gears of War: Reloaded** (lista por defecto de shortcutposters.com; PCGamingWiki bloquea): Espacio = A,
  E = X, F = B, R = RB, Mayus = LB (Tac-Com), Tab = BACK (marcador), Esc = START en partida / B en el menu principal
  (`g_frontendMenu`), Retroceso = B. Antes: Mayus = A, Tab = LB, Esc = B, Retroceso = BACK. Launcher (CONTROLES) y LEEME al dia.
- **Menu principal "vacio"** (captura del usuario: fondo + ACEPTAR/VOLVER/OPCIONES sin opciones). Dos cosas distintas:
  1. **TU (GOW3TU) se congela al pulsar START en el titulo**, tambien sin DLC, sin latino y sin enganches XMA
     (`logs/menu_tu*.log`): el hilo principal gira ocupado (lr TU 0x822C1514 / 0x822BC7B0; hilo 82336110 en 0x8240462C).
     PENDIENTE. Mientras tanto la TU es opcional y viene apagada: launcher.cpp pone `GOW3_NOTU=1` salvo `actualizacion=1`
     (opcion de AVANZADO marcada como experimental).
  2. **Base con 31 DLC**: no es cuelgue. Tras START lee los DLC (~1 s cada uno) y las partidas: el menu sale ~60-80 s despues,
     con ratos de 8+ s sin imagen ("LEYENDO PARTIDAS GUARDADAS"). Probado desde dist con la config del usuario (32:9, latino,
     anfitrion): menu completo a los 80 s. Mejora pendiente: acelerar esa lectura.
- Version 1.0.1 (AppVersion) con todo lo anterior: `release\GearsOfWar3-PC-v1.0.1-windows.zip`, aun sin publicar.

## Online como el de Guitar Hero (2026-11-19, peticion: "no tenia que poner ip ni seleccionar anfitrion")
- `runtime/net_gow3.cpp` reescrito (antes: un socket UDP real por socket del juego, difusiones solo a online_direccion,
  anfitrion/invitado elegido en el launcher). Ahora: un unico puerto UDP (3074 o el siguiente libre), sockets del juego = colas
  virtuales; paquete 'G3NP' v1 + tipo + IP virtual (4) + puerto origen (2) + puerto destino (2). Cada copia tiene una IP virtual
  10.64.0.0/10 al azar (XNADDR.ina). Busqueda local: HELLO cada 2 s por multicast 239.255.66.7:37361 + difusion dirigida en
  cada adaptador (LAN, Radmin VPN, ZeroTier; GetAdaptersAddresses) + 255.255.255.255. Internet: UPnP + STUN (de GH) ->
  codigo de amigo de 12 caracteres (IP publica + puerto); GOW3_NET_PEER = codigos o ip[:puerto] (se relee cada 2 s).
  Anfitrion automatico: xam.cpp XSessionCreate -> NetSetHost(flags & 1); el reenlace +100 del puerto de origen solo en invitados.
  Diagnostico: GOW3_NET_NOINET (sin UPnP/STUN), GOW3_NET_NOLAN, GOW3_NET_PORT.
- Launcher, EN LINEA: "Jugar en linea" Activado/Desactivado (online=1; crear/unirse antiguos se migran), "Tu codigo" + COPIAR,
  "Codigos de amigos" (online_amigos, Ctrl+V), nombre y estado (UPnP / sin UPnP / CGNAT / sin internet). El launcher abre la red
  en el mismo proceso (mismo puerto que el juego). La conexion del usuario es CGNAT: por internet, los dos ponen el codigo del otro.
- Probado: dos copias (tools/run_pair.py, ya sin IPs) se encuentran solas en <2 s, el grupo System Link muestra a los dos y
  entran juntos a la Horda (oleada 1, Checkout). Tres nodos (dos pruebas + el launcher abierto) tambien se ven.
- Incompatible con v1.0.1 (protocolo nuevo): todos con la misma version. v1.0.2 lista en release\ (sin publicar).

## Carga de los DLC al pulsar START (2026-11-19)
- Perfil (GOW3_PROFILE=27:5000 GOW3_PROFILEALL=1 con la carpeta de datos del usuario, 31 DLC): hilo principal al 97 % en
  UGameEngine::LoadPackagesFully (sub_8253F388, desde Engine.PackagesToFullyLoadForDLC: 82362B60 > 82363968 > 82363B80 >
  823647C0 > 82364898 > 8253F080). Por cada paquete: LoadPackage (82313260) y luego FObjectIterator (init 822BCF28, ++ 822BCEC8)
  sobre TODO GObjObjects (0x8343D188) subiendo por Outer (+40) -> ~1150 millones de comprobaciones.
- /O2 en el codigo PPC: sin mejora (64 s /O1 vs 66 s /O2 hasta el menu) -> se queda /O1 (`GOW3_PPC_OPT`, variable de CMake).
- hooks_gow3.cpp: ++ nativo solo cuando lr = 0x8253F744 / 0x8253F7EC (paquete en r26): salta al siguiente objeto del paquete,
  con cache por hueco (objeto, Outer directo) -> paquete raiz. GOW3_NODLCFAST = como antes; GOW3_DLCFASTCHECK = compara con
  la cadena entera (1150 M comprobaciones, 0 diferencias). Solo version base (#ifndef GOW3_TU).
- Resultado: menu a los 47 s del arranque (antes 64); fase de DLC 35 s -> 19 s. Lo que queda es la carga real de paquetes
  (FLinkerLoad 822E9188/822EA320/822E9608).

## v1.0.2 publicada (2026-11-19)
- Titulo de la ventana del juego: "Gears of War 3" (GOW3_DEBUGTITLE=1 = el de depuracion con frames/draws).
- Release v1.0.2 en GitHub (online nuevo, carga de DLC, titulo); commit 5c5c932 en el repo. La v1.0.1 sigue publicada
  (no se pidio borrarla). Pendiente: prueba con 3+ jugadores, oleada completa, aviso CGNAT mas claro ("pega tu el codigo de tu
  amigo"), sobreexposicion al empezar la Horda, TU congelada al entrar al menu.

## v1.0.3 (2026-11-19, sin publicar): TU, boton de actualizar como GH, destello de la Horda
- **TU "congelada al entrar al menu": no lo esta.** Con la TU importada (userdata del usuario) y DLC: tras aceptar la
  calibracion el menu sale vacio (fondo + ACEPTAR/VOLVER/OPCIONES) ~50 s con el hilo principal casi parado; luego aparece
  "DESCARGANDO CONTENIDO · Actualizando menus con el contenido descargado", vuelve al titulo y con START entra normal (probado:
  menu y Opciones en espanol). Ojo: el cuelgue de verdad de las pruebas de desarrollo era del entorno (run\userdata sin la TU
  importada -> sin ScriptPatch_GearGame -> GearDownloadableContentManager con la forma antigua de la clase -> la TU lee
  Num=1151242816 en +172 y recorre "para siempre" su lista nueva de .ini en sub_82AE9B90). Para probar la TU: RUN_DATA= la
  carpeta de datos con la TU importada.
- Atajo de los DLC tambien en la TU (tu_remap: 822BCEC8 -> 822CCD10; lr 0x8255022C/0x825502D4; GObjObjects 0x8344D958).
- Pendiente TU: la espera de ~52 s tras la calibracion (sin temporizadores de script; ninguna llamada al sistema en el hueco;
  aparecen ordinales 2493/2494 sin implementar, tambien en la base). La TU sigue apagada por defecto (texto del launcher corregido).
- Launcher: actualizaciones como en GH: AVANZADO -> "ACTUALIZACIONES Y LAUNCHER" (version + repositorio, BUSCAR/INSTALAR,
  buscar al abrir, mostrar launcher); al instalar, dialogo con las novedades de la release; INICIO solo muestra un aviso con
  INSTALAR si hay version nueva. AVANZADO compactado (filas -9).
- Destello al empezar la Horda: no existe ya; la partida empieza con un fundido desde negro (seq_000..007: 0 -> 37) y el
  brillo se mantiene (~58). La nota antigua era de la configuracion x32.
- Carga: lo que queda (~19 s con 31 DLC) es FLinkerLoad (822E9188/822EA320/822E9608); IsTimeLimitExceeded (822E9570) se
  llama muchisimo. No se toco.

- v1.0.3 publicada en GitHub (commit 7b1fbd7). v1.0.2 sigue publicada.

## v1.0.4 (2026-11-20, sin publicar): 3 jugadores, oleada, CGNAT, XUID
- **3 jugadores** (tools/run_trio.py: run, run2, run3 con su userdata; A crea el grupo con net_host.txt, B y C net_join.txt):
  los tres se encuentran solos, grupo System Link "Anfitrion, Invitado2, Invitado1" y los tres en la misma Horda (Checkout).
- **XUID en linea** = hash(nombre + GOW3_NET_IP + nombre del PC): antes solo el nombre -> dos amigos con "Jugador" tenian el
  mismo XUID. (XamContentGetCreator devuelve siempre el XUID actual, asi que las partidas siguen siendo del usuario.)
- **Oleada**: con GOW3_HORDESKIPBUILD=1: Preparar oleada 1 (0:29 -> 0:09) -> enemigos (GearPawn_LocustDrone disparando) ->
  disparos del jugador (municion 300 -> 196, impactos) -> "WAVE 1 FAILED!" con puntuacion y Reiniciar/Menu. Ganarla requiere
  jugar. Puesto de mando: X junto a la base abre UIScene_HordeBuilderContent (menu de construccion); llegar a la bandera
  andando a ciegas no es fiable (mostradores en medio).
- Aviso del launcher con CGNAT: "pide a tu amigo SU codigo y pegalo tu"; sin UPnP: "pega tu el de tu amigo; si el tampoco
  tiene UPnP, los dos".
- Lanzar una prueba justo al terminar otra larga a veces da log vacio (0 lineas): esperar unos segundos y repetir.
