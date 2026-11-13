#include "layout_map.h"
#include <unordered_map>

namespace {

struct Tables {
    std::unordered_map<wchar_t, wchar_t> en2ru;
    std::unordered_map<wchar_t, wchar_t> ru2en;

    Tables() {
        // Строки одинаковой длины: i-й символ EN стоит на той же клавише, что и i-й символ RU.
        const std::wstring en =
            L"`qwertyuiop[]asdfghjkl;'zxcvbnm,./"
            L"~QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?"
            L"!@#$%^&*()\\|";
        const std::wstring ru =
            L"ёйцукенгшщзхъфывапролджэячсмитьбю."
            L"ЁЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮ,"
            L"!\"№;%:?*()\\/";
        const size_t n = en.size() < ru.size() ? en.size() : ru.size();
        for (size_t i = 0; i < n; ++i) {
            en2ru[en[i]] = ru[i];
            ru2en[ru[i]] = en[i];
        }
    }
};

const Tables& T() {
    static Tables t;
    return t;
}

// Символ есть в обеих раскладках и превращается в разные символы ('.', ',', ';', ':', '?', '/', '"')
bool IsAmbiguous(wchar_t c) {
    const Tables& t = T();
    auto a = t.en2ru.find(c);
    auto b = t.ru2en.find(c);
    if (a == t.en2ru.end() || b == t.ru2en.end()) return false;
    return a->second != c || b->second != c;
}

}  // namespace

namespace lm {

wchar_t EnToRu(wchar_t c) {
    auto it = T().en2ru.find(c);
    return it == T().en2ru.end() ? c : it->second;
}

wchar_t RuToEn(wchar_t c) {
    auto it = T().ru2en.find(c);
    return it == T().ru2en.end() ? c : it->second;
}

bool IsCyrillic(wchar_t c) { return c >= 0x0400 && c <= 0x04FF; }
bool IsLatin(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool IsLetter(wchar_t c) { return IsCyrillic(c) || IsLatin(c); }

std::wstring ConvertSelection(const std::wstring& text, bool defaultIsRu) {
    std::wstring out;
    out.reserve(text.size());
    const size_t n = text.size();

    for (size_t i = 0; i < n; ++i) {
        const wchar_t c = text[i];

        if (IsLatin(c)) { out += EnToRu(c); continue; }
        if (IsCyrillic(c)) { out += RuToEn(c); continue; }

        if (IsAmbiguous(c)) {
            // раскладку знака определяем по ближайшей букве: сначала слева, потом справа
            bool isRu = defaultIsRu;
            bool found = false;
            for (size_t j = i; j-- > 0;) {
                if (IsLetter(text[j])) { isRu = IsCyrillic(text[j]); found = true; break; }
            }
            if (!found) {
                for (size_t j = i + 1; j < n; ++j) {
                    if (IsLetter(text[j])) { isRu = IsCyrillic(text[j]); break; }
                }
            }
            out += isRu ? RuToEn(c) : EnToRu(c);
            continue;
        }

        auto a = T().en2ru.find(c);
        if (a != T().en2ru.end()) { out += a->second; continue; }
        auto b = T().ru2en.find(c);
        if (b != T().ru2en.end()) { out += b->second; continue; }
        out += c;
    }
    return out;
}

}  // namespace lm
