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
#include <oleauto.h>
#include <mmsystem.h>
#include <process.h>
#include <uiautomationclient.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cwchar>
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
static bool g_selForce = false;     // выделение сделано мышью при зажатом Ctrl — точно текст, проверки не нужны
static POINT g_selDownPt = {0, 0};  // где нажата кнопка мыши при выделении
static POINT g_selUpPt = {0, 0};    // где она отпущена
static std::atomic<bool> g_busy(false);
static std::atomic<bool> g_resetAfter(false);
static std::atomic<DWORD> g_cooldownUntil(0);  // до этого момента (мс) двойной Ctrl не принимается

// игровой режим
static std::atomic<bool> g_gameArmed(false);    // в игре идёт набор текста (чат открыт)
static std::atomic<bool> g_gamePending(false);  // нажата клавиша чата, ждём подтверждения по курсору
static std::atomic<DWORD> g_gameLastKey(0);

// состояние хуков (используется только потоком хуков)
static bool g_caps = false, g_capsHeld = false;
static bool g_ctrlDown = false, g_ctrlDirty = false;
static bool g_downWithCtrl = false;
static bool g_keyDown[256] = {false};
static HWND g_lastFg = NULL;
static DWORD g_ctrlDownTick = 0, g_lastTap = 0;
static POINT g_downPt = {0, 0}, g_lastDownPt = {0, 0};
static DWORD g_lastDownTime = 0;
static int g_clickCount = 0;

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

// ---------------------------------------------------------------- утилиты раскладок
static HKL FgLayout() {
    HWND fg = GetForegroundWindow();
    DWORD tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    return GetKeyboardLayout(tid);
}

static WORD PrimaryLang(HKL h) { return PRIMARYLANGID(LOWORD((UINT_PTR)h)); }

static bool IsRuLayoutNow() { return PrimaryLang(FgLayout()) == LANG_RUSSIAN; }

static bool LayoutInstalled(bool ru) {
    const WORD want = ru ? LANG_RUSSIAN : LANG_ENGLISH;
    int n = GetKeyboardLayoutList(0, NULL);
    if (n <= 0) return false;
    std::vector<HKL> v(n);
    GetKeyboardLayoutList(n, v.data());
    for (HKL h : v) {
        if (PrimaryLang(h) == want) return true;
    }
    return false;
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
    if (PrimaryLang(FgLayout()) == want) return;
    if (!LayoutInstalled(ru)) return;

    // Нажимаем горячую клавишу, пока не получим нужную раскладку (при 3+ раскладках они идут по кругу)
    int n = GetKeyboardLayoutList(0, NULL);
    if (n < 2) return;
    if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) {
        if (PrimaryLang(FgLayout()) == want) return;
        if (!SendLayoutHotkey()) return;
        if (WaitForLayout(want, 200)) return;
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

// false — выделение заведомо находится не в элементе, где стоит фокус
// (например, выделен текст ответа на странице, а фокус остался в поле ввода).
// Для выделения мышью хотя бы одна из точек — нажатия или отпускания кнопки — должна лежать в элементе с фокусом.
static bool SelectionBelongsToFocus(bool byMouse, POINT down, POINT up) {
    if (!byMouse) return true;
    bool ok = true;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* el = NULL;
    if (CreateUia(&uia) && SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
        RECT r;
        ZeroMemory(&r, sizeof(r));
        if (SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && r.right > r.left && r.bottom > r.top) {
            InflateRect(&r, 8, 8);
            ok = PtInRect(&r, down) || PtInRect(&r, up);
        }
    }
    if (el) el->Release();
    if (uia) uia->Release();
    return ok;
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

    ClipBackup bak;
    bool saved = bak.Save();

    DWORD seq0 = GetClipboardSequenceNumber();
    SendChord(VK_CONTROL, 'C');
    DWORD t0 = GetTickCount();
    while (GetClipboardSequenceNumber() == seq0 && GetTickCount() - t0 < 500) Sleep(10);

    { Lock l; g_selHint = false; g_selForce = false; }

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

    // Вставка через Ctrl+V нужна для многострочного текста (Enter мог бы отправить сообщение)
    // и для адресной строки браузера. Всё остальное заменяем обычным вводом символов поверх выделения.
    bool hasCtl = false;
    for (wchar_t c : conv) {
        if (c < 0x20) { hasCtl = true; break; }
    }

    if (hasCtl || UsePasteForBuffer()) {
        WriteClipText(conv);
        SendChord(VK_CONTROL, 'V');
        PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);
        Sleep(350);  // даём приложению время забрать текст из буфера
        if (saved) bak.Restore();
    } else {
        if (saved) bak.Restore();
        std::vector<INPUT> in;
        for (wchar_t ch : conv) {
            in.push_back(UniInput(ch, false));
            in.push_back(UniInput(ch, true));
        }
        SendBatch(in);
        PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);
        Sleep(40);
    }

    if (g_cfg.reselect && conv.size() <= 5000 && !hasCtl) {
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

    // Адресная строка браузера: выделить набранный текст и заменить его одной вставкой
    if (UsePasteForBuffer()) {
        ClipBackup bak;
        bool saved = bak.Save();
        if (WriteClipText(newText)) {
            std::vector<INPUT> in;
            AddReselect(in, n);
            AddChord(in, VK_CONTROL, 'V');
            SendBatch(in);
            PostMessageW(g_hwnd, WM_PLAYSOUND, 0, 0);
            Sleep(350);
            done = true;
        }
        if (saved) bak.Restore();
    }

    // Везде остальное: Backspace x n и ввод Unicode-символов
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

    bool hint, byMouse, force;
    POINT down, up;
    std::vector<Rec> snap;
    {
        Lock l;
        hint = g_selHint;
        byMouse = g_selByMouse;
        force = g_selForce;
        down = g_selDownPt;
        up = g_selUpPt;
        snap = g_buf;
        g_resetAfter = false;
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
            if (!done) ConvertBuffer(snap);
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
        else if (!g_buf.empty()) g_buf.pop_back();
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
    if (g_selHint) { g_buf.clear(); g_selHint = false; g_selForce = false; }  // набор заменил выделенный текст
    if (!isRu && !isEn) { g_buf.clear(); return CallNextHookEx(NULL, code, wp, lp); }

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

    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exePath = path;
    g_exeDir = exePath.substr(0, exePath.find_last_of(L"\\/") + 1);
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
