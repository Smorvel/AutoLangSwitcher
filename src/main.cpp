// LangSwitch — переключение раскладки (латиница <-> кириллица) двойным нажатием Ctrl
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
#include <oleauto.h>
#include <mmsystem.h>
#include <process.h>
#include <uiautomationclient.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "layout_map.h"

#ifdef _MSC_VER
#define LS_IID(T) __uuidof(T)
#else
#define LS_IID(T) IID_##T
#endif

// ---------------------------------------------------------------- константы
static const ULONG_PTR kMarker = 0x4C535743;   // метка наших собственных SendInput-событий
static const UINT WM_TRIGGER = WM_APP + 2;
static const UINT WM_PLAYSOUND = WM_APP + 3;
static const UINT_PTR kTimerChat = 1;          // проверка "открылся ли чат" после клавиши чата в игре
static const DWORD kChatCheckMs = 250;
static const DWORD kChatIdleMs = 4000;         // пауза в наборе, после которой чат в игре считается закрытым
static const size_t kMaxUiaSelection = 5000;   // длиннее — читаем выделение обычным копированием
static const DWORD kSelChainMs = 120000;       // сколько живёт цепочка повторных конвертаций выделения

// ---------------------------------------------------------------- настройки
struct Settings {
    bool sound = true;
    bool wholeText = true;     // false -> только последнее слово
    bool switchLayout = true;
    bool reselect = true;
    bool gameFilter = true;    // особый режим для игр (полноэкранные окна, скрытый курсор)
    int doubleCtrlMs = 500;
    int maxBuffer = 1000;
};

static Settings g_cfg;
static HWND g_hwnd = NULL;
static HHOOK g_kbHook = NULL, g_msHook = NULL;
static std::wstring g_exeDir, g_iniPath;  // g_exeDir заканчивается на '\'

// форматы буфера обмена, запрещающие попадание в историю / облако / менеджеры
static UINT g_fmtHistory = 0, g_fmtCloud = 0, g_fmtMonitor = 0;

static const wchar_t* kSection = L"LangSwitch";

static void LoadSettings() {
    const wchar_t* ini = g_iniPath.c_str();
    auto geti = [&](const wchar_t* key, int def) {
        return (int)GetPrivateProfileIntW(kSection, key, def, ini);
    };
    g_cfg.sound = geti(L"Sound", 1) != 0;
    g_cfg.switchLayout = geti(L"SwitchLayout", 1) != 0;
    g_cfg.reselect = geti(L"Reselect", 1) != 0;
    g_cfg.gameFilter = geti(L"GameFilter", 1) != 0;
    g_cfg.doubleCtrlMs = geti(L"DoubleCtrlMs", 500);
    g_cfg.maxBuffer = geti(L"MaxBuffer", 1000);
    if (g_cfg.doubleCtrlMs < 100) g_cfg.doubleCtrlMs = 100;
    if (g_cfg.maxBuffer < 10) g_cfg.maxBuffer = 10;

    wchar_t buf[32] = {0};
    GetPrivateProfileStringW(kSection, L"Scope", L"all", buf, 32, ini);
    g_cfg.wholeText = _wcsicmp(buf, L"word") != 0;
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
    put(L"GameFilter", L"1");
    put(L"DoubleCtrlMs", L"500");
    put(L"MaxBuffer", L"1000");
}

// ---------------------------------------------------------------- общее состояние
struct Rec {
    wchar_t ch;      // текущий символ (после конвертаций)
    wchar_t origCh;  // символ в том виде, в каком он был набран
    HKL home;        // раскладка, в которой символ был набран
    HKL cur;         // раскладка, в которой символ находится сейчас
    bool neutral;    // не зависит от раскладки (цифры numpad)
};

// Установленные раскладки по алфавитам. Порядок: сначала последняя использованная, затем остальные по кругу.
struct Cycle {
    std::vector<HKL> lat;
    std::vector<HKL> cyr;
};

static CRITICAL_SECTION g_cs;
struct Lock {
    Lock() { EnterCriticalSection(&g_cs); }
    ~Lock() { LeaveCriticalSection(&g_cs); }
};

static std::vector<Rec> g_buf;      // что напечатано с момента последнего "сброса"
static bool g_chainActive = false;  // после последней конвертации ничего не набирали: повторное нажатие продолжает цепочку
static Cycle g_chainCycle;          // порядок раскладок, замороженный на время цепочки
static bool g_selHint = false;      // есть вероятность, что в приложении что-то выделено
static bool g_selByMouse = false;   // выделение сделано мышью
static bool g_selForce = false;     // выделение сделано мышью при зажатом Ctrl — точно текст, проверки не нужны
static POINT g_selDownPt = {0, 0};  // где нажата кнопка мыши при выделении
static POINT g_selUpPt = {0, 0};    // где она отпущена
static std::atomic<bool> g_busy(false);
static std::atomic<bool> g_resetAfter(false);
static std::atomic<DWORD> g_cooldownUntil(0);  // до этого момента (мс) двойной Ctrl не принимается

// Последние использованные раскладки (запоминаются глобально, не по приложениям)
static std::atomic<UINT_PTR> g_lastLat(0);
static std::atomic<UINT_PTR> g_lastCyr(0);

// Раскладка, которую мы переключили сами в окне, не сообщающем о смене раскладки (см. EffectiveLayout)
static std::atomic<UINT_PTR> g_assumedHkl(0);    // 0 — не используется
static std::atomic<UINT_PTR> g_assumedHwnd(0);   // окно, для которого это действует
static std::atomic<UINT_PTR> g_assumedStale(0);  // что окно продолжает сообщать (устаревшая раскладка)

// игровой режим
static std::atomic<bool> g_gameArmed(false);    // в игре идёт набор текста (чат открыт)
static std::atomic<bool> g_gamePending(false);  // нажата клавиша чата, ждём подтверждения по курсору
static std::atomic<DWORD> g_gameLastKey(0);

// состояние хуков (используется только потоком хуков)
static bool g_caps = false, g_capsHeld = false;
static bool g_ctrlDown = false, g_ctrlDirty = false;
static bool g_downWithCtrl = false;
static bool g_keyDown[256] = {false};
static int g_chordMask = 0;        // какие из Shift(1)/Alt(2)/Ctrl(4)/Win(8) нажимались с тех пор, как все были отпущены
static bool g_chordDirty = false;  // и нажималась ли между ними обычная клавиша
static HWND g_lastFg = NULL;
static DWORD g_ctrlDownTick = 0, g_lastTap = 0;
static POINT g_downPt = {0, 0}, g_lastDownPt = {0, 0};
static DWORD g_lastDownTime = 0;
static int g_clickCount = 0;

// Набор продолжили после конвертации: считаем уже сконвертированный текст набранным в его новых раскладках
// (вызывать под Lock)
static void NormalizeChain() {
    for (size_t i = 0; i < g_buf.size(); ++i) {
        g_buf[i].home = g_buf[i].cur;
        g_buf[i].origCh = g_buf[i].ch;
    }
    g_chainActive = false;
}

// "Взвести" набор текста в игре (чат открыт) / сбросить его
static void GameArm(DWORD now) {
    g_gamePending = false;
    g_gameArmed = true;
    g_gameLastKey = now;
    KillTimer(g_hwnd, kTimerChat);
    Lock l;
    g_buf.clear();
}

static void GameDisarm() {
    g_gameArmed = false;
    g_gamePending = false;
    KillTimer(g_hwnd, kTimerChat);
    Lock l;
    g_buf.clear();
}

// Забыть всё набранное и выделенное (например, при смене активного окна)
static void ResetTyping() {
    g_gameArmed = false;
    g_gamePending = false;
    KillTimer(g_hwnd, kTimerChat);
    Lock l;
    g_buf.clear();
    g_selHint = false;
    g_selByMouse = false;
    g_selForce = false;
}

// ---------------------------------------------------------------- раскладки клавиатуры
// Таблицы соответствия строятся из установленных раскладок Windows и кэшируются в памяти.

static CRITICAL_SECTION g_layCs;
struct LayLock {
    LayLock() { EnterCriticalSection(&g_layCs); }
    ~LayLock() { LeaveCriticalSection(&g_layCs); }
};

static std::vector<std::pair<UINT_PTR, int> > g_scriptCache;                 // раскладка -> алфавит
static std::map<std::pair<UINT_PTR, UINT_PTR>, lm::CharMap> g_mapCache;      // (откуда, куда) -> таблица символов
static std::map<UINT_PTR, std::set<wchar_t> > g_charsCache;                  // раскладка -> какие символы она даёт

// Скан-коды клавиш основного блока (` 1..0 - =, QWERTY-ряд, ASDF-ряд, \, ZXCV-ряд) и дополнительной клавиши ISO
static const UINT kScans[] = {0x29, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                              0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
                              0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2B,
                              0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x56};
// Клавиши букв — по ним определяем алфавит раскладки
static const UINT kLetterScans[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
                                    0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                    0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32};

static HKL FgLayout() {
    HWND fg = GetForegroundWindow();
    DWORD tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    return GetKeyboardLayout(tid);
}

// Какой символ даёт клавиша (скан-код) в раскладке h. false — клавиша ничего не печатает.
static bool KeyChar(HKL h, UINT scan, bool shift, bool caps, wchar_t& out) {
    const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK, h);
    if (!vk) return false;
    BYTE ks[256];
    ZeroMemory(ks, sizeof(ks));
    if (shift) ks[VK_SHIFT] = 0x80;
    if (caps) ks[VK_CAPITAL] = 0x01;
    wchar_t buf[8] = {0};
    const int n = ToUnicodeEx(vk, scan, ks, buf, 8, 0x4, h);  // 0x4 = не менять состояние dead keys
    if (n != 1 || buf[0] < 0x20 || buf[0] == 0x7F) return false;
    out = buf[0];
    return true;
}

// Алфавит раскладки: 1 — латиница, 2 — кириллица, 0 — другая (иврит, греческая и т.п.)
static int ScriptOfLayout(HKL h) {
    if (!h) return 0;
    {
        LayLock l;
        for (size_t i = 0; i < g_scriptCache.size(); ++i) {
            if (g_scriptCache[i].first == (UINT_PTR)h) return g_scriptCache[i].second;
        }
    }
    int lat = 0, cyr = 0;
    for (UINT s : kLetterScans) {
        wchar_t c;
        if (KeyChar(h, s, false, false, c)) {
            if (lm::IsLatin(c)) ++lat;
            else if (lm::IsCyrillic(c)) ++cyr;
        }
    }
    const int script = cyr > lat ? 2 : (lat > cyr ? 1 : 0);
    LayLock l;
    g_scriptCache.push_back(std::make_pair((UINT_PTR)h, script));
    return script;
}

// Символы, которые раскладка может напечатать
static const std::set<wchar_t>& LayoutChars(HKL h) {
    LayLock l;
    std::map<UINT_PTR, std::set<wchar_t> >::iterator it = g_charsCache.find((UINT_PTR)h);
    if (it != g_charsCache.end()) return it->second;
    std::set<wchar_t>& s = g_charsCache[(UINT_PTR)h];
    for (UINT scan : kScans) {
        for (int sh = 0; sh < 2; ++sh) {
            wchar_t c;
            if (KeyChar(h, scan, sh != 0, false, c)) s.insert(c);
        }
    }
    return s;
}

// Таблица "символ в раскладке from -> тот же символ на той же клавише в раскладке to"
static const lm::CharMap& MapFor(HKL from, HKL to) {
    LayLock l;
    const std::pair<UINT_PTR, UINT_PTR> key((UINT_PTR)from, (UINT_PTR)to);
    std::map<std::pair<UINT_PTR, UINT_PTR>, lm::CharMap>::iterator it = g_mapCache.find(key);
    if (it != g_mapCache.end()) return it->second;
    lm::CharMap& m = g_mapCache[key];
    for (UINT scan : kScans) {
        for (int sh = 0; sh < 2; ++sh) {
            wchar_t a, b;
            if (KeyChar(from, scan, sh != 0, false, a) && KeyChar(to, scan, sh != 0, false, b)) {
                if (m.find(a) == m.end()) m[a] = b;
            }
        }
    }
    return m;
}

static std::vector<HKL> InstalledLayouts() {
    std::vector<HKL> v;
    int n = GetKeyboardLayoutList(0, NULL);
    if (n <= 0) return v;
    v.resize(n);
    n = GetKeyboardLayoutList(n, v.data());
    v.resize(n > 0 ? n : 0);
    return v;
}

static void RotateTo(std::vector<HKL>& v, UINT_PTR first) {
    for (size_t i = 0; i < v.size(); ++i) {
        if ((UINT_PTR)v[i] == first) {
            std::rotate(v.begin(), v.begin() + i, v.end());
            return;
        }
    }
}

// Установленные латинские и кириллические раскладки; первыми — последние использованные
static Cycle BuildCycle() {
    Cycle c;
    const std::vector<HKL> list = InstalledLayouts();
    for (size_t i = 0; i < list.size(); ++i) {
        const int s = ScriptOfLayout(list[i]);
        if (s == 1) c.lat.push_back(list[i]);
        else if (s == 2) c.cyr.push_back(list[i]);
    }
    RotateTo(c.lat, g_lastLat);
    RotateTo(c.cyr, g_lastCyr);
    return c;
}

// Запомнить раскладку как последнюю использованную в своём алфавите
static void NoteLayoutUsed(HKL h) {
    const int s = ScriptOfLayout(h);
    if (s == 1) g_lastLat = (UINT_PTR)h;
    else if (s == 2) g_lastCyr = (UINT_PTR)h;
}

// В какую раскладку превратить символ, набранный в home и находящийся сейчас в cur.
// Цепочка: home -> раскладки противоположного алфавита по порядку -> снова home.
// При двух раскладках это просто переключение туда и обратно.
static HKL NextTarget(HKL home, HKL cur, const Cycle& cy) {
    const std::vector<HKL>& opp = (ScriptOfLayout(home) == 1) ? cy.cyr : cy.lat;
    if (opp.empty()) return cur;
    if (cur == home) return opp[0];
    for (size_t i = 0; i < opp.size(); ++i) {
        if (opp[i] == cur) return (i + 1 < opp.size()) ? opp[i + 1] : home;
    }
    return opp[0];
}

// Если в системе ровно две раскладки — вторая из них
static HKL OtherLayoutOfTwo(HKL h) {
    const std::vector<HKL> v = InstalledLayouts();
    if (v.size() != 2) return NULL;
    if (v[0] == h) return v[1];
    if (v[1] == h) return v[0];
    return NULL;
}

// Раскладка, в которой на самом деле идёт набор.
// Обычно это то, что сообщает окно. Но некоторые приложения (например, Epic Games Launcher) не сообщают о смене
// раскладки: после нашего переключения они продолжают называть старую. Тогда используем то, что мы переключили сами,
// пока окно не начнёт сообщать что-то другое.
static HKL EffectiveLayout() {
    const HKL reported = FgLayout();
    const UINT_PTR a = g_assumedHkl;
    if (a != 0) {
        if (g_assumedHwnd == (UINT_PTR)GetForegroundWindow() && g_assumedStale == (UINT_PTR)reported) return (HKL)a;
        g_assumedHkl = 0;  // окно сменилось или снова сообщает актуальную раскладку — доверяем ему
    }
    return reported;
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

// Символ вводим как при ручном наборе: пробел — настоящей клавишей (Unicode-события с пробелом
// некоторые приложения теряют), остальное — Unicode-событием
static void AddTypedChar(std::vector<INPUT>& v, wchar_t ch) {
    if (ch == L' ') {
        v.push_back(KeyInput(VK_SPACE, false));
        v.push_back(KeyInput(VK_SPACE, true));
    } else {
        v.push_back(UniInput(ch, false));
        v.push_back(UniInput(ch, true));
    }
}

static void AddTypedText(std::vector<INPUT>& v, const std::wstring& s) {
    for (wchar_t ch : s) AddTypedChar(v, ch);
}

// Делит текст на пробелы в начале, середину и пробелы в конце
static void SplitEdgeSpaces(const std::wstring& s, std::wstring& lead, std::wstring& core, std::wstring& trail) {
    size_t b = 0, e = s.size();
    while (b < e && s[b] == L' ') ++b;
    while (e > b && s[e - 1] == L' ') --e;
    lead = s.substr(0, b);
    core = s.substr(b, e - b);
    trail = s.substr(e);
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

// ---------------------------------------------------------------- переключение раскладки
// Эмуляция системной горячей клавиши смены раскладки (Alt+Shift / Ctrl+Shift / `)

// Тип горячей клавиши из настроек Windows: 1 = Alt+Shift, 2 = Ctrl+Shift, 3 = отключена, 4 = клавиша ` (Ё)
static int ReadSystemHotkeyType() {
    int type = 1;  // по умолчанию в Windows — Alt+Shift
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Keyboard Layout\\Toggle", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        const wchar_t* names[2] = {L"Language Hotkey", L"Hotkey"};
        for (int i = 0; i < 2; ++i) {
            wchar_t buf[16] = {0};
            DWORD sz = sizeof(buf) - sizeof(wchar_t);
            DWORD t = 0;
            if (RegQueryValueExW(k, names[i], NULL, &t, (LPBYTE)buf, &sz) == ERROR_SUCCESS && t == REG_SZ) {
                type = _wtoi(buf);
                break;
            }
        }
        RegCloseKey(k);
    }
    return type;
}

static bool SendLayoutHotkey() {
    const int t = ReadSystemHotkeyType();
    std::vector<INPUT> v;
    if (t == 1) {          // Alt+Shift
        v.push_back(KeyInput(VK_LMENU, false));
        v.push_back(KeyInput(VK_LSHIFT, false));
        v.push_back(KeyInput(VK_LSHIFT, true));
        v.push_back(KeyInput(VK_LMENU, true));
    } else if (t == 2) {   // Ctrl+Shift
        v.push_back(KeyInput(VK_LCONTROL, false));
        v.push_back(KeyInput(VK_LSHIFT, false));
        v.push_back(KeyInput(VK_LSHIFT, true));
        v.push_back(KeyInput(VK_LCONTROL, true));
    } else if (t == 4) {   // клавиша ` (Ё)
        v.push_back(KeyInput(VK_OEM_3, false));
        v.push_back(KeyInput(VK_OEM_3, true));
    } else {
        return false;      // горячая клавиша отключена в настройках Windows
    }
    SendBatch(v);
    return true;
}

// Ждём, пока раскладка активного окна изменится (на любую). true — изменилась.
static bool WaitLayoutChange(HKL before, DWORD ms) {
    const DWORD t0 = GetTickCount();
    for (;;) {
        if (FgLayout() != before) return true;
        if (GetTickCount() - t0 >= ms) return false;
        Sleep(10);
    }
}

// Переключить системную раскладку на target
static void SwitchLayoutTo(HKL target) {
    if (!g_cfg.switchLayout || !target) return;
    if (EffectiveLayout() == target) return;

    const std::vector<HKL> list = InstalledLayouts();
    if (list.size() < 2 || std::find(list.begin(), list.end(), target) == list.end()) return;
    int n = (int)list.size();
    if (n > 8) n = 8;

    // Нажимаем горячую клавишу и смотрим, что изменилось (чтобы дойти до нужной раскладки, достаточно n-1 нажатий).
    // Если изменений не видно вообще, приложение могло просто не сообщить о смене раскладки — повторное нажатие
    // вернуло бы раскладку обратно, поэтому останавливаемся.
    for (int i = 0; i < n - 1; ++i) {
        const HKL before = FgLayout();
        if (EffectiveLayout() == target) return;
        if (!SendLayoutHotkey()) return;
        if (!WaitLayoutChange(before, 400)) {
            // Окно не сообщает о смене раскладки (так ведут себя некоторые приложения, например Epic Games Launcher).
            // При двух раскладках одно нажатие горячей клавиши гарантированно включило нужную — запоминаем это,
            // чтобы дальше не полагаться на устаревшие данные окна.
            if (list.size() == 2) {
                g_assumedStale = (UINT_PTR)before;
                g_assumedHwnd = (UINT_PTR)GetForegroundWindow();
                g_assumedHkl = (UINT_PTR)target;
            }
            return;
        }
    }
}

// ---------------------------------------------------------------- буфер обмена
static bool OpenClip() {
    for (int i = 0; i < 20; ++i) {
        if (OpenClipboard(g_hwnd)) return true;
        Sleep(10);
    }
    return false;
}

// Положить в (уже открытый и очищенный) буфер флаг-формат со значением 0
static void AddFlagFormat(UINT fmt) {
    if (!fmt) return;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!g) return;
    DWORD* p = (DWORD*)GlobalLock(g);
    if (!p) { GlobalFree(g); return; }
    *p = 0;
    GlobalUnlock(g);
    if (!SetClipboardData(fmt, g)) GlobalFree(g);
}

// Запретить попадание содержимого буфера в историю Windows (Win+V), облачный буфер и менеджеры буфера
static void AddHistoryExclusion(bool skipHistory, bool skipCloud, bool skipMonitor) {
    if (!skipHistory) AddFlagFormat(g_fmtHistory);
    if (!skipCloud) AddFlagFormat(g_fmtCloud);
    if (!skipMonitor) AddFlagFormat(g_fmtMonitor);
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
        bool hasHistory = false, hasCloud = false, hasMonitor = false;
        for (const ClipItem& it : items) {
            if (it.fmt == g_fmtHistory) hasHistory = true;
            if (it.fmt == g_fmtCloud) hasCloud = true;
            if (it.fmt == g_fmtMonitor) hasMonitor = true;
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, it.data.size());
            if (!g) continue;
            void* p = GlobalLock(g);
            if (!p) { GlobalFree(g); continue; }
            memcpy(p, it.data.data(), it.data.size());
            GlobalUnlock(g);
            if (!SetClipboardData(it.fmt, g)) GlobalFree(g);
        }
        // Возврат прежнего содержимого не должен второй раз попадать в историю
        if (!items.empty()) AddHistoryExclusion(hasHistory, hasCloud, hasMonitor);
        CloseClipboard();
    }
};

// 1 — текст прочитан, 0 — в буфере нет текста, -1 — буфер сейчас занят другим приложением
static int ReadClipText(std::wstring& out) {
    if (!OpenClip()) return -1;
    int result = 0;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            out = p;
            GlobalUnlock(h);
            result = 1;
        }
    }
    CloseClipboard();
    return result;
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
    else AddHistoryExclusion(false, false, false);  // временный текст не должен попасть в историю
    CloseClipboard();
    return ok;
}

// Копирует выделение через Ctrl+C. Перед копированием кладём в буфер уникальную метку:
// выделением считается только текст, пришедший на её место. Так прежнее содержимое буфера
// никогда не будет принято за выделение (например, если буфер менял менеджер буфера).
static bool CopySelectionViaClipboard(std::wstring& out) {
    wchar_t tag[64];
    _snwprintf(tag, 63, L"__LS_%lu_%lu__", (unsigned long)GetTickCount(), (unsigned long)GetCurrentThreadId());
    tag[63] = 0;
    if (!WriteClipText(tag)) return false;

    DWORD seq = GetClipboardSequenceNumber();
    SendChord(VK_CONTROL, 'C');

    const DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 600) {
        const DWORD now = GetClipboardSequenceNumber();
        if (now != seq) {
            std::wstring t;
            const int r = ReadClipText(t);
            if (r < 0) { Sleep(10); continue; }  // буфер занят — повторим
            if (r == 1 && !t.empty() && t != tag) { out = t; return true; }
            seq = now;  // буфер тронул кто-то другой, а выделения в нём нет — продолжаем ждать
        }
        Sleep(10);
    }
    return false;
}

// ---------------------------------------------------------------- UI Automation: что сейчас в фокусе
static bool CreateUia(IUIAutomation** out) {
#ifdef _MSC_VER
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IUIAutomation), (void**)out);
#else
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation, (void**)out);
#endif
    return SUCCEEDED(hr) && *out;
}

static bool IsBrowserWindow(HWND fg) {
    wchar_t cls[64] = {0};
    if (fg) GetClassNameW(fg, cls, 64);
    return wcsncmp(cls, L"Chrome_WidgetWin", 16) == 0 || wcscmp(cls, L"MozillaWindowClass") == 0;
}

// Элементы, в которые заведомо нельзя вводить текст
static bool IsNonTextControl(CONTROLTYPEID type) {
    switch (type) {
        case UIA_ButtonControlTypeId:
        case UIA_CheckBoxControlTypeId:
        case UIA_RadioButtonControlTypeId:
        case UIA_HyperlinkControlTypeId:
        case UIA_ImageControlTypeId:
        case UIA_ListControlTypeId:
        case UIA_ListItemControlTypeId:
        case UIA_MenuControlTypeId:
        case UIA_MenuBarControlTypeId:
        case UIA_MenuItemControlTypeId:
        case UIA_ProgressBarControlTypeId:
        case UIA_ScrollBarControlTypeId:
        case UIA_SliderControlTypeId:
        case UIA_StatusBarControlTypeId:
        case UIA_TabControlTypeId:
        case UIA_TabItemControlTypeId:
        case UIA_TextControlTypeId:
        case UIA_ToolBarControlTypeId:
        case UIA_ToolTipControlTypeId:
        case UIA_TreeControlTypeId:
        case UIA_TreeItemControlTypeId:
        case UIA_TitleBarControlTypeId:
        case UIA_SeparatorControlTypeId:
        case UIA_ThumbControlTypeId:
        case UIA_HeaderControlTypeId:
        case UIA_HeaderItemControlTypeId:
        case UIA_SplitButtonControlTypeId:
        case UIA_DataItemControlTypeId:
            return true;
        default:
            return false;
    }
}

// 1 — значение можно менять, 0 — только чтение, -1 — у элемента нет значения
static int ValuePatternState(IUIAutomationElement* el) {
    int state = -1;
    IUnknown* unk = NULL;
    if (SUCCEEDED(el->GetCurrentPattern(UIA_ValuePatternId, &unk)) && unk) {
        IUIAutomationValuePattern* vp = NULL;
        if (SUCCEEDED(unk->QueryInterface(LS_IID(IUIAutomationValuePattern), (void**)&vp)) && vp) {
            BOOL ro = FALSE;
            if (SUCCEEDED(vp->get_CurrentIsReadOnly(&ro))) state = ro ? 0 : 1;
            vp->Release();
        }
        unk->Release();
    }
    return state;
}

// ARIA-роль textbox / searchbox / combobox (поля ввода, сделанные из div)
static bool HasTextAriaRole(IUIAutomationElement* el) {
    bool result = false;
    BSTR role = NULL;
    if (SUCCEEDED(el->get_CurrentAriaRole(&role)) && role) {
        result = wcsstr(role, L"textbox") || wcsstr(role, L"searchbox") || wcsstr(role, L"combobox");
        SysFreeString(role);
    }
    return result;
}

// Выделение/курсор внутри элемента помечены как редактируемые (contenteditable и подобное)
static bool TextRangeIsEditable(IUIAutomationElement* el) {
    bool editable = false;
    IUnknown* unk = NULL;
    if (SUCCEEDED(el->GetCurrentPattern(UIA_TextPatternId, &unk)) && unk) {
        IUIAutomationTextPattern* tp = NULL;
        if (SUCCEEDED(unk->QueryInterface(LS_IID(IUIAutomationTextPattern), (void**)&tp)) && tp) {
            IUIAutomationTextRangeArray* arr = NULL;
            if (SUCCEEDED(tp->GetSelection(&arr)) && arr) {
                int len = 0;
                arr->get_Length(&len);
                if (len > 0) {
                    IUIAutomationTextRange* range = NULL;
                    if (SUCCEEDED(arr->GetElement(0, &range)) && range) {
                        VARIANT v;
                        VariantInit(&v);
                        if (SUCCEEDED(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &v)) && v.vt == VT_BOOL)
                            editable = (v.boolVal == VARIANT_FALSE);
                        VariantClear(&v);
                        range->Release();
                    }
                }
                arr->Release();
            }
            tp->Release();
        }
        unk->Release();
    }
    return editable;
}

// Читает выделенный текст через UI Automation (TextPattern) — без Ctrl+C и без буфера обмена.
// true — найдено непустое выделение разумного размера.
static bool ReadSelectionViaUia(std::wstring& out) {
    bool ok = false;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* el = NULL;
    if (CreateUia(&uia) && SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
        IUnknown* unk = NULL;
        if (SUCCEEDED(el->GetCurrentPattern(UIA_TextPatternId, &unk)) && unk) {
            IUIAutomationTextPattern* tp = NULL;
            if (SUCCEEDED(unk->QueryInterface(LS_IID(IUIAutomationTextPattern), (void**)&tp)) && tp) {
                IUIAutomationTextRangeArray* arr = NULL;
                if (SUCCEEDED(tp->GetSelection(&arr)) && arr) {
                    int len = 0;
                    arr->get_Length(&len);
                    if (len == 1) {
                        IUIAutomationTextRange* range = NULL;
                        if (SUCCEEDED(arr->GetElement(0, &range)) && range) {
                            BSTR s = NULL;
                            if (SUCCEEDED(range->GetText((int)kMaxUiaSelection + 1, &s)) && s) {
                                const size_t n = SysStringLen(s);
                                if (n > 0 && n <= kMaxUiaSelection) {
                                    out.assign(s, n);
                                    ok = true;
                                }
                                SysFreeString(s);
                            }
                            range->Release();
                        }
                    }
                    arr->Release();
                }
                tp->Release();
            }
            unk->Release();
        }
    }
    if (el) el->Release();
    if (uia) uia->Release();
    return ok;
}

// true, если фокус стоит в текстовом поле интерфейса браузера (адресная строка, поиск по странице),
// а не внутри веб-страницы. Веб-страница в UI Automation всегда имеет предка типа "Document".
static bool FocusIsBrowserUiEdit() {
    bool result = false;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* focused = NULL;
    IUIAutomationTreeWalker* walker = NULL;

    if (CreateUia(&uia) && SUCCEEDED(uia->GetFocusedElement(&focused)) && focused) {
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

enum FocusKind { FK_UNKNOWN = 0, FK_TEXT = 1, FK_NOTEXT = 2 };

// Определяем, находится ли фокус в поле ввода текста.
// FK_NOTEXT — уверены, что это НЕ поле ввода (кнопка, список, значок, страница браузера и т.п.).
// FK_UNKNOWN — приложение ничего определённого не сообщает (работаем как обычно).
static FocusKind GetFocusKind() {
    FocusKind result = FK_UNKNOWN;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* el = NULL;

    if (CreateUia(&uia) && SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
        const bool browser = IsBrowserWindow(GetForegroundWindow());
        CONTROLTYPEID type = 0;
        el->get_CurrentControlType(&type);

        if (IsNonTextControl(type)) {
            result = FK_NOTEXT;
        } else {
            const int value = ValuePatternState(el);
            if (value == 1) result = FK_TEXT;
            else if (value == 0) result = FK_NOTEXT;
            else if (type == UIA_EditControlTypeId) result = FK_TEXT;
            else if (browser) {
                // В браузере поле ввода — это Edit/ComboBox, элемент с ролью textbox
                // или редактируемый блок (div с contenteditable). Всё остальное — страница.
                const bool text = type == UIA_ComboBoxControlTypeId || HasTextAriaRole(el) || TextRangeIsEditable(el);
                result = text ? FK_TEXT : FK_NOTEXT;
            }

            // В Word и подобных программах документ может некорректно сообщать себя read-only
            if (!browser && type == UIA_DocumentControlTypeId && result == FK_NOTEXT) result = FK_UNKNOWN;
        }
    }

    if (el) el->Release();
    if (uia) uia->Release();
    return result;
}

// true, если в точке pt лежит обычный нередактируемый текст страницы, не относящийся к элементу focused
static bool IsStaticTextAt(IUIAutomation* uia, IUIAutomationTreeWalker* walker, IUIAutomationElement* focused,
                           POINT pt) {
    bool isStatic = false;
    IUIAutomationElement* el = NULL;
    if (SUCCEEDED(uia->ElementFromPoint(pt, &el)) && el) {
        CONTROLTYPEID type = 0;
        el->get_CurrentControlType(&type);
        if (IsNonTextControl(type)) {
            isStatic = true;
            IUIAutomationElement* cur = el;
            cur->AddRef();
            for (int i = 0; i < 12 && cur; ++i) {
                // элемент внутри поля с фокусом или внутри редактируемого элемента — это не просто текст страницы
                BOOL same = FALSE;
                if (SUCCEEDED(uia->CompareElements(cur, focused, &same)) && same) { isStatic = false; break; }
                if (ValuePatternState(cur) == 1 || HasTextAriaRole(cur)) { isStatic = false; break; }
                IUIAutomationElement* parent = NULL;
                if (FAILED(walker->GetParentElement(cur, &parent)) || !parent) {
                    cur->Release();
                    cur = NULL;
                    break;
                }
                cur->Release();
                cur = parent;
            }
            if (cur) cur->Release();
        }
        el->Release();
    }
    return isStatic;
}

// false — только если есть явное доказательство, что выделен обычный текст страницы вне поля, где стоит фокус
// (например, выделен текст ответа в чате, а фокус остался в поле ввода).
// Если определить нельзя (редакторы вроде Google Docs, которые рисуют текст сами и ничего не отдают
// в UI Automation), выделение считается выделением в редакторе.
static bool SelectionBelongsToFocus(bool byMouse, POINT down, POINT up) {
    if (!byMouse) return true;
    bool belongs = true;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* focused = NULL;
    IUIAutomationTreeWalker* walker = NULL;

    if (CreateUia(&uia) && SUCCEEDED(uia->GetFocusedElement(&focused)) && focused) {
        // нажатие или отпускание кнопки внутри элемента с фокусом — выделение в нём
        bool inside = false;
        RECT r;
        ZeroMemory(&r, sizeof(r));
        if (SUCCEEDED(focused->get_CurrentBoundingRectangle(&r))) {
            const LONG w = r.right - r.left, h = r.bottom - r.top;
            if (w >= 50 && h >= 10) {
                InflateRect(&r, 8, 8);
                inside = PtInRect(&r, down) || PtInRect(&r, up);
            }
        }
        // иначе отклоняем выделение, только если оба конца лежат на обычном тексте страницы
        if (!inside && SUCCEEDED(uia->get_ControlViewWalker(&walker)) && walker) {
            if (IsStaticTextAt(uia, walker, focused, down) && IsStaticTextAt(uia, walker, focused, up))
                belongs = false;
        }
    }

    if (walker) walker->Release();
    if (focused) focused->Release();
    if (uia) uia->Release();
    return belongs;
}

// ---------------------------------------------------------------- определение игры
struct GameInfo {
    bool game;         // активное окно — вероятно игра
    bool cursorShown;  // системный курсор мыши виден
};

// Курсор виден? (CURSOR_SHOWING; игры, скрывающие курсор через SetCursor(NULL), тоже считаются со скрытым курсором)
static bool CursorShown() {
    CURSORINFO ci;
    ZeroMemory(&ci, sizeof(ci));
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci)) return true;
    return (ci.flags & CURSOR_SHOWING) != 0 && ci.hCursor != NULL;
}

// Окно на весь монитор и без заголовка (эксклюзивный или "безрамочный" полноэкранный режим)
static bool IsFullscreenWindow(HWND fg) {
    if (!fg) return false;
    wchar_t cls[64] = {0};
    GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
        !wcscmp(cls, L"Shell_SecondaryTrayWnd") || !wcscmp(cls, L"ConsoleWindowClass") ||
        !wcscmp(cls, L"CASCADIA_HOSTING_WINDOW_CLASS") || IsBrowserWindow(fg))
        return false;

    const LONG_PTR style = GetWindowLongPtrW(fg, GWL_STYLE);
    if ((style & WS_CAPTION) == WS_CAPTION) return false;  // обычное окно с заголовком (в т.ч. развёрнутое)

    RECT r;
    if (!GetWindowRect(fg, &r)) return false;
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right &&
           r.bottom >= mi.rcMonitor.bottom;
}

// Курсор заперт в рамке меньше экрана (ClipCursor) — так делают игры в окне
static bool CursorClipped() {
    RECT c;
    if (!GetClipCursor(&c)) return false;
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return c.left > vx || c.top > vy || c.right < vx + vw || c.bottom < vy + vh;
}

static GameInfo QueryGame(HWND fg) {
    GameInfo gi;
    gi.game = false;
    gi.cursorShown = true;
    if (!g_cfg.gameFilter) return gi;
    gi.cursorShown = CursorShown();
    gi.game = IsFullscreenWindow(fg) || (!gi.cursorShown && CursorClipped());
    return gi;
}

// Клавиши, которыми в играх обычно открывают чат или консоль
static bool IsChatTriggerVk(DWORD vk) {
    return vk == VK_RETURN || vk == 'T' || vk == 'Y' || vk == 'U' || vk == VK_OEM_2 || vk == VK_OEM_3;
}

// В игре без открытого чата и без выделенного текста двойной Ctrl ни к чему не ведёт
static bool GameIgnoresTrigger() {
    if (!g_cfg.gameFilter) return false;
    if (g_gameArmed || g_gamePending) return false;
    if (!QueryGame(GetForegroundWindow()).game) return false;
    Lock l;
    return !g_selHint;
}

// ---------------------------------------------------------------- конвертация выделенного текста
struct SelResult {
    std::wstring text;  // результат
    HKL endLayout;      // раскладка, в которую нужно переключиться (по последней букве), или NULL
};

// Раскладка из списка, которая лучше всего покрывает символы текста (при равенстве — первая в списке)
static HKL PickHome(const std::vector<HKL>& cand, const std::wstring& text, bool cyr) {
    if (cand.empty()) return NULL;
    if (cand.size() == 1) return cand[0];
    HKL best = cand[0];
    size_t bestCount = 0;
    bool first = true;
    for (size_t k = 0; k < cand.size(); ++k) {
        const std::set<wchar_t>& chars = LayoutChars(cand[k]);
        size_t cnt = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            const wchar_t c = text[i];
            if ((cyr ? lm::IsCyrillic(c) : lm::IsLatin(c)) && chars.count(c)) ++cnt;
        }
        if (first || cnt > bestCount) { best = cand[k]; bestCount = cnt; first = false; }
    }
    return best;
}

static wchar_t ConvChar(wchar_t c, HKL home, HKL target) {
    if (target == home) return c;
    const lm::CharMap& m = MapFor(home, target);
    const lm::CharMap::const_iterator it = m.find(c);
    return it == m.end() ? c : it->second;
}

// Конвертирует выделенный текст. src — текст в исходном виде, step — номер нажатия в цепочке (1 — первое).
// Набранное латиницей превращается в кириллицу, набранное кириллицей — в латиницу; при нескольких раскладках
// повторные нажатия перебирают их по кругу, возвращаясь к исходному тексту.
static SelResult ConvertSelectionText(const std::wstring& src, int step, const Cycle& cy, bool defaultCyr) {
    SelResult r;
    r.text = src;
    r.endLayout = NULL;
    if (cy.lat.empty() || cy.cyr.empty()) return r;

    const HKL L0 = PickHome(cy.lat, src, false);
    const HKL C0 = PickHome(cy.cyr, src, true);
    const std::vector<unsigned char> cls = lm::ClassifyText(src, MapFor(L0, C0), MapFor(C0, L0), defaultCyr);

    // idx == 0 — исходная раскладка (как набрано), иначе — раскладки противоположного алфавита по кругу
    auto targetFor = [&](HKL home, const std::vector<HKL>& opp) -> HKL {
        const size_t idx = (size_t)step % (opp.size() + 1);
        return idx == 0 ? home : opp[idx - 1];
    };
    const HKL tLat = targetFor(L0, cy.cyr);  // куда превращаем символы, набранные латиницей
    const HKL tCyr = targetFor(C0, cy.lat);  // куда — набранные кириллицей

    for (size_t i = 0; i < src.size(); ++i) {
        if (cls[i] == lm::CC_LATIN) r.text[i] = ConvChar(src[i], L0, tLat);
        else if (cls[i] == lm::CC_CYRILLIC) r.text[i] = ConvChar(src[i], C0, tCyr);
    }
    for (size_t i = src.size(); i-- > 0;) {
        if (cls[i] != lm::CC_KEEP && lm::IsLetter(r.text[i])) {
            r.endLayout = (cls[i] == lm::CC_LATIN) ? tLat : tCyr;
            break;
        }
    }
    return r;
}

// Цепочка повторных конвертаций выделенного текста
struct SelChain {
    bool valid = false;
    std::wstring orig;    // исходный текст (до первой конвертации цепочки)
    std::wstring result;  // что мы вставили последним
    int step = 0;
    UINT_PTR hwnd = 0;
    DWORD time = 0;
    Cycle cycle;
};
static SelChain g_selChain;

// ---------------------------------------------------------------- конвертация
// Заменять текст одной вставкой нужно только в адресной строке браузера: её подсказки и автодополнение
// реагируют на каждый Backspace и символ, из-за чего текст "печатается" по одному символу.
static bool UsePasteForBuffer() {
    if (!IsBrowserWindow(GetForegroundWindow())) return false;
    return FocusIsBrowserUiEdit();
}

// trusted — выделение гарантированно текст (игра или выделение с Ctrl), проверки поля ввода не нужны
static bool ConvertSelection(bool byMouse, POINT down, POINT up, bool trusted) {
    HWND fg = GetForegroundWindow();
    wchar_t cls[64] = {0};
    if (fg) GetClassNameW(fg, cls, 64);

    // В консолях Ctrl+C прерывает программу — там выделение не трогаем
    if (!wcscmp(cls, L"ConsoleWindowClass") || !wcscmp(cls, L"CASCADIA_HOSTING_WINDOW_CLASS")) {
        Lock l;
        g_selHint = false;
        return false;
    }

    // Выделение сделано не в том элементе, где фокус (например, текст на странице) — ничего не делаем
    if (!trusted && !SelectionBelongsToFocus(byMouse, down, up)) {
        Lock l;
        g_selHint = false;
        return true;
    }

    const bool defaultCyr = ScriptOfLayout(EffectiveLayout()) == 2;

    // Сначала пробуем прочитать выделение через UI Automation: без Ctrl+C и без буфера обмена.
    // Если приложение его не отдаёт — копируем через Ctrl+C.
    std::wstring text;
    ClipBackup bak;
    bool saved = false;
    // Текст из UIA используем, только если в нём есть что конвертировать: некоторые приложения
    // (Google Docs) отдают через UIA пустышку вместо настоящего выделения
    bool viaUia = ReadSelectionViaUia(text);
    if (viaUia && ConvertSelectionText(text, 1, BuildCycle(), defaultCyr).text == text) viaUia = false;
    if (!viaUia) {
        saved = bak.Save();
        if (!CopySelectionViaClipboard(text)) {
            if (saved) bak.Restore();
            Lock l;
            g_selHint = false;
            g_selForce = false;
            return false;
        }
    }

    { Lock l; g_selHint = false; g_selForce = false; }

    // Если выделен тот же текст, который мы только что вставили, продолжаем цепочку (следующая раскладка по кругу).
    // Иначе начинаем новую: запоминаем текущую раскладку и берём последние использованные.
    std::wstring src = text;
    int step = 1;
    Cycle cycle;
    if (g_selChain.valid && g_selChain.hwnd == (UINT_PTR)fg && text == g_selChain.result &&
        (DWORD)(GetTickCount() - g_selChain.time) < kSelChainMs) {
        src = g_selChain.orig;
        step = g_selChain.step + 1;
        cycle = g_selChain.cycle;
    } else {
        NoteLayoutUsed(EffectiveLayout());
        cycle = BuildCycle();
    }

    const SelResult res = ConvertSelectionText(src, step, cycle, defaultCyr);
    const std::wstring& conv = res.text;
    if (conv == text) {
        if (saved) bak.Restore();
        return true;
    }

    // Вставка через Ctrl+V нужна для многострочного текста (Enter мог бы отправить сообщение)
    // и для адресной строки браузера. Всё остальное заменяем обычным вводом символов поверх выделения.
    bool hasCtl = false;
    for (wchar_t c : conv) {
        if (c < 0x20) { hasCtl = true; break; }
    }

    if (hasCtl || UsePasteForBuffer()) {
        // Пробелы по краям вводим клавишей: многие поля обрезают их у вставляемого текста
        std::wstring lead, core, trail;
        SplitEdgeSpaces(conv, lead, core, trail);
        if (!saved) saved = bak.Save();  // при чтении через UIA буфер ещё не сохранён
        if (!core.empty()) WriteClipText(core);
        std::vector<INPUT> in;
        AddTypedText(in, lead);
        if (!core.empty()) AddChord(in, VK_CONTROL, 'V');
        AddTypedText(in, trail);
        SendBatch(in);
        PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);
        Sleep(450);  // даём приложению время забрать текст из буфера
        if (saved) bak.Restore();
    } else {
        if (saved) bak.Restore();  // буфер обмена больше не нужен — возвращаем сразу
        std::vector<INPUT> in;
        AddTypedText(in, conv);
        SendBatch(in);
        PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);
        Sleep(40);
    }

    g_selChain.valid = true;
    g_selChain.orig = src;
    g_selChain.result = conv;
    g_selChain.step = step;
    g_selChain.hwnd = (UINT_PTR)fg;
    g_selChain.time = GetTickCount();
    g_selChain.cycle = cycle;

    if (g_cfg.reselect && conv.size() <= 5000 && !hasCtl) {
        std::vector<INPUT> in;
        AddReselect(in, conv.size());
        SendBatch(in);
        Lock l;
        g_selHint = true;
        g_selByMouse = false;
    }

    SwitchLayoutTo(res.endLayout);
    return true;
}

static void ConvertBuffer(const std::vector<Rec>& snap, const Cycle& cycle) {
    if (snap.empty()) return;
    if (cycle.lat.empty() || cycle.cyr.empty()) return;  // нужны и латинская, и кириллическая раскладки

    size_t start = 0;
    if (!g_cfg.wholeText) {
        size_t i = snap.size();
        while (i > 0 && snap[i - 1].ch == L' ') --i;
        while (i > 0 && snap[i - 1].ch != L' ') --i;
        start = i;
    }

    // Каждый символ переходит в следующую раскладку своей цепочки: набранное латиницей — в кириллицу,
    // набранное кириллицей — в латиницу; при нескольких раскладках повторные нажатия перебирают их по кругу
    std::vector<Rec> neu;
    std::wstring newText;
    for (size_t i = start; i < snap.size(); ++i) {
        Rec r = snap[i];
        if (!r.neutral) {
            const HKL target = NextTarget(r.home, r.cur, cycle);
            if (target != r.cur) {
                r.ch = (target == r.home) ? r.origCh : ConvChar(r.origCh, r.home, target);
                r.cur = target;
            }
        }
        neu.push_back(r);
        newText += r.ch;
    }
    const size_t n = snap.size() - start;

    bool done = false;

    // Адресная строка браузера: выделить набранный текст и заменить его одной вставкой
    if (UsePasteForBuffer()) {
        std::wstring lead, core, trail;
        SplitEdgeSpaces(newText, lead, core, trail);  // пробелы по краям вводим клавишей, их вставка может обрезаться
        ClipBackup bak;
        bool saved = bak.Save();
        if (core.empty() || WriteClipText(core)) {
            std::vector<INPUT> in;
            AddReselect(in, n);
            AddTypedText(in, lead);
            if (!core.empty()) AddChord(in, VK_CONTROL, 'V');
            AddTypedText(in, trail);
            SendBatch(in);
            PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);
            Sleep(450);
            done = true;
        }
        if (saved) bak.Restore();
    }

    // Везде остальное: Backspace x n и ввод символов
    if (!done) {
        std::vector<INPUT> in;
        for (size_t i = 0; i < n; ++i) {
            in.push_back(KeyInput(VK_BACK, false));
            in.push_back(KeyInput(VK_BACK, true));
        }
        AddTypedText(in, newText);
        SendBatch(in);
        PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);
    }

    {
        Lock l;
        if (g_resetAfter || g_buf.size() != snap.size()) {
            g_buf.clear();
            g_chainActive = false;
        } else {
            g_buf.resize(start);
            g_buf.insert(g_buf.end(), neu.begin(), neu.end());
            g_chainActive = true;   // пока ничего не набрано, повторное нажатие продолжит цепочку
            g_chainCycle = cycle;
        }
    }

    // Системная раскладка — та, в которой оказалась последняя буква
    HKL endLayout = NULL;
    for (size_t i = neu.size(); i-- > 0;) {
        if (!neu[i].neutral && lm::IsLetter(neu[i].ch)) { endLayout = neu[i].cur; break; }
    }
    if (!endLayout) endLayout = neu.back().cur;
    SwitchLayoutTo(endLayout);
}

static unsigned __stdcall ConvertThread(void*) {
    HRESULT coHr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    Sleep(30);  // чтобы физическое отпускание Ctrl успело обработаться

    bool hint, byMouse, force, chain;
    POINT down, up;
    Cycle cycle;
    std::vector<Rec> snap;
    {
        Lock l;
        hint = g_selHint;
        byMouse = g_selByMouse;
        force = g_selForce;
        down = g_selDownPt;
        up = g_selUpPt;
        snap = g_buf;
        chain = g_chainActive;
        if (chain) cycle = g_chainCycle;
        g_resetAfter = false;
    }
    if (!chain) {
        // Новая цепочка: запоминаем текущую раскладку и берём последние использованные
        NoteLayoutUsed(EffectiveLayout());
        cycle = BuildCycle();
    }

    // В игре набранное учитываем, только если чат открыт и набор не "протух"
    const GameInfo gi = QueryGame(GetForegroundWindow());
    if (gi.game) {
        const bool active = (g_gameArmed || g_gamePending) &&
                            (DWORD)(GetTickCount() - g_gameLastKey.load()) <= kChatIdleMs + 1000;
        if (!active) snap.clear();
    }

    // В игре и при выделении с Ctrl проверки "это поле ввода?" не нужны
    const bool trusted = gi.game || force;

    if (hint || !snap.empty()) {
        if (!trusted && GetFocusKind() == FK_NOTEXT) {
            // Фокус не в поле ввода — это был "ввод в никуда". Ничего не трогаем и забываем набранное.
            Lock l;
            g_buf.clear();
            g_selHint = false;
        } else {
            bool done = false;
            if (hint) done = ConvertSelection(byMouse, down, up, trusted);
            if (!done) ConvertBuffer(snap, cycle);
        }
    }

    if (gi.game && (g_gameArmed || g_gamePending)) g_gameLastKey = GetTickCount();

    if (SUCCEEDED(coHr)) CoUninitialize();
    g_cooldownUntil = GetTickCount() + 400;  // защита от случайных повторных срабатываний
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

// Пользователь сам переключил раскладку горячей клавишей. Если мы ведём учёт раскладки вместо окна,
// которое о ней не сообщает, при двух раскладках она просто сменилась на другую.
static void OnManualLayoutSwitch() {
    const UINT_PTR a = g_assumedHkl;
    if (a == 0) return;
    const HKL other = OtherLayoutOfTwo((HKL)a);
    g_assumedHkl = (UINT_PTR)other;  // NULL, если раскладок не две: учёт сбрасывается
}

// Отслеживаем жест смены раскладки (Alt+Shift / Ctrl+Shift по настройкам Windows, Win+Space)
static void TrackLayoutChord(DWORD vk, bool down) {
    int bit = 0;
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) bit = 1;
    else if (vk == VK_LMENU || vk == VK_RMENU) bit = 2;
    else if (vk == VK_LCONTROL || vk == VK_RCONTROL) bit = 4;
    else if (vk == VK_LWIN || vk == VK_RWIN) bit = 8;

    if (bit == 0) {  // обычная клавиша
        if (down) {
            if (vk == VK_SPACE && (g_keyDown[VK_LWIN] || g_keyDown[VK_RWIN])) OnManualLayoutSwitch();
            if (g_chordMask) g_chordDirty = true;
        }
        return;
    }
    if (down) {
        g_chordMask |= bit;
        return;
    }

    // модификатор отпущен: жест завершён, когда отпущены все модификаторы
    const bool anyHeld = g_keyDown[VK_LSHIFT] || g_keyDown[VK_RSHIFT] || g_keyDown[VK_LMENU] || g_keyDown[VK_RMENU] ||
                         g_keyDown[VK_LCONTROL] || g_keyDown[VK_RCONTROL] || g_keyDown[VK_LWIN] || g_keyDown[VK_RWIN];
    if (anyHeld) return;
    if (!g_chordDirty && g_chordMask != 0) {
        const int t = ReadSystemHotkeyType();
        if ((t == 1 && g_chordMask == 3) || (t == 2 && g_chordMask == 5)) OnManualLayoutSwitch();
    }
    g_chordMask = 0;
    g_chordDirty = false;
}

static LRESULT CALLBACK KbProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return CallNextHookEx(NULL, code, wp, lp);
    const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
    if (k->dwExtraInfo == kMarker) return CallNextHookEx(NULL, code, wp, lp);  // наши собственные события

    const bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
    const DWORD vk = k->vkCode;

    // повтор клавиши (зажата): нажатие, пока она уже считается нажатой
    bool repeat = false;
    if (vk < 256) {
        if (down) { repeat = g_keyDown[vk]; g_keyDown[vk] = true; }
        else g_keyDown[vk] = false;
    }

    TrackLayoutChord(vk, down);

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
            // во время конвертации и сразу после неё нажатия Ctrl не учитываем
            const bool cooling = g_busy || (LONG)(g_cooldownUntil.load() - k->time) > 0;
            if (g_ctrlDown && !g_ctrlDirty && !cooling && (k->time - g_ctrlDownTick) <= 400) {
                if (g_lastTap != 0 && (k->time - g_lastTap) <= (DWORD)g_cfg.doubleCtrlMs) {
                    g_lastTap = 0;
                    if (!GameIgnoresTrigger()) PostMessageW(g_hwnd, WM_TRIGGER, 0, 0);
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

    if (!down) return CallNextHookEx(NULL, code, wp, lp);

    if (g_busy) {
        if (!IsModifierVk(vk)) g_resetAfter = true;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (IsModifierVk(vk)) return CallNextHookEx(NULL, code, wp, lp);

    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const bool win = ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
    const bool ctrl = g_ctrlDown;
    const bool nav = (vk >= VK_PRIOR && vk <= VK_DOWN);

    // Сменилось активное окно — набранное в другом окне забываем
    HWND fg = GetForegroundWindow();
    if (fg != g_lastFg) {
        g_lastFg = fg;
        ResetTyping();
    }

    // ---- игровой режим
    const GameInfo gi = QueryGame(fg);
    if (gi.game) {
        const DWORD now = k->time;
        if ((g_gameArmed || g_gamePending) && (DWORD)(now - g_gameLastKey.load()) > kChatIdleMs) GameDisarm();

        if (vk == VK_ESCAPE) {  // чат закрыт
            GameDisarm();
            Lock l;
            g_selHint = false;
            g_selForce = false;
            return CallNextHookEx(NULL, code, wp, lp);
        }

        if (!g_gameArmed && !g_gamePending) {
            // Идёт игра. Единственное, что нас интересует, — попытка открыть чат.
            if (IsChatTriggerVk(vk) && !repeat && !ctrl && !alt && !win) {
                GameDisarm();
                if (gi.cursorShown) {
                    GameArm(now);  // курсор уже виден (меню/RTS): подтвердить по нему нельзя, считаем чат открытым
                } else {
                    // Курсор скрыт: если клавиша открыла чат, курсор появится. Пока ждём, ввод запоминаем условно.
                    g_gamePending = true;
                    g_gameLastKey = now;
                    SetTimer(g_hwnd, kTimerChat, kChatCheckMs, NULL);
                }
            }
            return CallNextHookEx(NULL, code, wp, lp);
        }

        g_gameLastKey = now;  // чат открыт (или ждём подтверждения): продолжаем как обычный набор
    } else if (g_gameArmed || g_gamePending) {
        GameDisarm();
    }

    // Горячие клавиши: сбрасываем буфер. Ctrl+A и Ctrl/Shift+стрелки — вероятное выделение.
    if (ctrl || alt || win) {
        Lock l;
        g_buf.clear();
        g_selHint = ctrl && !alt && !win && (vk == 'A' || (shift && nav));
        g_selByMouse = false;
        g_selForce = false;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (vk == VK_BACK) {
        Lock l;
        if (g_selHint) { g_buf.clear(); g_selHint = false; g_selForce = false; }
        else if (!g_buf.empty()) {
            if (g_chainActive) NormalizeChain();
            g_buf.pop_back();
        }
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (vk == VK_RETURN) {
        if (gi.game) GameDisarm();  // сообщение отправлено — чат закрыт
        Lock l;
        g_buf.clear();
        g_selHint = false;
        g_selForce = false;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (vk == VK_TAB || vk == VK_ESCAPE || vk == VK_DELETE || vk == VK_INSERT || (vk >= VK_F1 && vk <= VK_F24)) {
        Lock l;
        g_buf.clear();
        g_selHint = false;
        g_selForce = false;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (nav) {
        Lock l;
        g_buf.clear();
        g_selHint = shift;
        g_selByMouse = false;
        g_selForce = false;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    // ---- обычная клавиша: узнаём, какой символ она даёт в текущей раскладке
    HKL hkl = EffectiveLayout();
    BYTE ks[256];
    ZeroMemory(ks, sizeof(ks));
    if (shift) ks[VK_SHIFT] = 0x80;
    if (g_caps) ks[VK_CAPITAL] = 0x01;
    wchar_t out[8] = {0};
    int n = ToUnicodeEx((UINT)vk, k->scanCode, ks, out, 8, 0x4, hkl);  // 0x4 = не менять состояние dead keys
    if (n != 1 || out[0] < 0x20 || out[0] == 0x7F) return CallNextHookEx(NULL, code, wp, lp);

    const int script = ScriptOfLayout(hkl);  // 1 — латинская раскладка, 2 — кириллическая, 0 — другая

    Lock l;
    if (g_selHint) { g_buf.clear(); g_selHint = false; g_selForce = false; }  // набор заменил выделенный текст
    if (script == 0) { g_buf.clear(); return CallNextHookEx(NULL, code, wp, lp); }

    // запоминаем последнюю использованную латиницу / кириллицу (глобально, не по приложениям)
    if (script == 1) g_lastLat = (UINT_PTR)hkl;
    else g_lastCyr = (UINT_PTR)hkl;

    if (gi.game) {
        // В игре зажатая клавиша или серия одинаковых символов (wwww) — это управление, а не текст
        const size_t sz = g_buf.size();
        const bool series = sz >= 3 && g_buf[sz - 1].ch == out[0] && g_buf[sz - 2].ch == out[0] &&
                            g_buf[sz - 3].ch == out[0];
        if (repeat || series) {
            g_buf.clear();
            g_gameArmed = false;
            g_gamePending = false;
            KillTimer(g_hwnd, kTimerChat);
            return CallNextHookEx(NULL, code, wp, lp);
        }
    }

    if (g_chainActive) NormalizeChain();  // набор продолжился: сконвертированное считаем набранным как есть

    Rec r;
    r.ch = out[0];
    r.origCh = out[0];
    r.home = hkl;
    r.cur = hkl;
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
            g_downWithCtrl = g_ctrlDown;
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
            g_selForce = false;
            break;
        }
        case WM_LBUTTONUP: {
            const bool drag = abs(m->pt.x - g_downPt.x) > 3 || abs(m->pt.y - g_downPt.y) > 3;
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            // выделение при зажатом Ctrl — гарантированно текст
            const bool force = g_downWithCtrl && (drag || g_clickCount >= 2);

            HWND fg = GetForegroundWindow();
            g_lastFg = fg;

            const GameInfo gi = QueryGame(fg);
            if (gi.game) {
                if (!gi.cursorShown) break;  // курсор скрыт — это управление камерой, а не выделение текста
                GameArm(m->time);            // курсор виден: клик мог открыть чат (или выбрать поле ввода)
            }

            if (drag || g_clickCount >= 2 || shift || force) {
                Lock l;
                g_selHint = true;
                g_selByMouse = true;
                g_selForce = force;
                g_selDownPt = g_downPt;
                g_selUpPt = m->pt;
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
            g_selForce = false;
            break;
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

// ---------------------------------------------------------------- звук
static void PlayMp3(const std::wstring& path) {
    mciSendStringW(L"close lssnd", NULL, 0, NULL);
    std::wstring cmd = L"open \"" + path + L"\" type mpegvideo alias lssnd";
    if (mciSendStringW(cmd.c_str(), NULL, 0, NULL) == 0) mciSendStringW(L"play lssnd", NULL, 0, NULL);
}

static void PlaySwitchSound() {
    if (!g_cfg.sound) return;
    std::wstring wav = g_exeDir + L"switch.wav";
    if (GetFileAttributesW(wav.c_str()) != INVALID_FILE_ATTRIBUTES) {
        PlaySoundW(wav.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        return;
    }
    std::wstring mp3 = g_exeDir + L"switch.mp3";
    if (GetFileAttributesW(mp3.c_str()) != INVALID_FILE_ATTRIBUTES) PlayMp3(mp3);
}

// ---------------------------------------------------------------- окно (скрытое, нужно для сообщений, таймера и буфера обмена)
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_TRIGGER:
            if (!g_busy.exchange(true)) {
                uintptr_t th = _beginthreadex(NULL, 0, ConvertThread, NULL, 0, NULL);
                if (th) CloseHandle((HANDLE)th);
                else g_busy = false;
            }
            return 0;
        case WM_PLAYSOUND:
            PlaySwitchSound();
            return 0;
        case WM_TIMER:
            if (w == kTimerChat) {
                // После клавиши чата в игре: курсор появился — чат открылся, иначе это был промах
                KillTimer(h, kTimerChat);
                if (g_gamePending) {
                    g_gamePending = false;
                    if (CursorShown()) {
                        g_gameArmed = true;
                        g_gameLastKey = GetTickCount();
                    } else {
                        Lock lk;
                        g_buf.clear();
                    }
                }
                return 0;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- main
// Чтобы координаты мыши и элементов интерфейса (UI Automation) совпадали при масштабировании экрана
static void EnableDpiAwareness() {
    typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    SetCtxFn fn = u ? (SetCtxFn)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (!(fn && fn((HANDLE)(INT_PTR)-4))) SetProcessDPIAware();  // -4 = PER_MONITOR_AWARE_V2
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    HANDLE mtx = CreateMutexW(NULL, TRUE, L"Local\\LangSwitch_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Программа уже работает: предлагаем её остановить
        if (MessageBoxW(NULL, L"Вы хотите остановить работу LangSwitch?", L"LangSwitch",
                        MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) == IDYES) {
            HWND running = FindWindowW(L"LangSwitchWnd", NULL);
            if (running) PostMessageW(running, WM_CLOSE, 0, 0);
        }
        if (mtx) CloseHandle(mtx);
        return 0;
    }

    EnableDpiAwareness();
    InitializeCriticalSection(&g_cs);
    InitializeCriticalSection(&g_layCs);

    g_fmtHistory = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    g_fmtCloud = RegisterClipboardFormatW(L"CanUploadToCloudClipboard");
    g_fmtMonitor = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");

    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exePath = path;
    g_exeDir = exePath.substr(0, exePath.find_last_of(L"\\/") + 1);
    g_iniPath = g_exeDir + L"LangSwitch.ini";

    if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) SaveDefaultSettings();
    LoadSettings();

    g_caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    NoteLayoutUsed(FgLayout());  // начальные "последние использованные" раскладки

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"LangSwitchWnd";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"LangSwitch", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, GetModuleHandleW(NULL), 0);
    g_msHook = SetWindowsHookExW(WH_MOUSE_LL, MsProc, GetModuleHandleW(NULL), 0);
    if (!g_kbHook) {
        MessageBoxW(NULL, L"Не удалось установить перехват клавиатуры.", L"LangSwitch", MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_kbHook);
    if (g_msHook) UnhookWindowsHookEx(g_msHook);
    DeleteCriticalSection(&g_layCs);
    DeleteCriticalSection(&g_cs);
    if (mtx) CloseHandle(mtx);
    return 0;
}
