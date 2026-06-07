#pragma once
#include <string>

namespace brain {

class Config {
public:
    static Config load(const std::string& path);
    static Config defaults();

    const std::string& shell() const { return m_shell; }
    const std::string& themePath() const { return m_themePath; }
    const std::string& fontFamily() const { return m_fontFamily; }
    int fontSize() const { return m_fontSize; }

private:
    std::string m_shell;
    std::string m_themePath;
    std::string m_fontFamily;
    int m_fontSize;

    Config(const std::string& shell,
           const std::string& theme,
           const std::string& fontFamily,
           int fontSize)
        : m_shell(shell),
          m_themePath(theme),
          m_fontFamily(fontFamily),
          m_fontSize(fontSize) {}
};

}