// Red de Gears of War 3: sockets (NetDll_*) y XNet para el enlace de sistema.
//
// Sin online (por defecto): sockets locales falsos. La campana es un servidor "?listen" (UIpNetDriver::InitListen abre un socket
// UDP/VDP 0xFE); sin un socket valido LoadMap fallaba con "Error initializing network layer." (sesion 7). sendto descarta,
// recvfrom da WSAEWOULDBLOCK, el enlace Ethernet aparece desconectado.
//
// Con online (online=1 en gow3.ini -> GOW3_NET=1; GoW3 sesion 5, como el online de Guitar Hero): un unico puerto UDP del PC
// (3074, o el siguiente libre) lleva todos los sockets del juego, que son colas virtuales por puerto. Cada copia tiene una
// direccion virtual 10.x.y.z (su XNADDR.ina), asi que nadie tiene que escribir IPs ni elegir anfitrion:
//   - red local: cada 2 s un HELLO por multicast 239.255.66.7:37361 (y difusion) anuncia la direccion virtual y el puerto;
//   - internet: UPnP abre el puerto en el router y STUN da la IP publica -> "codigo de amigo" (IP + puerto, 12 caracteres) que
//     se pone en el launcher del otro (GOW3_NET_PEER = codigos o ip[:puerto] separados por comas); a esos pares se les envia un
//     HELLO cada 2 s (perforacion del NAT si los dos ponen el codigo del otro; con UPnP basta con uno).
// Paquete: 'G3NP' + version + tipo + IP virtual de origen (4) + puerto de origen (2) + puerto de destino (2) + datos.
// Las difusiones del juego (255.255.255.255, x.x.x.255: el "LAN beacon" de UE3) van a todos los pares conocidos.
// Reenlace (sesion 10 de Judgment): el invitado vuelve a enlazar el mismo puerto VDP al viajar (grupo -> lobby -> partida); los
// paquetes rezagados de la conexion vieja confundian al anfitrion. Cada reenlace del invitado usa otro puerto de origen (+100); el
// anfitrion (el que crea la sesion, NetSetHost desde xam.cpp) conserva el suyo.
#include "runtime.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <random>
#include <algorithm>
void TexDirtyTouch(uint32_t addr, uint32_t len);   // texdirty.cpp (sesion 10)
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace
{
constexpr uint8_t kMagic[4] = { 'G', '3', 'N', 'P' };
constexpr uint8_t kVer = 1;
enum : uint8_t { T_DATA = 1, T_HELLO = 2, T_HELLOACK = 3 };
constexpr int kHdr = 14;
constexpr uint16_t kDiscPort = 37361;
constexpr uint32_t kDiscGroup = 0xEFFF4207;   // 239.255.66.7

// sin cachear: el launcher (mismo proceso) puede activarlo o desactivarlo antes de JUGAR
bool NetOn() { const char* m = getenv("GOW3_NET"); return m && *m && strcmp(m, "0") != 0 && strcmp(m, "no") != 0; }
uint64_t NowMs() { return GetTickCount64(); }
uint32_t ParseIp(const char* s)
{
    unsigned a, b, c, d;
    if (!s || sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return 0;
    return (a << 24) | (b << 16) | (c << 8) | d;
}
std::string IpStr(uint32_t ip) { char b[32]; snprintf(b, sizeof(b), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); return b; }
std::string Ep(const sockaddr_in& a) { return IpStr(ntohl(a.sin_addr.s_addr)) + ":" + std::to_string(ntohs(a.sin_port)); }
void Put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
void Put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
uint32_t Get32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint16_t Get16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
bool IsBroadcast(uint32_t ip) { return ip == 0xFFFFFFFFu || (ip & 0xFF) == 0xFF; }
bool PrivateIp(uint32_t ip) { return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 22) == 0x191 || (ip >> 24) == 127 || ip == 0; }

// ---- estado (todo bajo g_m) ----
struct Pkt { uint32_t vip; uint16_t port; std::vector<uint8_t> d; };
struct GuestSock { bool bound = false, vdp = false; uint16_t port = 0, src = 0; std::deque<Pkt> q; };
struct Peer { sockaddr_in ep{}; uint64_t seen = 0; };
std::mutex g_m;
std::condition_variable g_cv;
std::map<uint32_t, GuestSock> g_socks;
uint32_t g_nextSock = 0x100;
std::map<uint32_t, Peer> g_peers;              // IP virtual -> direccion real
std::vector<sockaddr_in> g_manualPeers;        // codigos de amigos
bool g_host = false;
uint32_t g_vip = 0x7F000001;                   // nuestra IP virtual (orden de host)
SOCKET g_mux = INVALID_SOCKET, g_disc = INVALID_SOCKET;
uint16_t g_muxPort = 0;
uint32_t g_lanIp = 0x7F000001;
std::mt19937_64 g_rng{ std::random_device{}() };
thread_local uint32_t t_wsaError = 0;

uint32_t LocalIp()
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    uint32_t ip = 0x7F000001;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(53); inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);
    if (s != INVALID_SOCKET && connect(s, (sockaddr*)&a, sizeof(a)) == 0)
    {
        sockaddr_in me{}; int l = sizeof(me);
        if (getsockname(s, (sockaddr*)&me, &l) == 0) ip = ntohl(me.sin_addr.s_addr);
    }
    if (s != INVALID_SOCKET) closesocket(s);
    return ip;
}
struct Iface { uint32_t ip, bcast; };
std::vector<Iface> LocalIfaces()
{
    std::vector<Iface> out;
    ULONG sz = 16384; std::vector<uint8_t> buf(sz);
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    ULONG fl = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, fl, nullptr, aa, &sz) == ERROR_BUFFER_OVERFLOW) { buf.resize(sz); aa = (IP_ADAPTER_ADDRESSES*)buf.data(); }
    if (GetAdaptersAddresses(AF_INET, fl, nullptr, aa, &sz) != NO_ERROR) return out;
    for (auto* a = aa; a; a = a->Next)
    {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
        {
            uint32_t ip = ntohl(((sockaddr_in*)u->Address.lpSockaddr)->sin_addr.s_addr);
            if ((ip >> 16) == 0xA9FE) continue;   // APIPA
            int pl = u->OnLinkPrefixLength; uint32_t mask = pl <= 0 ? 0 : pl >= 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> pl);
            out.push_back({ ip, (ip & mask) | ~mask });
        }
    }
    return out;
}
void SendRaw(const uint8_t* b, int n, const sockaddr_in& to) { if (g_mux != INVALID_SOCKET) sendto(g_mux, (const char*)b, n, 0, (const sockaddr*)&to, sizeof(to)); }
void SendHello(const sockaddr_in& to, uint8_t type)
{
    uint8_t b[kHdr]{}; memcpy(b, kMagic, 4); b[4] = kVer; b[5] = type; Put32(b + 6, g_vip); Put16(b + 10, g_muxPort);
    SendRaw(b, kHdr, to);
}
void LearnPeer(uint32_t vip, const sockaddr_in& ep)
{
    if (vip == g_vip || !vip) return;
    Peer& p = g_peers[vip];
    bool fresh = p.seen == 0, moved = !fresh && (p.ep.sin_addr.s_addr != ep.sin_addr.s_addr || p.ep.sin_port != ep.sin_port);
    p.ep = ep; p.seen = NowMs();
    if (fresh || moved) LOG("[net] %s par %s en %s", fresh ? "nuevo" : "se movio el", IpStr(vip).c_str(), Ep(ep).c_str());
}

// ---- internet: UPnP y STUN (de net.cpp de Guitar Hero, sesion 27) ----
struct InetState { bool done = false, upnp = false, cgnat = false; uint32_t pubIp = 0; uint16_t pubPort = 0; uint32_t stunIp = 0; uint16_t stunPort = 0; };
InetState g_inet;
std::string g_upnpControl, g_upnpService;
uint8_t g_stunTx[12]{};
std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
bool HttpReq(const std::wstring& url, const wchar_t* verb, const std::wstring& headers, const std::string& body, std::string& out, DWORD& status)
{
    URL_COMPONENTS uc{ sizeof(uc) }; wchar_t host[256] = {}, path[1024] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    HINTERNET ses = WinHttpOpen(L"GOW3PC", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return false;
    WinHttpSetTimeouts(ses, 2000, 2000, 3000, 3000);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, verb, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
    bool ok = req && WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), (DWORD)-1,
                                        body.empty() ? nullptr : (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)
              && WinHttpReceiveResponse(req, nullptr);
    if (ok)
    {
        DWORD sz = sizeof(status); WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
        char b[4096]; DWORD n = 0;
        while (WinHttpReadData(req, b, sizeof(b), &n) && n) out.append(b, n);
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}
std::string XmlTag(const std::string& x, const std::string& tag, size_t from = 0)
{
    size_t a = x.find("<" + tag + ">", from); if (a == std::string::npos) return "";
    a += tag.size() + 2; size_t b = x.find("</" + tag + ">", a);
    return b == std::string::npos ? "" : x.substr(a, b - a);
}
bool UpnpSoap(const char* action, const std::string& args, std::string& out)
{
    std::string body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" + std::string(action) + " xmlns:u=\"" + g_upnpService + "\">" +
        args + "</u:" + action + "></s:Body></s:Envelope>";
    std::wstring hdr = L"Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + Wide(g_upnpService) + L"#" + Wide(action) + L"\"\r\n";
    DWORD st = 0;
    return HttpReq(Wide(g_upnpControl), L"POST", hdr, body, out, st) && st == 200;
}
std::string UpnpMapArgs(int lease)
{
    return "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(g_muxPort) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>"
           "<NewInternalPort>" + std::to_string(g_muxPort) + "</NewInternalPort><NewInternalClient>" + IpStr(g_lanIp) + "</NewInternalClient>"
           "<NewEnabled>1</NewEnabled><NewPortMappingDescription>Gears of War 3 PC</NewPortMappingDescription><NewLeaseDuration>" + std::to_string(lease) + "</NewLeaseDuration>";
}
bool UpnpMap()
{
    std::string out;
    if (UpnpSoap("AddPortMapping", UpnpMapArgs(7200), out)) return true;
    out.clear(); return UpnpSoap("AddPortMapping", UpnpMapArgs(0), out);   // algunos routers solo admiten concesiones permanentes
}
void UpnpUnmap()
{
    if (g_upnpControl.empty() || !g_inet.upnp) return;
    std::string out;
    UpnpSoap("DeletePortMapping", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(g_muxPort) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>", out);
}
// Busca el router (SSDP) y abre el puerto. Devuelve la IP publica que dice el router (1 si no la dice, 0 si no hay UPnP).
uint32_t UpnpDiscover()
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(1900); inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
    std::vector<std::string> locs;
    for (const char* st : { "urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1" })
    {
        std::string m = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + st + "\r\n\r\n";
        sendto(s, m.data(), (int)m.size(), 0, (sockaddr*)&to, sizeof(to));
    }
    uint64_t end = NowMs() + 2500;
    char buf[2048];
    while (NowMs() < end)
    {
        fd_set rs; FD_ZERO(&rs); FD_SET(s, &rs); timeval tv{ 0, 200000 };
        if (select(0, &rs, nullptr, nullptr, &tv) <= 0) continue;
        int n = recv(s, buf, sizeof(buf) - 1, 0); if (n <= 0) continue; buf[n] = 0;
        std::string r = buf, low = r; for (auto& c : low) c = (char)tolower((unsigned char)c);
        size_t k = low.find("\nlocation:"); if (k == std::string::npos) continue;
        size_t a = k + 10; while (a < r.size() && r[a] == ' ') a++;
        size_t b = r.find_first_of("\r\n", a);
        std::string loc = r.substr(a, b - a);
        if (std::find(locs.begin(), locs.end(), loc) == locs.end()) locs.push_back(loc);
    }
    closesocket(s);
    for (auto& loc : locs)
    {
        std::string desc; DWORD st = 0;
        if (!HttpReq(Wide(loc), L"GET", L"", "", desc, st) || st != 200) continue;
        for (const char* svc : { "urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:service:WANPPPConnection:1" })
        {
            size_t k = desc.find(svc); if (k == std::string::npos) continue;
            std::string ctl = XmlTag(desc, "controlURL", k); if (ctl.empty()) continue;
            if (ctl.find("://") == std::string::npos)
            {   // URL relativa: se resuelve con la base de la descripcion
                std::string base = XmlTag(desc, "URLBase");
                if (base.empty()) { size_t p = loc.find('/', loc.find("://") + 3); base = loc.substr(0, p); }
                while (!base.empty() && base.back() == '/') base.pop_back();
                ctl = base + (ctl[0] == '/' ? "" : "/") + ctl;
            }
            g_upnpControl = ctl; g_upnpService = svc;
            if (!UpnpMap()) { LOG("[net] UPnP: el router %s no acepto abrir el puerto", loc.c_str()); continue; }
            std::string out; uint32_t ip = 0;
            if (UpnpSoap("GetExternalIPAddress", "", out)) { in_addr a{}; if (inet_pton(AF_INET, XmlTag(out, "NewExternalIPAddress").c_str(), &a) == 1) ip = ntohl(a.s_addr); }
            return ip ? ip : 1;
        }
    }
    return 0;
}
void SendStun()
{
    static const char* servers[][2] = { { "stun.l.google.com", "19302" }, { "stun.cloudflare.com", "3478" } };
    for (auto& sv : servers)
    {
        addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM; addrinfo* res = nullptr;
        if (getaddrinfo(sv[0], sv[1], &hints, &res) != 0 || !res) continue;
        uint8_t req[20] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42 };
        memcpy(req + 8, g_stunTx, 12);
        sendto(g_mux, (const char*)req, 20, 0, res->ai_addr, (int)res->ai_addrlen);
        freeaddrinfo(res);
    }
}
// Respuesta STUN (RFC 5389) en el puerto propio: XOR-MAPPED-ADDRESS / MAPPED-ADDRESS
bool HandleStun(const uint8_t* d, int n)
{
    if (n < 20 || Get32(d + 4) != 0x2112A442 || memcmp(d + 8, g_stunTx, 12) || Get16(d) != 0x0101) return false;
    int len = std::min<int>(Get16(d + 2), n - 20);
    for (int o = 20; o + 4 <= 20 + len;)
    {
        uint16_t t = Get16(d + o), l = Get16(d + o + 2);
        const uint8_t* v = d + o + 4;
        if ((t == 0x0020 || t == 0x0001) && l >= 8 && v[1] == 1)
        {
            uint16_t port = Get16(v + 2); uint32_t ip = Get32(v + 4);
            if (t == 0x0020) { port ^= 0x2112; ip ^= 0x2112A442; }
            std::lock_guard l2(g_m);
            g_inet.stunIp = ip; g_inet.stunPort = port;
            return true;
        }
        o += 4 + ((l + 3) & ~3);
    }
    return true;
}
void InetThread()
{
    uint32_t ip = UpnpDiscover();
    { std::lock_guard l(g_m); if (ip) { g_inet.upnp = true; if (ip > 1) g_inet.pubIp = ip; g_inet.pubPort = g_muxPort; } }
    if (ip) { LOG("[net] UPnP: puerto UDP %u abierto en el router (IP publica %s)", g_muxPort, IpStr(ip).c_str()); atexit([] { UpnpUnmap(); }); }
    for (int i = 0; i < 12 && !g_inet.stunIp; i++) { if (i % 4 == 0) SendStun(); Sleep(250); }
    {
        std::lock_guard l(g_m);
        if (g_inet.upnp && ip > 1 && PrivateIp(ip)) g_inet.cgnat = true;   // el router tiene una IP privada: hay otro NAT delante
        if (g_inet.stunIp && g_inet.upnp && ip > 1 && g_inet.stunIp != ip) g_inet.cgnat = true;
        if (g_inet.stunIp && (!g_inet.pubIp || g_inet.cgnat)) { g_inet.pubIp = g_inet.stunIp; g_inet.pubPort = g_inet.stunPort; }
        g_inet.done = true;
    }
    LOG("[net] internet: codigo con %s:%u (router UPnP %s, STUN %s:%u)%s", IpStr(g_inet.pubIp).c_str(), g_inet.pubPort, g_inet.upnp ? IpStr(ip).c_str() : "no",
        IpStr(g_inet.stunIp).c_str(), g_inet.stunPort, g_inet.cgnat ? ", CGNAT" : "");
    while (g_inet.upnp) { Sleep(3600 * 1000); UpnpMap(); }   // renovar la concesion cada hora
}

// ---- recepcion ----
// Paquete del juego para el puerto virtual dst: primero el socket cuyo puerto de origen es dst (reenlaces), si no el ultimo
// enlazado a ese puerto del juego
void Deliver(uint32_t vip, uint16_t srcPort, uint16_t dst, const uint8_t* d, int n)
{
    GuestSock* best = nullptr;
    for (auto& [h, s] : g_socks) if (s.bound && s.src == dst) best = &s;
    if (!best) for (auto& [h, s] : g_socks) if (s.bound && s.port == dst) best = &s;
    if (!best) return;
    if (best->q.size() >= 512) best->q.pop_front();
    best->q.push_back({ vip, srcPort, std::vector<uint8_t>(d, d + n) });
    g_cv.notify_all();
}
void HandlePacket(const uint8_t* d, int n, const sockaddr_in& from, bool viaDisc)
{
    if (n < kHdr || memcmp(d, kMagic, 4) || d[4] != kVer) return;
    uint32_t vip = Get32(d + 6);
    std::lock_guard l(g_m);
    if (vip == g_vip) return;
    if (d[5] == T_DATA)
    {
        LearnPeer(vip, from);
        Deliver(vip, Get16(d + 10), Get16(d + 12), d + kHdr, n - kHdr);
    }
    else if (d[5] == T_HELLO || d[5] == T_HELLOACK)
    {
        sockaddr_in ep = from;
        if (viaDisc) ep.sin_port = htons(Get16(d + 10));   // la busqueda local llega de otro puerto: el bueno va en el paquete
        LearnPeer(vip, ep);
        if (d[5] == T_HELLO && !viaDisc) SendHello(from, T_HELLOACK);
    }
}
std::vector<sockaddr_in> ParsePeers(const char* s);
void RecvThread()
{
    std::vector<uint8_t> buf(65536);
    uint64_t lastHello = 0;
    for (;;)
    {
        fd_set rs; FD_ZERO(&rs); FD_SET(g_mux, &rs); if (g_disc != INVALID_SOCKET) FD_SET(g_disc, &rs);
        timeval tv{ 0, 50000 };
        if (select(0, &rs, nullptr, nullptr, &tv) > 0)
        {
            for (SOCKET s : { g_mux, g_disc })
            {
                if (s == INVALID_SOCKET || !FD_ISSET(s, &rs)) continue;
                sockaddr_in from{}; int fl = sizeof(from);
                int n = recvfrom(s, (char*)buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fl);
                if (n <= 0) continue;
                if (s == g_mux && HandleStun(buf.data(), n)) continue;
                HandlePacket(buf.data(), n, from, s == g_disc);
            }
        }
        uint64_t now = NowMs();
        if (now - lastHello >= 2000)
        {
            lastHello = now;
            // busqueda local por cada red del PC (LAN y LAN virtuales como Radmin VPN o ZeroTier): multicast + difusion dirigida
            static std::vector<Iface> ifaces; static uint64_t lastIf = 0;
            if (now - lastIf >= 30000 || ifaces.empty()) { ifaces = LocalIfaces(); lastIf = now; }
            sockaddr_in mc{}; mc.sin_family = AF_INET; mc.sin_port = htons(kDiscPort); mc.sin_addr.s_addr = htonl(kDiscGroup);
            for (auto& f : ifaces)
            {
                in_addr ifa{}; ifa.s_addr = htonl(f.ip);
                setsockopt(g_mux, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ifa, sizeof(ifa));
                SendHello(mc, T_HELLO);
                sockaddr_in bc = mc; bc.sin_addr.s_addr = htonl(f.bcast);
                SendHello(bc, T_HELLO);
            }
            if (ifaces.empty()) SendHello(mc, T_HELLO);
            sockaddr_in bc = mc; bc.sin_addr.s_addr = INADDR_BROADCAST;
            SendHello(bc, T_HELLO);
            std::vector<sockaddr_in> manual = ParsePeers(getenv("GOW3_NET_PEER"));   // el launcher puede cambiarlos
            { std::lock_guard l(g_m); g_manualPeers = manual; }
            for (auto& m : manual) SendHello(m, T_HELLO);
        }
    }
}

// Codigos (o ip[:puerto]) separados por comas/espacios -> direcciones
static const char kB32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
bool CodeDecode(const std::string& code, uint32_t& ip, uint16_t& port)
{
    uint64_t v = 0; int n = 0;
    for (char ch : code)
    {
        char c = (char)toupper((unsigned char)ch);
        if (c == '-' || c == ' ') continue;
        if (c == 'O') c = '0'; if (c == 'I' || c == 'L') c = '1';
        const char* f = strchr(kB32, c); if (!f || !c) return false;
        v = (v << 5) | (uint64_t)(f - kB32); n++;
    }
    if (n != 12) return false;
    uint8_t b[7]; for (int i = 6; i >= 0; i--) { b[i] = (uint8_t)v; v >>= 8; }
    uint8_t c = 0xA5; for (int i = 0; i < 6; i++) c = (uint8_t)((c << 1 | c >> 7) ^ b[i]);
    if (c != b[6]) return false;
    ip = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3]; port = (uint16_t)((b[4] << 8) | b[5]);
    return ip != 0 && port != 0;
}
std::string CodeEncode(uint32_t ip, uint16_t port)
{
    uint8_t b[7] = { (uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip, (uint8_t)(port >> 8), (uint8_t)port, 0 };
    uint8_t c = 0xA5; for (int i = 0; i < 6; i++) c = (uint8_t)((c << 1 | c >> 7) ^ b[i]); b[6] = c;
    uint64_t v = 0; for (int i = 0; i < 7; i++) v = (v << 8) | b[i];   // 56 bits -> 12 x 5 bits
    std::string out;
    for (int i = 11; i >= 0; i--) { out += kB32[(v >> (i * 5)) & 31]; if (i == 8 || i == 4) out += '-'; }
    return out;
}
std::vector<sockaddr_in> ParsePeers(const char* s)
{
    std::vector<sockaddr_in> out;
    std::string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        uint32_t ip = 0; uint16_t port = 3074;
        if (!CodeDecode(cur, ip, port))
        {
            size_t k = cur.find(':'); ip = ParseIp(cur.substr(0, k).c_str());
            if (k != std::string::npos) port = (uint16_t)atoi(cur.c_str() + k + 1);
        }
        if (ip && port) { sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(ip); a.sin_port = htons(port); out.push_back(a); }
        cur.clear();
    };
    for (const char* p = s; p && *p; p++) { char c = *p; if (c == ',' || c == ';' || c == ' ' || c == '\n' || c == '\r' || c == '\t') flush(); else cur += c; }
    flush();
    return out;
}

std::mutex g_openM;
bool g_opened = false;
// Abre el puerto UDP propio, la busqueda local y el hilo de recepcion (tambien desde el launcher: mismo proceso, mismo codigo)
void NetOpen()
{
    std::lock_guard lo(g_openM);
    if (g_opened || !NetOn()) return;
    g_opened = true;
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    g_lanIp = LocalIp();
    for (int i = 0; i < 12; i++) g_stunTx[i] = (uint8_t)g_rng();
    {   // IP virtual 10.64.0.0/10 al azar (sin .0 ni .255: el juego trata x.x.x.255 como difusion)
        uint32_t r = (uint32_t)(g_rng() ^ (uint64_t(GetCurrentProcessId()) << 20) ^ NowMs());
        uint32_t v = 0x0A400000u | (r & 0x003FFFFFu);
        if ((v & 0xFF) == 0 || (v & 0xFF) == 0xFF) v ^= 0x01;
        g_vip = v;
    }
    int base = getenv("GOW3_NET_PORT") ? atoi(getenv("GOW3_NET_PORT")) : 3074;
    if (base <= 1024 || base >= 60000) base = 3074;
    for (int i = 0; i < 16 && g_mux == INVALID_SOCKET; i++)
    {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons((uint16_t)(base + i));
        if (bind(s, (sockaddr*)&a, sizeof(a)) == 0) { g_mux = s; g_muxPort = (uint16_t)(base + i); }
        else closesocket(s);
    }
    if (g_mux == INVALID_SOCKET) { LOG("[net] no se pudo abrir el puerto UDP %d-%d", base, base + 15); return; }
    BOOL on = TRUE; setsockopt(g_mux, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(on));
    int ttl = 1; setsockopt(g_mux, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
    BOOL loop = TRUE; setsockopt(g_mux, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));
    {   // ICMP "puerto inalcanzable" de un par que se fue: que no rompa recvfrom
        BOOL off = FALSE; DWORD br = 0; WSAIoctl(g_mux, _WSAIOW(IOC_VENDOR, 12), &off, sizeof(off), nullptr, 0, &br, nullptr, nullptr);
    }
    // busqueda en la red local: varias copias pueden escuchar el mismo puerto (SO_REUSEADDR) y el grupo multicast
    if (!getenv("GOW3_NET_NOLAN"))
    {
        g_disc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        BOOL reuse = TRUE; setsockopt(g_disc, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
        sockaddr_in d{}; d.sin_family = AF_INET; d.sin_addr.s_addr = INADDR_ANY; d.sin_port = htons(kDiscPort);
        ip_mreq mr{}; mr.imr_multiaddr.s_addr = htonl(kDiscGroup); mr.imr_interface.s_addr = INADDR_ANY;
        if (bind(g_disc, (sockaddr*)&d, sizeof(d)) != 0 || setsockopt(g_disc, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mr, sizeof(mr)) != 0)
        { LOG("[net] busqueda local no disponible (%d)", WSAGetLastError()); closesocket(g_disc); g_disc = INVALID_SOCKET; }
    }
    { std::lock_guard l(g_m); g_manualPeers = ParsePeers(getenv("GOW3_NET_PEER")); }
    LOG("[net] red P2P abierta: IP virtual %s, %s:%u, busqueda local %s, %zu pares por codigo", IpStr(g_vip).c_str(), IpStr(g_lanIp).c_str(), g_muxPort,
        g_disc != INVALID_SOCKET ? "si" : "no", g_manualPeers.size());
    std::thread(RecvThread).detach();
    if (!getenv("GOW3_NET_NOINET")) std::thread(InetThread).detach();
}
bool Online() { NetOpen(); return NetOn() && g_mux != INVALID_SOCKET; }

GuestSock* Find(uint32_t s) { auto it = g_socks.find(s); return it == g_socks.end() ? nullptr : &it->second; }
void ReadAddr(uint32_t a, uint32_t& ip, uint16_t& port) { port = (uint16_t)((g_base[a + 2] << 8) | g_base[a + 3]); ip = Load32(a + 4); }
void WriteAddr(uint32_t a, uint32_t ip, uint16_t port)
{
    memset(g_base + a, 0, 16); g_base[a + 1] = 2; g_base[a + 2] = (uint8_t)(port >> 8); g_base[a + 3] = (uint8_t)port; Store32(a + 4, ip);
}
void FillXnAddr(uint32_t a, uint32_t ip)
{
    memset(g_base + a, 0, 36); Store32(a, ip);
    uint8_t mac[6] = { 0x00, 0x22, (uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip };
    memcpy(g_base + a + 10, mac, 6);
}
} // namespace

// ---- para el launcher y xam.cpp ----
void NetLauncherStart() { NetOpen(); }
// true cuando ya se sabe todo. code = codigo de amigo (vacio sin IP publica); status = texto para el usuario
bool NetLauncherInfo(std::string& code, std::wstring& status)
{
    std::lock_guard l(g_m);
    code.clear();
    if (!NetOn()) { status.clear(); return true; }
    if (g_mux == INVALID_SOCKET) { status = L"No se pudo abrir el puerto UDP de red."; return true; }
    if (!g_inet.done) { status = L"Comprobando la conexión a internet..."; return false; }
    if (g_inet.pubIp) code = CodeEncode(g_inet.pubIp, g_inet.pubPort);
    wchar_t t[400];
    if (!g_inet.pubIp) swprintf(t, 400, L"Sin internet detectado: solo red local (o LAN virtual). Puerto UDP %u.", g_muxPort);
    else if (g_inet.cgnat) swprintf(t, 400, L"Tu proveedor usa CGNAT: puede que no te puedan conectar. Si falla, poned los dos el código del otro o usad Radmin VPN / ZeroTier.");
    else if (g_inet.upnp) swprintf(t, 400, L"Router: puerto UDP %u abierto automáticamente (UPnP). Basta con que tu amigo ponga tu código.", g_muxPort);
    else swprintf(t, 400, L"Router sin UPnP: poned los dos el código del otro (o abre el puerto UDP %u en el router).", g_muxPort);
    status = t;
    return true;
}
// xam.cpp: XSessionCreate con o sin HOST (decide si los reenlaces cambian el puerto de origen)
void NetSetHost(bool host)
{
    std::lock_guard l(g_m);
    if (g_host != host) LOG("[net] este PC es %s de la sesion", host ? "anfitrion" : "invitado");
    g_host = host;
}

// ---------------- sockets ----------------
// socket(caller, af, type, protocol)
GUEST_IMPL(NetDll_socket)
{
    bool online = Online();
    std::lock_guard l(g_m);
    uint32_t id = g_nextSock++;
    GuestSock gs; gs.vdp = ctx.r6.u32 == 254;   // IPPROTO_VDP: conexiones del juego
    g_socks[id] = gs;
    LOG("[net] socket(af %u, tipo %u, proto %u) -> %X%s", ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, id, online ? " (red P2P)" : "");
    ctx.r3.u64 = id;
}
// bind(caller, s, sockaddr*, len)
GUEST_IMPL(NetDll_bind)
{
    uint32_t ip; uint16_t port; ReadAddr(ctx.r5.u32, ip, port);
    if (!port) port = 1000;
    std::lock_guard l(g_m);
    GuestSock* gs = Find(ctx.r4.u32);
    if (!gs) { t_wsaError = WSAENOTSOCK; ctx.r3.u64 = 0xFFFFFFFF; return; }
    gs->port = port; gs->src = port; gs->bound = true;
    if (gs->vdp && !g_host) { static std::map<uint16_t, int> gens; int gen = gens[port]++; if (gen > 0 && gen < 60) gs->src = (uint16_t)(port + 100 * gen); }
    LOG("[net] bind(%X, puerto del juego %u, origen %u)", ctx.r4.u32, port, gs->src);
    ctx.r3.u64 = 0;
}
GUEST_IMPL(NetDll_closesocket)
{
    std::lock_guard l(g_m);
    g_socks.erase(ctx.r4.u32);
    ctx.r3.u64 = 0;
}
GUEST_IMPL(NetDll_ioctlsocket) { ctx.r3.u64 = 0; }   // siempre no bloqueantes
GUEST_IMPL(NetDll_setsockopt) { ctx.r3.u64 = 0; }
GUEST_IMPL(NetDll_getsockopt) { ctx.r3.u64 = 0; }
GUEST_IMPL(NetDll_listen) { ctx.r3.u64 = 0; }
GUEST_IMPL(NetDll_accept) { t_wsaError = WSAEWOULDBLOCK; ctx.r3.u64 = 0xFFFFFFFF; }
GUEST_IMPL(NetDll_connect) { t_wsaError = WSAECONNREFUSED; ctx.r3.u64 = 0xFFFFFFFF; }
// getsockname(caller, s, sockaddr*, int* len)
GUEST_IMPL(NetDll_getsockname)
{
    uint16_t port = 1000;
    uint32_t vip;
    { std::lock_guard l(g_m); if (GuestSock* gs = Find(ctx.r4.u32)) if (gs->port) port = gs->port; vip = g_vip; }
    if (ctx.r5.u32) WriteAddr(ctx.r5.u32, vip, port);
    if (ctx.r6.u32) Store32(ctx.r6.u32, 16);
    ctx.r3.u64 = 0;
}
// sendto(caller, s, buf, len, flags, to, tolen)
GUEST_IMPL(NetDll_sendto)
{
    uint32_t len = ctx.r6.u32;
    if (!Online() || !ctx.r8.u32 || len > 65000) { ctx.r3.u64 = len; return; }
    uint32_t ip; uint16_t port; ReadAddr(ctx.r8.u32, ip, port);
    std::vector<uint8_t> pkt(kHdr + len);
    std::vector<sockaddr_in> dests;
    {
        std::lock_guard l(g_m);
        GuestSock* gs = Find(ctx.r4.u32);
        if (!gs) { t_wsaError = WSAENOTSOCK; ctx.r3.u64 = 0xFFFFFFFF; return; }
        if (!gs->bound) { gs->bound = true; gs->port = gs->src = (uint16_t)(50000 + (ctx.r4.u32 & 0xFFF)); }   // enlace implicito
        memcpy(pkt.data(), kMagic, 4); pkt[4] = kVer; pkt[5] = T_DATA; Put32(pkt.data() + 6, g_vip); Put16(pkt.data() + 10, gs->src); Put16(pkt.data() + 12, port);
        memcpy(pkt.data() + kHdr, g_base + ctx.r5.u32, len);
        if (IsBroadcast(ip) || ip == 0)   // 0.0.0.0: direccion de sesion sin rellenar -> a los pares conocidos
            for (auto& [v, p] : g_peers) dests.push_back(p.ep);
        else if (ip == g_vip) Deliver(g_vip, gs->src, port, g_base + ctx.r5.u32, (int)len);
        else if (auto it = g_peers.find(ip); it != g_peers.end()) dests.push_back(it->second.ep);
    }
    for (auto& d : dests) SendRaw(pkt.data(), (int)pkt.size(), d);
    {   // registro: los primeros envios de cada (socket, destino)
        static std::mutex lm; static std::map<uint64_t, int> seen;
        std::lock_guard l(lm);
        int& k = seen[(uint64_t(ctx.r4.u32) << 48) ^ (uint64_t(ip) << 16) ^ port];
        if (k++ < 3 || (k % 500) == 0)
            LOG("[net] sendto #%d socket %X (%u bytes) a %s:%u%s (%zu destinos)", k, ctx.r4.u32, len, IpStr(ip).c_str(), port, IsBroadcast(ip) || !ip ? " (difusion)" : "", dests.size());
    }
    ctx.r3.u64 = len;
}
GUEST_IMPL(NetDll_send) { ctx.r3.u64 = ctx.r6.u32; }
// recvfrom(caller, s, buf, len, flags, from, fromlen*)
GUEST_IMPL(NetDll_recvfrom)
{
    Pkt p;
    {
        std::lock_guard l(g_m);
        GuestSock* gs = Find(ctx.r4.u32);
        if (!gs || gs->q.empty()) { t_wsaError = WSAEWOULDBLOCK; ctx.r3.u64 = 0xFFFFFFFF; return; }
        p = std::move(gs->q.front()); gs->q.pop_front();
    }
    uint32_t n = std::min<uint32_t>(ctx.r6.u32, (uint32_t)p.d.size());
    TexDirtyTouch(ctx.r5.u32, n);   // sesion 10 (texdirty.cpp)
    memcpy(g_base + ctx.r5.u32, p.d.data(), n);
    if (ctx.r8.u32) WriteAddr(ctx.r8.u32, p.vip, p.port);
    if (ctx.r9.u32) Store32(ctx.r9.u32, 16);
    {
        static std::mutex lm; static std::map<uint64_t, int> seen;
        std::lock_guard l(lm);
        int& k = seen[(uint64_t(ctx.r4.u32) << 48) ^ (uint64_t(p.vip) << 16) ^ p.port];
        if (k++ < 3 || (k % 500) == 0) LOG("[net] recvfrom #%d socket %X (%u bytes) de %s:%u", k, ctx.r4.u32, n, IpStr(p.vip).c_str(), p.port);
    }
    ctx.r3.u64 = n;
}
GUEST_IMPL(NetDll_recv) { t_wsaError = WSAEWOULDBLOCK; ctx.r3.u64 = 0xFFFFFFFF; }
// select(caller, nfds, read, write, except, timeval*): fd_set del invitado = cuenta + 64 sockets (big-endian)
GUEST_IMPL(NetDll_select)
{
    uint32_t rd = ctx.r5.u32, wr = ctx.r6.u32, ex = ctx.r7.u32, tv = ctx.r8.u32;
    int ready = 0;
    if (rd)
    {
        uint32_t n = std::min<uint32_t>(Load32(rd), 64);
        std::vector<uint32_t> ids; for (uint32_t i = 0; i < n; i++) ids.push_back(Load32(rd + 4 + i * 4));
        auto readable = [&] { std::vector<uint32_t> r; for (uint32_t s : ids) if (GuestSock* gs = Find(s)) if (!gs->q.empty()) r.push_back(s); return r; };
        std::unique_lock l(g_m);
        std::vector<uint32_t> r = readable();
        if (r.empty() && tv)
        {
            uint64_t us = (uint64_t)Load32(tv) * 1000000ull + Load32(tv + 4); if (us > 100000) us = 100000;
            if (us) { g_cv.wait_for(l, std::chrono::microseconds(us), [&] { return !readable().empty(); }); r = readable(); }
        }
        Store32(rd, (uint32_t)r.size()); for (size_t i = 0; i < r.size(); i++) Store32(rd + 4 + (uint32_t)i * 4, r[i]);
        ready += (int)r.size();
    }
    if (wr) ready += (int)Load32(wr);
    if (ex) Store32(ex, 0);
    ctx.r3.u64 = (uint32_t)ready;
}
GUEST_IMPL(NetDll_WSAGetLastError) { ctx.r3.u64 = t_wsaError; }
GUEST_IMPL(NetDll_WSASetLastError) { t_wsaError = ctx.r3.u32; ctx.r3.u64 = 0; }
// inet_addr(const char*)
GUEST_IMPL(NetDll_inet_addr) { uint32_t ip = ParseIp((const char*)(g_base + ctx.r3.u32)); ctx.r3.u64 = ip ? ip : 0xFFFFFFFF; }

// ---------------- XNet ----------------
// XNetGetTitleXnAddr(caller, XNADDR*): XNADDR = ina(4) inaOnline(4) wPortOnline(2) abEnet[6] abOnline[20]. Con online, ina = IP virtual
// y abEnet una MAC derivada de ella; XNET_GET_XNADDR_STATIC | ETHERNET.
GUEST_IMPL(NetDll_XNetGetTitleXnAddr)
{
    bool online = Online();
    uint32_t a = ctx.r4.u32;
    if (a) FillXnAddr(a, g_vip);
    ctx.r3.u64 = 0x04 | (online ? 0x01 : 0);
}
// XNetXnAddrToInAddr(caller, XNADDR*, XNKID*, IN_ADDR*): la "IP segura" es la propia ina (IP virtual)
GUEST_IMPL(NetDll_XNetXnAddrToInAddr) { if (ctx.r6.u32 && ctx.r4.u32) Store32(ctx.r6.u32, Load32(ctx.r4.u32)); ctx.r3.u64 = 0; }
// XNetInAddrToXnAddr(caller, ina, XNADDR*, XNKID*)
GUEST_IMPL(NetDll_XNetInAddrToXnAddr)
{
    if (ctx.r5.u32) FillXnAddr(ctx.r5.u32, ctx.r4.u32);
    if (ctx.r6.u32) memset(g_base + ctx.r6.u32, 0, 8);
    ctx.r3.u64 = 0;
}
// XNetRandom(caller, buffer, length): bytes aleatorios (claves de sesion y nonces)
GUEST_IMPL(NetDll_XNetRandom)
{
    static std::mt19937_64 rng{ std::random_device{}() ^ (uint64_t)GetTickCount64() };
    for (uint32_t i = 0; i < ctx.r5.u32; i++) g_base[ctx.r4.u32 + i] = (uint8_t)rng();
    ctx.r3.u64 = 0;
}
GUEST_IMPL(NetDll_XNetConnect) { ctx.r3.u64 = 0; }
GUEST_IMPL(NetDll_XNetRegisterKey) { ctx.r3.u64 = 0; }
GUEST_IMPL(NetDll_XNetUnregisterKey) { ctx.r3.u64 = 0; }
GUEST_IMPL(NetDll_XNetServerToInAddr) { ctx.r3.u64 = 0x80004005; }
GUEST_IMPL(NetDll_XNetQosListen) { ctx.r3.u64 = 0; }
GUEST_IMPL(NetDll_XNetQosRelease) { ctx.r3.u64 = 0; }
GUEST_IMPL(NetDll_XNetQosGetListenStats) { ctx.r3.u64 = 0; }
GUEST_IMPL(NetDll_XNetQosLookup) { ctx.r3.u64 = 0x80004005; }
// XNetGetEthernetLinkStatus(caller): con online, enlace activo a 100 Mb en duplex (ACTIVE | 100MBPS | FULL_DUPLEX)
GUEST_IMPL(NetDll_XNetGetEthernetLinkStatus) { ctx.r3.u64 = Online() ? 0x0B : 0; }

// XNADDR de esta consola (para XSESSION_INFO del anfitrion, xam.cpp)
void XnAddrFill(uint32_t a) { Online(); FillXnAddr(a, g_vip); }
