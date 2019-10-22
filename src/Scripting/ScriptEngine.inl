#include "Object.h"

namespace ng
{
template <>
void ScriptEngine::get(HSQUIRRELVM v, size_t index, bool &result)
{
    SQInteger integer = 0;
    sq_getinteger(v, index, &integer);
    result = integer != 0;
}

template <>
void ScriptEngine::get(HSQUIRRELVM v, size_t index, int &result)
{
    SQInteger integer = 0;
    sq_getinteger(v, index, &integer);
    result = integer;
}

template <>
void ScriptEngine::get(HSQUIRRELVM v, size_t index, const char *&result)
{
    const SQChar *text = nullptr;
    sq_getstring(v, index, &text);
    result = text;
}

template <>
void ScriptEngine::push<bool>(HSQUIRRELVM v, bool value)
{
    sq_pushbool(v, value ? SQTrue : SQFalse);
}

template <>
void ScriptEngine::push<int>(HSQUIRRELVM v, int value)
{
    sq_pushinteger(v, value);
}

template <>
void ScriptEngine::push<const char *>(HSQUIRRELVM v, const char *value)
{
    sq_pushstring(v, value, -1);
}

template <>
void ScriptEngine::push<SQFloat>(HSQUIRRELVM v, SQFloat value)
{
    sq_pushfloat(v, value);
}

template <>
void ScriptEngine::push<sf::Vector2i>(HSQUIRRELVM v, sf::Vector2i pos)
{
    sq_newtable(v);
    sq_pushstring(v, _SC("x"), -1);
    sq_pushinteger(v, static_cast<int>(pos.x));
    sq_newslot(v, -3, SQFalse);
    sq_pushstring(v, _SC("y"), -1);
    sq_pushinteger(v, static_cast<int>(pos.y));
    sq_newslot(v, -3, SQFalse);
}

template <>
void ScriptEngine::push<sf::Vector2f>(HSQUIRRELVM v, sf::Vector2f pos)
{
    return ScriptEngine::push(v, (sf::Vector2i)pos);
}

template <>
void ScriptEngine::push<Entity *>(HSQUIRRELVM v, Entity *pEntity)
{
    if (!pEntity)
    {
        sq_pushnull(v);
        return;
    }
    sq_pushobject(v, pEntity->getTable());
}

template <>
void ScriptEngine::push<Actor *>(HSQUIRRELVM v, Actor *pActor)
{
    if (!pActor)
    {
        sq_pushnull(v);
        return;
    }
    sq_pushobject(v, pActor->getTable());
}

template <>
void ScriptEngine::push<Room *>(HSQUIRRELVM v, Room *pRoom)
{
    if (!pRoom)
    {
        sq_pushnull(v);
        return;
    }
    sq_pushobject(v, pRoom->getTable());
}

template <>
void ScriptEngine::push<Object *>(HSQUIRRELVM v, Object *pObject)
{
    if (!pObject)
    {
        sq_pushnull(v);
        return;
    }
    sq_pushobject(v, pObject->getTable());
}

template <>
void ScriptEngine::push<std::nullptr_t>(HSQUIRRELVM v, std::nullptr_t _)
{
    sq_pushnull(v);
}
}
