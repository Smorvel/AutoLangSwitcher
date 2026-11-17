#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// Вспомогательные функции для работы с текстом в латинских и кириллических раскладках.
// Сами таблицы соответствия клавиш строятся в main.cpp из установленных раскладок Windows.
namespace lm {

typedef std::unordered_map<wchar_t, wchar_t> CharMap;

bool IsCyrillic(wchar_t c);
bool IsLatin(wchar_t c);
bool IsLetter(wchar_t c);

enum CharClass { CC_KEEP = 0, CC_LATIN = 1, CC_CYRILLIC = 2 };

// Для каждого символа текста определяет, в раскладке какого алфавита он набран:
// CC_LATIN, CC_CYRILLIC или CC_KEEP (символ не меняется: цифры, пробелы и т.п.).
// latKeys — символы латинской раскладки -> символы кириллической на тех же клавишах; cyrKeys — наоборот.
// Знаки препинания, которые есть в обеих раскладках, определяются по ближайшей букве внутри того же слова.
// defaultIsCyr — что считать раскладкой, если рядом нет ни одной буквы.
std::vector<unsigned char> ClassifyText(const std::wstring& text, const CharMap& latKeys, const CharMap& cyrKeys,
                                        bool defaultIsCyr);

}  // namespace lm
