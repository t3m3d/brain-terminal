#include "kterm/parser/AnsiParser.hpp"
#include <cctype>
#include <sstream>

using namespace kterm::parser;

AnsiParser::AnsiParser(int, int) {}

void AnsiParser::resize(int, int) {}

void AnsiParser::feed(
    const std::string& input,
    std::function<void(const std::string&)> onText,
    std::function<void(const EscapeSequence&)> onEscape
) {
    m_buffer += input;

    size_t pos = 0;
    while (pos < m_buffer.size()) {

        // Normal text until ESC
        if (m_buffer[pos] != '\x1b') {
            size_t nextEsc = m_buffer.find('\x1b', pos);
            if (nextEsc == std::string::npos) {
                onText(m_buffer.substr(pos));
                m_buffer.clear();
                return;
            }
            onText(m_buffer.substr(pos, nextEsc - pos));
            pos = nextEsc;
            continue;
        }

        // Need at least ESC + '['
        if (pos + 1 >= m_buffer.size())
            break;

        if (m_buffer[pos + 1] != '[') {
            pos += 2;
            continue;
        }

        // Find end of CSI sequence
        size_t end = pos + 2;
        while (end < m_buffer.size() && !std::isalpha(m_buffer[end]))
            end++;

        if (end >= m_buffer.size())
            break;

        std::string seq = m_buffer.substr(pos, end - pos + 1);
        onEscape(parseCSI(seq));

        pos = end + 1;
    }

    m_buffer = m_buffer.substr(pos);
}

bool AnsiParser::isCSI(const std::string& seq) const {
    return seq.size() >= 3 && seq[0] == '\x1b' && seq[1] == '[';
}

EscapeSequence AnsiParser::parseCSI(const std::string& seq) {
    EscapeSequence esc;

    if (!isCSI(seq))
        return esc;

    char final = seq.back();
    std::string paramsStr = seq.substr(2, seq.size() - 3);

    // Strip a leading private-mode / intermediate marker (?, >, =, <). Shells
    // emit ESC[?25l, ESC[?2004h, etc. constantly; we don't act on private
    // modes yet, but must not feed the marker to the integer parser.
    if (!paramsStr.empty() &&
        (paramsStr[0] == '?' || paramsStr[0] == '>' ||
         paramsStr[0] == '=' || paramsStr[0] == '<')) {
        paramsStr.erase(0, 1);
    }

    // Split parameters. Parse each as a leading integer, tolerating empty,
    // colon sub-params (38:2:r:g:b), and anything non-numeric WITHOUT throwing
    // — a bad CSI param must never crash the terminal.
    std::vector<int> params;
    if (!paramsStr.empty()) {
        std::stringstream ss(paramsStr);
        std::string part;
        while (std::getline(ss, part, ';')) {
            int v = 0;
            try {
                if (!part.empty()) v = std::stoi(part);
            } catch (...) {
                v = 0;
            }
            params.push_back(v);
        }
    }

    auto p = [&](size_t i, int def = 0) {
        return (i < params.size()) ? params[i] : def;
    };

    switch (final) {

        // Cursor movement
        case 'A': esc.type = EscapeType::CursorUp;    esc.value = p(0,1); break;
        case 'B': esc.type = EscapeType::CursorDown;  esc.value = p(0,1); break;
        case 'C': esc.type = EscapeType::CursorForward; esc.value = p(0,1); break;
        case 'D': esc.type = EscapeType::CursorBack;  esc.value = p(0,1); break;

        // Cursor position
        case 'H':
        case 'f':
            esc.type = EscapeType::SetCursorPos;
            esc.row = p(0,1);
            esc.col = p(1,1);
            break;

        // Clear screen / line. Carry the mode in .value:
        //   J: 0/none = cursor->end of screen, 1 = start->cursor, 2/3 = all.
        //   K: 0/none = cursor->end of line,   1 = start->cursor, 2   = whole line.
        // (Default 0 — NOT a full clear; shells emit ESC[J / ESC[K constantly.)
        case 'J': esc.type = EscapeType::ClearScreen; esc.value = p(0, 0); break;
        case 'K': esc.type = EscapeType::ClearLine;   esc.value = p(0, 0); break;

        // SGR (colors, attributes)
        case 'm': {
            // Emit the full SGR parameter list. Terminal applies colors AND
            // text attributes (bold/italic/underline/inverse) left-to-right;
            // an empty list means reset ([0]).
            esc.type = EscapeType::SGR;
            esc.params = params.empty() ? std::vector<int>{0} : params;
            break;
        }

        default:
            esc.type = EscapeType::Unknown;
            break;
    }

    return esc;
}