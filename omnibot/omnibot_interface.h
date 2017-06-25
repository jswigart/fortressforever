#ifndef __OMNIBOT_INTERFACE_H__
#define __OMNIBOT_INTERFACE_H__

class CFFInfoScript;

#include "Omni-Bot.h"
#include "FF_Config.h"
#include "TF_Messages.h"

#include "luabind/luabind.hpp"

class omnibot_interface
{
public:
	static void OnDLLInit();
	static void OnDLLShutdown();

	static void LevelInit();
	static bool InitBotInterface();
	static void ShutdownBotInterface();
	static void UpdateBotInterface();
	static void OmnibotCommand();

	static void Trigger( CBaseEntity *_ent, CBaseEntity *_activator, const char *_tagname, const char *_action );

	// Message Helpers
	static void Notify_GameStarted();
	static void Notify_GameEnded( int _winningteam );

	static void Notify_ChatMsg( CBasePlayer *_player, const char *_msg );
	static void Notify_TeamChatMsg( CBasePlayer *_player, const char *_msg );
	static void Notify_Spectated( CBasePlayer *_player, CBasePlayer *_spectated );

	static void Notify_ClientConnected( CBasePlayer *_player, bool _isbot, int _team = RANDOM_TEAM_IF_NO_TEAM, int _class = RANDOM_CLASS_IF_NO_CLASS );
	static void Notify_ClientDisConnected( CBasePlayer *_player );

	static void Notify_Hurt( CBasePlayer *_player, CBaseEntity *_attacker );
	static void Notify_Death( CBasePlayer *_player, CBaseEntity *_attacker, const char *_weapon );
	static void Notify_KilledSomeone( CBasePlayer *_player, CBaseEntity *_victim, const char *_weapon );

	static void Notify_Infected( CBasePlayer *_target, CBasePlayer *_infector );
	static void Notify_Cured( CBasePlayer *_target, CBasePlayer *_infector );
	static void Notify_BurnLevel( CBasePlayer *_target, CBasePlayer *_burner, int _burnlevel );

	static void Notify_ChangedTeam( CBasePlayer *_player, int _newteam );
	static void Notify_ChangedClass( CBasePlayer *_player, int _oldclass, int _newclass );

	static void Notify_Build_MustBeOnGround( CBasePlayer *_player, int _buildable );
	static void Notify_Build_CantBuild( CBasePlayer *_player, int _buildable );
	static void Notify_Build_AlreadyBuilt( CBasePlayer *_player, int _buildable );
	static void Notify_Build_NotEnoughAmmo( CBasePlayer *_player, int _buildable );
	static void Notify_Build_BuildCancelled( CBasePlayer *_player, int _buildable );

	static void Notify_CantDisguiseAsTeam( CBasePlayer *_player, int _disguiseTeam );
	static void Notify_CantDisguiseAsClass( CBasePlayer *_player, int _disguiseClass );
	static void Notify_Disguising( CBasePlayer *_player, int _disguiseTeam, int _disguiseClass );
	static void Notify_Disguised( CBasePlayer *_player, int _disguiseTeam, int _disguiseClass );
	static void Notify_DisguiseLost( CBasePlayer *_player );
	static void Notify_UnCloaked( CBasePlayer *_player );
	static void Notify_CantCloak( CBasePlayer *_player );
	static void Notify_Cloaked( CBasePlayer *_player );

	static void Notify_RadarDetectedEnemy( CBasePlayer *_player, CBaseEntity *_ent );
	static void Notify_RadioTagUpdate( CBasePlayer *_player, CBaseEntity *_ent );
	static void Notify_BuildableDamaged( CBasePlayer *_player, int _type, CBaseEntity *_buildableEnt );

	static void Notify_DispenserBuilding( CBasePlayer *_player, CBaseEntity *_buildEnt );
	static void Notify_DispenserBuilt( CBasePlayer *_player, CBaseEntity *_buildEnt );
	static void Notify_DispenserEnemyUsed( CBasePlayer *_player, CBaseEntity *_enemyUser );
	static void Notify_DispenserDestroyed( CBasePlayer *_player, CBaseEntity *_attacker );
	static void Notify_DispenserDetonated( CBasePlayer *_player );
	static void Notify_DispenserDismantled( CBasePlayer *_player );

	static void Notify_SentryUpgraded( CBasePlayer *_player, int _level );
	static void Notify_SentryBuilding( CBasePlayer *_player, CBaseEntity *_buildEnt );
	static void Notify_SentryBuilt( CBasePlayer *_player, CBaseEntity *_buildEnt );
	static void Notify_SentryDestroyed( CBasePlayer *_player, CBaseEntity *_attacker );
	static void Notify_SentryBuildCancel( CBasePlayer *_player );
	static void Notify_SentryDetonated( CBasePlayer *_player );
	static void Notify_SentryDismantled( CBasePlayer *_player );
	static void Notify_SentrySpottedEnemy( CBasePlayer *_player );
	static void Notify_SentryAimed( CBasePlayer *_player, CBaseEntity *_buildEnt, const Vector &_dir );

	static void Notify_DetpackBuilding( CBasePlayer *_player, CBaseEntity *_buildEnt );
	static void Notify_DetpackBuilt( CBasePlayer *_player, CBaseEntity *_buildEnt );
	static void Notify_DispenserBuildCancel( CBasePlayer *_player );
	static void Notify_DetpackDetonated( CBasePlayer *_player );

	static void Notify_DispenserSabotaged( CBasePlayer *_player, CBaseEntity *_saboteur );
	static void Notify_SentrySabotaged( CBasePlayer *_player, CBaseEntity *_saboteur );

	static void Notify_PlayerShoot( CBasePlayer *_player, int botweaponId, CBaseEntity *_projectile );
	static void Notify_PlayerUsed( CBasePlayer *_player, CBaseEntity *_entityUsed );

	static void Notify_GotSpannerArmor( CBasePlayer *_target, CBasePlayer *_engy, int _before, int _after );
	static void Notify_GaveSpannerArmor( CBasePlayer *_engy, CBasePlayer *_target, int _before, int _after );
	static void Notify_GotMedicHealth( CBasePlayer *_target, CBasePlayer *_medic, int _before, int _after );
	static void Notify_GaveMedicHealth( CBasePlayer *_medic, CBasePlayer *_target, int _before, int _after );
	static void Notify_GotDispenserAmmo( CBasePlayer *_player );
	static void Notify_Sound( CBaseEntity *_source, int _sndtype, const char *_name );
	static void Notify_ItemRemove( CBaseEntity *_entity );
	static void Notify_ItemRestore( CBaseEntity *_entity );
	static void Notify_ItemDropped( CBaseEntity *_entity );
	static void Notify_ItemPickedUp( CBaseEntity *_entity, CBaseEntity *_whodoneit );
	static void Notify_ItemRespawned( CBaseEntity *_entity );
	static void Notify_ItemReturned( CBaseEntity *_entity );

	static void Notify_FireOutput( const char *_entityname, const char *_output );

	static void BotSendTriggerEx( const char *_entityname, const char *_action );
	static void SendBotSignal( const char *_signal );

	static void ParseEntityInfo( EntityInfo& entInfo, const luabind::adl::object& table );
};

#endif
