/*
    RCVclient - minimal TCP client for the RØDECaster Video, for tally lights.

    Connects to the RCV on TCP port 10024, requests the show state (/show) and
    remote mode (/remote) and then keeps track of program / preview through
    RcvParser. Non-blocking apart from the TCP connect itself (short timeout).

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef RCVclient_h
#define RCVclient_h

#include "Arduino.h"
#include "RcvParser.h"

#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

#define RCV_DEFAULT_PORT            10024
#define RCV_RECONNECT_INTERVAL      5000
#define RCV_CONNECT_TIMEOUT         1500
#define RCV_MAX_BYTES_PER_LOOP      2048
#define RCV_RESYNC_DELAY            1000    // coalesce bursts of scene edits into one /show request
#define RCV_RESYNC_QUIET_TIME       3000    // ignore resync triggers right after a request (they may be part of the reply)

class RCVclient {
public:
    RCVclient();

    void begin(IPAddress ip, uint16_t port = RCV_DEFAULT_PORT);
    void end();

    // Call all the time from loop()
    void runLoop();

    bool isConnected();

    // Program / preview state
    RcvParser &state() { return _parser; }

    uint32_t connectionAttempts() const { return _connectionAttempts; }

private:
    WiFiClient _client;
    RcvParser _parser;
    IPAddress _ip;
    uint16_t _port;
    bool _begun;
    bool _connected;
    unsigned long _lastAttempt;
    uint32_t _connectionAttempts;
    bool _resyncPending;
    unsigned long _resyncRequestedAt;
    unsigned long _lastRequestAt;

    void _connect();
    void _sendRequests();
    void _sendOsc(const char *address);
};

#endif
