#include "brain/scrollback/ScrollbackBuffer.hpp"

using namespace brain::scrollback;

ScrollbackBuffer::ScrollbackBuffer(size_t maxLines)
    : m_maxLines(maxLines) {}

void ScrollbackBuffer::pushLine(const std::string& line) {
    m_lines.push_back(line);
    if (m_lines.size() > m_maxLines)
        m_lines.pop_front();
}

const std::deque<std::string>& ScrollbackBuffer::lines() const {
    return m_lines;
}

void ScrollbackBuffer::clear() {
    m_lines.clear();
}