/*
    RcvParser - see RcvParser.h

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "RcvParser.h"
#include <string.h>
#include <stdlib.h>

// XML scanner states
#define XS_TEXT     0   // outside tags
#define XS_TAGNAME  1   // after '<'
#define XS_INTAG    2   // between attributes
#define XS_ATTRNAME 3
#define XS_QUOTE    4   // after '=', waiting for the opening quote
#define XS_VALUE    5

// XML elements we care about
#define XE_OTHER    0
#define XE_ROOT     1   // <RcvShow>
#define XE_SCENE    2   // <Scenes><Scene>
#define XE_LAYER    3   // <Scenes><Scene><VideoLayers><Layer>

// Hardware button LED colour codes (from the Companion module)
#define RCV_BUTTON_COLOUR_PGM 4
#define RCV_BUTTON_COLOUR_PVW 5

// Hardware buttons 8-13 are video inputs 1-6
#define RCV_FIRST_INPUT_BUTTON 8

RcvParser::RcvParser() {
    reset();
}

void RcvParser::reset() {
    _lenPos = 0;
    _bodyLen = 0;
    _bodyPos = 0;
    _inBody = false;
    _xmlMode = false;
    _xmlReset();

    _pgm.type = RCV_SRC_NONE; _pgm.id = 0;
    _pvw.type = RCV_SRC_NONE; _pvw.id = 0;
    _scenesKnown = false;
    memset(_sceneInputs, 0, sizeof(_sceneInputs));
    memset(_sceneNames, 0, sizeof(_sceneNames));
    _xmlPgm = _pgm;
    _xmlPvw = _pgm;
    _pgmKnown = false;
    _pvwKnown = false;
    memset(_inputButtonColour, 0, sizeof(_inputButtonColour));
    _studioMode = true;
    _modeKnown = false;
    _streaming = false;
    _recording = false;

    _protocolError = false;
    _resyncRequested = false;
    _changed = true;
    _messageCount = 0;
    _lastAddress[0] = '\0';
}

void RcvParser::feed(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        feed(data[i]);
    }
}

void RcvParser::feed(uint8_t b) {
    if (_protocolError) {
        return;
    }

    if (!_inBody) {
        _lenBytes[_lenPos++] = b;
        if (_lenPos < 4) {
            return;
        }
        _lenPos = 0;
        int32_t len = (int32_t)((uint32_t)_lenBytes[0] | ((uint32_t)_lenBytes[1] << 8) | ((uint32_t)_lenBytes[2] << 16) | ((uint32_t)_lenBytes[3] << 24));
        if (len <= 0) {
            // Same as the Companion module: skip an empty/invalid prefix and carry on.
            return;
        }
        if ((uint32_t)len > RCV_MAX_PACKET_LENGTH) {
            _protocolError = true;
            return;
        }
        _bodyLen = (uint32_t)len;
        _bodyPos = 0;
        _xmlMode = false;
        _inBody = true;
        return;
    }

    _bodyByte(b);
}

void RcvParser::_bodyByte(uint8_t b) {
    if (_bodyPos < RCV_PARSER_BUFFER) {
        _buf[_bodyPos] = b;
    }
    _bodyPos++;

    if (_bodyPos == 8) {
        // "/show" padded to 8 bytes => the full show dump (XML string argument)
        _xmlMode = memcmp(_buf, "/show\0\0\0", 8) == 0;
        if (_xmlMode) {
            _xmlReset();
        }
    } else if (_bodyPos > 8 && _xmlMode) {
        _xmlByte(b);
    }

    if (_bodyPos >= _bodyLen) {
        _endBody();
        _inBody = false;
    }
}

void RcvParser::_endBody() {
    _messageCount++;

    if (_xmlMode) {
        _xmlEnd();
        strncpy(_lastAddress, "/show (XML)", sizeof(_lastAddress) - 1);
        _lastAddress[sizeof(_lastAddress) - 1] = '\0';
        return;
    }

    if (_bodyLen <= RCV_PARSER_BUFFER) {
        _buf[_bodyLen] = '\0';
        _parsePacket(_buf, _bodyLen, 0);
    }
    // Bigger non /show packets (e.g. meter blobs) are ignored.
}

uint32_t RcvParser::_readBE32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t pad4(uint32_t n) {
    return (n + 3) & ~3UL;
}

void RcvParser::_parsePacket(const uint8_t *p, uint32_t len, uint8_t depth) {
    if (len < 4 || depth > 4) {
        return;
    }

    // OSC bundle: "#bundle\0" + 8 byte timetag + (int32 size + element)*
    if (len >= 16 && memcmp(p, "#bundle\0", 8) == 0) {
        uint32_t off = 16;
        while (off + 4 <= len) {
            uint32_t size = _readBE32(p + off);
            off += 4;
            if (size > len - off) {
                return;
            }
            // Element data is not null terminated by itself; copy is not needed because
            // strings inside a valid OSC message are always null terminated and padded.
            _parsePacket(p + off, size, depth + 1);
            off += size;
        }
        return;
    }

    if (p[0] != '/') {
        return;
    }

    // Address
    const uint8_t *nul = (const uint8_t *)memchr(p, 0, len);
    if (nul == NULL) {
        return;
    }
    const char *address = (const char *)p;
    uint32_t off = pad4((uint32_t)(nul - p) + 1);

    OscArg args[4];
    uint8_t argc = 0;

    // Type tag string (optional in very old OSC, but always present here)
    if (off < len && p[off] == ',') {
        const uint8_t *tagNul = (const uint8_t *)memchr(p + off, 0, len - off);
        if (tagNul == NULL) {
            return;
        }
        const char *tags = (const char *)(p + off + 1);
        off = pad4((uint32_t)(tagNul - p) + 1);

        for (const char *t = tags; *t != '\0' && argc < 4; t++) {
            OscArg arg;
            arg.type = *t;
            arg.i = 0;
            arg.s = NULL;
            switch (*t) {
                case 'i':
                case 'f':   // keep raw bits, only ints are used
                case 'c':
                case 'r':
                case 'm':
                    if (off + 4 > len) return;
                    arg.i = (int32_t)_readBE32(p + off);
                    off += 4;
                    break;
                case 's':
                case 'S': {
                    if (off >= len) return;
                    const uint8_t *sNul = (const uint8_t *)memchr(p + off, 0, len - off);
                    if (sNul == NULL) return;
                    arg.s = (const char *)(p + off);
                    off = pad4((uint32_t)(sNul - p) + 1);
                    break;
                }
                case 'b': {
                    if (off + 4 > len) return;
                    uint32_t size = _readBE32(p + off);
                    off += 4;
                    if (size > len - off) return;
                    off += pad4(size);
                    break;
                }
                case 'h':
                case 't':
                case 'd':
                    if (off + 8 > len) return;
                    off += 8;
                    break;
                case 'T':
                    arg.i = 1;
                    break;
                case 'F':
                case 'N':
                case 'I':
                    break;
                default:
                    // Unknown type, stop parsing args but still dispatch what we have.
                    goto dispatch;
            }
            args[argc++] = arg;
        }
    }

dispatch:
    strncpy(_lastAddress, address, sizeof(_lastAddress) - 1);
    _lastAddress[sizeof(_lastAddress) - 1] = '\0';
    _dispatch(address, args, argc);
}

int32_t RcvParser::_argInt(const OscArg &arg) {
    if (arg.type == 's' || arg.type == 'S') {
        return arg.s ? atoi(arg.s) : 0;
    }
    return arg.i;
}

uint8_t RcvParser::_typeFromString(const char *s) {
    if (s == NULL) return RCV_SRC_NONE;
    if (strcmp(s, "videoIn") == 0) return RCV_SRC_INPUT;
    if (strcmp(s, "scene") == 0) return RCV_SRC_SCENE;
    if (strcmp(s, "media") == 0) return RCV_SRC_MEDIA;
    return RCV_SRC_NONE;
}

const char *RcvParser::sourceTypeName(uint8_t type) {
    switch (type) {
        case RCV_SRC_INPUT: return "Input";
        case RCV_SRC_SCENE: return "Scene";
        case RCV_SRC_MEDIA: return "Media";
        default: return "None";
    }
}

void RcvParser::_dispatch(const char *address, const OscArg *args, uint8_t argc) {
    if (strcmp(address, "/show/pgmcurrent") == 0 || strcmp(address, "/show/pvwcurrent") == 0) {
        if (argc < 2) return;
        RcvBus bus;
        bus.type = (args[0].type == 's' || args[0].type == 'S') ? _typeFromString(args[0].s) : RCV_SRC_NONE;
        int32_t id = _argInt(args[1]);
        bus.id = (id > 0 && id < 256) ? (uint8_t)id : 0;
        if (bus.id == 0) bus.type = RCV_SRC_NONE;

        if (strcmp(address, "/show/pgmcurrent") == 0) {
            _pgm = bus;
            _pgmKnown = true;
        } else {
            _pvw = bus;
            _pvwKnown = true;
        }
        _changed = true;
        return;
    }

    if (strcmp(address, "/show/switchingMode") == 0) {
        if (argc < 1 || args[0].s == NULL) return;
        _studioMode = strcmp(args[0].s, "instant") != 0;
        _modeKnown = true;
        _resyncRequested = true; // Companion re-requests /show and /remote here
        _changed = true;
        return;
    }

    if (strcmp(address, "/show/live") == 0) {
        if (argc < 1) return;
        _streaming = _argInt(args[0]) == 1;
        _changed = true;
        return;
    }

    if (strcmp(address, "/show/record") == 0) {
        if (argc < 1) return;
        _recording = _argInt(args[0]) == 1;
        _changed = true;
        return;
    }

    // /device/buttons/<N>/colour
    if (strncmp(address, "/device/buttons/", 16) == 0) {
        if (argc < 1) return;
        char *end;
        long n = strtol(address + 16, &end, 10);
        if (strcmp(end, "/colour") != 0) return;
        if (n >= RCV_FIRST_INPUT_BUTTON && n < RCV_FIRST_INPUT_BUTTON + RCV_MAX_INPUTS) {
            _inputButtonColour[n - RCV_FIRST_INPUT_BUTTON] = (uint8_t)_argInt(args[0]);
            _changed = true;
        }
        return;
    }

    // /show/scene/<n>/<property>: a scene was edited on the device. Re-request the
    // show dump so the scene composition stays up to date.
    if (strncmp(address, "/show/scene/", 12) == 0) {
        _resyncRequested = true;
        return;
    }

    // A small /show dump that arrived as a regular packet is handled via XML streaming
    // already (the first 8 bytes always identify it), so nothing to do here.
}

// ---------- Streaming XML scanner for the /show dump ----------
//
// The dump can be 40+ kB, so it is never buffered. A small state machine walks
// through it and only looks at:
//   <RcvShow PgmScene=".." PvwScene=".." switching_mode="..">
//   <Scenes><Scene name=".."><VideoLayers><Layer source="videoInput" source_file="3" video_enabled="true"/>

void RcvParser::_xmlReset() {
    _xState = XS_TEXT;
    _xTagLen = 0;
    _xClosing = false;
    _xSelfClosing = false;
    _xElement = XE_OTHER;
    _xNameLen = 0;
    _xValueLen = 0;
    _xQuote = '"';
    _xInScenes = false;
    _xSceneNo = 0;
    _xLayerIsInput = false;
    _xLayerEnabled = true;
    _xLayerInput = 0;
    memset(_xSceneInputs, 0, sizeof(_xSceneInputs));
    memset(_xSceneNames, 0, sizeof(_xSceneNames));
}

void RcvParser::_xmlEnd() {
    // Only take over the scene layout from a complete dump that actually had scenes.
    if (_xSceneNo > 0) {
        memcpy(_sceneInputs, _xSceneInputs, sizeof(_sceneInputs));
        memcpy(_sceneNames, _xSceneNames, sizeof(_sceneNames));
        _scenesKnown = true;
        _changed = true;
    }
}

static bool isNameChar(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':' || c == '.';
}

static bool isSpace(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void RcvParser::_xmlByte(uint8_t c) {
    switch (_xState) {
        case XS_TEXT:
            if (c == '<') {
                _xState = XS_TAGNAME;
                _xTagLen = 0;
                _xClosing = false;
                _xSelfClosing = false;
            }
            break;

        case XS_TAGNAME:
            if (c == '/' && _xTagLen == 0) {
                _xClosing = true;
            } else if (isNameChar(c) || ((c == '?' || c == '!') && _xTagLen == 0)) {
                if (_xTagLen < sizeof(_xTag) - 1) {
                    _xTag[_xTagLen++] = (char)c;
                }
            } else {
                _xTag[_xTagLen] = '\0';
                _xmlTagStart();
                if (c == '>') {
                    _xmlTagEnd();
                    _xState = XS_TEXT;
                } else {
                    if (c == '/') _xSelfClosing = true;
                    _xState = XS_INTAG;
                }
            }
            break;

        case XS_INTAG:
            if (c == '>') {
                _xmlTagEnd();
                _xState = XS_TEXT;
            } else if (c == '/') {
                _xSelfClosing = true;
            } else if (isNameChar(c)) {
                _xName[0] = (char)c;
                _xNameLen = 1;
                _xState = XS_ATTRNAME;
            }
            break;

        case XS_ATTRNAME:
            if (isNameChar(c)) {
                if (_xNameLen < sizeof(_xName) - 1) {
                    _xName[_xNameLen++] = (char)c;
                }
            } else if (c == '=') {
                _xName[_xNameLen] = '\0';
                _xState = XS_QUOTE;
            } else if (c == '>') {
                _xmlTagEnd();
                _xState = XS_TEXT;
            } else if (!isSpace(c)) {
                _xState = XS_INTAG;
            }
            break;

        case XS_QUOTE:
            if (c == '"' || c == '\'') {
                _xQuote = (char)c;
                _xValueLen = 0;
                _xState = XS_VALUE;
            } else if (c == '>') {
                _xmlTagEnd();
                _xState = XS_TEXT;
            } else if (!isSpace(c)) {
                _xState = XS_INTAG;
            }
            break;

        case XS_VALUE:
            if (c == (uint8_t)_xQuote) {
                _xValue[_xValueLen] = '\0';
                _xmlAttribute(_xName, _xValue);
                _xState = XS_INTAG;
            } else if (_xValueLen < sizeof(_xValue) - 1) {
                _xValue[_xValueLen++] = (char)c;
            }
            break;
    }
}

void RcvParser::_xmlTagStart() {
    _xElement = XE_OTHER;

    if (_xClosing) {
        if (strcmp(_xTag, "Scenes") == 0) {
            _xInScenes = false;
        }
        return;
    }

    if (strcmp(_xTag, "RcvShow") == 0) {
        _xElement = XE_ROOT;
    } else if (strcmp(_xTag, "Scenes") == 0) {
        _xInScenes = true;
        _xSceneNo = 0;
    } else if (_xInScenes && strcmp(_xTag, "Scene") == 0) {
        if (_xSceneNo < 255) _xSceneNo++;
        _xElement = XE_SCENE;
    } else if (_xInScenes && strcmp(_xTag, "Layer") == 0) {
        _xElement = XE_LAYER;
        _xLayerIsInput = false;
        _xLayerEnabled = true;
        _xLayerInput = 0;
    }
}

void RcvParser::_xmlTagEnd() {
    if (_xElement == XE_LAYER && _xLayerIsInput && _xLayerEnabled &&
        _xSceneNo >= 1 && _xSceneNo <= RCV_MAX_SCENES &&
        _xLayerInput >= 1 && _xLayerInput <= RCV_MAX_INPUTS) {
        _xSceneInputs[_xSceneNo - 1] |= (uint8_t)(1 << (_xLayerInput - 1));
    }
    _xElement = XE_OTHER;
}

void RcvParser::_xmlAttribute(const char *name, const char *value) {
    switch (_xElement) {
        case XE_ROOT:
            if (strcmp(name, "PgmScene") == 0 || strcmp(name, "PvwScene") == 0) {
                int id = atoi(value) + (RCV_XML_SCENE_ZERO_BASED ? 1 : 0);
                RcvBus bus;
                bus.type = (id >= 1 && id <= RCV_MAX_SCENES) ? RCV_SRC_SCENE : RCV_SRC_NONE;
                bus.id = bus.type == RCV_SRC_NONE ? 0 : (uint8_t)id;
                if (name[1] == 'g') {
                    _xmlPgm = bus;
                } else {
                    _xmlPvw = bus;
                }
                _changed = true;
            } else if (strcmp(name, "switching_mode") == 0) {
                _studioMode = strcmp(value, "instant") != 0;
                _modeKnown = true;
                _changed = true;
            }
            break;

        case XE_SCENE:
            if (strcmp(name, "name") == 0 && _xSceneNo >= 1 && _xSceneNo <= RCV_MAX_SCENES) {
                strncpy(_xSceneNames[_xSceneNo - 1], value, sizeof(_xSceneNames[0]) - 1);
                _xSceneNames[_xSceneNo - 1][sizeof(_xSceneNames[0]) - 1] = '\0';
            }
            break;

        case XE_LAYER:
            if (strcmp(name, "source") == 0) {
                _xLayerIsInput = strcmp(value, "videoInput") == 0;
            } else if (strcmp(name, "source_file") == 0) {
                int n = atoi(value);
                _xLayerInput = (n >= 1 && n <= RCV_MAX_INPUTS) ? (uint8_t)n : 0;
            } else if (strcmp(name, "video_enabled") == 0) {
                _xLayerEnabled = strcmp(value, "false") != 0;
            }
            break;
    }
}

uint8_t RcvParser::sceneInputs(uint8_t sceneNo) const {
    if (!_scenesKnown || sceneNo < 1 || sceneNo > RCV_MAX_SCENES) {
        return 0;
    }
    return _sceneInputs[sceneNo - 1];
}

const char *RcvParser::sceneName(uint8_t sceneNo) const {
    if (sceneNo < 1 || sceneNo > RCV_MAX_SCENES) {
        return "";
    }
    return _sceneNames[sceneNo - 1];
}

// ---------- Tally state ----------

RcvBus RcvParser::program() const {
    if (_pgmKnown) {
        return _pgm;
    }
    // Startup hints until the first /show/pgmcurrent arrives:
    // 1. an input button lit as PGM, 2. the scene from the /show dump.
    for (uint8_t i = 0; i < RCV_MAX_INPUTS; i++) {
        if (_inputButtonColour[i] == RCV_BUTTON_COLOUR_PGM) {
            RcvBus bus = { RCV_SRC_INPUT, (uint8_t)(i + 1) };
            return bus;
        }
    }
    return _xmlPgm;
}

RcvBus RcvParser::preview() const {
    RcvBus none = { RCV_SRC_NONE, 0 };
    if (_modeKnown && !_studioMode) {
        return none; // instant mode has no preview bus
    }
    if (_pvwKnown) {
        return _pvw;
    }
    for (uint8_t i = 0; i < RCV_MAX_INPUTS; i++) {
        if (_inputButtonColour[i] == RCV_BUTTON_COLOUR_PVW) {
            RcvBus bus = { RCV_SRC_INPUT, (uint8_t)(i + 1) };
            return bus;
        }
    }
    return _xmlPvw;
}

bool RcvParser::_busContainsInput(const RcvBus &bus, uint8_t inputNo, const uint8_t *sceneInputMasks) {
    if (inputNo < 1 || inputNo > RCV_MAX_INPUTS) {
        return false;
    }
    if (bus.type == RCV_SRC_INPUT) {
        return bus.id == inputNo;
    }
    if (bus.type == RCV_SRC_SCENE && sceneInputMasks != NULL && bus.id >= 1 && bus.id <= RCV_MAX_SCENES) {
        return (sceneInputMasks[bus.id - 1] >> (inputNo - 1)) & 1;
    }
    return false;
}

uint8_t RcvParser::inputTallyFlags(uint8_t inputNo, const uint8_t *sceneInputMasks) const {
    uint8_t flags = 0;
    if (_busContainsInput(program(), inputNo, sceneInputMasks)) {
        flags |= RCV_TALLY_FLAG_PROGRAM;
    }
    if (_busContainsInput(preview(), inputNo, sceneInputMasks)) {
        flags |= RCV_TALLY_FLAG_PREVIEW;
    }
    return flags;
}

bool RcvParser::consumeResyncRequest() {
    bool r = _resyncRequested;
    _resyncRequested = false;
    return r;
}

bool RcvParser::consumeChanged() {
    bool c = _changed;
    _changed = false;
    return c;
}
