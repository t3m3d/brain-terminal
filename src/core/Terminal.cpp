#include "brain/core/Terminal.hpp"
#include "brain/core/Sixel.hpp"
#include <algorithm>
#include <cstdio>

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
        // Bell (0x07) outside an OSC payload — the parser routes OSC bytes
        // separately, so anything reaching us here is a stand-alone BEL.
        // Fire the visual/audible bell callback and skip writing it to the
        // grid (Grid would drop it as a control char anyway).
        if (cp == 0x07) {
            if (m_bellCallback) m_bellCallback();
            i += len;
            continue;
        }
        m_grid.putCodepoint(cp);
        i += len;
    }
}

void Terminal::enterAltScreen() {
    if (m_altScreen) return;
    // Snapshot the live grid (so the main buffer can return intact when
    // vim/less exit) and blank the visible cells for the altscreen child.
    auto snap = m_grid.snapshot();
    m_savedRows.clear();
    m_savedRows.swap(snap.cells);   // we only need cells + cursor here
    m_savedCursorRow = snap.cursorRow;
    m_savedCursorCol = snap.cursorCol;
    m_grid.clear();
    m_grid.setCursor(0, 0);
    m_altScreen = true;
}

void Terminal::exitAltScreen() {
    if (!m_altScreen) return;
    if (!m_savedRows.empty()) {
        renderer::Grid::Snapshot s;
        s.cells = std::move(m_savedRows);
        s.cursorRow = m_savedCursorRow;
        s.cursorCol = m_savedCursorCol;
        m_grid.restore(s);
    }
    m_savedRows.clear();
    m_altScreen = false;
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

        case EscapeType::CursorColumn:   // CHA: absolute column (1-based)
            m_grid.setCursor(m_grid.cursorRow(), std::max(0, seq.value - 1));
            break;

        case EscapeType::CursorRow:      // VPA: absolute row (1-based)
            m_grid.setCursor(std::max(0, seq.value - 1), m_grid.cursorCol());
            break;

        case EscapeType::SetCursorStyle:   // DECSCUSR: vim insert-mode bar, etc.
            if (m_cursorStyleCallback) {
                const char* style = (seq.value <= 2) ? "block"
                                  : (seq.value <= 4) ? "underline" : "bar";
                m_cursorStyleCallback(style);
            }
            break;

        case EscapeType::ClearScreen:
            // ESC[J: 0/none = cursor->end, 1 = start->cursor, 2/3 = all.
            if (seq.value >= 2) {
                m_grid.clear();
                m_grid.setCursor(0, 0);
            } else if (seq.value == 1) {
                m_grid.eraseToScreenStart();
            } else {
                m_grid.eraseToScreenEnd();
            }
            break;

        case EscapeType::ClearLine:
            // ESC[K: 0/none = cursor->end, 1 = start->cursor, 2 = whole row.
            if (seq.value >= 2)      m_grid.clearLine(m_grid.cursorRow());
            else if (seq.value == 1) m_grid.eraseToLineStart();
            else                     m_grid.eraseToLineEnd();
            break;

        case EscapeType::InsertLines: m_grid.insertLines(seq.value); break;
        case EscapeType::DeleteLines: m_grid.deleteLines(seq.value); break;
        case EscapeType::InsertChars: m_grid.insertChars(seq.value); break;
        case EscapeType::DeleteChars: m_grid.deleteChars(seq.value); break;
        case EscapeType::EraseChars:  m_grid.eraseChars (seq.value); break;

        case EscapeType::SaveCursor:
            m_decscRow = m_grid.cursorRow();
            m_decscCol = m_grid.cursorCol();
            m_decscValid = true;
            break;

        case EscapeType::RestoreCursor:
            if (m_decscValid) m_grid.setCursor(m_decscRow, m_decscCol);
            break;

        case EscapeType::SetScrollRegion: {
            // CSI r — top;bottom in 1-based row coordinates. 0/missing
            // bottom means "to the end of the screen". CSI r with no
            // params resets the region to the full screen.
            int top = seq.row > 0 ? seq.row - 1 : 0;
            int bot = seq.col > 0 ? seq.col - 1 : m_rows - 1;
            if (bot >= m_rows) bot = m_rows - 1;
            if (top > bot) { top = 0; bot = m_rows - 1; }
            m_grid.setScrollRegion(top, bot);
            // DECSTBM sets the cursor at the home position of the
            // region; xterm does this and vim depends on it.
            m_grid.setCursor(top, 0);
            break;
        }

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
                switch (seq.value) {
                    case 25:   m_cursorVisible  = on; break;
                    case 2004: m_bracketedPaste = on; break;

                    // Mouse reporting: 1000 click, 1002 button-drag, 1003 any
                    // motion; 1006 switches to SGR encoding.
                    case 1000:
                    case 1002:
                    case 1003: m_mouseMode = on ? seq.value : 0; break;
                    case 1006: m_mouseSGR  = on; break;

                    // Alternate screen buffer variants. 1049 also saves /
                    // restores the cursor and clears the alt buffer on
                    // entry, which is what xterm does and what vim/less
                    // expect. We collapse 47/1047/1049 to the same path —
                    // good-enough behaviour for the apps in the wild.
                    case 47:
                    case 1047:
                    case 1049:
                        if (on) {
                            if (seq.value == 1049) {
                                m_decscRow = m_grid.cursorRow();
                                m_decscCol = m_grid.cursorCol();
                                m_decscValid = true;
                            }
                            enterAltScreen();
                        } else {
                            exitAltScreen();
                            if (seq.value == 1049 && m_decscValid)
                                m_grid.setCursor(m_decscRow, m_decscCol);
                        }
                        break;

                    // DECSC/DECRC alias.
                    case 1048:
                        if (on) {
                            m_decscRow = m_grid.cursorRow();
                            m_decscCol = m_grid.cursorCol();
                            m_decscValid = true;
                        } else if (m_decscValid) {
                            m_grid.setCursor(m_decscRow, m_decscCol);
                        }
                        break;
                }
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
                else if (code == 2)  m_grid.enableAttr(renderer::ATTR_DIM);
                else if (code == 3)  m_grid.enableAttr(renderer::ATTR_ITALIC);
                else if (code == 4)  m_grid.enableAttr(renderer::ATTR_UNDERLINE);
                else if (code == 7)  m_grid.enableAttr(renderer::ATTR_INVERSE);
                else if (code == 9)  m_grid.enableAttr(renderer::ATTR_STRIKE);
                else if (code == 22) m_grid.disableAttr(renderer::ATTR_BOLD | renderer::ATTR_DIM);
                else if (code == 23) m_grid.disableAttr(renderer::ATTR_ITALIC);
                else if (code == 24) m_grid.disableAttr(renderer::ATTR_UNDERLINE);
                else if (code == 27) m_grid.disableAttr(renderer::ATTR_INVERSE);
                else if (code == 29) m_grid.disableAttr(renderer::ATTR_STRIKE);
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
            const std::string& s = seq.osc;

            // OSC 8 — in-band hyperlinks. Format: 8;params;URI
            // The params field is for id=… (re-using an id across runs)
            // and is currently ignored. Empty URI clears the link.
            if (s.size() >= 2 && s[0] == '8' && s[1] == ';') {
                size_t semi = s.find(';', 2);
                std::string uri = (semi == std::string::npos)
                                ? std::string()
                                : s.substr(semi + 1);
                if (uri.empty()) {
                    m_grid.setCurrentLink(0);
                } else {
                    uint16_t id = m_nextLinkId++;
                    if (id == 0) id = m_nextLinkId++;   // skip the reserved 0
                    m_linkUris[id] = uri;
                    m_grid.setCurrentLink(id);
                }
                break;
            }

            // OSC 0 / OSC 1 / OSC 2 — set window/icon title.
            //   0;text  set both icon and window title
            //   1;text  set icon title only
            //   2;text  set window title only
            // We treat all three as "the window title", which is what
            // every modern terminal does for OSC 0/2 and a reasonable
            // fallback for OSC 1.
            if (s.size() >= 2 && (s[0] == '0' || s[0] == '1' || s[0] == '2') && s[1] == ';') {
                m_title = s.substr(2);
                if (m_titleCallback) m_titleCallback(m_title);
                break;
            }

            // OSC 52 — clipboard. 52;<sel>;<base64-data>. A bare "?" data field
            // is a READ request; we ignore reads (a remote app shouldn't be able
            // to exfiltrate the clipboard) but honour writes. The widget decodes
            // the base64 and sets the system clipboard.
            if (s.rfind("52;", 0) == 0) {
                size_t semi = s.find(';', 3);
                if (semi != std::string::npos) {
                    std::string data = s.substr(semi + 1);
                    if (data != "?" && !data.empty() && m_clipboardCallback)
                        m_clipboardCallback(data);
                }
                break;
            }

            // OSC 7 — working directory: 7;file://host/path (path may be
            // percent-encoded). Stored so "open a new tab here" can use it.
            if (s.rfind("7;", 0) == 0) {
                std::string uri = s.substr(2);
                std::string path = uri;
                const std::string fp = "file://";
                if (uri.rfind(fp, 0) == 0) {
                    size_t slash = uri.find('/', fp.size());   // skip the host part
                    path = (slash == std::string::npos) ? std::string() : uri.substr(slash);
                }
                std::string decoded;
                for (size_t i = 0; i < path.size(); ++i) {
                    if (path[i] == '%' && i + 2 < path.size()) {
                        auto hex = [](char c) -> int {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                            return -1;
                        };
                        int hi = hex(path[i+1]), lo = hex(path[i+2]);
                        if (hi >= 0 && lo >= 0) { decoded += (char)(hi * 16 + lo); i += 2; continue; }
                    }
                    decoded += path[i];
                }
                if (!decoded.empty()) m_cwd = decoded;
                break;
            }

            // OSC 133 shell integration (FinalTerm): A=prompt start, D[;code]=cmd done.
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

        case EscapeType::DCS: {
            // Sixel graphics: decode and anchor an inline image at the cursor.
            SixelImage img = decodeSixel(seq.osc);
            if (img.w > 0 && img.h > 0)
                placeImage(img.w, img.h, std::move(img.argb));
            break;
        }

        case EscapeType::DeviceAttr:
            if (m_responseCallback) {
                // Primary: VT220 (62) + Sixel (4) so chafa/img2sixel auto-enable
                // images. Secondary: claim to be an xterm-class terminal.
                m_responseCallback(seq.value == 1 ? "\x1b[>0;276;0c" : "\x1b[?62;4c");
            }
            break;

        default:
            break;
    }
}

void Terminal::placeImage(int wpx, int hpx, std::vector<uint32_t>&& argb) {
    renderer::TermImage im;
    im.anchorAbs = (long long)m_grid.absScroll() + m_grid.cursorRow();
    im.col  = m_grid.cursorCol();
    im.wpx  = wpx;
    im.hpx  = hpx;
    im.argb = std::move(argb);
    m_images.push_back(std::move(im));

    // Advance the cursor below the image (Sixel scrolling), scrolling the grid
    // as needed — feeding newlines reuses the grid's scroll/scrollback path.
    int rows = (m_cellPxH > 0) ? (hpx + m_cellPxH - 1) / m_cellPxH : 1;
    for (int i = 0; i < rows; ++i)
        m_grid.putCodepoint('\n');

    // Bound memory: drop images scrolled past the scrollback, and cap the count.
    long long oldest = (long long)m_grid.absScroll()
                     - m_grid.historyLines() - m_grid.rowCount();
    m_images.erase(std::remove_if(m_images.begin(), m_images.end(),
                   [&](const renderer::TermImage& g){ return g.anchorAbs < oldest; }),
                   m_images.end());
    const size_t kMaxImages = 128;
    if (m_images.size() > kMaxImages)
        m_images.erase(m_images.begin(), m_images.end() - kMaxImages);
}

std::string Terminal::mouseReport(int button, int col, int row, bool press,
                                  bool motion, int mods) const {
    if (m_mouseMode == 0) return "";
    int b = button + (motion ? 32 : 0) + mods;
    char buf[64];
    if (m_mouseSGR) {
        std::snprintf(buf, sizeof buf, "\x1b[<%d;%d;%d%c", b, col, row,
                      press ? 'M' : 'm');
    } else {
        int bb = press ? b : (3 + (motion ? 32 : 0) + mods);
        if (col > 223) col = 223;
        if (row > 223) row = 223;
        std::snprintf(buf, sizeof buf, "\x1b[M%c%c%c",
                      (char)(32 + bb), (char)(32 + col), (char)(32 + row));
    }
    return buf;
}
