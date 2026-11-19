// exclude.h — исключения по программам (exclude.txt рядом с программой).
// Формат: одна программа в строке, регистр не важен, # — комментарий.
//   chrome.exe         полностью отключить LangSwitch в этой программе
//   explorer.exe -a    отключить только авто-переключение раскладки (включая Words.txt)
#pragma once

enum { EX_NONE = 0, EX_AUTO = 1, EX_FULL = 2 };

static std::map<std::wstring, int> g_exMap;  // имя exe (в нижнем регистре) -> режим
static FILETIME g_exTime = {0, 0};
static DWORD g_exCheck = 0;
static bool g_exLoaded = false;
static HWND g_exHwnd = NULL;                 // для какого окна посчитан g_exMode
static int g_exMode = EX_NONE;

static std::wstring ExLower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

// Читает UTF-8 файл целиком. false — файла нет или он слишком большой.
static bool ExReadUtf8(const std::wstring& path, std::wstring& out, FILETIME* ft) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    if (ft) GetFileTime(h, NULL, NULL, ft);
    const DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > 4 * 1024 * 1024) { CloseHandle(h); return false; }
    std::string data(size, '\0');
    DWORD rd = 0;
    if (size > 0) ReadFile(h, &data[0], size, &rd, NULL);
    CloseHandle(h);
    data.resize(rd);
    size_t off = 0;
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF)
        off = 3;
    out.clear();
    if (data.size() <= off) return true;
    const int wn = MultiByteToWideChar(CP_UTF8, 0, data.c_str() + off, (int)(data.size() - off), NULL, 0);
    if (wn > 0) {
        out.assign((size_t)wn, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, data.c_str() + off, (int)(data.size() - off), &out[0], wn);
    }
    return true;
}

static std::wstring ExPath() { return g_exeDir + L"exclude.txt"; }

static void ExCreateTemplate() {
    HANDLE h = CreateFileW(ExPath().c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    const char* t =
        "# LangSwitch: программы-исключения. Одна программа в строке, регистр не важен.\r\n"
        "# chrome.exe        - полностью отключить LangSwitch в этой программе\r\n"
        "# explorer.exe -a   - отключить только авто-переключение раскладки в этой программе\r\n"
        "# (уберите # в начале строки, чтобы правило заработало)\r\n";
    DWORD wr = 0;
    WriteFile(h, t, (DWORD)strlen(t), &wr, NULL);
    CloseHandle(h);
}

static void ExLoad() {
    g_exMap.clear();
    g_exHwnd = NULL;
    g_exMode = EX_NONE;
    std::wstring text;
    if (!ExReadUtf8(ExPath(), text, &g_exTime)) {
        if (GetFileAttributesW(ExPath().c_str()) == INVALID_FILE_ATTRIBUTES) ExCreateTemplate();
        return;
    }
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = text.size();
        std::wstring line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t hash = line.find(L'#');
        if (hash != std::wstring::npos) line.resize(hash);

        std::vector<std::wstring> tok;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == L' ' || line[i] == L'\t' || line[i] == L'\r')) ++i;
            const size_t b = i;
            while (i < line.size() && line[i] != L' ' && line[i] != L'\t' && line[i] != L'\r') ++i;
            if (i > b) tok.push_back(line.substr(b, i - b));
        }
        if (tok.empty()) continue;

        std::wstring name = ExLower(tok[0]);
        if (name.find(L'.') == std::wstring::npos) name += L".exe";
        int mode = EX_FULL;
        for (size_t k = 1; k < tok.size(); ++k) {
            if (ExLower(tok[k]) == L"-a") mode = EX_AUTO;
        }
        const std::map<std::wstring, int>::iterator it = g_exMap.find(name);
        if (it == g_exMap.end() || mode > it->second) g_exMap[name] = mode;
    }
}

static std::wstring ExProcName(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid) return L"";
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return L"";
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD n = MAX_PATH * 2;
    std::wstring r;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
        r = buf;
        const size_t k = r.find_last_of(L"\\/");
        if (k != std::wstring::npos) r = r.substr(k + 1);
    }
    CloseHandle(p);
    return ExLower(r);
}

// Режим исключения для программы активного окна. Имя процесса определяется один раз на окно,
// файл перечитывается при изменении (проверка не чаще раза в 3 секунды).
static int ExMode(HWND fg) {
    if (!fg) return EX_NONE;
    const DWORD now = GetTickCount();
    if (!g_exLoaded) {
        g_exLoaded = true;
        g_exCheck = now;
        ExLoad();
    } else if (now - g_exCheck >= 3000) {
        g_exCheck = now;
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (GetFileAttributesExW(ExPath().c_str(), GetFileExInfoStandard, &d)) {
            if (CompareFileTime(&d.ftLastWriteTime, &g_exTime) != 0) ExLoad();
        } else if (!g_exMap.empty()) {
            g_exMap.clear();
            g_exHwnd = NULL;
            g_exMode = EX_NONE;
        }
    }
    if (fg == g_exHwnd) return g_exMode;
    g_exHwnd = fg;
    g_exMode = EX_NONE;
    if (!g_exMap.empty()) {
        const std::map<std::wstring, int>::const_iterator it = g_exMap.find(ExProcName(fg));
        if (it != g_exMap.end()) g_exMode = it->second;
    }
    return g_exMode;
}
