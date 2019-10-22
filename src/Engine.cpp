#include "squirrel.h"
#include "sqstdaux.h"
#include "Engine.h"
#include "ActorIconSlot.h"
#include "ActorIcons.h"
#include "Camera.h"
#include "Cutscene.h"
#include "Dialog/DialogManager.h"
#include "Font.h"
#include "Inventory.h"
#include "Preferences.h"
#include "Room.h"
#include "RoomScaling.h"
#include "Screen.h"
#include "ScriptEngine.h"
#include "ScriptExecute.h"
#include "Sentence.h"
#include "SoundDefinition.h"
#include "SoundId.h"
#include "SoundManager.h"
#include "SpriteSheet.h"
#include "Text.h"
#include "TextDatabase.h"
#include "Thread.h"
#include "Verb.h"
#include "VerbExecute.h"
#include "_DebugTools.h"
#include "_Util.h"
#include "Logger.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <math.h>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

namespace ng
{
CursorDirection operator|=(CursorDirection &lhs, CursorDirection rhs)
{
    lhs = static_cast<CursorDirection>(static_cast<std::underlying_type<CursorDirection>::type>(lhs) |
                                       static_cast<std::underlying_type<CursorDirection>::type>(rhs));
    return lhs;
}

bool operator&(CursorDirection lhs, CursorDirection rhs)
{
    return static_cast<CursorDirection>(static_cast<std::underlying_type<CursorDirection>::type>(lhs) &
                                        static_cast<std::underlying_type<CursorDirection>::type>(rhs)) >
           CursorDirection::None;
}

struct Engine::Impl
{
    Engine *_pEngine;
    std::unique_ptr<_DebugTools> _pDebugTools;
    EngineSettings &_settings;
    TextureManager _textureManager;
    Room *_pRoom;
    std::vector<std::unique_ptr<Actor>> _actors;
    std::vector<std::unique_ptr<Room>> _rooms;
    std::vector<std::unique_ptr<Function>> _newFunctions;
    std::vector<std::unique_ptr<Function>> _functions;
    Cutscene *_pCutscene{nullptr};
    sf::RenderWindow *_pWindow{nullptr};
    TextDatabase _textDb;
    Font _fntFont;
    Actor *_pCurrentActor{nullptr};
    std::array<VerbSlot, 6> _verbSlots;
    std::array<VerbUiColors, 6> _verbUiColors;
    bool _inputHUD{true};
    bool _inputActive;
    bool _showCursor;
    bool _inputVerbsActive;
    SpriteSheet _verbSheet, _gameSheet;
    Actor *_pFollowActor;
    std::array<sf::IntRect, 9> _verbRects;
    Entity *_pUseObject{nullptr};
    Entity *_pObj1{nullptr};
    Entity *_pObj2{nullptr};
    Entity *_pHoveredEntity{nullptr};
    sf::Vector2f _mousePos;
    sf::Vector2f _mousePosInRoom;
    std::unique_ptr<VerbExecute> _pVerbExecute;
    std::unique_ptr<ScriptExecute> _pScriptExecute;
    const Verb *_pVerb{nullptr};
    const Verb *_pVerbOverride{nullptr};
    std::vector<std::unique_ptr<ThreadBase>> _threads;
    DialogManager _dialogManager;
    Preferences _preferences;
    SoundManager _soundManager;
    CursorDirection _cursorDirection;
    std::array<ActorIconSlot, 6> _actorsIconSlots;
    UseFlag _useFlag{UseFlag::None};
    ActorIcons _actorIcons;
    Inventory _inventory;
    HSQUIRRELVM _vm{};
    sf::Time _time;
    bool _isMouseDown{false};
    bool _isMouseRightDown{false};
    int _frameCounter{0};
    HSQOBJECT _pDefaultObject{};
    Camera _camera;
    sf::Color _fadeColor{sf::Color::Transparent};
    std::unique_ptr<Sentence> _pSentence{};

    explicit Impl(EngineSettings &settings);

    sf::IntRect getVerbRect(const Verb &verb) const;
    void drawVerbs(sf::RenderWindow &window) const;
    void drawCursor(sf::RenderWindow &window) const;
    void drawCursorText(sf::RenderWindow &window) const;
    void clampCamera();
    int getCurrentActorIndex() const;
    sf::IntRect getCursorRect() const;
    void appendUseFlag(std::wstring &sentence) const;
    bool clickedAt(const sf::Vector2f &pos);
    void updateCutscene(const sf::Time &elapsed);
    void updateFunctions(const sf::Time &elapsed);
    void updateActorIcons(const sf::Time &elapsed);
    void updateSentence(const sf::Time &elapsed);
    void updateMouseCursor();
    void updateHoveredEntity(bool isRightClick);
    SQInteger enterRoom(Room *pRoom, Object *pObject);
    SQInteger exitRoom(Object *pObject);
    void updateScreenSize();
    void updateRoomScalings();
    void setCurrentRoom(Room *pRoom);
    int32_t getFlags(HSQOBJECT obj) const;
    int getDefaultVerb(HSQUIRRELVM vm, const Entity *pEntity) const;
    Entity *getHoveredEntity(const sf::Vector2f &mousPos);
    void actorEnter();
    void actorExit();
    void onLanguageChange(const std::string &lang);
    std::string getVerbName(const Verb &verb) const;
    void drawFade(sf::RenderTarget &target) const;
};

Engine::Impl::Impl(EngineSettings &settings)
    : _pEngine(nullptr), _settings(settings), _textureManager(settings), _pRoom(nullptr), _inputActive(false),
      _showCursor(false), _inputVerbsActive(false), _pFollowActor(nullptr), _soundManager(settings),
      _cursorDirection(CursorDirection::None), _actorIcons(_actorsIconSlots, _verbUiColors, _pCurrentActor),
      _inventory(_actorsIconSlots, _verbUiColors, _pCurrentActor)
{
    _verbSheet.setSettings(&settings);
    _verbSheet.setTextureManager(&_textureManager);
    _gameSheet.setSettings(&settings);
    _gameSheet.setTextureManager(&_textureManager);
    sq_resetobject(&_pDefaultObject);
}

void Engine::Impl::onLanguageChange(const std::string &lang)
{
    std::stringstream ss;
    ss << "ThimbleweedText_" << lang << ".tsv";
    _textDb.load(ss.str());

    ScriptEngine::call("onLanguageChange");
}

void Engine::Impl::drawFade(sf::RenderTarget &target) const
{
    sf::RectangleShape fadeShape;
    auto screen = target.getView().getSize();
    fadeShape.setSize(sf::Vector2f(screen.x, screen.y));
    fadeShape.setFillColor(_fadeColor);
    target.draw(fadeShape);
}

Engine::Engine(EngineSettings &settings) : _pImpl(std::make_unique<Impl>(settings))
{
    time_t t;
    auto seed = (unsigned)time(&t);
    info("seed: {}", seed);
    srand(seed);

    _pImpl->_pEngine = this;
    _pImpl->_pDebugTools = std::make_unique<_DebugTools>(*this);
    _pImpl->_soundManager.setEngine(this);
    _pImpl->_dialogManager.setEngine(this);
    _pImpl->_actorIcons.setEngine(this);
    _pImpl->_inventory.setEngine(this);
    _pImpl->_camera.setEngine(this);
    _pImpl->_fntFont.setTextureManager(&_pImpl->_textureManager);
    _pImpl->_fntFont.setSettings(&settings);

    auto retroFonts = _pImpl->_preferences.getUserPreference(PreferenceNames::RetroFonts, PreferenceDefaultValues::RetroFonts);
    _pImpl->_fntFont.load(retroFonts ? "FontRetroSheet": "FontModernSheet");

    // load all messages
    _pImpl->_textDb.setSettings(settings);

    std::stringstream s;
    auto lang = std::any_cast<std::string>(_pImpl->_preferences.getUserPreference(PreferenceNames::Language, PreferenceDefaultValues::Language));
    s << "ThimbleweedText_" << lang << ".tsv";
    _pImpl->_textDb.load(s.str());

    _pImpl->_verbSheet.load("VerbSheet");
    _pImpl->_gameSheet.load("GameSheet");

    _pImpl->_preferences.subscribe([this](const std::string &name, std::any value) {
        if (name == PreferenceNames::Language)
        {
            _pImpl->onLanguageChange(std::any_cast<std::string>(value));
        }
        else if (name == PreferenceNames::RetroFonts)
        {
            _pImpl->_fntFont.load(std::any_cast<bool>(value) ? "FontRetroSheet": "FontModernSheet");
        }
    });
}

Engine::~Engine() = default;

int Engine::getFrameCounter() const { return _pImpl->_frameCounter; }

void Engine::setWindow(sf::RenderWindow &window) { _pImpl->_pWindow = &window; }

const sf::RenderWindow &Engine::getWindow() const { return *_pImpl->_pWindow; }

TextureManager &Engine::getTextureManager() { return _pImpl->_textureManager; }

EngineSettings &Engine::getSettings() { return _pImpl->_settings; }

Room *Engine::getRoom() { return _pImpl->_pRoom; }

std::wstring Engine::getText(int id) const
{
    auto text = _pImpl->_textDb.getText(id);
    replaceAll(text, L"\\\"", L"\"");
    removeFirstParenthesis(text);
    return text;
}

void Engine::addActor(std::unique_ptr<Actor> actor) { _pImpl->_actors.push_back(std::move(actor)); }

void Engine::addRoom(std::unique_ptr<Room> room) { _pImpl->_rooms.push_back(std::move(room)); }

const std::vector<std::unique_ptr<Room>> &Engine::getRooms() const { return _pImpl->_rooms; }

std::vector<std::unique_ptr<Room>> &Engine::getRooms() { return _pImpl->_rooms; }

void Engine::addFunction(std::unique_ptr<Function> function) { _pImpl->_newFunctions.push_back(std::move(function)); }

std::vector<std::unique_ptr<Actor>> &Engine::getActors() { return _pImpl->_actors; }

Actor *Engine::getCurrentActor() { return _pImpl->_pCurrentActor; }

Actor *Engine::getFollowActor() { return _pImpl->_pFollowActor; }

void Engine::setVerb(int characterSlot, int verbSlot, const Verb &verb)
{
    _pImpl->_verbSlots.at(characterSlot).setVerb(verbSlot, verb);
}

void Engine::setVerbUiColors(int characterSlot, VerbUiColors colors)
{
    _pImpl->_verbUiColors.at(characterSlot) = colors;
}

VerbUiColors &Engine::getVerbUiColors(int characterSlot) { return _pImpl->_verbUiColors.at(characterSlot); }

bool Engine::getInputActive() const { return _pImpl->_inputActive; }

bool Engine::getInputVerbs() const { return _pImpl->_inputVerbsActive; }

void Engine::setInputState(int state)
{
    if ((state & InputStateConstants::UI_INPUT_ON) == InputStateConstants::UI_INPUT_ON)
    {
        _pImpl->_inputActive = true;
    }
    if ((state & InputStateConstants::UI_INPUT_OFF) == InputStateConstants::UI_INPUT_OFF)
    {
        _pImpl->_inputActive = false;
    }
    if ((state & InputStateConstants::UI_VERBS_ON) == InputStateConstants::UI_VERBS_ON)
    {
        _pImpl->_inputVerbsActive = true;
    }
    if ((state & InputStateConstants::UI_VERBS_OFF) == InputStateConstants::UI_VERBS_OFF)
    {
        _pImpl->_inputVerbsActive = false;
    }
    if ((state & InputStateConstants::UI_CURSOR_ON) == InputStateConstants::UI_CURSOR_ON)
    {
        _pImpl->_showCursor = true;
    }
    if ((state & InputStateConstants::UI_CURSOR_OFF) == InputStateConstants::UI_CURSOR_OFF)
    {
        _pImpl->_showCursor = false;
    }
    if ((state & InputStateConstants::UI_HUDOBJECTS_ON) == InputStateConstants::UI_HUDOBJECTS_ON)
    {
        _pImpl->_inputHUD = true;
    }
    if ((state & InputStateConstants::UI_HUDOBJECTS_OFF) == InputStateConstants::UI_HUDOBJECTS_OFF)
    {
        _pImpl->_inputHUD = false;
    }
}

int Engine::getInputState() const
{
    int inputState = 0;
    inputState |= (_pImpl->_inputActive ? InputStateConstants::UI_INPUT_ON : InputStateConstants::UI_INPUT_OFF);
    inputState |= (_pImpl->_inputVerbsActive ? InputStateConstants::UI_VERBS_ON : InputStateConstants::UI_VERBS_OFF);
    inputState |= (_pImpl->_showCursor ? InputStateConstants::UI_CURSOR_ON : InputStateConstants::UI_CURSOR_OFF);
    inputState |= (_pImpl->_inputHUD ? InputStateConstants::UI_HUDOBJECTS_ON : InputStateConstants::UI_HUDOBJECTS_OFF);
    return inputState;
}

void Engine::follow(Actor *pActor)
{
    auto panCamera =
        (_pImpl->_pFollowActor && pActor && _pImpl->_pFollowActor != pActor && _pImpl->_pFollowActor->getRoom() &&
         pActor->getRoom() && _pImpl->_pFollowActor->getRoom()->getId() == pActor->getRoom()->getId());
    _pImpl->_pFollowActor = pActor;
    if (!pActor)
        return;

    auto pos = pActor->getRealPosition();
    auto screen = _pImpl->_pWindow->getView().getSize();
    setRoom(pActor->getRoom());
    if (panCamera)
    {
        _pImpl->_camera.panTo(pos + pActor->getUsePosition() - sf::Vector2f(screen.x / 2, screen.y / 2), sf::seconds(4),
                              InterpolationMethod::EaseOut);
        return;
    }
    _pImpl->_camera.at(pos + pActor->getUsePosition() - sf::Vector2f(screen.x / 2, screen.y / 2));
}

void Engine::setVerbExecute(std::unique_ptr<VerbExecute> verbExecute)
{
    _pImpl->_pVerbExecute = std::move(verbExecute);
}

void Engine::setDefaultVerb()
{
    auto index = _pImpl->getCurrentActorIndex();
    if (index == -1)
        return;

    const auto &verbSlot = _pImpl->_verbSlots.at(index);
    _pImpl->_pVerb = &verbSlot.getVerb(0);
    _pImpl->_useFlag = UseFlag::None;
    _pImpl->_pUseObject = nullptr;
    _pImpl->_pObj1 = nullptr;
    _pImpl->_pObj2 = nullptr;
}

void Engine::setScriptExecute(std::unique_ptr<ScriptExecute> scriptExecute)
{
    _pImpl->_pScriptExecute = std::move(scriptExecute);
}

void Engine::addThread(std::unique_ptr<ThreadBase> thread) { _pImpl->_threads.push_back(std::move(thread)); }

sf::Vector2f Engine::getMousePos() const { return _pImpl->_mousePos; }

sf::Vector2f Engine::getMousePositionInRoom() const { return _pImpl->_mousePosInRoom; }

sf::Vector2f Engine::findScreenPosition(int verbId) const
{
    auto pVerb = getVerb(verbId);
    auto s = _pImpl->getVerbName(*pVerb);
    auto r = _pImpl->_verbSheet.getSpriteSourceSize(s);
    return sf::Vector2f(r.left + r.width / 2.f, r.top + r.height / 2.f);
}

Preferences &Engine::getPreferences() { return _pImpl->_preferences; }

SoundManager &Engine::getSoundManager() { return _pImpl->_soundManager; }

DialogManager &Engine::getDialogManager() { return _pImpl->_dialogManager; }

Camera &Engine::getCamera() { return _pImpl->_camera; }

sf::Time Engine::getTime() const { return _pImpl->_time; }

void Engine::setVm(HSQUIRRELVM vm) { _pImpl->_vm = vm; }

HSQUIRRELVM Engine::getVm() { return _pImpl->_vm; }

SQInteger Engine::Impl::exitRoom(Object *pObject)
{
    if (!_pRoom)
        return 0;

    auto pOldRoom = _pRoom;

    // call exit room function
    trace("call exit room function of {}", pOldRoom->getId());

    sq_pushobject(_vm, pOldRoom->getTable());
    sq_pushstring(_vm, _SC("exit"), -1);
    if (SQ_FAILED(sq_get(_vm, -2)))
    {
        error("can't find exit function");
        return 0;
    }

    SQInteger nparams, nfreevars;
    sq_getclosureinfo(_vm, -1, &nparams, &nfreevars);
    trace("enter function found with {} parameters", nparams);

    actorExit();

    sq_remove(_vm, -2);
    if (nparams == 2)
    {
        ScriptEngine::call(pOldRoom, "exit", pObject);
    }
    else
    {
        ScriptEngine::call(pOldRoom, "exit");
    }

    pOldRoom->exit();

    ScriptEngine::call("exitedRoom", pOldRoom);

    return 0;
}

int Engine::Impl::getDefaultVerb(HSQUIRRELVM v, const Entity *pEntity) const
{
    sq_pushobject(v, pEntity->getTable());
    sq_pushstring(v, _SC("defaultVerb"), -1);
    if (SQ_SUCCEEDED(sq_get(v, -2)))
    {
        SQInteger value = 0;
        sq_getinteger(v, -1, &value);
        sq_pop(v, 2);
        return value;
    }
    sq_pop(v, 1);
    return VerbConstants::VERB_LOOKAT;
}

void Engine::Impl::updateScreenSize()
{
    if (_pRoom)
    {
        if (_pRoom->getFullscreen() == 1)
        {
            auto roomSize = _pRoom->getRoomSize();
            sf::View view(sf::FloatRect(0, 0, roomSize.x, roomSize.y));
            _pWindow->setView(view);
        }
        else
        {
            auto height = _pRoom->getScreenHeight();
            switch (height)
            {
                case 128:
                {
                    sf::View view(sf::FloatRect(0, 0, 320, 180));
                    _pWindow->setView(view);
                    break;
                }
                case 172:
                {
                    sf::View view(sf::FloatRect(0, 0, 428, 240));
                    _pWindow->setView(view);
                    break;
                }
                case 256:
                {
                    sf::View view(sf::FloatRect(0, 0, 640, 360));
                    _pWindow->setView(view);
                    break;
                }
                default:
                {
                    height = 180.f * height / 128.f;
                    auto ratio = 320.f / 180.f;
                    sf::View view(sf::FloatRect(0, 0, ratio * height, height));
                    _pWindow->setView(view);
                    break;
                }
            }
        }

        auto screen = _pWindow->getView().getSize();
        sf::Vector2f size(screen.x / 6.f, screen.y / 14.f);
        for (auto i = 0; i < 9; i++)
        {
            auto left = (i / 3) * size.x;
            auto top = screen.y - size.y * 3 + (i % 3) * size.y;
            _verbRects.at(i) = sf::IntRect(left, top, size.x, size.y);
        }
    }
}

void Engine::Impl::actorEnter()
{
    if (!_pCurrentActor)
        return;

    ScriptEngine::call("actorEnter", _pCurrentActor);

    if (!_pRoom)
        return;

    ScriptEngine::call(_pRoom, "actorEnter", _pCurrentActor);
    ScriptEngine::call(_pCurrentActor, "actorArrived");
}

void Engine::Impl::actorExit()
{
    if (!_pCurrentActor || !_pRoom)
        return;

    ScriptEngine::call(_pRoom, "actorExit", _pCurrentActor);
}

SQInteger Engine::Impl::enterRoom(Room *pRoom, Object *pObject)
{
    // call enter room function
    trace("call enter room function of {}", pRoom->getId());
    sq_pushobject(_vm, pRoom->getTable());
    sq_pushstring(_vm, _SC("enter"), -1);
    if (SQ_FAILED(sq_rawget(_vm, -2)))
    {
        error("can't find enter function");
        return 0;
    }

    SQInteger nparams, nfreevars;
    sq_getclosureinfo(_vm, -1, &nparams, &nfreevars);
    trace("enter function found with {} parameters", nparams);

    sq_remove(_vm, -2);

    if (nparams == 2)
    {
        ScriptEngine::call(_pRoom, "enter", pObject);
    }
    else
    {
        ScriptEngine::call(_pRoom, "enter");
    }

    actorEnter();

    auto &objects = pRoom->getObjects();
    for (size_t i = 0; i < objects.size(); i++)
    {
        auto &obj = objects[i];

        if (obj->getId().empty())
            continue;

        ScriptEngine::call(obj.get(), "enter");
    }

    ScriptEngine::call("enteredRoom", pRoom);

    return 0;
}

void Engine::Impl::setCurrentRoom(Room *pRoom)
{
    if (pRoom)
    {
        std::ostringstream s;
        s << "currentRoom = " << pRoom->getId();
        _pScriptExecute->execute(s.str());
    }
    _camera.resetBounds();
    _pRoom = pRoom;
    updateScreenSize();
}

SQInteger Engine::setRoom(Room *pRoom)
{
    if (!pRoom)
        return 0;

    _pImpl->_fadeColor = sf::Color::Transparent;

    auto pOldRoom = _pImpl->_pRoom;
    if (pRoom == pOldRoom)
        return 0;

    auto result = _pImpl->exitRoom(nullptr);
    if (SQ_FAILED(result))
        return result;

    if (pRoom->getFullscreen() == 1)
    {
        setInputVerbs(false);
    }
    else if (_pImpl->_pRoom && _pImpl->_pRoom->getFullscreen() == 1)
    {
        setInputVerbs(true);
    }

    _pImpl->setCurrentRoom(pRoom);

    result = _pImpl->enterRoom(pRoom, nullptr);
    if (SQ_FAILED(result))
        return result;

    return 0;
}

SQInteger Engine::enterRoomFromDoor(Object *pDoor)
{
    auto dir = pDoor->getUseDirection();
    Facing facing;
    switch (dir)
    {
        case UseDirection::Back:
            facing = Facing::FACE_FRONT;
            break;
        case UseDirection::Front:
            facing = Facing::FACE_BACK;
            break;
        case UseDirection::Left:
            facing = Facing::FACE_RIGHT;
            break;
        case UseDirection::Right:
            facing = Facing::FACE_LEFT;
            break;
    }
    auto pRoom = pDoor->getRoom();
    auto pOldRoom = _pImpl->_pRoom;
    if (pRoom == pOldRoom)
        return 0;

    auto result = _pImpl->exitRoom(nullptr);
    if (SQ_FAILED(result))
        return result;

    _pImpl->setCurrentRoom(pRoom);

    auto actor = getCurrentActor();
    actor->getCostume().setFacing(facing);

    if (pRoom->getFullscreen() != 1)
    {
        actor->setRoom(pRoom);
        auto pos = pDoor->getRealPosition();
        actor->setPosition(pos + sf::Vector2f(pDoor->getUsePosition().x, -pDoor->getUsePosition().y));
        _pImpl->_camera.at(pos + pDoor->getUsePosition());
    }

    return _pImpl->enterRoom(pRoom, pDoor);
}

void Engine::setInputHUD(bool on) { _pImpl->_inputHUD = on; }

bool Engine::getInputHUD() const { return _pImpl->_inputHUD; }

bool Engine::isCursorVisible() const { return _pImpl->_showCursor; }

void Engine::setInputActive(bool active)
{
    _pImpl->_inputActive = active;
    _pImpl->_showCursor = active;
}

void Engine::inputSilentOff() { _pImpl->_inputActive = false; }

void Engine::setInputVerbs(bool on) { _pImpl->_inputVerbsActive = on; }

std::string Engine::Impl::getVerbName(const Verb &verb) const
{
    auto lang = _preferences.getUserPreference(PreferenceNames::Language, PreferenceDefaultValues::Language);
    auto isRetro = _preferences.getUserPreference(PreferenceNames::RetroVerbs, PreferenceDefaultValues::RetroVerbs);
    std::string s;
    s.append(verb.image).append(isRetro ? "_retro" : "").append("_").append(lang);
    return s;
}

sf::IntRect Engine::Impl::getVerbRect(const Verb &verb) const
{
    auto s = getVerbName(verb);
    return _verbSheet.getRect(s);
}

const Verb *Engine::getVerb(int id) const
{
    auto index = _pImpl->getCurrentActorIndex();
    if (index < 0)
        return nullptr;
    const auto &verbSlot = _pImpl->_verbSlots.at(index);
    for (auto i = 0; i < 10; i++)
    {
        const auto &verb = verbSlot.getVerb(i);
        if (verb.id == id)
        {
            return &verb;
        }
    }
    return nullptr;
}

void Engine::Impl::updateCutscene(const sf::Time &elapsed)
{
    if (_pCutscene)
    {
        (*_pCutscene)(elapsed);
        if (_pCutscene->isElapsed())
        {
            _pCutscene = nullptr;
        }
    }
}

void Engine::Impl::updateSentence(const sf::Time &elapsed)
{
    if(!_pSentence) return;
    (*_pSentence)(elapsed);
    if(!_pSentence->isElapsed()) return;
    _pEngine->stopSentence();
}

void Engine::Impl::updateFunctions(const sf::Time &elapsed)
{
    for (auto &function : _newFunctions)
    {
        _functions.push_back(std::move(function));
    }
    _newFunctions.clear();
    for (auto &function : _functions)
    {
        (*function)(elapsed);
    }
    _functions.erase(std::remove_if(_functions.begin(), _functions.end(),
                                    [](std::unique_ptr<Function> &f) { return f->isElapsed(); }),
                     _functions.end());
    for (auto &actor : _actors)
    {
        actor->update(elapsed);
    }
}

void Engine::Impl::updateActorIcons(const sf::Time &elapsed)
{
    _actorIcons.setMousePosition(_mousePos);
    _actorIcons.update(elapsed);
}

void Engine::Impl::updateMouseCursor()
{
    auto flags = _pObj1 ? getFlags(_pObj1->getTable()) : 0;
    auto screen = _pWindow->getView().getSize();
    _cursorDirection = CursorDirection::None;
    if ((_mousePos.x < 20) || (flags & ObjectFlagConstants::DOOR_LEFT) == ObjectFlagConstants::DOOR_LEFT)
        _cursorDirection |= CursorDirection::Left;
    else if ((_mousePos.x > screen.x - 20) ||
             (flags & ObjectFlagConstants::DOOR_RIGHT) == ObjectFlagConstants::DOOR_RIGHT)
        _cursorDirection |= CursorDirection::Right;
    if ((flags & ObjectFlagConstants::DOOR_FRONT) == ObjectFlagConstants::DOOR_FRONT)
        _cursorDirection |= CursorDirection::Down;
    else if ((flags & ObjectFlagConstants::DOOR_BACK) == ObjectFlagConstants::DOOR_BACK)
        _cursorDirection |= CursorDirection::Up;
    if ((_cursorDirection == CursorDirection::None) && _pObj1)
        _cursorDirection |= CursorDirection::Hotspot;
}

Entity *Engine::Impl::getHoveredEntity(const sf::Vector2f &mousPos)
{
    Entity *pCurrentObject = nullptr;

    // mouse on actor ?
    for (auto &&actor : _actors)
    {
        if (actor.get() == _pCurrentActor)
            continue;
        if (actor->getRoom() != _pRoom)
            continue;

        if (actor->contains(mousPos))
        {
            if (!pCurrentObject || actor->getZOrder() < pCurrentObject->getZOrder())
            {
                pCurrentObject = actor.get();
            }
        }
    }

    // mouse on object ?
    const auto &objects = _pRoom->getObjects();
    std::for_each(objects.cbegin(), objects.cend(), [mousPos, &pCurrentObject](const auto &pObj) {
        if (!pObj->isTouchable())
            return;
        auto rect = pObj->getRealHotspot();
        if (!rect.contains((sf::Vector2i)mousPos))
            return;
        if (!pCurrentObject || pObj->getZOrder() < pCurrentObject->getZOrder())
            pCurrentObject = pObj.get();
    });

    if (!pCurrentObject)
    {
        // mouse on inventory object ?
        pCurrentObject = _inventory.getCurrentInventoryObject();
    }

    return pCurrentObject;
}

void Engine::Impl::updateHoveredEntity(bool isRightClick)
{
    _pVerbOverride = nullptr;
    if (!_pVerb)
    {
        _pVerb = _pEngine->getVerb(VerbConstants::VERB_WALKTO);
    }

    if (_pUseObject)
    {
        _pObj1 = _pUseObject;
        _pObj2 = _pHoveredEntity;
    }
    else
    {
        _pObj1 = _pHoveredEntity;
        _pObj2 = nullptr;
    }

    // abort some invalid actions
    if (!_pObj1 || !_pVerb)
    {
        return;
    }

    if (_pObj2 == _pObj1)
    {
        _pObj2 = nullptr;
    }

    if (_pObj1 && isRightClick)
    {
        _pVerbOverride = _pEngine->getVerb(getDefaultVerb(_vm, _pObj1));
    }

    if (_pVerb->id == VerbConstants::VERB_WALKTO)
    {
        if (_pObj1 && _pObj1->isInventoryObject())
        {
            _pVerbOverride = _pEngine->getVerb(getDefaultVerb(_vm, _pObj1));
        }
    }
    else if (_pVerb->id == VerbConstants::VERB_TALKTO)
    {
        // select actor/object only if talkable flag is set
        auto flags = getFlags(_pObj1->getTable());
        if (!(flags & ObjectFlagConstants::TALKABLE))
            _pObj1 = nullptr;
    }
    else if (_pVerb->id == VerbConstants::VERB_GIVE)
    {
        if (!_pObj1->isInventoryObject())
            _pObj1 = nullptr;

        // select actor/object only if giveable flag is set
        if (_pObj2)
        {
            auto flags = getFlags(_pObj2->getTable());
            if (!(flags & ObjectFlagConstants::GIVEABLE))
                _pObj2 = nullptr;
        }
    }
}

int32_t Engine::Impl::getFlags(HSQOBJECT obj) const
{
    SQInteger flags = 0;
    sq_pushobject(_vm, obj);
    sq_pushstring(_vm, _SC("flags"), -1);
    if (SQ_SUCCEEDED(sq_rawget(_vm, -2)))
    {
        sq_getinteger(_vm, -1, &flags);
        sq_pop(_vm, 1);
    }
    sq_pop(_vm, 1);
    return flags;
}

void Engine::Impl::updateRoomScalings()
{
    auto actor = _pCurrentActor;
    if (!actor)
        return;

    auto &scalings = _pRoom->getScalings();
    auto &objects = _pRoom->getObjects();
    for (auto &&object : objects)
    {
        if (!object->isTrigger())
            continue;
        if (object->getRealHotspot().contains((sf::Vector2i)actor->getPosition()))
        {
            auto it = std::find_if(scalings.begin(), scalings.end(), [&object](const RoomScaling &s) {
                return s.getName() == tostring(object->getId());
            });
            if (it != scalings.end())
            {
                _pRoom->setRoomScaling(*it);
                return;
            }
        }
    }
    if (!scalings.empty())
    {
        _pRoom->setRoomScaling(scalings[0]);
    }
}

void Engine::update(const sf::Time &elapsed)
{
    ImGuiIO &io = ImGui::GetIO();
    _pImpl->_frameCounter++;
    auto wasMouseDown = !io.WantCaptureMouse && _pImpl->_isMouseDown;
    auto wasMouseRightDown = !io.WantCaptureMouse && _pImpl->_isMouseRightDown;
    _pImpl->_isMouseDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) && _pImpl->_pWindow->hasFocus();
    _pImpl->_isMouseRightDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Right) && _pImpl->_pWindow->hasFocus();
    bool isRightClick = wasMouseRightDown != _pImpl->_isMouseRightDown && !_pImpl->_isMouseRightDown;
    auto isMouseClick = wasMouseDown != _pImpl->_isMouseDown && !_pImpl->_isMouseDown;

    _pImpl->_time += elapsed;

    _pImpl->_camera.update(elapsed);
    _pImpl->_soundManager.update(elapsed);
    _pImpl->updateCutscene(elapsed);
    _pImpl->updateFunctions(elapsed);
    _pImpl->updateSentence(elapsed);

    if (!_pImpl->_pRoom)
        return;

    _pImpl->updateRoomScalings();

    auto screen = _pImpl->_pWindow->getView().getSize();
    _pImpl->_pRoom->update(elapsed);
    if (_pImpl->_pFollowActor && _pImpl->_pFollowActor->isVisible())
    {
        auto pos = _pImpl->_pFollowActor->getPosition() - sf::Vector2f(screen.x / 2, screen.y / 2);
        auto margin = screen.x / 4;
        auto cameraPos = _pImpl->_camera.getAt();
        if (_pImpl->_camera.isMoving() || (cameraPos.x > pos.x + margin) || (cameraPos.x < pos.x - margin))
        {
            _pImpl->_camera.panTo(pos, sf::seconds(4), InterpolationMethod::EaseOut);
        }
    }

    _pImpl->_mousePos = _pImpl->_pWindow->mapPixelToCoords(sf::Mouse::getPosition(*_pImpl->_pWindow));
    _pImpl->updateActorIcons(elapsed);

    _pImpl->_cursorDirection = CursorDirection::None;
    _pImpl->updateMouseCursor();

    _pImpl->_mousePosInRoom = _pImpl->_mousePos + _pImpl->_camera.getAt();

    _pImpl->_inventory.setMousePosition(_pImpl->_mousePos);
    _pImpl->_inventory.update(elapsed);
    _pImpl->_dialogManager.update(elapsed);
    _pImpl->_pHoveredEntity = _pImpl->getHoveredEntity(_pImpl->_mousePosInRoom);
    _pImpl->updateHoveredEntity(isRightClick);

    if (_pImpl->_pCurrentActor)
    {
        auto &objects = _pImpl->_pCurrentActor->getObjects();
        for (auto &object : objects)
        {
            object->update(elapsed);
        }
    }

    if (!_pImpl->_inputActive)
        return;

    if (!isMouseClick && !isRightClick)
        return;

    if (_pImpl->clickedAt(_pImpl->_mousePosInRoom))
        return;

    if (_pImpl->_dialogManager.getState() != DialogManagerState::None)
        return;

    if (_pImpl->_actorIcons.isMouseOver())
        return;

    int currentActorIndex = _pImpl->getCurrentActorIndex();
    if (currentActorIndex == -1)
        return;

    // input click on a verb ?
    if (_pImpl->_inputVerbsActive)
    {
        for (size_t i = 0; i < _pImpl->_verbRects.size(); i++)
        {
            if (_pImpl->_verbRects.at(i).contains((sf::Vector2i)_pImpl->_mousePos))
            {
                auto verbId = _pImpl->_verbSlots.at(currentActorIndex).getVerb(1 + i).id;
                _pImpl->_pVerb = getVerb(verbId);
                _pImpl->_useFlag = UseFlag::None;
                _pImpl->_pUseObject = nullptr;
                _pImpl->_pObj1 = nullptr;
                _pImpl->_pObj2 = nullptr;

                ScriptEngine::call("onVerbClick");
                return;
            }
        }
    }

    if (_pImpl->_pHoveredEntity)
    {
        ScriptEngine::call("onObjectClick", _pImpl->_pHoveredEntity);
        auto pVerb = _pImpl->_pVerbOverride;
        if (!pVerb)
        {
            pVerb = _pImpl->_pVerb;
        }
        if (_pImpl->_pObj1)
        {
            _pImpl->_pVerbExecute->execute(pVerb, _pImpl->_pObj1, _pImpl->_pObj2);
        }
        return;
    }

    _pImpl->_pCurrentActor->walkTo(_pImpl->_mousePosInRoom);
    setDefaultVerb();
}

void Engine::setCurrentActor(Actor *pCurrentActor, bool userSelected)
{
    _pImpl->_pCurrentActor = pCurrentActor;
    if (_pImpl->_pCurrentActor)
    {
        follow(_pImpl->_pCurrentActor);
    }

    ScriptEngine::call("onActorSelected", pCurrentActor, userSelected);
}

bool Engine::Impl::clickedAt(const sf::Vector2f &pos)
{
    if (!_pRoom)
        return false;

    bool handled = false;
    ScriptEngine::callFunc(handled, _pRoom, "clickedAt", pos.x, pos.y);
    return handled;
}

void Engine::draw(sf::RenderWindow &window) const
{
    if (!_pImpl->_pRoom)
        return;

    _pImpl->_pRoom->draw(window, _pImpl->_camera.getAt());
    _pImpl->drawFade(window);
    _pImpl->_pRoom->drawForeground(window, _pImpl->_camera.getAt());

    window.draw(_pImpl->_dialogManager);

    if ((_pImpl->_dialogManager.getState() == DialogManagerState::None) && _pImpl->_inputActive)
    {
        _pImpl->drawVerbs(window);
        if (_pImpl->_inputHUD)
        {
            window.draw(_pImpl->_inventory);
        }
        window.draw(_pImpl->_actorIcons);
    }

    _pImpl->drawCursor(window);
    _pImpl->drawCursorText(window);
    _pImpl->_pDebugTools->render();
}

void Engine::Impl::drawCursor(sf::RenderWindow &window) const
{
    if (!_showCursor)
        return;

    auto screen = _pWindow->getView().getSize();
    auto cursorSize = sf::Vector2f(68.f * screen.x / 1284, 68.f * screen.y / 772);
    sf::RectangleShape shape;
    shape.setPosition(_mousePos);
    shape.setOrigin(cursorSize / 2.f);
    shape.setSize(cursorSize);
    shape.setTexture(&_gameSheet.getTexture());

    shape.setTextureRect(getCursorRect());
    window.draw(shape);
}

sf::IntRect Engine::Impl::getCursorRect() const
{
    if (_dialogManager.getState() != DialogManagerState::None)
        return _gameSheet.getRect("cursor");

    if (_cursorDirection & CursorDirection::Left)
    {
        return _cursorDirection & CursorDirection::Hotspot ? _gameSheet.getRect("hotspot_cursor_left")
                                                           : _gameSheet.getRect("cursor_left");
    }
    if (_cursorDirection & CursorDirection::Right)
    {
        return _cursorDirection & CursorDirection::Hotspot ? _gameSheet.getRect("hotspot_cursor_right")
                                                           : _gameSheet.getRect("cursor_right");
    }
    if (_cursorDirection & CursorDirection::Up)
    {
        return _cursorDirection & CursorDirection::Hotspot ? _gameSheet.getRect("hotspot_cursor_back")
                                                           : _gameSheet.getRect("cursor_back");
    }
    if (_cursorDirection & CursorDirection::Down)
    {
        return (_cursorDirection & CursorDirection::Hotspot) ? _gameSheet.getRect("hotspot_cursor_front")
                                                             : _gameSheet.getRect("cursor_front");
    }
    return (_cursorDirection & CursorDirection::Hotspot) ? _gameSheet.getRect("hotspot_cursor")
                                                         : _gameSheet.getRect("cursor");
}

void Engine::Impl::drawCursorText(sf::RenderWindow &window) const
{
    if (!_showCursor)
        return;

    if (_dialogManager.getState() != DialogManagerState::None)
        return;

    auto pVerb = _pVerbOverride;
    if (!pVerb)
        pVerb = _pVerb;
    if (!pVerb)
        return;

    auto currentActorIndex = getCurrentActorIndex();
    if(currentActorIndex == -1)
        return;

    NGText text;
    text.setAlignment(NGTextAlignment::Center);
    text.setFont(_fntFont);
    text.setColor(_verbUiColors.at(currentActorIndex).sentence);

    std::wstring s;
    if (pVerb->id != VerbConstants::VERB_WALKTO || _pHoveredEntity)
    {
        auto id = std::strtol(pVerb->text.substr(1).data(), nullptr, 10);
        s.append(_pEngine->getText(id));
    }
    if (_pObj1)
    {
        s.append(L" ").append(_pObj1->getName());
    }
    appendUseFlag(s);
    if (_pObj2)
    {
        s.append(L" ").append(_pObj2->getName());
    }
    text.setText(s);

    // do display cursor position:
    // auto mousePosInRoom = _mousePos + _camera.getAt();
    // std::wstringstream s;
    // std::wstring txt = text.getText();
    // s << txt << L" (" << std::fixed << std::setprecision(0) << mousePosInRoom.x << L"," << mousePosInRoom.y << L")";
    // text.setText(s.str());

    auto screen = _pWindow->getView().getSize();
    auto y = _mousePos.y - 22 < 8 ? _mousePos.y + 8 : _mousePos.y - 22;
    if (y < 0)
        y = 0;
    auto x = std::clamp((int)_mousePos.x, 20, (int)screen.x - 20 - (int)text.getBoundRect().width / 2);
    text.setPosition(x, y);
    window.draw(text, sf::RenderStates::Default);
}

void Engine::Impl::appendUseFlag(std::wstring &sentence) const
{
    switch (_useFlag)
    {
        case UseFlag::UseWith:
            sentence.append(L" ").append(_pEngine->getText(10000));
            break;
        case UseFlag::UseOn:
            sentence.append(L" ").append(_pEngine->getText(10001));
            break;
        case UseFlag::UseIn:
            sentence.append(L" ").append(_pEngine->getText(10002));
            break;
        case UseFlag::GiveTo:
            sentence.append(L" ").append(_pEngine->getText(10003));
            break;
        case UseFlag::None:
            break;
    }
}

int Engine::Impl::getCurrentActorIndex() const
{
    for (auto i = 0; i < _actorsIconSlots.size(); i++)
    {
        const auto &selectableActor = _actorsIconSlots.at(i);
        if (selectableActor.pActor == _pCurrentActor)
        {
            return i;
        }
    }
    return -1;
}

void Engine::Impl::drawVerbs(sf::RenderWindow &window) const
{
    if (!_inputVerbsActive)
        return;

    int currentActorIndex = getCurrentActorIndex();
    if (currentActorIndex == -1 || _verbSlots.at(currentActorIndex).getVerb(0).id == 0)
        return;

    auto pVerb = _pVerbOverride;
    if (!pVerb)
    {
        pVerb = _pVerb;
    }
    auto verbId = pVerb->id;
    if (_pHoveredEntity && verbId == VerbConstants::VERB_WALKTO)
    {
        verbId = getDefaultVerb(_vm, _pHoveredEntity);
    }
    else
    {
        for (size_t i = 0; i < _verbRects.size(); i++)
        {
            if (_verbRects.at(i).contains((sf::Vector2i)_mousePos))
            {
                verbId = _verbSlots.at(currentActorIndex).getVerb(1 + i).id;
                break;
            }
        }
    }

    const auto view = window.getView();
    window.setView(sf::View(sf::FloatRect(0, 0, Screen::Width, Screen::Height)));

    // draw UI background
    auto hudSentence = _preferences.getUserPreference(PreferenceNames::HudSentence, PreferenceDefaultValues::HudSentence);
    auto uiBackingAlpha = _preferences.getUserPreference(PreferenceNames::UiBackingAlpha, PreferenceDefaultValues::UiBackingAlpha);
    auto invertVerbHighlight = _preferences.getUserPreference(PreferenceNames::InvertVerbHighlight, PreferenceDefaultValues::InvertVerbHighlight);
    sf::Color verbNormal; 
    sf::Color verbHighlight;
    if(invertVerbHighlight)
    {
        verbNormal = _verbUiColors.at(currentActorIndex).verbHighlightTint;
        verbHighlight = _verbUiColors.at(currentActorIndex).verbNormalTint;
    }
    else
    {
        verbNormal = _verbUiColors.at(currentActorIndex).verbNormalTint;
        verbHighlight = _verbUiColors.at(currentActorIndex).verbHighlightTint;
    }
    
    auto uiBackingRect = hudSentence ? _gameSheet.getRect("ui_backing_tall") : _gameSheet.getRect("ui_backing");
    sf::Sprite uiBacking;
    uiBacking.setColor(sf::Color(0, 0, 0, uiBackingAlpha * 255));
    uiBacking.setPosition(0, 720 - uiBackingRect.height);
    uiBacking.setTexture(_gameSheet.getTexture());
    uiBacking.setTextureRect(uiBackingRect);
    window.draw(uiBacking);

    // draw verbs
    sf::Vector2f size(uiBackingRect.width / 6, uiBackingRect.height / 3);
    for (int x = 0; x < 3; x++)
    {
        for (int y = 0; y < 3; y++)
        {
            auto verb = _verbSlots.at(currentActorIndex).getVerb(x * 3 + y + 1);
            auto rect = getVerbRect(verb);
            auto color = verb.id == verbId ? verbNormal : verbHighlight;
            auto s = getVerbName(verb);
            auto r = _verbSheet.getSpriteSourceSize(s);
            sf::Sprite verbSprite;
            verbSprite.setColor(color);
            verbSprite.setPosition(r.left, r.top);
            verbSprite.setTexture(_verbSheet.getTexture());
            verbSprite.setTextureRect(rect);
            window.draw(verbSprite);
        }
    }

    window.setView(view);
}

bool Engine::isThreadAlive(HSQUIRRELVM thread) const
{
    auto pRoom = _pImpl->_pRoom;
    if (pRoom && pRoom->isThreadAlive(thread))
        return true;

    return std::find_if(_pImpl->_threads.begin(), _pImpl->_threads.end(),
                        [&thread](const std::unique_ptr<ThreadBase> &t) { return t->getThread() == thread; }) !=
           _pImpl->_threads.end();
}

void Engine::startDialog(const std::string &dialog, const std::string &node)
{
    _pImpl->_dialogManager.start(dialog, node);
}

void Engine::execute(const std::string &code) { _pImpl->_pScriptExecute->execute(code); }

SoundDefinition *Engine::getSoundDefinition(const std::string &name)
{
    return _pImpl->_pScriptExecute->getSoundDefinition(name);
}

bool Engine::executeCondition(const std::string &code) { return _pImpl->_pScriptExecute->executeCondition(code); }

std::string Engine::executeDollar(const std::string &code) { return _pImpl->_pScriptExecute->executeDollar(code); }

void Engine::stopThread(HSQUIRRELVM thread)
{
    auto pRoom = getRoom();
    if (pRoom && pRoom->isThreadAlive(thread))
    {
        pRoom->stopThread(thread);
        return;
    }
    auto it = std::find_if(_pImpl->_threads.begin(), _pImpl->_threads.end(),
                           [&thread](const std::unique_ptr<ThreadBase> &t) { return t->getThread() == thread; });
    if (it == _pImpl->_threads.end())
        return;
    _pImpl->_threads.erase(it);
}

void Engine::addSelectableActor(int index, Actor *pActor)
{
    _pImpl->_actorsIconSlots.at(index - 1).selectable = true;
    _pImpl->_actorsIconSlots.at(index - 1).pActor = pActor;
}

void Engine::actorSlotSelectable(Actor *pActor, bool selectable)
{
    auto it = std::find_if(_pImpl->_actorsIconSlots.begin(), _pImpl->_actorsIconSlots.end(),
                           [&pActor](auto &selectableActor) { return selectableActor.pActor == pActor; });
    if (it != _pImpl->_actorsIconSlots.end())
    {
        it->selectable = selectable;
    }
}

void Engine::actorSlotSelectable(int index, bool selectable)
{
    _pImpl->_actorsIconSlots.at(index - 1).selectable = selectable;
}

void Engine::setActorSlotSelectable(ActorSlotSelectableMode mode) { _pImpl->_actorIcons.setMode(mode); }

void Engine::setUseFlag(UseFlag flag, Entity *object)
{
    _pImpl->_useFlag = flag;
    _pImpl->_pUseObject = object;
}

void Engine::cutsceneOverride()
{
    if (!_pImpl->_pCutscene)
        return;
    _pImpl->_pCutscene->cutsceneOverride();
}

void Engine::cutscene(std::unique_ptr<Cutscene> function)
{
    _pImpl->_pCutscene = function.get();
    addThread(std::move(function));
}

bool Engine::inCutscene() const { return _pImpl->_pCutscene && !_pImpl->_pCutscene->isElapsed(); }

HSQOBJECT &Engine::getDefaultObject() { return _pImpl->_pDefaultObject; }

void Engine::flashSelectableActor(bool on) { _pImpl->_actorIcons.flash(on); }

const Verb *Engine::getActiveVerb() const { return _pImpl->_pVerb; }

void Engine::setFadeAlpha(float fade) { _pImpl->_fadeColor.a = static_cast<uint8_t>(fade * 255); }

float Engine::getFadeAlpha() const { return _pImpl->_fadeColor.a / 255.f; }

void Engine::fadeTo(float destination, sf::Time time, InterpolationMethod method)
{
    auto m = ScriptEngine::getInterpolationMethod(method);
    auto get = [this](){return getFadeAlpha();};
    auto set = [this](const float& a){ setFadeAlpha(a);};
    auto f = std::make_unique<ChangeProperty<float>>(get, set, destination, time, m);
    _pImpl->_functions.push_back(std::move(f));
}

void Engine::pushSentence(int id, Entity* pObj1, Entity* pObj2)
{
    const Verb* pVerb = getVerb(id);
    if(!pVerb) return;
    _pImpl->_pVerbExecute->execute(pVerb, pObj1, pObj2);
}

void Engine::setSentence(std::unique_ptr<Sentence> sentence)
{
    _pImpl->_pSentence = std::move(sentence);
}

void Engine::stopSentence()
{
    if(!_pImpl->_pSentence) return;
    _pImpl->_pSentence->stop();
    _pImpl->_pSentence.reset();
}

} // namespace ng
