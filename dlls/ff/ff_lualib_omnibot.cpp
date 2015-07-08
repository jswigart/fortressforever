
// ff_lualib_omnibot.cpp

//---------------------------------------------------------------------------
// includes
//---------------------------------------------------------------------------
// includes
#include "cbase.h"
#include "ff_lualib.h"

#include "omnibot_interface.h"

// Lua includes
extern "C"
{
	#include "lua.h"
	#include "lualib.h"
	#include "lauxlib.h"
}

#include "luabind/luabind.hpp"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//---------------------------------------------------------------------------
using namespace luabind;

//---------------------------------------------------------------------------
class Omnibot_GoalTypes
{
public:
};

//---------------------------------------------------------------------------
void CFFLuaLib::InitOmnibot(lua_State* L)
{
	ASSERT(L);
	module(L)
	[
		def("SendBotTrigger",		&omnibot_interface::BotSendTriggerEx),
		def("SendBotSignal",		(void(*)(const char*))&omnibot_interface::SendBotSignal),

		class_<Omnibot_GoalTypes>("Bot")
			.enum_("GoalType")
			[
				value("kNone",				omnibot_interface::kNone),
				value("kBackPack_Ammo",		omnibot_interface::kBackPack_Ammo),
				value("kBackPack_Armor",	omnibot_interface::kBackPack_Armor),
				value("kBackPack_Health",	omnibot_interface::kBackPack_Health),
				value("kBackPack_Grenades",	omnibot_interface::kBackPack_Grenades),
				value("kFlag",				omnibot_interface::kFlag),
				value("kFlagCap",			omnibot_interface::kFlagCap),				
				value("kTrainerSpawn",		omnibot_interface::kTrainerSpawn),
				value("kHuntedEscape",		omnibot_interface::kHuntedEscape)
			]
	];
};
