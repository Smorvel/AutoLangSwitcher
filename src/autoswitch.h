// autoswitch.h — опциональное авто-переключение раскладки (русская <-> английская) и словарь Words.txt.
// Включается строкой AutoSwitch=1 в LangSwitch.ini. Словарь Words.txt работает и при AutoSwitch=0, если в нём есть слова.
// Подключается из main.cpp одной директивой #include и использует его внутренние функции, поэтому файл не
// компилируется отдельно. Чтобы убрать функцию, удалите этот файл и правки в main.cpp
// (ConvertBuffer: параметры forceStart/forceTarget/forceEnd, #include перед ConvertThread, WinMain: AsSetup, AsManualStart).
#pragma once

// определены в main.cpp ниже места подключения
static LRESULT CALLBACK KbProc(int code, WPARAM wp, LPARAM lp);
static bool IsModifierVk(DWORD vk);

enum { AL_NONE = 0, AL_EN = 1, AL_RU = 2 };

// ---------------------------------------------------------------- таблицы (частые буквосочетания)
static const wchar_t* kAsBigEn =
    L"th he in er an re on at en nd ti es or te of ed is it al ar st to nt ng se ha as ou io le ve co me de hi ri ro ic "
    L"ne ea ra ce li ch ll be ma si om ur ca el ta la ns di fo ho pe ec pr no ct us ac ot il tr ly nc et ut ss so rs un "
    L"lo wa ge ie wh ee wi em ad ol rt po we na ul ni ts mo ow pa im mi ai sh ir su id os iv ia am fi ci vi pl ig tu ev "
    L"ld ry mp fe bl ab gh ty op wo sa ay ex ke fr oo av ag if ap gr od bo sp rd do uc bu ei ov by rm ep tt oc fa ef cu "
    L"rn sc gi da yo cr cl du ga qu ue ff ba ey ls va um pp ua up lu go ht ru ug ds lt pi rc rr eg au ck ew bi br tl ms "
    L"ye ud mb ip ub oi oa ok ob ib ak ik ph lk lf lp lm rk rb rg rl rv rp nk nn nl nm gn gs gl hy hr kn kl ks ki ku ko "
    L"ka za ze zi zo xt xp yp yr ys yt yn ym ur us ut ve vi vo eb eg ej ek ep ew ez ob oj ok ow oz uf ug uk ul um un up "
    L"wn wr wl ws wd rw ww yb yc yd yf yg yl yw yi ya yu ax ay az aw ah ab ac af ag ak ap aq au av az ib id if ig ik "
    L"ix iz oh ox oy uy uz ym dl bs fs ds ps pt sk sl sm sn sw tc tw ft lt lv lb rf gm gt hm hn ml mn sy sr my ny dy "
    L"cy gy fy xe xi xa xo xu ja jo je ju zy zz rh";

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
    L"пь пё рё рю рь сё сю ся сь тё тю тя ть фя фю фь хё хю хя цю цё ща щу щё ое ио аи еа оа уа ри ра ря "
    L"бт вв вг вз вп вх вц гв дв дг дж дз дм дп дс дт дц жж жк жм жр зг зл зс кв кц лд лз лл лх лц мд мк мл мн мр мц мш нж "
    L"нн нц пн пс пт пц пч рб рв рл рм рп рх рч рш сб сг сд сж сз сх сц сч тл тм тс тх тц фл хв хл хм хн хр хт цв чк шк шт щн "
    L"ео уо иа ия иу ый ыми ид ще";

// короткие служебные слова (до 3 букв): по буквосочетаниям такие слова оценить нельзя
static const wchar_t* kAsFuncEn =
    L"a i am an as at be by do go he if in is it me my no of oh ok on or so to up us we the and are but can for her him "
    L"his how its not now one our out she you all any day get has had was who why yes let may new off old own say see two "
    L"way too use did put set try ask end big bad";

static const wchar_t* kAsFuncRu =
    L"я в и к с у о а на не но да ты мы он за из по до от ну же бы ли то во ко со об ни ой ах ох эх уж его все она они "
    L"уже или для мне нет вот тут там при без над под про чем кто где так еще ещё это что как вам вас нам нас мой наш "
    L"ваш был ага ого ура оно ему ней них ним сам сей тот той эта эти";

// сокращения, расширения файлов, технические термины: допустимы по-английски, но по-русски их тоже пишут
// (пдф, апк), поэтому сами по себе без контекста не переключаются
static const wchar_t* kAsTechEn =
    L"vs pc tv id ip os ui ux hr pr qa ai ml db js ts pm cv ps mr dr ms st nd rd th ft kg km cm mm gb mb kb tb hz "
    L"pdf api url sql css cpu gpu ram rom usb ssd hdd png jpg jpeg gif svg bmp ico psd dvd ceo faq fyi lol omg btw wtf vip "
    L"vpn app web org com dev bot bug git "
    L"apk txt exe dll zip rar doc docx xls xlsx ppt pptx csv rtf odt iso bat cmd ini cfg tmp sys bin msi ipa dmg deb "
    L"rpm html htm php xml json yaml yml java cpp lua bash mov wav flac ogg webm webp mkv avi tar "
    L"http https www ftp ssh ssl tls dns tcp udp lan wan gui cli sdk ide wifi bios ios mac npm pip";

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
    static std::set<unsigned> en, ru;
    static bool init = false;
    if (!init) {
        AsFillBigrams(en, kAsBigEn);
        AsFillBigrams(ru, kAsBigRu);
        init = true;
    }
    const unsigned key = ((unsigned)a << 16) | (unsigned)b;
    return (lang == AL_EN ? en : ru).count(key) != 0;
}

static bool AsInFunc(int lang, const std::wstring& w) {
    static std::set<std::wstring> en, ru;
    static bool init = false;
    if (!init) {
        AsFillWords(en, kAsFuncEn);
        AsFillWords(ru, kAsFuncRu);
        init = true;
    }
    return (lang == AL_EN ? en : ru).count(w) != 0;
}

// английское сокращение / расширение файла / технический термин (не обычное слово)
static bool AsInTech(const std::wstring& w) {
    static std::set<std::wstring> tech;
    static bool init = false;
    if (!init) {
        AsFillWords(tech, kAsTechEn);
        init = true;
    }
    return tech.count(w) != 0 && !AsInFunc(AL_EN, w);
}

static bool AsInShort(int lang, const std::wstring& w) { return AsInFunc(lang, w) || (lang == AL_EN && AsInTech(w)); }

static bool AsVowel(int lang, wchar_t c) {
    const wchar_t* v = (lang == AL_EN) ? L"aeiouy" : L"аеёиоуыэюя";
    return c != 0 && wcschr(v, c) != NULL;
}

static std::wstring AsLower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

// Язык раскладки. Латиница: английский язык Windows или QWERTY-раскладка с английскими буквами.
// Кириллица: русская (есть ы и э, нет украинских і ї є). Остальные раскладки (украинская и др.) не поддерживаются.
static int AsLayoutLang(HKL h) {
    static std::map<UINT_PTR, int> cache;
    const std::map<UINT_PTR, int>::const_iterator it = cache.find((UINT_PTR)h);
    if (it != cache.end()) return it->second;
    int lang = AL_NONE;
    const int script = ScriptOfLayout(h);
    if (script == 1) {
        if (PRIMARYLANGID(LOWORD((UINT_PTR)h)) == LANG_ENGLISH) {
            lang = AL_EN;
        } else {
            static const wchar_t* qw = L"qwertyuiop";
            bool qwerty = true;
            for (int i = 0; i < 10; ++i) {
                wchar_t c = 0;
                if (!KeyChar(h, (UINT)(0x10 + i), false, false, c) || c != qw[i]) { qwerty = false; break; }
            }
            if (qwerty) lang = AL_EN;
        }
    } else if (script == 2) {
        const std::set<wchar_t>& ch = LayoutChars(h);
        if (ch.count(L'ы') && ch.count(L'э') && !ch.count(L'і') && !ch.count(L'ї') && !ch.count(L'є')) lang = AL_RU;
    }
    cache[(UINT_PTR)h] = lang;
    return lang;
}

// ---------------------------------------------------------------- оценка слова
// Результат оценки слова в языке: rate 0..100, bad — слово не может быть словом языка, hard — есть невозможное
// сочетание (а не просто нет гласных: без гласных бывают сокращения)
struct AsE {
    int rate;
    bool bad;
    bool hard;
    int letters;
    AsE() : rate(0), bad(false), hard(false), letters(0) {}
};

static bool AsPairImpossible(int lang, wchar_t a, wchar_t b) {
    if (lang == AL_EN) {
        static const wchar_t* bad[] = {L"jq", L"qj", L"qz", L"zq", L"xq", L"qx", L"vq", L"qv", L"wq", L"jx",
                                       L"xj", L"jz", L"zj", L"vx", L"xv", L"zx", L"xz", L"vj", L"jv"};
        for (const wchar_t* p : bad) {
            if (a == p[0] && b == p[1]) return true;
        }
        return false;
    }
    if (b == L'ь' && AsVowel(lang, a)) return true;
    if (b == L'ъ' && AsVowel(lang, a)) return true;
    if (a == L'ъ' && !wcschr(L"еёюя", b)) return true;
    if (a == L'ь' && wcschr(L"ыэъь", b)) return true;
    if (wcschr(L"жшчщгкх", a) && b == L'ы') return true;
    if (wcschr(L"жшчщ", a) && b == L'я') return true;
    return false;
}

// Вес пары букв: 100 — частая, 70 — допустимая (чередование гласной и согласной), 40 — редкая
static int AsPairWeight(int lang, wchar_t a, wchar_t b) {
    if (AsHasBigram(lang, a, b)) return 100;
    return AsVowel(lang, a) != AsVowel(lang, b) ? 70 : 40;
}

// Оценка непрерывной группы букв
static void AsRunEval(const std::wstring& run, int lang, int& pairs, int& sum, bool& bad, bool& hard) {
    const size_t n = run.size();
    int vowels = 0, cons = 0, same = 1;
    for (size_t i = 0; i < n; ++i) {
        const wchar_t c = run[i];
        if (AsVowel(lang, c)) {
            ++vowels;
            cons = 0;
        } else if (++cons >= 5) {
            bad = true;
            hard = true;
        }
        if (i == 0) continue;
        const wchar_t p = run[i - 1];
        ++pairs;
        if (AsPairImpossible(lang, p, c)) {
            bad = true;
            hard = true;
        } else {
            sum += AsPairWeight(lang, p, c);
        }
        if (c == p) {
            if (++same >= 3) { bad = true; hard = true; }
        } else {
            same = 1;
        }
    }
    if (n >= 2 && vowels == 0) bad = true;
    if (lang == AL_EN) {
        for (size_t i = 0; i < n; ++i) {
            if (run[i] == L'q' && (i + 1 >= n || run[i + 1] != L'u')) { bad = true; hard = true; }
        }
    } else if (n > 0 && (run[0] == L'ъ' || run[0] == L'ы' || run[0] == L'ь')) {
        bad = true;
        hard = true;
    }
}

// Оценка слова (в нижнем регистре) как слова языка lang. В английском слове не бывает , ; [ ] < > { } : " внутри
// и апострофа в начале: если они есть, текст, скорее всего, набран в чужой раскладке (там это буквы б ж х ъ э).
static AsE AsEval(const std::wstring& s, int lang) {
    AsE r;
    int pairs = 0, sum = 0;
    std::wstring run;
    for (size_t i = 0; i <= s.size(); ++i) {
        const wchar_t c = i < s.size() ? s[i] : L'\0';
        if (c != L'\0' && lm::IsLetter(c)) {
            run += c;
            ++r.letters;
            continue;
        }
        if (!run.empty()) {
            AsRunEval(run, lang, pairs, sum, r.bad, r.hard);
            run.clear();
        }
        if (c != L'\0' && lang == AL_EN && wcschr(L",;[]<>{}:\"", c)) { r.bad = true; r.hard = true; }
        if (c == L'\'' && i == 0 && lang == AL_EN) { r.bad = true; r.hard = true; }
    }
    r.rate = pairs > 0 ? sum / pairs : (r.letters > 0 ? 50 : 0);
    return r;
}

// Повторяющийся шаблон длиной 1-3 символа (ахах, ыыыы, hehe)
static bool AsIsPattern(const std::wstring& s) {
    const size_t n = s.size();
    if (n < 4) return false;
    for (size_t p = 1; p <= 3 && 2 * p <= n; ++p) {
        bool ok = true;
        for (size_t i = p; i < n && ok; ++i) {
            if (s[i] != s[i - p]) ok = false;
        }
        if (ok) return true;
    }
    return false;
}

// Смех и подобные междометия (хахаха, пхпхпх, ыыыы, hahaha)
static bool AsLaughter(const std::wstring& c, int lang) {
    if (c.size() < 4 || lang == AL_NONE) return false;
    const wchar_t* set = (lang == AL_EN) ? L"haeiou" : L"хапыеиэоукфгм";
    const wchar_t key = (lang == AL_EN) ? L'h' : L'х';
    int keys = 0;
    for (size_t i = 0; i < c.size(); ++i) {
        if (!wcschr(set, c[i])) return false;
        if (c[i] == key) ++keys;
    }
    return keys >= 2 || AsIsPattern(c);
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

// ---------------------------------------------------------------- состояние модуля
static bool g_asHeur = false;  // оценки включены (AutoSwitch=1); иначе работает только Words.txt

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

// ---------------------------------------------------------------- слова строки и их оценка
struct AsFix {
    size_t start;  // индекс первого символа области замены в буфере
    size_t end;    // конец области замены (не включая); (size_t)-1 — до конца буфера
    HKL target;    // раскладка, в которую нужно перевести область
};

// Классы слова (по набранному тексту и его чтению в другой раскладке)
enum {
    AC_R = 0,     // слово верное, не трогаем
    AC_W = 1,     // слово точно набрано не в той раскладке
    AC_SK = 2,    // служебное слово другой раскладки: нужны подтверждения (вес weight)
    AC_TECH = 3,  // английское сокращение/расширение, набранное по-русски: переключается только с контекстом
    AC_AMB = 4,   // допустимо в обоих языках ("vs" и "мы"): само не переключается
    AC_U0 = 5,    // неясно
    AC_BAR = 6    // не оценивается (смешанная раскладка, цифры, чужая раскладка)
};

struct AsTok {
    size_t ps = 0, pe = 0;
    int cls = AC_BAR;
    HKL H = NULL, T = NULL;
    int langH = AL_NONE, langT = AL_NONE;
    int weight = 0;
    bool listed = false;
    bool dot = false;
};

// Язык предыдущих слов строки (до 3 слов, по раскладке, в которой они сейчас стоят); 0 — неопределённо
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
        const int lg = AsLayoutLang(buf[e - 1].cur);
        if (lg == AL_EN || lg == AL_RU) {
            ++cnt[lg];
            ++tokens;
        }
    }
    for (int lg = 1; lg <= 2; ++lg) {
        if (cnt[lg] >= 2 || (cnt[lg] == 1 && tokens == 1)) return lg;
    }
    return 0;
}

static bool AsAtStart(const std::vector<Rec>& buf, size_t pos) {
    for (size_t i = 0; i < pos && i < buf.size(); ++i) {
        if (buf[i].ch != L' ') return false;
    }
    return true;
}

// Последние слова буфера (до maxTokens), от первого к последнему
static std::vector<AsTok> AsTokens(const std::vector<Rec>& buf, size_t maxTokens) {
    std::vector<AsTok> v;
    size_t e = buf.size();
    while (v.size() < maxTokens) {
        while (e > 0 && buf[e - 1].ch == L' ') --e;
        if (e == 0) break;
        size_t s = e;
        while (s > 0 && buf[s - 1].ch != L' ') --s;
        AsTok t;
        t.ps = s;
        t.pe = e;
        v.push_back(t);
        e = s;
    }
    std::reverse(v.begin(), v.end());
    return v;
}

// Определяет класс слова. Слово переключается, только когда оно неправдоподобно в раскладке набора
// и правдоподобно в другой; сомнительные слова остаются без изменений и ждут соседей.
static void AsClassify(const std::vector<Rec>& buf, AsTok& t, const Cycle& cy, bool pause) {
    t.cls = AC_BAR;
    t.weight = 0;
    t.listed = false;
    t.dot = false;
    t.H = t.T = NULL;
    t.langH = t.langT = AL_NONE;

    const size_t len = t.pe - t.ps;
    if (len == 0 || len > 40) return;
    const HKL H = buf[t.ps].cur;
    std::wstring w;
    for (size_t i = t.ps; i < t.pe; ++i) {
        const Rec& r = buf[i];
        if (r.neutral || r.cur != H || r.home != H || AsBadChar(r.ch)) return;  // только свежий текст одной раскладки
        w += r.ch;
    }
    const int scrH = ScriptOfLayout(H);
    const int langH = AsLayoutLang(H);
    if (langH == AL_NONE) return;

    HKL T = NULL;
    const std::vector<HKL>& cand = (scrH == 1) ? cy.cyr : cy.lat;
    for (size_t i = 0; i < cand.size(); ++i) {
        const int lg = AsLayoutLang(cand[i]);
        if (lg != AL_NONE && lg != langH) { T = cand[i]; break; }
    }
    if (!T) return;
    const int langT = AsLayoutLang(T);

    t.H = H;
    t.T = T;
    t.langH = langH;
    t.langT = langT;
    t.cls = AC_R;  // по умолчанию слово не трогаем

    std::wstring cv(w.size(), L' ');
    for (size_t i = 0; i < w.size(); ++i) cv[i] = ConvChar(w[i], H, T);

    // ядро слова: без знаков по краям. Край обрезается, если в другой раскладке там знак препинания
    // (например, русская "ю" в начале слова — точка английской раскладки: ".apk")
    size_t a = 0, b = w.size();
    while (a < b && !lm::IsLetter(cv[a])) ++a;
    while (b > a && !lm::IsLetter(cv[b - 1])) --b;
    if (a >= b || a > 2 || w.size() - b > 3) return;

    const std::wstring lw = AsLower(w.substr(a, b - a));
    const std::wstring lc = AsLower(cv.substr(a, b - a));
    const size_t n = lw.size();
    t.dot = (a > 0 && cv[a - 1] == L'.');

    // словарь пользователя сильнее оценок
    if (!g_asDict.empty()) {
        if (g_asDict.count(lw)) return;  // набранная форма объявлена верной
        if (g_asDict.count(lc)) { t.cls = AC_W; return; }
    }
    if (!g_asHeur) return;  // режим только словаря

    bool lcLetters = true;
    for (size_t i = 0; i < lc.size(); ++i) {
        if (!lm::IsLetter(lc[i])) { lcLetters = false; break; }
    }
    const AsE eH = AsEval(lw, langH);
    AsE eT = AsEval(lc, langT);
    if (!lcLetters) eT.bad = true;

    const bool typedKnown = AsInShort(langH, lw);
    const bool tFunc = lcLetters && AsInFunc(langT, lc);
    const bool tTech = lcLetters && langT == AL_EN && AsInTech(lc);
    const bool laughH = AsLaughter(lw, langH);
    const bool laughT = lcLetters && AsLaughter(lc, langT);
    const int ctx = AsContext(buf, t.ps);

    if (typedKnown) {  // допустимо в своей раскладке
        t.cls = (tFunc || tTech) ? AC_AMB : AC_R;
        return;
    }
    if (laughH) return;
    if (laughT) { t.cls = AC_W; return; }

    if (tFunc && n <= 3) {  // служебное слово другой раскладки
        if (eH.bad) {
            t.cls = AC_W;  // набранное — не слово (нет гласных, невозможные сочетания)
        } else if (pause && n <= 2) {
            t.cls = AC_U0;
        } else {
            t.cls = AC_SK;
            t.listed = true;
            t.weight = (n == 1) ? 1 : 4;
        }
        return;
    }
    if (tTech) {
        t.cls = t.dot ? AC_W : AC_TECH;  // точка перед словом (".apk") — явный признак
        return;
    }
    if (n <= 2) { t.cls = AC_U0; return; }

    // Общий случай: слово переключается, если в набранной раскладке оно неправдоподобно,
    // а в другой правдоподобно (или читается заметно лучше)
    const bool unk3 = (n == 3);
    int relDiff = 25, tmin = unk3 ? 95 : 75;
    if (ctx == langH) {  // до этого текст был в той же раскладке: строже
        relDiff = 40;
        if (tmin < 85) tmin = 85;
    } else if (ctx == langT) {  // предыдущие слова уже в другой раскладке: мягче
        relDiff = 15;
    }
    if (pause) {
        relDiff = 35;
        if (tmin < 85) tmin = 85;
    }
    const bool goodT = !eT.bad && eT.rate >= tmin;
    const bool badH = eH.bad || eH.rate <= 45 || (goodT && eT.rate - eH.rate >= relDiff);
    const bool goodH = !badH && eH.rate >= 70;
    if (goodH) return;
    if (badH && goodT) {
        if (unk3) {
            if (pause) { t.cls = AC_U0; return; }
            t.cls = AC_SK;  // слово из трёх букв без списка: нужны подтверждения
            t.weight = eH.hard ? 4 : 1;
        } else {
            t.cls = AC_W;
        }
        return;
    }
    t.cls = AC_U0;
}

// Решает, нужно ли что-то переключить. mode: 0 — после пробела, 1 — по паузе в наборе, 2 — перед Enter.
// Слово решается по соседям: уверенное слово "тянет" за собой сомнительные слова, набранные до него в той же раскладке;
// верное слово, набранное в другой раскладке после служебных слов, подтверждает их.
static bool AsDecide(const std::vector<Rec>& buf, int mode, AsFix& out) {
    if (AsExcluded()) return false;
    out.start = 0;
    out.end = (size_t)-1;
    out.target = NULL;
    AsReloadDictIfChanged(GetTickCount());

    const bool pause = (mode == 1);
    const Cycle cy = BuildCycle();
    std::vector<AsTok> tk = AsTokens(buf, 9);
    if (tk.empty()) return false;
    for (size_t i = 0; i < tk.size(); ++i) AsClassify(buf, tk[i], cy, pause);

    const size_t k = tk.size() - 1;
    const AsTok& last = tk[k];
    if (last.cls == AC_BAR) return false;

    // --- последнее слово не верное: оцениваем его вместе с предыдущими сомнительными словами той же раскладки
    if (last.cls != AC_R) {
        size_t lo = k;
        while (lo > 0 && tk[lo - 1].cls != AC_R && tk[lo - 1].cls != AC_BAR && tk[lo - 1].H == last.H) --lo;
        if (!g_asHeur) lo = k;  // режим только словаря: соседние слова не трогаем

        bool strong = false;
        int total = 0, listed = 0, tech = 0;
        for (size_t i = lo; i <= k; ++i) {
            const AsTok& t = tk[i];
            if (t.cls == AC_W) strong = true;
            else if (t.cls == AC_SK) { total += t.weight; if (t.listed) listed += t.weight; }
            else if (t.cls == AC_TECH) ++tech;
        }
        const int ctx = AsContext(buf, tk[lo].ps);
        const bool atStart = AsAtStart(buf, tk[lo].ps);
        const int lt = last.langT;

        bool go = strong || total >= 6;
        if (!go && total >= 4 && (atStart || ctx == lt || mode == 2)) go = true;
        if (!go && total >= 2 && ctx == lt && !pause) go = true;
        if (!go && listed >= 1 && atStart && mode == 2) go = true;
        if (!go && !pause && tech >= 1 && lt == AL_EN && (tech >= 2 || ctx == AL_EN)) go = true;
        if (!go) return false;

        out.start = tk[lo].ps;
        out.end = (size_t)-1;
        out.target = last.T;
        return true;
    }

    // --- последнее слово верное: оно подтверждает служебные слова другой раскладки перед ним ("z при" -> "я при")
    if (pause || !g_asHeur || k == 0) return false;
    const size_t hi = k - 1;
    const AsTok& prev = tk[hi];
    if (prev.cls == AC_R || prev.cls == AC_BAR || prev.cls == AC_TECH || prev.H == last.H || prev.langT != last.langH)
        return false;
    size_t lo = hi;
    while (lo > 0 && tk[lo - 1].H == prev.H && tk[lo - 1].cls != AC_R && tk[lo - 1].cls != AC_BAR &&
           tk[lo - 1].cls != AC_TECH)
        --lo;
    bool strong = false;
    int total = 0;
    for (size_t i = lo; i <= hi; ++i) {
        if (tk[i].cls == AC_W) strong = true;
        else if (tk[i].cls == AC_SK) total += tk[i].weight;
    }
    const int ctx = AsContext(buf, tk[lo].ps);
    const bool atStart = AsAtStart(buf, tk[lo].ps);
    const int sum = total + 3;
    const bool go = strong || (total > 0 && (sum >= 6 || (sum >= 4 && (atStart || (ctx == prev.langT && total >= 2)))));
    if (!go) return false;
    out.start = tk[lo].ps;
    out.end = tk[hi].pe;
    out.target = last.H;
    return true;
}

// ---------------------------------------------------------------- замена слова (отдельный поток)
struct AsEv {
    WORD vk;
    WORD scan;
    bool ext;
    bool shift;
};
struct AsJob {
    size_t regionLen;  // сколько символов с конца буфера заменить (область и всё, что набрано после неё)
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
    if (fg && snap.size() == job.snapSize && snap.size() >= job.regionLen && job.regionLen > 0) {
        bool allow = true;
        const GameInfo gi = QueryGame(fg);
        if (!gi.game && GetFocusKind() == FK_NOTEXT) allow = false;
        if (allow && AsFocusIsPassword()) allow = false;  // пароли не трогаем
        if (allow) {
            const size_t st = snap.size() - job.regionLen;
            // Круг для повторного двойного Ctrl сразу после автозамены: только исходная и целевая раскладки,
            // чтобы первое нажатие вернуло исходное слово
            const Cycle cy = BuildCycle();
            Cycle ca;
            const HKL home0 = snap[st].home;
            for (size_t i = 0; i < cy.lat.size(); ++i) {
                if (cy.lat[i] == home0 || cy.lat[i] == job.target) ca.lat.push_back(cy.lat[i]);
            }
            for (size_t i = 0; i < cy.cyr.size(); ++i) {
                if (cy.cyr[i] == home0 || cy.cyr[i] == job.target) ca.cyr.push_back(cy.cyr[i]);
            }
            ConvertBuffer(snap, ca, st, job.target, job.convEnd);
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

static AsJob AsMakeJob(const AsFix& fx, size_t bufSize, bool sendEnter) {
    AsJob j;
    j.regionLen = bufSize - fx.start;
    j.target = fx.target;
    j.convEnd = fx.end;
    j.sendEnter = sendEnter;
    j.snapSize = bufSize;
    return j;
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
    if (!AsDecide(snap, 1, fx)) return;
    AsStartJob(AsMakeJob(fx, snap.size(), false));
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
    if (snap.empty()) return false;
    AsFix fx;
    if (!AsDecide(snap, 2, fx)) return false;
    return AsStartJob(AsMakeJob(fx, snap.size(), true));
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
            if (AsDecide(snap, 0, fx)) AsStartJob(AsMakeJob(fx, snap.size(), false));
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
// AutoSwitch=1 — полный режим (оценки + словарь); AutoSwitch=0 и в Words.txt есть слова — только словарь;
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
