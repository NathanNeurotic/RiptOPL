#ifndef RIPTOPL_RA_FEATURES_H
#define RIPTOPL_RA_FEATURES_H

/* One RA release supports both clients. Cards are compiled into RA builds,
   but require the explicit, default-Off experimental cards setting, with
   either client. xeRAbora sends no title, so its card names the id.
   An idle GIF DMA channel does not prove ownership of the game's GS transfer
   state: cards may flicker or freeze games. This compile-time switch can still
   exclude the renderer path in developer builds; it is not the user setting. */
#ifndef RA_EXPERIMENTAL_CARD_DMA
#define RA_EXPERIMENTAL_CARD_DMA 1
#endif

/* The card's GIF packet: six header quadwords and 192x40 32-bit pixels,
   rounded to a cache line. The launcher reserves it in module storage. */
#define RA_CARD_PACKET_BYTES ((6 * 16 + 192 * 40 * 4 + 63) & ~63)

#endif
