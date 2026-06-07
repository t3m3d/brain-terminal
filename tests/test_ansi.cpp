// test_ansi.cpp — unit test for the ANSI/VT parser, doubling as the
// cross-validation oracle for the Krypton port (krypton/linux/brain_ansi.k):
// it emits events in the exact same text format, so the two parsers' output
// can be diffed byte-for-byte.
//
// Build/run standalone:
//   g++ -std=c++20 -Iinclude tests/test_ansi.cpp src/parser/AnsiParser.cpp -o /tmp/t && /tmp/t
// Or via ctest once wired into CMakeLists.

#include "brain/parser/AnsiParser.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <sstream>

using namespace brain::parser;

static std::string escText(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\x1b': o += "\\e"; break;
            default: o += c;
        }
    }
    return o;
}

static std::string joinParams(const std::vector<int>& p) {
    std::ostringstream ss;
    for (size_t i = 0; i < p.size(); ++i) { if (i) ss << ';'; ss << p[i]; }
    return ss.str();
}

// Map an EscapeSequence to the same one-line event string brain_ansi.k emits.
static std::string eventStr(const EscapeSequence& e) {
    switch (e.type) {
        case EscapeType::CursorUp:      return "CURSOR_UP "    + std::to_string(e.value);
        case EscapeType::CursorDown:    return "CURSOR_DOWN "  + std::to_string(e.value);
        case EscapeType::CursorForward: return "CURSOR_FWD "   + std::to_string(e.value);
        case EscapeType::CursorBack:    return "CURSOR_BACK "  + std::to_string(e.value);
        case EscapeType::SetCursorPos:  return "SET_CURSOR "   + std::to_string(e.row) + " " + std::to_string(e.col);
        case EscapeType::CursorColumn:  return "CURSOR_COL "   + std::to_string(e.value);
        case EscapeType::CursorRow:     return "CURSOR_ROW "   + std::to_string(e.value);
        case EscapeType::ClearScreen:   return "CLEAR_SCREEN " + std::to_string(e.value);
        case EscapeType::ClearLine:     return "CLEAR_LINE "   + std::to_string(e.value);
        case EscapeType::SGR:           return "SGR " + joinParams(e.params);
        case EscapeType::SetMode:       return "SET_MODE "   + std::to_string(e.value) + " " + (e.privateMode ? "1" : "0");
        case EscapeType::ResetMode:     return "RESET_MODE " + std::to_string(e.value) + " " + (e.privateMode ? "1" : "0");
        case EscapeType::OSC:           return "OSC " + e.osc;
        case EscapeType::WindowOp:      return "WINDOW_OP " + std::to_string(e.value);
        default:                        return "UNKNOWN";
    }
}

static std::string parseToLog(const std::string& input) {
    AnsiParser p(80, 24);
    std::string log;
    p.feed(input,
        [&](const std::string& t) { log += "TEXT " + escText(t) + "\n"; },
        [&](const EscapeSequence& e) { log += eventStr(e) + "\n"; });
    return log;
}

int main() {
    const std::string ESC = "\x1b";
    const std::string BEL = "\x07";

    // Same fixture as brain_ansi.k's self-test.
    std::string input =
        "hello " + ESC + "[31m" + "red" + ESC + "[0m"
        + ESC + "[2J" + ESC + "]0;mytitle" + BEL + "done" + ESC + "[1;5H"
        + ESC + "[?25l" + ESC + "[38;5;200m" + "x" + ESC + "[A";

    std::string got = parseToLog(input);
    std::cout << "=== C++ AnsiParser event trace ===\n" << got;

    const std::string expected =
        "TEXT hello \n"
        "SGR 31\n"
        "TEXT red\n"
        "SGR 0\n"
        "CLEAR_SCREEN 2\n"
        "OSC 0;mytitle\n"
        "TEXT done\n"
        "SET_CURSOR 1 5\n"
        "RESET_MODE 25 1\n"
        "SGR 38;5;200\n"
        "TEXT x\n"
        "CURSOR_UP 1\n";

    int failures = 0;
    if (got != expected) {
        ++failures;
        std::cerr << "\nFAIL: event trace mismatch.\n--- expected ---\n"
                  << expected << "--- got ---\n" << got;
    }

    // A few targeted assertions on edge cases.
    auto check = [&](const std::string& in, const std::string& want, const char* name) {
        std::string g = parseToLog(in);
        if (g != want) {
            ++failures;
            std::cerr << "FAIL [" << name << "]\n  want: " << want << "  got:  " << g;
        }
    };
    check("ab", "TEXT ab\n", "plain text");
    check(ESC + "[m", "SGR 0\n", "empty SGR -> 0");
    check(ESC + "[H", "SET_CURSOR 1 1\n", "bare cursor home defaults");
    check(ESC + "[48;2;10;20;30m", "SGR 48;2;10;20;30\n", "truecolor bg");
    check(ESC + "[?2004h", "SET_MODE 2004 1\n", "bracketed paste set (private)");

    if (failures == 0) {
        std::cout << "\nPASS: all ANSI parser checks ok\n";
        return 0;
    }
    std::cerr << "\n" << failures << " failure(s)\n";
    return 1;
}
