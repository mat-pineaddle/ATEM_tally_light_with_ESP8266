/*
    RCVclient - see RCVclient.h

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "RCVclient.h"

RCVclient::RCVclient() :
    _port(RCV_DEFAULT_PORT),
    _begun(false),
    _connected(false),
    _lastAttempt(0),
    _connectionAttempts(0),
    _resyncPending(false),
    _resyncRequestedAt(0),
    _lastRequestAt(0) {
}

void RCVclient::begin(IPAddress ip, uint16_t port) {
    end();
    _ip = ip;
    _port = port;
    _begun = true;
    _lastAttempt = 0;
}

void RCVclient::end() {
    if (_client.connected()) {
        _client.stop();
    }
    _connected = false;
    _begun = false;
    _parser.reset();
}

bool RCVclient::isConnected() {
    return _connected && _client.connected();
}

void RCVclient::runLoop() {
    if (!_begun) {
        return;
    }

    if (!_client.connected()) {
        if (_connected) {
            _connected = false;
            _parser.reset();
            _client.stop();
        }
        if (_lastAttempt == 0 || millis() - _lastAttempt >= RCV_RECONNECT_INTERVAL) {
            _connect();
        }
        return;
    }

    uint8_t buf[256];
    int budget = RCV_MAX_BYTES_PER_LOOP;
    int available;
    while (budget > 0 && (available = _client.available()) > 0) {
        int toRead = available;
        if (toRead > (int)sizeof(buf)) toRead = sizeof(buf);
        if (toRead > budget) toRead = budget;
        int n = _client.read(buf, toRead);
        if (n <= 0) {
            break;
        }
        _parser.feed(buf, (size_t)n);
        budget -= n;
    }

    if (_parser.hasProtocolError()) {
        Serial.println("RCV: protocol error, reconnecting");
        _client.stop();
        _connected = false;
        _parser.reset();
        _lastAttempt = millis();
        return;
    }

    if (_parser.consumeResyncRequest() && millis() - _lastRequestAt >= RCV_RESYNC_QUIET_TIME) {
        _resyncPending = true;
        _resyncRequestedAt = millis();
    }
    if (_resyncPending && millis() - _resyncRequestedAt >= RCV_RESYNC_DELAY) {
        _resyncPending = false;
        _sendRequests();
    }
}

void RCVclient::_connect() {
    _lastAttempt = millis();
    if (_lastAttempt == 0) _lastAttempt = 1;
    _connectionAttempts++;
    _client.stop();
    _parser.reset();

#ifdef ESP32
    bool ok = _client.connect(_ip, _port, RCV_CONNECT_TIMEOUT);
#else
    _client.setTimeout(RCV_CONNECT_TIMEOUT);
    bool ok = _client.connect(_ip, _port);
#endif
    if (!ok) {
        return;
    }

    _client.setNoDelay(true);
    _connected = true;
    _resyncPending = false;
    _sendRequests();
}

void RCVclient::_sendRequests() {
    _lastRequestAt = millis();
    _sendOsc("/show");
    _sendOsc("/remote");
}

// Sends an OSC message without arguments, prefixed with a 4 byte little-endian length.
void RCVclient::_sendOsc(const char *address) {
    uint8_t packet[4 + 64 + 4];
    size_t addrLen = strlen(address);
    if (addrLen > 60) {
        return;
    }
    size_t addrPadded = (addrLen + 1 + 3) & ~(size_t)3;
    uint32_t msgLen = addrPadded + 4; // + ",\0\0\0"

    memset(packet, 0, sizeof(packet));
    packet[0] = msgLen & 0xff;
    packet[1] = (msgLen >> 8) & 0xff;
    packet[2] = (msgLen >> 16) & 0xff;
    packet[3] = (msgLen >> 24) & 0xff;
    memcpy(packet + 4, address, addrLen);
    packet[4 + addrPadded] = ',';

    _client.write(packet, 4 + msgLen);
}
