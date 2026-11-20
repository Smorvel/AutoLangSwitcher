// autoswitch.h — опциональное авто-переключение раскладки и словарь Words.txt.
// Включается строкой AutoSwitch=1 в LangSwitch.ini. Словарь Words.txt работает и при AutoSwitch=0, если файл существует.
// Подключается из main.cpp одной директивой #include и использует его внутренние функции, поэтому файл не
// компилируется отдельно. Чтобы убрать функцию, удалите этот файл и правки в main.cpp
// (ConvertBuffer: параметры forceStart/forceTarget/forceEnd, #include перед ConvertThread, WinMain: AsSetup, AsManualStart).
#pragma once

// определены в main.cpp ниже места подключения
static LRESULT CALLBACK KbProc(int code, WPARAM wp, LPARAM lp);
static bool IsModifierVk(DWORD vk);

enum { AL_NONE = 0, AL_EN = 1, AL_RU = 2, AL_UK = 3 };

// ---------------------------------------------------------------- таблицы (частые буквосочетания языков)
static const wchar_t* kAsBigEn =
    L"th he in er an re on at en nd ti es or te of ed is it al ar st to nt ng se ha as ou io le ve co me de hi ri ro ic "
    L"ne ea ra ce li ch ll be ma si om ur ca el ta la ns di fo ho pe ec pr no ct us ac ot il tr ly nc et ut ss so rs un "
    L"lo wa ge ie wh ee wi em ad ol rt po we na ul ni ts mo ow pa im mi ai sh ir su id os iv ia am fi ci vi pl ig tu ev "
    L"ld ry mp fe bl ab gh ty op wo sa ay ex ke fr oo av ag if ap gr od bo sp rd do uc bu ei ov by rm ep tt oc fa ef cu "
    L"rn sc gi da yo cr cl du ga qu ue ff ba ey ls va um pp ua up lu go ht ru ug ds lt pi rc rr eg au ck ew bi br tl ms "
    L"ye ud mb ip ub oi oa ok ob ib ak ik ph lk lf lp lm rk rb rg rl rv rp nk nn nl nm gn gs gl hy hr kn kl ks ki ku ko "
    L"ka za ze zi zo xt xp yp yr ys yt yn ym ur us ut ve vi vo eb eg ej ek ep ew ez ob oj ok ow oz uf ug uk ul um un up "
    L"wn wr wl ws wd rw ww yb yc yd yf yg yl yw yi ya yu ax ay az aw ah ab ac af ag ak ap aq au av az ib id if ig ik "
    L"ix iz oh ox oy uy uz ym";

static const wchar_t* kAsBigRu =
    L"ст но то на ен ов ни ра во ко ол ор ро по ре ан ер ос ти ли ет ва ль ем ат ал ла ка ть ог го ел ны ск ин ие ой ий "
    L"ых ым ом ед ик ит ле ме мо ми ма му не ну ту ты де да ди до др дн дл ве ви вы вн вс вл вр за зн зд зв из иц ир ис ив "
    L"ил им ию ия ке ки кл кр ку лу ло ля мн мы нг нд нс нт нь ню ня об од ое ож оз ок он оп от оф ох оч ош пе пи пл пр пу "
    L"пы рг рд рж рз рк рн рс рт ру рц ря са св се си сл см сн со сп ср су та тв те тк тн тр тя уб ув уд уж уз уй ук ул "
    L"ум ун уп ур ус ут уч фи фо фр ха хо ху ца це ци чи ча че чн чт чу ша ше ши шл шн щи ыв ые ыл ын ыт эт эк юд ющ юч "
    L"ят яв яз як ял ян ях ье ьн ьс ью ьк йт йс йн ай ей ую ою ая яя ее ое бо бы ба бе би бр бл бу гу га ге ги гл гр гн "
    L"жа же жи жн жд зо зи зе зы зм зр иб ег аб ек ап аз ав ад аж ак ам ар ас ач ау ах ац аш бн бс вд вк вм вп вт вч вш "
    L"ех еж ез ел еп ер ес еч еш ещ ёт ёл ён ёр им ин ио ип ич иш кн кс кт км лб лг лж лк лм лн лп лс лт лч лш мб мг мм "
    L"мп мс мт мч мя нб нв нж нз нк нл нм нн нп нр нц нч нш ох оц ощ ух уц уш ущ юб юв юг юж юк юл юм юн юр юс ют юх яд "
    L"яж яй ям яп яр яс яц яч яш ои ио еи ею аю уе уи ёс ёв ёз ёк ёж ьи ьё ьо ьу ья ъе ъё ъя ъю гу гд кё шь шё щё чё жё чь "
    L"ыс ыр ыд ыб ыг ыж ык ыч ыш ыз ыю бя бю бь бё вя вё вю вь гя гю дя дю дё дь зя зю зь кя кю кь лё лю мя мю мь мё пя пю "
    L"пь пё рё рю рь сё сю ся сь тё тю тя ть фя фю фь хё хю хя цю цё ща щу щё ое ио аи еа оа уа ри ра ря";

// украинские буквы: добавляются к русским биграммам
static const wchar_t* kAsBigUk =
    L"ій ої ою ія ію ії іс ін ів іл ід ім іч ік іт ір ні ці ді ші ри ки ви пи ми ти ли ґі ґо ґу ґе ґа ґи їх їм їй їн "
    L"їд їж їз їс їт єм єн єт єд єв єю ща ві рі ті зі мі лі сі кі пі бі гі хі фі чі ші жі ця сь ьк ьс ьо ьк ює ює";

// слова, допустимые сами по себе (служебные слова, междометия, сокращения, расширения файлов)
static const wchar_t* kAsShortEn =
    L"a i am an as at be by do go he if in is it me my no of oh ok on or so to up us we the and are but can for her him "
    L"his how its not now one our out she you all any day get has had was who why yes let may new off old own say see two "
    L"way too use did man men run sit set put red top hot big far few lot try ask end eye cut aid age ago air bad bar bed "
    L"bit box boy bus buy car cup die dog dry due ear eat egg fan fat fit fix fly fun gas god gun guy hat hit ice ill job "
    L"key kid lay leg lie lip log low map mix net nor oil pay pen pet pie pop raw row sad sea shy sky son sun tax tea tie "
    L"tip toy van war wet win won yet zoo ah eh hi ha ho "
    L"vs pc tv id ip os ui ux hr pr qa ai ml db js ts pm cv ps mr dr ms st nd rd th ft kg km cm mm gb mb kb tb hz "
    L"pdf api url sql css cpu gpu ram rom usb ssd hdd png jpg jpeg gif svg bmp ico psd dvd ceo faq fyi lol omg btw wtf vip "
    L"vpn app web org com dev bot bug git "
    L"apk txt exe dll zip rar doc docx xls xlsx ppt pptx csv rtf odt iso bat cmd ini cfg log tmp sys bin msi ipa dmg deb "
    L"rpm html htm php xml json yaml yml java cpp lua bash mov wav flac ogg webm webp mkv avi mp tar "
    L"http https www ftp ssh ssl tls dns tcp udp lan wan gui cli sdk ide wifi bios ios mac npm pip";

static const wchar_t* kAsShortRu =
    L"я в и к с у о а на не но да ты мы он за из по до от ну же бы ли то во ко со об ни ой ах ох эх уж его все она они "
    L"уже или для мне нет вот тут там при без над под про чем кто где так еще ещё это что как вам вас нам нас мой наш "
    L"ваш был ага ого ура чей той тот эта эти оно ему ней них ним сам сей ради";

static const wchar_t* kAsShortUk =
    L"я в у і й з до на не та чи це ми ти ви так ні ну ще як що хто де цей той ця ці ось він без для при про над під "
    L"від але там тут вже аж ох ах ой";

static void AsFillBigrams(std::set<unsigned>& dst, const wchar_t* list) {
    const wchar_t* p = list;
    while (*p) {
        while (*p == L' ') ++p;
        if (p[0] && p[1] && p[0] != L' ' && p[1] != L' ') dst.insert(((unsigned)p[0] << 16) | (unsigned)p[1]);
        while (*p && *p != L' ') ++p;
    }
}

static void AsFillWords(std::set<std::wstring>& dst, const wchar_t* list) {
    const wchar_t* p = list;
    while (*p) {
        while (*p == L' ') ++p;
        const wchar_t* q = p;
        while (*q && *q != L' ') ++q;
        if (q > p) dst.insert(std::wstring(p, (size_t)(q - p)));
        p = q;
    }
}

static bool AsHasBigram(int lang, wchar_t a, wchar_t b) {
    static std::set<unsigned> en, ru, uk;
    static bool init = false;
    if (!init) {
        AsFillBigrams(en, kAsBigEn);
        AsFillBigrams(ru, kAsBigRu);
        AsFillBigrams(uk, kAsBigUk);
        init = true;
    }
    const unsigned key = ((unsigned)a << 16) | (unsigned)b;
    if (lang == AL_EN) return en.count(key) != 0;
    if (lang == AL_RU) return ru.count(key) != 0;

    // украинский: своя небольшая таблица + русская с заменой близких букв (і, ї -> и; є -> е; ґ -> г).
    // Букв ы, э, ъ, ё в украинском нет.
    static const wchar_t* kNotUk = L"ыэъё";
    if (wcschr(kNotUk, a) || wcschr(kNotUk, b)) return false;
    if (uk.count(key)) return true;
    auto m = [](wchar_t c) -> wchar_t {
        return (c == L'і' || c == L'ї') ? L'и' : (c == L'є') ? L'е' : (c == L'ґ') ? L'г' : c;
    };
    return ru.count(((unsigned)m(a) << 16) | (unsigned)m(b)) != 0;
}

static bool AsInShort(int lang, const std::wstring& w) {
    static std::set<std::wstring> en, ru, uk;
    static bool init = false;
    if (!init) {
        AsFillWords(en, kAsShortEn);
        AsFillWords(ru, kAsShortRu);
        AsFillWords(uk, kAsShortUk);
        init = true;
    }
    const std::set<std::wstring>& t = (lang == AL_EN) ? en : (lang == AL_UK) ? uk : ru;
    return t.count(w) != 0;
}

static bool AsVowel(int lang, wchar_t c) {
    const wchar_t* v = (lang == AL_EN) ? L"aeiouy" : (lang == AL_UK) ? L"аеиіїоуєюя" : L"аеёиоуыэюя";
    return c != 0 && wcschr(v, c) != NULL;
}

static std::wstring AsLower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

static bool AsIsApos(wchar_t c) { return c == L'\'' || c == 0x2019 || c == 0x02BC; }
static bool AsRuCons(wchar_t c) { return c != 0 && wcschr(L"бвгджзйклмнпрстфхцчшщ", c) != NULL; }
static bool AsUkCons(wchar_t c) { return c != 0 && wcschr(L"бвгґджзйклмнпрстфхцчшщ", c) != NULL; }

// Язык раскладки: латиница — английская; кириллица — по буквам, которые она печатает
// (русская раскладка внутри украинского языка Windows и наоборот определяется правильно)
static int AsLayoutLang(HKL h) {
    static std::map<UINT_PTR, int> cache;
    const std::map<UINT_PTR, int>::const_iterator it = cache.find((UINT_PTR)h);
    if (it != cache.end()) return it->second;
    int lang = AL_NONE;
    const int script = ScriptOfLayout(h);
    if (script == 1) {
        if (PRIMARYLANGID(LOWORD((UINT_PTR)h)) == LANG_ENGLISH) lang = AL_EN;
    } else if (script == 2) {
        const std::set<wchar_t>& ch = LayoutChars(h);
        if (ch.count(L'ў')) lang = AL_NONE;
        else if (ch.count(L'ї') || ch.count(L'є') || ch.count(L'ґ')) lang = AL_UK;
        else if (ch.count(L'ы') || ch.count(L'э')) lang = AL_RU;
    }
    cache[(UINT_PTR)h] = lang;
    return lang;
}

// ---------------------------------------------------------------- оценка слова
struct AsScore {
    int pairs = 0, hits = 0, maxMissRun = 0, vowels = 0, maxConsRun = 0, letters = 0;
    bool badStart = false;
};

// Допустима ли пара букв, которой нет в таблице частых: чередование гласной и согласной допустимо,
// кроме сочетаний, невозможных в языке. Используется для оценки слова в целевой раскладке:
// таблицы частых пар неполные, и редкие, но нормальные слова иначе отбрасывались бы.
static bool AsPairLoose(int lang, wchar_t a, wchar_t b) {
    if (AsHasBigram(lang, a, b)) return true;
    if (lang == AL_EN) return a != L'q' && AsVowel(lang, a) != AsVowel(lang, b);
    if (a == L'ь' || a == L'ъ' || b == L'ь' || b == L'ъ') return false;
    const bool va = AsVowel(lang, a), vb = AsVowel(lang, b);
    if (va == vb) return false;
    if (!va) {  // согласная + гласная
        if (wcschr(L"жшчщ", a) && wcschr(L"ыяю", b)) return false;
        if (wcschr(L"гкх", a) && b == L'ы') return false;
    }
    return true;
}

// Пары букв считаются только внутри непрерывных групп букв. loose — допускать пары вне таблицы (см. AsPairLoose).
static AsScore AsScoreRuns(const std::wstring& s, int lang, bool loose = false) {
    AsScore r;
    int missRun = 0, consRun = 0;
    bool prevLetter = false;
    wchar_t prev = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (!lm::IsLetter(c)) {
            prevLetter = false;
            missRun = 0;
            consRun = 0;
            continue;
        }
        ++r.letters;
        if (AsVowel(lang, c)) {
            ++r.vowels;
            consRun = 0;
        } else {
            ++consRun;
            if (consRun > r.maxConsRun) r.maxConsRun = consRun;
        }
        if (prevLetter) {
            ++r.pairs;
            if (loose ? AsPairLoose(lang, prev, c) : AsHasBigram(lang, prev, c)) {
                ++r.hits;
                missRun = 0;
            } else {
                ++missRun;
                if (missRun > r.maxMissRun) r.maxMissRun = missRun;
            }
        } else if (i == 0 && lang != AL_EN && (c == L'ъ' || c == L'ы' || c == L'ь')) {
            r.badStart = true;
        }
        prev = c;
        prevLetter = true;
    }
    return r;
}

// Правдоподобие слова в языке, 0..100
static int AsRate(const AsScore& s) {
    if (s.letters == 0) return 0;
    int r = s.pairs > 0 ? s.hits * 100 / s.pairs : 50;
    if (s.vowels == 0 && s.letters >= 2) r -= 30;
    if (s.maxConsRun > 4) r -= 25;
    if (s.maxMissRun >= 2) r -= 15;
    if (s.badStart) r -= 30;
    return r < 0 ? 0 : r;
}

// Смех и подобные повторяющиеся междометия (хахаха, пхпхпх, hahaha)
static bool AsLaughter(const std::wstring& c, int lang) {
    if (c.size() < 4 || lang == AL_NONE) return false;
    const wchar_t* set = (lang == AL_EN) ? L"haeiou" : L"хапыеиэоукфгіє";
    const wchar_t key = (lang == AL_EN) ? L'h' : L'х';
    int keys = 0;
    for (size_t i = 0; i < c.size(); ++i) {
        if (!wcschr(set, c[i])) return false;
        if (c[i] == key) ++keys;
    }
    return keys >= 2;
}

// Клавиатурный мусор: четыре соседние клавиши одного ряда подряд (asdf, qwer, zxcv)
static bool AsKeysmash(const std::wstring& s) {
    static const wchar_t* rows[] = {L"qwertyuiop", L"asdfghjkl", L"zxcvbnm", L"poiuytrewq", L"lkjhgfdsa", L"mnbvcxz"};
    if (s.size() < 4) return false;
    for (size_t i = 0; i + 4 <= s.size(); ++i) {
        const std::wstring sub = s.substr(i, 4);
        for (size_t k = 0; k < 6; ++k) {
            if (wcsstr(rows[k], sub.c_str())) return true;
        }
    }
    return false;
}

static bool AsBadChar(wchar_t c) {
    if (c >= L'0' && c <= L'9') return true;
    switch (c) {
        case L'@': case L'\\': case L'_': case L'#': case L'$': case L'%':
        case L'^': case L'&': case L'=': case L'+': case L'|':
            return true;
    }
    return false;
}

// Явные признаки того, что слово набрано не в той кириллической раскладке (русская <-> украинская).
// Эти сочетания невозможны в языке набранной раскладки, но естественны в языке соседней: буквы, которых "не хватает",
// лежат на клавишах ъ (ї), э (є), ы (і) и ё (апостроф) русской раскладки.
// w — набранное слово в нижнем регистре, langH — язык набранной раскладки.
static bool AsCyrStrong(const std::wstring& w, int langH) {
    const size_t n = w.size();
    if (langH == AL_RU) {
        if (AsLaughter(w, AL_RU)) return false;  // "ыхыхых" — обычный смех, а не признак украинского
        for (size_t i = 0; i < n; ++i) {
            const wchar_t c = w[i];
            if (c == L'ъ') {
                const bool legit = i > 0 && AsRuCons(w[i - 1]) && i + 1 < n && wcschr(L"еёюя", w[i + 1]);
                if (!legit) return true;  // ъ не между приставкой и е/ё/ю/я — это украинская ї
            } else if (c == L'ё' && i + 1 < n && wcschr(L"яюэъ", w[i + 1])) {
                return true;              // апостроф перед я/ю/є/ї (м'ясо, п'ять)
            } else if (c == L'ы') {
                if (i == 0 && n >= 3 && w.find_first_not_of(L'ы') != std::wstring::npos) return true;  // слово на "ы" — "і"
                if (i > 0 && wcschr(L"гкхжчшщ", w[i - 1])) return true;  // "ы" после г, к, х, ж, ч, ш, щ невозможна по-русски
            } else if (c == L'э' && i > 0 && i + 1 == n && w[i - 1] != L'э') {
                return true;              // слово, оканчивающееся на "э", — украинская "є"
            }
        }
    } else if (langH == AL_UK) {
        for (size_t i = 0; i < n; ++i) {
            if (AsIsApos(w[i]) && (i == 0 || i + 1 == n)) return true;     // апостроф в начале/конце слова — русская ё
            if (w[i] == L'ї' && i > 0 && AsUkCons(w[i - 1])) return true;  // ї после согласной — русская ъ
        }
    }
    return false;
}

// ---------------------------------------------------------------- состояние модуля
static bool g_asHeur = false;  // оценки по биграммам включены (AutoSwitch=1); иначе работает только Words.txt

static bool AsExcluded() { return ExMode(GetForegroundWindow()) != EX_NONE; }

// ---------------------------------------------------------------- словарь пользователя (Words.txt)
static std::set<std::wstring> g_asDict;
static FILETIME g_asDictTime = {0, 0};
static DWORD g_asDictCheck = 0;

static std::wstring AsDictPath() { return g_exeDir + L"Words.txt"; }

static void AsLoadDict() {
    g_asDict.clear();
    std::wstring ws;
    if (!ExReadUtf8(AsDictPath(), ws, &g_asDictTime)) return;
    size_t pos = 0;
    while (pos <= ws.size()) {
        size_t nl = ws.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = ws.size();
        const std::wstring line = ws.substr(pos, nl - pos);
        pos = nl + 1;
        size_t b = 0, e = line.size();
        while (b < e && (line[b] == L' ' || line[b] == L'\t' || line[b] == L'\r')) ++b;
        while (e > b && (line[e - 1] == L' ' || line[e - 1] == L'\t' || line[e - 1] == L'\r')) --e;
        if (b >= e || line[b] == L'#') continue;
        g_asDict.insert(AsLower(line.substr(b, e - b)));
    }
}

static void AsCreateDictTemplate() {
    if (GetFileAttributesW(AsDictPath().c_str()) != INVALID_FILE_ATTRIBUTES) return;
    HANDLE h = CreateFileW(AsDictPath().c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    const char* t =
        "# LangSwitch: словарь авто-переключения раскладки. Одно слово в строке, строки с # игнорируются, регистр не важен.\r\n"
        "# Слово из словаря всегда переключается в раскладку, где оно набирается как записано (высший приоритет).\r\n"
        "# Пример: если записать \"бг\", то набранное в английской раскладке \",u\" всегда станет \"бг\".\r\n"
        "# Если записать оба варианта (\"бг\" и \",u\"), переключения для этого слова не будет.\r\n";
    DWORD wr = 0;
    WriteFile(h, t, (DWORD)strlen(t), &wr, NULL);
    CloseHandle(h);
}

static void AsReloadDictIfChanged(DWORD now) {
    if (now - g_asDictCheck < 3000) return;
    g_asDictCheck = now;
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(AsDictPath().c_str(), GetFileExInfoStandard, &d)) {
        g_asDict.clear();
        g_asDictTime.dwLowDateTime = 0;
        g_asDictTime.dwHighDateTime = 0;
        return;
    }
    if (CompareFileTime(&d.ftLastWriteTime, &g_asDictTime) != 0) AsLoadDict();
}

// ---------------------------------------------------------------- решение
struct AsFix {
    size_t start;  // индекс первого символа области замены в буфере
    size_t end;    // конец области замены (не включая); (size_t)-1 — до конца буфера
    HKL target;    // раскладка, в которую нужно перевести область
};

// Какой алфавит у предыдущих слов строки (до 3 слов); 0 — неопределённо
static int AsContext(const std::vector<Rec>& buf, size_t s) {
    int cnt[3] = {0, 0, 0};
    int tokens = 0;
    size_t i = s;
    for (int t = 0; t < 3; ++t) {
        while (i > 0 && buf[i - 1].ch == L' ') --i;
        if (i == 0) break;
        const size_t e = i;
        while (i > 0 && buf[i - 1].ch != L' ') --i;
        int letters = 0;
        for (size_t k = i; k < e; ++k) {
            if (lm::IsLetter(buf[k].ch)) ++letters;
        }
        if (letters < 2) continue;
        const int sc = ScriptOfLayout(buf[e - 1].cur);
        if (sc == 1 || sc == 2) {
            ++cnt[sc];
            ++tokens;
        }
    }
    for (int sc = 1; sc <= 2; ++sc) {
        if (cnt[sc] >= 2 || (cnt[sc] == 1 && tokens == 1)) return sc;
    }
    return 0;
}

// Слово из 1–3 букв, допустимое в обоих языках (например, "vs" и "мы"): само по себе не переключается.
// H — раскладка, в которой набрано слово; T — раскладка, в которую его можно перевести.
static bool AsPendingShort(const std::vector<Rec>& buf, size_t ps, size_t pe, HKL H, HKL T) {
    if (pe - ps > 3) return false;
    std::wstring w;
    for (size_t i = ps; i < pe; ++i) {
        const Rec& r = buf[i];
        if (r.neutral || r.cur != H || r.home != H || !lm::IsLetter(r.ch)) return false;
        w += r.ch;
    }
    const int langH = AsLayoutLang(H), langT = AsLayoutLang(T);
    if (langH == AL_NONE || langT == AL_NONE) return false;
    std::wstring cv(w.size(), L' ');
    for (size_t i = 0; i < w.size(); ++i) cv[i] = ConvChar(w[i], H, T);
    return AsInShort(langH, AsLower(w)) && AsInShort(langT, AsLower(cv));
}

// Неоднозначные слова переключаются вместе со следующим уверенно распознанным словом, но только в начале текста:
// если до слова s стоят только такие слова (набранные в раскладке Hpend), начиная с начала текста,
// возвращает начало первого из них, иначе s.
static size_t AsLookBack(const std::vector<Rec>& buf, size_t s, HKL Hpend, HKL T) {
    size_t first = s;
    size_t p = s;
    for (int guard = 0; guard < 6; ++guard) {
        size_t pe = p;
        while (pe > 0 && buf[pe - 1].ch == L' ') --pe;
        if (pe == 0) return first;  // дошли до начала текста: все слова до этого были подходящими
        size_t ps = pe;
        while (ps > 0 && buf[ps - 1].ch != L' ') --ps;
        if (!AsPendingShort(buf, ps, pe, Hpend, T)) return s;
        first = ps;
        p = ps;
    }
    return s;
}

// Решает, нужно ли переключить последнее слово буфера. pause — проверка по паузе (пороги строже).
static bool AsDecide(const std::vector<Rec>& buf, bool pause, AsFix& out) {
    if (AsExcluded()) return false;
    out.end = (size_t)-1;
    size_t e = buf.size();
    while (e > 0 && buf[e - 1].ch == L' ') --e;
    if (e == 0) return false;
    size_t s = e;
    while (s > 0 && buf[s - 1].ch != L' ') --s;
    const size_t len = e - s;
    if (len == 0 || len > 40) return false;

    const HKL H = buf[s].cur;
    std::wstring w;
    w.reserve(len);
    for (size_t i = s; i < e; ++i) {
        const Rec& r = buf[i];
        if (r.neutral || r.cur != H || r.home != H) return false;  // только свежий текст, набранный в одной раскладке
        if (AsBadChar(r.ch)) return false;
        w += r.ch;
    }
    const int scrH = ScriptOfLayout(H);
    if (scrH == 0) return false;
    const Cycle cy = BuildCycle();
    const std::vector<HKL>& cands = (scrH == 1) ? cy.cyr : cy.lat;
    const int langH = AsLayoutLang(H);

    const int ctx = AsContext(buf, s);
    AsReloadDictIfChanged(GetTickCount());

    // раскладка предыдущего слова: при равной оценке предпочитаем её (например, украинскую, а не русскую)
    HKL prevHkl = NULL;
    for (size_t i = s; i-- > 0;) {
        if (buf[i].ch != L' ') { prevHkl = buf[i].cur; break; }
    }

    HKL bestT = NULL;
    int best = -1;
    for (size_t ci = 0; ci < cands.size(); ++ci) {
        const HKL T = cands[ci];
        const int scrT = ScriptOfLayout(T);
        const int langT = AsLayoutLang(T);

        std::wstring cv(w.size(), L' ');
        for (size_t i = 0; i < w.size(); ++i) cv[i] = ConvChar(w[i], H, T);

        // ядро слова: без знаков по краям. Край обрезается, если в целевой раскладке там знак препинания
        // (например, русская "ю" в начале слова — это точка английской раскладки: ".apk")
        size_t a = 0, b = w.size();
        while (a < b && !lm::IsLetter(cv[a])) ++a;
        while (b > a && !lm::IsLetter(cv[b - 1])) --b;
        if (a >= b || a > 2 || w.size() - b > 3) continue;
        bool allLetters = true;
        for (size_t i = a; i < b; ++i) {
            if (!lm::IsLetter(cv[i])) { allLetters = false; break; }
        }
        if (!allLetters) continue;

        const std::wstring core = AsLower(w.substr(a, b - a));
        const std::wstring ccore = AsLower(cv.substr(a, b - a));
        const size_t n = core.size();
        const bool edged = (a != 0 || b != w.size());
        size_t inner = 0;
        for (size_t i = a; i < b; ++i) {
            if (!lm::IsLetter(w[i])) ++inner;
        }

        // словарь пользователя сильнее оценок
        if (!g_asDict.empty()) {
            if (g_asDict.count(core)) return false;  // набранная форма объявлена верной
            if (g_asDict.count(ccore)) {
                out.start = s;
                out.target = T;
                return true;
            }
        }
        if (!g_asHeur) continue;  // режим только словаря

        const bool tKnown = langT != AL_NONE && AsInShort(langT, ccore);
        const bool typedKnown = langH != AL_NONE && AsInShort(langH, core);
        if (typedKnown) continue;  // допустимо в своей раскладке: само не переключается (начало текста — AsLookBack)

        // при равной оценке предпочитаем раскладку предыдущего слова, затем последнюю использованную
        const int bonus = (T == prevHkl) ? 6 : (ci == 0 ? 3 : 0);

        int score = -1;
        if (tKnown) {
            // известное слово или сокращение целевого языка (я, ты, apk, txt, docx...)
            if (n <= 3 && langH == AL_NONE) continue;
            if (n <= 2 && (pause || edged)) continue;
            // двухбуквенные сокращения могут совпасть с настоящим словом набранного языка ("ем" и "tv")
            if (n == 2 && langT == AL_EN && AsRate(AsScoreRuns(core, langH)) > 50) continue;
            score = (n <= 3 ? 60 : 70) + bonus;
        } else if (n == 3) {
            // Неизвестное слово из трёх букв. Переключаем, если в целевой раскладке оно выглядит как слово
            // (обе пары букв допустимы, есть гласная), а в набранной хотя бы одна пара невозможна.
            if (!pause && langT != AL_NONE && langH != AL_NONE) {
                const AsScore st = AsScoreRuns(ccore, langT, true);
                const AsScore sh = AsScoreRuns(core, langH);
                if (st.letters == 3 && st.pairs == 2 && st.hits == 2 && st.vowels >= 1 && !st.badStart &&
                    sh.hits < sh.pairs)
                    score = 55 + bonus;
            }
        } else if (n > 3) {
            if (AsKeysmash(scrH == 1 ? core : ccore)) continue;
            if (langT == AL_NONE) continue;
            const bool hKn = langH != AL_NONE;
            if (!hKn && inner == 0) continue;  // язык набранной раскладки неизвестен — только знаки внутри слова

            const AsScore st = AsScoreRuns(ccore, langT, true);
            if (st.letters < 4 || st.pairs < 3) continue;
            const int rc = AsRate(st);
            int rh = hKn ? AsRate(AsScoreRuns(core, langH)) : 50;
            if (inner > 0) rh = rh > 20 ? rh - 20 : 0;  // знак внутри слова в набранной раскладке — признак чужой раскладки

            const bool laugh = AsLaughter(ccore, langT) && (hKn ? !AsLaughter(core, langH) : inner > 0);
            if (laugh) {
                score = 90 + bonus;
            } else {
                int tmin = 65, diff = 30;
                if (ctx == scrT) { tmin = 55; diff = 20; }  // предыдущие слова уже в целевой раскладке: мягче
                if (pause) { tmin += 5; diff += 5; }
                if (rc < tmin || rc - rh < diff) continue;
                score = rc + bonus;
            }
        }
        if (score > best) {
            best = score;
            bestT = T;
        }
    }
    if (bestT) {
        out.start = AsLookBack(buf, s, H, bestT);  // неоднозначные слова в начале текста переключаются вместе с этим
        out.target = bestT;
        return true;
    }

    // Русская <-> украинская кириллица: переключаем, когда в слове есть буквы, которых "не хватает" в набранной
    // раскладке. Если все буквы слова одинаковы в обеих раскладках, переключать незачем.
    if (scrH == 2 && (langH == AL_RU || langH == AL_UK)) {
        int best2 = -1;
        for (size_t ci = 0; ci < cy.cyr.size(); ++ci) {
            const HKL T = cy.cyr[ci];
            const int langT = AsLayoutLang(T);
            if (T == H || (langT != AL_RU && langT != AL_UK) || langT == langH) continue;

            std::wstring cv(w.size(), L' ');
            for (size_t i = 0; i < w.size(); ++i) cv[i] = ConvChar(w[i], H, T);
            if (cv == w) continue;

            auto keep = [&](size_t i) -> bool {
                return lm::IsLetter(w[i]) || lm::IsLetter(cv[i]) || AsIsApos(w[i]) || AsIsApos(cv[i]);
            };
            size_t a = 0, b = w.size();
            while (a < b && !keep(a)) ++a;
            while (b > a && !keep(b - 1)) --b;
            if (a >= b) continue;
            bool ok = true;
            for (size_t i = a; i < b; ++i) {
                if (!(lm::IsLetter(w[i]) || AsIsApos(w[i])) || !(lm::IsLetter(cv[i]) || AsIsApos(cv[i]))) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;

            const std::wstring core = AsLower(w.substr(a, b - a));
            const std::wstring ccore = AsLower(cv.substr(a, b - a));
            if (core == ccore) continue;

            if (!g_asDict.empty()) {
                if (g_asDict.count(core)) return false;
                if (g_asDict.count(ccore)) {
                    out.start = s;
                    out.target = T;
                    return true;
                }
            }
            if (!g_asHeur) continue;

            const AsScore sc = AsScoreRuns(ccore, langT);
            const int rc = AsRate(sc);
            const int rh = AsRate(AsScoreRuns(core, langH));
            const int bonus2 = (T == prevHkl) ? 6 : 0;

            int score = -1;
            if (sc.letters >= 2 && AsCyrStrong(core, langH)) {
                if (rc >= 45) score = 80 + bonus2;  // явный признак чужой кириллической раскладки
            } else if (sc.letters <= 3) {
                // короткие слова: только если предыдущее слово уже набрано в целевой раскладке
                if (T == prevHkl && AsInShort(langT, ccore) && !AsInShort(langH, core)) score = 60;
            } else {
                int tmin = 75, diff = 30;
                if (T == prevHkl) { tmin = 65; diff = 20; }
                if (pause) { tmin += 5; diff += 5; }
                if (rc >= tmin && rc - rh >= diff) score = rc + bonus2;
            }
            if (score > best2) {
                best2 = score;
                bestT = T;
            }
        }
        if (bestT) {
            out.start = s;
            out.target = bestT;
            return true;
        }
    }

    // Слова в начале текста, допустимые в двух языках ("vs" и "мы"), переключаются, если следующее слово набрано
    // в другой раскладке и уверенно принадлежит ей (например, "vs при": меняется только начало)
    if (g_asHeur && !pause && s > 0 && langH != AL_NONE) {
        bool allLetters = true;
        for (size_t i = 0; i < w.size(); ++i) {
            if (!lm::IsLetter(w[i])) { allLetters = false; break; }
        }
        if (allLetters) {
            const std::wstring lw = AsLower(w);
            const AsScore sw = AsScoreRuns(lw, langH);
            const bool confident = AsInShort(langH, lw) || (sw.letters >= 3 && AsRate(sw) >= 75);
            if (confident) {
                size_t p = s;
                while (p > 0 && buf[p - 1].ch == L' ') --p;
                if (p > 0) {
                    const HKL P = buf[p - 1].cur;
                    const int scrP = ScriptOfLayout(P);
                    if (P != H && scrP != 0 && scrP != scrH) {
                        const size_t st = AsLookBack(buf, s, P, H);
                        if (st < s) {
                            out.start = st;
                            out.end = s;
                            out.target = H;
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------- замена слова (отдельный поток)
struct AsEv {
    WORD vk;
    WORD scan;
    bool ext;
    bool shift;
};
struct AsJob {
    size_t regionLen;  // сколько символов с конца буфера заменить (слово и пробел после него)
    HKL target;
    size_t convEnd;    // конец области, которую нужно перевести в target ((size_t)-1 — до конца буфера)
    bool sendEnter;    // после замены отправить Enter (он был задержан)
    size_t snapSize;   // размер буфера в момент решения
};

static std::atomic<bool> g_asBusy(false);
static HWND g_asWnd = NULL;
static CRITICAL_SECTION g_asCs;
static std::vector<AsEv> g_asQueue;  // клавиши, нажатые во время замены
static bool g_asSw[256] = {false};   // клавиши, нажатие которых мы задержали (под g_asCs)
static AsJob g_asJob;
static const UINT_PTR kAsTimer = 1;
static const UINT kAsPauseMs = 1000;

static bool AsIsConsole(HWND fg) {
    wchar_t cls[64] = {0};
    if (fg) GetClassNameW(fg, cls, 64);
    return !wcscmp(cls, L"ConsoleWindowClass") || !wcscmp(cls, L"CASCADIA_HOSTING_WINDOW_CLASS");
}

static bool AsIsPassThrough(DWORD vk) {
    return IsModifierVk(vk) || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL;
}

static bool AsFocusIsPassword() {
    bool pw = false;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* el = NULL;
    if (CreateUia(&uia) && SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
        BOOL b = FALSE;
        if (SUCCEEDED(el->get_CurrentIsPassword(&b)) && b) pw = true;
    }
    if (el) el->Release();
    if (uia) uia->Release();
    return pw;
}

static INPUT AsPlainKey(WORD vk, WORD scan, bool ext, bool up) {
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = scan;
    in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (ext ? KEYEVENTF_EXTENDEDKEY : 0);
    in.ki.dwExtraInfo = 0;  // без метки: хук обработает эти нажатия как обычный ввод
    return in;
}

// Завершение замены: воспроизводим то, что пользователь печатал во время замены (уже в новой раскладке)
static void AsFinish() {
    std::vector<AsEv> q;
    EnterCriticalSection(&g_asCs);
    q.swap(g_asQueue);
    ZeroMemory(g_asSw, sizeof(g_asSw));
    g_asBusy = false;
    LeaveCriticalSection(&g_asCs);
    if (q.empty()) return;

    std::vector<INPUT> in;
    for (size_t i = 0; i < q.size(); ++i) {
        const AsEv& ev = q[i];
        const bool shiftNow = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool wrap = ev.shift && !shiftNow;
        if (wrap) in.push_back(KeyInput(VK_LSHIFT, false));
        in.push_back(AsPlainKey(ev.vk, ev.scan, ev.ext, false));
        in.push_back(AsPlainKey(ev.vk, ev.scan, ev.ext, true));
        if (wrap) in.push_back(KeyInput(VK_LSHIFT, true));
    }
    SendBatch(in);
}

static unsigned __stdcall AsFixThread(void*) {
    const HRESULT coHr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const AsJob job = g_asJob;
    Sleep(15);  // даём пробелу дойти до приложения

    std::vector<Rec> snap;
    {
        Lock l;
        snap = g_buf;
    }
    HWND fg = GetForegroundWindow();
    if (fg && snap.size() == job.snapSize && snap.size() >= job.regionLen) {
        bool allow = true;
        const GameInfo gi = QueryGame(fg);
        if (!gi.game && GetFocusKind() == FK_NOTEXT) allow = false;
        if (allow && AsFocusIsPassword()) allow = false;  // пароли не трогаем
        if (allow) {
            Cycle cy = BuildCycle();
            if (ScriptOfLayout(job.target) == 2) RotateTo(cy.cyr, (UINT_PTR)job.target);
            else RotateTo(cy.lat, (UINT_PTR)job.target);
            const size_t st = snap.size() - job.regionLen;
            ConvertBuffer(snap, cy, st, job.target, job.convEnd);
            // слово переключено автоматически: двойной Ctrl дальше работает с текстом от него
            Lock l;
            if (!job.sendEnter && !g_resetAfter && g_buf.size() > st) g_buf[st].asStart = true;
        }
    }

    if (job.sendEnter) {
        {
            Lock l;
            g_buf.clear();
            g_chainActive = false;
            g_selHint = false;
            g_selForce = false;
        }
        if (fg && QueryGame(fg).game) GameDisarm();
        std::vector<INPUT> in;
        in.push_back(KeyInput(VK_RETURN, false));
        in.push_back(KeyInput(VK_RETURN, true));
        SendBatch(in);
    }

    AsFinish();
    if (SUCCEEDED(coHr)) CoUninitialize();
    return 0;
}

static bool AsStartJob(const AsJob& j) {
    if (g_asBusy.exchange(true)) return false;
    g_asJob = j;
    const uintptr_t th = _beginthreadex(NULL, 0, AsFixThread, NULL, 0, NULL);
    if (th) {
        CloseHandle((HANDLE)th);
        return true;
    }
    g_asBusy = false;
    return false;
}

// ---------------------------------------------------------------- точки входа из хука и таймера
// Откуда начинать ручную конвертацию (двойной Ctrl): с последнего слова, переключённого автоматически.
// Если такого нет — (size_t)-1, и работает обычная настройка Scope (весь текст или последнее слово).
static size_t AsManualStart(const std::vector<Rec>& snap) {
    if (!g_cfg.wholeText) return (size_t)-1;
    for (size_t i = snap.size(); i-- > 0;) {
        if (snap[i].asStart) return i;
    }
    return (size_t)-1;
}

static void AsOnTimer() {
    KillTimer(g_asWnd, kAsTimer);
    if (g_asBusy || g_busy) return;
    HWND fg = GetForegroundWindow();
    if (!fg || fg != g_lastFg || AsIsConsole(fg)) return;
    std::vector<Rec> snap;
    {
        Lock l;
        snap = g_buf;
    }
    if (snap.empty() || snap.back().ch == L' ') return;
    AsFix fx;
    if (!AsDecide(snap, true, fx)) return;
    AsJob j;
    j.regionLen = snap.size() - fx.start;
    j.target = fx.target;
    j.convEnd = fx.end;
    j.sendEnter = false;
    j.snapSize = snap.size();
    AsStartJob(j);
}

static LRESULT CALLBACK AsWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER && w == kAsTimer) {
        AsOnTimer();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Во время замены нажатия обычных клавиш откладываем (true — нажатие задержано)
static bool AsSwallow(const KBDLLHOOKSTRUCT* k, bool down) {
    const DWORD vk = k->vkCode;
    if (vk >= 256) return false;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool hot = ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_LWIN) |
                       GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
    bool swallow = false;
    EnterCriticalSection(&g_asCs);
    if (g_asBusy) {
        if (down) {
            if (!hot) {
                if (g_asQueue.size() < 32) {
                    AsEv ev;
                    ev.vk = (WORD)vk;
                    ev.scan = (WORD)k->scanCode;
                    ev.ext = (k->flags & LLKHF_EXTENDED) != 0;
                    ev.shift = shift;
                    g_asQueue.push_back(ev);
                }
                g_asSw[vk] = true;
                swallow = true;
            }
        } else if (g_asSw[vk]) {
            g_asSw[vk] = false;
            swallow = true;
        }
    }
    LeaveCriticalSection(&g_asCs);
    return swallow;
}

static bool AsEnterPlain() {
    return ((GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU) |
             GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) == 0;
}

// Enter перед отправкой: если последнее слово набрано не в той раскладке — задерживаем Enter, чтобы заменить слово
static bool AsWantsEnter() {
    if (g_busy) return false;
    HWND fg = GetForegroundWindow();
    if (!fg || AsIsConsole(fg) || AsExcluded()) return false;
    std::vector<Rec> snap;
    {
        Lock l;
        snap = g_buf;
    }
    if (snap.empty() || snap.back().ch == L' ') return false;
    AsFix fx;
    if (!AsDecide(snap, false, fx)) return false;
    AsJob j;
    j.regionLen = snap.size() - fx.start;
    j.target = fx.target;
    j.convEnd = fx.end;
    j.sendEnter = true;
    j.snapSize = snap.size();
    return AsStartJob(j);
}

// Вызывается после обработки нажатия основным обработчиком (буфер уже обновлён)
static void AsAfterKey(DWORD vk) {
    if (AsIsPassThrough(vk) || g_busy) return;
    if (AsExcluded()) {
        KillTimer(g_asWnd, kAsTimer);
        return;
    }
    bool open = false, spaced = false;
    {
        Lock l;
        const size_t n = g_buf.size();
        open = n > 0 && g_buf[n - 1].ch != L' ';
        spaced = vk == VK_SPACE && n >= 2 && g_buf[n - 1].ch == L' ' && g_buf[n - 2].ch != L' ';
    }
    if (spaced && !g_asBusy) {
        HWND fg = GetForegroundWindow();
        if (fg && !AsIsConsole(fg)) {
            std::vector<Rec> snap;
            {
                Lock l;
                snap = g_buf;
            }
            AsFix fx;
            if (AsDecide(snap, false, fx)) {
                AsJob j;
                j.regionLen = snap.size() - fx.start;  // область замены и всё, что набрано после неё
                j.target = fx.target;
                j.convEnd = fx.end;
                j.sendEnter = false;
                j.snapSize = snap.size();
                AsStartJob(j);
            }
        }
    }
    if (open) SetTimer(g_asWnd, kAsTimer, kAsPauseMs, NULL);
    else KillTimer(g_asWnd, kAsTimer);
}

// Обработчик клавиатуры при включённом модуле: вызывает основной обработчик и добавляет проверку слов
static LRESULT CALLBACK AsKbProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return KbProc(code, wp, lp);
    const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
    if (k->dwExtraInfo == kMarker) return KbProc(code, wp, lp);
    const bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
    const DWORD vk = k->vkCode;

    if (g_asBusy && !AsIsPassThrough(vk) && AsSwallow(k, down)) return 1;
    if (down && vk == VK_RETURN && !g_asBusy && AsEnterPlain() && AsWantsEnter()) return 1;

    const LRESULT r = KbProc(code, wp, lp);
    if (down) AsAfterKey(vk);
    return r;
}

// Выбор обработчика клавиатуры при запуске.
// AutoSwitch=1 — полный режим (оценки + словарь); AutoSwitch=0 и есть Words.txt — только словарь;
// иначе возвращается обычный обработчик, ничего не инициализируя (работа программы как без этого модуля).
static HOOKPROC AsSetup() {
    const wchar_t* ini = g_iniPath.c_str();
    wchar_t buf[8] = {0};
    GetPrivateProfileStringW(kSection, L"AutoSwitch", L"", buf, 8, ini);
    if (!buf[0]) WritePrivateProfileStringW(kSection, L"AutoSwitch", L"0", ini);
    g_asHeur = buf[0] && _wtoi(buf) != 0;

    if (g_asHeur) AsCreateDictTemplate();
    AsLoadDict();

    // Автоопределение выключено и в Words.txt нет слов: работает обычный обработчик клавиатуры, как без этого модуля
    if (!g_asHeur && g_asDict.empty()) return KbProc;

    InitializeCriticalSection(&g_asCs);
    HINSTANCE hInst = GetModuleHandleW(NULL);
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = AsWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"LangSwitchAsWnd";
    RegisterClassW(&wc);
    g_asWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);
    if (!g_asWnd) return KbProc;
    return AsKbProc;
}
