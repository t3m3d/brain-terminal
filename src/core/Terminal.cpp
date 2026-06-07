#include "brain/core/Terminal.hpp"
#include <algorithm>

using namespace brain::core;

Terminal::Terminal(int cols, int rows)
    : m_cols(cols),
      m_rows(rows),
      m_cursorRow(0),
      m_cursorCol(0),
      m_grid(cols, rows),
      m_parser(cols, rows) {}

void Terminal::resize(int cols, int rows) {
    m_cols = cols;
    m_rows = rows;
    m_grid.resize(cols, rows);
    m_parser.resize(cols, rows);
    m_cursorRow = 0;
    m_cursorCol = 0;
    if (m_renderCallback) m_renderCallback();
}

void Terminal::onPTYOutput(const std::vector<char>& data) {
    std::string s(data.begin(), data.end());

    m_parser.feed(
        s,
        [this](const std::string& text) {
            handleText(text);
        },
        [this](const parser::EscapeSequence& esc) {
            applyEscape(esc);
        }
    );

    if (m_renderCallback) m_renderCallback();
}

void Terminal::handleText(const std::string& text) {
    // Decode a UTF-8 byte stream into Unicode codepoints. Any incomplete
    // trailing sequence is carried over to the next call (m_utf8) so a glyph
    // split across two PTY reads still decodes correctly.
    std::string s = m_utf8 + text;
    m_utf8.clear();

    size_t i = 0;
    while (i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int len;
        if (b < 0x80)            { cp = b;          len = 1; }
        else if ((b >> 5) == 0x6){ cp = b & 0x1F;   len = 2; }
        else if ((b >> 4) == 0xE){ cp = b & 0x0F;   len = 3; }
        else if ((b >> 3) == 0x1E){ cp = b & 0x07;  len = 4; }
        else                     { cp = b;          len = 1; }  // invalid lead

        if (i + len > s.size()) {           // incomplete tail, save for next feed
            m_utf8 = s.substr(i);
            break;
        }
        bool bad = false;
        for (int k = 1; k < len; ++k) {
            unsigned char cb = static_cast<unsigned char>(s[i + k]);
            if ((cb >> 6) != 0x2) { len = 1; cp = b; bad = true; break; }  // bad continuation
            cp = (cp << 6) | (cb & 0x3F);
        }
        // Reject overlong encodings, UTF-16 surrogates, and out-of-range
        // codepoints. Overlong forms are a known filter-bypass class; surrogates
        // and >U+10FFFF aren't valid scalar values. Map any of them to U+FFFD.
        if (!bad) {
            if      (len == 1 && b >= 0x80)      bad = true;   // invalid lead / lone continuation
            else if (len == 2 && cp < 0x80)      bad = true;
            else if (len == 3 && cp < 0x800)     bad = true;
            else if (len == 4 && cp < 0x10000)   bad = true;
            else if (cp >= 0xD800 && cp <= 0xDFFF) bad = true;
            else if (cp > 0x10FFFF)              bad = true;
        }
        if (bad) cp = 0xFFFD;
        m_grid.putCodepoint(cp);
        i += len;
    }
}

void Terminal::applyEscape(const parser::EscapeSequence& seq) {
    using parser::EscapeType;

    switch (seq.type) {
        case EscapeType::CursorUp:
            m_grid.cursorUp(seq.value > 0 ? seq.value : 1);
            break;

        case EscapeType::CursorDown:
            m_grid.cursorDown(seq.value > 0 ? seq.value : 1);
            break;

        case EscapeType::CursorForward:
            m_grid.cursorForward(seq.value > 0 ? seq.value : 1);
            break;

        case EscapeType::CursorBack:
            m_grid.cursorBack(seq.value > 0 ? seq.value : 1);
            break;

        case EscapeType::SetCursorPos: {
            int row = std::max(0, seq.row - 1);
            int col = std::max(0, seq.col - 1);
            m_grid.setCursor(row, col);
            break;
        }

        case EscapeType::ClearScreen:
            // ESC[J: only 2/3 clear the whole screen. 0 (the one shells emit on
            // every prompt redraw) erases cursor -> end of screen, not all.
            if (seq.value >= 2) {
                m_grid.clear();
                m_grid.setCursor(0, 0);
            } else {
                m_grid.eraseToScreenEnd();
            }
            break;

        case EscapeType::ClearLine:
            // ESC[K: 2 = whole line; otherwise cursor -> end of line. Uses the
            // grid's real cursor, not Terminal's stale m_cursorRow.
            if (seq.value >= 2) {
                m_grid.clearLine(m_grid.cursorRow());
            } else {
                m_grid.eraseToLineEnd();
            }
            break;

        case EscapeType::SetFGColor:
            m_grid.setFG16(seq.color);
            break;

        case EscapeType::SetBGColor:
            m_grid.setBG16(seq.color);
            break;

        case EscapeType::SetFGColor256:
            m_grid.setFG256(seq.color);
            break;

        case EscapeType::SetBGColor256:
            m_grid.setBG256(seq.color);
            break;

        case EscapeType::SetFGTrueColor:
            m_grid.setFGTrue(seq.r, seq.g, seq.b);
            break;

        case EscapeType::SetBGTrueColor:
            m_grid.setBGTrue(seq.r, seq.g, seq.b);
            break;

        case EscapeType::ResetAttributes:
            m_grid.resetAttributes();
            break;

        case EscapeType::SetMode:
        case EscapeType::ResetMode: {
            bool on = (seq.type == EscapeType::SetMode);
            if (seq.privateMode) {
                if (seq.value == 2004)     m_bracketedPaste = on;   // bracketed paste
                else if (seq.value == 25)  m_cursorVisible  = on;   // cursor show/hide
            }
            break;
        }

        case EscapeType::WindowOp: {
            // Reply to window-size queries so prompts (oh-my-posh/starship) that
            // size their panels by querying the terminal lay out correctly.
            if (!m_responseCallback) break;
            int cols = m_grid.cols();
            int rows = m_grid.rowCount();
            if (seq.value == 18)        // report text area size in characters
                m_responseCallback("\x1b[8;" + std::to_string(rows) + ";" +
                                   std::to_string(cols) + "t");
            else if (seq.value == 14)   // report text area size in pixels
                m_responseCallback("\x1b[4;" + std::to_string(rows * m_cellPxH) + ";" +
                                   std::to_string(cols * m_cellPxW) + "t");
            break;
        }

        case EscapeType::SGR: {
            const auto& ps = seq.params;
            for (size_t i = 0; i < ps.size(); ++i) {
                int code = ps[i];
                if      (code == 0)  m_grid.resetAttributes();
                else if (code == 1)  m_grid.enableAttr(renderer::ATTR_BOLD);
                else if (code == 3)  m_grid.enableAttr(renderer::ATTR_ITALIC);
                else if (code == 4)  m_grid.enableAttr(renderer::ATTR_UNDERLINE);
                else if (code == 7)  m_grid.enableAttr(renderer::ATTR_INVERSE);
                else if (code == 22) m_grid.disableAttr(renderer::ATTR_BOLD);
                else if (code == 23) m_grid.disableAttr(renderer::ATTR_ITALIC);
                else if (code == 24) m_grid.disableAttr(renderer::ATTR_UNDERLINE);
                else if (code == 27) m_grid.disableAttr(renderer::ATTR_INVERSE);
                else if (code >= 30 && code <= 37) m_grid.setFG16(code - 30);
                else if (code == 38) {
                    if (i + 2 < ps.size() && ps[i+1] == 5) { m_grid.setFG256(ps[i+2]); i += 2; }
                    else if (i + 4 < ps.size() && ps[i+1] == 2) { m_grid.setFGTrue(ps[i+2], ps[i+3], ps[i+4]); i += 4; }
                }
                else if (code == 39) m_grid.setFGDefault();
                else if (code >= 40 && code <= 47) m_grid.setBG16(code - 40);
                else if (code == 48) {
                    if (i + 2 < ps.size() && ps[i+1] == 5) { m_grid.setBG256(ps[i+2]); i += 2; }
                    else if (i + 4 < ps.size() && ps[i+1] == 2) { m_grid.setBGTrue(ps[i+2], ps[i+3], ps[i+4]); i += 4; }
                }
                else if (code == 49) m_grid.setBGDefault();
                else if (code >= 90 && code <= 97)   m_grid.setFG16(code - 90 + 8);   // bright fg
                else if (code >= 100 && code <= 107) m_grid.setBG16(code - 100 + 8);  // bright bg
            }
            break;
        }

        case EscapeType::OSC: {
            // OSC 133 shell integration (FinalTerm): A=prompt start, D[;code]=cmd done.
            const std::string& s = seq.osc;
            if (s.rfind("133;", 0) == 0 && s.size() >= 5) {
                char kind = s[4];
                if (kind == 'A') {
                    long line = m_grid.absScroll() + m_grid.cursorRow();
                    m_blockMarks[line] = 0;          // 0 = idle prompt (no bar)
                    m_lastMarkLine = line;
                } else if (kind == 'C') {
                    if (m_lastMarkLine >= 0)
                        m_blockMarks[m_lastMarkLine] = 3;  // 3 = running (output started)
                } else if (kind == 'D') {
                    int code = 0;
                    size_t semi = s.find(';', 4);
                    if (semi != std::string::npos) {
                        try { code = std::stoi(s.substr(semi + 1)); } catch (...) { code = 0; }
                    }
                    if (m_lastMarkLine >= 0)
                        m_blockMarks[m_lastMarkLine] = (code == 0) ? 1 : 2;  // 1 ok, 2 fail
                }
            }
            break;
        }

        default:
            break;
    }
}