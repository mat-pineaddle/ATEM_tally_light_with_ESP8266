/*
    RcvParser - protocol parser for the RØDECaster Video (RCV) network control
    protocol, used to derive tally information.

    The RCV speaks OSC over TCP (port 10024). Every OSC packet is prefixed with
    a 4 byte little-endian length. This parser is fed raw bytes from the TCP
    stream and keeps track of what is on program / preview.

    The protocol is not officially documented. Everything here is derived from
    the Bitfocus Companion module (github.com/bitfocus/companion-module-rode-rcv).

    This class is plain C++ (no Arduino dependencies), so it can be unit tested
    on a PC.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef RcvParser_h
#define RcvParser_h

#include <stdint.h>
#include <stddef.h>

#define RCV_SRC_NONE    0
#define RCV_SRC_INPUT   1   // "videoIn"
#define RCV_SRC_SCENE   2   // "scene"
#define RCV_SRC_MEDIA   3   // "media"

#define RCV_MAX_INPUTS  6
#define RCV_MAX_SCENES  7

#define RCV_TALLY_FLAG_PROGRAM 0x01
#define RCV_TALLY_FLAG_PREVIEW 0x02

// OSC messages up to this size are fully buffered and parsed. Bigger messages
// (the /show XML dump, meter blobs) are streamed; only /show is inspected.
#ifndef RCV_PARSER_BUFFER
#define RCV_PARSER_BUFFER 256
#endif

// Anything bigger than this is treated as a broken stream.
#define RCV_MAX_PACKET_LENGTH (4UL * 1024UL * 1024UL)

// The Companion module treats PgmScene / PvwScene from the /show XML as
// 0-based (while /show/pgmcurrent ids are 1-based). Set to 0 if a real device
// proves otherwise.
#ifndef RCV_XML_SCENE_ZERO_BASED
#define RCV_XML_SCENE_ZERO_BASED 1
#endif

struct RcvBus {
    uint8_t type;   // RCV_SRC_*
    uint8_t id;     // 1-based (input 1-6, scene 1-7 = A-G), 0 = none
};

class RcvParser {
public:
    RcvParser();

    // Clear all state. Call on every (re)connect.
    void reset();

    // Feed raw bytes received from the TCP socket.
    void feed(const uint8_t *data, size_t length);
    void feed(uint8_t byte);

    // What is currently on program / preview. Preview is RCV_SRC_NONE in instant mode.
    RcvBus program() const;
    RcvBus preview() const;

    // Tally flags (RCV_TALLY_FLAG_*) for a video input (1-based), taking scenes into account.
    // sceneInputMasks: RCV_MAX_SCENES bitmasks, bit n = input n+1 is part of that scene.
    uint8_t inputTallyFlags(uint8_t inputNo, const uint8_t *sceneInputMasks) const;

    bool isStudioMode() const { return _studioMode; }
    bool isSwitchingModeKnown() const { return _modeKnown; }
    bool isStreaming() const { return _streaming; }
    bool isRecording() const { return _recording; }

    // True once the stream is unrecoverable (bad length prefix). The connection should be reset.
    bool hasProtocolError() const { return _protocolError; }

    // True (once) when the device asked us to re-request the show state (switching mode changed).
    bool consumeResyncRequest();

    // True (once) after any tally relevant change.
    bool consumeChanged();

    uint32_t messageCount() const { return _messageCount; }
    const char *lastAddress() const { return _lastAddress; }

    static const char *sourceTypeName(uint8_t type);

private:
    struct OscArg {
        char type;
        int32_t i;
        const char *s;
    };

    // Frame state
    uint8_t _lenBytes[4];
    uint8_t _lenPos;
    uint32_t _bodyLen;
    uint32_t _bodyPos;
    bool _inBody;
    uint8_t _buf[RCV_PARSER_BUFFER + 1];
    bool _xmlMode;

    // XML attribute scanner state (for the /show dump)
    uint8_t _xState;
    char _xName[24];
    uint8_t _xNameLen;
    char _xValue[32];
    uint8_t _xValueLen;
    char _xQuote;
    bool _xPgmFound, _xPvwFound, _xModeFound;

    // Tally state
    RcvBus _pgm, _pvw;              // authoritative, from /show/pgmcurrent and /show/pvwcurrent
    bool _pgmKnown, _pvwKnown;
    RcvBus _xmlPgm, _xmlPvw;        // startup hints from the /show XML dump
    uint8_t _inputButtonColour[RCV_MAX_INPUTS]; // hardware button LEDs, startup hint
    bool _studioMode, _modeKnown;
    bool _streaming, _recording;

    bool _protocolError;
    bool _resyncRequested;
    bool _changed;
    uint32_t _messageCount;
    char _lastAddress[40];

    void _bodyByte(uint8_t b);
    void _endBody();
    void _parsePacket(const uint8_t *p, uint32_t len, uint8_t depth);
    void _dispatch(const char *address, const OscArg *args, uint8_t argc);

    void _xmlReset();
    void _xmlByte(uint8_t c);
    void _xmlAttribute(const char *name, const char *value);

    static bool _busContainsInput(const RcvBus &bus, uint8_t inputNo, const uint8_t *sceneInputMasks);
    static uint8_t _typeFromString(const char *s);
    static uint32_t _readBE32(const uint8_t *p);
    static int32_t _argInt(const OscArg &arg);
};

#endif
