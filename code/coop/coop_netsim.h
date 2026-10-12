#pragma once
// Simulated packet loss and latency for coop testing (coop_simLoss, coop_simLatency).
// Pure module: no engine headers (unit tested). net_ip.cpp owns one instance.

#define COOP_SIM_MAX_PACKETS	512
#define COOP_SIM_MAX_BYTES		1500		// a coop fragment is 1200 + 12 header bytes
#define COOP_SIM_ADDR_BYTES		32			// room for a netadr_t

struct coopSimPacket_t {
	int				releaseTime;
	int				length;
	unsigned char	data[COOP_SIM_MAX_BYTES];
	unsigned char	addr[COOP_SIM_ADDR_BYTES];
};

struct coopNetSim_t {
	coopSimPacket_t	queue[COOP_SIM_MAX_PACKETS];
	int				head;		// oldest queued packet
	int				count;
	unsigned		rng;
};

void					CoopSim_Init( coopNetSim_t *s, unsigned seed );	// empties the queue
bool					CoopSim_Drop( coopNetSim_t *s, int lossPercent );	// true: drop this packet
bool					CoopSim_Push( coopNetSim_t *s, int releaseTime, const void *data, int length, const void *addr, int addrSize );	// false: full or too big
const coopSimPacket_t	*CoopSim_Due( const coopNetSim_t *s, int now );	// oldest packet with releaseTime <= now, else nullptr
void					CoopSim_Pop( coopNetSim_t *s );
