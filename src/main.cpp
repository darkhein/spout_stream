// spout_stream : capture d'un ecran ou d'une fenetre vers un flux Spout.
//
// Chaine entierement GPU, sans aller-retour CPU :
//   Windows.Graphics.Capture (texture D3D11) -> CopySubresourceRegion -> texture partagee Spout
// Chaque image est envoyee des son arrivee (callback du frame pool, thread libre),
// ce qui minimise la latence.

#include <windows.h>
#include <unknwn.h>
#include <dwmapi.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <psapi.h>
#include <io.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include "SpoutDX.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

// ---------------------------------------------------------------------------
// Utilitaires
// ---------------------------------------------------------------------------

static std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Les noms de sender Spout sont des chaines ANSI (char[256])
static std::string ToAnsi(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring ToLower(std::wstring s)
{
    if (!s.empty()) CharLowerBuffW(s.data(), (DWORD)s.size());
    return s;
}

static std::wstring WindowTitle(HWND hwnd)
{
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring t(len + 1, L'\0');
    t.resize(GetWindowTextW(hwnd, t.data(), len + 1));
    return t;
}

static std::wstring ProcessName(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    std::wstring name;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &size)) {
            name = path;
            name = name.substr(name.find_last_of(L'\\') + 1);
        }
        CloseHandle(h);
    }
    return name;
}

static void Log(const char* fmt, ...)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

struct Options {
    enum class Mode { None, Screen, Window, List, Version, Help } mode = Mode::None;
    int screenIndex = -1;          // -1 = ecran principal
    std::wstring windowTitle;
    bool exact = false;            // correspondance exacte du titre (sinon sous-chaine, insensible a la casse)
    bool clientOnly = false;       // rogne a la zone client (sans barre de titre ni bordures)
    bool cursor = true;
    bool border = false;           // cadre jaune de capture Windows
    bool blankOnLoss = true;       // envoie une image noire quand la source disparait
    int pollMs = 500;
    std::wstring senderName = L"SpoutStream";
};

static void PrintUsage()
{
    printf(
        "spout_stream - diffuse un ecran ou une fenetre en flux Spout\n"
        "\n"
        "Usage :\n"
        "  spout_stream --screen [N]          [options]\n"
        "  spout_stream --window \"titre\"      [options]\n"
        "  spout_stream --list\n"
        "\n"
        "Source :\n"
        "  --screen [N]        Capture l'ecran N (voir --list). Sans N : ecran principal.\n"
        "  --window \"titre\"    Capture la premiere fenetre dont le titre contient \"titre\"\n"
        "                      (insensible a la casse). Si la fenetre n'existe pas encore,\n"
        "                      le programme l'attend ; si elle est fermee, il attend sa reouverture.\n"
        "  --list              Liste les ecrans et fenetres disponibles.\n"
        "\n"
        "Options :\n"
        "  --name \"nom\"        Nom du flux Spout (defaut : SpoutStream).\n"
        "  --exact             Le titre de fenetre doit correspondre exactement.\n"
        "  --client            Fenetre : ne diffuse que la zone client (sans barre de titre).\n"
        "  --no-cursor         Ne capture pas le curseur de la souris.\n"
        "  --border            Affiche le cadre jaune de capture de Windows.\n"
        "  --keep-last         Garde la derniere image quand la source disparait (defaut : noir).\n"
        "  --poll MS           Intervalle de recherche de la fenetre en ms (defaut : 500).\n"
        "  --version           Affiche la version.\n"
        "  -h, --help          Affiche cette aide.\n");
}

static bool IsNumber(const wchar_t* s)
{
    if (!s || !*s) return false;
    for (; *s; ++s)
        if (!iswdigit(*s)) return false;
    return true;
}

static bool ParseArgs(int argc, wchar_t** argv, Options& o)
{
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&](const char* opt) -> const wchar_t* {
            if (i + 1 >= argc) {
                printf("Erreur : %s attend une valeur.\n", opt);
                return nullptr;
            }
            return argv[++i];
        };
        if (a == L"-h" || a == L"--help" || a == L"/?") {
            o.mode = Options::Mode::Help;
            return true;
        }
        else if (a == L"--screen") {
            o.mode = Options::Mode::Screen;
            if (i + 1 < argc && IsNumber(argv[i + 1]))
                o.screenIndex = _wtoi(argv[++i]);
        }
        else if (a == L"--window") {
            auto v = next("--window");
            if (!v) return false;
            o.mode = Options::Mode::Window;
            o.windowTitle = v;
        }
        else if (a == L"--list") {
            o.mode = Options::Mode::List;
        }
        else if (a == L"--version") {
            o.mode = Options::Mode::Version;
            return true;
        }
        else if (a == L"--name") {
            auto v = next("--name");
            if (!v) return false;
            o.senderName = v;
        }
        else if (a == L"--exact")      o.exact = true;
        else if (a == L"--client")     o.clientOnly = true;
        else if (a == L"--no-cursor")  o.cursor = false;
        else if (a == L"--border")     o.border = true;
        else if (a == L"--keep-last")  o.blankOnLoss = false;
        else if (a == L"--poll") {
            auto v = next("--poll");
            if (!v || !IsNumber(v)) return false;
            o.pollMs = std::max(50, _wtoi(v));
        }
        else {
            printf("Option inconnue : %s\n\n", ToUtf8(a).c_str());
            return false;
        }
    }
    if (o.mode == Options::Mode::None) {
        printf("Erreur : indiquez --screen, --window ou --list.\n\n");
        return false;
    }
    if (o.mode == Options::Mode::Window && o.windowTitle.empty()) {
        printf("Erreur : titre de fenetre vide.\n\n");
        return false;
    }
    if (o.senderName.empty() || o.senderName.size() >= 256) {
        printf("Erreur : le nom Spout doit faire entre 1 et 255 caracteres.\n\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Enumeration des sources
// ---------------------------------------------------------------------------

struct MonitorInfo {
    HMONITOR handle;
    std::wstring device;
    RECT rect;
    bool primary;
};

static std::vector<MonitorInfo> EnumMonitors()
{
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hm, HDC, LPRECT, LPARAM p) -> BOOL {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hm, &mi)) {
            reinterpret_cast<std::vector<MonitorInfo>*>(p)->push_back(
                { hm, mi.szDevice, mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0 });
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&list));
    return list;
}

static HMONITOR FindMonitor(int index)
{
    auto list = EnumMonitors();
    if (index < 0) {
        for (auto& m : list)
            if (m.primary) return m.handle;
        return nullptr;
    }
    return index < (int)list.size() ? list[index].handle : nullptr;
}

// Fenetre "reelle" visible par l'utilisateur (meme filtre que le selecteur de capture Windows)
static bool IsCapturableWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd)
        return false;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() || hwnd == GetConsoleWindow())
        return false;
    return GetWindowTextLengthW(hwnd) > 0;
}

static std::vector<HWND> EnumCapturableWindows()
{
    std::vector<HWND> list;
    EnumWindows([](HWND hwnd, LPARAM p) -> BOOL {
        if (IsCapturableWindow(hwnd))
            reinterpret_cast<std::vector<HWND>*>(p)->push_back(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&list));
    return list;
}

static HWND FindTargetWindow(const Options& o)
{
    std::wstring wanted = ToLower(o.windowTitle);
    for (HWND hwnd : EnumCapturableWindows()) {
        std::wstring title = ToLower(WindowTitle(hwnd));
        if (o.exact ? title == wanted : title.find(wanted) != std::wstring::npos)
            return hwnd;
    }
    return nullptr;
}

static void ListSources()
{
    printf("Ecrans (--screen N) :\n");
    auto monitors = EnumMonitors();
    for (size_t i = 0; i < monitors.size(); ++i) {
        auto& m = monitors[i];
        printf("  %zu : %s  %ldx%ld @ (%ld,%ld)%s\n", i, ToUtf8(m.device).c_str(),
            m.rect.right - m.rect.left, m.rect.bottom - m.rect.top,
            m.rect.left, m.rect.top, m.primary ? "  [principal]" : "");
    }
    printf("\nFenetres (--window \"titre\") :\n");
    for (HWND hwnd : EnumCapturableWindows()) {
        printf("  %-24s %s\n", ToUtf8(ProcessName(hwnd)).c_str(), ToUtf8(WindowTitle(hwnd)).c_str());
    }
}

// ---------------------------------------------------------------------------
// Sortie Spout (partagee entre les sessions de capture successives)
// ---------------------------------------------------------------------------

struct SpoutOutput {
    spoutDX spout;
    std::mutex mutex;  // spoutDX et le contexte immediat D3D11 ne sont pas thread-safe
};

// ---------------------------------------------------------------------------
// Session de capture Windows.Graphics.Capture
// ---------------------------------------------------------------------------

class CaptureSession : public std::enable_shared_from_this<CaptureSession> {
public:
    static std::shared_ptr<CaptureSession> Start(const wd3d::IDirect3DDevice& device, SpoutOutput& out,
                                                 const Options& opt, HWND hwnd, HMONITOR monitor)
    {
        auto self = std::shared_ptr<CaptureSession>(new CaptureSession(device, out, opt, hwnd));
        self->Init(monitor);
        return self;
    }

    bool IsLost() const
    {
        return m_closed || (m_hwnd && !IsWindow(m_hwnd));
    }

    // Appele par le proprietaire (thread principal) avant la destruction
    void Stop()
    {
        if (!m_pool) return;
        try { m_pool.FrameArrived(m_frameToken); } catch (...) {}
        try { m_item.Closed(m_closedToken); } catch (...) {}
        {
            // Attend la fin d'un eventuel envoi en cours
            std::lock_guard lock(m_out.mutex);
            m_active = false;
        }
        m_session.Close();
        m_pool.Close();
        m_session = nullptr;
        m_pool = nullptr;
        m_item = nullptr;
    }

    winrt::Windows::Graphics::SizeInt32 Size() const { return m_lastSize; }

private:
    CaptureSession(const wd3d::IDirect3DDevice& device, SpoutOutput& out, const Options& opt, HWND hwnd)
        : m_device(device), m_out(out), m_opt(opt), m_hwnd(hwnd) {}

    void Init(HMONITOR monitor)
    {
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        if (m_hwnd) {
            winrt::check_hresult(interop->CreateForWindow(m_hwnd,
                winrt::guid_of<wgc::IGraphicsCaptureItem>(), winrt::put_abi(m_item)));
        }
        else {
            winrt::check_hresult(interop->CreateForMonitor(monitor,
                winrt::guid_of<wgc::IGraphicsCaptureItem>(), winrt::put_abi(m_item)));
        }

        m_lastSize = m_item.Size();
        // 2 tampons : le minimum pour que la capture ne bloque pas pendant qu'on lit une image
        m_pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            m_device, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_lastSize);
        m_session = m_pool.CreateCaptureSession(m_item);

        // Proprietes optionnelles selon la version de Windows
        try { m_session.IsCursorCaptureEnabled(m_opt.cursor); } catch (...) {}
        try { m_session.IsBorderRequired(m_opt.border); } catch (...) {}
        // Supprime la limitation de cadence par defaut (Windows 11 24H2+)
        try { m_session.MinUpdateInterval(std::chrono::milliseconds(1)); } catch (...) {}

        std::weak_ptr<CaptureSession> weak = weak_from_this();
        m_frameToken = m_pool.FrameArrived([weak](auto const& pool, auto const&) {
            if (auto self = weak.lock()) self->OnFrameArrived(pool);
        });
        m_closedToken = m_item.Closed([weak](auto const&, auto const&) {
            if (auto self = weak.lock()) self->m_closed = true;
        });

        m_active = true;
        m_session.StartCapture();
    }

    void OnFrameArrived(wgc::Direct3D11CaptureFramePool const& pool)
    {
        std::lock_guard lock(m_out.mutex);
        if (!m_active) return;

        // Ne garde que l'image la plus recente
        wgc::Direct3D11CaptureFrame frame{ nullptr };
        while (auto f = pool.TryGetNextFrame()) {
            if (frame) frame.Close();
            frame = f;
        }
        if (!frame) return;

        auto contentSize = frame.ContentSize();
        if (contentSize.Width != m_lastSize.Width || contentSize.Height != m_lastSize.Height) {
            m_lastSize = contentSize;
            pool.Recreate(m_device, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, contentSize);
        }

        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        if (FAILED(access->GetInterface(IID_PPV_ARGS(texture.put())))) {
            frame.Close();
            return;
        }

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        // Zone utile : le contenu peut etre plus petit que la texture (redimensionnement en cours)
        LONG x = 0, y = 0;
        LONG w = std::min<LONG>(contentSize.Width, desc.Width);
        LONG h = std::min<LONG>(contentSize.Height, desc.Height);

        if (m_opt.clientOnly && m_hwnd)
            ClientRegion(x, y, w, h);

        if (w > 0 && h > 0)
            m_out.spout.SendTexture(texture.get(), (UINT)x, (UINT)y, (UINT)w, (UINT)h);

        frame.Close();
    }

    // La texture capturee correspond aux limites DWM de la fenetre (sans l'ombre) :
    // on en deduit la position de la zone client.
    void ClientRegion(LONG& x, LONG& y, LONG& w, LONG& h) const
    {
        RECT frameRect{}, client{};
        POINT origin{ 0, 0 };
        if (FAILED(DwmGetWindowAttribute(m_hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect, sizeof(frameRect)))
            || !GetClientRect(m_hwnd, &client) || !ClientToScreen(m_hwnd, &origin))
            return;

        LONG cx = std::max<LONG>(0, origin.x - frameRect.left);
        LONG cy = std::max<LONG>(0, origin.y - frameRect.top);
        LONG cw = std::min<LONG>(client.right, w - cx);
        LONG ch = std::min<LONG>(client.bottom, h - cy);
        if (cw > 0 && ch > 0) {
            x = cx; y = cy; w = cw; h = ch;
        }
    }

    wd3d::IDirect3DDevice m_device;
    SpoutOutput& m_out;
    const Options& m_opt;
    HWND m_hwnd;

    wgc::GraphicsCaptureItem m_item{ nullptr };
    wgc::Direct3D11CaptureFramePool m_pool{ nullptr };
    wgc::GraphicsCaptureSession m_session{ nullptr };
    winrt::event_token m_frameToken{}, m_closedToken{};
    winrt::Windows::Graphics::SizeInt32 m_lastSize{};
    bool m_active = false;  // protege par m_out.mutex
    std::atomic<bool> m_closed{ false };
};

// ---------------------------------------------------------------------------
// Arret propre (Ctrl+C, fermeture de la console)
// ---------------------------------------------------------------------------

static HANDLE g_stopEvent = nullptr;
static HANDLE g_doneEvent = nullptr;

static BOOL WINAPI ConsoleHandler(DWORD type)
{
    SetEvent(g_stopEvent);
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT)
        WaitForSingleObject(g_doneEvent, 3000);  // le processus est tue au retour du handler
    return TRUE;
}

// ---------------------------------------------------------------------------
// Programme principal
// ---------------------------------------------------------------------------

static void SendBlackFrame(SpoutOutput& out)
{
    std::lock_guard lock(out.mutex);
    if (!out.spout.IsInitialized()) return;
    UINT w = out.spout.GetWidth(), h = out.spout.GetHeight();
    std::vector<unsigned char> black((size_t)w * h * 4, 0);
    for (size_t i = 3; i < black.size(); i += 4) black[i] = 255;  // alpha opaque
    out.spout.SendImage(black.data(), w, h);
}

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    // Coordonnees en pixels physiques (necessaire pour --client sur ecrans HiDPI)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    Options opt;
    if (!ParseArgs(argc, argv, opt)) {
        PrintUsage();
        return 1;
    }
    if (opt.mode == Options::Mode::Help) {
        PrintUsage();
        return 0;
    }
    if (opt.mode == Options::Mode::Version) {
        printf("spout_stream %s\n", APP_VERSION);
        return 0;
    }
    if (opt.mode == Options::Mode::List) {
        ListSources();
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    if (!wgc::GraphicsCaptureSession::IsSupported()) {
        printf("Erreur : Windows.Graphics.Capture n'est pas disponible (Windows 10 1903+ requis).\n");
        return 1;
    }

    // Le titre de la console contient sinon la ligne de commande, donc le titre recherche
    SetConsoleTitleW((L"spout_stream - " + opt.senderName).c_str());

    // Autorise la capture sans cadre jaune (Windows 11)
    if (!opt.border) {
        try {
            wgc::GraphicsCaptureAccess::RequestAccessAsync(wgc::GraphicsCaptureAccessKind::Borderless).get();
        } catch (...) {}
    }

    // Peripherique D3D11 partage entre la capture et Spout
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, d3dDevice.put(), nullptr, d3dContext.put());
    if (FAILED(hr)) {
        printf("Erreur : creation du peripherique D3D11 impossible (0x%08lX).\n", hr);
        return 1;
    }
    // La capture utilise le peripherique depuis ses propres threads
    if (auto mt = d3dDevice.try_as<ID3D11Multithread>())
        mt->SetMultithreadProtected(TRUE);

    wd3d::IDirect3DDevice device{ nullptr };
    {
        auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
        device = inspectable.as<wd3d::IDirect3DDevice>();
    }

    SpoutOutput out;
    out.spout.OpenDirectX11(d3dDevice.get());
    std::string spoutName = ToAnsi(opt.senderName);
    out.spout.SetSenderName(spoutName.c_str());

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    if (opt.mode == Options::Mode::Screen)
        Log("Flux Spout \"%s\" - source : ecran %s", ToUtf8(opt.senderName).c_str(),
            opt.screenIndex < 0 ? "principal" : std::to_string(opt.screenIndex).c_str());
    else
        Log("Flux Spout \"%s\" - source : fenetre \"%s\"%s", ToUtf8(opt.senderName).c_str(),
            ToUtf8(opt.windowTitle).c_str(), opt.exact ? " (titre exact)" : "");
    Log("Ctrl+C pour quitter.");

    std::shared_ptr<CaptureSession> capture;
    HWND failedHwnd = nullptr;   // evite de reessayer en boucle une fenetre non capturable
    bool waitingLogged = false;

    do {
        if (capture && capture->IsLost()) {
            capture->Stop();
            capture.reset();
            Log("Source perdue.");
            if (opt.blankOnLoss) SendBlackFrame(out);
            waitingLogged = false;
        }
        if (capture) continue;

        HWND hwnd = nullptr;
        HMONITOR monitor = nullptr;
        if (opt.mode == Options::Mode::Window) {
            hwnd = FindTargetWindow(opt);
            if (hwnd != failedHwnd) failedHwnd = nullptr;
            else hwnd = nullptr;
        }
        else {
            monitor = FindMonitor(opt.screenIndex);
        }

        if (hwnd || monitor) {
            try {
                capture = CaptureSession::Start(device, out, opt, hwnd, monitor);
                auto size = capture->Size();
                if (hwnd)
                    Log("Capture de la fenetre \"%s\" (%s) %dx%d", ToUtf8(WindowTitle(hwnd)).c_str(),
                        ToUtf8(ProcessName(hwnd)).c_str(), size.Width, size.Height);
                else
                    Log("Capture de l'ecran %dx%d", size.Width, size.Height);
            }
            catch (winrt::hresult_error const& e) {
                Log("Echec de la capture : %s (0x%08X)", ToUtf8(e.message().c_str()).c_str(), (unsigned)e.code());
                if (capture) capture->Stop();
                capture.reset();
                failedHwnd = hwnd;
            }
        }
        else if (!waitingLogged) {
            Log(opt.mode == Options::Mode::Window ? "En attente de la fenetre..." : "En attente de l'ecran...");
            waitingLogged = true;
        }
    } while (WaitForSingleObject(g_stopEvent, opt.pollMs) == WAIT_TIMEOUT);

    Log("Arret.");
    if (capture) capture->Stop();
    capture.reset();
    {
        std::lock_guard lock(out.mutex);
        out.spout.ReleaseSender();
    }
    SetEvent(g_doneEvent);
    return 0;
}
