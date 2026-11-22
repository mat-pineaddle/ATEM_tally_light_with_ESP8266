// Native unit test for RcvParser (no Arduino needed).
//   c++ -std=c++11 -Wall -Wextra -I libraries/RCVclient libraries/RCVclient/RcvParser.cpp tools/test_rcv_parser.cpp -o /tmp/rcvtest && /tmp/rcvtest

#include "RcvParser.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

typedef std::vector<uint8_t> Bytes;

static void putStr(Bytes &b, const std::string &s) {
    for (char c : s) b.push_back((uint8_t)c);
    b.push_back(0);
    while (b.size() % 4) b.push_back(0);
}
static void putInt(Bytes &b, int32_t v) {
    uint32_t u = (uint32_t)v;
    b.push_back(u >> 24); b.push_back(u >> 16); b.push_back(u >> 8); b.push_back(u);
}

struct Arg { char t; int32_t i; std::string s; };
static Arg I(int32_t v) { return Arg{'i', v, ""}; }
static Arg S(const std::string &v) { return Arg{'s', 0, v}; }

static Bytes msg(const std::string &addr, std::vector<Arg> args = {}) {
    Bytes b;
    putStr(b, addr);
    std::string tags = ",";
    for (auto &a : args) tags += a.t;
    putStr(b, tags);
    for (auto &a : args) {
        if (a.t == 'i') putInt(b, a.i); else putStr(b, a.s);
    }
    return b;
}

static Bytes frame(const Bytes &p) {
    Bytes b;
    uint32_t n = p.size();
    b.push_back(n); b.push_back(n >> 8); b.push_back(n >> 16); b.push_back(n >> 24);
    b.insert(b.end(), p.begin(), p.end());
    return b;
}

// Feed in small, uneven chunks to exercise stream reassembly.
static void feedChunked(RcvParser &p, const Bytes &b, size_t chunk = 7) {
    for (size_t i = 0; i < b.size(); i += chunk) {
        size_t n = std::min(chunk, b.size() - i);
        p.feed(b.data() + i, n);
    }
}

int main() {
    uint8_t masks[RCV_MAX_SCENES] = {0};
    masks[1] = 0b000011; // Scene B = inputs 1 + 2 (picture in picture)
    masks[2] = 0b000110; // Scene C = inputs 2 + 3

    // --- Inputs on program / preview ---
    {
        RcvParser p;
        feedChunked(p, frame(msg("/show/pgmcurrent", {S("videoIn"), I(2)})));
        feedChunked(p, frame(msg("/show/pvwcurrent", {S("videoIn"), I(3)})));
        CHECK(p.program().type == RCV_SRC_INPUT && p.program().id == 2);
        CHECK(p.preview().type == RCV_SRC_INPUT && p.preview().id == 3);
        CHECK(p.inputTallyFlags(2, masks) == RCV_TALLY_FLAG_PROGRAM);
        CHECK(p.inputTallyFlags(3, masks) == RCV_TALLY_FLAG_PREVIEW);
        CHECK(p.inputTallyFlags(1, masks) == 0);
        CHECK(p.messageCount() == 2);
    }

    // --- Scenes: all mapped inputs light up, both program and preview ---
    {
        RcvParser p;
        feedChunked(p, frame(msg("/show/pgmcurrent", {S("scene"), I(2)})));  // B
        feedChunked(p, frame(msg("/show/pvwcurrent", {S("scene"), I(3)})));  // C
        CHECK(p.inputTallyFlags(1, masks) == RCV_TALLY_FLAG_PROGRAM);
        CHECK(p.inputTallyFlags(2, masks) == (RCV_TALLY_FLAG_PROGRAM | RCV_TALLY_FLAG_PREVIEW));
        CHECK(p.inputTallyFlags(3, masks) == RCV_TALLY_FLAG_PREVIEW);
        CHECK(p.inputTallyFlags(4, masks) == 0);
        // Media on program: no camera is live
        feedChunked(p, frame(msg("/show/pgmcurrent", {S("media"), I(1)})));
        CHECK(p.inputTallyFlags(1, masks) == 0);
        CHECK(p.inputTallyFlags(2, masks) == RCV_TALLY_FLAG_PREVIEW);
    }

    // --- Instant mode hides preview ---
    {
        RcvParser p;
        feedChunked(p, frame(msg("/show/pvwcurrent", {S("videoIn"), I(4)})));
        CHECK(p.inputTallyFlags(4, masks) == RCV_TALLY_FLAG_PREVIEW);
        feedChunked(p, frame(msg("/show/switchingMode", {S("instant")})));
        CHECK(p.inputTallyFlags(4, masks) == 0);
        CHECK(p.consumeResyncRequest());
        CHECK(!p.consumeResyncRequest());
        feedChunked(p, frame(msg("/show/switchingMode", {S("studioLeft")})));
        CHECK(p.inputTallyFlags(4, masks) == RCV_TALLY_FLAG_PREVIEW);
    }

    // --- Big /show XML dump (streamed, bigger than the buffer) gives startup hints ---
    {
        RcvParser p;
        std::string xml = "<?xml version=\"1.0\"?>\n<RcvShow name=\"My show\" switching_mode=\"studioLeft\" PgmScene=\"1\" PvwScene=\"2\" frameRate=\"25\">\n<Scenes>";
        for (int i = 0; i < 50; i++) xml += "<Scene name=\"Some long scene name to make it big\" PgmScene=\"6\"/>\n";
        xml += "</Scenes></RcvShow>";
        Bytes f = frame(msg("/show", {S(xml)}));
        CHECK(f.size() > RCV_PARSER_BUFFER);
        feedChunked(p, f, 13);
        // XML scenes are 0-based: 1 => B, 2 => C
        CHECK(p.program().type == RCV_SRC_SCENE && p.program().id == 2);
        CHECK(p.preview().type == RCV_SRC_SCENE && p.preview().id == 3);
        CHECK(p.isSwitchingModeKnown() && p.isStudioMode());
        // Live update overrides the hint
        feedChunked(p, frame(msg("/show/pgmcurrent", {S("videoIn"), I(5)})));
        CHECK(p.program().type == RCV_SRC_INPUT && p.program().id == 5);
        CHECK(p.inputTallyFlags(1, masks) == 0);
        CHECK(p.inputTallyFlags(5, masks) == RCV_TALLY_FLAG_PROGRAM);
    }

    // --- Button LED colours as startup hint ---
    {
        RcvParser p;
        feedChunked(p, frame(msg("/device/buttons/10/colour", {I(4)}))); // input 3 PGM
        feedChunked(p, frame(msg("/device/buttons/8/colour", {I(5)})));  // input 1 PV
        feedChunked(p, frame(msg("/device/buttons/2/colour", {I(4)})));  // scene/media button - ignored
        CHECK(p.inputTallyFlags(3, masks) == RCV_TALLY_FLAG_PROGRAM);
        CHECK(p.inputTallyFlags(1, masks) == RCV_TALLY_FLAG_PREVIEW);
    }

    // --- Streaming / recording, bundles, unknown + big messages ---
    {
        RcvParser p;
        Bytes bundle;
        putStr(bundle, "#bundle");
        for (int i = 0; i < 8; i++) bundle.push_back(0);
        Bytes m1 = msg("/show/live", {I(1)});
        Bytes m2 = msg("/show/record", {I(1)});
        putInt(bundle, m1.size()); bundle.insert(bundle.end(), m1.begin(), m1.end());
        putInt(bundle, m2.size()); bundle.insert(bundle.end(), m2.begin(), m2.end());
        feedChunked(p, frame(bundle));
        CHECK(p.isStreaming());
        CHECK(p.isRecording());

        Bytes big = msg("/meters/values", {S(std::string(2000, 'x'))});
        feedChunked(p, frame(big), 100);
        feedChunked(p, frame(msg("/some/unknown", {I(1), S("x"), I(2)})));
        feedChunked(p, frame(msg("/show/pgmcurrent", {S("videoIn"), I(6)})));
        CHECK(p.program().id == 6);
        CHECK(!p.hasProtocolError());
    }

    // --- Zero length prefix is skipped, absurd length is an error ---
    {
        RcvParser p;
        Bytes zero = {0, 0, 0, 0};
        p.feed(zero.data(), zero.size());
        feedChunked(p, frame(msg("/show/pgmcurrent", {S("videoIn"), I(1)})));
        CHECK(p.program().id == 1);
        Bytes bad = {0xff, 0xff, 0xff, 0x7f};
        p.feed(bad.data(), bad.size());
        CHECK(p.hasProtocolError());
        p.reset();
        CHECK(!p.hasProtocolError());
        CHECK(p.program().type == RCV_SRC_NONE);
    }

    if (failures == 0) {
        printf("All RcvParser tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
