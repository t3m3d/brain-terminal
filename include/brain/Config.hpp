#pragma once
#include <string>

namespace brain {

// User-tunable settings read from brain.conf. See
// resources/config/brain.conf for the documented defaults.
class Config {
public:
    static Config load(const std::string& path);
    static Config defaults();

    const std::string& shell()          const { return m_shell; }
    const std::string& themePath()      const { return m_themePath; }
    const std::string& fontFamily()     const { return m_fontFamily; }
    int                fontSize()       const { return m_fontSize; }
    int                windowWidth()    const { return m_windowWidth; }
    int                windowHeight()   const { return m_windowHeight; }
    int                scrollback()     const { return m_scrollback; }
    int                opacityPercent() const { return m_opacityPercent; }
    const std::string& cursorStyle()    const { return m_cursorStyle; }
    const std::string& startupCommand() const { return m_startupCommand; }

private:
    std::string m_shell;
    std::string m_themePath;
    std::string m_fontFamily;
    int         m_fontSize       = 14;
    int         m_windowWidth    = 1000;
    int         m_windowHeight   = 640;
    int         m_scrollback     = 5000;
    int         m_opacityPercent = 100;
    std::string m_cursorStyle    = "block";    // block | underline | bar
    std::string m_startupCommand = "";         // command sent to shell after spawn

    Config() = default;
};

}
