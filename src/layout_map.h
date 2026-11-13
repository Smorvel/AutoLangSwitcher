#pragma once
#include <string>

// Таблицы соответствия клавиш: английская (US QWERTY) <-> русская (ЙЦУКЕН)
namespace lm {

wchar_t EnToRu(wchar_t c);   // символ, набранный в EN-раскладке -> символ на той же клавише в RU
wchar_t RuToEn(wchar_t c);   // символ, набранный в RU-раскладке -> символ на той же клавише в EN

bool IsCyrillic(wchar_t c);
bool IsLatin(wchar_t c);
bool IsLetter(wchar_t c);

// Конвертация произвольного (в т.ч. смешанного) текста:
// латиница -> кириллица, кириллица -> латиница.
// Знаки препинания, которые есть в обеих раскладках, определяются по ближайшей букве.
// defaultIsRu - что считать раскладкой, если в тексте вообще нет букв.
std::wstring ConvertSelection(const std::wstring& text, bool defaultIsRu);

}  // namespace lm
