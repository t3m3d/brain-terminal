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

    std::cout << (ok18 && ok14 ? "PASS\n" : "FAIL\n");
    return (ok18 && ok14 && okCHA) ? 0 : 1;
}
