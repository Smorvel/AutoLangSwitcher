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

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#include "layout_map.h"

// ---------------------------------------------------------------- константы
static const ULONG_PTR kMarker = 0x4C535743;  // метка наших собственных SendInput-событий
static const UINT WM_TRIGGER = WM_APP + 2;
static const UINT WM_PLAYSOUND = WM_APP + 3;

enum PasteMode { PASTE_NEVER = 0, PASTE_AUTO = 1, PASTE_ALWAYS = 2 };

// ---------------------------------------------------------------- настройки
struct Settings {
    bool sound = true;
    bool wholeText = true;     // false -> только последнее слово
    bool switchLayout = true;
    bool reselect = true;
    bool debug = false;        // писать журнал LangSwitch.log (без набранного текста)
    int pasteMode = PASTE_AUTO;
    int doubleCtrlMs = 500;
    int maxBuffer = 1000;
};

static Settings g_cfg;
static HWND g_hwnd = NULL;
static HHOOK g_kbHook = NULL, g_msHook = NULL;
static std::wstring g_exePath, g_exeDir, g_iniPath;  // g_exeDir заканчивается на '\'

static const wchar_t* kSection = L"LangSwitch";

static void LoadSettings() {
    const wchar_t* ini = g_iniPath.c_str();
    auto geti = [&](const wchar_t* key, int def) {
        return (int)GetPrivateProfileIntW(kSection, key, def, ini);
    };
    g_cfg.sound = geti(L"Sound", 1) != 0;
    g_cfg.switchLayout = geti(L"SwitchLayout", 1) != 0;
    g_cfg.reselect = geti(L"Reselect", 1) != 0;
    g_cfg.debug = geti(L"Debug", 0) != 0;
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
    put(L"PasteMode", L"auto");
    put(L"DoubleCtrlMs", L"500");
    put(L"MaxBuffer", L"1000");
    put(L"Debug", L"0");
}

// ---------------------------------------------------------------- журнал (только при Debug=1)
// В журнал попадают только служебные сведения (классы окон, длины, шаги). Набранный текст не записывается.
static void Log(const wchar_t* fmt, ...) {
    if (!g_cfg.debug) return;
    wchar_t msg[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(msg, 511, fmt, ap);
    va_end(ap);
    msg[511] = 0;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[700];
    _snwprintf(line, 699, L"%02d:%02d:%02d.%03d [t%lu] %ls\r\n", (int)st.wHour, (int)st.wMinute, (int)st.wSecond,
               (int)st.wMilliseconds, (unsigned long)GetCurrentThreadId(), msg);
    line[699] = 0;

    std::wstring path = g_exeDir + L"LangSwitch.log";
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    char u8[2100];
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, (int)sizeof(u8), NULL, NULL);
    if (n > 1) {
        DWORD written = 0;
        WriteFile(h, u8, (DWORD)(n - 1), &written, NULL);
    }
    CloseHandle(h);
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
static bool g_selByMouse = false;   // выделение сделано мышью
static POINT g_selPt = {0, 0};      // где отпущена кнопка мыши при выделении
static std::atomic<bool> g_busy(false);
static std::atomic<bool> g_resetAfter(false);
static std::atomic<DWORD> g_cooldownUntil(0);  // до этого момента (мс) двойной Ctrl не принимается

// состояние хуков (используется только потоком хуков)
static bool g_caps = false, g_capsHeld = false;
static bool g_ctrlDown = false, g_ctrlDirty = false;
static DWORD g_ctrlDownTick = 0, g_lastTap = 0;
static POINT g_downPt = {0, 0}, g_lastDownPt = {0, 0};
static DWORD g_lastDownTime = 0;
static int g_clickCount = 0;

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

// ---------------------------------------------------------------- переключение раскладки
// Эмуляция системной горячей клавиши смены раскладки (Alt+Shift / Ctrl+Shift / `),
// то есть то же самое, что нажать её руками. Сообщения WM_INPUTLANGCHANGEREQUEST не используются.

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

static bool WaitForLayout(WORD wantLang, DWORD ms) {
    const DWORD t0 = GetTickCount();
    for (;;) {
        if (PrimaryLang(FgLayout()) == wantLang) return true;
        if (GetTickCount() - t0 >= ms) return false;
        Sleep(10);
    }
}

static void SwitchLayoutTo(bool ru) {
    if (!g_cfg.switchLayout) return;
    const WORD want = ru ? LANG_RUSSIAN : LANG_ENGLISH;
    Log(L"LAYOUT want=%ls current=0x%04X", ru ? L"RU" : L"EN", (unsigned)LOWORD((UINT_PTR)FgLayout()));
    if (PrimaryLang(FgLayout()) == want) return;
    if (!FindLayout(ru)) return;  // нужная раскладка не установлена

    // Нажимаем горячую клавишу, пока не получим нужную раскладку (при 3+ раскладках они идут по кругу)
    int n = GetKeyboardLayoutList(0, NULL);
    if (n < 2) return;
    if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) {
        if (PrimaryLang(FgLayout()) == want) return;
        if (!SendLayoutHotkey()) { Log(L"LAYOUT hotkey disabled in Windows"); return; }
        bool ok = WaitForLayout(want, 200);
        Log(L"LAYOUT hotkey press #%d -> %ls", i + 1, ok ? L"switched" : L"not switched (yet)");
        if (ok) return;
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

// ---------------------------------------------------------------- UI Automation: что сейчас в фокусе
#ifdef _MSC_VER
#define LS_IID_VALUEPATTERN __uuidof(IUIAutomationValuePattern)
#else
#define LS_IID_VALUEPATTERN IID_IUIAutomationValuePattern
#endif

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

// true, если фокус стоит в текстовом поле интерфейса браузера (адресная строка, поиск по странице),
// а не внутри веб-страницы. Веб-страница в UI Automation всегда имеет предка типа "Document".
// COM в потоке должен быть уже инициализирован.
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
// FK_NOTEXT возвращается только когда мы уверены, что это НЕ поле ввода
// (кнопка, список, значок, меню, страница браузера без поля и т.п.).
// Если приложение не сообщает ничего определённого — FK_UNKNOWN (работаем как обычно).
static FocusKind GetFocusKind() {
    FocusKind result = FK_UNKNOWN;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* el = NULL;

    if (CreateUia(&uia) && SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
        CONTROLTYPEID type = 0;
        el->get_CurrentControlType(&type);

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
                result = FK_NOTEXT;
                break;
            default:
                break;
        }

        if (result == FK_UNKNOWN) {
            // Если у элемента есть значение, смотрим, можно ли его менять
            IUnknown* unk = NULL;
            if (SUCCEEDED(el->GetCurrentPattern(UIA_ValuePatternId, &unk)) && unk) {
                IUIAutomationValuePattern* vp = NULL;
                if (SUCCEEDED(unk->QueryInterface(LS_IID_VALUEPATTERN, (void**)&vp)) && vp) {
                    BOOL ro = FALSE;
                    if (SUCCEEDED(vp->get_CurrentIsReadOnly(&ro))) result = ro ? FK_NOTEXT : FK_TEXT;
                    vp->Release();
                }
                unk->Release();
            }
            if (type == UIA_DocumentControlTypeId) {
                if (IsBrowserWindow(GetForegroundWindow())) {
                    // Фокус на самой веб-странице (не в поле ввода) — это не ввод текста
                    if (result != FK_TEXT) result = FK_NOTEXT;
                } else if (result == FK_NOTEXT) {
                    // В Word и подобных программах документ может некорректно сообщать себя read-only
                    result = FK_UNKNOWN;
                }
            }
        }

        if (result == FK_UNKNOWN && type == UIA_EditControlTypeId) result = FK_TEXT;
        Log(L"FOCUS controlType=%d kind=%d", (int)type, (int)result);
    } else {
        Log(L"FOCUS: UIA gave no focused element");
    }

    if (el) el->Release();
    if (uia) uia->Release();
    return result;
}

// false — выделение заведомо находится не в элементе, где стоит фокус
// (например, выделен текст ответа на странице, а фокус остался в поле ввода).
// Проверяем только выделения, сделанные мышью: точка отпускания кнопки должна лежать внутри элемента с фокусом.
static bool SelectionBelongsToFocus(bool byMouse, POINT pt) {
    if (!byMouse) return true;
    bool ok = true;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* el = NULL;
    if (CreateUia(&uia) && SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
        RECT r;
        ZeroMemory(&r, sizeof(r));
        if (SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && r.right > r.left && r.bottom > r.top) {
            RECT rr = r;
            InflateRect(&rr, 6, 6);
            ok = PtInRect(&rr, pt) != FALSE;
            Log(L"SELCHECK pt=(%ld,%ld) rect=(%ld,%ld,%ld,%ld) inside=%d", (long)pt.x, (long)pt.y, (long)r.left,
                (long)r.top, (long)r.right, (long)r.bottom, ok ? 1 : 0);
        }
    }
    if (el) el->Release();
    if (uia) uia->Release();
    return ok;
}

// ---------------------------------------------------------------- конвертация
static bool UsePasteForBuffer() {
    if (g_cfg.pasteMode == PASTE_NEVER) return false;
    if (g_cfg.pasteMode == PASTE_ALWAYS) return true;

    // auto: только адресная строка браузеров (Chrome, Edge, Opera, Firefox...).
    // Её подсказки/автодополнение реагируют на каждый Backspace и символ, из-за этого текст "печатается" медленно.
    if (!IsBrowserWindow(GetForegroundWindow())) return false;
    return FocusIsBrowserUiEdit();
}

static bool ConvertSelection(bool byMouse, POINT pt) {
    HWND fg = GetForegroundWindow();
    wchar_t cls[64] = {0};
    if (fg) GetClassNameW(fg, cls, 64);
    Log(L"SEL start class=%ls byMouse=%d", cls, byMouse ? 1 : 0);

    // В консолях Ctrl+C прерывает программу — там выделение не трогаем
    if (!wcscmp(cls, L"ConsoleWindowClass") || !wcscmp(cls, L"CASCADIA_HOSTING_WINDOW_CLASS")) {
        Lock l;
        g_selHint = false;
        return false;
    }

    // Выделение сделано не в том элементе, где фокус (например, текст на странице) — ничего не делаем
    if (!SelectionBelongsToFocus(byMouse, pt)) {
        Log(L"SEL abort: selection is outside the focused element");
        Lock l;
        g_selHint = false;
        return true;  // "обработано": дальше ничего не конвертируем
    }

    ClipBackup bak;
    bool saved = bak.Save();

    DWORD seq0 = GetClipboardSequenceNumber();
    SendChord(VK_CONTROL, 'C');
    DWORD t0 = GetTickCount();
    while (GetClipboardSequenceNumber() == seq0 && GetTickCount() - t0 < 500) Sleep(10);

    { Lock l; g_selHint = false; }

    if (GetClipboardSequenceNumber() == seq0) {
        Log(L"SEL copy: clipboard did not change (no selection?)");
        return false;
    }

    std::wstring text;
    if (!ReadClipText(text) || text.empty()) {
        Log(L"SEL copy: no text on clipboard");
        if (saved) bak.Restore();
        return false;
    }

    std::wstring conv = lm::ConvertSelection(text, IsRuLayoutNow());
    Log(L"SEL copied len=%u hasNewline=%d changed=%d", (unsigned)text.size(),
        text.find_first_of(L"\r\n") != std::wstring::npos ? 1 : 0, conv != text ? 1 : 0);
    if (conv == text) {
        if (saved) bak.Restore();
        return true;
    }

    WriteClipText(conv);
    SendChord(VK_CONTROL, 'V');
    Log(L"SEL pasted");
    PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);

    // Ждём, пока приложение действительно вставит текст (Chrome обрабатывает вставку отдельно от клавиш),
    // и только потом возвращаем буфер обмена и заново выделяем текст
    Sleep(350);
    if (saved) bak.Restore();

    bool re = g_cfg.reselect && conv.size() <= 5000 && conv.find_first_of(L"\r\n") == std::wstring::npos;
    Log(L"SEL reselect=%d (len=%u)", re ? 1 : 0, (unsigned)conv.size());
    if (re) {
        std::vector<INPUT> in;
        AddReselect(in, conv.size());
        SendBatch(in);
        Lock l;
        g_selHint = true;
        g_selByMouse = false;
    }

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
        Log(L"BUF path=paste n=%u", (unsigned)n);
        ClipBackup bak;
        bool saved = bak.Save();
        if (WriteClipText(newText)) {
            std::vector<INPUT> in;
            AddReselect(in, n);                 // Shift+Left x n (выделение набранного текста)
            AddChord(in, VK_CONTROL, 'V');      // вставка нового текста поверх выделения
            SendBatch(in);
            PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);
            Sleep(350);
            done = true;
        }
        if (saved) bak.Restore();
    }

    // Способ 2 (везде остальное): Backspace x n и ввод Unicode-символов
    if (!done) {
        Log(L"BUF path=type n=%u", (unsigned)n);
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
        PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);
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

    bool endRu = neu.back().ru;
    for (size_t i = neu.size(); i-- > 0;) {
        if (!neu[i].neutral && lm::IsLetter(neu[i].ch)) { endRu = neu[i].ru; break; }
    }
    SwitchLayoutTo(endRu);
}

static unsigned __stdcall ConvertThread(void*) {
    HRESULT coHr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    Sleep(30);  // чтобы физическое отпускание Ctrl успело обработаться

    bool hint, byMouse;
    POINT pt;
    std::vector<Rec> snap;
    {
        Lock l;
        hint = g_selHint;
        byMouse = g_selByMouse;
        pt = g_selPt;
        snap = g_buf;
        g_resetAfter = false;
    }
    Log(L"THREAD start hint=%d byMouse=%d buf=%u", hint ? 1 : 0, byMouse ? 1 : 0, (unsigned)snap.size());

    if (!hint && snap.empty()) {
        Log(L"THREAD nothing to convert");
    } else if (GetFocusKind() == FK_NOTEXT) {
        // Фокус не в поле ввода — это был "ввод в никуда". Ничего не трогаем и забываем набранное.
        Log(L"THREAD focus is not a text field: skipped");
        Lock l;
        g_buf.clear();
        g_selHint = false;
    } else {
        bool done = false;
        if (hint) done = ConvertSelection(byMouse, pt);
        if (!done) ConvertBuffer(snap);
    }

    if (SUCCEEDED(coHr)) CoUninitialize();
    g_cooldownUntil = GetTickCount() + 400;  // защита от случайных повторных срабатываний
    g_busy = false;
    Log(L"THREAD end");
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
            // во время конвертации и сразу после неё нажатия Ctrl не учитываем
            const bool cooling = g_busy || (LONG)(g_cooldownUntil.load() - k->time) > 0;
            if (g_ctrlDown && !g_ctrlDirty && !cooling && (k->time - g_ctrlDownTick) <= 400) {
                if (g_lastTap != 0 && (k->time - g_lastTap) <= (DWORD)g_cfg.doubleCtrlMs) {
                    g_lastTap = 0;
                    Log(L"CTRL tap #2 -> trigger");
                    PostMessageW(g_hwnd, WM_TRIGGER, 0, 0);
                } else {
                    g_lastTap = k->time;
                    Log(L"CTRL tap #1");
                }
            } else {
                if (g_ctrlDown && !g_ctrlDirty && cooling) Log(L"CTRL tap ignored (busy/cooldown)");
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

    // Горячие клавиши: сбрасываем буфер. Ctrl+A и Ctrl/Shift+стрелки — вероятное выделение.
    if (ctrl || alt || win) {
        Lock l;
        g_buf.clear();
        g_selHint = ctrl && !alt && !win && (vk == 'A' || (shift && nav));
        g_selByMouse = false;
        return CallNextHookEx(NULL, code, wp, lp);
    }

    if (vk == VK_BACK) {
        Lock l;
        if (g_selHint) { g_buf.clear(); g_selHint = false; }
        else if (!g_buf.empty()) g_buf.pop_back();
        return CallNextHookEx(NULL, code, wp, lp);
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
        g_selByMouse = false;
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
            if (drag || g_clickCount >= 2 || shift) {
                Lock l;
                g_selHint = true;
                g_selByMouse = true;
                g_selPt = m->pt;
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

// ---------------------------------------------------------------- звук
static void PlayFileMci(const std::wstring& path, const wchar_t* type) {
    mciSendStringW(L"close lssnd", NULL, 0, NULL);
    std::wstring cmd = L"open \"" + path + L"\"";
    if (type && *type) cmd += std::wstring(L" type ") + type;
    cmd += L" alias lssnd";
    MCIERROR e = mciSendStringW(cmd.c_str(), NULL, 0, NULL);
    if (e == 0) e = mciSendStringW(L"play lssnd", NULL, 0, NULL);
    Log(L"SOUND MCI result=%lu", (unsigned long)e);
}

static void PlaySwitchSound() {
    if (!g_cfg.sound) return;
    std::wstring wav = g_exeDir + L"switch.wav";
    if (GetFileAttributesW(wav.c_str()) != INVALID_FILE_ATTRIBUTES) {
        BOOL ok = PlaySoundW(wav.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        Log(L"SOUND PlaySound ok=%d err=%lu", ok ? 1 : 0, (unsigned long)GetLastError());
        if (!ok) PlayFileMci(wav, NULL);  // запасной способ, если PlaySound не смог проиграть файл
        return;
    }
    std::wstring mp3 = g_exeDir + L"switch.mp3";
    if (GetFileAttributesW(mp3.c_str()) != INVALID_FILE_ATTRIBUTES) {
        PlayFileMci(mp3, L"mpegvideo");
        return;
    }
    Log(L"SOUND no sound file found");
}

// ---------------------------------------------------------------- окно (скрытое, нужно для сообщений и буфера обмена)
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_TRIGGER:
            if (!g_busy.exchange(true)) {
                uintptr_t th = _beginthreadex(NULL, 0, ConvertThread, NULL, 0, NULL);
                if (th) CloseHandle((HANDLE)th);
                else g_busy = false;
            } else {
                Log(L"TRIGGER ignored: already busy");
            }
            return 0;
        case WM_PLAYSOUND:
            PlaySwitchSound();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- main
static void EnableDpiAwareness() {
    // Чтобы координаты мыши и элементов интерфейса (UI Automation) совпадали при масштабировании экрана
    typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    SetCtxFn fn = u ? (SetCtxFn)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (!(fn && fn((HANDLE)(INT_PTR)-4))) SetProcessDPIAware();  // -4 = PER_MONITOR_AWARE_V2
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    HANDLE mtx = CreateMutexW(NULL, TRUE, L"Local\\LangSwitch_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    EnableDpiAwareness();
    InitializeCriticalSection(&g_cs);

    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    g_exePath = path;
    g_exeDir = g_exePath.substr(0, g_exePath.find_last_of(L"\\/") + 1);
    g_iniPath = g_exeDir + L"LangSwitch.ini";

    if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) SaveDefaultSettings();
    LoadSettings();
    Log(L"=== LangSwitch started ===");

    g_caps = (GetKeyState(VK_CAPITAL) & 1) != 0;

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
    DeleteCriticalSection(&g_cs);
    if (mtx) CloseHandle(mtx);
    return 0;
}
