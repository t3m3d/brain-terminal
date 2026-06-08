#pragma once
#include <functional>
#include <string>
#include <vector>
#include <map>
#include "brain/renderer/Grid.hpp"
#include "brain/parser/AnsiParser.hpp"

namespace brain::core {

class Terminal {
public:
    using RenderCallback = std::function<void()>;

    Terminal(int cols, int rows);

    void resize(int cols, int rows);

    // Called by PTY when new data arrives
    void onPTYOutput(const std::vector<char>& data);

    // Expose grid for renderer
    const renderer::Grid& grid() const { return m_grid; }

    // Renderer calls this so Terminal can request repaints
    void setRenderCallback(RenderCallback cb) { m_renderCallback = std::move(cb); }

    using TitleCallback    = std::function<void(const std::string&)>;
    using BellCallback     = std::function<void()>;
    using ResponseCallback = std::function<void(const std::string&)>;
    // DECSCUSR cursor-shape change; arg is "block" | "underline" | "bar".
    using CursorStyleCallback = std::function<void(const std::string&)>;
    // OSC 52 clipboard write; arg is the base64 payload the app sent.
    using ClipboardCallback = std::function<void(const std::string&)>;
    void setTitleCallback   (TitleCallback    cb) { m_titleCallback    = std::move(cb); }
    void setBellCallback    (BellCallback     cb) { m_bellCallback     = std::move(cb); }
    void setResponseCallback(ResponseCallback cb) { m_responseCallback = std::move(cb); }
    void setCursorStyleCallback(CursorStyleCallback cb) { m_cursorStyleCallback = std::move(cb); }
    void setClipboardCallback(ClipboardCallback cb) { m_clipboardCallback = std::move(cb); }

    // Most-recent working directory reported by the shell via OSC 7, or empty.
    const std::string& cwd() const { return m_cwd; }

    // Cell size in pixels, so CSI 14t (report size in pixels) can be answered.
    void setCellPixels(int w, int h) { m_cellPxW = w; m_cellPxH = h; }
    void setPaletteColor(int idx, uint32_t argb) { m_grid.setPaletteColor(idx, argb); }

    // Alternate screen buffer flag — true while the child is in altscreen
    // (vim, less, htop…). Exposed so the widget can suppress scrollback
    // rendering and block wheel-scroll while the altscreen is active.
    bool altScreen() const { return m_altScreen; }

    // Most-recent window title set by the child (OSC 0 / OSC 2).
    const std::string& title() const { return m_title; }

    void setScrollback(int n) { m_grid.setHistoryMax(n); }

    // OSC 8 link id → URI. Returns empty string if id is unknown or 0.
    const std::string& linkUri(uint16_t id) const {
        static const std::string empty;
        auto it = m_linkUris.find(id);
        return it == m_linkUris.end() ? empty : it->second;
    }

    // Terminal modes (DEC private) for the frontend.
    bool bracketedPaste() const { return m_bracketedPaste; }
    bool cursorVisible()  const { return m_cursorVisible; }

    // Mouse reporting (DEC 1000 click, 1002 drag, 1003 any-motion; 1006 SGR
    // encoding). 0 = off. mouseReport() builds the escape to send over the PTY.
    // button: 0 left,1 middle,2 right,64 wheel-up,65 wheel-down. col/row 1-based.
    // mods: shift=4, alt=8, ctrl=16.
    int  mouseMode() const { return m_mouseMode; }
    bool mouseSGR()  const { return m_mouseSGR; }
    std::string mouseReport(int button, int col, int row, bool press,
                            bool motion, int mods) const;

    // OSC 133 command blocks. Status of the block CONTAINING an absolute line:
    // -1 none, 0 idle prompt (no bar), 1 success (exit 0), 2 failure (nonzero),
    // 3 running. Returns the status of the nearest prompt-start mark at/above.
    int blockStatusForLine(long absLine) const {
        if (m_blockMarks.empty()) return -1;
        auto it = m_blockMarks.upper_bound(absLine);   // first mark strictly above
        if (it == m_blockMarks.begin()) return -1;     // no mark at/above this line
        --it;
        return it->second;
    }
    bool hasBlockMarks() const { return !m_blockMarks.empty(); }

    // Status ONLY if absLine is itself a command's prompt-start line (else -1).
    // Used to draw a single tick per command rather than a full-block stripe.
    int blockMarkExact(long absLine) const {
        auto it = m_blockMarks.find(absLine);
        return it == m_blockMarks.end() ? -1 : it->second;
    }

    // Absolute line of the prompt-start of the block CONTAINING absLine (the
    // nearest mark at or above it), or -1 if none. Defines a block's top edge.
    long blockStartForLine(long absLine) const {
        if (m_blockMarks.empty()) return -1;
        auto it = m_blockMarks.upper_bound(absLine);
        if (it == m_blockMarks.begin()) return -1;
        --it;
        return it->first;
    }

    // Nearest prompt-start strictly above / below absLine (for Cmd-Up/Down
    // navigation), or -1 if there is none in that direction.
    long prevPromptLine(long absLine) const {
        auto it = m_blockMarks.lower_bound(absLine);   // first mark >= absLine
        if (it == m_blockMarks.begin()) return -1;
        --it;
        return it->first;
    }
    long nextPromptLine(long absLine) const {
        auto it = m_blockMarks.upper_bound(absLine);   // first mark > absLine
        return it == m_blockMarks.end() ? -1 : it->first;
    }

    // First/last prompt-start lines, or -1 when there are no marks. lastPrompt
    // is the "active" (most recent) command, lightly marked even without hover.
    long firstPromptLine() const { return m_blockMarks.empty() ? -1 : m_blockMarks.begin()->first; }
    long lastPromptLine()  const { return m_blockMarks.empty() ? -1 : m_blockMarks.rbegin()->first; }

private:
    int m_cols;
    int m_rows;
    int m_cursorRow;
    int m_cursorCol;

    renderer::Grid m_grid;
    parser::AnsiParser m_parser;

    std::string m_utf8;   // incomplete trailing UTF-8 sequence carried between feeds
    bool m_bracketedPaste = false;
    bool m_cursorVisible  = true;
    int  m_mouseMode = 0;     // 0 off, else 1000/1002/1003
    bool m_mouseSGR  = false; // DEC 1006 SGR encoding

    // OSC 133 command blocks: prompt-start absolute line -> status (0 run,1 ok,2 fail).
    std::map<long, int> m_blockMarks;
    long m_lastMarkLine = -1;   // abs line of the most recent prompt-start (133;A)

    // OSC 8 hyperlink table. id 0 is reserved for "no link". Allocated
    // sequentially; 16-bit wrap is fine — old entries simply get reused
    // and any cells still pointing at them resolve to the new URI. With
    // 64 K capacity this only matters on multi-hour ls --hyperlink
    // sessions and the wrap is correct, not corrupting.
    std::map<uint16_t, std::string> m_linkUris;
    uint16_t m_nextLinkId = 1;

    RenderCallback   m_renderCallback;
    TitleCallback    m_titleCallback;
    BellCallback     m_bellCallback;
    ResponseCallback m_responseCallback;
    CursorStyleCallback m_cursorStyleCallback;
    ClipboardCallback   m_clipboardCallback;
    std::string m_cwd;   // OSC 7 reported working directory
    int m_cellPxW = 8;    // cell pixel size, for CSI 14t replies
    int m_cellPxH = 16;

    // Alternate screen buffer support. We snapshot the entire main grid
    // (cells + cursor + attrs) on enter, and restore it on exit.
    bool m_altScreen = false;
    std::vector<std::vector<renderer::Cell>> m_savedRows;
    int  m_savedCursorRow = 0;
    int  m_savedCursorCol = 0;
    std::string m_title;

    // DEC save/restore cursor (DECSC / DECRC).
    int m_decscRow = 0;
    int m_decscCol = 0;
    bool m_decscValid = false;

    void handleText(const std::string& text);
    void applyEscape(const parser::EscapeSequence& seq);
    void enterAltScreen();
    void exitAltScreen();
};

} // namespace brain::core