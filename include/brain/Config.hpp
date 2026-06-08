#pragma once
#include <string>
#include <cstdint>
#include <array>

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
    const std::string& fontWeight()     const { return m_fontWeight; }
    bool               useBold()        const { return m_useBold; }
    int                windowWidth()    const { return m_windowWidth; }
    int                windowHeight()   const { return m_windowHeight; }
    int                scrollback()     const { return m_scrollback; }
    int                opacityPercent() const { return m_opacityPercent; }
    const std::string& cursorStyle()    const { return m_cursorStyle; }
    bool               cursorBlink()    const { return m_cursorBlink; }
    int                cursorBlinkInterval() const { return m_cursorBlinkInterval; }
    int                scrollLines()    const { return m_scrollLines; }
    const std::string& bell()           const { return m_bell; }
    const std::string& wordSeparators() const { return m_wordSeparators; }
    bool               copyOnSelect()   const { return m_copyOnSelect; }
    const std::string& startupCommand() const { return m_startupCommand; }

    // "auto" | "on" | "off". auto = tabs everywhere except tiling WMs.
    const std::string& tabsMode()       const { return m_tabsMode; }

    // Colours as 0xAARRGGBB; 0 means "unset, use the theme/default".
    uint32_t foreground()  const { return m_foreground; }
    uint32_t background()  const { return m_background; }
    uint32_t cursorColor() const { return m_cursorColor; }
    uint32_t selectionBg() const { return m_selectionBg; }
    uint32_t selectionFg() const { return m_selectionFg; }
    uint32_t paletteColor(int i) const { return (i >= 0 && i < 16) ? m_palette[i] : 0; }
    int      paddingX()    const { return m_paddingX; }
    int      paddingY()    const { return m_paddingY; }

    // Absolute/relative path of the file load() actually read ("" if none was
    // found and defaults were used). Used to watch the file for live reload.
    const std::string& sourcePath() const { return m_sourcePath; }

private:
    std::string m_shell;
    std::string m_themePath;
    std::string m_fontFamily;
    int         m_fontSize       = 14;
    std::string m_fontWeight     = "normal";   // thin/extralight/light/normal/medium/demibold/bold/extrabold/black
    bool        m_useBold        = true;       // honour SGR 1 bold
    int         m_windowWidth    = 1000;
    int         m_windowHeight   = 640;
    int         m_scrollback     = 5000;
    int         m_opacityPercent = 100;
    std::string m_cursorStyle    = "block";    // block | underline | bar
    bool        m_cursorBlink    = false;       // blink the cursor
    int         m_cursorBlinkInterval = 530;    // blink half-period in ms
    int         m_scrollLines    = 3;           // lines per wheel notch
    std::string m_bell           = "urgent";    // urgent | audible | none
    std::string m_wordSeparators = "";          // extra chars that break a word-select
    bool        m_copyOnSelect   = false;        // also copy selection to the clipboard
    std::string m_startupCommand = "";         // command sent to shell after spawn
    std::string m_tabsMode       = "auto";     // auto | on | off

    uint32_t m_foreground  = 0;   // 0 = unset
    uint32_t m_background  = 0;
    uint32_t m_cursorColor = 0;
    uint32_t m_selectionBg = 0;
    uint32_t m_selectionFg = 0;
    std::array<uint32_t, 16> m_palette{};   // all 0 = unset
    int m_paddingX = 6;
    int m_paddingY = 4;
    std::string m_sourcePath;   // file load() read, for live-reload watching

    Config() = default;
};

}
