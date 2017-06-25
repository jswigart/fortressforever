
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
class Omnibot_Groups
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

		class_<Omnibot_Groups>("Bot")
			.enum_("GroupType")
			[
				value("GRP_UNKNOWN",		ENT_GRP_UNKNOWN),
				value("GRP_RESUPPLY",		ENT_GRP_RESUPPLY),
				value("GRP_FLAG",			ENT_GRP_FLAG),
				value("GRP_FLAGCAPPOINT",	ENT_GRP_FLAGCAPPOINT),
				value("GRP_CONTROLPOINT",	ENT_GRP_CONTROLPOINT),
				value("GRP_BUTTON",			ENT_GRP_BUTTON),				
				value("GRP_GOAL",			ENT_GRP_GOAL)
			]
	];
};
