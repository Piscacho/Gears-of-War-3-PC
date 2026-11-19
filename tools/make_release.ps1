# Release para GitHub (GoW3 sesion 5, como la de Guitar Hero sesion 24):
#   <Out>\GearsOfWar3-PC-v<ver>-windows.zip (+ .sha256)  para subir como asset de la release v<ver>
# El launcher compara la etiqueta de la ultima release (vX.Y.Z) con AppVersion() de runtime\updater.cpp: subir esa version antes.
# Uso (desde la raiz del proyecto): powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 [-Out <carpeta>] [-NoBuild]
param([string]$Out = "", [switch]$NoBuild)
$ErrorActionPreference = "Stop"
$R = Split-Path $PSScriptRoot -Parent
if (-not $Out) { $Out = "$R\release" }
$name = "GearsOfWar3"
$ver = [regex]::Match((Get-Content "$R\runtime\updater.cpp" -Raw), 'AppVersion\(\) \{ return "([0-9.]+)"').Groups[1].Value
if (-not $ver) { throw "No se encontro AppVersion() en runtime\updater.cpp" }
Write-Host "Release v$ver"

if (-not $NoBuild) {
    cmd /c "`"$R\tools\build_game.bat`"" | Out-Null; if ($LASTEXITCODE -ne 0) { throw "La compilacion de GOW3.exe fallo" }
    cmd /c "`"$R\tools\build_game_tu.bat`"" | Out-Null; if ($LASTEXITCODE -ne 0) { throw "La compilacion de GOW3TU.exe fallo" }
}
cmd /c "`"$R\tools\make_dist.bat`"" | Out-Null; if ($LASTEXITCODE -ne 0) { throw "make_dist fallo" }

# Carpeta limpia: el programa sin datos personales ni nada del juego
$stage = "$R\release_staging\$name"
Remove-Item "$R\release_staging" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage, "$stage\dlc" | Out-Null
Get-ChildItem "$R\dist\$name" | Where-Object { @("userdata", "shadercache", "shaders", "dlc", "gow3.ini", "gow3.log") -notcontains $_.Name } |
    ForEach-Object { Copy-Item $_.FullName $stage -Recurse }
# gow3.ini de la release: sin rutas personales y con el actualizador activado
$ini = Get-Content "$R\dist_files\gow3.ini" -Encoding UTF8 | Where-Object { $_ -notmatch '^(iso|actualizar_al_abrir|actualizar_repo)=' }
$ini = @('iso=') + $ini + @('; ---- Actualizaciones ----', '; actualizar_al_abrir: 1 = el launcher busca versiones nuevas en GitHub al abrirse', 'actualizar_al_abrir=1',
       '; actualizar_repo: repositorio de GitHub de donde se descargan', 'actualizar_repo=Piscacho/Gears-of-War-3-PC')
[IO.File]::WriteAllLines("$stage\gow3.ini", $ini, (New-Object Text.UTF8Encoding $false))
[IO.File]::WriteAllText("$stage\dlc\LEEME.txt", "Copia aqui tus paquetes de DLC y la actualizacion del titulo de Xbox 360. Se importan solos al arrancar.`r`n", (New-Object Text.UTF8Encoding $false))
$bad = Get-ChildItem $stage -Recurse -Include "*.iso", "*.xex", "*.bin", "*.cso", "*.log", "shot_*", "*.sav" -File
if ($bad) { throw "Archivos que no deben ir en la release: $($bad.FullName -join ', ')" }

New-Item -ItemType Directory -Force $Out | Out-Null
$zip = "$Out\GearsOfWar3-PC-v$ver-windows.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
& "$env:SystemRoot\System32\tar.exe" -a -cf $zip -C "$R\release_staging" $name
if ($LASTEXITCODE -ne 0) { throw "No se pudo crear el zip" }
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path $zip -Leaf)" | Set-Content "$zip.sha256" -Encoding ASCII
Write-Host "Zip:     $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
Write-Host "SHA-256: $hash"
Write-Host "Etiqueta para GitHub: v$ver"
