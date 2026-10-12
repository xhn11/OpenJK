#pragma once
// Snapshot frames of replicated entities: a ring of recent frames on each side of the link, a delta
// writer with a byte budget (sender) and the matching reader (receiver). Pure module (unit tested).
// Entity state is opaque here: three fixed-size blobs per entity (entityState_t, an optional
// playerState_t, coopEntExtra_t); sizes are multiples of 4 (coop_delta.h).
//
// blob
//   int32  frameNum
//   int32  baseFrameNum          -1: decode against nothing
//   int32  serverTime
//   int32  flags                 COOP_SNAPF_COMPLETE: nothing was deferred for lack of room
//   records, ascending entity number, only for entities that differ from the base frame:
//     uint16 num
//     uint8  bits                COOP_SNAPREC_REMOVED | COOP_SNAPREC_PS
//     es delta, [ps delta when COOP_SNAPREC_PS], ex delta      (absent when REMOVED; vs zeros when not in base)
//   uint16 COOP_SNAP_END
// The writer stores in its ring exactly what the receiver will have after this blob, so a change
// that did not fit is simply still pending against the next base.
#include <cstdint>

#define COOP_FRAME_BACKUP		32
#define COOP_SNAP_MAX_NUM		1024		// entity numbers 0..1023 (MAX_GENTITIES)
#define COOP_SNAP_MAX_PS		256			// entities with a playerState_t in one frame
#define COOP_SNAP_HEADER		16
#define COOP_SNAP_END			0xffff

#define COOP_SNAPF_COMPLETE		1
#define COOP_SNAPREC_REMOVED	1
#define COOP_SNAPREC_PS			2

enum { COOP_SNAP_OK = 0, COOP_SNAP_BAD = -1, COOP_SNAP_NOBASE = -2, COOP_SNAP_NOROOM = -3 };
enum { COOP_SNAPCHG_ADDED, COOP_SNAPCHG_CHANGED, COOP_SNAPCHG_REMOVED };

struct coopSnapFrame_t {
	int		frameNum;			// -1: slot unused
	int		serverTime;
	int		flags;
	int		firstEnt;			// absolute index of the first record in the entity pool
	int		numEnts;
	int		firstPs;			// absolute index of the first playerState slot
	int		numPs;
};

struct coopSnapRing_t {
	int				esSize, psSize, exSize;
	int				recSize;			// 8-byte header (num, playerState slot) + es + ex
	int				poolEnts, poolPs;
	int				scratchSize;		// one encoded record; decode buffers follow it
	uint8_t			*ents;
	uint8_t			*ps;
	uint8_t			*scratch;
	int				nextEnt;			// absolute index of the next record written
	int				nextPs;
	coopSnapFrame_t	frames[COOP_FRAME_BACKUP];
};

struct coopSnapInput_t {				// the sender's current state, ascending entity numbers
	int						count;
	const int				*nums;
	const uint8_t *const	*es;
	const uint8_t *const	*ps;		// nullptr entries: no playerState_t
	const uint8_t *const	*ex;
};

struct coopSnapWriteInfo_t {
	int		bytes;
	int		records;					// records written
	int		deferred;					// changes left for a later frame
	int		entities;					// entities in the stored frame
	int		base;						// base frame used, -1 = none
};

// es/ps/ex are nullptr for COOP_SNAPCHG_REMOVED, ps is nullptr for entities without a playerState_t
typedef void ( *coopSnapChangeFn )( void *ctx, int num, int change, const uint8_t *es, const uint8_t *ps, const uint8_t *ex );

bool					CoopSnap_Init( coopSnapRing_t *r, int esSize, int psSize, int exSize, int poolEnts, int poolPs );
void					CoopSnap_Shutdown( coopSnapRing_t *r );
void					CoopSnap_Clear( coopSnapRing_t *r );
bool					CoopSnap_HasFrame( const coopSnapRing_t *r, int frameNum );
const coopSnapFrame_t	*CoopSnap_Frame( const coopSnapRing_t *r, int frameNum );
int						CoopSnap_Write( coopSnapRing_t *r, int frameNum, int baseFrameNum, int serverTime,
							const coopSnapInput_t *in, uint8_t *out, int outMax, coopSnapWriteInfo_t *info );
int						CoopSnap_Read( coopSnapRing_t *r, const uint8_t *in, int inLen, int *frameNum );
int						CoopSnap_Diff( const coopSnapRing_t *r, int oldFrameNum, int newFrameNum, coopSnapChangeFn fn, void *ctx );
int						CoopSnap_ForEach( const coopSnapRing_t *r, int frameNum, coopSnapChangeFn fn, void *ctx );
