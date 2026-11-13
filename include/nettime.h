#ifndef __NETTIME_H
#define __NETTIME_H

// General & System -> Date and Time -> Set from Internet (Launchstation issue 346 request).
enum {
    NETTIME_OK = 0,
    NETTIME_ERR_NETBUSY, // UDPBD or UDPFS owns the network adapter
    NETTIME_ERR_NETWORK, // no link or no address
    NETTIME_ERR_SERVER,  // no server answered with a usable Date header
};

// Parses an HTTP "Date: Fri, 13 Nov 2026 03:37:42 GMT" header out of a response header block.
// t receives year, month, day, hour, minute, second in UTC. Returns 1 when a date in 2000-2099
// was found.
int netTimeParseDate(const char *headers, int t[6]);

// Brings the network up if needed and reads the current UTC time from a web server's Date header
// over plain HTTP (the console clock may be too wrong for HTTPS). Every wait is bounded.
int netTimeFetch(int t[6]);

#endif
