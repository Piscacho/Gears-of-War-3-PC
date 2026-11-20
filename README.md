# Gears of War 3 para PC

Recompilación estática para PC de **Gears of War 3** (Xbox 360, 4D5308AB) para Windows 10/11 con DirectX 12.

> **No se incluyen archivos del juego.** Necesitas tu propio ISO de Xbox 360 de Gears of War 3.
> Gears of War es una marca de Microsoft. Este proyecto no está afiliado a Microsoft, Xbox Game Studios, The Coalition
> ni Epic Games.

## Descargar y jugar

1. Descarga el `.zip` desde la página de
   [Releases](https://github.com/Piscacho/Gears-of-War-3-PC/releases/latest).
2. Descomprímelo en una carpeta, por ejemplo `C:\Juegos\Gears of War 3`.
3. Abre **`GOW3.exe`**. Si Windows muestra "Windows protegió tu PC", pulsa
   **Más información → Ejecutar de todas formas**.
4. En el launcher elige el ISO y pulsa **JUGAR**.

- **Campaña**, **Horda** (partida privada) y **Bestia**.
- **Horda online por enlace de sistema**, como el online de Guitar Hero: sin anfitrión fijo ni IPs. En red local
  (o Radmin VPN / ZeroTier) os encontráis solos; por internet, con un **código de amigo** (UPnP + STUN). Pasos en `LEEME.txt`.
- **DLC y actualización del título**: se eligen sus carpetas en el launcher y se importan solos al arrancar.
- **Español latino**: con tu disco latino (carpeta con `GearGame\`) se usan sus textos, voces y vídeos narrados.
- **Imagen**: 30 / 60 / 120 / 144 / 165 / 240 fps o sin límite, 32:9 con el HUD centrado, nitidez ajustable.
- **Controles**: mando de Xbox (XInput) o teclado y ratón con la distribución de Gears of War: Reloaded
  (Espacio cubrirse, E usar, F cuerpo a cuerpo, R recargar, Mayús Tac-Com, Esc pausa; lista completa en `LEEME.txt`).
- **Actualizaciones**: el launcher avisa de versiones nuevas (INICIO) y las instala desde AVANZADO → Actualizaciones, conservando
  `gow3.ini` y las partidas (`userdata\`).

### Teclas

F11 o Alt+Intro: pantalla completa · F3: contador de FPS · F12: captura · F10: liberar o capturar el ratón.

### Problemas conocidos

- La primera vez que aparece un efecto puede faltar un instante mientras se compila su shader.
- La Horda STANDARD pide Xbox LIVE Gold: usa PRIVATE (también en línea).
- Con muchos DLC, tras pulsar START el menú principal tarda (unos 30 s con 31 DLC, "Leyendo partidas guardadas"):
  la imagen puede quedarse quieta unos segundos, no cierres el juego.
- La opción "Usar la actualización del título" (AVANZADO) viene apagada. Si la activas y tienes DLC, la primera vez
  el menú tarda ~1 minuto, vuelve a la pantalla de título y hay que pulsar START otra vez.
- El juego en línea es solo por enlace de sistema (no hay Xbox Live).

Para informar de un fallo, adjunta `gow3.log` (el registro de la última partida) y, si es gráfico, la captura de F12.

## Compilar desde el código

El repositorio **no contiene nada del juego**: ni el ISO, ni el XEX, ni el código C++ que XenonRecomp genera a partir
de él (`ppc/`, `ppc_tu/`). Cada persona lo genera desde su propio ISO.

Requisitos:
- Visual Studio 2022 Build Tools (MSVC 14.4x, con CMake y Ninja).
- LLVM 23 para Windows (clang-cl + lld-link) y FFmpeg 8 compartido (`avcodec-62`, `avutil-60`, `swresample-6`).
  Las rutas están al principio de `tools/build_game.bat`, `tools/build_msvc.bat`, `tools/make_dist.bat` y
  `project/CMakeLists.txt`.
- Python 3.8+ y `openssl` en el PATH (para `tools/xex2.py`).

Pasos (desde la raíz del repositorio):

```bat
git clone https://github.com/Piscacho/Gears-of-War-3-PC.git
cd Gears-of-War-3-PC

rem 1. Sacar el ejecutable del ISO y descifrarlo (genera xex\basefile.bin)
python -I tools\xdvdfs.py "<ruta al ISO>" extract default.xex xex\default.xex
python -I tools\xex2.py xex\default.xex xex

rem 2. XenonRecomp con los cambios para este juego
git clone --recursive https://github.com/hedge-dev/XenonRecomp.git
cd XenonRecomp
git checkout ddd128bcca99fe8bfbb99bea583c972351fa6ace
git apply ..\xenonrecomp_gow3.diff
cd ..
tools\build_msvc.bat

rem 3. Generar el código C++ del juego (ppc\)
python -I tools\make_functions.py
mkdir ppc
python tools\run_recomp.py

rem 4. Compilar y crear la release
tools\build_game.bat
powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 -NoBuild
```

La versión con la actualización del título (`GOW3TU.exe`, `ppc_tu\`, `tools\build_game_tu.bat`) se explica en
`NOTAS_SESION*.md`, igual que el historial del trabajo y las variables de diagnóstico `GOW3_*`.

## Licencia

El código de este proyecto (runtime, launcher y herramientas) está bajo licencia MIT (`LICENSE`). Los componentes de
terceros (FFmpeg, XenonRecomp, partes basadas en Xenia) están en `THIRD-PARTY-NOTICES.txt`.
