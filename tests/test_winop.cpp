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
    std::string cstyle;
    Terminal t3(20, 5);
    t3.setCursorStyleCallback([&](const std::string& s){ cstyle = s; });
    auto feed3 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t3.onPTYOutput(v); };
    feed3("\x1b[5 q"); bool okBar   = (cstyle == "bar");
    feed3("\x1b[2 q"); bool okBlock = (cstyle == "block");
    feed3("\x1b[4 q"); bool okUnder = (cstyle == "underline");
    bool okCursor = okBar && okBlock && okUnder;
    std::cout << "DECSCUSR 5/2/4 -> bar/block/underline: " << (okCursor ? "yes" : "NO") << "\n";

    // SGR 9 strikethrough + SGR 2 dim land on the right cells.
    Terminal t4(20, 5);
    auto feed4 = [&](const std::string& s){ std::vector<char> v(s.begin(), s.end()); t4.onPTYOutput(v); };
    feed4("\x1b[9mA\x1b[0m\x1b[2mB");
    bool okStrike = (t4.grid().rows()[0][0].attrs & brain::renderer::ATTR_STRIKE) != 0;
    bool okDim    = (t4.grid().rows()[0][1].attrs & brain::renderer::ATTR_DIM)    != 0;
    std::cout << "SGR 9 strike / SGR 2 dim: " << ((okStrike && okDim) ? "yes" : "NO") << "\n";

    bool all = ok18 && ok14 && okCHA && okCursor && okStrike && okDim;
    std::cout << (all ? "PASS\n" : "FAIL\n");
    return all ? 0 : 1;
}
