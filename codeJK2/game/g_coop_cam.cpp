#include "g_headers.h"
#include "g_local.h"
#include "g_functions.h"
#include "g_coop.h"
#include "g_icarus.h"
#include "../icarus/interface.h"
#include "../cgame/cg_camera.h"

// Cutscenes (design 14, 21): the host's scripts drive the camera; every camera call also goes to the
// guest, whose cgame plays it in step (its clock follows the host's). Both players are locked like the
// SP player (in_camera); the host pulls every NPC home for the cutscene. Skipping needs both players'
// use key, or the host pressing it twice.

extern bool		in_camera;
extern camera_t	client_camera;
extern void		G_StartCinematicSkip( void );

#define COOP_SKIP_WINDOW_MS		10000
#define COOP_SKIP_DOUBLE_MS		1500
#define COOP_FAR_FROM_KYLE		1024.0f

static void G_CoopCamSend( const char *fmt, ... ) {
	if ( g_coopMode != COOP_MODE_HOST ) {
		return;
	}
	va_list	argptr;
	char	text[1024];
	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	G_CoopSend( "cam %s", text );
}

// ---------------------------------------------------------------- ICARUS camera calls (host)

static void G_CoopCamEnable( void ) {
	CGCam_Enable();
	G_CoopPullAllToHost( qtrue );
	G_CoopCamSend( "enable" );
}

static void G_CoopCamDisable( void ) {
	CGCam_Disable();
	G_CoopPullAllToHost( qfalse );
	// design 21: player 2 comes back where the script left its role (the stand-in), or next to Kyle
	const gentity_t *standIn = G_CoopStandIn();
	const gentity_t *spot = standIn ? standIn : &g_entities[0];
	G_CoopCamSend( "disable %.0f %.0f %.0f %d", spot->currentOrigin[0], spot->currentOrigin[1], spot->currentOrigin[2], standIn ? 1 : 0 );
}

static void G_CoopCamZoom( float fov, float duration ) {
	CGCam_Zoom( fov, duration );
	G_CoopCamSend( "zoom %g %g", fov, duration );
}

static void G_CoopCamMove( vec3_t dest, float duration ) {
	CGCam_Move( dest, duration );
	G_CoopCamSend( "move %g %g %g %g", dest[0], dest[1], dest[2], duration );
}

static void G_CoopCamPan( vec3_t dest, vec3_t dir, float duration ) {
	CGCam_Pan( dest, dir, duration );
	G_CoopCamSend( "pan %g %g %g %g %g %g %g", dest[0], dest[1], dest[2], dir[0], dir[1], dir[2], duration );
}

static void G_CoopCamRoll( float angle, float duration ) {
	CGCam_Roll( angle, duration );
	G_CoopCamSend( "roll %g %g", angle, duration );
}

static void G_CoopCamFollow( const char *name, float speed, float initLerp ) {
	CGCam_Follow( name, speed, initLerp );
	G_CoopCamSend( "follow \"%s\" %g %g", name ? name : "", speed, initLerp );
}

static void G_CoopCamTrack( const char *name, float speed, float initLerp ) {
	CGCam_Track( name, speed, initLerp );
	G_CoopCamSend( "track \"%s\" %g %g", name ? name : "", speed, initLerp );
}

static void G_CoopCamDistance( float dist, float initLerp ) {
	CGCam_Distance( dist, initLerp );
	G_CoopCamSend( "distance %g %g", dist, initLerp );
}

static void G_CoopCamFade( float sr, float sg, float sb, float sa, float dr, float dg, float db, float da, float duration ) {
	vec4_t src = { sr, sg, sb, sa }, dst = { dr, dg, db, da };
	CGCam_Fade( src, dst, duration );
	G_CoopCamSend( "fade %g %g %g %g %g %g %g %g %g", sr, sg, sb, sa, dr, dg, db, da, duration );
}

static void G_CoopCamPath( const char *name ) {
	CGCam_StartRoff( G_NewString( name ) );
	G_CoopCamSend( "path \"%s\"", name );
}

static void G_CoopCamShake( float intensity, int duration ) {
	CGCam_Shake( intensity, duration );
	G_CoopCamSend( "shake %g %d", intensity, duration );
}

void G_CoopHookCamera( interface_export_t *pe ) {
	pe->I_CameraEnable = G_CoopCamEnable;
	pe->I_CameraDisable = G_CoopCamDisable;
	pe->I_CameraZoom = G_CoopCamZoom;
	pe->I_CameraMove = G_CoopCamMove;
	pe->I_CameraPan = G_CoopCamPan;
	pe->I_CameraRoll = G_CoopCamRoll;
	pe->I_CameraTrack = G_CoopCamTrack;
	pe->I_CameraFollow = G_CoopCamFollow;
	pe->I_CameraDistance = G_CoopCamDistance;
	pe->I_CameraShake = G_CoopCamShake;
	pe->I_CameraFade = G_CoopCamFade;
	pe->I_CameraPath = G_CoopCamPath;
}

// camera groups live on entities the guest only has as replicas (Q3_SetCameraGroup and friends)
void G_CoopCamGroup( const gentity_t *ent, const char *group ) {
	if ( g_coopMode == COOP_MODE_HOST ) {
		G_CoopSend( "cam group %d \"%s\"", G_CoopWireNum( ent->s.number ), group ? group : "" );
	}
}

void G_CoopCamGroupSettings( void ) {
	G_CoopCamSend( "groupset %g \"%s\"", client_camera.cameraGroupZOfs, client_camera.cameraGroupTag );
}

// ---------------------------------------------------------------- guest: play the host's camera

static float G_CoopArgF( int i ) {
	return atof( gi.argv( i ) );
}

static void G_CoopCmd_Cam( void ) {
	if ( g_coopMode != COOP_MODE_GUEST ) {
		return;
	}
	const char *op = gi.argv( 2 );
	if ( !Q_stricmp( op, "enable" ) ) {
		CGCam_Enable();
	} else if ( !Q_stricmp( op, "disable" ) ) {
		CGCam_Disable();
		const vec3_t spot = { G_CoopArgF( 3 ), G_CoopArgF( 4 ), G_CoopArgF( 5 ) };
		const qboolean role = (qboolean)( atoi( gi.argv( 6 ) ) != 0 );
		gentity_t *self = &g_entities[0];
		if ( self->client && ( role || Distance( self->currentOrigin, spot ) > COOP_FAR_FROM_KYLE ) ) {
			G_CoopTeleportNear( self, spot );	// design 21: where the role ended, or back next to Kyle
		}
	} else if ( !Q_stricmp( op, "zoom" ) ) {
		CGCam_Zoom( G_CoopArgF( 3 ), G_CoopArgF( 4 ) );
	} else if ( !Q_stricmp( op, "move" ) ) {
		vec3_t dest = { G_CoopArgF( 3 ), G_CoopArgF( 4 ), G_CoopArgF( 5 ) };
		CGCam_Move( dest, G_CoopArgF( 6 ) );
	} else if ( !Q_stricmp( op, "pan" ) ) {
		vec3_t dest = { G_CoopArgF( 3 ), G_CoopArgF( 4 ), G_CoopArgF( 5 ) };
		vec3_t dir = { G_CoopArgF( 6 ), G_CoopArgF( 7 ), G_CoopArgF( 8 ) };
		CGCam_Pan( dest, dir, G_CoopArgF( 9 ) );
	} else if ( !Q_stricmp( op, "roll" ) ) {
		CGCam_Roll( G_CoopArgF( 3 ), G_CoopArgF( 4 ) );
	} else if ( !Q_stricmp( op, "follow" ) ) {
		CGCam_Follow( G_NewString( gi.argv( 3 ) ), G_CoopArgF( 4 ), G_CoopArgF( 5 ) );
	} else if ( !Q_stricmp( op, "track" ) ) {
		CGCam_Track( G_NewString( gi.argv( 3 ) ), G_CoopArgF( 4 ), G_CoopArgF( 5 ) );
	} else if ( !Q_stricmp( op, "distance" ) ) {
		CGCam_Distance( G_CoopArgF( 3 ), G_CoopArgF( 4 ) );
	} else if ( !Q_stricmp( op, "fade" ) ) {
		vec4_t src = { G_CoopArgF( 3 ), G_CoopArgF( 4 ), G_CoopArgF( 5 ), G_CoopArgF( 6 ) };
		vec4_t dst = { G_CoopArgF( 7 ), G_CoopArgF( 8 ), G_CoopArgF( 9 ), G_CoopArgF( 10 ) };
		CGCam_Fade( src, dst, G_CoopArgF( 11 ) );
	} else if ( !Q_stricmp( op, "path" ) ) {
		CGCam_StartRoff( G_NewString( gi.argv( 3 ) ) );
	} else if ( !Q_stricmp( op, "shake" ) ) {
		CGCam_Shake( G_CoopArgF( 3 ), atoi( gi.argv( 4 ) ) );
	} else if ( !Q_stricmp( op, "group" ) ) {
		const int num = atoi( gi.argv( 3 ) );
		if ( num >= 0 && num < ENTITYNUM_WORLD && g_entities[num].inuse ) {
			g_entities[num].cameraGroup = gi.argv( 4 )[0] ? G_NewString( gi.argv( 4 ) ) : NULL;
		}
	} else if ( !Q_stricmp( op, "groupset" ) ) {
		client_camera.cameraGroupZOfs = G_CoopArgF( 3 );
		Q_strncpyz( client_camera.cameraGroupTag, gi.argv( 4 ), sizeof( client_camera.cameraGroupTag ) );
	}
}

// ---------------------------------------------------------------- skipping (design 21)

static int g_coopSkipHost, g_coopSkipGuest;

static void G_CoopSkipNow( void ) {
	g_coopSkipHost = g_coopSkipGuest = 0;
	G_StartCinematicSkip();
}

// the local player pressed use in a cutscene: qtrue = skip now (solo, or both want it)
qboolean G_CoopCinematicSkipPressed( void ) {
	if ( !G_CoopPartnerInWorld() ) {
		return qtrue;
	}
	if ( g_coopMode == COOP_MODE_GUEST ) {
		G_CoopSend( "skipreq" );
		gi.SendServerCommand( 0, "cp \"Skip requested, waiting for the partner\"" );
		return qfalse;
	}
	const qboolean doublePress = (qboolean)( g_coopSkipHost && level.time - g_coopSkipHost < COOP_SKIP_DOUBLE_MS );
	const qboolean guestWants = (qboolean)( g_coopSkipGuest && level.time - g_coopSkipGuest < COOP_SKIP_WINDOW_MS );
	g_coopSkipHost = level.time;
	if ( doublePress || guestWants ) {
		g_coopSkipHost = g_coopSkipGuest = 0;
		return qtrue;
	}
	gi.SendServerCommand( 0, "cp \"Waiting for the partner (press again to skip)\"" );
	return qfalse;
}

static void G_CoopCmd_SkipRequest( void ) {
	if ( g_coopMode != COOP_MODE_HOST || !in_camera ) {
		return;
	}
	g_coopSkipGuest = level.time;
	if ( g_coopSkipHost && level.time - g_coopSkipHost < COOP_SKIP_WINDOW_MS ) {
		G_CoopSkipNow();
		return;
	}
	gi.SendServerCommand( 0, "cp \"The partner wants to skip the cutscene\"" );
}

void G_CoopCamCommand( const char *name ) {
	if ( !Q_stricmp( name, "cam" ) ) {
		G_CoopCmd_Cam();
	} else if ( !Q_stricmp( name, "skipreq" ) ) {
		G_CoopCmd_SkipRequest();
	}
}
