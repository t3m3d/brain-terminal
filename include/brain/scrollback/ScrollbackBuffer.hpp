#pragma once
#include <deque>
#include <string>
#include <vector>

namespace brain::scrollback {

class ScrollbackBuffer {
public:
    ScrollbackBuffer(size_t maxLines = 1000);

    void pushLine(const std::string& line);
    const std::deque<std::string>& lines() const;
    void clear();

private:
    size_t m_maxLines;
    std::deque<std::string> m_lines;
};

}