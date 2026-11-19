# Sesión 1: extracción y mapeo del XEX (Gears of War 3, X360)

Fecha: 2026-11-14. Sin commits (igual que en GH WoR, NG2 y Judgment).
Usamos nuestro propio método: XenonRecomp parcheado + runtime de Judgment/NG2/GH. **No se usa código de ports ajenos**
(ni ReXGlue ni COGBox).
**El objetivo n.º 1 es la Horda online con amigos** (ver "Plan para la red" más abajo).

## Carpetas

- `G:\Gears of war 3 pc\*.7z`: el original. `juego\Gears of War 3 (World) (...).iso` se sacó con el `tar.exe` de
  Windows (libarchive lee 7z, y no hay 7-Zip instalado). **Solo lectura.**
- Trabajo: `tools\` (copiadas de Judgment; jt_finder ya apunta aquí), `xex\`, `config\gow3.toml`, `ref\` (tablas de Xenia, BSD),
  `tu\`, `logs\`, `rea\`.

## Qué se hizo

1. **ISO → default.xex** (`tools/xdvdfs.py`): es XGD3, con la partición en `0x2080000` y 2.597 entradas (`xex/iso_filelist.txt`). Un solo disco.
   - `xex/default.xex`: 21.938.176 bytes, SHA1 `2450fd8e4c3cf4d772cd471785d1e38cad3888ad`.
   - Los datos de UE3 están en `GearGame/CookedXbox360/` (hay paquetes `GEARGAMEHORDE_SF*`, `*_LOC_<idioma>`, `CharTextures.tfc`).
2. **XEX → imagen** (`tools/xex2.py`), con cifrado retail y compresión **basic**.
   - `xex/basefile.bin`: 22.708.224 bytes (imagen 0x15B0000), SHA1 `828c1c55888664999560c74e173179231c9c49c8`.
3. Secciones en `xex/pe_sections.txt`. Imports en `xex/imports_named.txt`: 280 funciones y 293 variables, todas con nombre.
4. **Funciones** (`tools/analyze.py`): **41.369** en `.pdata` (`xex/functions_pdata.txt`).
5. **Huecos** (`tools/pdata_gaps.py`): 6.460 huecos (~207.000 palabras) y **2.750 destinos de `bl` fuera de `.pdata`**
   (en Judgment eran 2.516; es el trabajo principal de la sesión 2).
6. **Tablas de saltos**: XenonAnalyse (el ejecutable compilado en Judgment) encuentra 76 y `jt_finder` otras 29 (13 BYTE, 14 SHRT, 2 ABS).
   El total está en `config/gow3_switch_tables_all.toml`: **105**.
7. **TOML borrador**: `config/gow3.toml`.

## Hallazgos

| Dato | Valor |
|---|---|
| Title ID / media ID | **4D5308AB** / 58122AD6 |
| Versión del XEX | 0xB (disco) |
| Entry point | 0x82CE8EB0 |
| Motor | Unreal Engine 3 (el mismo que Judgment), vídeo Bink |
| `.text` | 0x822A0000, tamaño 0xD291CC |
| BINK / RADCODE | 0x82FC9200 / 0x82FE9E00 |
| `.data` | 0x82FF5C00, tamaño 0x479520 |
| Thunks de import | alrededor de 0x82FC804C…0x82FC88xx |

### Direcciones para XenonRecomp (ya están en `config/gow3.toml`)

| | |
|---|---|
| savegprlr_14 / restgprlr_14 | 0x82CC98C0 / 0x82CC9910 |
| savefpr_14 / restfpr_14 | 0x82CCA180 / 0x82CCA1CC |
| savevmx_14 / restvmx_14 | 0x82CCA280 / 0x82CCA518 |
| savevmx_64 / restvmx_64 | 0x82CCA314 / 0x82CCA5AC (vmx_14 + 0x94, como siempre) |
| longjmp | 0x82CCC3F0 (len 0x314) |
| setjmp | 0x82CCC710 (sin .pdata, 0x2CC). Gancho en `lis r4,0x8347; lwz r0,-0x1434(r4)`, es decir, **0x8346EBCC** |

## Plan para la red (Horda online)

En el runtime de Judgment **toda la red es un stub**: `NetDll_socket`/`bind` devuelven error y `XNetGetEthernetLinkStatus` = sin enlace.
Gears 3 importa justo lo que hace falta para jugar en System Link:
`NetDll_socket/bind/sendto/recvfrom/select/connect/...`, `XNetStartup`, `XNetGetTitleXnAddr`, `XNetXnAddrToInAddr`,
`XNetInAddrToXnAddr`, `XNetRegisterKey/UnregisterKey`, `XNetSetSystemLinkPort`, `XNetQosListen/Lookup`,
`XamSessionCreateHandle`, `XamUserGetXUID`…

Idea (se hará cuando el juego llegue al menú, más o menos en la sesión 5-6):
1. NetDll_* → Winsock real (big endian ↔ host, `sockaddr` de invitado ↔ nativo, `select` con `fd_set` de invitado).
2. XNADDR = IP real del adaptador elegido (LAN, o Radmin VPN/ZeroTier para jugar por internet). XnAddr↔InAddr = identidad.
   `XNetGetEthernetLinkStatus` = conectado.
3. System Link = difusión UDP al puerto de `XNetSetSystemLinkPort`. `XNetQos*` responde con los datos de la partida.
4. `XSessionCreate`/XLiveBase (app 0xFC) en modo System Link, con XUID distintos para cada PC.
5. Lo probaremos con dos instancias en el mismo PC (con IPs distintas o puertos desplazados) antes de pasarlo a los amigos.

Todo esto se podrá llevar luego al port de Judgment (Survival/Horda en System Link).

## Notas

- No hay actualización del título (TU) de Gears 3 en la carpeta. Para el System Link todos tienen que usar la misma versión
  del juego. Si se consigue la TU, se aplica con `xexp_records.py` + `lzxdelta`, como en Judgment.
- Los scripts de `tools/` son nuestros: se ejecutan con `python` normal (con `-I` falla el import de `pe_sections`).

## Próximos pasos (sesión 2)

1. Compilar o copiar el XenonRecomp parcheado de Judgment (`tools/patch_recompiler_gowj.py`).
2. Generar `functions` para los ~2.750 destinos sin `.pdata` (`gap_functions.py` / `make_functions.py`) y repetir con el log.
3. XenonRecomp con `config/gow3.toml` → `ppc/`, y conseguir que compile (CMake + Ninja + clang-cl).
4. Copiar `runtime\` de Judgment a `runtime\` (con `hooks_gow3.cpp` y `kernel_gow3.cpp`) y arrancar el CRT.
