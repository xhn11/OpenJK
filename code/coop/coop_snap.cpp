#include "coop_snap.h"
#include "coop_delta.h"
#include <climits>
#include <cstdlib>
#include <cstring>

static void PutU16( uint8_t *p, int v ) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)( v >> 8 );
}

static int GetU16( const uint8_t *p ) {
	return p[0] | ( p[1] << 8 );
}

static void PutI32( uint8_t *p, int v ) {
	const uint32_t u = (uint32_t)v;
	p[0] = (uint8_t)u;
	p[1] = (uint8_t)( u >> 8 );
	p[2] = (uint8_t)( u >> 16 );
	p[3] = (uint8_t)( u >> 24 );
}

static int GetI32( const uint8_t *p ) {
	return (int)( (uint32_t)p[0] | ( (uint32_t)p[1] << 8 ) | ( (uint32_t)p[2] << 16 ) | ( (uint32_t)p[3] << 24 ) );
}

// pool record: int32 num, int32 playerState slot (absolute, -1 = none), es bytes, ex bytes
static uint8_t *Rec( const coopSnapRing_t *r, int abs ) {
	return r->ents + (size_t)( abs % r->poolEnts ) * r->recSize;
}

static int RecNum( const coopSnapRing_t *r, int abs ) {
	int n;
	memcpy( &n, Rec( r, abs ), 4 );
	return n;
}

static uint8_t *PsSlot( const coopSnapRing_t *r, int slot ) {
	return r->ps + (size_t)( slot % r->poolPs ) * r->psSize;
}

static const uint8_t *RecEs( const coopSnapRing_t *r, int abs ) {
	return Rec( r, abs ) + 8;
}

static const uint8_t *RecEx( const coopSnapRing_t *r, int abs ) {
	return Rec( r, abs ) + 8 + r->esSize;
}

static const uint8_t *RecPs( const coopSnapRing_t *r, int abs ) {
	int slot;
	memcpy( &slot, Rec( r, abs ) + 4, 4 );
	return slot >= 0 ? PsSlot( r, slot ) : nullptr;
}

struct coopSnapBuilder_t {
	coopSnapRing_t	*r;
	coopSnapFrame_t	f;
	bool			failed;
};

static void BeginFrame( coopSnapBuilder_t *b, coopSnapRing_t *r, int frameNum, int serverTime ) {
	b->r = r;
	b->failed = false;
	b->f.frameNum = frameNum;
	b->f.serverTime = serverTime;
	b->f.flags = 0;
	b->f.firstEnt = r->nextEnt;
	b->f.numEnts = 0;
	b->f.firstPs = r->nextPs;
	b->f.numPs = 0;
}

static void Append( coopSnapBuilder_t *b, int num, const uint8_t *es, const uint8_t *ps, const uint8_t *ex ) {
	coopSnapRing_t *r = b->r;
	if ( b->failed ) {
		return;
	}
	if ( b->f.numEnts >= COOP_SNAP_MAX_NUM || ( ps && b->f.numPs >= COOP_SNAP_MAX_PS ) ) {
		b->failed = true;
		return;
	}
	int slot = -1;
	if ( ps ) {
		slot = r->nextPs++;
		memmove( PsSlot( r, slot ), ps, r->psSize );
		b->f.numPs++;
	}
	uint8_t *rec = Rec( r, r->nextEnt++ );
	memcpy( rec, &num, 4 );
	memcpy( rec + 4, &slot, 4 );
	memmove( rec + 8, es, r->esSize );
	memmove( rec + 8 + r->esSize, ex, r->exSize );
	b->f.numEnts++;
}

bool CoopSnap_Init( coopSnapRing_t *r, int esSize, int psSize, int exSize, int poolEnts, int poolPs ) {
	memset( r, 0, sizeof( *r ) );
	if ( esSize <= 0 || psSize <= 0 || exSize <= 0 || esSize % 4 || psSize % 4 || exSize % 4
		|| poolEnts < 2 * COOP_SNAP_MAX_NUM || poolPs < 2 * COOP_SNAP_MAX_PS ) {
		return false;
	}
	r->esSize = esSize;
	r->psSize = psSize;
	r->exSize = exSize;
	r->recSize = 8 + esSize + exSize;
	r->poolEnts = poolEnts;
	r->poolPs = poolPs;
	r->scratchSize = 3 + CoopDelta_MaxSize( esSize ) + CoopDelta_MaxSize( psSize ) + CoopDelta_MaxSize( exSize );
	r->ents = (uint8_t *)malloc( (size_t)poolEnts * r->recSize );
	r->ps = (uint8_t *)malloc( (size_t)poolPs * psSize );
	r->scratch = (uint8_t *)malloc( (size_t)r->scratchSize + esSize + psSize + exSize );
	if ( !r->ents || !r->ps || !r->scratch ) {
		CoopSnap_Shutdown( r );
		return false;
	}
	CoopSnap_Clear( r );
	return true;
}

void CoopSnap_Shutdown( coopSnapRing_t *r ) {
	free( r->ents );
	free( r->ps );
	free( r->scratch );
	memset( r, 0, sizeof( *r ) );
}

void CoopSnap_Clear( coopSnapRing_t *r ) {
	for ( int i = 0; i < COOP_FRAME_BACKUP; i++ ) {
		r->frames[i].frameNum = -1;
	}
}

bool CoopSnap_HasFrame( const coopSnapRing_t *r, int frameNum ) {
	if ( !r->ents || frameNum < 0 ) {
		return false;
	}
	const coopSnapFrame_t *f = &r->frames[frameNum % COOP_FRAME_BACKUP];
	if ( f->frameNum != frameNum ) {
		return false;
	}
	// a frame stays readable while writing one more full frame cannot reach its records
	if ( f->firstEnt < r->nextEnt + COOP_SNAP_MAX_NUM - r->poolEnts ) {
		return false;
	}
	if ( f->numPs && f->firstPs < r->nextPs + COOP_SNAP_MAX_PS - r->poolPs ) {
		return false;
	}
	return true;
}

const coopSnapFrame_t *CoopSnap_Frame( const coopSnapRing_t *r, int frameNum ) {
	return CoopSnap_HasFrame( r, frameNum ) ? &r->frames[frameNum % COOP_FRAME_BACKUP] : nullptr;
}

int CoopSnap_Write( coopSnapRing_t *r, int frameNum, int baseFrameNum, int serverTime,
	const coopSnapInput_t *in, uint8_t *out, int outMax, coopSnapWriteInfo_t *info ) {
	coopSnapWriteInfo_t unused;
	if ( !info ) {
		info = &unused;
	}
	memset( info, 0, sizeof( *info ) );
	info->base = -1;
	if ( !r->ents || frameNum < 0 ) {
		return COOP_SNAP_BAD;
	}
	if ( outMax < COOP_SNAP_HEADER + 2 ) {
		return COOP_SNAP_NOROOM;
	}
	coopSnapFrame_t base = {};
	bool haveBase = false;
	if ( baseFrameNum >= 0 && baseFrameNum < frameNum && frameNum - baseFrameNum < COOP_FRAME_BACKUP
		&& CoopSnap_HasFrame( r, baseFrameNum ) ) {
		base = r->frames[baseFrameNum % COOP_FRAME_BACKUP];
		haveBase = true;
		info->base = baseFrameNum;
	}
	PutI32( out, frameNum );
	PutI32( out + 4, haveBase ? baseFrameNum : -1 );
	PutI32( out + 8, serverTime );
	PutI32( out + 12, 0 );
	int pos = COOP_SNAP_HEADER;
	const int limit = outMax - 2;	// the end marker always fits
	coopSnapBuilder_t b;
	BeginFrame( &b, r, frameNum, serverTime );
	const int bn = haveBase ? base.numEnts : 0;
	int bi = 0, ii = 0, prev = -1;
	while ( bi < bn || ii < in->count ) {
		const int bnum = bi < bn ? RecNum( r, base.firstEnt + bi ) : INT_MAX;
		int inum = INT_MAX;
		if ( ii < in->count ) {
			inum = in->nums[ii];
			if ( inum < 0 || inum >= COOP_SNAP_MAX_NUM || inum <= prev ) {
				return COOP_SNAP_BAD;
			}
		}
		if ( bnum < inum ) {
			// the sender no longer has it
			const int abs = base.firstEnt + bi++;
			if ( pos + 3 <= limit ) {
				PutU16( out + pos, bnum );
				out[pos + 2] = COOP_SNAPREC_REMOVED;
				pos += 3;
				info->records++;
			} else {
				Append( &b, bnum, RecEs( r, abs ), RecPs( r, abs ), RecEx( r, abs ) );	// the receiver keeps it for now
				info->deferred++;
			}
			continue;
		}
		prev = inum;
		const uint8_t *bes = nullptr, *bps = nullptr, *bex = nullptr;
		if ( bnum == inum ) {
			const int abs = base.firstEnt + bi++;
			bes = RecEs( r, abs );
			bps = RecPs( r, abs );
			bex = RecEx( r, abs );
		}
		const uint8_t *nes = in->es[ii], *nps = in->ps[ii], *nex = in->ex[ii];
		ii++;
		if ( bes && ( nps == nullptr ) == ( bps == nullptr ) && !memcmp( bes, nes, r->esSize ) && !memcmp( bex, nex, r->exSize )
			&& ( !nps || !memcmp( bps, nps, r->psSize ) ) ) {
			Append( &b, inum, nes, nps, nex );	// unchanged: no record
			continue;
		}
		uint8_t *rec = r->scratch;
		PutU16( rec, inum );
		rec[2] = nps ? COOP_SNAPREC_PS : 0;
		int len = 3;
		int n = CoopDelta_Encode( bes, nes, r->esSize, rec + len, r->scratchSize - len );
		if ( n < 0 ) {
			return COOP_SNAP_BAD;
		}
		len += n;
		if ( nps ) {
			n = CoopDelta_Encode( bps, nps, r->psSize, rec + len, r->scratchSize - len );
			if ( n < 0 ) {
				return COOP_SNAP_BAD;
			}
			len += n;
		}
		n = CoopDelta_Encode( bex, nex, r->exSize, rec + len, r->scratchSize - len );
		if ( n < 0 ) {
			return COOP_SNAP_BAD;
		}
		len += n;
		if ( pos + len <= limit ) {
			memcpy( out + pos, rec, len );
			pos += len;
			info->records++;
			Append( &b, inum, nes, nps, nex );
		} else {
			info->deferred++;
			if ( bes ) {
				Append( &b, inum, bes, bps, bex );	// the receiver keeps the base state
			}
		}
	}
	if ( b.failed ) {
		return COOP_SNAP_BAD;
	}
	PutU16( out + pos, COOP_SNAP_END );
	pos += 2;
	b.f.flags = info->deferred ? 0 : COOP_SNAPF_COMPLETE;
	PutI32( out + 12, b.f.flags );
	r->frames[frameNum % COOP_FRAME_BACKUP] = b.f;
	info->bytes = pos;
	info->entities = b.f.numEnts;
	return pos;
}

int CoopSnap_Read( coopSnapRing_t *r, const uint8_t *in, int inLen, int *frameNumOut ) {
	if ( !r->ents || inLen < COOP_SNAP_HEADER + 2 ) {
		return COOP_SNAP_BAD;
	}
	const int frameNum = GetI32( in ), baseNum = GetI32( in + 4 ), serverTime = GetI32( in + 8 ), flags = GetI32( in + 12 );
	if ( frameNum < 0 || baseNum < -1 ) {
		return COOP_SNAP_BAD;
	}
	coopSnapFrame_t base = {};
	bool haveBase = false;
	if ( baseNum >= 0 ) {
		if ( baseNum >= frameNum || frameNum - baseNum >= COOP_FRAME_BACKUP || !CoopSnap_HasFrame( r, baseNum ) ) {
			return COOP_SNAP_NOBASE;
		}
		base = r->frames[baseNum % COOP_FRAME_BACKUP];
		haveBase = true;
	}
	uint8_t *tes = r->scratch + r->scratchSize, *tps = tes + r->esSize, *tex = tps + r->psSize;
	coopSnapBuilder_t b;
	BeginFrame( &b, r, frameNum, serverTime );
	const int bn = haveBase ? base.numEnts : 0;
	int bi = 0, pos = COOP_SNAP_HEADER, prev = -1;
	for ( ;; ) {
		if ( pos + 2 > inLen ) {
			return COOP_SNAP_BAD;
		}
		const int num = GetU16( in + pos );
		if ( num == COOP_SNAP_END ) {
			pos += 2;
			break;
		}
		if ( num >= COOP_SNAP_MAX_NUM || num <= prev || pos + 3 > inLen ) {
			return COOP_SNAP_BAD;
		}
		const int bits = in[pos + 2];
		pos += 3;
		prev = num;
		if ( bits & ~( COOP_SNAPREC_REMOVED | COOP_SNAPREC_PS ) ) {
			return COOP_SNAP_BAD;
		}
		while ( bi < bn && RecNum( r, base.firstEnt + bi ) < num ) {
			const int abs = base.firstEnt + bi++;
			Append( &b, RecNum( r, abs ), RecEs( r, abs ), RecPs( r, abs ), RecEx( r, abs ) );
		}
		const uint8_t *bes = nullptr, *bps = nullptr, *bex = nullptr;
		const bool inBase = bi < bn && RecNum( r, base.firstEnt + bi ) == num;
		if ( inBase ) {
			const int abs = base.firstEnt + bi++;
			bes = RecEs( r, abs );
			bps = RecPs( r, abs );
			bex = RecEx( r, abs );
		}
		if ( bits & COOP_SNAPREC_REMOVED ) {
			if ( !inBase || ( bits & COOP_SNAPREC_PS ) ) {
				return COOP_SNAP_BAD;
			}
			continue;
		}
		int n = CoopDelta_Decode( bes, tes, r->esSize, in + pos, inLen - pos );
		if ( n < 0 ) {
			return COOP_SNAP_BAD;
		}
		pos += n;
		if ( bits & COOP_SNAPREC_PS ) {
			n = CoopDelta_Decode( bps, tps, r->psSize, in + pos, inLen - pos );
			if ( n < 0 ) {
				return COOP_SNAP_BAD;
			}
			pos += n;
		}
		n = CoopDelta_Decode( bex, tex, r->exSize, in + pos, inLen - pos );
		if ( n < 0 ) {
			return COOP_SNAP_BAD;
		}
		pos += n;
		Append( &b, num, tes, ( bits & COOP_SNAPREC_PS ) ? tps : nullptr, tex );
	}
	while ( bi < bn ) {
		const int abs = base.firstEnt + bi++;
		Append( &b, RecNum( r, abs ), RecEs( r, abs ), RecPs( r, abs ), RecEx( r, abs ) );
	}
	if ( pos != inLen || b.failed ) {
		return COOP_SNAP_BAD;
	}
	b.f.flags = flags & COOP_SNAPF_COMPLETE;
	r->frames[frameNum % COOP_FRAME_BACKUP] = b.f;
	if ( frameNumOut ) {
		*frameNumOut = frameNum;
	}
	return COOP_SNAP_OK;
}

int CoopSnap_Diff( const coopSnapRing_t *r, int oldFrameNum, int newFrameNum, coopSnapChangeFn fn, void *ctx ) {
	if ( !CoopSnap_HasFrame( r, newFrameNum ) ) {
		return -1;
	}
	if ( oldFrameNum == newFrameNum ) {
		return 0;
	}
	const coopSnapFrame_t nf = r->frames[newFrameNum % COOP_FRAME_BACKUP];
	const bool haveOld = CoopSnap_HasFrame( r, oldFrameNum );
	coopSnapFrame_t of = {};
	if ( haveOld ) {
		of = r->frames[oldFrameNum % COOP_FRAME_BACKUP];
	}
	const int on = haveOld ? of.numEnts : 0;
	int oi = 0, ni = 0;
	while ( oi < on || ni < nf.numEnts ) {
		const int onum = oi < on ? RecNum( r, of.firstEnt + oi ) : INT_MAX;
		const int nnum = ni < nf.numEnts ? RecNum( r, nf.firstEnt + ni ) : INT_MAX;
		if ( onum < nnum ) {
			fn( ctx, onum, COOP_SNAPCHG_REMOVED, nullptr, nullptr, nullptr );
			oi++;
			continue;
		}
		const int na = nf.firstEnt + ni++;
		if ( onum > nnum ) {
			fn( ctx, nnum, COOP_SNAPCHG_ADDED, RecEs( r, na ), RecPs( r, na ), RecEx( r, na ) );
			continue;
		}
		const int oa = of.firstEnt + oi++;
		const uint8_t *ops = RecPs( r, oa ), *nps = RecPs( r, na );
		if ( ( ops == nullptr ) != ( nps == nullptr ) || memcmp( RecEs( r, oa ), RecEs( r, na ), r->esSize )
			|| memcmp( RecEx( r, oa ), RecEx( r, na ), r->exSize ) || ( nps && memcmp( ops, nps, r->psSize ) ) ) {
			fn( ctx, nnum, COOP_SNAPCHG_CHANGED, RecEs( r, na ), nps, RecEx( r, na ) );
		}
	}
	return haveOld ? 0 : 1;
}

int CoopSnap_ForEach( const coopSnapRing_t *r, int frameNum, coopSnapChangeFn fn, void *ctx ) {
	if ( !CoopSnap_HasFrame( r, frameNum ) ) {
		return -1;
	}
	const coopSnapFrame_t f = r->frames[frameNum % COOP_FRAME_BACKUP];
	for ( int i = 0; i < f.numEnts; i++ ) {
		const int abs = f.firstEnt + i;
		fn( ctx, RecNum( r, abs ), COOP_SNAPCHG_ADDED, RecEs( r, abs ), RecPs( r, abs ), RecEx( r, abs ) );
	}
	return f.numEnts;
}
