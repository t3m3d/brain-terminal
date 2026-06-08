#include "brain/core/Terminal.hpp"
#include <iostream>
#include <string>
using namespace brain::core;
int main() {
    Terminal t(95, 28);
    t.resize(95, 28);
    t.setCellPixels(9, 18);
    std::string reply;
    t.setResponseCallback([&](const std::string& s){ reply += s; });
    auto feed = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t.onPTYOutput(v); };
    feed("\x1b[18t");
    std::cout << "CSI 18t reply: " << (reply.size()? reply.substr(1) : "(none)") << "  [esc-stripped]\n";
    bool ok18 = (reply == "\x1b[8;28;95t");
    reply.clear();
    feed("\x1b[14t");
    std::cout << "CSI 14t reply: " << (reply.size()? reply.substr(1) : "(none)") << "\n";
    bool ok14 = (reply == "\x1b[4;504;855t");   // 28*18=504, 95*9=855
    // CHA positioning: "abc", then ESC[1G (col 1) overwrites with "X" at col 0.
    Terminal t2(20, 5);
    auto feed2 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t2.onPTYOutput(v); };
    feed2("abc\x1b[1GX");
    bool okCHA = (t2.grid().rows()[0][0].ch == 'X' && t2.grid().rows()[0][1].ch == 'b');
    std::cout << "CHA (ESC[1G) repositions: " << (okCHA ? "yes" : "NO") << "\n";

    // DECSCUSR (CSI Ps SP q): vim insert-mode bar, etc.
    std::string cstyle; bool cblink = false;
    Terminal t3(20, 5);
    t3.setCursorStyleCallback([&](const std::string& s, bool b){ cstyle = s; cblink = b; });
    auto feed3 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t3.onPTYOutput(v); };
    feed3("\x1b[5 q"); bool okBar   = (cstyle == "bar") && cblink;       // 5 = blinking bar
    feed3("\x1b[2 q"); bool okBlock = (cstyle == "block") && !cblink;    // 2 = steady block
    feed3("\x1b[4 q"); bool okUnder = (cstyle == "underline") && !cblink;// 4 = steady underline
    bool okCursor = okBar && okBlock && okUnder;
    std::cout << "DECSCUSR 5/2/4 -> bar/block/underline: " << (okCursor ? "yes" : "NO") << "\n";

    // SGR 9 strikethrough + SGR 2 dim land on the right cells.
    Terminal t4(20, 5);
    auto feed4 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t4.onPTYOutput(v); };
    feed4("\x1b[9mA\x1b[0m\x1b[2mB");
    bool okStrike = (t4.grid().rows()[0][0].attrs & brain::renderer::ATTR_STRIKE) != 0;
    bool okDim    = (t4.grid().rows()[0][1].attrs & brain::renderer::ATTR_DIM)    != 0;
    std::cout << "SGR 9 strike / SGR 2 dim: " << ((okStrike && okDim) ? "yes" : "NO") << "\n";

    // OSC 52 clipboard write: callback receives the base64 payload.
    std::string clip;
    Terminal t5(20, 5);
    t5.setClipboardCallback([&](const std::string& b64){ clip = b64; });
    auto feed5 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t5.onPTYOutput(v); };
    feed5("\x1b]52;c;aGVsbG8=\x07");          // base64("hello")
    bool okClip = (clip == "aGVsbG8=");
    std::cout << "OSC 52 clipboard payload: " << (okClip ? "yes" : "NO") << "\n";

    // OSC 7 working directory: percent-decoded path stored.
    Terminal t6(20, 5);
    auto feed6 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t6.onPTYOutput(v); };
    feed6("\x1b]7;file://host/home/me/a%20b\x07");
    bool okCwd = (t6.cwd() == "/home/me/a b");
    std::cout << "OSC 7 cwd (percent-decoded): " << (okCwd ? "yes" : "NO") << "\n";

    // Wide characters: a CJK glyph occupies two columns (glyph + spacer),
    // ASCII after it stays aligned, and copy skips the spacer.
    Terminal t7(20, 3);
    auto feed7 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t7.onPTYOutput(v); };
    feed7("\xE4\xB8\x80""X");          // U+4E00 (一, wide) then 'X'
    const auto& r0 = t7.grid().rows()[0];
    bool okWide = (r0[0].ch == 0x4E00)   // wide glyph in col 0
               && (r0[1].ch == 0)         // continuation spacer in col 1
               && (r0[2].ch == 'X');      // ASCII lands in col 2, not col 1
    std::cout << "Wide CJK occupies 2 cols (一X -> [0]=U+4E00 [1]=spacer [2]=X): "
              << (okWide ? "yes" : "NO") << "\n";

    // Sixel: a DCS image registers an inline image and moves the cursor below it.
    Terminal t8(40, 12);
    t8.setCellPixels(10, 20);
    auto feed8 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t8.onPTYOutput(v); };
    // 2 colours, a couple of bands of solid sixels: "#0;2;100;0;0" red, "@@@" etc.
    feed8("\x1bP0;0;8q\"1;1;6;12#0;2;100;0;0~~~~~~$-~~~~~~\x1b\\");
    bool okSixel = (t8.images().size() == 1) && (t8.images()[0].wpx == 6) && (t8.images()[0].hpx == 12);
    std::cout << "Sixel DCS -> 1 image " << (t8.images().empty() ? "(none)" :
                 (std::to_string(t8.images()[0].wpx) + "x" + std::to_string(t8.images()[0].hpx)))
              << ": " << (okSixel ? "yes" : "NO") << "\n";

    // Primary DA must advertise sixel (4) so chafa/img2sixel auto-enable images.
    std::string da;
    Terminal t9(20, 5);
    t9.setResponseCallback([&](const std::string& s){ da += s; });
    auto feed9 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t9.onPTYOutput(v); };
    feed9("\x1b[c");
    bool okDA = (da == "\x1b[?62;4c");
    std::cout << "Primary DA advertises sixel (ESC[?62;4c): " << (okDA ? "yes" : "NO") << "\n";

    // Underline styles: SGR 4:3 = curly (undercurl), used by LSP/nvim.
    Terminal t10(20, 3);
    auto feedA = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t10.onPTYOutput(v); };
    feedA("\x1b[4:3mU\x1b[4:2mD\x1b[24mN");
    const auto& ru = t10.grid().rows()[0];
    bool okUl = (ru[0].attrs & brain::renderer::ATTR_UNDERLINE) && ru[0].ulStyle == brain::renderer::UL_CURLY
             && (ru[1].attrs & brain::renderer::ATTR_UNDERLINE) && ru[1].ulStyle == brain::renderer::UL_DOUBLE
             && !(ru[2].attrs & brain::renderer::ATTR_UNDERLINE);
    std::cout << "Underline styles (4:3 curly / 4:2 double / 24 off): " << (okUl ? "yes" : "NO") << "\n";

    // Underline colour (SGR 58): colon truecolor + reset (LSP red squiggles).
    Terminal t11(20, 3);
    auto feedB = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t11.onPTYOutput(v); };
    feedB("\x1b[4:3;58:2::255:0:0mE\x1b[59mF");
    const auto& rc = t11.grid().rows()[0];
    bool okUlColor = (rc[0].ulColor == 0xFFFF0000) && (rc[1].ulColor == 0);
    std::cout << "Underline colour (58:2 red, 59 reset): " << (okUlColor ? "yes" : "NO") << "\n";

    // DEC 1004 focus-reporting mode toggles.
    Terminal t12(20, 3);
    auto feedC = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t12.onPTYOutput(v); };
    feedC("\x1b[?1004h"); bool on1004  = t12.focusReporting();
    feedC("\x1b[?1004l"); bool off1004 = !t12.focusReporting();
    bool okFocus = on1004 && off1004;
    std::cout << "DEC 1004 focus reporting toggles: " << (okFocus ? "yes" : "NO") << "\n";

    // OSC 11 background query → rgb reply (light/dark detection by delta/bat/nvim).
    std::string oscReply;
    Terminal t13(20, 3);
    t13.setReportColors(0xFFC0CAF5, 0xFF1A1B26, 0xFFC0CAF5);
    t13.setResponseCallback([&](const std::string& s){ oscReply += s; });
    auto feedD = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t13.onPTYOutput(v); };
    feedD("\x1b]11;?\x1b\\");
    bool okOscQ = (oscReply == "\x1b]11;rgb:1a1a/1b1b/2626\x1b\\");
    std::cout << "OSC 11 bg query reply: " << (okOscQ ? "yes" : "NO") << " [" << oscReply.substr(oscReply.size()>2?2:0) << "]\n";

    // OSC 4 palette set: 4;1;#ff0000 recolours ANSI red, visible via SGR 31.
    Terminal t14(20, 3);
    auto feedE = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t14.onPTYOutput(v); };
    feedE("\x1b]4;1;#ff0000\x1b\\\x1b[31mR");
    bool okOsc4 = (t14.grid().rows()[0][0].fg == 0xFFFF0000);
    std::cout << "OSC 4 palette set (red): " << (okOsc4 ? "yes" : "NO") << "\n";

    bool all = ok18 && ok14 && okCHA && okCursor && okStrike && okDim
            && okClip && okCwd && okWide && okSixel && okDA && okUl && okUlColor && okFocus
            && okOscQ && okOsc4;
    std::cout << (all ? "PASS\n" : "FAIL\n");
    return all ? 0 : 1;
}
