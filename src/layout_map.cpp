#include "layout_map.h"

namespace lm {

bool IsCyrillic(wchar_t c) { return c >= 0x0400 && c <= 0x052F; }

bool IsLatin(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= 0x00C0 && c <= 0x024F && c != 0x00D7 && c != 0x00F7);
}

bool IsLetter(wchar_t c) { return IsCyrillic(c) || IsLatin(c); }

static bool IsSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }

std::vector<unsigned char> ClassifyText(const std::wstring& text, const CharMap& latKeys, const CharMap& cyrKeys,
                                        bool defaultIsCyr) {
    const size_t n = text.size();
    std::vector<unsigned char> cls(n, CC_KEEP);

    for (size_t i = 0; i < n; ++i) {
        const wchar_t c = text[i];

        if (IsLatin(c)) { cls[i] = CC_LATIN; continue; }
        if (IsCyrillic(c)) { cls[i] = CC_CYRILLIC; continue; }

        const CharMap::const_iterator a = latKeys.find(c);
        const CharMap::const_iterator b = cyrKeys.find(c);
        const bool inL = a != latKeys.end();
        const bool inC = b != cyrKeys.end();

        if (inL && inC) {
            if (a->second == c && b->second == c) continue;  // цифры и подобное — не меняются

            // Знак есть в обеих раскладках. Раскладку определяем по ближайшей букве внутри того же слова
            // (до пробела): знак, приклеенный к слову, набран в той же раскладке, что и его буквы
            // (например, ",tp" — это "без", набранное латиницей, а не русская запятая).
            // Если в слове букв нет — берём ближайшую букву слева, затем справа (через пробелы).
            bool isCyr = defaultIsCyr;
            size_t left = n, right = n;  // n означает "не найдено"
            for (size_t j = i; j > 0 && !IsSpace(text[j - 1]); --j) {
                if (IsLetter(text[j - 1])) { left = j - 1; break; }
            }
            for (size_t j = i + 1; j < n && !IsSpace(text[j]); ++j) {
                if (IsLetter(text[j])) { right = j; break; }
            }
            if (left != n || right != n) {
                const bool useLeft = (right == n) || (left != n && i - left <= right - i);
                isCyr = IsCyrillic(text[useLeft ? left : right]);
            } else {
                bool found = false;
                for (size_t j = i; j-- > 0;) {
                    if (IsLetter(text[j])) { isCyr = IsCyrillic(text[j]); found = true; break; }
                }
                if (!found) {
                    for (size_t j = i + 1; j < n; ++j) {
                        if (IsLetter(text[j])) { isCyr = IsCyrillic(text[j]); break; }
                    }
                }
            }
            cls[i] = isCyr ? CC_CYRILLIC : CC_LATIN;
        } else if (inL) {
            cls[i] = CC_LATIN;
        } else if (inC) {
            cls[i] = CC_CYRILLIC;
        }
    }
    return cls;
}

}  // namespace lm
