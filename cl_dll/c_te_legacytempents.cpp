//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "model_types.h"
#include "view_shared.h"
#include "iviewrender.h"
#include "tempentity.h"
#include "dlight.h"
#include "tempent.h"
#include "c_te_legacytempents.h"
#include "clientsideeffects.h"
#include "cl_animevent.h"
#include "iefx.h"
#include "engine/IEngineSound.h"
#include "env_wind_shared.h"
#include "ClientEffectPrecacheSystem.h"
#include "fx_sparks.h"
#include "fx.h"
#include "movevars_shared.h"
#include "engine/ivmodelinfo.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "view.h"
#include "tier0/vprof.h"
#include "particles_localspace.h"
#include "physpropclientside.h"
#include "c_te_effect_dispatch.h"	// |-- Mirv: Needed for client nail

// NOTE: Always include this last!
#include "tier0/memdbgon.h"

extern ConVar muzzleflash_light;

#define TENT_WIND_ACCEL 50

// |-- Mirv: FC request
static ConVar cl_brasstime("cl_brasstime", "10.0");

//Precahce the effects
CLIENTEFFECT_REGISTER_BEGIN( PrecacheEffectMuzzleFlash )
CLIENTEFFECT_MATERIAL( "sprites/ar2_muzzle1" )
CLIENTEFFECT_MATERIAL( "sprites/ar2_muzzle3" )
CLIENTEFFECT_MATERIAL( "sprites/ar2_muzzle4" )
CLIENTEFFECT_MATERIAL( "sprites/muzzleflash4" )
CLIENTEFFECT_MATERIAL( "particle/fire" )

CLIENTEFFECT_MATERIAL( "effects/combinemuzzle1_noz" )
CLIENTEFFECT_MATERIAL( "effects/combinemuzzle2_noz" )

CLIENTEFFECT_MATERIAL( "effects/muzzleflash1_noz" )
CLIENTEFFECT_MATERIAL( "effects/muzzleflash2_noz" )
CLIENTEFFECT_MATERIAL( "effects/muzzleflash3_noz" )
CLIENTEFFECT_MATERIAL( "effects/muzzleflash4_noz" )

CLIENTEFFECT_REGISTER_END()

//Whether or not to eject brass from weapons
ConVar cl_ejectbrass( "cl_ejectbrass", "1" );

ConVar ffdev_disablemuzzleflashes("ffdev_disablemuzzleflashes", "0");

ConVar func_break_max_pieces( "func_break_max_pieces", "15", FCVAR_ARCHIVE | FCVAR_REPLICATED );


#if !defined( HL1_CLIENT_DLL )		// HL1 implements a derivative of CTempEnts
// Temp entity interface
static CTempEnts g_TempEnts;
// Expose to rest of the client .dll
ITempEnts *tempents = ( ITempEnts * )&g_TempEnts;
#endif




C_LocalTempEntity::C_LocalTempEntity()
{
#ifdef _DEBUG
	tentOffset.Init();
	m_vecTempEntVelocity.Init();
	m_vecTempEntAngVelocity.Init();
	m_vecNormal.Init();
#endif
}


#if defined( CSTRIKE_DLL ) || defined (FF_DLL )

#define TE_RIFLE_SHELL 1024
#define TE_PISTOL_SHELL 2048
#define TE_SHOTGUN_SHELL 4096

#endif

//-----------------------------------------------------------------------------
// Purpose: Prepare a temp entity for creation
// Input  : time - 
//			*model - 
//-----------------------------------------------------------------------------
void C_LocalTempEntity::Prepare( model_t *pmodel, float time )
{
	Interp_SetupMappings( GetVarMapping() );

	index = -1;
	Clear();

	// Use these to set per-frame and termination conditions / actions
	flags = FTENT_NONE;		
	die = time + 0.75;
	SetModelPointer( pmodel );
	SetRenderMode( kRenderNormal );
	m_nRenderFX = kRenderFxNone;
	m_nBody = 0;
	m_nSkin = 0;
	fadeSpeed = 0.5;
	hitSound = 0;
	clientIndex = -1;
	bounceFactor = 1;
	m_nFlickerFrame = 0;
}

//-----------------------------------------------------------------------------
// Sets the velocity
//-----------------------------------------------------------------------------
void C_LocalTempEntity::SetVelocity( const Vector &vecVelocity )
{
	m_vecTempEntVelocity = vecVelocity;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int C_LocalTempEntity::DrawStudioModel( int flags )
{
	VPROF_BUDGET( "C_LocalTempEntity::DrawStudioModel", VPROF_BUDGETGROUP_MODEL_RENDERING );
	int drawn = 0;

	if ( !GetModel() || modelinfo->GetModelType( GetModel() ) != mod_studio )
		return drawn;
	
	// Make sure m_pstudiohdr is valid for drawing
	if ( !GetModelPtr() )
		return drawn;

	drawn = modelrender->DrawModel( 
		flags, 
		this,
		MODEL_INSTANCE_INVALID,
		index, 
		GetModel(),
		GetAbsOrigin(),
		GetAbsAngles(),
		m_nSkin,
		m_nBody,
		m_nHitboxSet );
	return drawn;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flags - 
//-----------------------------------------------------------------------------
int	C_LocalTempEntity::DrawModel( int flags )
{
	int drawn = 0;

	if ( !GetModel() )
	{
		return drawn;
	}

	if ( this->flags & FTENT_BEOCCLUDED )
	{
		// Check normal
		Vector vecDelta = (GetAbsOrigin() - MainViewOrigin());
		VectorNormalize( vecDelta );
		float flDot = DotProduct( m_vecNormal, vecDelta );
		if ( flDot > 0 )
		{
			float flAlpha = RemapVal( min(flDot,0.3), 0, 0.3, 0, 1 );
			flAlpha = max( 1.0, tempent_renderamt - (tempent_renderamt * flAlpha) );
			SetRenderColorA( flAlpha );
		}
	}

	switch ( modelinfo->GetModelType( GetModel() ) )
	{
	case mod_sprite:
		drawn = DrawSprite( 
			this,
			GetModel(), 
			GetAbsOrigin(), 
			GetAbsAngles(), 
			m_flFrame,  // sprite frame to render
			m_nBody > 0 ? cl_entitylist->GetBaseEntity( m_nBody ) : NULL,  // attach to
			m_nSkin,  // attachment point
			GetRenderMode(), // rendermode
			m_nRenderFX, // renderfx
			m_clrRender->a, // alpha
			m_clrRender->r,
			m_clrRender->g,
			m_clrRender->b,
			m_flSpriteScale		  // sprite scale
			);
		break;
	case mod_studio:
		drawn = DrawStudioModel( flags );
		break;
	default:
		break;
	}

	return drawn;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_LocalTempEntity::IsActive( void )
{
	bool active = true;

	float life = die - gpGlobals->curtime;
	
	if ( life < 0 )
	{
		if ( flags & FTENT_FADEOUT )
		{
			int alpha;
			if (GetRenderMode() == kRenderNormal)
			{
				SetRenderMode( kRenderTransTexture );
			}
			
			alpha = tempent_renderamt * ( 1 + life * fadeSpeed );
			
			if ( alpha <= 0 )
			{
				active = false;
				alpha = 0;
			}

			SetRenderColorA( alpha );
		}
		else 
		{
			active = false;
		}
	}

	// Never die tempents only die when their die is cleared
	if ( flags & FTENT_NEVERDIE )
	{
		active = (die != 0);
	}

	return active;
}

bool C_LocalTempEntity::Frame( float frametime, int framenumber )
{
	float fastFreq = gpGlobals->curtime * 5.5;
	float gravity = -frametime * sv_gravity.GetFloat();
	float gravitySlow = gravity * 0.5;
	float traceFraction = 1;

	Assert( !GetMoveParent() );

	m_vecPrevLocalOrigin = GetLocalOrigin();

	// --> Mirv: FF Projectiles like nails and darts
	if (flags & FTENT_FFPROJECTILE)
	{
		// Start off by advancing the projectile
		SetLocalOrigin( GetLocalOrigin() + m_vecTempEntVelocity * frametime );

		// Stationary, fade out
		if (m_vecTempEntVelocity == vec3_origin)
		{
			int alpha = 255 * (die - gpGlobals->curtime);

			// Starting to fade out now, ensure that it's the correct rendermode for this
			if (alpha < 255)
			{
				if (GetRenderMode() == kRenderNormal)
					SetRenderMode(kRenderTransTexture);

				SetRenderColorA(max(alpha, 0));
			}
			
			return true;
		}

		// Trace along the change in positon that we have just simulated
		trace_t pm;
		UTIL_TraceLine(m_vecPrevLocalOrigin, GetLocalOrigin(), MASK_SOLID, NULL, COLLISION_GROUP_PROJECTILE, &pm);

		// Make sure it didn't bump into itself... (?!?)
		if ((pm.fraction != 1) && ((pm.DidHitWorld()) || (pm.m_pEnt != ClientEntityList().GetEnt(clientIndex))))
		{
			// Are we going through breakable glass? That won't stop us.
			if (pm.m_pEnt->GetCollisionGroup() == COLLISION_GROUP_BREAKABLE_GLASS)
				return true;

			// Place at contact point
			Vector newOrigin;
			VectorMA(m_vecPrevLocalOrigin, pm.fraction * frametime, m_vecTempEntVelocity, newOrigin);
			SetLocalOrigin(newOrigin);

			// Play an impact effect if this isn't a player who'll bleed
			// Possibly we should do blood here too.
			// TODO: We can reduce the changes of an effect here.
			if (!pm.m_pEnt->IsPlayer())
			{
				CEffectData	data;
				data.m_vStart = m_vecPrevLocalOrigin;
				data.m_vOrigin = newOrigin;
				data.m_vNormal = pm.plane.normal;
				data.m_nSurfaceProp = pm.surface.surfaceProps;
				data.m_nEntIndex = pm.GetEntityIndex();

				// TODO: Add the rest of these as needed
//				if (pModel == m_pFF_Nail)
					DispatchEffect("NailImpact", data);
//				else if (pModel == m_pFF_Dart)
//					DispatchEffect("DartImpact", data);
			}

			// Remove straight away if its something that is movable or is the skybox
			if (pm.m_pEnt->GetMoveType() != MOVETYPE_NONE || (pm.surface.flags & SURF_SKY))
				die = gpGlobals->curtime;

			// TODO: Remove straight away if angle with normal is too big?

			// Otherwise jam into whatever we hit for a bit
			// TODO: A cvar to disable jamming
			else
			{
				m_vecTempEntVelocity = Vector(0, 0, 0);
				
				die = gpGlobals->curtime + 4.0f;
				flags &= ~FTENT_FADEOUT;
			}
		}

		return true;
	}
	// <-- Mirv

	if ( flags & FTENT_PLYRATTACHMENT )
	{
		if ( IClientEntity *pClient = cl_entitylist->GetClientEntity( clientIndex ) )
		{
			SetLocalOrigin( pClient->GetAbsOrigin() + tentOffset );
		}
	}
	else if ( flags & FTENT_SINEWAVE )
	{
		x += m_vecTempEntVelocity[0] * frametime;
		y += m_vecTempEntVelocity[1] * frametime;

		SetLocalOrigin( Vector(
			x + sin( m_vecTempEntVelocity[2] + gpGlobals->curtime /* * anim.prevframe */ ) * (10*m_flSpriteScale),
			y + sin( m_vecTempEntVelocity[2] + fastFreq + 0.7 ) * (8*m_flSpriteScale),
			GetLocalOriginDim( Z_INDEX ) + m_vecTempEntVelocity[2] * frametime ) );
	}
	else if ( flags & FTENT_SPIRAL )
	{
		float s, c;
		SinCos( m_vecTempEntVelocity[2] + fastFreq, &s, &c );

		SetLocalOrigin( GetLocalOrigin() + Vector(
			m_vecTempEntVelocity[0] * frametime + 8 * sin( gpGlobals->curtime * 20 ),
			m_vecTempEntVelocity[1] * frametime + 4 * sin( gpGlobals->curtime * 30 ),
			m_vecTempEntVelocity[2] * frametime ) );
	}
	else
	{
		SetLocalOrigin( GetLocalOrigin() + m_vecTempEntVelocity * frametime );
	}
	
	if ( flags & FTENT_SPRANIMATE )
	{
		m_flFrame += frametime * m_flFrameRate;
		if ( m_flFrame >= m_flFrameMax )
		{
			m_flFrame = m_flFrame - (int)(m_flFrame);

			if ( !(flags & FTENT_SPRANIMATELOOP) )
			{
				// this animating sprite isn't set to loop, so destroy it.
				die = 0.0f;
				return false;
			}
		}
	}
	else if ( flags & FTENT_SPRCYCLE )
	{
		m_flFrame += frametime * 10;
		if ( m_flFrame >= m_flFrameMax )
		{
			m_flFrame = m_flFrame - (int)(m_flFrame);
		}
	}

	if ( flags & FTENT_SMOKEGROWANDFADE )
	{
		m_flSpriteScale += frametime * 0.5f;
		//m_clrRender.a -= frametime * 1500;
	}

	if ( flags & FTENT_ROTATE )
	{
		SetLocalAngles( GetLocalAngles() + m_vecTempEntAngVelocity * frametime );
	}

	if ( flags & (FTENT_COLLIDEALL | FTENT_COLLIDEWORLD) )
	{
		Vector	traceNormal;
		
		traceNormal.Init();

		if ( flags & FTENT_COLLIDEALL )
		{
			// If the FTENT_COLLISIONGROUP flag is set, use the entity's collision group
			int collisionGroup = COLLISION_GROUP_NONE;
			if ( flags & FTENT_COLLISIONGROUP )
			{
				collisionGroup = GetCollisionGroup();
			}

			trace_t pm;		
			UTIL_TraceLine( m_vecPrevLocalOrigin, GetLocalOrigin(), MASK_SOLID, NULL, collisionGroup, &pm );

			// Make sure it didn't bump into itself... (?!?)
			if  ( 
				(pm.fraction != 1) && 
					( (pm.DidHitWorld()) || 
					  (pm.m_pEnt != ClientEntityList().GetEnt(clientIndex)) ) 
				)
			{
				traceFraction = pm.fraction;
				VectorCopy( pm.plane.normal, traceNormal );
			}
		}
		else if ( flags & FTENT_COLLIDEWORLD )
		{
			trace_t trace;
			CTraceFilterWorldOnly traceFilter;
			UTIL_TraceLine( m_vecPrevLocalOrigin, GetLocalOrigin(), MASK_SOLID, &traceFilter, &trace );
			if ( trace.fraction != 1 )
			{
				traceFraction = trace.fraction;
				VectorCopy( trace.plane.normal, traceNormal );
			}
		}
		
		if ( traceFraction != 1 )	// Decent collision now, and damping works
		{
			float  proj, damp;

			// Place at contact point
			Vector newOrigin;
			VectorMA( m_vecPrevLocalOrigin, traceFraction*frametime, m_vecTempEntVelocity, newOrigin );
			SetLocalOrigin( newOrigin );
			
			// Damp velocity
			damp = bounceFactor;
			if ( flags & (FTENT_GRAVITY|FTENT_SLOWGRAVITY) )
			{
				damp *= 0.5;
				if ( traceNormal[2] > 0.9 )		// Hit floor?
				{
					if ( m_vecTempEntVelocity[2] <= 0 && m_vecTempEntVelocity[2] >= gravity*3 )
					{
						damp = 0;		// Stop
						flags &= ~(FTENT_ROTATE|FTENT_GRAVITY|FTENT_SLOWGRAVITY|FTENT_COLLIDEWORLD|FTENT_SMOKETRAIL);
						SetLocalAnglesDim( X_INDEX, 0 );
						SetLocalAnglesDim( Z_INDEX, 0 );
					}
				}
			}

			if ( flags & (FTENT_CHANGERENDERONCOLLIDE) )
			{
				m_RenderGroup = RENDER_GROUP_OTHER;
				flags &= ~FTENT_CHANGERENDERONCOLLIDE;
			}	

			if (hitSound)
			{
				tempents->PlaySound(this, damp);
			}

			if (flags & FTENT_COLLIDEKILL)
			{
				// die on impact
				flags &= ~FTENT_FADEOUT;	
				die = gpGlobals->curtime;			
			}
			else
			{
				// Reflect velocity
				if ( damp != 0 )
				{
					proj = ((Vector)m_vecTempEntVelocity).Dot(traceNormal);
					VectorMA( m_vecTempEntVelocity, -proj*2, traceNormal, m_vecTempEntVelocity );
					// Reflect rotation (fake)
					SetLocalAnglesDim( Y_INDEX, -GetLocalAnglesDim( Y_INDEX ) );
				}
				
				if ( damp != 1 )
				{
					VectorScale( m_vecTempEntVelocity, damp, m_vecTempEntVelocity );
					SetLocalAngles( GetLocalAngles() * 0.9 );
				}
			}
		}
	}


	if ( (flags & FTENT_FLICKER) && framenumber == m_nFlickerFrame )
	{
		dlight_t *dl = effects->CL_AllocDlight (LIGHT_INDEX_TE_DYNAMIC);
		VectorCopy (GetLocalOrigin(), dl->origin);
		dl->radius = 60;
		dl->color.r = 255;
		dl->color.g = 120;
		dl->color.b = 0;
		dl->die = gpGlobals->curtime + 0.01;
	}

	if ( flags & FTENT_SMOKETRAIL )
	{
		 Assert( !"FIXME:  Rework smoketrail to be client side\n" );
	}

	// add gravity if we didn't collide in this frame
	if ( traceFraction == 1 )
	{
		if ( flags & FTENT_GRAVITY )
			m_vecTempEntVelocity[2] += gravity;
		else if ( flags & FTENT_SLOWGRAVITY )
			m_vecTempEntVelocity[2] += gravitySlow;
	}

	if ( flags & FTENT_WINDBLOWN )
	{
		Vector vecWind;
		GetWindspeedAtTime( gpGlobals->curtime, vecWind );

		for ( int i = 0 ; i < 2 ; i++ )
		{
			if ( m_vecTempEntVelocity[i] < vecWind[i] )
			{
				m_vecTempEntVelocity[i] += ( frametime * TENT_WIND_ACCEL );

				// clamp
				if ( m_vecTempEntVelocity[i] > vecWind[i] )
					m_vecTempEntVelocity[i] = vecWind[i];
			}
			else if (m_vecTempEntVelocity[i] > vecWind[i] )
			{
				m_vecTempEntVelocity[i] -= ( frametime * TENT_WIND_ACCEL );

				// clamp.
				if ( m_vecTempEntVelocity[i] < vecWind[i] )
					m_vecTempEntVelocity[i] = vecWind[i];
			}
		}
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CTempEnts::CTempEnts( void ) :
	m_TempEntsPool( ( MAX_TEMP_ENTITIES / 20 ), CMemoryPool::GROW_SLOW )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CTempEnts::~CTempEnts( void )
{
	m_TempEntsPool.Clear();
	m_TempEnts.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Create a fizz effect
// Input  : *pent - 
//			modelIndex - 
//			density - 
//-----------------------------------------------------------------------------
void CTempEnts::FizzEffect( C_BaseEntity *pent, int modelIndex, int density, int current )
{
	C_LocalTempEntity		*pTemp;
	const model_t	*model;
	int				i, width, depth, count, frameCount;
	float			maxHeight, speed, xspeed, yspeed;
	Vector			origin;
	Vector			mins, maxs;

	if ( !pent->GetModel() || !modelIndex ) 
		return;

	model = modelinfo->GetModel( modelIndex );
	if ( !model )
		return;

	count = density + 1;
	density = count * 3 + 6;

	modelinfo->GetModelBounds( pent->GetModel(), mins, maxs );

	maxHeight = maxs[2] - mins[2];
	width = maxs[0] - mins[0];
	depth = maxs[1] - mins[1];
	speed = current;

	SinCos( pent->GetLocalAngles()[1]*M_PI/180, &yspeed, &xspeed );
	xspeed *= speed;
	yspeed *= speed;
	frameCount = modelinfo->GetModelFrameCount( model );

	for (i=0 ; i<count ; i++)
	{
		origin[0] = mins[0] + random->RandomInt(0,width-1);
		origin[1] = mins[1] + random->RandomInt(0,depth-1);
		origin[2] = mins[2];
		pTemp = TempEntAlloc( origin, (model_t *)model );
		if (!pTemp)
			return;

		pTemp->flags |= FTENT_SINEWAVE;

		pTemp->x = origin[0];
		pTemp->y = origin[1];

		float zspeed = random->RandomInt(80,140);
		pTemp->SetVelocity( Vector(xspeed, yspeed, zspeed) );
		pTemp->die = gpGlobals->curtime + (maxHeight / zspeed) - 0.1;
		pTemp->m_flFrame = random->RandomInt(0,frameCount-1);
		// Set sprite scale
		pTemp->m_flSpriteScale = 1.0 / random->RandomFloat(2,5);
		pTemp->SetRenderMode( kRenderTransAlpha );
		pTemp->SetRenderColorA( 255 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Create bubbles
// Input  : *mins - 
//			*maxs - 
//			height - 
//			modelIndex - 
//			count - 
//			speed - 
//-----------------------------------------------------------------------------
void CTempEnts::Bubbles( const Vector &mins, const Vector &maxs, float height, int modelIndex, int count, float speed )
{
	C_LocalTempEntity			*pTemp;
	const model_t		*model;
	int					i, frameCount;
	float				sine, cosine;
	Vector				origin;

	if ( !modelIndex ) 
		return;

	model = modelinfo->GetModel( modelIndex );
	if ( !model )
		return;

	frameCount = modelinfo->GetModelFrameCount( model );

	for (i=0 ; i<count ; i++)
	{
		origin[0] = random->RandomInt( mins[0], maxs[0] );
		origin[1] = random->RandomInt( mins[1], maxs[1] );
		origin[2] = random->RandomInt( mins[2], maxs[2] );
		pTemp = TempEntAlloc( origin, ( model_t * )model );
		if (!pTemp)
			return;

		pTemp->flags |= FTENT_SINEWAVE;

		pTemp->x = origin[0];
		pTemp->y = origin[1];
		SinCos( random->RandomInt( -M_PI, M_PI ), &sine, &cosine );
		
		float zspeed = random->RandomInt(80,140);
		pTemp->SetVelocity( Vector(speed * cosine, speed * sine, zspeed) );
		pTemp->die = gpGlobals->curtime + ((height - (origin[2] - mins[2])) / zspeed) - 0.1;
		pTemp->m_flFrame = random->RandomInt( 0, frameCount-1 );
		
		// Set sprite scale
		pTemp->m_flSpriteScale = 1.0 / random->RandomFloat(4,16);
		pTemp->SetRenderMode( kRenderTransAlpha );
		
		pTemp->SetRenderColor( 255, 255, 255, 192 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Create bubble trail
// Input  : *start - 
//			*end - 
//			height - 
//			modelIndex - 
//			count - 
//			speed - 
//-----------------------------------------------------------------------------
void CTempEnts::BubbleTrail( const Vector &start, const Vector &end, float flWaterZ, int modelIndex, int count, float speed )
{
	C_LocalTempEntity			*pTemp;
	const model_t		*model;
	int					i, frameCount;
	float				dist, angle;
	Vector				origin;

	if ( !modelIndex ) 
		return;

	model = modelinfo->GetModel( modelIndex );
	if ( !model )
		return;

	frameCount = modelinfo->GetModelFrameCount( model );

	for (i=0 ; i<count ; i++)
	{
		dist = random->RandomFloat( 0, 1.0 );
		VectorLerp( start, end, dist, origin );
		pTemp = TempEntAlloc( origin, ( model_t * )model );
		if (!pTemp)
			return;

		pTemp->flags |= FTENT_SINEWAVE;

		pTemp->x = origin[0];
		pTemp->y = origin[1];
		angle = random->RandomInt( -M_PI, M_PI );

		float zspeed = random->RandomInt(80,140);
		pTemp->SetVelocity( Vector(speed * cos(angle), speed * sin(angle), zspeed) );
		pTemp->die = gpGlobals->curtime + ((flWaterZ - origin[2]) / zspeed) - 0.1;
		pTemp->m_flFrame = random->RandomInt(0,frameCount-1);
		// Set sprite scale
		pTemp->m_flSpriteScale = 1.0 / random->RandomFloat(4,8);
		pTemp->SetRenderMode( kRenderTransAlpha );
		
		pTemp->SetRenderColor( 255, 255, 255, 192 );
	}
}

#define SHARD_VOLUME 12.0	// on shard ever n^3 units

//-----------------------------------------------------------------------------
// Purpose: Create model shattering shards
// Input  : *pos - 
//			*size - 
//			*dir - 
//			random - 
//			life - 
//			count - 
//			modelIndex - 
//			flags - 
//-----------------------------------------------------------------------------
void CTempEnts::BreakModel( const Vector &pos, const QAngle &angles, const Vector &size, const Vector &dir, 
						   float randRange, float life, int count, int modelIndex, char flags)
{
	int					i, frameCount;
	C_LocalTempEntity			*pTemp;
	const model_t		*pModel;
	char				type;

	if (!modelIndex) 
		return;

	type = flags & BREAK_TYPEMASK;

	pModel = modelinfo->GetModel( modelIndex );
	if ( !pModel )
		return;

	frameCount = modelinfo->GetModelFrameCount( pModel );

	if (count == 0)
	{
		// assume surface (not volume)
		count = (size[0] * size[1] + size[1] * size[2] + size[2] * size[0])/(3 * SHARD_VOLUME * SHARD_VOLUME);
	}

	if ( count > func_break_max_pieces.GetInt() )
	{
		count = func_break_max_pieces.GetInt();
	}

	matrix3x4_t transform;
	AngleMatrix( angles, pos, transform );
	for ( i = 0; i < count; i++ ) 
	{
		Vector vecLocalSpot, vecSpot;

		// fill up the box with stuff
		vecLocalSpot[0] = random->RandomFloat(-0.5,0.5) * size[0];
		vecLocalSpot[1] = random->RandomFloat(-0.5,0.5) * size[1];
		vecLocalSpot[2] = random->RandomFloat(-0.5,0.5) * size[2];
		VectorTransform( vecLocalSpot, transform, vecSpot );

		pTemp = TempEntAlloc(vecSpot, ( model_t * )pModel);
		
		if (!pTemp)
			return;

		// keep track of break_type, so we know how to play sound on collision
		pTemp->hitSound = type;
		
		if ( modelinfo->GetModelType( pModel ) == mod_sprite )
		{
			pTemp->m_flFrame = random->RandomInt(0,frameCount-1);
		}
		else if ( modelinfo->GetModelType( pModel ) == mod_studio )
		{
			pTemp->m_nBody = random->RandomInt(0,frameCount-1);
		}

		pTemp->flags |= FTENT_COLLIDEWORLD | FTENT_FADEOUT | FTENT_SLOWGRAVITY;

		if ( random->RandomInt(0,255) < 200 ) 
		{
			pTemp->flags |= FTENT_ROTATE;
			pTemp->m_vecTempEntAngVelocity[0] = random->RandomFloat(-256,255);
			pTemp->m_vecTempEntAngVelocity[1] = random->RandomFloat(-256,255);
			pTemp->m_vecTempEntAngVelocity[2] = random->RandomFloat(-256,255);
		}

		if ( (random->RandomInt(0,255) < 100 ) && (flags & BREAK_SMOKE) )
		{
			pTemp->flags |= FTENT_SMOKETRAIL;
		}

		if ((type == BREAK_GLASS) || (flags & BREAK_TRANS))
		{
			pTemp->SetRenderMode( kRenderTransTexture );
			pTemp->SetRenderColorA( 128 );
			pTemp->tempent_renderamt = 128;
			pTemp->bounceFactor = 0.3f;
		}
		else
		{
			pTemp->SetRenderMode( kRenderNormal );
			pTemp->tempent_renderamt = 255;		// Set this for fadeout
		}

		pTemp->SetVelocity( Vector( dir[0] + random->RandomFloat(-randRange,randRange),
							dir[1] + random->RandomFloat(-randRange,randRange),
							dir[2] + random->RandomFloat(   0,randRange) ) );

		pTemp->die = gpGlobals->curtime + life + random->RandomFloat(0,1);	// Add an extra 0-1 secs of life
	}
}

void CTempEnts::PhysicsProp( int modelindex, const Vector& pos, const QAngle &angles, const Vector& vel, int flags )
{
	C_PhysPropClientside *pEntity = C_PhysPropClientside::CreateNew();
	
	if ( !pEntity )
		return;

	const model_t *model = modelinfo->GetModel( modelindex );

	if ( !model )
	{
		DevMsg("CTempEnts::PhysicsProp: model index %i not found\n", modelinfo );
		return;
	}

	pEntity->SetModelName( modelinfo->GetModelName(model) );
	pEntity->SetAbsOrigin( pos );
	pEntity->SetAbsAngles( angles );
	pEntity->SetPhysicsMode( PHYSICS_MULTIPLAYER_CLIENTSIDE );

	if ( !pEntity->Initialize() )
	{
		pEntity->Release();
		return;
	}

	IPhysicsObject *pPhysicsObject = pEntity->VPhysicsGetObject();

	if( pPhysicsObject )
	{
		pPhysicsObject->AddVelocity( &vel, NULL );
	}
	else
	{
		// failed to create a physics object
		pEntity->Release();
		return;
	}

	if ( flags & 1 )
	{
		pEntity->SetHealth( 0 );
		pEntity->Break();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Create sprite TE
// Input  : *pos - 
//			*dir - 
//			scale - 
//			modelIndex - 
//			rendermode - 
//			renderfx - 
//			a - 
//			life - 
//			flags - 
// Output : C_LocalTempEntity
//-----------------------------------------------------------------------------
C_LocalTempEntity *CTempEnts::TempSprite( const Vector &pos, const Vector &dir, float scale, int modelIndex, int rendermode, int renderfx, float a, float life, int flags, const Vector &normal )
{
	C_LocalTempEntity			*pTemp;
	const model_t		*model;
	int					frameCount;

	if ( !modelIndex ) 
		return NULL;

	model = modelinfo->GetModel( modelIndex );
	if ( !model )
	{
		Warning("No model %d!\n", modelIndex);
		return NULL;
	}

	frameCount = modelinfo->GetModelFrameCount( model );

	pTemp = TempEntAlloc( pos, ( model_t * )model );
	if (!pTemp)
		return NULL;

	pTemp->m_flFrameMax = frameCount - 1;
	pTemp->m_flFrameRate = 10;
	pTemp->SetRenderMode( (RenderMode_t)rendermode );
	pTemp->m_nRenderFX = renderfx;
	pTemp->m_flSpriteScale = scale;
	pTemp->tempent_renderamt = a * 255;
	pTemp->m_vecNormal = normal;
	pTemp->SetRenderColor( 255, 255, 255, a * 255 );

	pTemp->flags |= flags;

	pTemp->SetVelocity( dir );
	pTemp->SetLocalOrigin( pos );
	if ( life )
		pTemp->die = gpGlobals->curtime + life;
	else
		pTemp->die = gpGlobals->curtime + (frameCount * 0.1) + 1;

	pTemp->m_flFrame = 0;
	return pTemp;
}

//-----------------------------------------------------------------------------
// Purpose: Spray sprite
// Input  : *pos - 
//			*dir - 
//			modelIndex - 
//			count - 
//			speed - 
//			iRand - 
//-----------------------------------------------------------------------------
void CTempEnts::Sprite_Spray( const Vector &pos, const Vector &dir, int modelIndex, int count, int speed, int iRand )
{
	C_LocalTempEntity			*pTemp;
	const model_t		*pModel;
	float				noise;
	float				znoise;
	int					frameCount;
	int					i;

	noise = (float)iRand / 100;

	// more vertical displacement
	znoise = noise * 1.5;
	
	if ( znoise > 1 )
	{
		znoise = 1;
	}

	pModel = modelinfo->GetModel( modelIndex );
	
	if ( !pModel )
	{
		Warning("No model %d!\n", modelIndex);
		return;
	}

	frameCount = modelinfo->GetModelFrameCount( pModel ) - 1;

	for ( i = 0; i < count; i++ )
	{
		pTemp = TempEntAlloc( pos, ( model_t * )pModel );
		if (!pTemp)
			return;

		pTemp->SetRenderMode( kRenderTransAlpha );
		pTemp->SetRenderColor( 255, 255, 255, 255 );
		pTemp->tempent_renderamt = 255;
		pTemp->m_nRenderFX = kRenderFxNoDissipation;
		//pTemp->scale = random->RandomFloat( 0.1, 0.25 );
		pTemp->m_flSpriteScale = 0.5;
		pTemp->flags |= FTENT_FADEOUT | FTENT_SLOWGRAVITY;
		pTemp->fadeSpeed = 2.0;

		// make the spittle fly the direction indicated, but mix in some noise.
		Vector velocity;
		velocity.x = dir[ 0 ] + random->RandomFloat ( -noise, noise );
		velocity.y = dir[ 1 ] + random->RandomFloat ( -noise, noise );
		velocity.z = dir[ 2 ] + random->RandomFloat ( 0, znoise );
		velocity *= random->RandomFloat( (speed * 0.8), (speed * 1.2) );
		pTemp->SetVelocity( velocity );

		pTemp->SetLocalOrigin( pos );

		pTemp->die = gpGlobals->curtime + 0.35;

		pTemp->m_flFrame = random->RandomInt( 0, frameCount );
	}
}

void CTempEnts::Sprite_Trail( const Vector &vecStart, const Vector &vecEnd, int modelIndex, int nCount, float flLife, float flSize, float flAmplitude, int nRenderamt, float flSpeed )
{
	C_LocalTempEntity	*pTemp;
	const model_t		*pModel;
	int					flFrameCount;

	pModel = modelinfo->GetModel( modelIndex );
	
	if ( !pModel )
	{
		Warning("No model %d!\n", modelIndex);
		return;
	}

	flFrameCount = modelinfo->GetModelFrameCount( pModel );

	Vector vecDelta;
	VectorSubtract( vecEnd, vecStart, vecDelta );

	Vector vecDir;
	VectorCopy( vecDelta, vecDir );
	VectorNormalize( vecDir );

	flAmplitude /= 256.0;

	for ( int i = 0 ; i < nCount; i++ )
	{
		Vector vecPos;

		// Be careful of divide by 0 when using 'count' here...
		if ( i == 0 )
		{
			VectorMA( vecStart, 0, vecDelta, vecPos );
		}
		else
		{
			VectorMA( vecStart, i / (nCount - 1.0), vecDelta, vecPos );
		}

		pTemp = TempEntAlloc( vecPos, ( model_t * )pModel );
		if (!pTemp)
			return;

		pTemp->flags |= FTENT_COLLIDEWORLD | FTENT_SPRCYCLE | FTENT_FADEOUT | FTENT_SLOWGRAVITY;

		Vector vecVel = vecDir * flSpeed;
		vecVel.x += random->RandomInt( -127,128 ) * flAmplitude;
		vecVel.y += random->RandomInt( -127,128 ) * flAmplitude;
		vecVel.z += random->RandomInt( -127,128 ) * flAmplitude;
		pTemp->SetVelocity( vecVel );
		pTemp->SetLocalOrigin( vecPos );

		pTemp->m_flSpriteScale		= flSize;
		pTemp->SetRenderMode( kRenderGlow );
		pTemp->m_nRenderFX			= kRenderFxNoDissipation;
		pTemp->tempent_renderamt	= nRenderamt;
		pTemp->SetRenderColor( 255, 255, 255 );

		pTemp->m_flFrame	= random->RandomInt( 0, flFrameCount - 1 );
		pTemp->m_flFrameMax	= flFrameCount - 1;
		pTemp->die			= gpGlobals->curtime + flLife + random->RandomFloat( 0, 4 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Attaches entity to player
// Input  : client - 
//			modelIndex - 
//			zoffset - 
//			life - 
//-----------------------------------------------------------------------------
void CTempEnts::AttachTentToPlayer( int client, int modelIndex, float zoffset, float life )
{
	C_LocalTempEntity			*pTemp;
	const model_t		*pModel;
	Vector				position;
	int					frameCount;

	if ( client < 0 || client > gpGlobals->maxClients )
	{
		Warning("Bad client in AttachTentToPlayer()!\n");
		return;
	}

	IClientEntity *clientClass = cl_entitylist->GetClientEntity( client );
	if ( !clientClass )
	{
		Warning("Couldn't get IClientEntity for %i\n", client );
		return;
	}

	pModel = modelinfo->GetModel( modelIndex );
	
	if ( !pModel )
	{
		Warning("No model %d!\n", modelIndex);
		return;
	}

	VectorCopy( clientClass->GetAbsOrigin(), position );
	position[ 2 ] += zoffset;

	pTemp = TempEntAllocHigh( position, ( model_t * )pModel );
	if (!pTemp)
	{
		Warning("No temp ent.\n");
		return;
	}

	pTemp->SetRenderMode( kRenderNormal );
	pTemp->SetRenderColorA( 255 );
	pTemp->tempent_renderamt = 255;
	pTemp->m_nRenderFX = kRenderFxNoDissipation;
	
	pTemp->clientIndex = client;
	pTemp->tentOffset[ 0 ] = 0;
	pTemp->tentOffset[ 1 ] = 0;
	pTemp->tentOffset[ 2 ] = zoffset;
	pTemp->die = gpGlobals->curtime + life;
	pTemp->flags |= FTENT_PLYRATTACHMENT | FTENT_PERSIST;

	// is the model a sprite?
	if ( modelinfo->GetModelType( pTemp->GetModel() ) == mod_sprite )
	{
		frameCount = modelinfo->GetModelFrameCount( pModel );
		pTemp->m_flFrameMax = frameCount - 1;
		pTemp->flags |= FTENT_SPRANIMATE | FTENT_SPRANIMATELOOP;
		pTemp->m_flFrameRate = 10;
	}
	else
	{
		// no animation support for attached clientside studio models.
		pTemp->m_flFrameMax = 0;
	}

	pTemp->m_flFrame = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Detach entity from player
//-----------------------------------------------------------------------------
void CTempEnts::KillAttachedTents( int client )
{
	if ( client < 0 || client > gpGlobals->maxClients )
	{
		Warning("Bad client in KillAttachedTents()!\n");
		return;
	}

	FOR_EACH_LL( m_TempEnts, i )
	{
		C_LocalTempEntity *pTemp = m_TempEnts[ i ];

		if ( pTemp->flags & FTENT_PLYRATTACHMENT )
		{
			// this TENT is player attached.
			// if it is attached to this client, set it to die instantly.
			if ( pTemp->clientIndex == client )
			{
				pTemp->die = gpGlobals->curtime;// good enough, it will die on next tent update. 
			}
		}
	}
}



//-----------------------------------------------------------------------------
// Purpose: Create ricochet sprite
// Input  : *pos - 
//			*pmodel - 
//			duration - 
//			scale - 
//-----------------------------------------------------------------------------
void CTempEnts::RicochetSprite( const Vector &pos, model_t *pmodel, float duration, float scale )
{
	C_LocalTempEntity	*pTemp;

	pTemp = TempEntAlloc( pos, ( model_t * )pmodel );
	if (!pTemp)
		return;

	pTemp->SetRenderMode( kRenderGlow );
	pTemp->m_nRenderFX = kRenderFxNoDissipation;
	pTemp->SetRenderColorA( 200 );
	pTemp->tempent_renderamt = 200;
	pTemp->m_flSpriteScale = scale;
	pTemp->flags = FTENT_FADEOUT;

	pTemp->SetVelocity( vec3_origin );

	pTemp->SetLocalOrigin( pos );
	
	pTemp->fadeSpeed = 8;
	pTemp->die = gpGlobals->curtime;

	pTemp->m_flFrame = 0;
	pTemp->SetLocalAnglesDim( Z_INDEX, 45 * random->RandomInt( 0, 7 ) );
}

//-----------------------------------------------------------------------------
// Purpose: Create blood sprite
// Input  : *org - 
//			colorindex - 
//			modelIndex - 
//			modelIndex2 - 
//			size - 
//-----------------------------------------------------------------------------
void CTempEnts::BloodSprite( const Vector &org, int r, int g, int b, int a, int modelIndex, int modelIndex2, float size )
{
	const model_t			*model;

	//Validate the model first
	if ( modelIndex && (model = modelinfo->GetModel( modelIndex ) ) != NULL )
	{
		C_LocalTempEntity		*pTemp;
		int						frameCount = modelinfo->GetModelFrameCount( model );
		color32					impactcolor = { r, g, b, a };

		//Large, single blood sprite is a high-priority tent
		if ( ( pTemp = TempEntAllocHigh( org, ( model_t * )model ) ) != NULL )
		{
			pTemp->SetRenderMode( kRenderTransTexture );
			pTemp->m_nRenderFX		= kRenderFxClampMinScale;
			pTemp->m_flSpriteScale	= random->RandomFloat( size / 25, size / 35);
			pTemp->flags			= FTENT_SPRANIMATE;
 
			pTemp->m_clrRender		= impactcolor;
			pTemp->tempent_renderamt= pTemp->m_clrRender->a;

			pTemp->SetVelocity( vec3_origin );

			pTemp->m_flFrameRate	= frameCount * 4; // Finish in 0.250 seconds
			pTemp->die				= gpGlobals->curtime + (frameCount / pTemp->m_flFrameRate); // Play the whole thing Once

			pTemp->m_flFrame		= 0;
			pTemp->m_flFrameMax		= frameCount - 1;
			pTemp->bounceFactor		= 0;
			pTemp->SetLocalAnglesDim( Z_INDEX, random->RandomInt( 0, 360 ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Create default sprite TE
// Input  : *pos - 
//			spriteIndex - 
//			framerate - 
// Output : C_LocalTempEntity
//-----------------------------------------------------------------------------
C_LocalTempEntity *CTempEnts::DefaultSprite( const Vector &pos, int spriteIndex, float framerate )
{
	C_LocalTempEntity		*pTemp;
	int				frameCount;
	const model_t	*pSprite;

	// don't spawn while paused
	if ( gpGlobals->frametime == 0.0 )
		return NULL;

	pSprite = modelinfo->GetModel( spriteIndex );
	if ( !spriteIndex || !pSprite || modelinfo->GetModelType( pSprite ) != mod_sprite )
	{
		DevWarning( 1,"No Sprite %d!\n", spriteIndex);
		return NULL;
	}

	frameCount = modelinfo->GetModelFrameCount( pSprite );

	pTemp = TempEntAlloc( pos, ( model_t * )pSprite );
	if (!pTemp)
		return NULL;

	pTemp->m_flFrameMax = frameCount - 1;
	pTemp->m_flSpriteScale = 1.0;
	pTemp->flags |= FTENT_SPRANIMATE;
	if ( framerate == 0 )
		framerate = 10;

	pTemp->m_flFrameRate = framerate;
	pTemp->die = gpGlobals->curtime + (float)frameCount / framerate;
	pTemp->m_flFrame = 0;
	pTemp->SetLocalOrigin( pos );

	return pTemp;
}

//-----------------------------------------------------------------------------
// Purpose: Create sprite smoke
// Input  : *pTemp - 
//			scale - 
//-----------------------------------------------------------------------------
void CTempEnts::Sprite_Smoke( C_LocalTempEntity *pTemp, float scale )
{
	if ( !pTemp )
		return;

	pTemp->SetRenderMode( kRenderTransAlpha );
	pTemp->m_nRenderFX = kRenderFxNone;
	pTemp->SetVelocity( Vector( 0, 0, 30 ) );
	int iColor = random->RandomInt(20,35);
	pTemp->SetRenderColor( iColor,
		iColor,
		iColor,
		255 );
	pTemp->SetLocalOriginDim( Z_INDEX, pTemp->GetLocalOriginDim( Z_INDEX ) + 20 );
	pTemp->m_flSpriteScale = scale;
	pTemp->flags = FTENT_WINDBLOWN;

}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pos1 - 
//			angles - 
//			type - 
//-----------------------------------------------------------------------------
void CTempEnts::EjectBrass( const Vector &pos1, const QAngle &angles, const QAngle &gunAngles, int type )
{
	if ( cl_ejectbrass.GetBool() == false )
		return;

	const model_t *pModel = m_pShells[type];
	
	if ( pModel == NULL )
		return;

	C_LocalTempEntity	*pTemp = TempEntAlloc( pos1, ( model_t * ) pModel );

	if ( pTemp == NULL )
		return;

	//Keep track of shell type
	if ( type == 2 )
	{
		pTemp->hitSound = BOUNCE_SHOTSHELL;
	}
	else
	{
		pTemp->hitSound = BOUNCE_SHELL;
	}

	pTemp->m_nBody	= 0;

	pTemp->flags |= ( FTENT_COLLIDEWORLD | FTENT_FADEOUT | FTENT_GRAVITY | FTENT_ROTATE );

	pTemp->m_vecTempEntAngVelocity[0] = random->RandomFloat(-1024,1024);
	pTemp->m_vecTempEntAngVelocity[1] = random->RandomFloat(-1024,1024);
	pTemp->m_vecTempEntAngVelocity[2] = random->RandomFloat(-1024,1024);

	//Face forward
	pTemp->SetAbsAngles( gunAngles );

	pTemp->SetRenderMode( kRenderNormal );
	pTemp->tempent_renderamt = 255;		// Set this for fadeout

	Vector	dir;

	AngleVectors( angles, &dir );

	dir *= random->RandomFloat( 150.0f, 200.0f );

	pTemp->SetVelocity( Vector(dir[0] + random->RandomFloat(-64,64),
						dir[1] + random->RandomFloat(-64,64),
						dir[2] + random->RandomFloat(  0,64) ) );

	pTemp->die = gpGlobals->curtime + 1.0f + random->RandomFloat( 0.0f, 1.0f );	// Add an extra 0-1 secs of life	
}

//-----------------------------------------------------------------------------
// Purpose: Create some simple physically simulated models
//-----------------------------------------------------------------------------
C_LocalTempEntity * CTempEnts::SpawnTempModel( model_t *pModel, const Vector &vecOrigin, const QAngle &vecAngles, const Vector &vecVelocity, float flLifeTime, int iFlags )
{
	Assert( pModel );

	// Alloc a new tempent
	C_LocalTempEntity *pTemp = TempEntAlloc( vecOrigin, ( model_t * ) pModel );
	if ( !pTemp )
		return NULL;

	pTemp->SetAbsAngles( vecAngles );
	pTemp->m_nBody	= 0;
	pTemp->flags |= iFlags;
	pTemp->m_vecTempEntAngVelocity[0] = random->RandomFloat(-255,255);
	pTemp->m_vecTempEntAngVelocity[1] = random->RandomFloat(-255,255);
	pTemp->m_vecTempEntAngVelocity[2] = random->RandomFloat(-255,255);
	pTemp->SetRenderMode( kRenderNormal );
	pTemp->tempent_renderamt = 255;
	pTemp->SetVelocity( vecVelocity );
	pTemp->die = gpGlobals->curtime + flLifeTime;

	return pTemp;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : type - 
//			entityIndex - 
//			attachmentIndex - 
//			firstPerson - 
//-----------------------------------------------------------------------------
void CTempEnts::MuzzleFlash( int type, int entityIndex, int attachmentIndex, bool firstPerson )
{
	// Mirv: Optionally disable'd muzzle flashes
	if (ffdev_disablemuzzleflashes.GetBool())
		return;

	DevMsg("[muzzleflash] type: %d, ent: %d, attachment: %d fp: %d\n", type, entityIndex, attachmentIndex, firstPerson );

/*		if ( firstPerson )
		{
			MuzzleFlash_Combine_Player( entityIndex, attachmentIndex );
		}
		else
		{
			MuzzleFlash_Combine_NPC( entityIndex, attachmentIndex );
		}
	return;*/

	switch( type )
	{
	case MUZZLEFLASH_COMBINE:
		if ( firstPerson )
		{
			MuzzleFlash_Combine_Player( entityIndex, attachmentIndex );
		}
		else
		{
			MuzzleFlash_Combine_NPC( entityIndex, attachmentIndex );
		}
		break;

	case MUZZLEFLASH_SMG1:
		if ( firstPerson )
		{
			MuzzleFlash_SMG1_Player( entityIndex, attachmentIndex );
		}
		else
		{
			MuzzleFlash_SMG1_NPC( entityIndex, attachmentIndex );
		}
		break;

	case MUZZLEFLASH_PISTOL:
		if ( firstPerson )
		{
			MuzzleFlash_Pistol_Player( entityIndex, attachmentIndex );
		}
		else
		{
			MuzzleFlash_Pistol_NPC( entityIndex, attachmentIndex );
		}
		break;
	case MUZZLEFLASH_SHOTGUN:
		if ( firstPerson )
		{
			MuzzleFlash_Shotgun_Player( entityIndex, attachmentIndex );
		}
		else
		{
			MuzzleFlash_Shotgun_NPC( entityIndex, attachmentIndex );
		}
		break;
	case MUZZLEFLASH_357:
		if ( firstPerson )
		{
			MuzzleFlash_357_Player( entityIndex, attachmentIndex );
		}
		break;
	case MUZZLEFLASH_RPG:
		if ( firstPerson )
		{
			// MuzzleFlash_RPG_Player( entityIndex, attachmentIndex );
		}
		else
		{
			MuzzleFlash_RPG_NPC( entityIndex, attachmentIndex );
		}
		break;
		break;
	default:
		{
			//NOTENOTE: This means you specified an invalid muzzleflash type, check your spelling?
			//Assert( 0 );
		}
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Play muzzle flash
// Input  : *pos1 - 
//			type - 
//-----------------------------------------------------------------------------
void CTempEnts::MuzzleFlash( const Vector& pos1, const QAngle& angles, int type, int entityIndex, bool firstPerson )
{
#ifdef CSTRIKE_DLL

#else

	// Mirv: Optionally disable'd muzzle flashes
	if (ffdev_disablemuzzleflashes.GetBool())
		return;

	DevMsg("[muzzleflash] type: %d, ent: %d, fp: %d\n", type, entityIndex, firstPerson );
/*		if ( firstPerson )
		{
			MuzzleFlash_AR2_Player( pos1, angles, entityIndex );
		}
		else
		{
			MuzzleFlash_AR2_NPC( pos1, angles, entityIndex );
		}
	return;*/

	//NOTENOTE: This function is becoming obsolete as the muzzles are moved over to being local to attachments

	switch ( type )
	{
	//
	// AR2
	//
	case MUZZLEFLASH_AR2:
		if ( firstPerson )
		{
			MuzzleFlash_AR2_Player( pos1, angles, entityIndex );
		}
		else
		{
			MuzzleFlash_AR2_NPC( pos1, angles, entityIndex );
		}
		break;

	//
	// Shotgun
	//
	case MUZZLEFLASH_SHOTGUN:
		if ( firstPerson )
		{
			//FIXME: These should go away
			MuzzleFlash_Shotgun_Player( entityIndex, 1 );
		}
		else
		{
			//FIXME: These should go away
			MuzzleFlash_Shotgun_NPC( entityIndex, 1 );
		}
		break;

	// UNDONE: These need their own effects/sprites.  For now use the pistol
	// SMG1
	case MUZZLEFLASH_SMG1:
		if ( firstPerson )
		{
			//FIXME: These should go away
			MuzzleFlash_SMG1_Player( entityIndex, 1 );
		}
		else
		{
			//FIXME: These should go away
			MuzzleFlash_SMG1_NPC( entityIndex, 1 );
		}
		break;

	// SMG2
	case MUZZLEFLASH_SMG2:
	case MUZZLEFLASH_PISTOL:
		if ( firstPerson )
		{
			MuzzleFlash_Pistol_Player( entityIndex, 1 );
		}
		else
		{
			//FIXME: These should go away
			MuzzleFlash_Pistol_NPC( entityIndex, 1 );
		}
		break;

	case MUZZLEFLASH_COMBINE:
		if ( firstPerson )
		{
			//FIXME: These should go away
			MuzzleFlash_Combine_Player( entityIndex, 1 );
		}
		else
		{
			//FIXME: These should go away
			MuzzleFlash_Combine_NPC( entityIndex, 1 );
		}
		break;
	
	default:
		// There's no supported muzzle flash for the type specified!
		//Assert(0);
		break;
	}

#endif

}

//-----------------------------------------------------------------------------
// Purpose: Create explosion sprite
// Input  : *pTemp - 
//			scale - 
//			flags - 
//-----------------------------------------------------------------------------
void CTempEnts::Sprite_Explode( C_LocalTempEntity *pTemp, float scale, int flags )
{
	if ( !pTemp )
		return;

	if ( flags & TE_EXPLFLAG_NOADDITIVE )
	{
		// solid sprite
		pTemp->SetRenderMode( kRenderNormal );
		pTemp->SetRenderColorA( 255 ); 
	}
	else if( flags & TE_EXPLFLAG_DRAWALPHA )
	{
		// alpha sprite
		pTemp->SetRenderMode( kRenderTransAlpha ); 
		pTemp->SetRenderColorA( 180 );
	}
	else
	{
		// additive sprite
		pTemp->SetRenderMode( kRenderTransAdd );
		pTemp->SetRenderColorA( 180 );
	}

	if ( flags & TE_EXPLFLAG_ROTATE )
	{
		pTemp->SetLocalAnglesDim( Z_INDEX, random->RandomInt( 0, 360 ) );
	}

	pTemp->m_nRenderFX = kRenderFxNone;
	pTemp->SetVelocity( Vector( 0, 0, 8 ) );
	pTemp->SetRenderColor( 255, 255, 255 );
	pTemp->SetLocalOriginDim( Z_INDEX, pTemp->GetLocalOriginDim( Z_INDEX ) + 10 );
	pTemp->m_flSpriteScale = scale;
}

enum
{
	SHELL_NONE = 0,
	SHELL_SMALL,
	SHELL_BIG,
	SHELL_SHOTGUN,
};

//-----------------------------------------------------------------------------
// Purpose: Clear existing temp entities
//-----------------------------------------------------------------------------
void CTempEnts::Clear( void )
{
	FOR_EACH_LL( m_TempEnts, i )
	{
		C_LocalTempEntity *p = m_TempEnts[ i ];

		m_TempEntsPool.Free( p );
	}

	m_TempEnts.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Allocate temp entity ( normal/low priority )
// Input  : *org - 
//			*model - 
// Output : C_LocalTempEntity
//-----------------------------------------------------------------------------
C_LocalTempEntity *CTempEnts::TempEntAlloc( const Vector& org, model_t *model )
{
	C_LocalTempEntity		*pTemp;

	if ( !model )
	{
		DevWarning( 1, "Can't create temporary entity with NULL model!\n" );
		return NULL;
	}

	pTemp = TempEntAlloc();

	if ( !pTemp )
	{
		DevWarning( 1, "Overflow %d temporary ents!\n", MAX_TEMP_ENTITIES );
		return NULL;
	}

	m_TempEnts.AddToTail( pTemp );

	pTemp->Prepare( model, gpGlobals->curtime );

	pTemp->priority = TENTPRIORITY_LOW;
	pTemp->SetAbsOrigin( org );

	pTemp->m_RenderGroup = RENDER_GROUP_OTHER;
	pTemp->AddToLeafSystem( pTemp->m_RenderGroup );

	return pTemp;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : C_LocalTempEntity
//-----------------------------------------------------------------------------
C_LocalTempEntity *CTempEnts::TempEntAlloc()
{
	if ( m_TempEnts.Count() >= MAX_TEMP_ENTITIES )
		return NULL;

	C_LocalTempEntity *pTemp = m_TempEntsPool.AllocZero();
	return pTemp;
}

void CTempEnts::TempEntFree( int index )
{
	C_LocalTempEntity *pTemp = m_TempEnts[ index ];
	if ( pTemp )
	{
		// Remove from the active list.
		m_TempEnts.Remove( index );

		// Cleanup its data.
		pTemp->RemoveFromLeafSystem();
	
		m_TempEntsPool.Free( pTemp );
	}
}


// Free the first low priority tempent it finds.
bool CTempEnts::FreeLowPriorityTempEnt()
{
	int next = 0;
	for( int i = m_TempEnts.Head(); i != m_TempEnts.InvalidIndex(); i = next )
	{
		next = m_TempEnts.Next( i );

		C_LocalTempEntity *pActive = m_TempEnts[ i ];

		if ( pActive->priority == TENTPRIORITY_LOW )
		{
			TempEntFree( i );
			return true;
		}
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Allocate a temp entity, if there are no slots, kick out a low priority
//  one if possible
// Input  : *org - 
//			*model - 
// Output : C_LocalTempEntity
//-----------------------------------------------------------------------------
C_LocalTempEntity *CTempEnts::TempEntAllocHigh( const Vector& org, model_t *model )
{
	C_LocalTempEntity		*pTemp;

	if ( !model )
	{
		DevWarning( 1, "temporary ent model invalid\n" );
		return NULL;
	}

	pTemp = TempEntAlloc();
	if ( !pTemp )
	{
		// no temporary ents free, so find the first active low-priority temp ent 
		// and overwrite it.
		FreeLowPriorityTempEnt();

		pTemp = TempEntAlloc();
	}

	
	if ( !pTemp )
	{
		// didn't find anything? The tent list is either full of high-priority tents
		// or all tents in the list are still due to live for > 10 seconds. 
		DevWarning( 1,"Couldn't alloc a high priority TENT (max %i)!\n", MAX_TEMP_ENTITIES );
		return NULL;
	}

	m_TempEnts.AddToTail( pTemp );

	pTemp->Prepare( model, gpGlobals->curtime );

	pTemp->priority = TENTPRIORITY_HIGH;
	pTemp->SetLocalOrigin( org );

	pTemp->m_RenderGroup = RENDER_GROUP_OTHER;
	pTemp->AddToLeafSystem( pTemp->m_RenderGroup );

	return pTemp;
}

//-----------------------------------------------------------------------------
// Purpose: Play sound when temp ent collides with something
// Input  : *pTemp - 
//			damp - 
//-----------------------------------------------------------------------------
void CTempEnts::PlaySound ( C_LocalTempEntity *pTemp, float damp )
{
	const char	*soundname = NULL;
	float fvol;
	bool isshellcasing = false;
	int zvel;

	switch ( pTemp->hitSound )
	{
	default:
		return;	// null sound

	case BOUNCE_GLASS:
		{
			soundname = "Bounce.Glass";
		}
		break;

	case BOUNCE_METAL:
		{
			soundname = "Bounce.Metal";
		}
		break;

	case BOUNCE_FLESH:
		{
			soundname = "Bounce.Flesh";
		}
		break;

	case BOUNCE_WOOD:
		{
			soundname = "Bounce.Wood";
		}
		break;

	case BOUNCE_SHRAP:
		{
			soundname = "Bounce.Shrapnel";
		}
		break;

	case BOUNCE_SHOTSHELL:
		{
			soundname = "Bounce.ShotgunShell";
			isshellcasing = true; // shell casings have different playback parameters
		}
		break;

	case BOUNCE_SHELL:
		{
			soundname = "Bounce.Shell";
			isshellcasing = true; // shell casings have different playback parameters
		}
		break;

	case BOUNCE_CONCRETE:
		{
			soundname = "Bounce.Concrete";
		}
		break;

#ifdef CSTRIKE_DLL

		case TE_PISTOL_SHELL:
		{
			soundname = "Bounce.PistolShell";
		}
		break;

		case TE_RIFLE_SHELL:
		{
			soundname = "Bounce.RifleShell";
		}
		break;

		case TE_SHOTGUN_SHELL:
		{
			soundname = "Bounce.ShotgunShell";
		}
		break;
#endif
	}

	zvel = abs( pTemp->GetVelocity()[2] );
		
	// only play one out of every n

	if ( isshellcasing )
	{	
		// play first bounce, then 1 out of 3		
		if ( zvel < 200 && random->RandomInt(0,3) )
			return;
	}
	else
	{
		if ( random->RandomInt(0,5) ) 
			return;
	}

	CSoundParameters params;
	if ( !C_BaseEntity::GetParametersForSound( soundname, params, NULL ) )
		return;

	fvol = params.volume;

	if ( damp > 0.0 )
	{
		int pitch;
		
		if ( isshellcasing )
		{
			fvol *= min (1.0, ((float)zvel) / 350.0); 
		}
		else
		{
			fvol *= min (1.0, ((float)zvel) / 450.0); 
		}
		
		if ( !random->RandomInt(0,3) && !isshellcasing )
		{
			pitch = random->RandomInt( params.pitchlow, params.pitchhigh );
		}
		else
		{
			pitch = params.pitch;
		}

		CLocalPlayerFilter filter;

		EmitSound_t ep;
		ep.m_nChannel = params.channel;
		ep.m_pSoundName =  params.soundname;
		ep.m_flVolume = fvol;
		ep.m_SoundLevel = params.soundlevel;
		ep.m_nPitch = pitch;
		ep.m_pOrigin = &pTemp->GetAbsOrigin();

		C_BaseEntity::EmitSound( filter, SOUND_FROM_WORLD, ep );
	}
}
					
//-----------------------------------------------------------------------------
// Purpose: Add temp entity to visible entities list of it's in PVS
// Input  : *pEntity - 
// Output : int
//-----------------------------------------------------------------------------
int CTempEnts::AddVisibleTempEntity( C_LocalTempEntity *pEntity )
{
	int i;
	Vector mins, maxs;
	Vector model_mins, model_maxs;

	if ( !pEntity->GetModel() )
		return 0;

	modelinfo->GetModelBounds( pEntity->GetModel(), model_mins, model_maxs );

	for (i=0 ; i<3 ; i++)
	{
		mins[i] = pEntity->GetAbsOrigin()[i] + model_mins[i];
		maxs[i] = pEntity->GetAbsOrigin()[i] + model_maxs[i];
	}

	// FIXME: Vis isn't setup by the time we get here, so this call fails if 
	//		  you try to add a tempent before the first frame is drawn, and it's
	//		  one frame behind the rest of the time. Fix this.
	// does the box intersect a visible leaf?
	//if ( engine->IsBoxInViewCluster( mins, maxs ) )
	{
		// Temporary entities have no corresponding element in cl_entitylist
		pEntity->index = -1;
		
		// Add to list
		if( pEntity->m_RenderGroup == RENDER_GROUP_OTHER )
		{
			pEntity->AddToLeafSystem();
		}
		else
		{
			pEntity->AddToLeafSystem( pEntity->m_RenderGroup );
		}
		
		return 1;
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Runs Temp Ent simulation routines
//-----------------------------------------------------------------------------
void CTempEnts::Update(void)
{
	static int gTempEntFrame = 0;
	float		frametime;

	// Don't simulate while loading
	if ( ( m_TempEnts.Count() == 0 ) || !engine->IsInGame() )		
	{
		return;
	}

	// !!!BUGBUG	-- This needs to be time based
	gTempEntFrame = (gTempEntFrame+1) & 31;

	frametime = gpGlobals->frametime;

	// in order to have tents collide with players, we have to run the player prediction code so
	// that the client has the player list. We run this code once when we detect any COLLIDEALL 
	// tent, then set this BOOL to true so the code doesn't get run again if there's more than
	// one COLLIDEALL ent for this update. (often are).

	// !!! Don't simulate while paused....  This is sort of a hack, revisit.
	if ( frametime == 0 )
	{
		FOR_EACH_LL( m_TempEnts, i )
		{
			C_LocalTempEntity *current = m_TempEnts[ i ];

			AddVisibleTempEntity( current );
		}
	}
	else
	{
		int next = 0;
		for( int i = m_TempEnts.Head(); i != m_TempEnts.InvalidIndex(); i = next )
		{
			next = m_TempEnts.Next( i );

			C_LocalTempEntity *current = m_TempEnts[ i ];

			// Kill it
			if ( !current->IsActive() || !current->Frame( frametime, gTempEntFrame ) )
			{
				TempEntFree( i );
			}
			else
			{
				// Cull to PVS (not frustum cull, just PVS)
				if ( !AddVisibleTempEntity( current ) )
				{
					if ( !( current->flags & FTENT_PERSIST ) ) 
					{
						// If we can't draw it this frame, just dump it.
						current->die = gpGlobals->curtime;
						// Don't fade out, just die
						current->flags &= ~FTENT_FADEOUT;

						TempEntFree( i );
					}
				}
			}
		}
	}
}

// Recache tempents which might have been flushed
void CTempEnts::LevelInit()
{
	m_pSpriteMuzzleFlash[0] = (model_t *)engine->LoadModel( "sprites/ar2_muzzle1.vmt" );
	m_pSpriteMuzzleFlash[1] = (model_t *)engine->LoadModel( "sprites/muzzleflash4.vmt" );
	m_pSpriteMuzzleFlash[2] = (model_t *)engine->LoadModel( "sprites/muzzleflash4.vmt" );

	m_pSpriteAR2Flash[0] = (model_t *)engine->LoadModel( "sprites/ar2_muzzle1b.vmt" );
	m_pSpriteAR2Flash[1] = (model_t *)engine->LoadModel( "sprites/ar2_muzzle2b.vmt" );
	m_pSpriteAR2Flash[2] = (model_t *)engine->LoadModel( "sprites/ar2_muzzle3b.vmt" );
	m_pSpriteAR2Flash[3] = (model_t *)engine->LoadModel( "sprites/ar2_muzzle4b.vmt" );

	m_pSpriteCombineFlash[0] = (model_t *)engine->LoadModel( "effects/combinemuzzle1.vmt" );
	m_pSpriteCombineFlash[1] = (model_t *)engine->LoadModel( "effects/combinemuzzle2.vmt" );

	m_pShells[0] = (model_t *) engine->LoadModel( "models/weapons/shell.mdl" );
	m_pShells[1] = (model_t *) engine->LoadModel( "models/weapons/rifleshell.mdl" );
	m_pShells[2] = (model_t *) engine->LoadModel( "models/weapons/shotgun_shell.mdl" );

#if defined( HL1_CLIENT_DLL )
	m_pHL1Shell			= (model_t *)engine->LoadModel( "models/shell.mdl" );
	m_pHL1ShotgunShell	= (model_t *)engine->LoadModel( "models/shotgunshell.mdl" );
#endif

#if defined( CSTRIKE_DLL ) || defined ( FF_DLL )
	m_pCS_9MMShell		= (model_t *)engine->LoadModel( "models/shells/shell_9mm.mdl" );
	m_pCS_57Shell		= (model_t *)engine->LoadModel( "models/shells/shell_57.mdl" );
	m_pCS_12GaugeShell	= (model_t *)engine->LoadModel( "models/shells/shell_12gauge.mdl" );
	m_pCS_556Shell		= (model_t *)engine->LoadModel( "models/shells/shell_556.mdl" );
	m_pCS_762NATOShell	= (model_t *)engine->LoadModel( "models/shells/shell_762nato.mdl" );
	m_pCS_338MAGShell	= (model_t *)engine->LoadModel( "models/shells/shell_338mag.mdl" );
	m_pFF_40MMShell		= (model_t *)engine->LoadModel( "models/shells/shell_40mm.mdl" );
#endif

	// --> Mirv: Load FF projectile models
	m_pFF_Nail			= (model_t *) engine->LoadModel("models/projectiles/nail/w_nail.mdl");
	m_pFF_Dart			= (model_t *) engine->LoadModel("models/projectiles/dart/w_dart.mdl");
	// <-- Mirv
}


//-----------------------------------------------------------------------------
// Purpose: Initialize TE system
//-----------------------------------------------------------------------------
void CTempEnts::Init (void)
{
	m_pSpriteMuzzleFlash[0] = NULL;
	m_pSpriteMuzzleFlash[1] = NULL;
	m_pSpriteMuzzleFlash[2] = NULL;

	m_pSpriteAR2Flash[0] = NULL;
	m_pSpriteAR2Flash[1] = NULL;
	m_pSpriteAR2Flash[2] = NULL;
	m_pSpriteAR2Flash[3] = NULL;

	m_pSpriteCombineFlash[0] = NULL;
	m_pSpriteCombineFlash[1] = NULL;

	m_pShells[0] = NULL;
	m_pShells[1] = NULL;
	m_pShells[2] = NULL;

#if defined( HL1_CLIENT_DLL )
	m_pHL1Shell			= NULL;
	m_pHL1ShotgunShell	= NULL;
#endif

#if defined( CSTRIKE_DLL ) || defined ( FF_DLL )
	m_pCS_9MMShell		= NULL;
	m_pCS_57Shell		= NULL;
	m_pCS_12GaugeShell	= NULL;
	m_pCS_556Shell		= NULL;
	m_pCS_762NATOShell	= NULL;
	m_pCS_338MAGShell	= NULL;
#endif

	// Clear out lists to start
	Clear();
}


void CTempEnts::LevelShutdown()
{
	// Free all active tempents.
	Clear();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTempEnts::Shutdown()
{
	LevelShutdown();
}

//==================================================
// Purpose: 
// Input: 
//==================================================

void CTempEnts::MuzzleFlash_AR2_Player( const Vector &origin, const QAngle &angles, int entityIndex )
{
#if 1

	float	scale = 1.5f;
	Vector	forward, offset;

	AngleVectors( angles, &forward );
	float flScale = random->RandomFloat( scale-0.25f, scale );

	if ( flScale < 0.5f )
	{
		flScale = 0.5f;
	}

	//
	// Flash
	//
	C_LocalTempEntity *pTemp;

	for ( int i = 1; i < 9; i++ )
	{
		offset = origin + (forward * (i*8.0f*flScale));

		pTemp = TempEntAllocHigh( offset, (model_t *) m_pSpriteAR2Flash[random->RandomInt(0,3)] );
		
		if ( pTemp == NULL )
			return;

		//Setup our colors and type
		pTemp->SetRenderMode( kRenderTransAdd );
		pTemp->SetRenderColor( 164, 164, 164+random->RandomInt(0,64), 255 );
		pTemp->tempent_renderamt= 255;
		pTemp->m_nRenderFX		= kRenderFxNone;
		pTemp->flags			= FTENT_PERSIST;
		pTemp->die				= gpGlobals->curtime + 0.025f;
		pTemp->m_flFrame		= 0;
		pTemp->m_flFrameMax		= 0;
		
		pTemp->SetLocalOrigin( offset );
		pTemp->SetLocalAngles( vec3_angle );
		
		pTemp->SetVelocity( vec3_origin );
		
		//Scale and rotate
		pTemp->m_flSpriteScale	= ( (random->RandomFloat( 6.0f, 8.0f ) * (12-(i))/9) * flScale ) / 32;
		pTemp->SetLocalAnglesDim( Z_INDEX, random->RandomInt( 0, 360 ) );

		pTemp->AddToLeafSystem();
	}

	//Flare
	pTemp = TempEntAlloc( origin, (model_t *) m_pSpriteMuzzleFlash[0] );
	
	if ( pTemp == NULL )
		return;

	//Setup our colors and type
	pTemp->SetRenderMode( kRenderTransAdd );
	pTemp->SetRenderColor( 255, 255, 255, 255 );
	pTemp->tempent_renderamt= 255;
	pTemp->m_nRenderFX		= kRenderFxNone;
	pTemp->flags			= FTENT_PERSIST;
	pTemp->die				= gpGlobals->curtime + 0.025f;
	pTemp->m_flFrame		= 0;
	pTemp->m_flFrameMax		= 0;
	
	pTemp->SetLocalOrigin( origin );
	pTemp->SetLocalAngles( vec3_angle );
	
	pTemp->SetVelocity( vec3_origin );
	
	//Scale and rotate
	pTemp->m_flSpriteScale	= random->RandomFloat( 0.05f, 0.1f );
	pTemp->SetLocalAnglesDim( Z_INDEX, random->RandomInt( 0, 360 ) );

	pTemp->AddToLeafSystem();

#else

	C_LocalTempEntity *pTemp = TempEntAlloc( origin, (model_t *) m_pSpriteMuzzleFlash[0] );
	
	if ( pTemp == NULL )
		return;

	//Setup our colors and type
	pTemp->m_nRenderMode	= kRenderTransAdd;
	pTemp->m_clrRender.r	= 255;
	pTemp->m_clrRender.g	= 255;
	pTemp->m_clrRender.b	= 255;
	pTemp->m_clrRender.a	= 255;
	pTemp->tempent_renderamt= 255;
	pTemp->m_nRenderFX		= kRenderFxNone;
	pTemp->flags			= FTENT_NONE;
	pTemp->die				= gpGlobals->curtime + 0.025f;
	pTemp->m_flFrame		= 0;
	pTemp->m_flFrameMax		= 0;
	
	pTemp->SetLocalOrigin( origin );
	pTemp->SetLocalAngles( vec3_angle );
	
	pTemp->SetVelocity( vec3_origin );
	
	//Scale and rotate
	pTemp->m_flSpriteScale		= random->RandomFloat( 0.1f, 0.15f );
	pTemp->SetAnglesDim( Z_INDEX, (360.0f/6.0f) * random->RandomInt( 0, 5 ) );

	pTemp->AddToLeafSystem();

	//
	// Smoke
	//

	Vector forward, start, end;

	AngleVectors( angles, &forward );

	start   = (Vector) origin - ( forward * 8 );
	end		= (Vector) origin + ( forward * random->RandomFloat( 48, 128 ) );

	unsigned int	flags = 0;

	if ( random->RandomInt( 0, 1 ) )
	{
		flags |= FXSTATICLINE_FLIP_HORIZONTAL;
	}

	const char *text = ( random->RandomInt( 0, 1 ) ) ? "sprites/ar2_muzzle3" : "sprites/ar2_muzzle4";

	FX_AddStaticLine( start, end, random->RandomFloat( 8.0f, 14.0f ), 0.01f, text, flags );

#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : entityIndex - 
//			attachmentIndex - 
//-----------------------------------------------------------------------------
void CTempEnts::MuzzleFlash_Combine_Player( int entityIndex, int attachmentIndex )
{
	VPROF_BUDGET( "MuzzleFlash_Combine_Player", VPROF_BUDGETGROUP_PARTICLE_RENDERING );
	CSmartPtr<CLocalSpaceEmitter> pSimple = CLocalSpaceEmitter::Create( "MuzzleFlash", entityIndex, attachmentIndex, FLE_VIEWMODEL );

	SimpleParticle *pParticle;
	Vector			forward(1,0,0), offset; //NOTENOTE: All coords are in local space

	float flScale = random->RandomFloat( 2.0f, 2.25f );

	pSimple->SetDrawBeforeViewModel( true );

	// Flash
	for ( int i = 1; i < 6; i++ )
	{
		offset = (forward * (i*8.0f*flScale));

		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/combinemuzzle%d_noz", random->RandomInt(1,2) ) ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= 0.025f;

		pParticle->m_vecVelocity.Init();

		pParticle->m_uchColor[0]	= 255;
		pParticle->m_uchColor[1]	= 255;
		pParticle->m_uchColor[2]	= 200+random->RandomInt(0,55);

		pParticle->m_uchStartAlpha	= 255;
		pParticle->m_uchEndAlpha	= 255;

		pParticle->m_uchStartSize	= ( (random->RandomFloat( 6.0f, 8.0f ) * (12-(i))/12) * flScale );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= 0.0f;
	}

	// Tack on the smoke
	pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/combinemuzzle%d_noz", random->RandomInt(1,2) ) ), vec3_origin );
		
	if ( pParticle == NULL )
		return;

	pParticle->m_flLifetime		= 0.0f;
	pParticle->m_flDieTime		= 0.025f;

	pParticle->m_vecVelocity.Init();

	pParticle->m_uchColor[0]	= 255;
	pParticle->m_uchColor[1]	= 255;
	pParticle->m_uchColor[2]	= 255;

	pParticle->m_uchStartAlpha	= random->RandomInt( 64, 128 );
	pParticle->m_uchEndAlpha	= 32;

	pParticle->m_uchStartSize	= random->RandomFloat( 10.0f, 16.0f );
	pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
	
	pParticle->m_flRoll			= random->RandomInt( 0, 360 );
	pParticle->m_flRollDelta	= 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &origin - 
//			&angles - 
//			entityIndex - 
//-----------------------------------------------------------------------------
void CTempEnts::MuzzleFlash_Combine_NPC( int entityIndex, int attachmentIndex )
{
	VPROF_BUDGET( "MuzzleFlash_Strider", VPROF_BUDGETGROUP_PARTICLE_RENDERING );
	CSmartPtr<CLocalSpaceEmitter> pSimple = CLocalSpaceEmitter::Create( "MuzzleFlash_Strider", entityIndex, attachmentIndex );

	SimpleParticle *pParticle;
	Vector			forward(1,0,0), offset; //NOTENOTE: All coords are in local space

	float flScale = random->RandomFloat( 1.0f, 1.5f );

	float burstSpeed = random->RandomFloat( 50.0f, 150.0f );

#define	FRONT_LENGTH 6

	// Front flash
	for ( int i = 1; i < FRONT_LENGTH; i++ )
	{
		offset = (forward * (i*2.0f*flScale));

		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/combinemuzzle%d", random->RandomInt(1,2) ) ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= 0.1f;

		pParticle->m_vecVelocity = forward * burstSpeed;

		pParticle->m_uchColor[0]	= 255;
		pParticle->m_uchColor[1]	= 255;
		pParticle->m_uchColor[2]	= 255;

		pParticle->m_uchStartAlpha	= 255.0f;
		pParticle->m_uchEndAlpha	= 0;

		pParticle->m_uchStartSize	= ( (random->RandomFloat( 6.0f, 8.0f ) * (FRONT_LENGTH*1.25f-(i))/(FRONT_LENGTH)) * flScale );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= 0.0f;
	}
	
	Vector right(0,1,0), up(0,0,1);
	Vector dir = right - up;

#define	SIDE_LENGTH	6

	burstSpeed = random->RandomFloat( 50.0f, 150.0f );

	// Diagonal flash
	for ( i = 1; i < SIDE_LENGTH; i++ )
	{
		offset = (dir * (i*flScale));

		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/combinemuzzle%d", random->RandomInt(1,2) ) ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= 0.2f;

		pParticle->m_vecVelocity = dir * burstSpeed * 0.25f;

		pParticle->m_uchColor[0]	= 255;
		pParticle->m_uchColor[1]	= 255;
		pParticle->m_uchColor[2]	= 255;

		pParticle->m_uchStartAlpha	= 255;
		pParticle->m_uchEndAlpha	= 0;

		pParticle->m_uchStartSize	= ( (random->RandomFloat( 2.0f, 4.0f ) * (SIDE_LENGTH-(i))/(SIDE_LENGTH*0.5f)) * flScale );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= 0.0f;
	}

	dir = right + up;
	burstSpeed = random->RandomFloat( 50.0f, 150.0f );

	// Diagonal flash
	for ( i = 1; i < SIDE_LENGTH; i++ )
	{
		offset = (-dir * (i*flScale));

		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/combinemuzzle%d", random->RandomInt(1,2) ) ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= 0.2f;

		pParticle->m_vecVelocity = dir * -burstSpeed * 0.25f;

		pParticle->m_uchColor[0]	= 255;
		pParticle->m_uchColor[1]	= 255;
		pParticle->m_uchColor[2]	= 255;

		pParticle->m_uchStartAlpha	= 255;
		pParticle->m_uchEndAlpha	= 0;

		pParticle->m_uchStartSize	= ( (random->RandomFloat( 2.0f, 4.0f ) * (SIDE_LENGTH-(i))/(SIDE_LENGTH*0.5f)) * flScale );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= 0.0f;
	}

	dir = up;
	burstSpeed = random->RandomFloat( 50.0f, 150.0f );

	// Top flash
	for ( i = 1; i < SIDE_LENGTH; i++ )
	{
		offset = (dir * (i*flScale));

		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/combinemuzzle%d", random->RandomInt(1,2) ) ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= 0.2f;

		pParticle->m_vecVelocity = dir * burstSpeed * 0.25f;

		pParticle->m_uchColor[0]	= 255;
		pParticle->m_uchColor[1]	= 255;
		pParticle->m_uchColor[2]	= 255;

		pParticle->m_uchStartAlpha	= 255;
		pParticle->m_uchEndAlpha	= 0;

		pParticle->m_uchStartSize	= ( (random->RandomFloat( 2.0f, 4.0f ) * (SIDE_LENGTH-(i))/(SIDE_LENGTH*0.5f)) * flScale );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= 0.0f;
	}

	pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( "effects/strider_muzzle" ), vec3_origin );
		
	if ( pParticle == NULL )
		return;

	pParticle->m_flLifetime		= 0.0f;
	pParticle->m_flDieTime		= random->RandomFloat( 0.3f, 0.4f );

	pParticle->m_vecVelocity.Init();

	pParticle->m_uchColor[0]	= 255;
	pParticle->m_uchColor[1]	= 255;
	pParticle->m_uchColor[2]	= 255;

	pParticle->m_uchStartAlpha	= 255;
	pParticle->m_uchEndAlpha	= 0;

	pParticle->m_uchStartSize	= flScale * random->RandomFloat( 12.0f, 16.0f );
	pParticle->m_uchEndSize		= 0.0f;
	pParticle->m_flRoll			= random->RandomInt( 0, 360 );
	pParticle->m_flRollDelta	= 0.0f;

	matrix3x4_t	matAttachment;
	Vector		origin;
	
	// Grab the origin out of the transform for the attachment
	if ( FX_GetAttachmentTransform( entityIndex, attachmentIndex, matAttachment ) )
	{
		origin.x = matAttachment[0][3];
		origin.y = matAttachment[1][3];
		origin.z = matAttachment[2][3];
	}
	else
	{
		//NOTENOTE: If you're here, you've specified an entity or an attachment that is invalid
		Assert(0);
		return;
	}

	if ( muzzleflash_light.GetBool() )
	{
		dlight_t *el = effects->CL_AllocElight( LIGHT_INDEX_MUZZLEFLASH + entityIndex );

		el->origin	= origin;

		el->color.r = 64;
		el->color.g = 128;
		el->color.b = 255;
		el->color.exponent = 5;

		el->radius	= random->RandomInt( 32, 128 );
		el->decay	= el->radius / 0.05f;
		el->die		= gpGlobals->curtime + 0.05f;
	}
}

//==================================================
// Purpose: 
// Input: 
//==================================================

void CTempEnts::MuzzleFlash_AR2_NPC( const Vector &origin, const QAngle &angles, int entityIndex )
{
	//Draw the cloud of fire
	FX_MuzzleEffect( origin, angles, 1.0f, entityIndex );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &origin - 
//			&angles - 
//-----------------------------------------------------------------------------
void CTempEnts::MuzzleFlash_SMG1_NPC( int entityIndex, int attachmentIndex )
{
	//Draw the cloud of fire
	FX_MuzzleEffectAttached( 1.0f, entityIndex, attachmentIndex, NULL, true );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &origin - 
//			&angles - 
//-----------------------------------------------------------------------------
void CTempEnts::MuzzleFlash_SMG1_Player( int entityIndex, int attachmentIndex )
{
	VPROF_BUDGET( "MuzzleFlash_SMG1_Player", VPROF_BUDGETGROUP_PARTICLE_RENDERING );
	CSmartPtr<CLocalSpaceEmitter> pSimple = CLocalSpaceEmitter::Create( "MuzzleFlash_SMG1_Player", entityIndex, attachmentIndex, FLE_VIEWMODEL );

	SimpleParticle *pParticle;
	Vector			forward(1,0,0), offset; //NOTENOTE: All coords are in local space

	float flScale = random->RandomFloat( 1.25f, 1.5f );

	pSimple->SetDrawBeforeViewModel( true );

	// Flash
	for ( int i = 1; i < 6; i++ )
	{
		offset = (forward * (i*8.0f*flScale));

		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/muzzleflash%d_noz", random->RandomInt(1,4) ) ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= 0.025f;

		pParticle->m_vecVelocity.Init();

		pParticle->m_uchColor[0]	= 255;
		pParticle->m_uchColor[1]	= 255;
		pParticle->m_uchColor[2]	= 200+random->RandomInt(0,55);

		pParticle->m_uchStartAlpha	= 255;
		pParticle->m_uchEndAlpha	= 255;

		pParticle->m_uchStartSize	= ( (random->RandomFloat( 6.0f, 8.0f ) * (8-(i))/6) * flScale );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= 0.0f;
	}
}

//==================================================
// Purpose: 
// Input: 
//==================================================

void CTempEnts::MuzzleFlash_Shotgun_Player( int entityIndex, int attachmentIndex )
{
	VPROF_BUDGET( "MuzzleFlash_Shotgun_Player", VPROF_BUDGETGROUP_PARTICLE_RENDERING );
	CSmartPtr<CSimpleEmitter> pSimple = CSimpleEmitter::Create( "MuzzleFlash_Shotgun_Player" );

	pSimple->SetDrawBeforeViewModel( true );

	Vector origin;
	QAngle angles;

	// Get our attachment's transformation matrix
	FX_GetAttachmentTransform( entityIndex, attachmentIndex, &origin, &angles );

	Vector forward;
	AngleVectors( angles, &forward, NULL, NULL );

	SimpleParticle *pParticle;
	Vector			offset;

	float flScale = random->RandomFloat( 1.25f, 1.5f );

	// Flash
	for ( int i = 1; i < 6; i++ )
	{
		offset = origin + (forward * (i*8.0f*flScale));

		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/muzzleflash%d_noz", random->RandomInt(1,4) ) ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= 0.0001f;

		pParticle->m_vecVelocity.Init();

		pParticle->m_uchColor[0]	= 255;
		pParticle->m_uchColor[1]	= 255;
		pParticle->m_uchColor[2]	= 200+random->RandomInt(0,55);

		pParticle->m_uchStartAlpha	= 255;
		pParticle->m_uchEndAlpha	= 255;

		pParticle->m_uchStartSize	= ( (random->RandomFloat( 6.0f, 8.0f ) * (8-(i))/6) * flScale );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= 0.0f;
	}
}

//==================================================
// Purpose: 
// Input: 
//==================================================

void CTempEnts::MuzzleFlash_Shotgun_NPC( int entityIndex, int attachmentIndex )
{
	//Draw the cloud of fire
	FX_MuzzleEffectAttached( 0.75f, entityIndex, attachmentIndex );

	QAngle	angles;

	Vector	forward;
	int		i;

	// Setup the origin.
	Vector	origin;
	C_BaseEntity *pEnt = ClientEntityList().GetEnt( entityIndex );
	if ( pEnt )
	{
		pEnt->GetAttachment( attachmentIndex, origin, angles );
	}
	else
	{
		return;
	}

	AngleVectors( angles, &forward );

	//Embers less often
	if ( random->RandomInt( 0, 2 ) == 0 )
	{
		//Embers
		CSmartPtr<CEmberEffect> pEmbers = CEmberEffect::Create( "muzzle_embers" );
		pEmbers->SetSortOrigin( origin );

		SimpleParticle	*pParticle;

		int	numEmbers = random->RandomInt( 0, 4 );

		for ( int i = 0; i < numEmbers; i++ )
		{
			pParticle = (SimpleParticle *) pEmbers->AddParticle( sizeof( SimpleParticle ), pEmbers->GetPMaterial( "effects/muzzleflash1" ), origin );
				
			if ( pParticle == NULL )
				return;

			pParticle->m_flLifetime		= 0.0f;
			pParticle->m_flDieTime		= random->RandomFloat( 0.2f, 0.4f );

			pParticle->m_vecVelocity.Random( -0.05f, 0.05f );
			pParticle->m_vecVelocity += forward;
			VectorNormalize( pParticle->m_vecVelocity );

			pParticle->m_vecVelocity	*= random->RandomFloat( 64.0f, 256.0f );

			pParticle->m_uchColor[0]	= 255;
			pParticle->m_uchColor[1]	= 128;
			pParticle->m_uchColor[2]	= 64;

			pParticle->m_uchStartAlpha	= 255;
			pParticle->m_uchEndAlpha	= 0;

			pParticle->m_uchStartSize	= 1;
			pParticle->m_uchEndSize		= 0;

			pParticle->m_flRoll			= 0;
			pParticle->m_flRollDelta	= 0;
		}
	}

	//
	// Trails
	//
	
	CSmartPtr<CTrailParticles> pTrails = CTrailParticles::Create( "MuzzleFlash_Shotgun_NPC" );
	pTrails->SetSortOrigin( origin );

	TrailParticle	*pTrailParticle;

	pTrails->SetFlag( bitsPARTICLE_TRAIL_FADE );
	pTrails->m_ParticleCollision.SetGravity( 0.0f );

	int	numEmbers = random->RandomInt( 4, 8 );

	for ( i = 0; i < numEmbers; i++ )
	{
		pTrailParticle = (TrailParticle *) pTrails->AddParticle( sizeof( TrailParticle ), pTrails->GetPMaterial( "effects/muzzleflash1" ), origin );
			
		if ( pTrailParticle == NULL )
			return;

		pTrailParticle->m_flLifetime		= 0.0f;
		pTrailParticle->m_flDieTime		= random->RandomFloat( 0.1f, 0.2f );

		float spread = 0.05f;

		pTrailParticle->m_vecVelocity.Random( -spread, spread );
		pTrailParticle->m_vecVelocity += forward;
		
		VectorNormalize( pTrailParticle->m_vecVelocity );
		VectorNormalize( forward );

		float dot = forward.Dot( pTrailParticle->m_vecVelocity );

		dot = (1.0f-fabs(dot)) / spread;
		pTrailParticle->m_vecVelocity *= (random->RandomFloat( 256.0f, 1024.0f ) * (1.0f-dot));

		Color32Init( pTrailParticle->m_color, 255, 242, 191, 255 );

		pTrailParticle->m_flLength	= 0.05f;
		pTrailParticle->m_flWidth	= random->RandomFloat( 0.25f, 0.5f );
	}
}

//==================================================
// Purpose: 
//==================================================
void CTempEnts::MuzzleFlash_357_Player( int entityIndex, int attachmentIndex )
{
	VPROF_BUDGET( "MuzzleFlash_357_Player", VPROF_BUDGETGROUP_PARTICLE_RENDERING );
	CSmartPtr<CSimpleEmitter> pSimple = CSimpleEmitter::Create( "MuzzleFlash_357_Player" );

	pSimple->SetDrawBeforeViewModel( true );

	Vector origin;
	QAngle angles;

	// Get our attachment's transformation matrix
	FX_GetAttachmentTransform( entityIndex, attachmentIndex, &origin, &angles );

	Vector forward;
	AngleVectors( angles, &forward, NULL, NULL );

	SimpleParticle *pParticle;
	Vector			offset;

	// Smoke
	offset = origin + forward * 8.0f;

	pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( "particle/particle_smokegrenade" ), offset );
		
	if ( pParticle == NULL )
		return;

	pParticle->m_flLifetime		= 0.0f;
	pParticle->m_flDieTime		= random->RandomFloat( 0.5f, 1.0f );

	pParticle->m_vecVelocity.Init();
	pParticle->m_vecVelocity = forward * random->RandomFloat( 8.0f, 64.0f );
	pParticle->m_vecVelocity[2] += random->RandomFloat( 4.0f, 16.0f );

	int color = random->RandomInt( 200, 255 );
	pParticle->m_uchColor[0]	= color;
	pParticle->m_uchColor[1]	= color;
	pParticle->m_uchColor[2]	= color;

	pParticle->m_uchStartAlpha	= random->RandomInt( 64, 128 );
	pParticle->m_uchEndAlpha	= 0;

	pParticle->m_uchStartSize	= random->RandomInt( 2, 4 );
	pParticle->m_uchEndSize		= pParticle->m_uchStartSize * 8.0f;
	pParticle->m_flRoll			= random->RandomInt( 0, 360 );
	pParticle->m_flRollDelta	= random->RandomFloat( -0.5f, 0.5f );

	float flScale = random->RandomFloat( 1.25f, 1.5f );

	// Flash
	for ( int i = 1; i < 6; i++ )
	{
		offset = origin + (forward * (i*8.0f*flScale));

		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/muzzleflash%d_noz", random->RandomInt(1,4) ) ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= 0.01f;

		pParticle->m_vecVelocity.Init();

		pParticle->m_uchColor[0]	= 255;
		pParticle->m_uchColor[1]	= 255;
		pParticle->m_uchColor[2]	= 200+random->RandomInt(0,55);

		pParticle->m_uchStartAlpha	= 255;
		pParticle->m_uchEndAlpha	= 255;

		pParticle->m_uchStartSize	= ( (random->RandomFloat( 6.0f, 8.0f ) * (8-(i))/6) * flScale );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= 0.0f;
	}
}

//==================================================
// Purpose: 
// Input: 
//==================================================

void CTempEnts::MuzzleFlash_Pistol_Player( int entityIndex, int attachmentIndex )
{
	VPROF_BUDGET( "MuzzleFlash_Pistol_Player", VPROF_BUDGETGROUP_PARTICLE_RENDERING );
	CSmartPtr<CSimpleEmitter> pSimple = CSimpleEmitter::Create( "MuzzleFlash_Pistol_Player" );

	pSimple->SetDrawBeforeViewModel( true );

	Vector origin;
	QAngle angles;

	// Get our attachment's transformation matrix
	FX_GetAttachmentTransform( entityIndex, attachmentIndex, &origin, &angles );

	Vector forward;
	AngleVectors( angles, &forward, NULL, NULL );

	SimpleParticle *pParticle;
	Vector			offset;

	// Smoke
	offset = origin + forward * 8.0f;

	if ( random->RandomInt( 0, 3 ) != 0 )
	{
		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( "particle/particle_smokegrenade" ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= random->RandomFloat( 0.25f, 0.5f );

		pParticle->m_vecVelocity.Init();
		pParticle->m_vecVelocity = forward * random->RandomFloat( 48.0f, 64.0f );
		pParticle->m_vecVelocity[2] += random->RandomFloat( 4.0f, 16.0f );

		int color = random->RandomInt( 200, 255 );
		pParticle->m_uchColor[0]	= color;
		pParticle->m_uchColor[1]	= color;
		pParticle->m_uchColor[2]	= color;

		pParticle->m_uchStartAlpha	= random->RandomInt( 64, 128 );
		pParticle->m_uchEndAlpha	= 0;

		pParticle->m_uchStartSize	= random->RandomInt( 2, 4 );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize * 4.0f;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= random->RandomFloat( -0.1f, 0.1f );
	}

	float flScale = random->RandomFloat( 1.0f, 1.25f );

	// Flash
	for ( int i = 1; i < 6; i++ )
	{
		offset = origin + (forward * (i*4.0f*flScale));

		pParticle = (SimpleParticle *) pSimple->AddParticle( sizeof( SimpleParticle ), pSimple->GetPMaterial( VarArgs( "effects/muzzleflash%d_noz", random->RandomInt(1,4) ) ), offset );
			
		if ( pParticle == NULL )
			return;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= 0.01f;

		pParticle->m_vecVelocity.Init();

		pParticle->m_uchColor[0]	= 255;
		pParticle->m_uchColor[1]	= 255;
		pParticle->m_uchColor[2]	= 200+random->RandomInt(0,55);

		pParticle->m_uchStartAlpha	= 255;
		pParticle->m_uchEndAlpha	= 255;

		pParticle->m_uchStartSize	= ( (random->RandomFloat( 6.0f, 8.0f ) * (8-(i))/6) * flScale );
		pParticle->m_uchEndSize		= pParticle->m_uchStartSize;
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= 0.0f;
	}
}

//==================================================
// Purpose: 
// Input: 
//==================================================

void CTempEnts::MuzzleFlash_Pistol_NPC( int entityIndex, int attachmentIndex )
{
	FX_MuzzleEffectAttached( 0.5f, entityIndex, attachmentIndex, NULL, true );
}




//==================================================
// Purpose: 
// Input: 
//==================================================

void CTempEnts::MuzzleFlash_RPG_NPC( int entityIndex, int attachmentIndex )
{
	//Draw the cloud of fire
	FX_MuzzleEffectAttached( 1.5f, entityIndex, attachmentIndex );

}



void CTempEnts::RocketFlare( const Vector& pos )
{
	C_LocalTempEntity	*pTemp;
	const model_t		*model;
	int					nframeCount;

	model = (model_t *)engine->LoadModel( "sprites/animglow01.vmt" );
	if ( !model )
	{
		return;
	}

	nframeCount = modelinfo->GetModelFrameCount( model );

	pTemp = TempEntAlloc( pos, (model_t *)model );
	if ( !pTemp )
		return;

	pTemp->m_flFrameMax = nframeCount - 1;
	pTemp->SetRenderMode( kRenderGlow );
	pTemp->m_nRenderFX = kRenderFxNoDissipation;
	pTemp->tempent_renderamt = 255;
	pTemp->m_flFrameRate = 1.0;
	pTemp->m_flFrame = random->RandomInt( 0, nframeCount - 1);
	pTemp->m_flSpriteScale = 1.0;
	pTemp->SetAbsOrigin( pos );
	pTemp->die = gpGlobals->curtime + 0.01;
}


void CTempEnts::HL1EjectBrass( const Vector &vecPosition, const QAngle &angAngles, const Vector &vecVelocity, int nType )
{
	const model_t *pModel = NULL;

#if defined( HL1_CLIENT_DLL )
	switch ( nType )
	{
	case 0:
	default:
		pModel = m_pHL1Shell;
		break;
	case 1:
		pModel = m_pHL1ShotgunShell;
		break;
	}
#endif
	if ( pModel == NULL )
		return;

	C_LocalTempEntity	*pTemp = TempEntAlloc( vecPosition, ( model_t * ) pModel );

	if ( pTemp == NULL )
		return;

	switch ( nType )
	{
	case 0:
	default:
		pTemp->hitSound = BOUNCE_SHELL;
		break;
	case 1:
		pTemp->hitSound = BOUNCE_SHOTSHELL;
		break;
	}

	pTemp->m_nBody	= 0;
	pTemp->flags |= ( FTENT_COLLIDEWORLD | FTENT_FADEOUT | FTENT_GRAVITY | FTENT_ROTATE );

	pTemp->m_vecTempEntAngVelocity[0] = random->RandomFloat( -512,511 );
	pTemp->m_vecTempEntAngVelocity[1] = random->RandomFloat( -256,255 );
	pTemp->m_vecTempEntAngVelocity[2] = random->RandomFloat( -256,255 );

	//Face forward
	pTemp->SetAbsAngles( angAngles );

	pTemp->SetRenderMode( kRenderNormal );
	pTemp->tempent_renderamt	= 255;		// Set this for fadeout

	pTemp->SetVelocity( vecVelocity );

	pTemp->die = gpGlobals->curtime + 2.5;
}

#define SHELLTYPE_PISTOL	0
#define SHELLTYPE_RIFLE		1
#define SHELLTYPE_SHOTGUN	2


void CTempEnts::CSEjectBrass( const Vector &vecPosition, const QAngle &angVelocity, int nVelocity, int shellType, CBasePlayer *pShooter )
{
	const model_t *pModel = NULL;
	int hitsound = TE_BOUNCE_SHELL;

#if defined ( CSTRIKE_DLL ) || defined ( FF_DLL )

	switch( shellType )
	{
	default:
	case CS_SHELL_9MM:
		hitsound = TE_PISTOL_SHELL;
		pModel = m_pCS_9MMShell;
		break;
	case CS_SHELL_57:
		hitsound = TE_PISTOL_SHELL;
		pModel = m_pCS_57Shell;
		break;
	case CS_SHELL_12GAUGE:
		hitsound = TE_SHOTGUN_SHELL;
		pModel = m_pCS_12GaugeShell;
		break;
	case CS_SHELL_556:
		hitsound = TE_RIFLE_SHELL;
		pModel = m_pCS_556Shell;
		break;
	case CS_SHELL_762NATO:
		hitsound = TE_RIFLE_SHELL;
		pModel = m_pCS_762NATOShell;
		break;
	case CS_SHELL_338MAG:
		hitsound = TE_RIFLE_SHELL;
		pModel = m_pCS_338MAGShell;
		break;
	case FF_SHELL_40MM:
		hitsound = TE_RIFLE_SHELL;
		pModel = m_pFF_40MMShell;
	}
#endif

	if ( pModel == NULL )
		return;

	Vector forward, right, up;
	Vector velocity;
	Vector origin;
	QAngle angle;
	
	// Add some randomness to the velocity

	AngleVectors( angVelocity, &forward, &right, &up );
	
	velocity = forward * nVelocity * random->RandomFloat( 1.2, 2.8 ) +
			   up * random->RandomFloat( -10, 10 ) +
			   right * random->RandomFloat( -20, 20 );

	if( pShooter )
		velocity += pShooter->GetAbsVelocity();

	C_LocalTempEntity *pTemp = TempEntAlloc( vecPosition, ( model_t * )pModel );
	if ( !pTemp )
		return;

	if( pShooter )
		pTemp->SetAbsAngles( pShooter->EyeAngles() );
	else
		pTemp->SetAbsAngles( vec3_angle );

	pTemp->SetVelocity( velocity );

	pTemp->hitSound = hitsound;

	pTemp->SetGravity( 0.4 );

	pTemp->m_nBody	= 0;
	pTemp->flags = FTENT_FADEOUT | FTENT_GRAVITY | FTENT_COLLIDEALL | FTENT_HITSOUND | FTENT_ROTATE | FTENT_CHANGERENDERONCOLLIDE;

	pTemp->m_vecTempEntAngVelocity[0] = random->RandomFloat(-256,256);
	pTemp->m_vecTempEntAngVelocity[1] = random->RandomFloat(-256,256);
	pTemp->m_vecTempEntAngVelocity[2] = 0;
#if defined ( CSTRIKE_DLL ) || defined ( FF_DLL )
	if (shellType == FF_SHELL_40MM)
		pTemp->m_vecTempEntAngVelocity = QAngle(0, 0, 0);
#endif

	pTemp->SetRenderMode( kRenderNormal );
	pTemp->tempent_renderamt = 255;
	
	pTemp->die = gpGlobals->curtime + cl_brasstime.GetFloat();

	bool bViewModelBrass = false;

	if ( pShooter && pShooter->GetObserverMode() == OBS_MODE_IN_EYE )
	{
		// we are spectating the shooter in first person view
		pShooter = ToBasePlayer( pShooter->GetObserverTarget() );
		bViewModelBrass = true;
	}

	if ( pShooter )
	{
		pTemp->clientIndex = pShooter->entindex();
		bViewModelBrass |= pShooter->IsLocalPlayer();
	}
	else
	{
		pTemp->clientIndex = 0;
	}

	if ( bViewModelBrass )
	{
		// for viewmodel brass put it in the viewmodel renderer group
		pTemp->m_RenderGroup = RENDER_GROUP_VIEW_MODEL_OPAQUE;
	}
}

// --> Mirv: A FF projectile
void CTempEnts::FFProjectile(const Vector &vecPosition, const QAngle &angVelocity, int iSpeed, int projectileType, int entIndex)
{
	const model_t *pModel = NULL;
	int hitsound = TE_BOUNCE_SHELL;

	switch (projectileType)
	{
	default:
	case FF_PROJECTILE_NAIL:
		//hitsound = BOUNCE_METAL;
		pModel = m_pFF_Nail;
		break;

	case FF_PROJECTILE_DART:
		pModel = m_pFF_Dart;
		break;
	}

	if (pModel == NULL)
		return;

	Vector velocity;

	AngleVectors(angVelocity, &velocity);
	velocity *= iSpeed;

	// Allocate new temporary entity
	C_LocalTempEntity *pTemp = TempEntAlloc(vecPosition, (model_t *) pModel);

	if (!pTemp)
		return;

	pTemp->SetVelocity(velocity);
	pTemp->SetAbsAngles(angVelocity);

	pTemp->hitSound = hitsound;

	pTemp->SetGravity(0);

	pTemp->m_nBody = 0;
	pTemp->flags = FTENT_FFPROJECTILE; //FTENT_FADEOUT | /*FTENT_GRAVITY |*/ FTENT_COLLIDEKILL | FTENT_COLLIDEALL | FTENT_HITSOUND /*| FTENT_ROTATE | FTENT_CHANGERENDERONCOLLIDE*/;

	pTemp->m_vecTempEntAngVelocity = QAngle(0, 0, 0);

	pTemp->SetRenderMode(kRenderNormal);
	pTemp->tempent_renderamt = 255;

	pTemp->die = gpGlobals->curtime + 10;

	pTemp->clientIndex = entIndex;
}
// <-- Mirv