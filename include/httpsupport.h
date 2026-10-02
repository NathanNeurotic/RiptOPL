#ifndef __HTTP_SUPPORT_H
#define __HTTP_SUPPORT_H

#include "include/iosupport.h"
#include "include/opl.h"         // HTTP_BASE_PATH_MAX; u64 (tamtypes.h)
#include "include/supportbase.h" // base_game_info_t

// A network catalog, so the same slow-poll cadence SMB and UDPFS use.
constexpr int HTTP_MODE_UPDATE_DELAY = 300;

// Longest relative ISO path a catalog row may carry. Matches CATALOG_PATH_MAX in
// tools/pc/http/catalog_reference.py -- the two parsers are one contract and the fixtures under
// tools/pc/http/fixtures/ hold them to it.
constexpr int HTTP_CATALOG_PATH_MAX = 255;

// Bound on catalog size and row count. An over-limit catalog is REPORTED, never silently clipped:
// a library that is quietly missing its last hundred games is worse than one that says so.
#define HTTP_CATALOG_BYTES_MAX 262144
#define HTTP_CATALOG_ROWS_MAX  2048

// The server HTTP talks to: the four IP octets as ints, the port, and the normalised base path.
typedef struct
{
    int ip[4];
    int port;
    char base[HTTP_BASE_PATH_MAX];
} http_endpoint_t;

// HTTP's persistent state (src/sources/httpsupport.cpp): the configured server endpoint, the
// listed catalog, the refresh flag and the local prefix per-game data resolves through. One
// instance (gHttpSupport); HttpSource's virtuals and the httpXxx entry points below reach the
// state through it. Members are constant-initialised only, so no dynamic initialisation runs
// before main (EE rule). The catalog row type (http_catalog_extra_t), the catalog parser and the
// last-stream-error stay file-scope in the .cpp because tools/pc/http/tests/test_catalog_limits.py
// extracts the row type and httpParseCatalog out of that file into a host harness -- hence the
// opaque void *extras below.
class HttpSupport
{
public:
    // The server the source talks to, taken from the Network settings when the source starts.
    void setEndpoint(const int *ip, int port, const char *base);
    const http_endpoint_t *endpoint() const { return &endpoint_; }

    // The listed catalog. setCatalog frees the previous lists and publishes a freshly parsed pair;
    // clearCatalog drops them (source cleanUp/shutdown, and before a refetch replaces them).
    int gameCount() const { return gameCount_; }
    base_game_info_t *games() const { return games_; }
    void *extras() const { return extras_; } // really http_catalog_extra_t *, parallel to games()
    void setCatalog(base_game_info_t *games, void *extras, int count);
    void clearCatalog();

    int refreshPending() const { return refreshPending_; }
    void setRefreshPending(int pending) { refreshPending_ = pending; }

    // Built on first use: the LOCAL prefix cheats/cover art/watch list resolve through, since the
    // server is read-only and there is no http: filesystem.
    const char *localPrefix();

    // Opens a connection, issues one GET (ranged or not), and returns the socket with a live
    // stream. Returns <0 on failure, and sets the file-scope httpLastError to something the UI
    // can name.
    int openRequest(const http_endpoint_t *endpoint, const char *relative, int useRange, u64 from, u64 to,
                    int *contentLen, u64 *total, int *hasTotal);
    // Fetch the catalog into a freshly allocated buffer. Returns the byte count, or <0 on failure.
    // A partial transfer is a failure, never a shorter success: publishing half a library as if it
    // were whole is the failure mode that makes a missing game look like a deleted one.
    int fetchCatalog(const http_endpoint_t *endpoint, char **outBuf);

private:
    base_game_info_t *games_ = NULL;
    void *extras_ = NULL; // http_catalog_extra_t rows, parallel to games_ (the type lives in the .cpp)
    int gameCount_ = 0;
    int refreshPending_ = 1;
    char localPrefix_[256] = {};
    http_endpoint_t endpoint_ = {};
};

extern HttpSupport gHttpSupport;

item_list_t *httpGetObject(int initOnly);

// Normalise a user-entered base path in place: force a leading '/', drop any trailing one, collapse
// an empty string to "/". Called on load and after the settings dialog writes the value.
void httpNormalizeBasePath(char *path, size_t size);

// The Test server action. Fetches the catalog, parses it, and -- when there is a game to try --
// verifies one small byte range and the total size. Writes a human-readable outcome into msg.
void httpTestServer(const int *ip, int port, const char *base, char *msg, size_t msgSize);


#endif
