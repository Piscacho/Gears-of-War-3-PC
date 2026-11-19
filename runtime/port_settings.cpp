// GoW3 sesion 3: ajustes del port que se aplican a los ini del juego (fs.cpp, ApplyIniOverrides) y valores compartidos.
#include "runtime.h"
#include <string>
#include <utility>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

// Tope de fotogramas: GOW3_MAXFPS (gow3.ini: fps=). 0 = sin tope. Por defecto 120.
int PortMaxFps()
{
    static int v = [] { const char* s = getenv("GOW3_MAXFPS"); int f = s ? atoi(s) : 120; return f < 0 ? 120 : f; }();
    return v;
}
// Resolucion de salida del juego (modo de video que ve la consola): GOW3_RES=<ancho>x<alto>. Por defecto 1280x720.
void PortResolution(uint32_t& w, uint32_t& h)
{
    static uint32_t rw = 1280, rh = 720;
    static bool init = [] { if (const char* s = getenv("GOW3_RES")) { unsigned a = 0, b = 0; if (sscanf(s, "%ux%u", &a, &b) == 2 && a >= 640 && b >= 360 && a <= 7680 && b <= 2160) { rw = a; rh = b; } } return true; }();
    (void)init; w = rw; h = rh;
}
std::vector<std::pair<std::string, std::string>> IniOverrides()
{
    std::vector<std::pair<std::string, std::string>> o;
    int fps = PortMaxFps();
    // UE3 limita el bucle con bSmoothFrameRate a MaxSmoothedFrameRate (62 en la 360)
    if (fps == 0) o.push_back({ "Engine.Engine:bSmoothFrameRate", "FALSE" });
    else o.push_back({ "Engine.Engine:MaxSmoothedFrameRate", std::to_string(fps + 2) });
    // GoW3 sesion 5: sin aceleracion de vista (con el raton hacia que los giros rapidos se dispararan x2.5). GOW3_VIEWACCEL=1 la deja.
    if (!getenv("GOW3_VIEWACCEL")) o.push_back({ "GearGame.GearPlayerInput:bViewAccelEnabled", "FALSE" });
    // GoW3 sesion 5 (como Judgment sesion 11): desenfoque de movimiento apagado por defecto: el bufer de velocidades de los personajes
    // falla en el port y los emborrona al moverse con la camara. GOW3_MOTIONBLUR=1 (desenfoque_movimiento=1 en gow3.ini) lo deja.
    if (!getenv("GOW3_MOTIONBLUR")) o.push_back({ "SystemSettings:MotionBlur", "False" });
    uint32_t w, h; PortResolution(w, h);
    if (w != 1280 || h != 720) { o.push_back({ "SystemSettings:ResX", std::to_string(w) }); o.push_back({ "SystemSettings:ResY", std::to_string(h) }); }
    return o;
}
