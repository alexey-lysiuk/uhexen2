#ifndef	_LOKI_XDELTA_H
#define	_LOKI_XDELTA_H

/* XDelta is linked in, for space reasons .. I wish it didn't use glib.. */
extern int loki_xdelta(const char *old, const char *new, const char *out);
extern int loki_xpatch(const char *pat, const char *old, const char *out);

#endif	/* _LOKI_XDELTA_H */

