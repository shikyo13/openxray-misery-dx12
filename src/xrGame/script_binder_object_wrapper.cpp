////////////////////////////////////////////////////////////////////////////
//	Module 		: script_binder_object_wrapper.cpp
//	Created 	: 29.03.2004
//  Modified 	: 29.03.2004
//	Author		: Dmitriy Iassenev
//	Description : Script object binder wrapper
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_binder_object_wrapper.h"
#include "script_game_object.h"
#include "xrServer_Objects_ALife.h"

namespace
{
    char netRelcaseDefaultKey;
}

CScriptBinderObjectWrapper::CScriptBinderObjectWrapper(CScriptGameObject* object) : CScriptBinderObject(object) {}
CScriptBinderObjectWrapper::~CScriptBinderObjectWrapper() {}
void CScriptBinderObjectWrapper::reinit() { luabind::call_member<void>(this, "reinit"); }
void CScriptBinderObjectWrapper::reinit_static(CScriptBinderObject* script_binder_object)
{
    script_binder_object->CScriptBinderObject::reinit();
}

void CScriptBinderObjectWrapper::reload(LPCSTR section) { luabind::call_member<void>(this, "reload", section); }
void CScriptBinderObjectWrapper::reload_static(CScriptBinderObject* script_binder_object, LPCSTR section)
{
    script_binder_object->CScriptBinderObject::reload(section);
}

bool CScriptBinderObjectWrapper::net_Spawn(SpawnType DC) { return (luabind::call_member<bool>(this, "net_spawn", DC)); }
bool CScriptBinderObjectWrapper::net_Spawn_static(CScriptBinderObject* script_binder_object, SpawnType DC)
{
    return (script_binder_object->CScriptBinderObject::net_Spawn(DC));
}

void CScriptBinderObjectWrapper::net_Destroy() { luabind::call_member<void>(this, "net_destroy"); }
void CScriptBinderObjectWrapper::net_Destroy_static(CScriptBinderObject* script_binder_object)
{
    script_binder_object->CScriptBinderObject::net_Destroy();
}

void CScriptBinderObjectWrapper::net_Import(NET_Packet* net_packet)
{
    luabind::call_member<void>(this, "net_import", net_packet);
}

void CScriptBinderObjectWrapper::net_Import_static(CScriptBinderObject* script_binder_object, NET_Packet* net_packet)
{
    script_binder_object->CScriptBinderObject::net_Import(net_packet);
}

void CScriptBinderObjectWrapper::net_Export(NET_Packet* net_packet)
{
    luabind::call_member<void>(this, "net_export", net_packet);
}

void CScriptBinderObjectWrapper::net_Export_static(CScriptBinderObject* script_binder_object, NET_Packet* net_packet)
{
    script_binder_object->CScriptBinderObject::net_Export(net_packet);
}

void CScriptBinderObjectWrapper::shedule_Update(u32 time_delta)
{
    luabind::call_member<void>(this, "update", time_delta);
}

void CScriptBinderObjectWrapper::shedule_Update_static(CScriptBinderObject* script_binder_object, u32 time_delta)
{
    script_binder_object->CScriptBinderObject::shedule_Update(time_delta);
}

void CScriptBinderObjectWrapper::save(NET_Packet* output_packet)
{
    luabind::call_member<void>(this, "save", output_packet);
}

void CScriptBinderObjectWrapper::save_static(CScriptBinderObject* script_binder_object, NET_Packet* output_packet)
{
    script_binder_object->CScriptBinderObject::save(output_packet);
}

void CScriptBinderObjectWrapper::load(IReader* input_packet) { luabind::call_member<void>(this, "load", input_packet); }
void CScriptBinderObjectWrapper::load_static(CScriptBinderObject* script_binder_object, IReader* input_packet)
{
    script_binder_object->CScriptBinderObject::load(input_packet);
}

bool CScriptBinderObjectWrapper::net_SaveRelevant() { return (luabind::call_member<bool>(this, "net_save_relevant")); }
bool CScriptBinderObjectWrapper::net_SaveRelevant_static(CScriptBinderObject* script_binder_object)
{
    return (script_binder_object->CScriptBinderObject::net_SaveRelevant());
}

void CScriptBinderObjectWrapper::register_net_Relcase_default(lua_State* luaState)
{
    // Keep the exact registered fallback alive in this Lua state's registry.
    // A later script override must never be mistaken for the native default.
    lua_pushlightuserdata(luaState, &netRelcaseDefaultKey);
    lua_getglobal(luaState, "object_binder");
    lua_getfield(luaState, -1, "net_Relcase");
    R_ASSERT(lua_isfunction(luaState, -1));
    lua_remove(luaState, -2);
    lua_rawset(luaState, LUA_REGISTRYINDEX);
}

void CScriptBinderObjectWrapper::net_Relcase(CScriptGameObject* object)
{
    const auto& self = luabind::detail::wrap_access::ref(*this);
    lua_State* luaState = self.state();
    self.get(luaState);
    R_ASSERT(!lua_isnil(luaState, -1));
    // Use luabind's normal selection each time: class, instance and runtime
    // overrides retain their existing dispatch and error behavior.
    luabind::detail::do_call_member_selection(luaState, "net_Relcase");
    if (lua_isnil(luaState, -1))
    {
        lua_pop(luaState, 1);
        throw luabind::unresolved_name("Attempt to call nonexistent function", "net_Relcase");
    }
    lua_pushlightuserdata(luaState, &netRelcaseDefaultKey);
    lua_rawget(luaState, LUA_REGISTRYINDEX);
    const bool nativeDefault = lua_rawequal(luaState, -1, -2) != 0;
    lua_pop(luaState, 1);
    if (nativeDefault)
    {
        lua_pop(luaState, 1);
        // Execute the original native callback without a Lua round trip and
        // repeated conversion of the same removed object for every observer.
        CScriptBinderObject::net_Relcase(object);
    }
    else
    {
        self.get(luaState);
        luabind::detail::call_member_impl<void>(luaState, std::true_type{}, luabind::meta::index_list<1>{}, object);
    }
}

void CScriptBinderObjectWrapper::net_Relcase_static(
    CScriptBinderObject* script_binder_object, CScriptGameObject* object)
{
    script_binder_object->CScriptBinderObject::net_Relcase(object);
}
