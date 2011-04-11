//	=============== Fortress Forever ==============
//	======== A modification for Half-Life 2 =======
//
//	@file ff_weapon_shield.cpp
//	@author Greg "GreenMushy" Stefanakis
//	@date Nov 28, 2010
//	@brief Prototype demoman shield weapon ( inactive weapon state )
//	===============================================

#include "cbase.h"
#include "ff_weapon_base.h"
#include "ff_fx_shared.h"
#include "in_buttons.h"


#ifdef CLIENT_DLL 
	#define CFFWeaponShield C_FFWeaponShield
	#define CFFShield C_FFShield

	#include "c_ff_player.h"
	#include "ff_hud_chat.h"
#else
	#include "ff_player.h"
#endif

//#include "ff_buildableobjects_shared.h"

//The value that the player's speed% is reduced to when actively using the shield
ConVar ffdev_shield_speed( "ffdev_shield_speed", "0.3", FCVAR_REPLICATED | FCVAR_NOTIFY);
#define FF_SHIELD_SPEEDEFFECT ffdev_shield_speed.GetFloat()

////Show the "buildable" object that is the physical shield in front of the player
//ConVar ffdev_shield_showmodel( "ffdev_shield_showmodel", "1", FCVAR_REPLICATED | FCVAR_NOTIFY );
//#define SHIELD_SHOWMODEL ffdev_shield_showmodel.GetBool()

//=============================================================================
// CFFWeaponShield
//=============================================================================

class CFFWeaponShield : public CFFWeaponBase
{
public:
	DECLARE_CLASS(CFFWeaponShield, CFFWeaponBase);
	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();
	
	CFFWeaponShield( void );
#ifdef CLIENT_DLL 
	~CFFWeaponShield( void ){ }
#endif

	void ShieldActive( void );	//Draw shield and start blocking
	void ShieldIdle( void );	//Holster shield and remove block
	virtual void ItemPostFrame( void );	//acts as the update function
	virtual bool Deploy();	//need this to override the base deploy() and put in an extra animation call
	virtual bool Holster(); // need this to override the base holster() and delete the shield in the world

	virtual FFWeaponID GetWeaponID( void ) const		{ return FF_WEAPON_SHIELD; }

	//Get the shield bool
	bool GetShieldActive(){ return m_bShieldActive; }

private:

	CFFWeaponShield(const CFFWeaponShield &);
	bool m_bShieldActive;//used to keep track of active shield to only call animations once per active/deactive

};

//=============================================================================
// CFFWeaponShield tables
//=============================================================================

IMPLEMENT_NETWORKCLASS_ALIASED(FFWeaponShield, DT_FFWeaponShield) 

BEGIN_NETWORK_TABLE(CFFWeaponShield, DT_FFWeaponShield) 
END_NETWORK_TABLE() 

BEGIN_PREDICTION_DATA(CFFWeaponShield) 
END_PREDICTION_DATA() 

LINK_ENTITY_TO_CLASS(ff_weapon_shield, CFFWeaponShield);
PRECACHE_WEAPON_REGISTER(ff_weapon_shield);

//=============================================================================
// CFFWeaponShield implementation
//=============================================================================

//----------------------------------------------------------------------------
// Purpose: Constructor
//----------------------------------------------------------------------------
CFFWeaponShield::CFFWeaponShield( void ) 
{
	//Start shield holstered
	m_bShieldActive = false;
}

//-----------------------------------------------------------------------------
// Purpose: A modified ItemPostFrame to allow for different cycledecrements
//-----------------------------------------------------------------------------
void CFFWeaponShield::ItemPostFrame()
{
	//Get the owner
	CFFPlayer *pPlayer = GetPlayerOwner();

	//If there is no player gtfo
	if( !pPlayer )
		return;

	//-------------------------
	//Pressing Attack
	//-------------------------
	if ((pPlayer->m_nButtons & IN_ATTACK || pPlayer->m_afButtonPressed & IN_ATTACK) /*&& (pPlayer->GetFlags() & FL_ONGROUND)*/ )
	{
		ShieldActive();
	}

	// -----------------------
	//  No buttons down
	// -----------------------
	//if (! ((pPlayer->m_nButtons & IN_ATTACK)))
	//{
		//ShieldIdle();
	//}
}
//----------------------------------------------------------------------------
// Purpose: Override the deploy call and immediatly start holstering it so its not "active"to the player's viewmodel
//----------------------------------------------------------------------------
bool CFFWeaponShield::Deploy()
{
	//BaseClass::Deploy();

	//Start the shield out inactive
	m_bShieldActive = false;
#ifdef CLIENT_DLL
	DevMsg("Idle shield deploy called.\n");
#endif
	return BaseClass::Deploy();
	//just return true i guess
	//return SendWeaponAnim( ACT_VM_HOLSTER );
}

//----------------------------------------------------------------------------
// Purpose: Override the holster call to delete the shield if there is one in the world
//----------------------------------------------------------------------------
bool CFFWeaponShield::Holster()
{
	//Set the shield to idle to do cleanup and reseting stuff
	//ShieldIdle();

	//Now do normal holster
	return BaseClass::Holster();
}

//----------------------------------------------------------------------------
// Purpose: Handles whatever should be done when they fire(build, aim, etc) 
//----------------------------------------------------------------------------
void CFFWeaponShield::ShieldActive( void ) 
{
	CFFPlayer *pPlayer = GetPlayerOwner();

	//If no player gtfo
	if( pPlayer == NULL )
		return;

	//Check so this only gets called once
	if( m_bShieldActive == false )
	{
		m_bShieldActive = true;

#ifdef GAME_DLL
		pPlayer->SetRiotShieldActive(true);

		//If the effect is NOT on, then add it!
		if( pPlayer->IsSpeedEffectSet(SE_SHIELD) == false )
		{
			pPlayer->AddSpeedEffect(SE_SHIELD, 999, FF_SHIELD_SPEEDEFFECT, SEM_BOOLEAN);
		}
#endif

#ifdef CLIENT_DLL
		//Quickswap
		pPlayer->SwapToWeapon(FF_WEAPON_DEPLOYSHIELD);

		DevMsg("Swapping to active shield.\n");
#endif
	}
/* Old Block -->
	//Get the owner of this weapon
	CFFPlayer *pPlayer = GetPlayerOwner();

	//If no player gtfo
	if( pPlayer == NULL )
		return;

	//Check so it only does this once per click
	if( m_bShieldActive == false )
	{
		m_bShieldActive = true;

		// Don't fire again until fire animation has completed
		pPlayer->DoAnimationEvent(PLAYERANIMEVENT_FIRE_GUN_PRIMARY);

		// Send animation
		SendWeaponAnim( ACT_VM_DRAW );

#ifdef CLIENT_DLL
		DevMsg("Shield Active\n");
#endif
	}

#ifdef GAME_DLL

	//If the effect is NOT on, then add it!
	if( pPlayer->IsSpeedEffectSet(SE_SHIELD) == false )
	{
		pPlayer->AddSpeedEffect(SE_SHIELD, 999, FF_SHIELD_SPEEDEFFECT, SEM_BOOLEAN);
	}

	//If the player currently has no shield
	if( pPlayer->GetShield() == NULL )
	{
		//create the build info using shield
		CFFBuildableInfo hBuildInfo( pPlayer, FF_BUILD_SHIELD );

		//Create the shield in the world
		CFFShield *pShield = (CFFShield *)CBaseEntity::Create( "FF_Shield", hBuildInfo.GetBuildOrigin(), hBuildInfo.GetBuildAngles(), pPlayer);

		//Check if the world model should show or not-GreenMushy
		if( SHIELD_SHOWMODEL == 0 )
		{
			//I guess this makes it visible or not
			pShield->AddEffects( EF_NODRAW );
		}

		//Not sure if this helps make it solid and use the collision group
		pShield->VPhysicsInitNormal( SOLID_VPHYSICS, pShield->GetSolidFlags(), true );

		//I suppose this spawns the object?
		pShield->Spawn();

		//Set this shield to the buildable collision group so it blocks nearly everything
		pShield->SetCollisionGroup( COLLISION_GROUP_PLAYER );

		//Set the shield pointer in player to this newly created object, use GetShield() to find it again
		pPlayer->SetShield( pShield );

		DevMsg("Shield Created\n");
	}
	//the shield is in the world, so set up its position and angles
	else
	{
		//Just create this shield to shorten the text
		CFFShield *pShield = pPlayer->GetShield();

		//create the build info using shield
		CFFBuildableInfo hBuildInfo( pPlayer, FF_BUILD_SHIELD );

		//If the player is currently owning a shield*, then update its position with this player
		pShield->SetAbsOrigin( hBuildInfo.GetBuildOrigin() );
		pShield->SetAbsAngles( hBuildInfo.GetBuildAngles() );
	}
#endif
*/
}
//----------------------------------------------------------------------------
// Purpose: Checks validity of ground at this point or whatever
//----------------------------------------------------------------------------
void CFFWeaponShield::ShieldIdle( void ) 
{
/* Old Block -->
	//Get the owner of this shield
	CFFPlayer *pPlayer = GetPlayerOwner();

	//If there is no player, gtfo
	if( pPlayer == NULL )
		return;

	//So this only fires once per click
	if( m_bShieldActive == true )
	{
		m_bShieldActive = false;
		
		//Send the attack animation for the shield
		GetPlayerOwner()->DoAnimationEvent(PLAYERANIMEVENT_FIRE_GUN_SECONDARY);

		//This is broken too.  It starts the weapon out in front of the player.
		BaseClass::Deploy();

		//Send animation
		SendWeaponAnim( ACT_VM_HOLSTER );

#ifdef CLIENT_DLL
		DevMsg("Shield Idle\n");
#endif
	}

#ifdef GAME_DLL

	//If the player is effected by the shield slow, remove it
	if( pPlayer->IsSpeedEffectSet(SE_SHIELD) == true )
	{
		//Remove the effect that was put on earlier
		pPlayer->RemoveSpeedEffect(SE_SHIELD);
	}

	//If the player has a valid shield pointer
	if( pPlayer->GetShield() != NULL)
	{
		// Remove entity from game 
		UTIL_Remove( pPlayer->GetShield() );
		//Reset the shield to null for the future
		pPlayer->SetShield(NULL);

		DevMsg("Shield Deleted\n");
	}
#endif
*/
}