#pragma once

#include <array>

namespace Studio {

// AnyDevKit Studio's own 5x7 font, drawn for this project and MIT licensed with the rest of the app.
// Rows run top to bottom and bit 4 is the leftmost cell. Capitals and digits stand on row 5, lowercase
// starts at row 2 and row 6 holds descenders. The PS5 runtime carries the same table in
// runtime/SceneGame/src/Graphics/Font.cpp.in so the stage matches the game; tst_font_tables compares them.
inline std::array<unsigned, 7> pixelGlyph(char character) {
    switch (character) {
    case ' ': return {0, 0, 0, 0, 0, 0, 0};
    case '!': return {4, 4, 4, 4, 0, 4, 0};
    case '"': return {10, 10, 0, 0, 0, 0, 0};
    case '#': return {10, 31, 10, 10, 31, 10, 0};
    case '$': return {4, 15, 20, 14, 5, 30, 4};
    case '%': return {25, 26, 4, 4, 11, 19, 0};
    case '&': return {12, 18, 12, 21, 18, 13, 0};
    case '\'': return {4, 4, 0, 0, 0, 0, 0};
    case '(': return {2, 4, 8, 8, 8, 4, 2};
    case ')': return {8, 4, 2, 2, 2, 4, 8};
    case '*': return {0, 4, 21, 14, 21, 4, 0};
    case '+': return {0, 4, 4, 31, 4, 4, 0};
    case ',': return {0, 0, 0, 0, 0, 4, 8};
    case '-': return {0, 0, 0, 14, 0, 0, 0};
    case '.': return {0, 0, 0, 0, 0, 4, 0};
    case '/': return {1, 2, 2, 4, 8, 8, 16};
    case '0': return {14, 17, 21, 21, 17, 14, 0};
    case '1': return {4, 12, 4, 4, 4, 14, 0};
    case '2': return {14, 17, 2, 4, 8, 31, 0};
    case '3': return {14, 17, 6, 1, 17, 14, 0};
    case '4': return {2, 6, 10, 18, 31, 2, 0};
    case '5': return {31, 16, 30, 1, 17, 14, 0};
    case '6': return {14, 16, 30, 17, 17, 14, 0};
    case '7': return {31, 1, 2, 4, 4, 4, 0};
    case '8': return {14, 17, 14, 17, 17, 14, 0};
    case '9': return {14, 17, 17, 15, 1, 14, 0};
    case ':': return {0, 0, 4, 0, 0, 4, 0};
    case ';': return {0, 0, 4, 0, 0, 4, 8};
    case '<': return {0, 2, 4, 8, 4, 2, 0};
    case '=': return {0, 0, 31, 0, 31, 0, 0};
    case '>': return {0, 8, 4, 2, 4, 8, 0};
    case '?': return {14, 17, 2, 4, 0, 4, 0};
    case '@': return {14, 17, 23, 21, 23, 16, 15};
    case 'A': return {14, 17, 17, 31, 17, 17, 0};
    case 'B': return {30, 17, 30, 17, 17, 30, 0};
    case 'C': return {14, 17, 16, 16, 17, 14, 0};
    case 'D': return {28, 18, 17, 17, 18, 28, 0};
    case 'E': return {31, 16, 30, 16, 16, 31, 0};
    case 'F': return {31, 16, 30, 16, 16, 16, 0};
    case 'G': return {14, 17, 16, 19, 17, 14, 0};
    case 'H': return {17, 17, 31, 17, 17, 17, 0};
    case 'I': return {14, 4, 4, 4, 4, 14, 0};
    case 'J': return {7, 2, 2, 2, 18, 12, 0};
    case 'K': return {17, 18, 28, 20, 18, 17, 0};
    case 'L': return {16, 16, 16, 16, 16, 31, 0};
    case 'M': return {17, 27, 21, 21, 17, 17, 0};
    case 'N': return {17, 25, 21, 21, 19, 17, 0};
    case 'O': return {14, 17, 17, 17, 17, 14, 0};
    case 'P': return {30, 17, 30, 16, 16, 16, 0};
    case 'Q': return {14, 17, 17, 21, 18, 13, 0};
    case 'R': return {30, 17, 30, 20, 18, 17, 0};
    case 'S': return {15, 16, 14, 1, 1, 30, 0};
    case 'T': return {31, 4, 4, 4, 4, 4, 0};
    case 'U': return {17, 17, 17, 17, 17, 14, 0};
    case 'V': return {17, 17, 17, 10, 10, 4, 0};
    case 'W': return {17, 17, 21, 21, 27, 17, 0};
    case 'X': return {17, 10, 4, 4, 10, 17, 0};
    case 'Y': return {17, 17, 10, 4, 4, 4, 0};
    case 'Z': return {31, 1, 6, 8, 16, 31, 0};
    case '[': return {14, 8, 8, 8, 8, 8, 14};
    case '\\': return {16, 8, 8, 4, 2, 2, 1};
    case ']': return {14, 2, 2, 2, 2, 2, 14};
    case '^': return {4, 10, 17, 0, 0, 0, 0};
    case '_': return {0, 0, 0, 0, 0, 0, 31};
    case '`': return {8, 4, 0, 0, 0, 0, 0};
    case 'a': return {0, 0, 14, 18, 18, 15, 0};
    case 'b': return {16, 16, 30, 17, 17, 30, 0};
    case 'c': return {0, 0, 15, 16, 16, 15, 0};
    case 'd': return {1, 1, 15, 17, 17, 15, 0};
    case 'e': return {0, 0, 14, 19, 28, 15, 0};
    case 'f': return {7, 8, 30, 8, 8, 8, 0};
    case 'g': return {0, 0, 15, 17, 15, 1, 14};
    case 'h': return {16, 16, 30, 17, 17, 17, 0};
    case 'i': return {4, 0, 12, 4, 4, 14, 0};
    case 'j': return {2, 0, 6, 2, 2, 2, 12};
    case 'k': return {16, 16, 18, 20, 28, 18, 0};
    case 'l': return {12, 4, 4, 4, 4, 3, 0};
    case 'm': return {0, 0, 26, 21, 21, 21, 0};
    case 'n': return {0, 0, 30, 17, 17, 17, 0};
    case 'o': return {0, 0, 14, 17, 17, 14, 0};
    case 'p': return {0, 0, 30, 17, 30, 16, 16};
    case 'q': return {0, 0, 15, 17, 15, 1, 1};
    case 'r': return {0, 0, 22, 25, 16, 16, 0};
    case 's': return {0, 0, 15, 24, 3, 30, 0};
    case 't': return {0, 8, 30, 8, 8, 6, 0};
    case 'u': return {0, 0, 17, 17, 17, 15, 0};
    case 'v': return {0, 0, 17, 17, 10, 4, 0};
    case 'w': return {0, 0, 17, 21, 21, 10, 0};
    case 'x': return {0, 0, 27, 4, 4, 27, 0};
    case 'y': return {0, 0, 17, 17, 15, 1, 14};
    case 'z': return {0, 0, 31, 6, 8, 31, 0};
    case '{': return {3, 4, 4, 8, 4, 4, 3};
    case '|': return {4, 4, 4, 4, 4, 4, 4};
    case '}': return {24, 4, 4, 2, 4, 4, 24};
    case '~': return {0, 0, 0, 13, 22, 0, 0};
    default: return {};
    }
}

}
