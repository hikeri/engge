#pragma once
#include "squirrel.h"
#include "Engine.h"
#include "_RoomTrigger.h"

namespace ng
{
class _ChangeColor : public TimeFunction
{
  public:
    _ChangeColor(Engine &engine, sf::Color startColor, sf::Color endColor, const sf::Time &time, std::function<float(float)> anim = Interpolations::linear, bool isLooping = false)
        : TimeFunction(time),
          _engine(engine),
          _startColor(startColor),
          _current(startColor),
          _endColor(endColor),
          _anim(anim),
          _isLooping(isLooping),
          _a(static_cast<sf::Int16>(endColor.a - startColor.a)),
          _r(static_cast<sf::Int16>(endColor.r - startColor.r)),
          _g(static_cast<sf::Int16>(endColor.g - startColor.g)),
          _b(static_cast<sf::Int16>(endColor.b - startColor.b))
    {
    }

    void operator()() override
    {
        _engine.setFadeColor(_current);
        if (!isElapsed())
        {
            auto t = _clock.getElapsedTime().asSeconds() / _time.asSeconds();
            auto f = _anim(t);
            _current = plusColor(_startColor, f);
        }
    }

    bool isElapsed() override
    {
        if (!_isLooping)
            return TimeFunction::isElapsed();
        return false;
    }

    void onElapsed() override
    {
        _engine.setFadeColor(_endColor);
    }

  private:
    sf::Color plusColor(const sf::Color &color1, float f)
    {
        auto a = static_cast<sf::Uint8>(color1.a + f * _a);
        auto r = static_cast<sf::Uint8>(color1.r + f * _r);
        auto g = static_cast<sf::Uint8>(color1.g + f * _g);
        auto b = static_cast<sf::Uint8>(color1.b + f * _b);
        return sf::Color(r, g, b, a);
    }

  private:
    Engine &_engine;
    bool _isLooping;
    std::function<float(float)> _anim;
    sf::Int16 _a, _r, _g, _b;
    sf::Color _startColor;
    sf::Color _endColor;
    sf::Color _current;
};

class _RoomPack : public Pack
{
  private:
    static Engine *g_pEngine;

  private:
    void addTo(ScriptEngine &engine) const override
    {
        g_pEngine = &engine.getEngine();
        engine.registerGlobalFunction(addTrigger, "addTrigger");
        engine.registerGlobalFunction(removeTrigger, "removeTrigger");
        engine.registerGlobalFunction(roomFade, "roomFade");
        engine.registerGlobalFunction(roomOverlayColor, "roomOverlayColor");
        engine.registerGlobalFunction(defineRoom, "defineRoom");
        engine.registerGlobalFunction(definePseudoRoom, "definePseudoRoom");
        engine.registerGlobalFunction(walkboxHidden, "walkboxHidden");
        engine.registerGlobalFunction(createLight, "createLight");
        engine.registerGlobalFunction(lightBrightness, "lightBrightness");
        engine.registerGlobalFunction(lightConeDirection, "lightConeDirection");
        engine.registerGlobalFunction(lightConeAngle, "lightConeAngle");
        engine.registerGlobalFunction(lightConeFalloff, "lightConeFalloff");
        engine.registerGlobalFunction(lightCutOffRadius, "lightCutOffRadius");
        engine.registerGlobalFunction(lightHalfRadius, "lightHalfRadius");
        engine.registerGlobalFunction(lightZRange, "lightZRange");
    }

    static void _fadeTo(float a, const sf::Time &time)
    {
        auto get = std::bind(&Engine::getFadeAlpha, g_pEngine);
        auto set = std::bind(&Engine::setFadeAlpha, g_pEngine, std::placeholders::_1);
        auto fadeTo = std::make_unique<ChangeProperty<float>>(get, set, a, time);
        g_pEngine->addFunction(std::move(fadeTo));
    }

    static SQInteger createLight(HSQUIRRELVM v)
    {
        std::cerr << "TODO: createLight: not implemented" << std::endl;
        sq_pushnull(v);
        return 1;
    }

    static SQInteger lightBrightness(HSQUIRRELVM v)
    {
        std::cerr << "TODO: lightBrightness: not implemented" << std::endl;
        return 0;
    }

    static SQInteger lightConeDirection(HSQUIRRELVM v)
    {
        std::cerr << "TODO: lightConeDirection: not implemented" << std::endl;
        return 0;
    }

    static SQInteger lightConeAngle(HSQUIRRELVM v)
    {
        std::cerr << "TODO: lightConeAngle: not implemented" << std::endl;
        return 0;
    }

    static SQInteger lightConeFalloff(HSQUIRRELVM v)
    {
        std::cerr << "TODO: lightConeFalloff: not implemented" << std::endl;
        return 0;
    }

    static SQInteger lightCutOffRadius(HSQUIRRELVM v)
    {
        std::cerr << "TODO: lightCutOffRadius: not implemented" << std::endl;
        return 0;
    }

    static SQInteger lightHalfRadius(HSQUIRRELVM v)
    {
        std::cerr << "TODO: lightHalfRadius: not implemented" << std::endl;
        return 0;
    }

    static SQInteger lightZRange(HSQUIRRELVM v)
    {
        std::cerr << "TODO: lightZRange: not implemented" << std::endl;
        return 0;
    }

    static SQInteger addTrigger(HSQUIRRELVM v)
    {
        auto object = ScriptEngine::getObject(v, 2);
        if (!object)
        {
            return sq_throwerror(v, _SC("failed to get object"));
        }
        HSQOBJECT inside;
        sq_resetobject(&inside);
        if (SQ_FAILED(sq_getstackobj(v, 3, &inside)))
        {
            return sq_throwerror(v, _SC("failed to get insideTriggerFunction"));
        }
        sq_addref(v, &inside);

        HSQOBJECT outside;
        sq_resetobject(&outside);
        auto numArgs = sq_gettop(v) - 2;
        if (numArgs == 4)
        {
            if (SQ_FAILED(sq_getstackobj(v, 4, &outside)))
            {
                return sq_throwerror(v, _SC("failed to get outsideTriggerFunction"));
            }
            sq_addref(v, &outside);
        }
        auto trigger = std::make_shared<_RoomTrigger>(*g_pEngine, *object, inside, outside);
        object->addTrigger(trigger);

        return 0;
    }

    static SQInteger walkboxHidden(HSQUIRRELVM v)
    {
        const SQChar *name = nullptr;
        if (SQ_FAILED(sq_getstring(v, 2, &name)))
        {
            return sq_throwerror(v, _SC("failed to get walkbox name"));
        }
        SQBool hidden;
        sq_tobool(v, 3, &hidden);

        g_pEngine->getRoom().setWalkboxEnabled(name, hidden == SQFalse);
        return 0;
    }

    static SQInteger removeTrigger(HSQUIRRELVM v)
    {
        auto object = ScriptEngine::getObject(v, 2);
        if (!object)
        {
            return sq_throwerror(v, _SC("failed to get object"));
        }
        object->removeTrigger();
        return 0;
    }

    static SQInteger roomFade(HSQUIRRELVM v)
    {
        SQInteger type;
        SQFloat t;
        if (SQ_FAILED(sq_getinteger(v, 2, &type)))
        {
            return sq_throwerror(v, _SC("failed to get type"));
        }
        if (SQ_FAILED(sq_getfloat(v, 3, &t)))
        {
            return sq_throwerror(v, _SC("failed to get time"));
        }
        _fadeTo(type == 0 ? 0.f : 1.f, sf::seconds(t));
        return 0;
    }

    static SQInteger roomOverlayColor(HSQUIRRELVM v)
    {
        SQInteger startColor, endColor;
        auto numArgs = sq_gettop(v) - 1;
        if (SQ_FAILED(sq_getinteger(v, 2, &startColor)))
        {
            return sq_throwerror(v, _SC("failed to get startColor"));
        }
        g_pEngine->setFadeColor(_toColor(startColor));
        if (numArgs == 3)
        {
            if (SQ_FAILED(sq_getinteger(v, 3, &endColor)))
            {
                return sq_throwerror(v, _SC("failed to get endColor"));
            }
            SQFloat duration;
            if (SQ_FAILED(sq_getfloat(v, 4, &duration)))
            {
                return sq_throwerror(v, _SC("failed to get duration"));
            }
            auto fadeTo = std::make_unique<_ChangeColor>(*g_pEngine, _toColor(startColor), _toColor(endColor), sf::seconds(duration), Interpolations::linear, false);
            g_pEngine->addFunction(std::move(fadeTo));
        }
        return 0;
    }

    static void setObjectSlot(HSQUIRRELVM v, const SQChar *name, Object &object)
    {
        sq_pushstring(v, name, -1);
        ScriptEngine::pushObject(v, object);
        sq_pushstring(v, _SC("name"), -1);
        sq_pushstring(v, object.getName().data(), -1);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
    }

    template <typename T>
    static T _get(HSQUIRRELVM v);

    template <typename T>
    static void _getField(HSQUIRRELVM v, HSQOBJECT object, const SQChar *name, std::function<void(T)> func)
    {
        sq_pushobject(v, object);
        sq_pushstring(v, _SC(name), -1);
        if (SQ_SUCCEEDED(sq_get(v, -2)))
        {
            T value = _get<T>(v);
            func(value);
        }
    }

    static SQInteger definePseudoRoom(HSQUIRRELVM v)
    {
        std::cerr << "TODO: definePseudoRoom: not implemented" << std::endl;
        return 0;
    }

    static SQInteger defineRoom(HSQUIRRELVM v)
    {
        auto pRoom = std::make_unique<Room>(g_pEngine->getTextureManager(), g_pEngine->getSettings());
        auto table = std::make_unique<HSQOBJECT>();
        auto pTable = table.get();
        sq_getstackobj(v, 2, pTable);

        // loadRoom
        sq_pushobject(v, *pTable);
        sq_pushstring(v, _SC("background"), -1);
        if (SQ_FAILED(sq_get(v, -2)))
        {
            return sq_throwerror(v, _SC("can't find background entry"));
        }
        const SQChar *name;
        sq_getstring(v, -1, &name);
        sq_pop(v, 2);
        pRoom->setTable(std::move(table));
        pRoom->load(name);

        if (strcmp(name, "Inventory") == 0)
        {
            int tmp = 42;
        }

        // define instance
        sq_pushobject(v, *pTable);
        sq_pushstring(v, _SC("_type"), -1);
        sq_pushinteger(v, Room::RoomType);
        sq_newslot(v, -3, SQFalse);
        sq_pushstring(v, _SC("instance"), -1);
        sq_pushuserpointer(v, pRoom.get());
        sq_newslot(v, -3, SQFalse);

        std::unordered_map<const SQChar *, HSQOBJECT> inventory;

        // define room objects
        sq_pushobject(v, *pTable);
        sq_pushnull(v);
        while (SQ_SUCCEEDED(sq_next(v, -2)))
        {
            //here -1 is the value and -2 is the key
            auto type = sq_gettype(v, -1);
            if (type == OT_TABLE)
            {
                const SQChar *key = nullptr;
                sq_getstring(v, -2, &key);
                std::cout << "### Obj " << key << std::endl;
                HSQOBJECT object;
                sq_resetobject(&object);
                if (SQ_SUCCEEDED(sq_getstackobj(v, -1, &object)))
                {
                    sq_addref(v, &object);
                    inventory[key] = object;
                }
            }
            sq_pop(v, 2); //pops key and val before the nex iteration
        }
        sq_pop(v, 1); //pops the null iterator

        for (auto obj : inventory)
        {
            sq_pushobject(v, obj.second);
            sq_pushstring(v, _SC("icon"), -1);
            if (SQ_FAILED(sq_rawget(v, -2)))
            {
                sq_release(v, &obj.second);
                inventory.erase(obj.first);
            }
        }

        // don't know if this is the best way to do this
        // but it seems that inventory objects are accessible
        // from the roottable
        for (auto obj : inventory)
        {
            sq_pushroottable(v);
            sq_pushstring(v, obj.first, -1);
            sq_pushobject(v, obj.second);
            sq_newslot(v, -3, SQFalse);
        }

        for (auto &obj : pRoom->getObjects())
        {
            sq_resetobject(&obj->getTable());

            sq_pushobject(v, *pTable);
            sq_pushstring(v, obj->getName().data(), -1);
            if (SQ_FAILED(sq_get(v, -2)))
            {
                setObjectSlot(v, obj->getName().data(), *obj);

                sq_pushobject(v, *pTable);
                sq_pushstring(v, obj->getName().data(), -1);
                sq_get(v, -2);
                sq_getstackobj(v, -1, &obj->getTable());
                if (!sq_istable(obj->getTable()))
                {
                    return sq_throwerror(v, _SC("object should be a table entry"));
                }

                continue;
            }

            sq_getstackobj(v, -1, &obj->getTable());
            if (!sq_istable(obj->getTable()))
            {
                return sq_throwerror(v, _SC("object should be a table entry"));
            }

            _getField<SQInteger>(v, obj->getTable(), _SC("initState"), [&obj](SQInteger value) { obj->setStateAnimIndex(value); });
            _getField<SQBool>(v, obj->getTable(), _SC("initTouchable"), [&obj](SQBool value) { obj->setTouchable(value == SQTrue); });
            _getField<SQInteger>(v, obj->getTable(), _SC("defaultVerb"), [&obj](SQInteger value) { obj->setDefaultVerb(value); });
            _getField<const SQChar *>(v, obj->getTable(), _SC("name"), [&obj](const SQChar *value) {
                if (strlen(value) > 0 && value[0] == '@')
                {
                    std::string s(value);
                    s = s.substr(1);
                    auto id = std::strtol(s.c_str(), nullptr, 10);
                    auto text = g_pEngine->getText(id);
                    obj->setId(obj->getName());
                    obj->setName(text);
                }
                else
                {
                    obj->setId(obj->getName());
                    obj->setName(value);
                }
            });

            sq_pushobject(v, obj->getTable());
            sq_pushstring(v, _SC("instance"), -1);
            sq_pushuserpointer(v, obj.get());
            sq_newslot(v, -3, SQFalse);

            sq_pushobject(v, obj->getTable());
            sq_pushobject(v, *pTable);
            sq_setdelegate(v, -2);
        }

        g_pEngine->addRoom(std::move(pRoom));
        return 0;
    }
};

template <>
SQInteger _RoomPack::_get(HSQUIRRELVM v)
{
    SQInteger value = 0;
    sq_getinteger(v, -1, &value);
    return value;
}

template <>
SQBool _RoomPack::_get(HSQUIRRELVM v)
{
    SQBool value = false;
    sq_getbool(v, -1, &value);
    return value;
}

template <>
const SQChar *_RoomPack::_get(HSQUIRRELVM v)
{
    const SQChar *value = nullptr;
    sq_getstring(v, -1, &value);
    return value;
}

Engine *_RoomPack::g_pEngine = nullptr;

} // namespace ng