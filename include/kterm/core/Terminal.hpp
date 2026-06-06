#pragma once
#include <functional>
#include <string>
#include <vector>
#include <map>
#include "kterm/renderer/Grid.hpp"
#include "kterm/parser/AnsiParser.hpp"

namespace kterm::core {

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

    // Terminal modes (DEC private) for the frontend.
    bool bracketedPaste() const { return m_bracketedPaste; }
    bool cursorVisible()  const { return m_cursorVisible; }

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

    // OSC 133 command blocks: prompt-start absolute line -> status (0 run,1 ok,2 fail).
    std::map<long, int> m_blockMarks;
    long m_lastMarkLine = -1;   // abs line of the most recent prompt-start (133;A)

    RenderCallback m_renderCallback;

    void handleText(const std::string& text);
    void applyEscape(const parser::EscapeSequence& seq);
};

} // namespace kterm::core