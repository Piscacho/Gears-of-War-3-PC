@echo off
rem Crea la carpeta de distribucion dist\GearsOfWar3: ejecutable, DLL de FFmpeg (LGPL), gow3.ini de ejemplo, LEEME, imagenes del
rem launcher y licencias. No copia nada del juego. Uso (desde la raiz del proyecto): tools\make_dist.bat
setlocal
set R=%~dp0..
set DIST=%R%\dist\GearsOfWar3
if not exist "%DIST%" mkdir "%DIST%"
if not exist "%DIST%\dlc" mkdir "%DIST%\dlc"
if not exist "%DIST%\licenses" mkdir "%DIST%\licenses"
copy /y "%R%\project\build\GOW3.exe" "%DIST%\GOW3.exe" >nul || (echo Falta project\build\GOW3.exe, o esta abierto & exit /b 1)
rem version con la actualizacion del titulo v0x60B: GOW3.exe le cede el paso si la TU esta importada (carpeta Update o dlc)
if exist "%R%\project\build_tu\GOW3TU.exe" copy /y "%R%\project\build_tu\GOW3TU.exe" "%DIST%\GOW3TU.exe" >nul || (echo No se pudo copiar GOW3TU.exe & exit /b 1)
copy /y "%R%\project\build\avcodec-62.dll" "%DIST%" >nul
copy /y "%R%\project\build\avutil-60.dll" "%DIST%" >nul
copy /y "%R%\project\build\swresample-6.dll" "%DIST%" >nul
copy /y "%R%\dist_files\LEEME.txt" "%DIST%" >nul
rem imagenes del launcher (SteamGridDB): launcher\portada.jpg y launcher\logo.png
if not exist "%DIST%\launcher" mkdir "%DIST%\launcher"
copy /y "%R%\dist_files\launcher\*.*" "%DIST%\launcher\" >nul
set GHTC=G:\Guitar Hero Warrior of rock\Recomp\toolchain
if exist "%GHTC%\ffmpeg\LICENSE.txt" copy /y "%GHTC%\ffmpeg\LICENSE.txt" "%DIST%\licenses\FFmpeg-LGPL-3.0.txt" >nul
if exist "%R%\XenonRecomp\LICENSE.md" copy /y "%R%\XenonRecomp\LICENSE.md" "%DIST%\licenses\XenonRecomp-MIT.txt" >nul
if not exist "%DIST%\gow3.ini" copy /y "%R%\dist_files\gow3.ini" "%DIST%" >nul
echo Distribucion creada en %DIST%
endlocal
