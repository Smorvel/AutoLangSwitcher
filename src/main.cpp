// LangSwitch — переключение раскладки RU <-> EN двойным нажатием Ctrl
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <process.h>
#include <uiautomationclient.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

#include "layout_map.h"

// ---------------------------------------------------------------- константы
static const ULONG_PTR kMarker = 0x4C535743;  // метка наших собственных SendInput-событий
static const UINT WM_TRIGGER = WM_APP + 2;
static const UINT WM_APPLY_MOUSE = WM_APP + 4;

enum PasteMode { PASTE_NEVER = 0, PASTE_AUTO = 1, PASTE_ALWAYS = 2 };

// ---------------------------------------------------------------- настройки
struct Settings {
    bool sound = true;
    bool wholeText = true;     // false -> только последнее слово
    bool switchLayout = true;
    bool reselect = true;
    bool simpleSwitch = true;  // двойной Ctrl без текста просто переключает раскладку
    bool gameMode = true;
    int pasteMode = PASTE_AUTO;
    int doubleCtrlMs = 500;
    int maxBuffer = 1000;
};

static Settings g_cfg;
static std::vector<std::wstring> g_forceList, g_ignoreList, g_gameList;  // имена exe в нижнем регистре
static std::vector<DWORD> g_chatKeys;
static HWND g_hwnd = NULL;
static HHOOK g_kbHook = NULL, g_msHook = NULL;
static HWINEVENTHOOK g_winEvt = NULL;
static std::wstring g_exePath, g_exeDir, g_iniPath;  // g_exeDir заканчивается на '\'

static const wchar_t* kSection = L"LangSwitch";

static std::vector<std::wstring> ParseList(const wchar_t* s) {
    std::vector<std::wstring> v;
    std::wstring cur;
    auto flush = [&]() {
        size_t a = cur.find_first_not_of(L" \t");
        size_t b = cur.find_last_not_of(L" \t");
        if (a != std::wstring::npos) {
            std::wstring t = cur.substr(a, b - a + 1);
            for (auto& c : t) c = (wchar_t)towlower(c);
            v.push_back(t);
        }
        cur.clear();
    };
    for (const wchar_t* p = s; *p; ++p) {
        if (*p == L';' || *p == L',') flush();
        else cur += *p;
    }
    flush();
    return v;
}

static void LoadSettings() {
    const wchar_t* ini = g_iniPath.c_str();
    auto geti = [&](const wchar_t* key, int def) {
        return (int)GetPrivateProfileIntW(kSection, key, def, ini);
    };
    g_cfg.sound = geti(L"Sound", 1) != 0;
    g_cfg.switchLayout = geti(L"SwitchLayout", 1) != 0;
    g_cfg.reselect = geti(L"Reselect", 1) != 0;
    g_cfg.simpleSwitch = geti(L"SimpleSwitch", 1) != 0;
    g_cfg.doubleCtrlMs = geti(L"DoubleCtrlMs", 500);
    g_cfg.maxBuffer = geti(L"MaxBuffer", 1000);
    if (g_cfg.doubleCtrlMs < 100) g_cfg.doubleCtrlMs = 100;
    if (g_cfg.maxBuffer < 10) g_cfg.maxBuffer = 10;

    wchar_t buf[32] = {0};
    GetPrivateProfileStringW(kSection, L"Scope", L"all", buf, 32, ini);
    g_cfg.wholeText = _wcsicmp(buf, L"word") != 0;

    GetPrivateProfileStringW(kSection, L"PasteMode", L"auto", buf, 32, ini);
    if (_wcsicmp(buf, L"never") == 0) g_cfg.pasteMode = PASTE_NEVER;
    else if (_wcsicmp(buf, L"always") == 0) g_cfg.pasteMode = PASTE_ALWAYS;
    else g_cfg.pasteMode = PASTE_AUTO;

    GetPrivateProfileStringW(kSection, L"GameMode", L"auto", buf, 32, ini);
    g_cfg.gameMode = _wcsicmp(buf, L"off") != 0;

    wchar_t lst[1024] = {0};
    GetPrivateProfileStringW(kSection, L"ForceTrack", L"", lst, 1024, ini);
    g_forceList = ParseList(lst);
    GetPrivateProfileStringW(kSection, L"IgnoreProcesses", L"", lst, 1024, ini);
    g_ignoreList = ParseList(lst);
    GetPrivateProfileStringW(kSection, L"GameProcesses", L"", lst, 1024, ini);
    g_gameList = ParseList(lst);

    GetPrivateProfileStringW(kSection, L"GameChatKeys", L"ENTER", lst, 1024, ini);
    g_chatKeys.clear();
    for (const std::wstring& t : ParseList(lst)) {
        if (t == L"enter") g_chatKeys.push_back(VK_RETURN);
        else if (t.size() == 1 && iswalnum(t[0])) g_chatKeys.push_back((DWORD)towupper(t[0]));
    }
    if (g_chatKeys.empty()) g_chatKeys.push_back(VK_RETURN);
}

static void SaveDefaultSettings() {
    const wchar_t* ini = g_iniPath.c_str();
    auto put = [&](const wchar_t* key, const wchar_t* v) {
        WritePrivateProfileStringW(kSection, key, v, ini);
    };
    put(L"Sound", L"1");
    put(L"Scope", L"all");
    put(L"SwitchLayout", L"1");
    put(L"Reselect", L"1");
    put(L"SimpleSwitch", L"1");
    put(L"PasteMode", L"auto");
    put(L"GameMode", L"auto");
    put(L"GameChatKeys", L"ENTER");
    put(L"GameProcesses", L"");
    put(L"ForceTrack", L"");
    put(L"IgnoreProcesses", L"");
    put(L"DoubleCtrlMs", L"500");
    put(L"MaxBuffer", L"1000");
}

// ---------------------------------------------------------------- общее состояние
struct Rec {
    wchar_t ch;
    bool ru;       // в какой раскладке напечатан символ
    bool neutral;  // не зависит от раскладки (цифры numpad)
};

static CRITICAL_SECTION g_cs;
struct Lock {
    Lock() { EnterCriticalSection(&g_cs); }
    ~Lock() { LeaveCriticalSection(&g_cs); }
};

static std::vector<Rec> g_buf;      // что напечатано с момента последнего "сброса"
static bool g_selHint = false;      // есть вероятность, что в приложении что-то выделено
static std::atomic<bool> g_busy(false);
static std::atomic<bool> g_resetAfter(false);

// состояние хуков (используется только главным потоком, где стоят хуки)
static bool g_caps = false, g_capsHeld = false;
static bool g_ctrlDown = false, g_ctrlDirty = false;
static DWORD g_ctrlDownTick = 0, g_lastTap = 0;
static POINT g_downPt = {0, 0}, g_lastDownPt = {0, 0};
static DWORD g_lastDownTime = 0;
static int g_clickCount = 0;

// состояние активного окна
static HWND g_cachedFg = NULL;
static DWORD g_lastFgCheck = 0;
static std::atomic<bool> g_fgIgnored(false);  // программа в IgnoreProcesses
static std::atomic<bool> g_fgForce(false);    // программа в ForceTrack
static std::atomic<bool> g_fgGame(false);     // окно похоже на игру
static bool g_chatMode = false;               // в игре открыт чат
static bool g_mouseWanted = true;

// звук играет в отдельном потоке, чтобы не задерживать обработку ввода
static HANDLE g_soundEvt = NULL, g_quitEvt = NULL, g_soundThread = NULL;

// ---------------------------------------------------------------- утилиты раскладок
static HKL FgLayout() {
    HWND fg = GetForegroundWindow();
    DWORD tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    return GetKeyboardLayout(tid);
}

static WORD PrimaryLang(HKL h) { return PRIMARYLANGID(LOWORD((UINT_PTR)h)); }

static bool IsRuLayoutNow() { return PrimaryLang(FgLayout()) == LANG_RUSSIAN; }

static HKL FindLayout(bool ru) {
    const WORD want = ru ? LANG_RUSSIAN : LANG_ENGLISH;
    const WORD preferred = ru ? 0x0419 : 0x0409;
    int n = GetKeyboardLayoutList(0, NULL);
    if (n <= 0) return NULL;
    std::vector<HKL> v(n);
    GetKeyboardLayoutList(n, v.data());
    HKL found = NULL;
    for (HKL h : v) {
        WORD lid = LOWORD((UINT_PTR)h);
        if (PRIMARYLANGID(lid) == want) {
            if (lid == preferred) return h;
            if (!found) found = h;
        }
    }
    return found;
}

static void ActivateLayout(bool ru) {
    HKL h = FindLayout(ru);
    HWND fg = GetForegroundWindow();
    if (!h || !fg || h == FgLayout()) return;
    PostMessageW(fg, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)h);
}

static void SwitchLayoutTo(bool ru) {
    if (!g_cfg.switchLayout) return;
    ActivateLayout(ru);
}

// ---------------------------------------------------------------- звук
static void PlaySwitchSound() {
    std::wstring wav = g_exeDir + L"switch.wav";
    if (GetFileAttributesW(wav.c_str()) != INVALID_FILE_ATTRIBUTES) {
        PlaySoundW(wav.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        return;
    }
    std::wstring mp3 = g_exeDir + L"switch.mp3";
    if (GetFileAttributesW(mp3.c_str()) != INVALID_FILE_ATTRIBUTES) {
        mciSendStringW(L"close lssnd", NULL, 0, NULL);
        std::wstring cmd = L"open \"" + mp3 + L"\" type mpegvideo alias lssnd";
        if (mciSendStringW(cmd.c_str(), NULL, 0, NULL) == 0) mciSendStringW(L"play lssnd", NULL, 0, NULL);
    }
}

static unsigned __stdcall SoundThread(void*) {
    HANDLE evts[2] = { g_quitEvt, g_soundEvt };
    for (;;) {
        DWORD w = WaitForMultipleObjects(2, evts, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0 + 1) break;
        PlaySwitchSound();
    }
    return 0;
}

static void RequestSound() {
    if (g_cfg.sound && g_soundEvt) SetEvent(g_soundEvt);
}

// ---------------------------------------------------------------- процессы и окна
static std::wstring ProcNameOfWindow(HWND h) {
    DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    if (!pid) return L"";
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return L"";
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = MAX_PATH;
    std::wstring r;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
        r = buf;
        size_t i = r.find_last_of(L"\\/");
        if (i != std::wstring::npos) r = r.substr(i + 1);
        for (auto& c : r) c = (wchar_t)towlower(c);
    }
    CloseHandle(p);
    return r;
}

static bool InList(const std::vector<std::wstring>& l, const std::wstring& name) {
    return std::find(l.begin(), l.end(), name) != l.end();
}

static bool IsBrowserWindow(HWND fg) {
    wchar_t cls[64] = {0};
    if (fg) GetClassNameW(fg, cls, 64);
    return wcsncmp(cls, L"Chrome_WidgetWin", 16) == 0 || wcscmp(cls, L"MozillaWindowClass") == 0;
}

// Окно без рамки, закрывающее весь монитор (полноэкранный режим игры)
static bool IsFullscreenGameWindow(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t cls[64] = {0};
    GetClassNameW(hwnd, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return false;
    if (IsBrowserWindow(hwnd)) return false;  // браузеры и Electron-приложения в полноэкранном режиме - не игры

    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    if ((style & WS_CAPTION) == WS_CAPTION) return false;  // обычное окно с заголовком

    RECT r;
    if (!GetWindowRect(hwnd, &r)) return false;
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return false;
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

// Фокус на элементе, где печатать текст нельзя (список файлов Проводника, дерево папок, рабочий стол)
static bool FocusIsNonText() {
    if (g_fgForce) return false;
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    wchar_t cls[64] = {0};
    GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW")) return true;

    GUITHREADINFO gi;
    ZeroMemory(&gi, sizeof(gi));
    gi.cbSize = sizeof(gi);
    DWORD tid = GetWindowThreadProcessId(fg, NULL);
    if (!GetGUIThreadInfo(tid, &gi) || !gi.hwndFocus) return false;
    GetClassNameW(gi.hwndFocus, cls, 64);
    static const wchar_t* kNonText[] = {
        L"SysListView32", L"DirectUIHWND", L"SysTreeView32", L"NamespaceTreeControl", L"SHELLDLL_DefView"
    };
    for (const wchar_t* c : kNonText)
        if (!wcscmp(cls, c)) return true;
    return false;
}

// Вызывается в главном потоке (из хука клавиатуры и из WinEvent-колбэка).
// Обновляет сведения об активном окне: игнор, игра, принудительное запоминание.
static void RefreshFgState() {
    HWND fg = GetForegroundWindow();
    DWORD now = GetTickCount();
    const bool changed = (fg != g_cachedFg);
    if (!changed && now - g_lastFgCheck < 1000) return;
    g_lastFgCheck = now;

    static bool listed = false;
    if (changed) {
        g_cachedFg = fg;
        bool ign = false, frc = false;
        listed = false;
        if (fg && (!g_ignoreList.empty() || !g_forceList.empty() || !g_gameList.empty())) {
            std::wstring name = ProcNameOfWindow(fg);
            if (!name.empty()) {
                ign = InList(g_ignoreList, name);
                frc = InList(g_forceList, name);
                listed = InList(g_gameList, name);
            }
        }
        g_fgIgnored = ign;
        g_fgForce = frc;
        g_chatMode = false;
        Lock l;
        g_buf.clear();
        g_selHint = false;
    }

    const bool game = !g_fgForce && !g_fgIgnored && (listed || (g_cfg.gameMode && IsFullscreenGameWindow(fg)));
    g_fgGame = game;

    // Перехват мыши нужен только в обычных окнах: в играх он зря нагружает систему
    const bool wantMouse = !g_fgIgnored && !game;
    if (wantMouse != g_mouseWanted) {
        g_mouseWanted = wantMouse;
        PostMessageW(g_hwnd, WM_APPLY_MOUSE, 0, 0);
    }
}

static bool IsChatOpenKey(DWORD vk) {
    return std::find(g_chatKeys.begin(), g_chatKeys.end(), vk) != g_chatKeys.end();
}

// ---------------------------------------------------------------- SendInput
static INPUT KeyInput(WORD vk, bool up) {
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (vk == VK_LEFT ? KEYEVENTF_EXTENDEDKEY : 0);
    in.ki.dwExtraInfo = kMarker;
    return in;
}

static INPUT UniInput(wchar_t ch, bool up) {
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = ch;
    in.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
    in.ki.dwExtraInfo = kMarker;
    return in;
}

static void SendBatch(const std::vector<INPUT>& v) {
    const size_t chunk = 64;
    for (size_t i = 0; i < v.size(); i += chunk) {
        UINT cnt = (UINT)((v.size() - i) < chunk ? (v.size() - i) : chunk);
        SendInput(cnt, const_cast<INPUT*>(&v[i]), sizeof(INPUT));
        Sleep(1);
    }
}

static void AddChord(std::vector<INPUT>& v, WORD mod, WORD key) {
    v.push_back(KeyInput(mod, false));
    v.push_back(KeyInput(key, false));
    v.push_back(KeyInput(key, true));
    v.push_back(KeyInput(mod, true));
}

static void AddReselect(std::vector<INPUT>& v, size_t n) {
    v.push_back(KeyInput(VK_SHIFT, false));
    for (size_t i = 0; i < n; ++i) {
        v.push_back(KeyInput(VK_LEFT, false));
        v.push_back(KeyInput(VK_LEFT, true));
    }
    v.push_back(KeyInput(VK_SHIFT, true));
}

static void SendChord(WORD mod, WORD key) {
    std::vector<INPUT> v;
    AddChord(v, mod, key);
    SendBatch(v);
}

// ---------------------------------------------------------------- буфер обмена
static bool OpenClip() {
    for (int i = 0; i < 20; ++i) {
        if (OpenClipboard(g_hwnd)) return true;
        Sleep(10);
    }
    return false;
}

struct ClipItem {
    UINT fmt;
    std::vector<char> data;
};

struct ClipBackup {
    std::vector<ClipItem> items;

    bool Save() {
        if (!OpenClip()) return false;
        UINT f = 0;
        while ((f = EnumClipboardFormats(f)) != 0) {
            if (f == CF_BITMAP || f == CF_PALETTE || f == CF_METAFILEPICT || f == CF_ENHMETAFILE) continue;
            if (f >= 0x80 && f <= 0x8F) continue;
            if (f >= 0x300 && f <= 0x3FF) continue;
            HANDLE h = GetClipboardData(f);
            if (!h) continue;
            SIZE_T sz = GlobalSize(h);
            if (sz == 0) continue;
            const char* p = (const char*)GlobalLock(h);
            if (!p) continue;
            ClipItem it;
            it.fmt = f;
            it.data.assign(p, p + sz);
            GlobalUnlock(h);
            items.push_back(std::move(it));
        }
        CloseClipboard();
        return true;
    }

    void Restore() {
        if (!OpenClip()) return;
        EmptyClipboard();
        for (const ClipItem& it : items) {
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, it.data.size());
            if (!g) continue;
            void* p = GlobalLock(g);
            if (!p) { GlobalFree(g); continue; }
            memcpy(p, it.data.data(), it.data.size());
            GlobalUnlock(g);
            if (!SetClipboardData(it.fmt, g)) GlobalFree(g);
        }
        CloseClipboard();
    }
};

static bool ReadClipText(std::wstring& out) {
    if (!OpenClip()) return false;
    bool ok = false;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            out = p;
            GlobalUnlock(h);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

static bool WriteClipText(const std::wstring& s) {
    if (!OpenClip()) return false;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!g) { CloseClipboard(); return false; }
    void* p = GlobalLock(g);
    if (!p) { GlobalFree(g); CloseClipboard(); return false; }
    memcpy(p, s.c_str(), bytes);
    GlobalUnlock(g);
    bool ok = SetClipboardData(CF_UNICODETEXT, g) != NULL;
    if (!ok) GlobalFree(g);
    CloseClipboard();
    return ok;
}

// ---------------------------------------------------------------- определение адресной строки браузера
// true, если фокус стоит в текстовом поле интерфейса браузера (адресная строка, поиск по странице),
// а не внутри веб-страницы. Веб-страница в UI Automation всегда имеет предка типа "Document".
// COM в потоке должен быть уже инициализирован.
static bool FocusIsBrowserUiEdit() {
    bool result = false;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* focused = NULL;
    IUIAutomationTreeWalker* walker = NULL;

#ifdef _MSC_VER
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IUIAutomation), (void**)&uia);
#else
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation, (void**)&uia);
#endif

    if (SUCCEEDED(hr) && uia && SUCCEEDED(uia->GetFocusedElement(&focused)) && focused) {
        CONTROLTYPEID type = 0;
        focused->get_CurrentControlType(&type);
        if (type == UIA_EditControlTypeId || type == UIA_ComboBoxControlTypeId) {
            if (SUCCEEDED(uia->get_ControlViewWalker(&walker)) && walker) {
                bool inDocument = false;
                IUIAutomationElement* cur = focused;
                cur->AddRef();
                for (int i = 0; i < 40 && cur; ++i) {
                    IUIAutomationElement* parent = NULL;
                    if (FAILED(walker->GetParentElement(cur, &parent)) || !parent) {
                        cur->Release();
                        cur = NULL;
                        break;
                    }
                    cur->Release();
                    cur = parent;
                    CONTROLTYPEID pt = 0;
                    cur->get_CurrentControlType(&pt);
                    if (pt == UIA_DocumentControlTypeId) { inDocument = true; break; }
                }
                if (cur) cur->Release();
                result = !inDocument;
            }
        }
    }

    if (walker) walker->Release();
    if (focused) focused->Release();
    if (uia) uia->Release();
    return result;
}

// ---------------------------------------------------------------- конвертация
static bool UsePasteForBuffer() {
    if (g_cfg.pasteMode == PASTE_NEVER) return false;
    if (g_cfg.pasteMode == PASTE_ALWAYS) return true;

    // auto: только адресная строка браузеров (Chrome, Edge, Opera, Firefox...).
    // Её подсказки/автодополнение реагируют на каждый Backspace и символ, из-за этого текст "печатается" медленно.
    if (!IsBrowserWindow(GetForegroundWindow())) return false;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool r = FocusIsBrowserUiEdit();
    if (SUCCEEDED(hr)) CoUninitialize();
    return r;
}

static bool ConvertSelection() {
    HWND fg = GetForegroundWindow();
    wchar_t cls[64] = {0};
    if (fg) GetClassNameW(fg, cls, 64);
    // В консолях Ctrl+C прерывает программу — там выделение не трогаем
    if (!wcscmp(cls, L"ConsoleWindowClass") || !wcscmp(cls, L"CASCADIA_HOSTING_WINDOW_CLASS")) {
        Lock l;
        g_selHint = false;
        return false;
    }

    ClipBackup bak;
    bool saved = bak.Save();

    DWORD seq0 = GetClipboardSequenceNumber();
    SendChord(VK_CONTROL, 'C');
    DWORD t0 = GetTickCount();
    while (GetClipboardSequenceNumber() == seq0 && GetTickCount() - t0 < 500) Sleep(10);

    { Lock l; g_selHint = false; }

    if (GetClipboardSequenceNumber() == seq0) return false;  // ничего не скопировалось

    std::wstring text;
    if (!ReadClipText(text) || text.empty()) {
        if (saved) bak.Restore();
        return false;
    }

    std::wstring conv = lm::ConvertSelection(text, IsRuLayoutNow());
    if (conv == text) {
        if (saved) bak.Restore();
        return true;
    }

    WriteClipText(conv);

    bool re = g_cfg.reselect && conv.size() <= 5000 && conv.find_first_of(L"\r\n") == std::wstring::npos;
    std::vector<INPUT> in;
    AddChord(in, VK_CONTROL, 'V');
    if (re) AddReselect(in, conv.size());
    SendBatch(in);

    Sleep(350);  // даём приложению время забрать текст из буфера
    if (saved) bak.Restore();
    if (re) { Lock l; g_selHint = true; }

    RequestSound();

    bool endRu = IsRuLayoutNow();
    for (size_t i = conv.size(); i-- > 0;) {
        if (lm::IsLetter(conv[i])) { endRu = lm::IsCyrillic(conv[i]); break; }
    }
    SwitchLayoutTo(endRu);
    return true;
}

static void ConvertBuffer(const std::vector<Rec>& snap) {
    if (snap.empty()) return;

    size_t start = 0;
    if (!g_cfg.wholeText) {
        size_t i = snap.size();
        while (i > 0 && snap[i - 1].ch == L' ') --i;
        while (i > 0 && snap[i - 1].ch != L' ') --i;
        start = i;
    }

    std::vector<Rec> neu;
    std::wstring newText;
    for (size_t i = start; i < snap.size(); ++i) {
        Rec r = snap[i];
        if (!r.neutral) {
            if (r.ru) { r.ch = lm::RuToEn(r.ch); r.ru = false; }
            else      { r.ch = lm::EnToRu(r.ch); r.ru = true; }
        }
        neu.push_back(r);
        newText += r.ch;
    }
    const size_t n = snap.size() - start;

    bool done = false;

    // Способ 1 (только адресная строка браузера): выделить текст и заменить одной вставкой — выглядит мгновенно
    if (UsePasteForBuffer()) {
        ClipBackup bak;
        bool saved = bak.Save();
        if (WriteClipText(newText)) {
            std::vector<INPUT> in;
            AddReselect(in, n);                 // Shift+Left x n (выделение набранного текста)
            AddChord(in, VK_CONTROL, 'V');      // вставка нового текста поверх выделения
            SendBatch(in);
            Sleep(350);
            done = true;
        }
        if (saved) bak.Restore();
    }

    // Способ 2 (везде остальное): Backspace x n и ввод Unicode-символов
    if (!done) {
        std::vector<INPUT> in;
        for (size_t i = 0; i < n; ++i) {
            in.push_back(KeyInput(VK_BACK, false));
            in.push_back(KeyInput(VK_BACK, true));
        }
        for (wchar_t ch : newText) {
            in.push_back(UniInput(ch, false));
            in.push_back(UniInput(ch, true));
        }
        SendBatch(in);
    }

    {
        Lock l;
        if (g_resetAfter || g_buf.size() != snap.size()) {
            g_buf.clear();
        } else {
            g_buf.resize(start);
            g_buf.insert(g_buf.end(), neu.begin(), neu.end());
        }
    }

    RequestSound();

    bool endRu = neu.back().ru;
    for (size_t i = neu.size(); i-- > 0;) {
        if (!neu[i].neutral && lm::IsLetter(neu[i].ch)) { endRu = neu[i].ru; break; }
    }
    SwitchLayoutTo(endRu);
}

// Текста нет — просто переключаем раскладку RU <-> EN (из третьей раскладки переходим на русскую)
static void ToggleLayout() {
    ActivateLayout(!IsRuLayoutNow());
    RequestSound();
}

static unsigned __stdcall ConvertThread(void*) {
    Sleep(30);  // чтобы физическое отпускание Ctrl успело обработаться

    bool hint;
    std::vector<Rec> snap;
    {
        Lock l;
        hint = g_selHint;
        snap = g_buf;
        g_resetAfter = false;
    }

    // Ctrl+C не отправляем, если фокус на списке файлов и т.п. (иначе скопируются файлы)
    if (hint && FocusIsNonText()) hint = false;

    bool done = false;
    if (hint) done = ConvertSelection();
    if (!done) {
        if (snap.empty()) {
            if (g_cfg.simpleSwitch) ToggleLayout();
        } else {
            ConvertBuffer(snap);
        }
    }

    g_busy = false;
    return 0;
}

// ---------------------------------------------------------------- хуки
static bool IsModifierVk(DWORD vk) {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
            return true;
    }
    return false;
}

static LRESULT CALLBACK KbProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return CallNextHookEx(NULL, code, wp, lp);
    const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
    if (k->dwExtraInfo == kMarker) return CallNextHookEx(NULL, code, wp, lp);  // наши собственные события

    RefreshFgState();
    const bool ignored = g_fgIgnored;

    const bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
    const DWORD vk = k->vkCode;

    if (vk == VK_CAPITAL) {
        if (down) { if (!g_capsHeld) { g_capsHeld = true; g_caps = !g_caps; } }
        else g_capsHeld = false;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    // ---- Ctrl: ловим двойное "одиночное" нажатие
    if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL) {
        if (down) {
            if (!g_ctrlDown) {
                g_ctrlDown = true;
                g_ctrlDirty = false;
                g_ctrlDownTick = k->time;
            }
        } else {
            if (g_ctrlDown && !g_ctrlDirty && !ignored && (k->time - g_ctrlDownTick) <= 400) {
                if (g_lastTap != 0 && (k->time - g_lastTap) <= (DWORD)g_cfg.doubleCtrlMs) {
                    g_lastTap = 0;
                    PostMessageW(g_hwnd, WM_TRIGGER, 0, 0);
                } else {
                    g_lastTap = k->time;
                }
            } else {
                g_lastTap = 0;
            }
            g_ctrlDown = false;
        }
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (down) {
        if (g_ctrlDown) g_ctrlDirty = true;
        g_lastTap = 0;
    }

    if (!down || ignored) return CallNextHookEx(NULL, code, wp, lp);

    if (g_busy) {
        if (!IsModifierVk(vk)) g_resetAfter = true;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (IsModifierVk(vk)) return CallNextHookEx(NULL, code, wp, lp);

    const bool ctrl = g_ctrlDown;

    if (g_fgGame) {
        // Игра: ничего не запоминаем, пока не открыт чат
        if (!g_chatMode) {
            if (!ctrl && IsChatOpenKey(vk)) {
                g_chatMode = true;
                Lock l;
                g_buf.clear();
                g_selHint = false;
            }
            return CallNextHookEx(NULL, code, wp, lp);
        }
    } else if (FocusIsNonText()) {
        // Список файлов Проводника, рабочий стол и т.п. — не запоминаем
        Lock l;
        g_buf.clear();
        g_selHint = false;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const bool win = ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
    const bool nav = (vk >= VK_PRIOR && vk <= VK_DOWN);

    // Горячие клавиши: сбрасываем буфер. Ctrl+A и Ctrl/Shift+стрелки — вероятное выделение.
    if (ctrl || alt || win) {
        Lock l;
        g_buf.clear();
        g_selHint = ctrl && !alt && !win && (vk == 'A' || (shift && nav));
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (vk == VK_BACK) {
        Lock l;
        if (g_selHint) { g_buf.clear(); g_selHint = false; }
        else if (!g_buf.empty()) g_buf.pop_back();
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (vk == VK_RETURN || vk == VK_ESCAPE) {
        if (g_fgGame) g_chatMode = false;  // чат в игре закрыт
    }

    if (vk == VK_RETURN || vk == VK_TAB || vk == VK_ESCAPE || vk == VK_DELETE || vk == VK_INSERT ||
        (vk >= VK_F1 && vk <= VK_F24)) {
        Lock l;
        g_buf.clear();
        g_selHint = false;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (nav) {
        Lock l;
        g_buf.clear();
        g_selHint = shift;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    // ---- обычная клавиша: узнаём, какой символ она даёт в текущей раскладке
    HKL hkl = FgLayout();
    BYTE ks[256];
    ZeroMemory(ks, sizeof(ks));
    if (shift) ks[VK_SHIFT] = 0x80;
    if (g_caps) ks[VK_CAPITAL] = 0x01;
    wchar_t out[8] = {0};
    int n = ToUnicodeEx((UINT)vk, k->scanCode, ks, out, 8, 0x4, hkl);  // 0x4 = не менять состояние dead keys
    if (n != 1 || out[0] < 0x20 || out[0] == 0x7F) return CallNextHookEx(NULL, code, wp, lp);

    const WORD lang = PrimaryLang(hkl);
    const bool isRu = (lang == LANG_RUSSIAN);
    const bool isEn = (lang == LANG_ENGLISH);

    Lock l;
    if (g_selHint) { g_buf.clear(); g_selHint = false; }  // набор заменил выделенный текст
    if (!isRu && !isEn) { g_buf.clear(); return CallNextHookEx(NULL, code, wp, lp); }

    Rec r;
    r.ch = out[0];
    r.ru = isRu;
    r.neutral = (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE);
    g_buf.push_back(r);
    if ((int)g_buf.size() > g_cfg.maxBuffer) {
        g_buf.erase(g_buf.begin(), g_buf.begin() + (g_buf.size() - (size_t)g_cfg.maxBuffer));
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static LRESULT CALLBACK MsProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION || wp == WM_MOUSEMOVE) return CallNextHookEx(NULL, code, wp, lp);
    const MSLLHOOKSTRUCT* m = (const MSLLHOOKSTRUCT*)lp;
    if (m->dwExtraInfo == kMarker || g_busy) return CallNextHookEx(NULL, code, wp, lp);

    switch (wp) {
        case WM_LBUTTONDOWN: {
            if (g_ctrlDown) g_ctrlDirty = true;
            g_lastTap = 0;
            g_downPt = m->pt;
            const int dx = abs(m->pt.x - g_lastDownPt.x), dy = abs(m->pt.y - g_lastDownPt.y);
            if (g_lastDownTime != 0 && (m->time - g_lastDownTime) <= GetDoubleClickTime() &&
                dx <= GetSystemMetrics(SM_CXDOUBLECLK) && dy <= GetSystemMetrics(SM_CYDOUBLECLK))
                g_clickCount++;
            else
                g_clickCount = 1;
            g_lastDownTime = m->time;
            g_lastDownPt = m->pt;
            Lock l;
            g_buf.clear();
            g_selHint = false;
            break;
        }
        case WM_LBUTTONUP: {
            const bool drag = abs(m->pt.x - g_downPt.x) > 3 || abs(m->pt.y - g_downPt.y) > 3;
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            if ((drag || g_clickCount >= 2 || shift) && !FocusIsNonText()) {
                Lock l;
                g_selHint = true;
            }
            break;
        }
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN: {
            if (g_ctrlDown) g_ctrlDirty = true;
            g_lastTap = 0;
            Lock l;
            g_buf.clear();
            g_selHint = false;
            break;
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

// Событие смены активного окна
static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    RefreshFgState();
}

// ---------------------------------------------------------------- окно (скрытое, нужно для сообщений и буфера обмена)
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_TRIGGER: {
            if (!g_cfg.simpleSwitch) {
                // без текста и без простого переключателя делать нечего
                Lock lk;
                if (g_buf.empty() && !g_selHint) return 0;
            }
            if (!g_busy.exchange(true)) {
                uintptr_t th = _beginthreadex(NULL, 0, ConvertThread, NULL, 0, NULL);
                if (th) CloseHandle((HANDLE)th);
                else g_busy = false;
            }
            return 0;
        }
        case WM_APPLY_MOUSE:
            if (g_mouseWanted && !g_msHook) {
                g_msHook = SetWindowsHookExW(WH_MOUSE_LL, MsProc, GetModuleHandleW(NULL), 0);
            } else if (!g_mouseWanted && g_msHook) {
                UnhookWindowsHookEx(g_msHook);
                g_msHook = NULL;
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- main
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    HANDLE mtx = CreateMutexW(NULL, TRUE, L"Local\\LangSwitch_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    InitializeCriticalSection(&g_cs);

    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    g_exePath = path;
    g_exeDir = g_exePath.substr(0, g_exePath.find_last_of(L"\\/") + 1);
    g_iniPath = g_exeDir + L"LangSwitch.ini";

    if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) SaveDefaultSettings();
    LoadSettings();

    g_caps = (GetKeyState(VK_CAPITAL) & 1) != 0;

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"LangSwitchWnd";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"LangSwitch", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    // Звук: отдельный поток
    g_quitEvt = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_soundEvt = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_soundThread = (HANDLE)_beginthreadex(NULL, 0, SoundThread, NULL, 0, NULL);

    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, GetModuleHandleW(NULL), 0);
    g_msHook = SetWindowsHookExW(WH_MOUSE_LL, MsProc, GetModuleHandleW(NULL), 0);
    if (!g_kbHook) {
        MessageBoxW(NULL, L"Не удалось установить перехват клавиатуры.", L"LangSwitch", MB_ICONERROR);
        return 1;
    }

    g_winEvt = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    RefreshFgState();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_winEvt) UnhookWinEvent(g_winEvt);
    UnhookWindowsHookEx(g_kbHook);
    if (g_msHook) UnhookWindowsHookEx(g_msHook);
    SetEvent(g_quitEvt);
    if (g_soundThread) {
        WaitForSingleObject(g_soundThread, 1000);
        CloseHandle(g_soundThread);
    }
    DeleteCriticalSection(&g_cs);
    if (mtx) CloseHandle(mtx);
    return 0;
}
