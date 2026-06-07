#include "brain/parser/AnsiParser.hpp"
#include <cctype>
#include <sstream>

using namespace brain::parser;

// Cap on an unterminated escape sequence held in the buffer. A hostile program
// can emit "ESC]" (or "ESC[") and then stream megabytes with no terminator;
// without a cap m_buffer grows without bound -> memory-exhaustion DoS. 64 KiB
// is far larger than any real OSC/CSI, so legitimate sequences are unaffected.
static constexpr size_t kMaxEscPending = 1u << 16;

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

        // Need at least ESC + one more byte
        if (pos + 1 >= m_buffer.size())
            break;

        // OSC: ESC ] <payload> (BEL | ESC '\')
        if (m_buffer[pos + 1] == ']') {
            size_t i = pos + 2; size_t termLen = 0;
            while (i < m_buffer.size()) {
                if (m_buffer[i] == '\x07') { termLen = 1; break; }                       // BEL
                if (m_buffer[i] == '\x1b' && i + 1 < m_buffer.size() && m_buffer[i+1] == '\\') { termLen = 2; break; }  // ST
                i++;
            }
            if (i >= m_buffer.size()) {          // terminator not here yet, wait
                // Runaway OSC with no terminator: discard it rather than buffer
                // unbounded. Drop everything from this ESC onward.
                if (m_buffer.size() - pos > kMaxEscPending) pos = m_buffer.size();
                break;
            }
            EscapeSequence esc;
            esc.type = EscapeType::OSC;
            esc.osc  = m_buffer.substr(pos + 2, i - (pos + 2));
            onEscape(esc);
            pos = i + termLen;
            continue;
        }

        if (m_buffer[pos + 1] != '[') {
            pos += 2;
            continue;
        }

        // Find end of CSI sequence. Cast to unsigned char: isalpha() on a
        // negative value other than EOF is undefined, and bytes >0x7F are
        // negative under signed char (arm64).
        size_t end = pos + 2;
        while (end < m_buffer.size() &&
               !std::isalpha(static_cast<unsigned char>(m_buffer[end])))
            end++;

        if (end >= m_buffer.size()) {
            // Runaway CSI with no final byte: discard rather than buffer forever.
            if (m_buffer.size() - pos > kMaxEscPending) pos = m_buffer.size();
            break;
        }

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
        if (paramsStr[0] == '?') esc.privateMode = true;
        paramsStr.erase(0, 1);
    }

    // Split parameters. Parse each as a leading integer, tolerating empty
    // fields, colon sub-params (38:2:r:g:b), and non-numeric junk without
    // throwing. A bad CSI param must never crash the terminal.
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
        // Default 0 is not a full clear; shells emit ESC[J / ESC[K constantly.
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

        // Mode set/reset (e.g. ESC[?2004h bracketed paste, ESC[?25l hide cursor)
        case 'h': esc.type = EscapeType::SetMode;   esc.value = p(0, 0); break;
        case 'l': esc.type = EscapeType::ResetMode; esc.value = p(0, 0); break;

        default:
            esc.type = EscapeType::Unknown;
            break;
    }

    return esc;
}