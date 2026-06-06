#pragma once
#include <functional>
#include <string>
#include <vector>
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

    RenderCallback m_renderCallback;

    void handleText(const std::string& text);
    void applyEscape(const parser::EscapeSequence& seq);
};

} // namespace kterm::core