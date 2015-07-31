//////////////////////////////////////////////////////////////////////////
// Bot-Related Includes
#include "cbase.h"
#include "ammodef.h"
#include "in_buttons.h"
#include "playerinfomanager.h"
#include "filesystem.h"
#include "Color.h"
#include "world.h"
#include "ff_item_flag.h"
#include "triggers.h"
#include "movevars_shared.h"
#include "nav.h"
#include "nav_ladder.h"

#include "ff_scriptman.h"
#include "ff_luacontext.h"
#include "ff_utils.h"
#include "ff_gamerules.h"

extern ConVar mp_prematch;

// Mirv: Just added this to stop all the redefinition warnings whenever i do a full recompile
#pragma warning(disable: 4005)

//#pragma optimize("", off)

#include <sstream>

#include "BotExports.h"
#include "omnibot_interface.h"
#include "omnibot_eventhandler.h"
#include "Omni-Bot_Events.h"

ConVar	omnibot_enable( "omnibot_enable", "1", FCVAR_ARCHIVE | FCVAR_PROTECTED );
ConVar	omnibot_path( "omnibot_path", "omni-bot", FCVAR_ARCHIVE | FCVAR_PROTECTED );
ConVar	omnibot_nav( "omnibot_nav", "1", FCVAR_ARCHIVE | FCVAR_PROTECTED );
ConVar	omnibot_debug( "omnibot_debug", "0", FCVAR_ARCHIVE | FCVAR_PROTECTED );

#define OMNIBOT_MODNAME "Fortress Forever"

extern IServerPluginHelpers *serverpluginhelpers;

//////////////////////////////////////////////////////////////////////////

struct BotGoalInfo
{
	char		mGoalName[ 64 ];
	char		mGoalType[ 64 ];
	int			mGoalTeam;
	edict_t *	mEntity;
};

const int		MAX_DEFERRED_GOALS = 64;
int				gDeferredGoalIndex = 0;
BotGoalInfo		gDeferredGoals[ MAX_DEFERRED_GOALS ] = {};
bool			gStarted = false;

struct BotSpawnInfo
{
	char			mName[ 64 ];
	int				mTeam;
	int				mClass;
	CFFInfoScript *	mSpawnPoint;
};

BotSpawnInfo gDeferredSpawn[ MAX_PLAYERS ] = { 0 };
int gDeferredBotSpawnIndex = 0;

//const int MAX_ENTITIES = 4096;
//BotEntity		m_EntityHandles[MAX_ENTITIES] = {DefaultBotEntity()};

inline void ConvertBit( int & srcValue, int & dstValue, int matchBit, int toBit )
{
	if ( srcValue & matchBit )
	{
		dstValue |= toBit;
		srcValue &= ~matchBit; // so we can debug bits we dont handle
	}
}

inline Vector Convert( const obVec3 & vec )
{
	return Vector( vec.x, vec.y, vec.z );
}

enum
{
	MAX_MODELS = 512
};
unsigned short	m_DeletedMapModels[ MAX_MODELS ] = {};
int				m_NumDeletedMapModels = 0;

//////////////////////////////////////////////////////////////////////////
void NormalizeAngles( QAngle& angles )
{
	// Normalize angles to -180 to 180 range
	for ( int i = 0; i < 3; i++ )
	{
		if ( angles[ i ] > 180.0 )
		{
			angles[ i ] -= 360.0;
		}
		else if ( angles[ i ] < -180.0 )
		{
			angles[ i ] += 360.0;
		}
	}
}
//////////////////////////////////////////////////////////////////////////

void Omnibot_Load_PrintMsg( const char *_msg )
{
	Msg( "Omni-bot Loading: %s\n", _msg );
}

void Omnibot_Load_PrintErr( const char *_msg )
{
	Warning( "Omni-bot Loading: %s\n", _msg );
}

//////////////////////////////////////////////////////////////////////////

CON_COMMAND( bot, "Omni-Bot Commands" )
{
	omnibot_interface::OmnibotCommand();
}

//-----------------------------------------------------------------

struct WeaponEnum
{
	const char *	name;
	TF_Weapon		id;
};

const WeaponEnum gWeapons[] =
{
	{ "ff_weapon_umbrella", TF_WP_UMBRELLA },
	{ "ff_weapon_crowbar", TF_WP_CROWBAR },
	{ "ff_weapon_medkit", TF_WP_MEDKIT },
	{ "ff_weapon_knife", TF_WP_KNIFE },
	{ "ff_weapon_spanner", TF_WP_SPANNER },
	{ "ff_weapon_shotgun", TF_WP_SHOTGUN },
	{ "ff_weapon_supershotgun", TF_WP_SUPERSHOTGUN },
	{ "ff_weapon_nailgun", TF_WP_NAILGUN },
	{ "ff_weapon_supernailgun", TF_WP_SUPERNAILGUN },
	{ "ff_weapon_grenadelauncher", TF_WP_GRENADE_LAUNCHER },
	{ "ff_weapon_rpg", TF_WP_ROCKET_LAUNCHER },
	{ "ff_weapon_sniperrifle", TF_WP_SNIPER_RIFLE },
	{ "ff_weapon_railgun", TF_WP_RAILGUN },
	{ "ff_weapon_flamethrower", TF_WP_FLAMETHROWER },
	{ "ff_weapon_assaultcannon", TF_WP_MINIGUN },
	{ "ff_weapon_autorifle", TF_WP_AUTORIFLE },
	{ "ff_weapon_tranq", TF_WP_DARTGUN },
	{ "ff_weapon_pipelauncher", TF_WP_PIPELAUNCHER },
	{ "ff_weapon_ic", TF_WP_NAPALMCANNON },
	{ "ff_weapon_tommygun", TF_WP_TOMMYGUN },
	{ "ff_weapon_deploysentrygun", TF_WP_DEPLOY_SG },
	{ "ff_weapon_deploydispenser", TF_WP_DEPLOY_DISP },
	{ "ff_weapon_deploydetpack", TF_WP_DEPLOY_DETP },
	{ "ff_weapon_deploymancannon", TF_WP_DEPLOY_JUMPPAD },	
};
const size_t gNumWeapons = ARRAYSIZE( gWeapons );

int obUtilGetWeaponId( const char *_weaponName )
{
	if ( _weaponName )
	{
		for ( int i = 0; i < gNumWeapons; ++i )
		{
			if ( !Q_strcmp( gWeapons[ i ].name, _weaponName ) )
				return gWeapons[ i ].id;
		}
	}
	return TF_WP_NONE;
}

const char *obUtilGetStringFromWeaponId( int _weaponId )
{
	for ( int i = 0; i < gNumWeapons; ++i )
	{
		if ( gWeapons[ i ].id == _weaponId )
			return gWeapons[ i ].name;
	}
	return 0;
}

const int obUtilGetBotTeamFromGameTeam( int _team )
{
	switch ( _team )
	{
	case TEAM_BLUE:
		return TF_TEAM_BLUE;
	case TEAM_RED:
		return TF_TEAM_RED;
	case TEAM_YELLOW:
		return TF_TEAM_YELLOW;
	case TEAM_GREEN:
		return TF_TEAM_GREEN;
	}
	return TF_TEAM_NONE;
}

const int obUtilGetGameTeamFromBotTeam( int _team )
{
	switch ( _team )
	{
	case TF_TEAM_BLUE:
		return TEAM_BLUE;
	case TF_TEAM_RED:
		return TEAM_RED;
	case TF_TEAM_YELLOW:
		return TEAM_YELLOW;
	case TF_TEAM_GREEN:
		return TEAM_GREEN;
	}
	return TEAM_UNASSIGNED;
}

const int obUtilGetGameClassFromBotClass( int _class )
{
	switch ( _class )
	{
	case TF_CLASS_SCOUT:
		return CLASS_SCOUT;
	case TF_CLASS_SNIPER:
		return CLASS_SNIPER;
	case TF_CLASS_SOLDIER:
		return CLASS_SOLDIER;
	case TF_CLASS_DEMOMAN:
		return CLASS_DEMOMAN;
	case TF_CLASS_MEDIC:
		return CLASS_MEDIC;
	case TF_CLASS_HWGUY:
		return CLASS_HWGUY;
	case TF_CLASS_PYRO:
		return CLASS_PYRO;
	case TF_CLASS_SPY:
		return CLASS_SPY;
	case TF_CLASS_ENGINEER:
		return CLASS_ENGINEER;
	case TF_CLASS_CIVILIAN:
		return CLASS_CIVILIAN;
	}
	return -1;
}

const int obUtilGetBotClassFromGameClass( int _class )
{
	switch ( _class )
	{
	case CLASS_SCOUT:
		return TF_CLASS_SCOUT;
	case CLASS_SNIPER:
		return TF_CLASS_SNIPER;
	case CLASS_SOLDIER:
		return TF_CLASS_SOLDIER;
	case CLASS_DEMOMAN:
		return TF_CLASS_DEMOMAN;
	case CLASS_MEDIC:
		return TF_CLASS_MEDIC;
	case CLASS_HWGUY:
		return TF_CLASS_HWGUY;
	case CLASS_PYRO:
		return TF_CLASS_PYRO;
	case CLASS_SPY:
		return TF_CLASS_SPY;
	case CLASS_ENGINEER:
		return TF_CLASS_ENGINEER;
	case CLASS_CIVILIAN:
		return TF_CLASS_CIVILIAN;
	}
	return TF_CLASS_NONE;
}

const int obUtilGetBotWeaponFromGameWeapon( int _gameWpn )
{
	switch ( _gameWpn )
	{
	case FF_WEAPON_CROWBAR:
		return TF_WP_CROWBAR;
	case FF_WEAPON_KNIFE:
		return TF_WP_KNIFE;
	case FF_WEAPON_MEDKIT:
		return TF_WP_MEDKIT;
	case FF_WEAPON_SPANNER:
		return TF_WP_SPANNER;
	case FF_WEAPON_UMBRELLA:
		return TF_WP_UMBRELLA;
	case FF_WEAPON_SHOTGUN:
		return TF_WP_SHOTGUN;
	case FF_WEAPON_SUPERSHOTGUN:
		return TF_WP_SUPERSHOTGUN;
	case FF_WEAPON_NAILGUN:
		return TF_WP_NAILGUN;
	case FF_WEAPON_SUPERNAILGUN:
		return TF_WP_SUPERNAILGUN;
	case FF_WEAPON_GRENADELAUNCHER:
		return TF_WP_GRENADE_LAUNCHER;
	case FF_WEAPON_PIPELAUNCHER:
		return TF_WP_PIPELAUNCHER;
	case FF_WEAPON_AUTORIFLE:
		return TF_WP_AUTORIFLE;
	case FF_WEAPON_SNIPERRIFLE:
		return TF_WP_SNIPER_RIFLE;
	case FF_WEAPON_FLAMETHROWER:
		return TF_WP_FLAMETHROWER;
	case FF_WEAPON_IC:
		return TF_WP_NAPALMCANNON;
	case FF_WEAPON_RAILGUN:
		return TF_WP_RAILGUN;
	case FF_WEAPON_TRANQUILISER:
		return TF_WP_DARTGUN;
	case FF_WEAPON_ASSAULTCANNON:
		return TF_WP_MINIGUN;
	case FF_WEAPON_RPG:
		return TF_WP_ROCKET_LAUNCHER;
	case FF_WEAPON_DEPLOYDISPENSER:
		return TF_WP_DEPLOY_DISP;
	case FF_WEAPON_DEPLOYSENTRYGUN:
		return TF_WP_DEPLOY_SG;
	case FF_WEAPON_DEPLOYDETPACK:
		return TF_WP_DEPLOY_DETP;
	default:
		return TF_WP_NONE;
	}
}

const int obUtilGetGameWeaponFromBotWeapon( int _botWpn )
{
	switch ( _botWpn )
	{
	case TF_WP_CROWBAR:
		return FF_WEAPON_CROWBAR;
	case TF_WP_KNIFE:
		return FF_WEAPON_KNIFE;
	case TF_WP_MEDKIT:
		return FF_WEAPON_MEDKIT;
	case TF_WP_SPANNER:
		return FF_WEAPON_SPANNER;
	case TF_WP_UMBRELLA:
		return FF_WEAPON_UMBRELLA;
	case TF_WP_SHOTGUN:
		return FF_WEAPON_SHOTGUN;
	case TF_WP_SUPERSHOTGUN:
		return FF_WEAPON_SUPERSHOTGUN;
	case TF_WP_NAILGUN:
		return FF_WEAPON_NAILGUN;
	case TF_WP_SUPERNAILGUN:
		return FF_WEAPON_SUPERNAILGUN;
	case TF_WP_GRENADE_LAUNCHER:
		return FF_WEAPON_GRENADELAUNCHER;
	case TF_WP_PIPELAUNCHER:
		return FF_WEAPON_PIPELAUNCHER;
	case TF_WP_AUTORIFLE:
		return FF_WEAPON_AUTORIFLE;
	case TF_WP_SNIPER_RIFLE:
		return FF_WEAPON_SNIPERRIFLE;
	case TF_WP_FLAMETHROWER:
		return FF_WEAPON_FLAMETHROWER;
	case TF_WP_NAPALMCANNON:
		return FF_WEAPON_IC;
	case TF_WP_RAILGUN:
		return FF_WEAPON_RAILGUN;
	case TF_WP_DARTGUN:
		return FF_WEAPON_TRANQUILISER;
	case TF_WP_MINIGUN:
		return FF_WEAPON_ASSAULTCANNON;
	case TF_WP_ROCKET_LAUNCHER:
		return FF_WEAPON_RPG;
	case TF_WP_DEPLOY_DISP:
		return FF_WEAPON_DEPLOYDISPENSER;
	case TF_WP_DEPLOY_SG:
		return FF_WEAPON_DEPLOYSENTRYGUN;
	case TF_WP_DEPLOY_DETP:
		return FF_WEAPON_DEPLOYDETPACK;
	default:
		return FF_WEAPON_NONE;
	}
}

void Bot_Event_EntityCreated( CBaseEntity *pEnt );
void Bot_Event_EntityDeleted( CBaseEntity *pEnt );

edict_t* INDEXEDICT( int iEdictNum )
{
	return engine->PEntityOfEntIndex( iEdictNum );
}

int ENTINDEX( const edict_t *pEdict )
{
	return engine->IndexOfEdict( pEdict );
}

CBaseEntity *EntityFromHandle( GameEntity _ent )
{
	if ( !_ent.IsValid() )
		return NULL;
	CBaseHandle hndl( _ent.GetIndex(), _ent.GetSerial() );
	CBaseEntity *entity = CBaseEntity::Instance( hndl );
	return entity;
}


GameEntity HandleFromEntity( CBaseEntity *_ent )
{
	if ( _ent )
	{
		const CBaseHandle &hndl = _ent->GetRefEHandle();
		return GameEntity( hndl.GetEntryIndex(), hndl.GetSerialNumber() );
	}
	else
		return GameEntity();
}

GameEntity HandleFromEntity( edict_t *_ent )
{
	return HandleFromEntity( CBaseEntity::Instance( _ent ) );
}

//////////////////////////////////////////////////////////////////////////

class FFInterface : public IEngineInterface
{
public:
	int AddBot( const MessageHelper &_data )
	{
		OB_GETMSG( Msg_Addbot );

		int iClientNum = -1;

		edict_t *pEdict = engine->CreateFakeClient( pMsg->mName );
		if ( !pEdict )
		{
			PrintError( "Unable to Add Bot!" );
			return -1;
		}

		// Allocate a player entity for the bot, and call spawn
		CBasePlayer *pPlayer = ( (CBasePlayer*)CBaseEntity::Instance( pEdict ) );

		/*CFFPlayer *pFFPlayer = ToFFPlayer( pPlayer );
		if ( pFFPlayer && pMsg->mSpawnPointName[ 0 ] )
		{
			CBaseEntity *pSpawnPt = gEntList.FindEntityByName( NULL, pMsg->mSpawnPointName );
			if ( pSpawnPt )
				pFFPlayer->m_SpawnPointOverride = pSpawnPt;
			else
				Warning( "Bot Spawn Point Not Found: %s", pMsg->mSpawnPointName );
		}*/

		pPlayer->ClearFlags();
		pPlayer->AddFlag( FL_CLIENT | FL_FAKECLIENT );

		pPlayer->ChangeTeam( TEAM_UNASSIGNED );
		pPlayer->RemoveAllItems( true );
		pPlayer->Spawn();

		// Get the index of the bot.
		iClientNum = engine->IndexOfEdict( pEdict );

		//////////////////////////////////////////////////////////////////////////
		// Success!, return its client num.
		return iClientNum;
	}

	void RemoveBot( const MessageHelper &_data )
	{
		OB_GETMSG( Msg_Kickbot );
		if ( pMsg->mGameId != Msg_Kickbot::InvalidGameId )
		{
			CBasePlayer *ent = UTIL_PlayerByIndex( pMsg->mGameId );
			if ( ent && ent->IsBot() )
				engine->ServerCommand( UTIL_VarArgs( "kick %s\n", ent->GetPlayerName() ) );
		}
		else
		{
			CBasePlayer *ent = UTIL_PlayerByName( pMsg->mName );
			if ( ent && ent->IsBot() )
				engine->ServerCommand( UTIL_VarArgs( "kick %s\n", ent->GetPlayerName() ) );
		}
	}

	obResult ChangeTeam( int _client, int _newteam, const MessageHelper *_data )
	{
		edict_t *pEdict = INDEXEDICT( _client );

		if ( pEdict )
		{
			const char *pTeam = "auto";
			switch ( obUtilGetGameTeamFromBotTeam( _newteam ) )
			{
			case TEAM_BLUE:
				pTeam = "blue";
				break;
			case TEAM_RED:
				pTeam = "red";
				break;
			case TEAM_YELLOW:
				pTeam = "yellow";
				break;
			case TEAM_GREEN:
				pTeam = "green";
				break;
			default:
				{
					// pick a random available team
					int iRandTeam = UTIL_PickRandomTeam();
					switch ( iRandTeam )
					{
					case TEAM_BLUE:
						pTeam = "blue";
						break;
					case TEAM_RED:
						pTeam = "red";
						break;
					case TEAM_YELLOW:
						pTeam = "yellow";
						break;
					case TEAM_GREEN:
						pTeam = "green";
						break;
					}
					break;
				}
			}
			serverpluginhelpers->ClientCommand( pEdict, UTIL_VarArgs( "team %s", pTeam ) );
			return Success;
		}
		return InvalidEntity;
	}

	obResult ChangeClass( int _client, int _newclass, const MessageHelper *_data )
	{
		edict_t *pEdict = INDEXEDICT( _client );

		if ( pEdict )
		{
			CBaseEntity *pEntity = CBaseEntity::Instance( pEdict );
			CFFPlayer *pFFPlayer = dynamic_cast<CFFPlayer*>( pEntity );
			ASSERT( pFFPlayer );
			if ( pFFPlayer )
			{
				const char *pClassName = "randompc";
				switch ( obUtilGetGameClassFromBotClass( _newclass ) )
				{
				case CLASS_SCOUT:
					pClassName = "scout";
					break;
				case CLASS_SNIPER:
					pClassName = "sniper";
					break;
				case CLASS_SOLDIER:
					pClassName = "soldier";
					break;
				case CLASS_DEMOMAN:
					pClassName = "demoman";
					break;
				case CLASS_MEDIC:
					pClassName = "medic";
					break;
				case CLASS_HWGUY:
					pClassName = "hwguy";
					break;
				case CLASS_PYRO:
					pClassName = "pyro";
					break;
				case CLASS_SPY:
					pClassName = "spy";
					break;
				case CLASS_ENGINEER:
					pClassName = "engineer";
					break;
				case CLASS_CIVILIAN:
					pClassName = "civilian";
					break;
				default:
					{
						int iRandTeam = UTIL_PickRandomClass( pFFPlayer->GetTeamNumber() );
						pClassName = Class_IntToString( iRandTeam );
						break;
					}
				}
				serverpluginhelpers->ClientCommand( pEdict, UTIL_VarArgs( "class %s", pClassName ) );
				return Success;
			}
		}
		return InvalidEntity;
	}

	void UpdateBotInput( int _client, const ClientInput &_input )
	{
		edict_t *pEdict = INDEXENT( _client );
		CBaseEntity *pEntity = pEdict && !FNullEnt( pEdict ) ? CBaseEntity::Instance( pEdict ) : 0;
		CFFPlayer *pPlayer = pEntity ? ToFFPlayer( pEntity ) : 0;
		if ( pPlayer && pPlayer->IsBot() )
		{
			CBotCmd cmd;
			//CUserCmd cmd;

			// Process the bot keypresses.
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_ATTACK1 ) )
				cmd.buttons |= IN_ATTACK;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_ATTACK2 ) )
				cmd.buttons |= IN_ATTACK2;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_WALK ) )
				cmd.buttons |= IN_SPEED;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_USE ) )
				cmd.buttons |= IN_USE;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_JUMP ) )
				cmd.buttons |= IN_JUMP;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_CROUCH ) )
				cmd.buttons |= IN_DUCK;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_RELOAD ) )
				cmd.buttons |= IN_RELOAD;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_RESPAWN ) )
				cmd.buttons |= IN_ATTACK;

			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_AIM ) )
				cmd.buttons |= IN_ZOOM;

			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_GREN1 ) )
				serverpluginhelpers->ClientCommand( pEdict, "primeone" );
			else if ( pPlayer->IsGrenade1Primed() )
				serverpluginhelpers->ClientCommand( pEdict, "throwgren" );

			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_GREN2 ) )
				serverpluginhelpers->ClientCommand( pEdict, "primetwo" );
			else if ( pPlayer->IsGrenade2Primed() )
				serverpluginhelpers->ClientCommand( pEdict, "throwgren" );

			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_DROPITEM ) )
				serverpluginhelpers->ClientCommand( pEdict, "dropitems" );
			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_DROPAMMO ) )
				serverpluginhelpers->ClientCommand( pEdict, "discard" );

			if ( !pPlayer->IsBuilding() )
			{
				if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_BUILDSENTRY ) )
					serverpluginhelpers->ClientCommand( pEdict, "sentrygun" );
				if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_AIMSENTRY ) )
					serverpluginhelpers->ClientCommand( pEdict, "aimsentry" );
				if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_BUILDDISPENSER ) )
					serverpluginhelpers->ClientCommand( pEdict, "dispenser" );
				if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_BUILDDETPACK_5 ) )
					serverpluginhelpers->ClientCommand( pEdict, "detpack 5" );
				if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_BUILDDETPACK_10 ) )
					serverpluginhelpers->ClientCommand( pEdict, "detpack 10" );
				if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_BUILDDETPACK_20 ) )
					serverpluginhelpers->ClientCommand( pEdict, "detpack 20" );
				if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_BUILDDETPACK_30 ) )
					serverpluginhelpers->ClientCommand( pEdict, "detpack 30" );
			}
			else
			{
				if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_CANCELBUILD ) )
				{
					if ( pPlayer->IsBuilding() )
					{
						switch ( pPlayer->GetCurrentBuild() )
						{
						case FF_BUILD_DISPENSER: pPlayer->Command_BuildDispenser(); break;
						case FF_BUILD_SENTRYGUN: pPlayer->Command_BuildSentryGun(); break;
						case FF_BUILD_DETPACK: pPlayer->Command_BuildDetpack(); break; break;
						case FF_BUILD_MANCANNON: pPlayer->Command_BuildManCannon(); break;
						}
						return;
					}
				}
			}

			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_DETSENTRY ) )
				serverpluginhelpers->ClientCommand( pEdict, "detsentry" );
			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_DETDISPENSER ) )
				serverpluginhelpers->ClientCommand( pEdict, "detdispenser" );
			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_DETPIPES ) )
				cmd.buttons |= IN_ATTACK2;
			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_CALLFORMEDIC ) )
				serverpluginhelpers->ClientCommand( pEdict, "saveme" );
			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_CALLFORENGY ) )
				serverpluginhelpers->ClientCommand( pEdict, "engyme" );
			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_SABOTAGE_SENTRY ) )
				serverpluginhelpers->ClientCommand( pEdict, "sentrysabotage" );
			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_SABOTAGE_DISPENSER ) )
				serverpluginhelpers->ClientCommand( pEdict, "dispensersabotage" );
			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_CLOAK ) )
				serverpluginhelpers->ClientCommand( pEdict, "cloak" );
			if ( _input.mButtonFlags.CheckFlag( TF_BOT_BUTTON_SILENT_CLOAK ) )
				serverpluginhelpers->ClientCommand( pEdict, "scloak" );

			// Convert the facing vector to angles.
			const QAngle currentAngles = pPlayer->EyeAngles();
			Vector vFacing( _input.mFacing[ 0 ], _input.mFacing[ 1 ], _input.mFacing[ 2 ] );
			VectorAngles( vFacing, cmd.viewangles );
			NormalizeAngles( cmd.viewangles );

			// Any facings that go abive the clamp need to have their yaw fixed just in case.
			if ( cmd.viewangles[ PITCH ] > 89 || cmd.viewangles[ PITCH ] < -89 )
				cmd.viewangles[ YAW ] = currentAngles[ YAW ];

			//cmd.viewangles[PITCH] = clamp(cmd.viewangles[PITCH],-89,89);

			// Calculate the movement vector, taking into account the view direction.
			QAngle angle2d = cmd.viewangles; angle2d.x = 0;

			Vector vForward, vRight, vUp;
			Vector vMoveDir( _input.mMoveDir[ 0 ], _input.mMoveDir[ 1 ], _input.mMoveDir[ 2 ] );
			AngleVectors( angle2d, &vForward, &vRight, &vUp );

			const Vector worldUp( 0.f, 0.f, 1.f );
			cmd.forwardmove = vForward.Dot( vMoveDir ) * pPlayer->MaxSpeed();
			cmd.sidemove = vRight.Dot( vMoveDir ) * pPlayer->MaxSpeed();
			cmd.upmove = worldUp.Dot( vMoveDir ) * pPlayer->MaxSpeed();

			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_MOVEUP ) )
				cmd.upmove = 127;
			else if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_MOVEDN ) )
				cmd.upmove = -127;

			if ( cmd.sidemove > 0 )
				cmd.buttons |= IN_MOVERIGHT;
			else if ( cmd.sidemove < 0 )
				cmd.buttons |= IN_MOVELEFT;

			if ( pPlayer->IsOnLadder() )
			{
				if ( cmd.upmove > 0 )
					cmd.buttons |= IN_FORWARD;
				else if ( cmd.upmove < 0 )
					cmd.buttons |= IN_BACK;
			}
			else
			{
				if ( cmd.forwardmove > 0 )
					cmd.buttons |= IN_FORWARD;
				else if ( cmd.forwardmove < 0 )
					cmd.buttons |= IN_BACK;
			}

			// Do we have this weapon?
			const char *pNewWeapon = obUtilGetStringFromWeaponId( _input.mCurrentWeapon );
			CBaseCombatWeapon *pCurrentWpn = pPlayer->GetActiveWeapon();

			if ( pNewWeapon && ( !pCurrentWpn || !FStrEq( pCurrentWpn->GetClassname(), pNewWeapon ) ) )
			{
				CBaseCombatWeapon *pWpn = pPlayer->Weapon_OwnsThisType( pNewWeapon );
				if ( pWpn != pCurrentWpn )
				{
					pPlayer->Weapon_Switch( pWpn );
				}
			}

			pPlayer->GetBotController()->RunPlayerMove( &cmd );
			//pPlayer->ProcessUsercmds(&cmd, 1, 1, 0, false);
			pPlayer->GetBotController()->PostClientMessagesSent();
		}
	}

	void BotCommand( int _client, const char *_cmd )
	{
		edict_t *pEdict = INDEXENT( _client );
		if ( pEdict && !FNullEnt( pEdict ) )
		{
			serverpluginhelpers->ClientCommand( pEdict, _cmd );
		}
	}

	obBool IsInPVS( const float _pos[ 3 ], const float _target[ 3 ] )
	{
		Vector start( _pos[ 0 ], _pos[ 1 ], _pos[ 2 ] );
		Vector end( _target[ 0 ], _target[ 1 ], _target[ 2 ] );

		byte pvs[ MAX_MAP_CLUSTERS / 8 ];
		int iPVSCluster = engine->GetClusterForOrigin( start );
		int iPVSLength = engine->GetPVSForCluster( iPVSCluster, sizeof( pvs ), pvs );

		return engine->CheckOriginInPVS( end, pvs, iPVSLength ) ? True : False;
	}

	obResult TraceLine( obTraceResult &_result, const float _start[ 3 ], const float _end[ 3 ],
		const AABB *_pBBox, int _mask, int _user, obBool _bUsePVS )
	{
		Vector start( _start[ 0 ], _start[ 1 ], _start[ 2 ] );
		Vector end( _end[ 0 ], _end[ 1 ], _end[ 2 ] );

		byte pvs[ MAX_MAP_CLUSTERS / 8 ];
		int iPVSCluster = engine->GetClusterForOrigin( start );
		int iPVSLength = engine->GetPVSForCluster( iPVSCluster, sizeof( pvs ), pvs );

		bool bInPVS = _bUsePVS ? engine->CheckOriginInPVS( end, pvs, iPVSLength ) : true;
		if ( bInPVS )
		{
			int iMask = 0;
			Ray_t ray;
			trace_t trace;

			CTraceFilterWorldAndPropsOnly filterWorldPropsOnly;

			CBaseEntity *pIgnoreEnt = _user > 0 ? CBaseEntity::Instance( _user ) : 0;
			CTraceFilterSimple filterSimple( pIgnoreEnt, iMask );

			ITraceFilter * traceFilter = &filterSimple;

			// Set up the collision masks
			if ( _mask & TR_MASK_ALL )
				iMask |= MASK_ALL;
			else
			{
				if ( _mask & TR_MASK_SOLID )
					iMask |= MASK_SOLID;
				if ( _mask & TR_MASK_PLAYER )
					iMask |= MASK_PLAYERSOLID;
				if ( _mask & TR_MASK_SHOT )
					iMask |= MASK_SHOT;
				if ( _mask & TR_MASK_OPAQUE )
					iMask |= MASK_OPAQUE;
				if ( _mask & TR_MASK_WATER )
					iMask |= MASK_WATER;
				if ( _mask & TR_MASK_GRATE )
					iMask |= CONTENTS_GRATE;
				if ( _mask & TR_MASK_FLOODFILL )
				{
					traceFilter = &filterWorldPropsOnly;
					iMask |= ( MASK_NPCWORLDSTATIC | CONTENTS_PLAYERCLIP );
				}
				if ( _mask & TR_MASK_FLOODFILLENT )
				{
					iMask |= ( MASK_NPCWORLDSTATIC | CONTENTS_PLAYERCLIP );					
				}
			}

			filterSimple.SetCollisionGroup( iMask );

			// Initialize a ray with or without a bounds
			if ( _pBBox )
			{
				Vector mins( _pBBox->mMins[ 0 ], _pBBox->mMins[ 1 ], _pBBox->mMins[ 2 ] );
				Vector maxs( _pBBox->mMaxs[ 0 ], _pBBox->mMaxs[ 1 ], _pBBox->mMaxs[ 2 ] );
				ray.Init( start, end, mins, maxs );
			}
			else
			{
				ray.Init( start, end );
			}

			enginetrace->TraceRay( ray, iMask, traceFilter, &trace );

			if ( trace.DidHit() && trace.m_pEnt && ( trace.m_pEnt->entindex() != 0 ) )
				_result.mHitEntity = HandleFromEntity( trace.m_pEnt );
			else
				_result.mHitEntity = GameEntity();

			// Fill in the bot traceflag.			
			_result.mFraction = trace.fraction;
			_result.mStartSolid = trace.startsolid;
			_result.mEndpos[ 0 ] = trace.endpos.x;
			_result.mEndpos[ 1 ] = trace.endpos.y;
			_result.mEndpos[ 2 ] = trace.endpos.z;
			_result.mNormal[ 0 ] = trace.plane.normal.x;
			_result.mNormal[ 1 ] = trace.plane.normal.y;
			_result.mNormal[ 2 ] = trace.plane.normal.z;
			_result.mContents = ConvertValue( trace.contents, ConvertContentsFlags, ConvertGameToBot );
			return Success;
		}

		// No Hit or Not in PVS
		_result.mFraction = 0.0f;
		_result.mHitEntity = GameEntity();

		return bInPVS ? Success : OutOfPVS;
	}

	int GetPointContents( const float _pos[ 3 ] )
	{
		const int iContents = UTIL_PointContents( Vector( _pos[ 0 ], _pos[ 1 ], _pos[ 2 ] ) );
		return ConvertValue( iContents, ConvertContentsFlags, ConvertGameToBot );
	}

	virtual int ConvertValue( int value, ConvertType ctype, ConvertDirection cdir )
	{
		if ( cdir == ConvertGameToBot )
		{
			switch ( ctype )
			{
			case ConvertSurfaceFlags:
				{
					// clear flags we don't care about
					value &= ~( SURF_LIGHT | SURF_WARP | SURF_TRANS | SURF_TRIGGER |
						SURF_NODRAW | SURF_NOLIGHT | SURF_BUMPLIGHT | SURF_NOSHADOWS |
						SURF_NODECALS | SURF_NOCHOP );

					int iBotSurface = 0;
					ConvertBit( value, iBotSurface, SURF_SKY, SURFACE_SKY );
					ConvertBit( value, iBotSurface, SURF_SKIP, SURFACE_IGNORE );
					ConvertBit( value, iBotSurface, SURF_HINT, SURFACE_IGNORE );
					ConvertBit( value, iBotSurface, SURF_NODRAW, SURFACE_NODRAW );
					ConvertBit( value, iBotSurface, SURF_HITBOX, SURFACE_HITBOX );

					assert( value == 0 && "Unhandled flag" );
					return iBotSurface;
				}
			case ConvertContentsFlags:
				{
					value &= ~( CONTENTS_IGNORE_NODRAW_OPAQUE | CONTENTS_AREAPORTAL |
						CONTENTS_AREAPORTAL | CONTENTS_MONSTERCLIP |
						CONTENTS_CURRENT_0 | CONTENTS_CURRENT_90 |
						CONTENTS_CURRENT_180 | CONTENTS_CURRENT_270 |
						CONTENTS_CURRENT_UP | CONTENTS_CURRENT_DOWN |
						CONTENTS_ORIGIN | CONTENTS_MONSTER | CONTENTS_DEBRIS |
						CONTENTS_DETAIL | CONTENTS_TRANSLUCENT | CONTENTS_GRATE |
						CONTENTS_WINDOW | CONTENTS_AUX | LAST_VISIBLE_CONTENTS );

					int iBotContents = 0;
					ConvertBit( value, iBotContents, CONTENTS_SOLID, CONT_SOLID );
					ConvertBit( value, iBotContents, CONTENTS_WATER, CONT_WATER );
					ConvertBit( value, iBotContents, CONTENTS_SLIME, CONT_SLIME );
					ConvertBit( value, iBotContents, CONTENTS_LADDER, CONT_LADDER );
					ConvertBit( value, iBotContents, CONTENTS_MOVEABLE, CONT_MOVER );
					ConvertBit( value, iBotContents, CONTENTS_PLAYERCLIP, CONT_PLYRCLIP );
					ConvertBit( value, iBotContents, CONTENTS_DETAIL, CONT_NONSOLID );
					ConvertBit( value, iBotContents, CONTENTS_TESTFOGVOLUME, CONT_FOG );
					ConvertBit( value, iBotContents, CONTENTS_HITBOX, CONT_HITBOX );

					assert( value == 0 && "Unhandled flag" );
					return iBotContents;
				}
			}
		}
		else
		{
			switch ( ctype )
			{
			case ConvertSurfaceFlags:
				{
					int iBotSurface = 0;
					ConvertBit( value, iBotSurface, SURFACE_SKY, SURF_SKY );
					ConvertBit( value, iBotSurface, SURFACE_IGNORE, SURF_SKIP );
					ConvertBit( value, iBotSurface, SURFACE_NODRAW, SURF_NODRAW );
					ConvertBit( value, iBotSurface, SURFACE_HITBOX, SURF_HITBOX );

					assert( value == 0 && "Unhandled flag" );
					return iBotSurface;
				}
			case ConvertContentsFlags:
				{
					int iBotContents = 0;
					ConvertBit( value, iBotContents, CONT_SOLID, CONTENTS_SOLID );
					ConvertBit( value, iBotContents, CONT_WATER, CONTENTS_WATER );
					ConvertBit( value, iBotContents, CONT_SLIME, CONTENTS_SLIME );
					ConvertBit( value, iBotContents, CONT_LADDER, CONTENTS_LADDER );
					ConvertBit( value, iBotContents, CONT_MOVER, CONTENTS_MOVEABLE );
					ConvertBit( value, iBotContents, CONT_PLYRCLIP, CONTENTS_PLAYERCLIP );
					ConvertBit( value, iBotContents, CONT_NONSOLID, CONTENTS_DETAIL );
					ConvertBit( value, iBotContents, CONT_FOG, CONTENTS_TESTFOGVOLUME );
					ConvertBit( value, iBotContents, CONT_HITBOX, CONTENTS_HITBOX );

					assert( value == 0 && "Unhandled flag" );
					return iBotContents;
				}
			}
		}
		assert( 0 && "Unhandled conversion" );
		return 0;
	}

	// Function: GetEntityForMapModel
	obResult GetEntityForMapModel( int mapModelId, GameEntity& entityOut )
	{		
		for ( int i = 0; i < m_NumDeletedMapModels; ++i )
		{
			if ( m_DeletedMapModels[ i ] == mapModelId )
			{
				return InvalidEntity;
			}
		}
		
		for ( CBaseEntity *pEntity = gEntList.FirstEnt(); pEntity != NULL; pEntity = gEntList.NextEnt( pEntity ) )
		{
			string_t mdlName = pEntity->GetModelName();
			if ( mdlName.ToCStr() && mdlName.ToCStr()[0] == '*' && mapModelId == atoi( &mdlName.ToCStr()[1] ) )
			{
				entityOut = HandleFromEntity( pEntity->edict() );
				return Success;
			}
		}

		if ( mapModelId == 0 )
		{
			entityOut = HandleFromEntity( GetWorldEntity() );
			return Success;
		}

		return Success;
	}

	obResult GetEntityForName( const char* entName, GameEntity& entityOut )
	{
		CBaseEntity * ent = gEntList.FirstEnt();
		while ( ent != NULL )
		{
			obStringBuffer nameBuffer;
			GetEntityName( HandleFromEntity( ent ), nameBuffer );

			if ( !Q_stricmp( entName, nameBuffer.mBuffer ) )
			{
				entityOut = HandleFromEntity( ent );
				return Success;
			}

			ent = gEntList.NextEnt( ent );
		}
		return InvalidEntity;
	}

	obResult GetWorldModel( GameModelInfo & modelOut, MemoryAllocator & alloc )
	{
		Q_snprintf( modelOut.mModelType, sizeof( modelOut.mModelType ), "bsp" );
		Q_snprintf( modelOut.mModelName, sizeof( modelOut.mModelName ), "maps/%s.bsp", GetMapName() );
		return GetModel( modelOut, alloc );
	}

	obResult GetEntityModel( const GameEntity _ent, GameModelInfo & modelOut, MemoryAllocator & alloc )
	{
		CBaseEntity *pEnt = EntityFromHandle( _ent );
		if ( pEnt )
		{
			GetEntityLocalAABB( _ent, modelOut.mAABB );

			const string_t mdlName = pEnt->GetModelName();

			Q_strncpy( modelOut.mModelName, mdlName.ToCStr(), sizeof( modelOut.mModelName ) );

			const int len = strlen( modelOut.mModelName );
			for ( int i = 0; i < len; ++i )
			{
				if ( modelOut.mModelName[ i ] == '.' )
					Q_strncpy( modelOut.mModelType, &modelOut.mModelName[ i + 1 ], sizeof( modelOut.mModelType ) );
			}
			
			if ( modelOut.mModelName[0] == '*' )
			{
				Q_strncpy( modelOut.mModelType, "submodel", sizeof( modelOut.mModelType ) );
				Q_strncpy( modelOut.mModelName, &modelOut.mModelName[ 1 ], sizeof( modelOut.mModelName ) );
				return Success;
			}

			return GetModel( modelOut, alloc );
		}
		return InvalidEntity;
	}

	obResult GetModel( GameModelInfo & modelOut, MemoryAllocator & alloc )
	{
		if ( !Q_stricmp( modelOut.mModelType, "mdl" ) )
		{
			const int modelIndex = engine->PrecacheModel( modelOut.mModelName, true );
			//const int modelIndex = modelinfo->GetModelIndex( modelOut.mModelName );
			const vcollide_t * collide = modelinfo->GetVCollide( modelIndex );
			if ( collide == NULL )
			{
				const model_t* mdl = modelinfo->GetModel( modelIndex );
				if ( mdl )
				{
					Vector mins( FLT_MAX, FLT_MAX, FLT_MAX ), maxs( -FLT_MAX, -FLT_MAX, -FLT_MAX );
					modelinfo->GetModelBounds( mdl, mins, maxs );

					if ( mins.x <= maxs.x && mins.y <= maxs.y && mins.z <= maxs.z )
					{
						modelOut.mAABB.Set( mins.Base(), maxs.Base() );
					}
				}
				return Success;
			}

			std::stringstream str;

			size_t baseVertBuffer = 0;

			for ( int c = 0; c < collide->solidCount; ++c )
			{
				Vector * outVerts;
				const int vertCount = physcollision->CreateDebugMesh( collide->solids[ c ], &outVerts );

				str << "# Vertices " << vertCount << std::endl;
				for ( unsigned short v = 0; v < vertCount; ++v )
				{
					const Vector & vert = outVerts[ v ];
					str << "v " << vert.x << " " << vert.y << " " << vert.z << std::endl;
				}
				str << std::endl;

				const int numTris = vertCount / 3;
				str << "# Faces " << numTris << std::endl;
				for ( unsigned short p = 0; p < numTris; ++p )
				{
					str << "f " << 
						( baseVertBuffer + p * 3 + 1 ) << " " << 
						( baseVertBuffer + p * 3 + 2 ) << " " << 
						( baseVertBuffer + p * 3 + 3 ) << std::endl;
				}
				str << std::endl;

				physcollision->DestroyDebugMesh( vertCount, outVerts );

				baseVertBuffer += vertCount;
			}

			modelOut.mDataBufferSize = str.str().length()+1;
			modelOut.mDataBuffer = alloc.AllocateMemory( modelOut.mDataBufferSize );
			memset( modelOut.mDataBuffer, 0, modelOut.mDataBufferSize );
			Q_strncpy( modelOut.mDataBuffer, str.str().c_str(), modelOut.mDataBufferSize );
			Q_strncpy( modelOut.mModelType, "obj", sizeof( modelOut.mModelType ) );
			return Success;
		}

		FileHandle_t file = filesystem->Open( modelOut.mModelName, "rb" );
		if ( FILESYSTEM_INVALID_HANDLE != file )
		{
			modelOut.mDataBufferSize = filesystem->Size( file );
			modelOut.mDataBuffer = alloc.AllocateMemory( modelOut.mDataBufferSize );

			filesystem->Read( modelOut.mDataBuffer, modelOut.mDataBufferSize, file );
			filesystem->Close( file );
			return Success;
		}
		return InvalidParameter;
	}

	GameEntity GetLocalGameEntity()
	{
		if ( !engine->IsDedicatedServer() )
		{
			CBasePlayer *localPlayer = UTIL_PlayerByIndex( 1 );
			if ( localPlayer )
				return HandleFromEntity( localPlayer );
		}
		return GameEntity();
	}

	obResult GetEntityInfo( const GameEntity _ent, EntityInfo& classInfo )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			classInfo = EntityInfo();
			if ( pEntity->GetOmnibotEntityType( classInfo ) )
				return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityEyePosition( const GameEntity _ent, float _pos[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			Vector vPos = pEntity->EyePosition();
			_pos[ 0 ] = vPos.x;
			_pos[ 1 ] = vPos.y;
			_pos[ 2 ] = vPos.z;
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityBonePosition( const GameEntity _ent, int _boneid, float _pos[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		CBasePlayer *pPlayer = pEntity ? pEntity->MyCharacterPointer() : 0;
		if ( pPlayer )
		{
			int iBoneIndex = -1;
			switch ( _boneid )
			{
			case BONE_TORSO:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_Spine3" );
				break;
			case BONE_PELVIS:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_Hips" );
				break;
			case BONE_HEAD:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_Head" );
				break;
			case BONE_RIGHTARM:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_RightForeArm" );
				break;
			case BONE_LEFTARM:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_LeftForeArm" );
				break;
			case BONE_RIGHTHAND:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_RightHand" );
				break;
			case BONE_LEFTHAND:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_LeftHand" );
				break;
			case BONE_RIGHTLEG:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_RightLeg" );
				break;
			case BONE_LEFTLEG:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_LeftLeg" );
				break;
			case BONE_RIGHTFOOT:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_RightFoot" );
				break;
			case BONE_LEFTFOOT:
				iBoneIndex = pPlayer->LookupBone( "ffSkel_LeftFoot" );
				break;
				//"ffSg_Yaw"
			}

			if ( iBoneIndex != -1 )
			{
				Vector vBonePos;
				QAngle boneAngle;

				pPlayer->GetBonePosition( iBoneIndex, vBonePos, boneAngle );

				_pos[ 0 ] = vBonePos.x;
				_pos[ 1 ] = vBonePos.y;
				_pos[ 2 ] = vBonePos.z;

				return Success;
			}
			return InvalidParameter;
		}
		return InvalidEntity;
	}

	obResult GetEntityOrientation( const GameEntity _ent, float _fwd[ 3 ], float _right[ 3 ], float _up[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			QAngle viewAngles = pEntity->EyeAngles();
			AngleVectors( viewAngles, (Vector*)_fwd, (Vector*)_right, (Vector*)_up );
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityVelocity( const GameEntity _ent, float _velocity[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			const Vector &vVelocity = pEntity->GetAbsVelocity();
			_velocity[ 0 ] = vVelocity.x;
			_velocity[ 1 ] = vVelocity.y;
			_velocity[ 2 ] = vVelocity.z;
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityPosition( const GameEntity _ent, float _pos[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			const Vector &vPos = pEntity->GetAbsOrigin();
			_pos[ 0 ] = vPos.x;
			_pos[ 1 ] = vPos.y;
			_pos[ 2 ] = vPos.z;
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityWorldAABB( const GameEntity _ent, AABB &_aabb )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			Vector vMins, vMaxs;

			CBasePlayer *pPlayer = pEntity->MyCharacterPointer();
			if ( pPlayer )
			{
				Vector vOrig = pPlayer->GetAbsOrigin();
				vMins = vOrig + pPlayer->GetPlayerMins();
				vMaxs = vOrig + pPlayer->GetPlayerMaxs();
			}
			else
			{
				if ( !pEntity->CollisionProp() || pEntity->entindex() == 0 )
					return InvalidEntity;

				pEntity->CollisionProp()->WorldSpaceAABB( &vMins, &vMaxs );
			}

			_aabb.mMins[ 0 ] = vMins.x;
			_aabb.mMins[ 1 ] = vMins.y;
			_aabb.mMins[ 2 ] = vMins.z;
			_aabb.mMaxs[ 0 ] = vMaxs.x;
			_aabb.mMaxs[ 1 ] = vMaxs.y;
			_aabb.mMaxs[ 2 ] = vMaxs.z;
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityWorldOBB( const GameEntity _ent, float *_center, float *_axis0, float *_axis1, float *_axis2, float *_extents )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			Vector vMins, vMaxs;

			CBasePlayer *pPlayer = ToBasePlayer( pEntity );
			if ( pPlayer )
			{
				Vector vOrig = pPlayer->GetAbsOrigin();
				vMins = vOrig + pPlayer->GetPlayerMins();
				vMaxs = vOrig + pPlayer->GetPlayerMaxs();
				const Vector center = ( vMins + vMaxs )*0.5f;
				const Vector size = vMaxs - vMins;

				_center[ 0 ] = center.x;
				_center[ 1 ] = center.y;
				_center[ 2 ] = center.z;

				QAngle viewAngles = pEntity->EyeAngles();
				AngleVectors( viewAngles, (Vector*)_axis0, (Vector*)_axis1, (Vector*)_axis2 );

				_extents[ 0 ] = size.x * 0.5f;
				_extents[ 1 ] = size.y * 0.5f;
				_extents[ 2 ] = size.z * 0.5f;
			}
			else
			{
				if ( !pEntity->CollisionProp() || pEntity->entindex() == 0 )
					return InvalidEntity;

				const Vector obbCenter = pEntity->CollisionProp()->OBBCenter();
				const Vector obbSize = pEntity->CollisionProp()->OBBSize();

				_center[ 0 ] = obbCenter.x;
				_center[ 1 ] = obbCenter.y;
				_center[ 2 ] = obbCenter.z;

				QAngle viewAngles = pEntity->EyeAngles();
				AngleVectors( viewAngles, (Vector*)_axis0, (Vector*)_axis1, (Vector*)_axis2 );

				_extents[ 0 ] = obbSize.x;
				_extents[ 1 ] = obbSize.y;
				_extents[ 2 ] = obbSize.z;
			}

			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityLocalAABB( const GameEntity _ent, AABB &_aabb )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			Vector vMins, vMaxs;

			CBasePlayer *pPlayer = ToBasePlayer( pEntity );
			if ( pPlayer )
			{
				vMins = pPlayer->GetPlayerMins();
				vMaxs = pPlayer->GetPlayerMaxs();
			}
			else
			{
				if ( !pEntity->CollisionProp() || pEntity->entindex() == 0 )
					return InvalidEntity;

				VectorCopy( pEntity->CollisionProp()->BoundsMins(), vMins );
				VectorCopy( pEntity->CollisionProp()->BoundsMaxs(), vMaxs );
			}

			_aabb.mMins[ 0 ] = vMins.x;
			_aabb.mMins[ 1 ] = vMins.y;
			_aabb.mMins[ 2 ] = vMins.z;
			_aabb.mMaxs[ 0 ] = vMaxs.x;
			_aabb.mMaxs[ 1 ] = vMaxs.y;
			_aabb.mMaxs[ 2 ] = vMaxs.z;
			return Success;
		}
		return InvalidEntity;
	}
	obResult GetEntityGroundEntity( const GameEntity _ent, GameEntity &moveent )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			CBaseEntity *pEnt = pEntity->GetGroundEntity();
			if ( pEnt && pEnt != GetWorldEntity() )
				moveent = HandleFromEntity( pEnt );
			return Success;
		}
		return InvalidEntity;
	}

	GameEntity GetEntityOwner( const GameEntity _ent )
	{
		GameEntity owner;

		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			switch ( pEntity->Classify() )
			{
			case CLASS_DISPENSER:
			case CLASS_SENTRYGUN:
			case CLASS_DETPACK:
				{
					CFFBuildableObject *pBuildable = static_cast<CFFBuildableObject*>( pEntity );
					CFFPlayer *pOwner = pBuildable->GetOwnerPlayer();
					if ( pOwner )
						owner = HandleFromEntity( pOwner );
					break;
				}
			default:
				{
					CBaseEntity *pOwner = pEntity->GetOwnerEntity();
					if ( pOwner )
						owner = HandleFromEntity( pOwner );
					break;
				}
			}
		}
		return owner;
	}

	int GetEntityTeam( const GameEntity _ent )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			return obUtilGetBotTeamFromGameTeam( pEntity->GetTeamNumber() );
		}
		return 0;
	}

	obResult GetEntityName( const GameEntity _ent, obStringBuffer& nameOut )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			CBasePlayer *pPlayer = ToBasePlayer( pEntity );
			if ( pPlayer )
			{
				const char* pName = pPlayer->GetPlayerName();
				Q_snprintf( nameOut.mBuffer, obStringBuffer::BUFFER_LENGTH, "%s", pName );
			}
			else
			{
				CBaseEntity* entParent = pEntity->GetParent();
				const char* entParentName = entParent ? entParent->GetEntityName().ToCStr() : NULL;

				const char* pName = pName = pEntity->GetEntityName().ToCStr();
				const char* cls = pEntity->GetClassname();
				const CBaseHandle &hndl = pEntity->GetRefEHandle();

				Q_snprintf( nameOut.mBuffer, obStringBuffer::BUFFER_LENGTH, 
					"%s%s%s_%d", 
					entParentName ? entParentName : "", 
					entParentName ? "_" : "",
					(pName && pName[0]) ? pName : cls, 
					hndl.GetEntryIndex() );
			}
			return Success;
		}
		return InvalidEntity;
	}

	int GetCurrentWeapons( const GameEntity ent, int weaponIds [], int maxWeapons )
	{
		CBaseEntity *pEntity = EntityFromHandle( ent );
		CBasePlayer *pPlayer = ToBasePlayer( pEntity );
		if ( pPlayer )
		{
			int weaponCount = 0;
			for ( int i = 0; i < pPlayer->WeaponCount() && weaponCount < maxWeapons; ++i )
			{
				CBaseCombatWeapon* wpn = pPlayer->GetWeapon( i );
				if ( wpn )
				{
					weaponIds[ weaponCount ] = obUtilGetWeaponId( wpn->GetClassname() );
					if ( weaponIds[ weaponCount ] != TF_WP_NONE )
						++weaponCount;
				}
			}
			return weaponCount;
		}
		return 0;
	}

	obResult GetCurrentWeaponClip( const GameEntity _ent, FireMode _mode, int &_curclip, int &_maxclip )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		CBasePlayer *pPlayer = pEntity ? pEntity->MyCharacterPointer() : 0;
		if ( pPlayer )
		{
			CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
			if ( pWeapon )
			{
				_curclip = pWeapon->Clip1();
				_maxclip = pWeapon->GetMaxClip1();
			}
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetCurrentAmmo( const GameEntity _ent, int _weaponId, FireMode _mode, int &_cur, int &_max )
	{
		_cur = 0;
		_max = 0;

		CBaseEntity *pEntity = EntityFromHandle( _ent );
		CFFPlayer *pPlayer = ToFFPlayer( pEntity );
		if ( pPlayer )
		{
			const char *weaponClass = obUtilGetStringFromWeaponId( _weaponId );
			if ( weaponClass )
			{
				CBaseCombatWeapon *pWpn = pPlayer->Weapon_OwnsThisType( weaponClass );
				if ( pWpn )
				{
					//const int iAmmoType = GetAmmoDef()->Index( _mode==Primary?pWpn->GetPrimaryAmmoType():pWpn->GetSecondaryAmmoType());

					_cur = pPlayer->GetAmmoCount( _mode == Primary ? pWpn->GetPrimaryAmmoType() : pWpn->GetSecondaryAmmoType() );
					//_cur = _mode==Primary?pWpn->GetPrimaryAmmoCount():pWpn->GetSecondaryAmmoCount();
					_max = GetAmmoDef()->MaxCarry( _mode == Primary ? pWpn->GetPrimaryAmmoType() : pWpn->GetSecondaryAmmoType() );
				}
			}
			return Success;
		}

		_cur = 0;
		_max = 0;

		return InvalidEntity;
	}

	int GetGameTime()
	{
		return int( gpGlobals->curtime * 1000.0f );
	}

	void GetGoals()
	{
	}

	void GetPlayerInfo( obPlayerInfo &info )
	{
		for ( int i = TEAM_BLUE; i <= TEAM_GREEN; ++i )
		{
			CFFTeam *pTeam = GetGlobalFFTeam( i );
			if ( pTeam->GetTeamLimits() > 0 )
				info.mAvailableTeams |= ( 1 << obUtilGetBotTeamFromGameTeam( i ) );
		}
		info.mMaxPlayers = gpGlobals->maxClients;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer	*pEnt = UTIL_PlayerByIndex( i );
			if ( pEnt )
			{
				GameEntity ge = HandleFromEntity( pEnt );
				GetEntityInfo( ge, info.mPlayers[ i ].mEntInfo );
				info.mPlayers[ i ].mTeam = GetEntityTeam( ge );
				info.mPlayers[ i ].mController = pEnt->IsBot() ? obPlayerInfo::Bot : obPlayerInfo::Human;
			}
		}
	}

	obResult InterfaceSendMessage( const MessageHelper &_data, const GameEntity _ent )
	{
		CBaseEntity *pEnt = EntityFromHandle( _ent );
		CBasePlayer *pPlayer = pEnt ? pEnt->MyCharacterPointer() : 0;

#pragma warning(default: 4062) // enumerator 'identifier' in switch of enum 'enumeration' is not handled
		switch ( _data.GetMessageId() )
		{
			///////////////////////
			// General Messages. //
			///////////////////////
		case GEN_MSG_ISALIVE:
			{
				OB_GETMSG( Msg_IsAlive );
				if ( pMsg )
				{
					pMsg->mIsAlive = pEnt && pEnt->IsAlive() && pEnt->GetHealth() > 0 ? True : False;
				}
				break;
			}
		case GEN_MSG_ISRELOADING:
			{
				OB_GETMSG( Msg_Reloading );
				if ( pMsg )
				{
					pMsg->mReloading = pPlayer && pPlayer->IsPlayingGesture( ACT_GESTURE_RELOAD ) ? True : False;
				}
				break;
			}
		case GEN_MSG_ISREADYTOFIRE:
			{
				OB_GETMSG( Msg_ReadyToFire );
				if ( pMsg )
				{
					CBaseCombatWeapon *pWeapon = pPlayer ? pPlayer->GetActiveWeapon() : 0;
					pMsg->mReady = pWeapon && ( pWeapon->m_flNextPrimaryAttack <= gpGlobals->curtime ) ? True : False;
				}
				break;
			}
		case GEN_MSG_ISALLIED:
			{
				OB_GETMSG( Msg_IsAllied );
				if ( pMsg )
				{
					CBaseEntity *pEntOther = EntityFromHandle( pMsg->mTargetEntity );
					if ( pEnt && pEntOther )
					{
						pMsg->mIsAllied = g_pGameRules->PlayerRelationship( pEnt, pEntOther ) != GR_NOTTEAMMATE ? True : False;
						if ( pMsg->mIsAllied && pEntOther->Classify() == CLASS_SENTRYGUN )
						{
							CFFSentryGun *pSentry = static_cast<CFFSentryGun*>( pEntOther );
							if ( pSentry->IsMaliciouslySabotaged() )
								pMsg->mIsAllied = False;
						}
					}
				}
				break;
			}
		case GEN_MSG_GETEQUIPPEDWEAPON:
			{
				OB_GETMSG( WeaponStatus );
				if ( pMsg )
				{
					CBaseCombatWeapon *pWeapon = pPlayer ? pPlayer->GetActiveWeapon() : 0;
					pMsg->mWeaponId = pWeapon ? obUtilGetWeaponId( pWeapon->GetName() ) : 0;
				}
				break;
			}
		case GEN_MSG_GETMOUNTEDWEAPON:
			{
				break;
			}		
		case GEN_MSG_GETFLAGSTATE:
			{
				OB_GETMSG( Msg_FlagState );
				if ( pMsg )
				{
					if ( pEnt && pEnt->Classify() == CLASS_INFOSCRIPT )
					{
						CFFInfoScript *pInfoScript = static_cast<CFFInfoScript*>( pEnt );
						if ( pInfoScript->IsReturned() )
							pMsg->mFlagState = S_FLAG_AT_BASE;
						else if ( pInfoScript->IsDropped() )
							pMsg->mFlagState = S_FLAG_DROPPED;
						else if ( pInfoScript->IsCarried() )
							pMsg->mFlagState = S_FLAG_CARRIED;
						else if ( pInfoScript->IsRemoved() )
							pMsg->mFlagState = S_FLAG_UNAVAILABLE;
						pMsg->mOwner = HandleFromEntity( pEnt->GetOwnerEntity() );
					}
				}
				break;
			}
		case GEN_MSG_GAMESTATE:
			{
				OB_GETMSG( Msg_GameState );
				if ( pMsg )
				{
					CFFGameRules *pRules = FFGameRules();
					if ( pRules )
					{
						pMsg->mTimeLeft = ( mp_timelimit.GetFloat() * 60.0f ) - gpGlobals->curtime;
						if ( pRules->HasGameStarted() )
						{
							if ( g_fGameOver && gpGlobals->curtime < pRules->m_flIntermissionEndTime )
								pMsg->mGameState = GAME_STATE_INTERMISSION;
							else
								pMsg->mGameState = GAME_STATE_PLAYING;
						}
						else
						{
							float flPrematch = pRules->GetRoundStart() + mp_prematch.GetFloat() * 60.0f;
							if ( gpGlobals->curtime < flPrematch )
							{
								float flTimeLeft = flPrematch - gpGlobals->curtime;
								if ( flTimeLeft < 10 )
									pMsg->mGameState = GAME_STATE_WARMUP_COUNTDOWN;
								else
									pMsg->mGameState = GAME_STATE_WARMUP;
							}
							else
							{
								pMsg->mGameState = GAME_STATE_WAITINGFORPLAYERS;
							}
						}
					}
				}
				break;
			}
		case GEN_MSG_GETWEAPONLIMITS:
			{
				WeaponLimits *pMsg = _data.Get<WeaponLimits>();
				if ( pMsg )
					pMsg->mLimited = False;
				break;
			}
		case GEN_MSG_GETMAXSPEED:
			{
				OB_GETMSG( Msg_PlayerMaxSpeed );
				if ( pMsg && pPlayer )
				{
					pMsg->mMaxSpeed = pPlayer->MaxSpeed();
				}
				break;
			}
		case GEN_MSG_ENTITYSTAT:
			{
				OB_GETMSG( Msg_EntityStat );
				if ( pMsg )
				{
					if ( pPlayer && !Q_strcmp( pMsg->mStatName, "kills" ) )
						pMsg->mResult = obUserData( pPlayer->FragCount() );
					else if ( pPlayer && !Q_strcmp( pMsg->mStatName, "deaths" ) )
						pMsg->mResult = obUserData( pPlayer->DeathCount() );
					else if ( pPlayer && !Q_strcmp( pMsg->mStatName, "score" ) )
						pMsg->mResult = obUserData( 0 ); // TODO:
				}
				break;
			}
		case GEN_MSG_TEAMSTAT:
			{
				OB_GETMSG( Msg_TeamStat );
				if ( pMsg )
				{
					CTeam *pTeam = GetGlobalTeam( obUtilGetGameTeamFromBotTeam( pMsg->mTeam ) );
					if ( pTeam )
					{
						if ( !Q_strcmp( pMsg->mStatName, "score" ) )
							pMsg->mResult = obUserData( pTeam->GetScore() );
						else if ( !Q_strcmp( pMsg->mStatName, "deaths" ) )
							pMsg->mResult = obUserData( pTeam->GetDeaths() );
					}
				}
				break;
			}
		case GEN_MSG_WPCHARGED:
			{
				OB_GETMSG( WeaponCharged );
				if ( pMsg && pPlayer )
				{
					pMsg->mIsCharged = True;
				}
				break;
			}
		case GEN_MSG_WPHEATLEVEL:
			{
				OB_GETMSG( WeaponHeatLevel );
				if ( pMsg && pPlayer )
				{
					CBaseCombatWeapon *pWp = pPlayer->GetActiveWeapon();
					if ( pWp )
					{
						pWp->GetHeatLevel( pMsg->mFireMode, pMsg->mCurrentHeat, pMsg->mMaxHeat );
					}
				}
				break;
			}
		case GEN_MSG_ENTITYKILL:
			{
				break;
			}
		case GEN_MSG_SERVERCOMMAND:
			{
				OB_GETMSG( Msg_ServerCommand );
				if ( pMsg && pMsg->mCommand[ 0 ] && sv_cheats->GetBool() )
				{
					const char *cmd = pMsg->mCommand;
					while ( *cmd && *cmd == ' ' )
						++cmd;
					if ( cmd && *cmd )
					{
						engine->ServerCommand( UTIL_VarArgs( "%s\n", cmd ) );
					}
				}
				break;
			}
		case GEN_MSG_PLAYSOUND:
			{
				struct LastSound
				{
					char m_SoundName[ 64 ];
				};
				static LastSound m_LastPlayerSound[ MAX_PLAYERS ] = {};

				OB_GETMSG( Event_PlaySound );
				if ( pPlayer )
					pPlayer->EmitSound( pMsg->mSoundName );
				/*else
				FFLib::BroadcastSound(pMsg->mSoundName);*/
				break;
			}
		case GEN_MSG_STOPSOUND:
			{
				OB_GETMSG( Event_StopSound );
				if ( pPlayer )
					pPlayer->StopSound( pMsg->mSoundName );
				/*else
				FFLib::BroadcastSound(pMsg->mSoundName);*/
				break;
			}
		case GEN_MSG_SCRIPTEVENT:
			{
				OB_GETMSG( Event_ScriptEvent );

				CFFLuaSC hEvent;
				if ( pMsg->mParam1[ 0 ] )
					hEvent.Push( pMsg->mParam1 );
				if ( pMsg->mParam2[ 0 ] )
					hEvent.Push( pMsg->mParam2 );
				if ( pMsg->mParam3[ 0 ] )
					hEvent.Push( pMsg->mParam3 );

				CBaseEntity *pEnt = NULL;
				if ( pMsg->mEntityName[ 0 ] )
					pEnt = gEntList.FindEntityByName( NULL, pMsg->mEntityName );
				_scriptman.RunPredicates_LUA( pEnt, &hEvent, pMsg->mFunctionName );
				break;
			}
		case GEN_MSG_MOVERAT:
			{
				OB_GETMSG( Msg_MoverAt );
				if ( pMsg )
				{
					Vector org(
						pMsg->mPosition[ 0 ],
						pMsg->mPosition[ 1 ],
						pMsg->mPosition[ 2 ] );
					Vector under(
						pMsg->mUnder[ 0 ],
						pMsg->mUnder[ 1 ],
						pMsg->mUnder[ 2 ] );

					trace_t tr;
					unsigned int iMask = MASK_PLAYERSOLID_BRUSHONLY;
					UTIL_TraceLine( org, under, iMask, NULL, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );

					if ( tr.DidHitNonWorldEntity() &&
						!tr.m_pEnt->IsPlayer() &&
						!tr.startsolid )
					{
						pMsg->mEntity = HandleFromEntity( tr.m_pEnt );
					}
				}
				break;
			}
		case GEN_MSG_GETCONTROLLINGTEAM:
			{
				OB_GETMSG( ControllingTeam );
				if ( pEnt != NULL )
				{
					// todo:
				}
				break;
			}
			//////////////////////////////////
			// Game specific messages next. //
			//////////////////////////////////
		case TF_MSG_GETBUILDABLES:
			{
				OB_GETMSG( TF_BuildInfo );
				if ( pMsg )
				{
					CFFPlayer *pFFPlayer = static_cast<CFFPlayer*>( pPlayer );
					if ( pFFPlayer )
					{
						CBaseAnimating *pSentry = pFFPlayer->GetSentryGun();
						pMsg->mSentryStats.mEntity = HandleFromEntity( pSentry );

						CBaseAnimating *pDispenser = pFFPlayer->GetDispenser();
						pMsg->mDispenserStats.mEntity = HandleFromEntity( pDispenser );

						CBaseAnimating *pDetpack = pFFPlayer->GetDetpack();
						pMsg->mDetpackStats.mEntity = HandleFromEntity( pDetpack );
					}
				}
				break;
			}
		case TF_MSG_PLAYERPIPECOUNT:
			{
				OB_GETMSG( TF_PlayerPipeCount );
				if ( pMsg && pEnt )
				{
					int iNumPipes = 0;
					CBaseEntity *pPipe = 0;
					while ( ( pPipe = gEntList.FindEntityByClassT( pPipe, CLASS_PIPEBOMB ) ) != NULL )
					{
						if ( pPipe->GetOwnerEntity() == pEnt )
							++iNumPipes;
					}

					pMsg->mNumPipes = iNumPipes;
					pMsg->mMaxPipes = 8;
				}
				break;
			}
		case TF_MSG_TEAMPIPEINFO:
			{
				OB_GETMSG( TF_TeamPipeInfo );
				if ( pMsg )
				{
					pMsg->mNumTeamPipes = 0;
					pMsg->mNumTeamPipers = 0;
					pMsg->mMaxPipesPerPiper = 8;
				}
				break;
			}
		case TF_MSG_CANDISGUISE:
			{
				OB_GETMSG( TF_DisguiseOptions );
				if ( pMsg )
				{
					const int iCheckTeam = obUtilGetGameTeamFromBotTeam( pMsg->mCheckTeam );
					for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
					{
						CFFTeam *pTeam = GetGlobalFFTeam( t );
						pMsg->mTeam[ obUtilGetBotTeamFromGameTeam( t ) ] =
							( pTeam && ( pTeam->GetTeamLimits() != -1 ) ) ? True : False;

						if ( pTeam && ( t == iCheckTeam ) )
						{
							for ( int c = CLASS_SCOUT; c <= CLASS_CIVILIAN; ++c )
							{
								pMsg->mClass[ obUtilGetBotClassFromGameClass( c ) ] =
									( pTeam->GetClassLimit( c ) != -1 ) ? True : False;
							}
						}
					}
				}
				break;
			}
		case TF_MSG_DISGUISE:
			{
				OB_GETMSG( TF_Disguise );
				if ( pMsg )
				{
					int iTeam = obUtilGetGameTeamFromBotTeam( pMsg->mDisguiseTeam );
					int iClass = obUtilGetGameClassFromBotClass( pMsg->mDisguiseClass );
					if ( iTeam != TEAM_UNASSIGNED && iClass != -1 )
					{
						serverpluginhelpers->ClientCommand( pPlayer->edict(),
							UTIL_VarArgs( "disguise %d %d", iTeam - 1, iClass ) );
					}
					else
					{
						return InvalidParameter;
					}
				}
				break;
			}
		case TF_MSG_CLOAK:
			{
				OB_GETMSG( TF_FeignDeath );
				if ( pMsg )
				{
					serverpluginhelpers->ClientCommand( pPlayer->edict(),
						pMsg->mSilent ? "scloak" : "cloak" );
				}
				break;
			}
		case TF_MSG_LOCKPOSITION:
			{
				OB_GETMSG( TF_LockPosition );
				if ( pMsg )
				{
					CBaseEntity *pEnt = EntityFromHandle( pMsg->mTargetPlayer );
					if ( pEnt )
					{
						if ( pMsg->mLock == True )
							pEnt->AddFlag( FL_FROZEN );
						else
							pEnt->RemoveFlag( FL_FROZEN );
						pMsg->mSucceeded = True;
					}
				}
				break;
			}
		case TF_MSG_HUDHINT:
			{
				OB_GETMSG( TF_HudHint );
				CBaseEntity *pEnt = EntityFromHandle( pMsg->mTargetPlayer );
				pPlayer = pEnt ? pEnt->MyCharacterPointer() : 0;
				if ( pMsg && ToFFPlayer( pPlayer ) )
				{
					FF_HudHint( ToFFPlayer( pPlayer ), 0, pMsg->mId, pMsg->mMessage );
				}
				break;
			}
		case TF_MSG_HUDMENU:
			{
				OB_GETMSG( TF_HudMenu );
				pEnt = EntityFromHandle( pMsg->mTargetPlayer );
				pPlayer = pEnt ? pEnt->MyCharacterPointer() : 0;
				if ( pMsg && ToFFPlayer( pPlayer ) )
				{
					KeyValues *kv = new KeyValues( "menu" );
					kv->SetString( "title", pMsg->mTitle );
					kv->SetInt( "level", pMsg->mLevel );
					kv->SetColor( "color", Color( pMsg->mColor.r(), pMsg->mColor.g(), pMsg->mColor.b(), pMsg->mColor.a() ) );
					kv->SetInt( "time", pMsg->mTimeOut );
					kv->SetString( "msg", pMsg->mMessage );

					for ( int i = 0; i < 10; ++i )
					{
						if ( pMsg->mOption[ i ][ 0 ] )
						{
							char num[ 10 ];
							Q_snprintf( num, sizeof( num ), "%i", i );
							KeyValues *item1 = kv->FindKey( num, true );
							item1->SetString( "msg", pMsg->mOption[ i ] );
							item1->SetString( "command", pMsg->mCommand[ i ] );
						}
					}

					DIALOG_TYPE type = DIALOG_MSG;
					switch ( pMsg->mMenuType )
					{
					case TF_HudMenu::GuiAlert:
						type = DIALOG_MSG; // just an on screen message
						break;
					case TF_HudMenu::GuiMenu:
						type = DIALOG_MENU; // an options menu
						break;
					case TF_HudMenu::GuiTextBox:
						type = DIALOG_TEXT; // a richtext dialog
						break;
					}
					//serverpluginhelpers->CreateMessage( pPlayer->edict(), type, kv, &g_ServerPlugin );
					kv->deleteThis();
				}
				break;
			}
		case TF_MSG_HUDTEXT:
			{
				OB_GETMSG( TF_HudText );
				pEnt = EntityFromHandle( pMsg->mTargetPlayer );
				pPlayer = pEnt ? pEnt->MyCharacterPointer() : 0;
				if ( pMsg && pMsg->mMessage[ 0 ] )
				{
					int iDest = HUD_PRINTCONSOLE;
					switch ( pMsg->mMessageType )
					{
					case TF_HudText::MsgConsole:
						{
							iDest = HUD_PRINTCONSOLE;
							break;
						}
					case TF_HudText::MsgHudCenter:
						{
							iDest = HUD_PRINTCENTER;
							break;
						}
					}
					ClientPrint( pPlayer, iDest, pMsg->mMessage );
				}
				break;
			}
		default:
			{
				assert( 0 && "Unknown Interface Message" );
				return InvalidParameter;
			}
		}
#pragma warning(disable: 4062) // enumerator 'identifier' in switch of enum 'enumeration' is not handled
		return Success;
	}

	bool DebugLine( const float _start[ 3 ], const float _end[ 3 ], const obColor &_color, float _time )
	{
		if ( debugoverlay )
		{
			Vector vStart( _start[ 0 ], _start[ 1 ], _start[ 2 ] );
			Vector vEnd( _end[ 0 ], _end[ 1 ], _end[ 2 ] );
			debugoverlay->AddLineOverlay( vStart,
				vEnd,
				_color.r(),
				_color.g(),
				_color.b(),
				false,
				_time );
		}
		return true;
	}

	bool DebugRadius( const float _pos[ 3 ], const float _radius, const obColor &_color, float _time )
	{
		if ( debugoverlay )
		{
			Vector center( _pos[ 0 ], _pos[ 1 ], _pos[ 2 ] + 4 );
			Vector point = Vector( _radius, 0.0, 0.0 );
			
			const int subDiv = 12;
			float fStepSize = 360.0f / (float)subDiv;
			for ( int i = 0; i < subDiv; ++i )
			{
				Vector newPoint;
				VectorYawRotate( point, fStepSize, newPoint );
				
				debugoverlay->AddLineOverlay(
					center + point,
					center + newPoint,
					_color.r(),
					_color.g(),
					_color.b(),
					false,
					_time );

				point = newPoint;
			}
		}
		return true;
	}

	bool DebugPolygon( const obVec3 *_verts, const int _numverts, const obColor &_color, float _time, int _flags )
	{
		if ( debugoverlay )
		{
			if ( _numverts >= 3 )
			{
				Vector
					p1( _verts[ 0 ].x, _verts[ 0 ].y, _verts[ 0 ].z ),
					p2( _verts[ 1 ].x, _verts[ 1 ].y, _verts[ 1 ].z ),
					p3( _verts[ 2 ].x, _verts[ 2 ].y, _verts[ 2 ].z );

				debugoverlay->AddTriangleOverlay( p3, p2, p1,
					_color.r(),
					_color.g(),
					_color.b(),
					_color.a(), _flags&IEngineInterface::DR_NODEPTHTEST, _time );
				debugoverlay->AddTriangleOverlay( p1, p2, p3,
					_color.r(),
					_color.g(),
					_color.b(),
					_color.a(), _flags&IEngineInterface::DR_NODEPTHTEST, _time );

				for ( int p = 3; p < _numverts; ++p )
				{
					p2 = p3;

					p3 = Vector( _verts[ p ].x, _verts[ p ].y, _verts[ p ].z );

					debugoverlay->AddTriangleOverlay( p3, p2, p1,
						_color.r(),
						_color.g(),
						_color.b(),
						_color.a(), _flags&IEngineInterface::DR_NODEPTHTEST, _time );
					debugoverlay->AddTriangleOverlay( p1, p2, p3,
						_color.r(),
						_color.g(),
						_color.b(),
						_color.a(), _flags&IEngineInterface::DR_NODEPTHTEST, _time );
				}
			}
		}
		return true;
	}

	void PrintError( const char *_error )
	{
		if ( _error )
			Warning( "%s\n", _error );
	}

	void PrintMessage( const char *_msg )
	{
		if ( _msg )
			Msg( "%s\n", _msg );
	}

	bool PrintScreenText( const float _pos[ 3 ], float _duration, const obColor &_color, const char *_msg )
	{
		if ( _msg )
		{
			float fVertical = 0.75;

			// Handle newlines
			char buffer[ 1024 ] = {};
			Q_strncpy( buffer, _msg, 1024 );
			char *pbufferstart = buffer;

			int line = 0;
			int iLength = Q_strlen( buffer );
			for ( int i = 0; i < iLength; ++i )
			{
				if ( buffer[ i ] == '\n' || buffer[ i + 1 ] == '\0' )
				{
					buffer[ i++ ] = 0;

					if ( _pos )
					{
						Vector vPosition( _pos[ 0 ], _pos[ 1 ], _pos[ 2 ] );
						debugoverlay->AddTextOverlayRGB( vPosition, line++, _duration, _color.rF(), _color.gF(), _color.bF(), _color.aF(), pbufferstart );
					}
					else
					{
						debugoverlay->AddScreenTextOverlay( 0.3f, fVertical, _duration,
							_color.r(), _color.g(), _color.b(), _color.a(), pbufferstart );
					}

					fVertical += 0.02f;
					pbufferstart = &buffer[ i ];
				}
			}
		}
		return true;
	}

	const char *GetMapName()
	{
		static char mapname[ 256 ] = { 0 };
		if ( gpGlobals->mapname.ToCStr() )
		{
			Q_snprintf( mapname, sizeof( mapname ), STRING( gpGlobals->mapname ) );
		}
		return mapname;
	}

	void GetMapExtents( AABB &_aabb )
	{
		memset( &_aabb, 0, sizeof( AABB ) );

		CWorld *world = GetWorldEntity();
		if ( world )
		{
			Vector mins, maxs;
			world->GetWorldBounds( mins, maxs );

			for ( int i = 0; i < 3; ++i )
			{
				_aabb.mMins[ i ] = mins[ i ];
				_aabb.mMaxs[ i ] = maxs[ i ];
			}
		}
	}

	GameEntity EntityFromID( const int _gameId )
	{
		CBaseEntity *pEnt = CBaseEntity::Instance( _gameId );
		return HandleFromEntity( pEnt );
	}

	GameEntity EntityByName( const char *_name )
	{
		CBaseEntity *pEnt = _name ? gEntList.FindEntityByName( NULL, _name, NULL ) : NULL;
		return HandleFromEntity( pEnt );
	}

	int IDFromEntity( const GameEntity _ent )
	{
		CBaseEntity *pEnt = EntityFromHandle( _ent );
		return pEnt ? ENTINDEX( pEnt->edict() ) : -1;
	}

	bool DoesEntityStillExist( const GameEntity &_hndl )
	{
		return _hndl.IsValid() ? EntityFromHandle( _hndl ) != NULL : false;
	}

	int GetAutoNavFeatures( AutoNavFeature *_feature, int _max )
	{
		Vector vForward, vRight, vUp;

		int iNumFeatures = 0;

		CBaseEntity *pEnt = gEntList.FirstEnt();
		while ( pEnt )
		{
			_feature[ iNumFeatures ].mEntityInfo = EntityInfo();

			if ( iNumFeatures >= _max )
				return iNumFeatures;

			Vector vPos = pEnt->GetAbsOrigin();
			AngleVectors( pEnt->GetAbsAngles(), &vForward, &vRight, &vUp );
			for ( int j = 0; j < 3; ++j )
			{
				_feature[ iNumFeatures ].mPosition[ j ] = vPos[ j ];
				_feature[ iNumFeatures ].mTargetPosition[ j ] = vPos[ j ];
				_feature[ iNumFeatures ].mFacing[ j ] = vForward[ j ];

				_feature[ iNumFeatures ].mBounds.mMins[ j ] = 0.f;
				_feature[ iNumFeatures ].mBounds.mMaxs[ j ] = 0.f;

				_feature[ iNumFeatures ].mTargetBounds.mMins[ j ] = 0.f;
				_feature[ iNumFeatures ].mTargetBounds.mMaxs[ j ] = 0.f;
			}

			//////////////////////////////////////////////////////////////////////////

			if ( FClassnameIs( pEnt, "info_player_coop" ) ||
				FClassnameIs( pEnt, "info_player_deathmatch" ) ||
				FClassnameIs( pEnt, "info_player_start" ) ||
				FClassnameIs( pEnt, "info_ff_teamspawn" ) )
			{
				_feature[ iNumFeatures ].mEntityInfo.mGroup = ENT_GRP_PLAYERSTART;
			}
			else if ( FClassnameIs( pEnt, "trigger_teleport" ) )
			{
				CBaseEntity *pTarget = pEnt->GetNextTarget();
				if ( pTarget )
				{
					Vector vTargetPos = pTarget->GetAbsOrigin();
					for ( int j = 0; j < 3; ++j )
					{
						_feature[ iNumFeatures ].mTargetPosition[ j ] = vTargetPos[ j ];
					}
					_feature[ iNumFeatures ].mEntityInfo.mGroup = ENT_GRP_TELEPORTER;
				}
			}
			else if ( FClassnameIs( pEnt, "info_ladder" ) )
			{
				CInfoLadder *pLadder = dynamic_cast<CInfoLadder*>( pEnt );
				if ( pLadder )
				{
					for ( int j = 0; j < 3; ++j )
					{
						_feature[ iNumFeatures ].mBounds.mMins[ j ] = pLadder->mins[ j ];
						_feature[ iNumFeatures ].mBounds.mMaxs[ j ] = pLadder->maxs[ j ];
					}
					_feature[ iNumFeatures ].mBounds.CenterBottom( _feature[ iNumFeatures ].mPosition );
					_feature[ iNumFeatures ].mBounds.CenterBottom( _feature[ iNumFeatures ].mTargetPosition );
					_feature[ iNumFeatures ].mEntityInfo.mGroup = ENT_GRP_LADDER;
				}
			}
			else if ( FClassnameIs( pEnt, "info_ff_script" ) )
			{
				CFFInfoScript *pInfo = dynamic_cast<CFFInfoScript*>( pEnt );
				if ( pInfo->GetBotGoalType() == omnibot_interface::kTrainerSpawn )
					_feature[ iNumFeatures ].mEntityInfo.mGroup = ENT_GRP_PLAYERSTART;
			}

			if ( _feature[ iNumFeatures ].mEntityInfo.mGroup != 0 )
			{
				++iNumFeatures;
			}
			pEnt = gEntList.NextEnt( pEnt );
		}
		return iNumFeatures;
	}

	const char *GetGameName()
	{
		return "Halflife 2";
	}

	const char *GetModName()
	{
		return g_pGameRules ? g_pGameRules->GetGameDescription() : "unknown";
	}

	const char *GetModVers()
	{
		static char buffer[ 256 ];
		engine->GetGameDir( buffer, 256 );
		return buffer;
	}

	const char *GetBotPath()
	{
		return Omnibot_GetLibraryPath();
	}

	const char *GetLogPath()
	{
		static char botPath[ 512 ] = { 0 };

		char buffer[ 512 ] = { 0 };
		filesystem->GetLocalPath(
			UTIL_VarArgs( "%s/%s", omnibot_path.GetString(), "omnibot_ff.dll" ), buffer, 512 );

		Q_ExtractFilePath( buffer, botPath, 512 );
		return botPath;
	}
};

//-----------------------------------------------------------------

void omnibot_interface::OnDLLInit()
{
	assert( !g_pEventHandler );
	if ( !g_pEventHandler )
	{
		g_pEventHandler = new omnibot_eventhandler;
		g_pEventHandler->ExtractEvents();
		g_pEventHandler->RegisterEvents();
	}
}

void omnibot_interface::OnDLLShutdown()
{
	if ( g_pEventHandler )
	{
		g_pEventHandler->UnRegisterEvents();
		delete g_pEventHandler;
		g_pEventHandler = 0;
	}
}

//-----------------------------------------------------------------

void omnibot_interface::OmnibotCommand()
{
	if ( IsOmnibotLoaded() )
	{
		Arguments args;
		for ( int i = 0; i < engine->Cmd_Argc(); ++i )
		{
			Q_strncpy( args.mArgs[ args.mNumArgs++ ], engine->Cmd_Argv( i ), Arguments::MaxArgLength );
		}
		gBotFunctions->ConsoleCommand( args );
	}
	else
		Warning( "Omni-bot Not Loaded\n" );
}

void omnibot_interface::Trigger( CBaseEntity *_ent, CBaseEntity *_activator, const char *_tagname, const char *_action )
{
	if ( IsOmnibotLoaded() )
	{
		TriggerInfo ti;
		ti.mEntity = HandleFromEntity( _ent );
		ti.mActivator = HandleFromEntity( _activator );
		Q_strncpy( ti.mAction, _action, TriggerBufferSize );
		Q_strncpy( ti.mTagName, _tagname, TriggerBufferSize );
		gBotFunctions->SendTrigger( ti );
	}
}
//////////////////////////////////////////////////////////////////////////

class OmnibotEntityListener : public IEntityListener
{
	virtual void OnEntityCreated( CBaseEntity *pEntity )
	{
	}
	virtual void OnEntitySpawned( CBaseEntity *pEntity )
	{
		Bot_Event_EntityCreated( pEntity );
	}
	virtual void OnEntityDeleted( CBaseEntity *pEntity )
	{
		Bot_Event_EntityDeleted( pEntity );
	}
};

OmnibotEntityListener gBotEntityListener;

//////////////////////////////////////////////////////////////////////////
// Interface Functions
void omnibot_interface::LevelInit()
{
	// done here because map loads before InitBotInterface is called.
}
bool omnibot_interface::InitBotInterface()
{
	if ( !omnibot_enable.GetBool() )
	{
		Msg( "Omni-bot Currently Disabled. Re-enable with cvar omnibot_enable\n" );
		return false;
	}

	/*if( !gameLocal.isServer )
	return false;*/

	Msg( "-------------- Omni-bot Init ----------------\n" );

	// Look for the bot dll.
	const int BUF_SIZE = 1024;
	char botFilePath[ BUF_SIZE ] = { 0 };
	char botPath[ BUF_SIZE ] = { 0 };

	filesystem->GetLocalPath(
		UTIL_VarArgs( "%s/%s", omnibot_path.GetString(), "omnibot_ff.dll" ), botFilePath, BUF_SIZE );
	Q_ExtractFilePath( botFilePath, botPath, BUF_SIZE );
	botPath[ strlen( botPath ) - 1 ] = 0;
	Q_FixSlashes( botPath );

	gGameFunctions = new FFInterface;
	omnibot_error err = Omnibot_LoadLibrary( FF_VERSION_LATEST, "omnibot_ff", Omnibot_FixPath( botPath ) );
	if ( err == BOT_ERROR_NONE )
	{
		gStarted = false;
		gEntList.RemoveListenerEntity( &gBotEntityListener );
		gEntList.AddListenerEntity( &gBotEntityListener );

		// add the initial set of entities
		CBaseEntity * ent = gEntList.FirstEnt();
		while ( ent != NULL )
		{
			Bot_Event_EntityCreated( ent );
			ent = gEntList.NextEnt( ent );
		}
	}
	Msg( "---------------------------------------------\n" );
	return err == BOT_ERROR_NONE;
}

void omnibot_interface::ShutdownBotInterface()
{
	gEntList.RemoveListenerEntity( &gBotEntityListener );
	if ( IsOmnibotLoaded() )
	{
		Msg( "------------ Omni-bot Shutdown --------------\n" );
		Notify_GameEnded( 0 );
		gBotFunctions->Shutdown();
		Omnibot_FreeLibrary();
		Msg( "Omni-bot Shut Down Successfully\n" );
		Msg( "---------------------------------------------\n" );
	}

	// Temp fix?
	if ( debugoverlay )
		debugoverlay->ClearAllOverlays();
}

void omnibot_interface::UpdateBotInterface()
{
	VPROF_BUDGET( "Omni-bot::Update", _T( "Omni-bot" ) );

	if ( IsOmnibotLoaded() )
	{
		static float serverGravity = 0.0f;
		if ( serverGravity != sv_gravity.GetFloat() )
		{
			Event_SystemGravity d = { -sv_gravity.GetFloat() };
			gBotFunctions->SendGlobalEvent( MessageHelper( GAME_GRAVITY, &d, sizeof( d ) ) );
			serverGravity = sv_gravity.GetFloat();
		}
		static bool cheatsEnabled = false;
		if ( sv_cheats->GetBool() != cheatsEnabled )
		{
			Event_SystemCheats d = { sv_cheats->GetBool() ? True : False };
			gBotFunctions->SendGlobalEvent( MessageHelper( GAME_CHEATS, &d, sizeof( d ) ) );
			cheatsEnabled = sv_cheats->GetBool();
		}
		//////////////////////////////////////////////////////////////////////////
		if ( !engine->IsDedicatedServer() )
		{
			for ( int i = 1; i <= gpGlobals->maxClients; i++ )
			{
				CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
				if ( pPlayer && !pPlayer->IsBot() &&
					( pPlayer->GetObserverMode() != OBS_MODE_ROAMING &&
					pPlayer->GetObserverMode() != OBS_MODE_DEATHCAM ) )
				{
					CBasePlayer *pSpectatedPlayer = ToBasePlayer( pPlayer->GetObserverTarget() );
					if ( pSpectatedPlayer )
					{
						Notify_Spectated( pPlayer, pSpectatedPlayer );
					}
				}
			}
		}
		//////////////////////////////////////////////////////////////////////////
		if ( !gStarted )
		{
			Notify_GameStarted();
			gStarted = true;
		}

		/*for ( int i = 0; i < gDeferredGoalIndex; ++i )
		{
			CBaseEntity *pGoalEnt = CBaseEntity::Instance( gDeferredGoals[ i ].mEntity );
			if ( pGoalEnt )
			{
				MapGoalDef goaldef;
				goaldef.Props.SetString( "Type", gDeferredGoals[ i ].mGoalType );
				goaldef.Props.SetEntity( "Entity", HandleFromEntity( pGoalEnt ) );
				goaldef.Props.SetInt( "Team", gDeferredGoals[ i ].mGoalTeam );
				goaldef.Props.SetString( "TagName", gDeferredGoals[ i ].mGoalName );
				goaldef.Props.SetInt( "InterfaceGoal", 1 );
				gBotFunctions->AddGoal( goaldef );
			}
		}
		gDeferredGoalIndex = 0;*/

		gBotFunctions->Update();
	}
}

//////////////////////////////////////////////////////////////////////////
// Message Helpers
void omnibot_interface::Notify_GameStarted()
{
	if ( !IsOmnibotLoaded() )
		return;

	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_STARTGAME ) );
}

void omnibot_interface::Notify_GameEnded( int _winningteam )
{
	if ( !IsOmnibotLoaded() )
		return;
	//obUtilGetBotTeamFromGameTeam( _winningteam );
	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_ENDGAME ) );
}

void omnibot_interface::Notify_ChatMsg( CBasePlayer *_player, const char *_msg )
{
	if ( !IsOmnibotLoaded() )
		return;

	Event_ChatMessage d;
	d.mWhoSaidIt = HandleFromEntity( _player );
	Q_strncpy( d.mMessage, _msg ? _msg : "<unknown>",
		sizeof( d.mMessage ) / sizeof( d.mMessage[ 0 ] ) );
	gBotFunctions->SendGlobalEvent( MessageHelper( PERCEPT_HEAR_GLOBALCHATMSG, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_TeamChatMsg( CBasePlayer *_player, const char *_msg )
{
	if ( !IsOmnibotLoaded() )
		return;

	Event_ChatMessage d;
	d.mWhoSaidIt = HandleFromEntity( _player );
	Q_strncpy( d.mMessage, _msg ? _msg : "<unknown>",
		sizeof( d.mMessage ) / sizeof( d.mMessage[ 0 ] ) );

	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );

		// Check player classes on this player's team
		if ( pPlayer && pPlayer->IsBot() && pPlayer->GetTeamNumber() == _player->GetTeamNumber() )
		{
			gBotFunctions->SendEvent( pPlayer->entindex(),
				MessageHelper( PERCEPT_HEAR_TEAMCHATMSG, &d, sizeof( d ) ) );
		}
	}
}

void omnibot_interface::Notify_Spectated( CBasePlayer *_player, CBasePlayer *_spectated )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_spectated->IsBot() )
		return;

	if ( _spectated && _spectated->IsBot() )
	{
		int iGameId = _spectated->entindex();
		Event_Spectated d = { _player->entindex() - 1 };
		gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_SPECTATED, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_ClientConnected( CBasePlayer *_player, bool _isbot, int _team, int _class )
{
	if ( !IsOmnibotLoaded() )
		return;

	int iGameId = _player->entindex();
	Event_SystemClientConnected d;
	d.mGameId = iGameId;
	d.mIsBot = _isbot ? True : False;
	d.mDesiredTeam = _team;
	d.mDesiredClass = _class;

	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_CLIENTCONNECTED, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_ClientDisConnected( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;

	int iGameId = _player->entindex();
	Event_SystemClientDisConnected d = { iGameId };
	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_CLIENTDISCONNECTED, &d, sizeof( d ) ) );

}

void omnibot_interface::Notify_Hurt( CBasePlayer *_player, CBaseEntity *_attacker )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_TakeDamage d = { HandleFromEntity( _attacker ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( PERCEPT_FEEL_PAIN, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_Death( CBasePlayer *_player, CBaseEntity *_attacker, const char *_weapon )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();

	Event_Death d;
	d.mWhoKilledMe = HandleFromEntity( _attacker );
	Q_strncpy( d.mMeansOfDeath, _weapon ? _weapon : "<unknown>", sizeof( d.mMeansOfDeath ) );
	gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_DEATH, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_KilledSomeone( CBasePlayer *_player, CBaseEntity *_victim, const char *_weapon )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_KilledSomeone d;
	d.mWhoIKilled = HandleFromEntity( _victim );
	Q_strncpy( d.mMeansOfDeath, _weapon ? _weapon : "<unknown>", sizeof( d.mMeansOfDeath ) );
	gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_KILLEDSOMEONE, &d, sizeof( d ) ) );

}

void omnibot_interface::Notify_ChangedTeam( CBasePlayer *_player, int _newteam )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_ChangeTeam d = { _newteam };
	gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_CHANGETEAM, &d, sizeof( d ) ) );

}

void omnibot_interface::Notify_ChangedClass( CBasePlayer *_player, int _oldclass, int _newclass )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_ChangeClass d = { _newclass };
	gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_CHANGECLASS, &d, sizeof( d ) ) );

}

void omnibot_interface::Notify_Build_MustBeOnGround( CBasePlayer *_player, int _buildable )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_BUILD_MUSTBEONGROUND ) );

}

void omnibot_interface::Notify_Build_CantBuild( CBasePlayer *_player, int _buildable )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	int iMsg = 0;
	switch ( _buildable )
	{
	case FF_BUILD_DETPACK:
		iMsg = TF_MSG_DETPACK_CANTBUILD;
		break;
	case FF_BUILD_DISPENSER:
		iMsg = TF_MSG_DISPENSER_CANTBUILD;
		break;
	case FF_BUILD_SENTRYGUN:
		iMsg = TF_MSG_SENTRY_CANTBUILD;
		break;
	}
	if ( iMsg != 0 )
	{
		gBotFunctions->SendEvent( iGameId, MessageHelper( iMsg ) );
	}
}

void omnibot_interface::Notify_Build_AlreadyBuilt( CBasePlayer *_player, int _buildable )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	int iMsg = 0;
	switch ( _buildable )
	{
	case FF_BUILD_DETPACK:
		iMsg = TF_MSG_DETPACK_ALREADYBUILT;
		break;
	case FF_BUILD_DISPENSER:
		iMsg = TF_MSG_DISPENSER_ALREADYBUILT;
		break;
	case FF_BUILD_SENTRYGUN:
		iMsg = TF_MSG_SENTRY_ALREADYBUILT;
		break;
	}
	if ( iMsg != 0 )
	{
		gBotFunctions->SendEvent( iGameId, MessageHelper( iMsg ) );
	}

}

void omnibot_interface::Notify_Build_NotEnoughAmmo( CBasePlayer *_player, int _buildable )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	int iMsg = 0;
	switch ( _buildable )
	{
	case FF_BUILD_DETPACK:
		iMsg = TF_MSG_DETPACK_NOTENOUGHAMMO;
		break;
	case FF_BUILD_DISPENSER:
		iMsg = TF_MSG_DISPENSER_NOTENOUGHAMMO;
		break;
	case FF_BUILD_SENTRYGUN:
		iMsg = TF_MSG_SENTRY_NOTENOUGHAMMO;
		break;
	}
	if ( iMsg != 0 )
	{
		gBotFunctions->SendEvent( iGameId, MessageHelper( iMsg ) );
	}
}

void omnibot_interface::Notify_Build_BuildCancelled( CBasePlayer *_player, int _buildable )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	int iMsg = 0;
	switch ( _buildable )
	{
	case FF_BUILD_DETPACK:
		iMsg = TF_MSG_DETPACK_BUILDCANCEL;
		break;
	case FF_BUILD_DISPENSER:
		iMsg = TF_MSG_DISPENSER_BUILDCANCEL;
		break;
	case FF_BUILD_SENTRYGUN:
		iMsg = TF_MSG_SENTRY_BUILDCANCEL;
		break;
	}
	if ( iMsg != 0 )
	{
		gBotFunctions->SendEvent( iGameId, MessageHelper( iMsg ) );
	}
}

void omnibot_interface::Notify_CantDisguiseAsTeam( CBasePlayer *_player, int _disguiseTeam )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_CantDisguiseTeamTF d = { obUtilGetBotTeamFromGameTeam( _disguiseTeam ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_CANTDISGUISE_AS_TEAM, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_CantDisguiseAsClass( CBasePlayer *_player, int _disguiseClass )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_CantDisguiseClass_TF d = { obUtilGetBotClassFromGameClass( _disguiseClass ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_CANTDISGUISE_AS_CLASS, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_Disguising( CBasePlayer *_player, int _disguiseTeam, int _disguiseClass )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_Disguise_TF d;
	d.mClassId = _disguiseClass;
	d.mTeamId = _disguiseTeam;
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DISGUISING, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_Disguised( CBasePlayer *_player, int _disguiseTeam, int _disguiseClass )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_Disguise_TF d;
	d.mClassId = _disguiseClass;
	d.mTeamId = _disguiseTeam;
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DISGUISING, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_DisguiseLost( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DISGUISE_LOST ) );
}

void omnibot_interface::Notify_UnCloaked( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_UNCLOAKED ) );
}

void omnibot_interface::Notify_CantCloak( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_CANT_CLOAK ) );
}

void omnibot_interface::Notify_Cloaked( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_CLOAKED ) );
}

void omnibot_interface::Notify_RadioTagUpdate( CBasePlayer *_player, CBaseEntity *_ent )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_RadarUpdate_TF d = { HandleFromEntity( _ent ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_RADIOTAG_UPDATE, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_BuildableDamaged( CBasePlayer *_player, int _type, CBaseEntity *_buildEnt )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iMsg = 0;
	switch ( _type )
	{
	case CLASS_DISPENSER:
		iMsg = TF_MSG_DISPENSER_DAMAGED;
		break;
	case CLASS_SENTRYGUN:
		iMsg = TF_MSG_SENTRY_DAMAGED;
		break;
	default:
		return;
	}

	if ( iMsg != 0 )
	{
		int iGameId = _player->entindex();
		Event_BuildableDamaged_TF d = { HandleFromEntity( _buildEnt ) };
		gBotFunctions->SendEvent( iGameId, MessageHelper( iMsg, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_DispenserBuilding( CBasePlayer *_player, CBaseEntity *_buildEnt )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_DispenserBuilding_TF d = { HandleFromEntity( _buildEnt ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DISPENSER_BUILDING, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_DispenserBuilt( CBasePlayer *_player, CBaseEntity *_buildEnt )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_DispenserBuilt_TF d = { HandleFromEntity( _buildEnt ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DISPENSER_BUILT, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_DispenserEnemyUsed( CBasePlayer *_player, CBaseEntity *_enemyUser )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_DispenserEnemyUsed_TF d = { HandleFromEntity( _enemyUser ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DISPENSER_ENEMYUSED, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_DispenserDestroyed( CBasePlayer *_player, CBaseEntity *_attacker )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_BuildableDestroyed_TF d = { HandleFromEntity( _attacker ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DISPENSER_DESTROYED, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_SentryUpgraded( CBasePlayer *_player, int _level )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_SentryUpgraded_TF d = { _level };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SENTRY_UPGRADED, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_SentryBuilding( CBasePlayer *_player, CBaseEntity *_buildEnt )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_SentryBuilding_TF d = { HandleFromEntity( _buildEnt ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SENTRY_BUILDING, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_SentryBuilt( CBasePlayer *_player, CBaseEntity *_buildEnt )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_SentryBuilt_TF d = { HandleFromEntity( _buildEnt ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SENTRY_BUILT, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_SentryDestroyed( CBasePlayer *_player, CBaseEntity *_attacker )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_BuildableDestroyed_TF d = { HandleFromEntity( _attacker ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SENTRY_DESTROYED, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_SentrySpottedEnemy( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_SentrySpotEnemy_TF d = { GameEntity() }; // TODO
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SENTRY_SPOTENEMY, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_SentryAimed( CBasePlayer *_player, CBaseEntity *_buildEnt, const Vector &_dir )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_SentryAimed_TF d;
	d.mSentry = HandleFromEntity( _buildEnt );
	d.mDirection[ 0 ] = _dir[ 0 ];
	d.mDirection[ 1 ] = _dir[ 1 ];
	d.mDirection[ 2 ] = _dir[ 2 ];
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SENTRY_AIMED, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_DetpackBuilding( CBasePlayer *_player, CBaseEntity *_buildEnt )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_DetpackBuilding_TF d = { HandleFromEntity( _buildEnt ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DETPACK_BUILDING, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_DetpackBuilt( CBasePlayer *_player, CBaseEntity *_buildEnt )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_DetpackBuilt_TF d = { HandleFromEntity( _buildEnt ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DETPACK_BUILT, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_DetpackDetonated( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DETPACK_DETONATED ) );
}

void omnibot_interface::Notify_DispenserSabotaged( CBasePlayer *_player, CBaseEntity *_saboteur )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_BuildableSabotaged_TF d = { HandleFromEntity( _saboteur ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SABOTAGED_DISPENSER, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_SentrySabotaged( CBasePlayer *_player, CBaseEntity *_saboteur )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_BuildableSabotaged_TF d = { HandleFromEntity( _saboteur ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SABOTAGED_SENTRY, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_DispenserDetonated( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DISPENSER_DETONATED ) );
}

void omnibot_interface::Notify_DispenserDismantled( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_DISPENSER_DISMANTLED ) );
}

void omnibot_interface::Notify_SentryDetonated( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SENTRY_DETONATED ) );
}

void omnibot_interface::Notify_SentryDismantled( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_SENTRY_DISMANTLED ) );
}

void omnibot_interface::Notify_PlayerShoot( CBasePlayer *_player, int botweaponId, CBaseEntity *_projectile )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_WeaponFire d = {};
	d.mWeaponId = botweaponId;
	d.mProjectile = HandleFromEntity( _projectile );
	d.mFireMode = Primary;
	gBotFunctions->SendEvent( iGameId, MessageHelper( ACTION_WEAPON_FIRE, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_PlayerUsed( CBasePlayer *_player, CBaseEntity *_entityUsed )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	CBasePlayer *pUsedPlayer = ToBasePlayer( _entityUsed );
	if ( pUsedPlayer && pUsedPlayer->IsBot() )
	{
		int iGameId = pUsedPlayer->entindex();
		Event_PlayerUsed d = { HandleFromEntity( _player ) };
		gBotFunctions->SendEvent( iGameId, MessageHelper( PERCEPT_FEEL_PLAYER_USE, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_GotSpannerArmor( CBasePlayer *_target, CBasePlayer *_engy, int _before, int _after )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_target->IsBot() )
		return;

	if ( _target && _target->IsBot() )
	{
		int iGameId = _target->entindex();
		Event_GotEngyArmor d = { HandleFromEntity( _engy ), _before, _after };
		gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_GOT_ENGY_ARMOR, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_GaveSpannerArmor( CBasePlayer *_engy, CBasePlayer *_target, int _before, int _after )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_engy->IsBot() )
		return;

	if ( _engy && _engy->IsBot() )
	{
		int iGameId = _target->entindex();
		Event_GaveEngyArmor d = { HandleFromEntity( _target ), _before, _after };
		gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_GAVE_ENGY_ARMOR, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_GotMedicHealth( CBasePlayer *_target, CBasePlayer *_medic, int _before, int _after )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_target->IsBot() )
		return;

	if ( _target && _target->IsBot() )
	{
		int iGameId = _target->entindex();
		Event_GotMedicHealth d = { HandleFromEntity( _medic ), _before, _after };
		gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_GOT_MEDIC_HEALTH, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_GaveMedicHealth( CBasePlayer *_medic, CBasePlayer *_target, int _before, int _after )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_medic->IsBot() )
		return;

	if ( _medic && _medic->IsBot() )
	{
		int iGameId = _target->entindex();
		Event_GaveMedicHealth d = { HandleFromEntity( _target ), _before, _after };
		gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_GAVE_MEDIC_HEALTH, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_Infected( CBasePlayer *_target, CBasePlayer *_infector )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_target->IsBot() )
		return;

	if ( _target && _target->IsBot() )
	{
		int iGameId = _target->entindex();
		Event_Infected d = { HandleFromEntity( _infector ) };
		gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_INFECTED, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_Cured( CBasePlayer *_curee, CBasePlayer *_curer )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_curee->IsBot() )
		return;

	if ( _curee && _curee->IsBot() )
	{
		int iGameId = _curee->entindex();
		Event_Cured d = { HandleFromEntity( _curer ) };
		gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_CURED, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_BurnLevel( CBasePlayer *_target, CBasePlayer *_burner, int _burnlevel )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_target->IsBot() )
		return;

	if ( _target && _target->IsBot() )
	{
		int iGameId = _target->entindex();
		Event_Burn d = { HandleFromEntity( _burner ), _burnlevel };
		gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_BURNLEVEL, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_GotDispenserAmmo( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	if ( _player )
	{
		int iGameId = _player->entindex();
		gBotFunctions->SendEvent( iGameId, MessageHelper( TF_MSG_GOT_DISPENSER_AMMO ) );
	}
}

void omnibot_interface::Notify_Sound( CBaseEntity *_source, int _sndtype, const char *_name )
{
	if ( IsOmnibotLoaded() )
	{
		Event_Sound d = {};
		d.mSource = HandleFromEntity( _source );
		d.mSoundType = _sndtype;
		Vector v = _source->GetAbsOrigin();
		d.mOrigin[ 0 ] = v[ 0 ];
		d.mOrigin[ 1 ] = v[ 1 ];
		d.mOrigin[ 2 ] = v[ 2 ];
		Q_strncpy( d.mSoundName, _name ? _name : "<unknown>", sizeof( d.mSoundName ) / sizeof( d.mSoundName[ 0 ] ) );
		gBotFunctions->SendGlobalEvent( MessageHelper( GAME_SOUND, &d, sizeof( d ) ) );
	}
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::Notify_GoalInfo( CBaseEntity *_entity, int _type, int _teamflags )
{
	//BotGoalInfo gi;

	////////////////////////////////////////////////////////////////////////////
	//const int iAllTeams = ( 1 << TF_TEAM_BLUE ) | ( 1 << TF_TEAM_RED ) | ( 1 << TF_TEAM_YELLOW ) | ( 1 << TF_TEAM_GREEN );
	//gi.mGoalTeam = _teamflags;
	////////////////////////////////////////////////////////////////////////////

	//if ( gi.mGoalTeam != 0 || _type == kTrainerSpawn )
	//{
	//	gi.mEntity = _entity->edict();
	//	const char *pName = _entity->GetName();
	//	Q_strncpy( gi.mGoalName, pName ? pName : "", sizeof( gi.mGoalName ) );

	//	switch ( _type )
	//	{
	//	case kBackPack_Grenades:
	//		{
	//			//Bot_Queue_EntityCreated( _entity );
	//			return;
	//		}
	//	case kBackPack_Health:
	//		{
	//			//Bot_Queue_EntityCreated( _entity );
	//			return;
	//		}
	//	case kBackPack_Armor:
	//		{
	//			//Bot_Queue_EntityCreated( _entity );
	//			return;
	//		}
	//	case kBackPack_Ammo:
	//		{
	//			//Bot_Queue_EntityCreated( _entity );
	//			return;
	//		}
	//	case kFlag:
	//		{
	//			Q_strncpy(gi.mGoalType,"flag",sizeof(gi.mGoalType));
	//			break;
	//		}
	//	case kFlagCap:
	//		{
	//			gi.mGoalTeam ^= iAllTeams;
	//			Q_strncpy(gi.mGoalType,"flagcap",sizeof(gi.mGoalType));
	//			break;
	//		}
	//	case kTrainerSpawn:
	//		{
	//			Q_strncpy(gi.mGoalType,"trainerspawn",sizeof(gi.mGoalType));
	//			break;
	//		}
	//	case kHuntedEscape:
	//		{
	//			Q_strncpy(gi.mGoalType,"huntedescape",sizeof(gi.mGoalType));
	//			break;
	//		}
	//	default:
	//		return;
	//	}

	//	if ( gDeferredGoalIndex < MAX_DEFERRED_GOALS - 1 )
	//	{
	//		gDeferredGoals[ gDeferredGoalIndex++ ] = gi;
	//	}
	//	else
	//	{
	//		gGameFunctions->PrintError( "Omni-bot: Out of deferred goal slots!" );
	//	}
	//}
	//else
	//{
	//	gGameFunctions->PrintError( "Invalid Goal Entity" );
	//}
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::Notify_ItemRemove( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetName(), "item_removed" );
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::Notify_ItemRestore( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetName(), "item_restored" );
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::Notify_ItemDropped( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetName(), "item_dropped" );
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::Notify_ItemPickedUp( CBaseEntity *_entity, CBaseEntity *_whodoneit )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, _whodoneit, _entity->GetName(), "item_pickedup" );
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::Notify_ItemRespawned( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetName(), "item_respawned" );
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::Notify_ItemReturned( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetName(), "item_returned" );
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::Notify_FireOutput( const char *_entityname, const char *_output )
{
	if ( !IsOmnibotLoaded() )
		return;

	CBaseEntity *pEnt = _entityname ? gEntList.FindEntityByName( NULL, _entityname, NULL ) : NULL;
	omnibot_interface::Trigger( pEnt, NULL, _entityname, _output );
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::BotSendTriggerEx( const char *_entityname, const char *_action )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( NULL, NULL, _entityname, _action );
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::SendBotSignal( const char *_signal )
{
	if ( !IsOmnibotLoaded() )
		return;

	Event_ScriptSignal d;
	memset( &d, 0, sizeof( d ) );
	Q_strncpy( d.mSignalName, _signal, sizeof( d.mSignalName ) );
	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_SCRIPTSIGNAL, &d, sizeof( d ) ) );
}

//////////////////////////////////////////////////////////////////////////

void Bot_Event_EntityCreated( CBaseEntity *pEnt )
{
	if ( pEnt && IsOmnibotLoaded() )
	{
		// Get common properties.
		Event_EntityCreated d;
		d.mEntity = HandleFromEntity( pEnt );
		if ( SUCCESS( gGameFunctions->GetEntityInfo( d.mEntity, d.mEntityInfo ) ) )
		{	
			gBotFunctions->SendGlobalEvent( MessageHelper( GAME_ENTITYCREATED, &d, sizeof( d ) ) );
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void Bot_Event_EntityDeleted( CBaseEntity *pEnt )
{
	if ( pEnt && IsOmnibotLoaded() )
	{
		if ( pEnt->GetCollideable() && pEnt->GetCollideable()->GetCollisionModelIndex() != -1 )
		{
			string_t mdlName = pEnt->GetModelName();
			if ( mdlName.ToCStr() && mdlName.ToCStr()[ 0 ] == '*' )
			{
				m_DeletedMapModels[ m_NumDeletedMapModels++ ] = atoi( &mdlName.ToCStr()[ 1 ] );
			}
		}

		Event_EntityDeleted d;
		d.mEntity = HandleFromEntity( pEnt );
		gBotFunctions->SendGlobalEvent( MessageHelper( GAME_ENTITYDELETED, &d, sizeof( d ) ) );
	}
}

//////////////////////////////////////////////////////////////////////////

void CFFPlayer::SendBotMessage( const char *_msg, const char *_d1, const char *_d2, const char *_d3 )
{
	if ( !IsBot() || !IsOmnibotLoaded() )
		return;

	Event_ScriptMessage d;
	memset( &d, 0, sizeof( d ) );
	Q_strncpy( d.mMessageName, _msg, sizeof( d.mMessageName ) );
	if ( _d1 ) Q_strncpy( d.mMessageData1, _d1, sizeof( d.mMessageData1 ) );
	if ( _d2 ) Q_strncpy( d.mMessageData2, _d2, sizeof( d.mMessageData2 ) );
	if ( _d3 ) Q_strncpy( d.mMessageData3, _d3, sizeof( d.mMessageData3 ) );
	gBotFunctions->SendEvent( entindex(), MessageHelper( MESSAGE_SCRIPTMSG, &d, sizeof( d ) ) );
}

//////////////////////////////////////////////////////////////////////////

void CFFSentryGun::SendStatsToBot( void )
{
	VPROF_BUDGET( "CFFSentryGun::SendStatsTobot", VPROF_BUDGETGROUP_FF_BUILDABLE );

	if ( !IsOmnibotLoaded() )
		return;

	CFFPlayer *pOwner = static_cast<CFFPlayer *>( m_hOwner.Get() );
	if ( pOwner && pOwner->IsBot() )
	{
		int iGameId = pOwner->entindex();

		const Vector &vPos = GetAbsOrigin();
		QAngle viewAngles = EyeAngles();

		Vector vFacing;
		AngleVectors( viewAngles, &vFacing, 0, 0 );

		Event_SentryStatus_TF d;
		d.mEntity = HandleFromEntity( this );
		d.mHealth = m_iHealth;
		d.mMaxHealth = m_iMaxHealth;
		d.mShells[ 0 ] = m_iShells;
		d.mShells[ 1 ] = m_iMaxShells;
		d.mRockets[ 0 ] = m_iRockets;
		d.mRockets[ 1 ] = m_iMaxRockets;
		d.mLevel = m_iLevel;
		d.mPosition[ 0 ] = vPos.x;
		d.mPosition[ 1 ] = vPos.y;
		d.mPosition[ 2 ] = vPos.z;
		d.mFacing[ 0 ] = vFacing.x;
		d.mFacing[ 1 ] = vFacing.y;
		d.mFacing[ 2 ] = vFacing.z;
		gBotFunctions->SendEvent( iGameId,
			MessageHelper( TF_MSG_SENTRY_STATS, &d, sizeof( d ) ) );
	}
}

void CFFDispenser::SendStatsToBot()
{
	VPROF_BUDGET( "CFFDispenser::SendStatsToBot", VPROF_BUDGETGROUP_FF_BUILDABLE );

	if ( !IsOmnibotLoaded() )
		return;

	CFFPlayer *pOwner = static_cast<CFFPlayer*>( m_hOwner.Get() );
	if ( pOwner && pOwner->IsBot() )
	{
		int iGameId = pOwner->entindex();

		const Vector &vPos = GetAbsOrigin();
		QAngle viewAngles = EyeAngles();

		Vector vFacing;
		AngleVectors( viewAngles, &vFacing, 0, 0 );

		Event_DispenserStatus_TF d;
		d.mEntity = HandleFromEntity( this );
		d.mHealth = m_iHealth;
		d.mShells = m_iShells;
		d.mNails = m_iNails;
		d.mRockets = m_iRockets;
		d.mCells = m_iCells;
		d.mArmor = m_iArmor;
		d.mPosition[ 0 ] = vPos.x;
		d.mPosition[ 1 ] = vPos.y;
		d.mPosition[ 2 ] = vPos.z;
		d.mFacing[ 0 ] = vFacing.x;
		d.mFacing[ 1 ] = vFacing.y;
		d.mFacing[ 2 ] = vFacing.z;
		gBotFunctions->SendEvent( iGameId,
			MessageHelper( TF_MSG_DISPENSER_STATS, &d, sizeof( d ) ) );
	}
}
