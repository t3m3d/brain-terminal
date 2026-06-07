#pragma once
#include "Grid.hpp"

namespace brain::renderer {

class Renderer {
public:
    Renderer() = default;
    virtual ~Renderer() = default;

    // Called by TerminalWidget to draw the entire grid
    virtual void drawGrid(const Grid& grid) = 0;

    // Optional: draw a single cell (UI backend may override)
    virtual void drawCell(int row, int col, const Cell& cell) = 0;
};

}