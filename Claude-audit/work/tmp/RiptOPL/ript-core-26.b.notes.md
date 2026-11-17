# ript-core-26.b notes (lens defects)

## Done
- all 6 files read fully (cuesupport.cpp re-read 1-1288 in attempt 2)

## Kept (writing findings)
- ethsupport.cpp:829-850 | 64 KB ShareEntry_t sharelist[128] on IO worker stack (96 KB), SIF DMA target via smbman_ra DMA_sendEE (smb_fio.c:871-889,1000), no cache writeback/invalidate, not 64-aligned
- ethsupport.cpp:601-630,686-709 | reconnectSmb clears reconnectQueued_/Requested_ at end; a settings reconnect requested during the run is dropped (guisettings.cpp:1053-1056)
- ethsupport.cpp:832-838,863-866 | share picker gate lets LINK_FAIL/DHCP_FAIL (310/311) through to GETSHARELIST; error replaced by LISTSHARES
- cuesupport.cpp:311-313,331-336 | argv pool check skipped on the folder (no discs.txt) path; ETH prefix + 150-160 char folder refused after deinit (elfldr_noreset.cpp:200-205)

## Dropped
- saveIconPs1Directory NULL root; single read() settings; cueResolveGameImage truncation (640 buffers); cueGameSizeMB (info path only, cached); ps1FillGameList peak mem; ps1DropTwins O(v*c) (rare, IO worker); cueScanDir per-folder opendir (documented design)
- needsUpdate stats (IO worker); smb2LogIopFree (diagLog is live feature); string copies (sizes match 32/32); pc_ip %u overflow (contained in struct, corrupt cfg only); fav_raw_t 141 KB (import path only, temporary)
