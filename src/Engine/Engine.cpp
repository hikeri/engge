#include "squirrel.h"
#include "Engine/Engine.hpp"
#include "Engine/ActorIconSlot.hpp"
#include "Engine/ActorIcons.hpp"
#include "Engine/Camera.hpp"
#include "Engine/Cutscene.hpp"
#include "Engine/Hud.hpp"
#include "Dialog/DialogManager.hpp"
#include "Font/GGFont.hpp"
#include "Math/PathFinding/Graph.hpp"
#include "Engine/Inventory.hpp"
#include "UI/OptionsDialog.hpp"
#include "UI/StartScreenDialog.hpp"
#include "Engine/Preferences.hpp"
#include "Room/Room.hpp"
#include "Room/RoomScaling.hpp"
#include "Graphics/Screen.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptExecute.hpp"
#include "Engine/Sentence.hpp"
#include "Audio/SoundDefinition.hpp"
#include "Audio/SoundId.hpp"
#include "Audio/SoundManager.hpp"
#include "Graphics/SpriteSheet.hpp"
#include "Engine/TextDatabase.hpp"
#include "Engine/Thread.hpp"
#include "Engine/Verb.hpp"
#include "Graphics/Text.hpp"
#include "Scripting/VerbExecute.hpp"
#include "../System/_DebugTools.hpp"
#include "../System/_Util.hpp"
#include "../Entities/Actor/_TalkingState.hpp"
#include "System/Logger.hpp"
#include "../Math/PathFinding/_WalkboxDrawable.hpp"
#include <iostream>
#include <cmath>
#include <memory>
#include <set>
#include <string>

namespace ng {
CursorDirection operator|=(CursorDirection &lhs, CursorDirection rhs) {
  lhs = static_cast<CursorDirection>(static_cast<std::underlying_type<CursorDirection>::type>(lhs) |
      static_cast<std::underlying_type<CursorDirection>::type>(rhs));
  return lhs;
}

bool operator&(CursorDirection lhs, CursorDirection rhs) {
  return static_cast<CursorDirection>(static_cast<std::underlying_type<CursorDirection>::type>(lhs) &
      static_cast<std::underlying_type<CursorDirection>::type>(rhs)) >
      CursorDirection::None;
}

enum class EngineState {
  Game, Paused, Options, StartScreen
};

struct Engine::Impl {
  Engine *_pEngine{nullptr};
  std::unique_ptr<_DebugTools> _pDebugTools;
  TextureManager _textureManager;
  Room *_pRoom{nullptr};
  std::vector<std::unique_ptr<Actor>> _actors;
  std::vector<std::unique_ptr<Room>> _rooms;
  std::vector<std::unique_ptr<Function>> _newFunctions;
  std::vector<std::unique_ptr<Function>> _functions;
  std::vector<std::unique_ptr<Callback>> _callbacks;
  Cutscene *_pCutscene{nullptr};
  sf::RenderWindow *_pWindow{nullptr};
  TextDatabase _textDb;
  Actor *_pCurrentActor{nullptr};
  bool _inputHUD{false};
  bool _inputActive{false};
  bool _showCursor{true};
  bool _inputVerbsActive{false};
  SpriteSheet _gameSheet, _saveLoadSheet;
  Actor *_pFollowActor{nullptr};
  Entity *_pUseObject{nullptr};
  Entity *_pObj1{nullptr};
  Entity *_pObj2{nullptr};
  sf::Vector2f _mousePos;
  sf::Vector2f _mousePosInRoom;
  std::unique_ptr<VerbExecute> _pVerbExecute;
  std::unique_ptr<ScriptExecute> _pScriptExecute;
  std::vector<std::unique_ptr<ThreadBase>> _threads;
  DialogManager _dialogManager;
  Preferences &_preferences;
  SoundManager &_soundManager;
  CursorDirection _cursorDirection{CursorDirection::None};
  std::array<ActorIconSlot, 6> _actorsIconSlots;
  UseFlag _useFlag{UseFlag::None};
  ActorIcons _actorIcons;
  HSQUIRRELVM _vm{};
  sf::Time _time;
  bool _isMouseDown{false};
  sf::Time _mouseDownTime;
  bool _isMouseRightDown{false};
  int _frameCounter{0};
  HSQOBJECT _pDefaultObject{};
  Camera _camera;
  sf::Color _fadeColor{sf::Color::Transparent};
  std::unique_ptr<Sentence> _pSentence{};
  std::set<int> _oldKeyDowns;
  std::set<int> _newKeyDowns;
  EngineState _state{EngineState::StartScreen};
  _TalkingState _talkingState;
  int _showDrawWalkboxes{0};
  OptionsDialog _optionsDialog;
  StartScreenDialog _startScreenDialog;
  bool _run{false};
  sf::Time _noOverrideElapsed{sf::seconds(2)};
  Hud _hud;

  Impl();

  void drawHud(sf::RenderWindow &window) const;
  void drawCursor(sf::RenderWindow &window) const;
  void drawCursorText(sf::RenderTarget &target) const;
  void drawNoOverride(sf::RenderTarget &target) const;
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
  int getFlags(Entity *pEntity) const;
  int getDefaultVerb(Entity *pEntity) const;
  Entity *getHoveredEntity(const sf::Vector2f &mousPos);
  void actorEnter();
  void actorExit();
  void onLanguageChange(const std::string &lang);
  void drawFade(sf::RenderTarget &target) const;
  void onVerbClick(const Verb *pVerb);
  void updateKeyboard();
  bool isKeyPressed(int key);
  void updateKeys();
  static int toKey(const std::string &keyText);
  void drawPause(sf::RenderTarget &target) const;
  void stopThreads();
  void drawWalkboxes(sf::RenderTarget &target) const;
  const Verb *getHoveredVerb() const;
  std::wstring getDisplayName(const std::wstring &name) const;
  void run(bool state);
  void stopAllTalking();
  Entity *getEntity(Entity *pEntity) const;
  const Verb *overrideVerb(const Verb *pVerb) const;
};

Engine::Impl::Impl()
    : _preferences(Locator<Preferences>::get()),
      _soundManager(Locator<SoundManager>::get()),
      _actorIcons(_actorsIconSlots, _hud, _pCurrentActor) {
  _gameSheet.setTextureManager(&_textureManager);
  _saveLoadSheet.setTextureManager(&_textureManager);
  _hud.setTextureManager(&_textureManager);
  sq_resetobject(&_pDefaultObject);
}

void Engine::Impl::onLanguageChange(const std::string &lang) {
  std::stringstream ss;
  ss << "ThimbleweedText_" << lang << ".tsv";
  _textDb.load(ss.str());

  ScriptEngine::call("onLanguageChange");
}

void Engine::Impl::drawFade(sf::RenderTarget &target) const {
  sf::RectangleShape fadeShape;
  auto screen = target.getView().getSize();
  fadeShape.setSize(sf::Vector2f(screen.x, screen.y));
  fadeShape.setFillColor(_fadeColor);
  target.draw(fadeShape);
}

Engine::Engine() : _pImpl(std::make_unique<Impl>()) {
  time_t t;
  auto seed = (unsigned) time(&t);
  info("seed: {}", seed);
  srand(seed);

  _pImpl->_pEngine = this;
  _pImpl->_pDebugTools = std::make_unique<_DebugTools>(*this);
  _pImpl->_soundManager.setEngine(this);
  _pImpl->_dialogManager.setEngine(this);
  _pImpl->_actorIcons.setEngine(this);
  _pImpl->_camera.setEngine(this);
  _pImpl->_talkingState.setEngine(this);

  // load all messages
  std::stringstream s;
  auto lang =
      _pImpl->_preferences.getUserPreference<std::string>(PreferenceNames::Language, PreferenceDefaultValues::Language);
  s << "ThimbleweedText_" << lang << ".tsv";
  _pImpl->_textDb.load(s.str());

  _pImpl->_optionsDialog.setEngine(this);
  _pImpl->_optionsDialog.setCallback([this]() {
    showOptions(false);
  });
  _pImpl->_startScreenDialog.setEngine(this);
  _pImpl->_startScreenDialog.setNewGameCallback([this]() {
    _pImpl->_state = EngineState::Game;
    _pImpl->exitRoom(nullptr);
    ScriptEngine::call("start", true);
  });

  _pImpl->_gameSheet.load("GameSheet");
  _pImpl->_saveLoadSheet.load("SaveLoadSheet");

  _pImpl->_preferences.subscribe([this](const std::string &name) {
    if (name == PreferenceNames::Language) {
      auto newLang = _pImpl->_preferences.getUserPreference<std::string>(PreferenceNames::Language,
                                                                         PreferenceDefaultValues::Language);
      _pImpl->onLanguageChange(newLang);
    }
  });
}

Engine::~Engine() = default;

int Engine::getFrameCounter() const { return _pImpl->_frameCounter; }

void Engine::setWindow(sf::RenderWindow &window) { _pImpl->_pWindow = &window; }

const sf::RenderWindow &Engine::getWindow() const { return *_pImpl->_pWindow; }

TextureManager &Engine::getTextureManager() { return _pImpl->_textureManager; }

Room *Engine::getRoom() { return _pImpl->_pRoom; }

std::wstring Engine::getText(int id) const {
  auto text = _pImpl->_textDb.getText(id);
  replaceAll(text, L"\\\"", L"\"");
  removeFirstParenthesis(text);
  return text;
}

std::wstring Engine::getText(const std::string &text) const {
  if (!text.empty() && text[0] == '@') {
    auto id = std::strtol(text.c_str() + 1, nullptr, 10);
    return getText(id);
  }
  return towstring(text);
}

void Engine::addActor(std::unique_ptr<Actor> actor) { _pImpl->_actors.push_back(std::move(actor)); }

void Engine::addRoom(std::unique_ptr<Room> room) { _pImpl->_rooms.push_back(std::move(room)); }

std::vector<std::unique_ptr<Room>> &Engine::getRooms() { return _pImpl->_rooms; }

void Engine::addFunction(std::unique_ptr<Function> function) { _pImpl->_newFunctions.push_back(std::move(function)); }

void Engine::addCallback(std::unique_ptr<Callback> callback) { _pImpl->_callbacks.push_back(std::move(callback)); }

void Engine::removeCallback(int id) {
  auto it = std::find_if(_pImpl->_callbacks.begin(), _pImpl->_callbacks.end(),
                         [id](auto &callback) -> bool { return callback->getId() == id; });
  if (it != _pImpl->_callbacks.end()) {
    _pImpl->_callbacks.erase(it);
  }
}

std::vector<std::unique_ptr<Actor>> &Engine::getActors() { return _pImpl->_actors; }

Actor *Engine::getCurrentActor() { return _pImpl->_pCurrentActor; }

const VerbUiColors *Engine::getVerbUiColors(const std::string &name) const {
  if (name.empty()) {
    auto index = _pImpl->getCurrentActorIndex();
    if (index == -1)
      return nullptr;
    return &_pImpl->_hud.getVerbUiColors(index);
  }
  for (int i = 0; i < static_cast<int>(_pImpl->_actorsIconSlots.size()); i++) {
    const auto &selectableActor = _pImpl->_actorsIconSlots.at(i);
    if (selectableActor.pActor->getKey() == name) {
      return &_pImpl->_hud.getVerbUiColors(i);
    }
  }
  return nullptr;
}

bool Engine::getInputActive() const { return _pImpl->_inputActive; }

void Engine::setInputState(int state) {
  if ((state & InputStateConstants::UI_INPUT_ON) == InputStateConstants::UI_INPUT_ON) {
    _pImpl->_inputHUD = true;
  }
  if ((state & InputStateConstants::UI_INPUT_OFF) == InputStateConstants::UI_INPUT_OFF) {
    _pImpl->_inputHUD = false;
  }
  if ((state & InputStateConstants::UI_VERBS_ON) == InputStateConstants::UI_VERBS_ON) {
    _pImpl->_inputVerbsActive = true;
  }
  if ((state & InputStateConstants::UI_VERBS_OFF) == InputStateConstants::UI_VERBS_OFF) {
    _pImpl->_inputVerbsActive = false;
  }
  if ((state & InputStateConstants::UI_CURSOR_ON) == InputStateConstants::UI_CURSOR_ON) {
    _pImpl->_showCursor = true;
  }
  if ((state & InputStateConstants::UI_CURSOR_OFF) == InputStateConstants::UI_CURSOR_OFF) {
    _pImpl->_showCursor = false;
  }
  if ((state & InputStateConstants::UI_HUDOBJECTS_ON) == InputStateConstants::UI_HUDOBJECTS_ON) {
    _pImpl->_inputHUD = true;
  }
  if ((state & InputStateConstants::UI_HUDOBJECTS_OFF) == InputStateConstants::UI_HUDOBJECTS_OFF) {
    _pImpl->_inputHUD = false;
  }
}

int Engine::getInputState() const {
  int inputState = 0;
  inputState |= (_pImpl->_inputActive ? InputStateConstants::UI_INPUT_ON : InputStateConstants::UI_INPUT_OFF);
  inputState |= (_pImpl->_inputVerbsActive ? InputStateConstants::UI_VERBS_ON : InputStateConstants::UI_VERBS_OFF);
  inputState |= (_pImpl->_showCursor ? InputStateConstants::UI_CURSOR_ON : InputStateConstants::UI_CURSOR_OFF);
  inputState |= (_pImpl->_inputHUD ? InputStateConstants::UI_HUDOBJECTS_ON : InputStateConstants::UI_HUDOBJECTS_OFF);
  return inputState;
}

void Engine::follow(Actor *pActor) {
  auto panCamera =
      (_pImpl->_pFollowActor && pActor && _pImpl->_pFollowActor != pActor && _pImpl->_pFollowActor->getRoom() &&
          pActor->getRoom() && _pImpl->_pFollowActor->getRoom()->getId() == pActor->getRoom()->getId());
  _pImpl->_pFollowActor = pActor;
  if (!pActor)
    return;

  auto pos = pActor->getRealPosition();
  auto screen = _pImpl->_pWindow->getView().getSize();
  setRoom(pActor->getRoom());
  if (panCamera) {
    _pImpl->_camera.panTo(pos - sf::Vector2f(screen.x / 2, screen.y / 2), sf::seconds(4),
                          InterpolationMethod::EaseOut);
    return;
  }
  _pImpl->_camera.at(pos - sf::Vector2f(screen.x / 2, screen.y / 2));
}

void Engine::setVerbExecute(std::unique_ptr<VerbExecute> verbExecute) {
  _pImpl->_pVerbExecute = std::move(verbExecute);
}

void Engine::setDefaultVerb() {
  _pImpl->_hud.setHoveredEntity(nullptr);
  auto index = _pImpl->getCurrentActorIndex();
  if (index == -1)
    return;

  const auto &verbSlot = _pImpl->_hud.getVerbSlot(index);
  _pImpl->_hud.setCurrentVerb(&verbSlot.getVerb(0));
  _pImpl->_useFlag = UseFlag::None;
  _pImpl->_pUseObject = nullptr;
  _pImpl->_pObj1 = nullptr;
  _pImpl->_pObj2 = nullptr;
}

void Engine::setScriptExecute(std::unique_ptr<ScriptExecute> scriptExecute) {
  _pImpl->_pScriptExecute = std::move(scriptExecute);
}

void Engine::addThread(std::unique_ptr<ThreadBase> thread) { _pImpl->_threads.push_back(std::move(thread)); }

std::vector<std::unique_ptr<ThreadBase>> &Engine::getThreads() { return _pImpl->_threads; }

sf::Vector2f Engine::getMousePositionInRoom() const { return _pImpl->_mousePosInRoom; }

Preferences &Engine::getPreferences() { return _pImpl->_preferences; }

SoundManager &Engine::getSoundManager() { return _pImpl->_soundManager; }

DialogManager &Engine::getDialogManager() { return _pImpl->_dialogManager; }

Camera &Engine::getCamera() { return _pImpl->_camera; }

sf::Time Engine::getTime() const { return _pImpl->_time; }

void Engine::setVm(HSQUIRRELVM vm) { _pImpl->_vm = vm; }

HSQUIRRELVM Engine::getVm() { return _pImpl->_vm; }

SQInteger Engine::Impl::exitRoom(Object *pObject) {
  _pEngine->setDefaultVerb();
  _talkingState.stop();

  if (!_pRoom)
    return 0;

  auto pOldRoom = _pRoom;

  // call exit room function
  trace("call exit room function of {}", pOldRoom->getId());

  sq_pushobject(_vm, pOldRoom->getTable());
  sq_pushstring(_vm, _SC("exit"), -1);
  if (SQ_FAILED(sq_get(_vm, -2))) {
    error("can't find exit function");
    return 0;
  }

  SQInteger nparams, nfreevars;
  sq_getclosureinfo(_vm, -1, &nparams, &nfreevars);
  trace("enter function found with {} parameters", nparams);

  actorExit();

  sq_remove(_vm, -2);
  if (nparams == 2) {
    ScriptEngine::rawCall(pOldRoom, "exit", pObject);
  } else {
    ScriptEngine::rawCall(pOldRoom, "exit");
  }

  pOldRoom->exit();

  ScriptEngine::rawCall("exitedRoom", pOldRoom);

  // remove all local threads
  _threads.erase(std::remove_if(_threads.begin(), _threads.end(), [](auto &pThread) -> bool {
    return !pThread->isGlobal();
  }), _threads.end());

  return 0;
}

int Engine::Impl::getDefaultVerb(Entity *pEntity) const {
  pEntity = getEntity(pEntity);
  const char *dialog = nullptr;
  if (ScriptEngine::rawGet(pEntity, "dialog", dialog) && dialog)
    return VerbConstants::VERB_TALKTO;

  int value = 0;
  if (ScriptEngine::rawGet(pEntity, "defaultVerb", value))
    return value;

  return VerbConstants::VERB_LOOKAT;
}

void Engine::Impl::updateScreenSize() {
  if (!_pRoom)
    return;

  auto screen = _pRoom->getFullscreen() == 1 ? _pRoom->getRoomSize() : _pRoom->getScreenSize();
  sf::View view(sf::FloatRect(0, 0, screen.x, screen.y));
  _pWindow->setView(view);
}

void Engine::Impl::actorEnter() {
  if (!_pCurrentActor)
    return;

  ScriptEngine::rawCall("actorEnter", _pCurrentActor);

  if (!_pRoom)
    return;

  ScriptEngine::rawCall(_pRoom, "actorEnter", _pCurrentActor);
}

void Engine::Impl::actorExit() {
  if (!_pCurrentActor || !_pRoom)
    return;

  ScriptEngine::rawCall(_pRoom, "actorExit", _pCurrentActor);
}

SQInteger Engine::Impl::enterRoom(Room *pRoom, Object *pObject) {
  // call enter room function
  trace("call enter room function of {}", pRoom->getName());
  auto nparams = ScriptEngine::getParameterCount(pRoom, "enter");
  if (nparams == 2) {
    ScriptEngine::rawCall(pRoom, "enter", pObject);
  } else {
    ScriptEngine::rawCall(pRoom, "enter");
  }

  actorEnter();

  auto lang = Locator<Preferences>::get().getUserPreference<std::string>(PreferenceNames::Language,
                                                                         PreferenceDefaultValues::Language);
  const auto &spriteSheet = pRoom->getSpriteSheet();
  auto &objects = pRoom->getObjects();
  for (auto &obj : objects) {
    for (auto &anim : obj->getAnims()) {
      for (size_t i = 0; i < anim->size(); ++i) {
        auto &frame = anim->at(i);
        auto name = frame.getName();
        if (!endsWith(name, "_en"))
          continue;

        checkLanguage(name);
        auto rect = spriteSheet.getRect(name);
        auto sourceRect = spriteSheet.getSpriteSourceSize(name);
        auto size = spriteSheet.getSourceSize(name);
        frame.setRect(rect);
        frame.setSourceRect(sourceRect);
        frame.setSize(size);
      }
    }
    if (obj->getId() == 0 || obj->isTemporary())
      continue;

    if (ScriptEngine::exists(obj.get(), "enter")) {
      ScriptEngine::rawCall(obj.get(), "enter");
    }
  }

  ScriptEngine::rawCall("enteredRoom", pRoom);

  return 0;
}

void Engine::Impl::run(bool state) {
  if (_run != state) {
    _run = state;
    if (_pCurrentActor) {
      ScriptEngine::call(_pCurrentActor, "run", state);
    }
  }
}

void Engine::Impl::setCurrentRoom(Room *pRoom) {
  if (pRoom) {
    std::ostringstream s;
    s << "currentRoom = " << pRoom->getName();
    _pScriptExecute->execute(s.str());
  }
  _camera.resetBounds();
  _camera.at(sf::Vector2f(0, 0));
  _pRoom = pRoom;
  updateScreenSize();
}

SQInteger Engine::setRoom(Room *pRoom) {
  if (!pRoom)
    return 0;

  _pImpl->_fadeColor = sf::Color::Transparent;

  auto pOldRoom = _pImpl->_pRoom;
  if (pRoom == pOldRoom)
    return 0;

  auto result = _pImpl->exitRoom(nullptr);
  if (SQ_FAILED(result))
    return result;

  _pImpl->setCurrentRoom(pRoom);

  result = _pImpl->enterRoom(pRoom, nullptr);
  if (SQ_FAILED(result))
    return result;

  return 0;
}

SQInteger Engine::enterRoomFromDoor(Object *pDoor) {
  auto dir = pDoor->getUseDirection();
  Facing facing;
  switch (dir) {
  case UseDirection::Back:facing = Facing::FACE_FRONT;
    break;
  case UseDirection::Front:facing = Facing::FACE_BACK;
    break;
  case UseDirection::Left:facing = Facing::FACE_RIGHT;
    break;
  case UseDirection::Right:facing = Facing::FACE_LEFT;
    break;
  default:throw std::invalid_argument("direction is invalid");
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

  if (pRoom->getFullscreen() != 1) {
    actor->setRoom(pRoom);
    auto pos = pDoor->getRealPosition();
    auto usePos = pDoor->getUsePosition();
    pos += usePos;
    actor->setPosition(pos);
    _pImpl->_camera.at(pos);
  }

  return _pImpl->enterRoom(pRoom, pDoor);
}

void Engine::setInputHUD(bool on) { _pImpl->_inputHUD = on; }

void Engine::setInputActive(bool active) {
  _pImpl->_inputActive = active;
  _pImpl->_showCursor = active;
}

void Engine::inputSilentOff() { _pImpl->_inputActive = false; }

void Engine::setInputVerbs(bool on) { _pImpl->_inputVerbsActive = on; }

void Engine::Impl::updateCutscene(const sf::Time &elapsed) {
  if (_pCutscene) {
    (*_pCutscene)(elapsed);
    if (_pCutscene->isElapsed()) {
      _pCutscene = nullptr;
    }
  }
}

void Engine::Impl::updateSentence(const sf::Time &elapsed) {
  if (!_pSentence)
    return;
  (*_pSentence)(elapsed);
  if (!_pSentence->isElapsed())
    return;
  _pEngine->stopSentence();
}

void Engine::Impl::updateFunctions(const sf::Time &elapsed) {
  for (auto &function : _newFunctions) {
    _functions.push_back(std::move(function));
  }
  _newFunctions.clear();
  for (auto &function : _functions) {
    (*function)(elapsed);
  }
  _functions.erase(std::remove_if(_functions.begin(), _functions.end(),
                                  [](std::unique_ptr<Function> &f) { return f->isElapsed(); }),
                   _functions.end());
  for (auto &callback : _callbacks) {
    (*callback)(elapsed);
  }
}

void Engine::Impl::updateActorIcons(const sf::Time &elapsed) {
  _actorIcons.setMousePosition(_mousePos);
  _actorIcons.update(elapsed);
}

void Engine::Impl::updateMouseCursor() {
  auto flags = getFlags(_pObj1);
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

Entity *Engine::Impl::getHoveredEntity(const sf::Vector2f &mousPos) {
  Entity *pCurrentObject = nullptr;

  // mouse on actor ?
  for (auto &&actor : _actors) {
    if (actor.get() == _pCurrentActor)
      continue;
    if (actor->getRoom() != _pRoom)
      continue;

    if (actor->contains(mousPos)) {
      if (!pCurrentObject || actor->getZOrder() < pCurrentObject->getZOrder()) {
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
    if (!rect.contains((sf::Vector2i) mousPos))
      return;
    if (!pCurrentObject || pObj->getZOrder() < pCurrentObject->getZOrder())
      pCurrentObject = pObj.get();
  });

  if (!pCurrentObject && _pRoom && _pRoom->getFullscreen() != 1) {
    // mouse on inventory object ?
    pCurrentObject = _hud.getInventory().getCurrentInventoryObject();
  }

  return pCurrentObject;
}

void Engine::Impl::updateHoveredEntity(bool isRightClick) {
  _hud.setVerbOverride(nullptr);
  if (!_hud.getCurrentVerb()) {
    _hud.setCurrentVerb(_hud.getVerb(VerbConstants::VERB_WALKTO));
  }

  if (_pUseObject) {
    _pObj1 = _pUseObject;
    _pObj2 = _hud.getHoveredEntity();
  } else {
    _pObj1 = _hud.getHoveredEntity();
    _pObj2 = nullptr;
  }

  // abort some invalid actions
  if (!_pObj1 || !_hud.getCurrentVerb()) {
    return;
  }

  if (_pObj2 == _pObj1) {
    _pObj2 = nullptr;
  }

  if (_pObj1 && isRightClick) {
    _hud.setVerbOverride(_hud.getVerb(getDefaultVerb(_pObj1)));
  }

  if (_hud.getCurrentVerb()->id == VerbConstants::VERB_WALKTO) {
    if (_pObj1 && _pObj1->isInventoryObject()) {
      _hud.setVerbOverride(_hud.getVerb(getDefaultVerb(_pObj1)));
    }
  } else if (_hud.getCurrentVerb()->id == VerbConstants::VERB_TALKTO) {
    // select actor/object only if talkable flag is set
    auto flags = getFlags(_pObj1);
    if (!(flags & ObjectFlagConstants::TALKABLE))
      _pObj1 = nullptr;
  } else if (_hud.getCurrentVerb()->id == VerbConstants::VERB_GIVE) {
    if (!_pObj1->isInventoryObject())
      _pObj1 = nullptr;

    // select actor/object only if giveable flag is set
    if (_pObj2) {
      auto flags = getFlags(_pObj2);
      if (!(flags & ObjectFlagConstants::GIVEABLE))
        _pObj2 = nullptr;
    }
  }
}

Entity *Engine::Impl::getEntity(Entity *pEntity) const {
  if (!pEntity)
    return nullptr;

  // if an actor has the same name then get its flags
  auto itActor = std::find_if(_actors.begin(), _actors.end(), [pEntity](const auto &pActor) -> bool {
    return pActor->getName() == pEntity->getName();
  });
  if (itActor != _actors.end()) {
    return itActor->get();
  }
  return pEntity;
}

int Engine::Impl::getFlags(Entity *pEntity) const {
  if (!pEntity)
    return 0;

  pEntity = getEntity(pEntity);

  int flags = 0;
  ScriptEngine::rawGet(pEntity, "flags", flags);
  return flags;
}

void Engine::Impl::updateRoomScalings() {
  auto actor = _pCurrentActor;
  if (!actor)
    return;

  auto &scalings = _pRoom->getScalings();
  auto &objects = _pRoom->getObjects();
  for (auto &&object : objects) {
    if (object->getType() != ObjectType::Trigger)
      continue;
    if (object->getRealHotspot().contains((sf::Vector2i) actor->getPosition())) {
      auto it = std::find_if(scalings.begin(), scalings.end(), [&object](const auto &s) -> bool {
        return s.getName() == object->getName();
      });
      if (it != scalings.end()) {
        _pRoom->setRoomScaling(*it);
        return;
      }
    }
  }
  if (!scalings.empty()) {
    _pRoom->setRoomScaling(scalings[0]);
  }
}

const Verb *Engine::Impl::getHoveredVerb() const {
  if (!_hud.getActive())
    return nullptr;
  if (_pRoom && _pRoom->getFullscreen() == 1)
    return nullptr;

  return _hud.getHoveredVerb();
}

void Engine::update(const sf::Time &el) {
  auto gameSpeedFactor =
      getPreferences().getUserPreference(PreferenceNames::GameSpeedFactor, PreferenceDefaultValues::GameSpeedFactor);
  const sf::Time elapsed(sf::seconds(el.asSeconds() * gameSpeedFactor));
  _pImpl->stopThreads();
  _pImpl->_mousePos = _pImpl->_pWindow->mapPixelToCoords(sf::Mouse::getPosition(*_pImpl->_pWindow));
  if (_pImpl->_pRoom) {
    auto screenSize = _pImpl->_pRoom->getScreenSize();
    auto screenMouse = toDefaultView((sf::Vector2i) _pImpl->_mousePos, screenSize);
    _pImpl->_hud.setMousePosition(screenMouse);
    _pImpl->_dialogManager.setMousePosition(screenMouse);
  }
  if (_pImpl->_state == EngineState::Options) {
    _pImpl->_optionsDialog.update(elapsed);
  } else if (_pImpl->_state == EngineState::StartScreen) {
    _pImpl->_startScreenDialog.update(elapsed);
  } else if (_pImpl->isKeyPressed(InputConstants::KEY_SPACE)) {
    _pImpl->_state = _pImpl->_state == EngineState::Game ? EngineState::Paused : EngineState::Game;
    if (_pImpl->_state == EngineState::Paused) {
      _pImpl->_soundManager.pauseAllSounds();
    } else {
      _pImpl->_soundManager.resumeAllSounds();
    }
  } else if (_pImpl->isKeyPressed(InputConstants::KEY_ESCAPE)) {
    if (inCutscene()) {
      if (_pImpl->_pCutscene && _pImpl->_pCutscene->hasCutsceneOverride()) {
        cutsceneOverride();
      } else {
        _pImpl->_noOverrideElapsed = sf::seconds(0);
      }
    }
  }

  if (_pImpl->_state == EngineState::Paused) {
    _pImpl->updateKeys();
    return;
  }

  _pImpl->_talkingState.update(elapsed);

  ImGuiIO &io = ImGui::GetIO();
  _pImpl->_frameCounter++;
  auto wasMouseDown = !io.WantCaptureMouse && _pImpl->_isMouseDown;
  auto wasMouseRightDown = !io.WantCaptureMouse && _pImpl->_isMouseRightDown;
  _pImpl->_isMouseDown =
      !io.WantCaptureMouse && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) && _pImpl->_pWindow->hasFocus();
  if (!wasMouseDown || !_pImpl->_isMouseDown) {
    _pImpl->_mouseDownTime = sf::seconds(0);
    _pImpl->run(false);
  } else {
    _pImpl->_mouseDownTime += elapsed;
    if (_pImpl->_mouseDownTime > sf::seconds(0.5f)) {
      _pImpl->run(true);
    }
  }
  _pImpl->_isMouseRightDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Right) && _pImpl->_pWindow->hasFocus();
  bool isRightClick = wasMouseRightDown != _pImpl->_isMouseRightDown && !_pImpl->_isMouseRightDown;
  auto isMouseClick = wasMouseDown != _pImpl->_isMouseDown && !_pImpl->_isMouseDown;

  _pImpl->_time += elapsed;
  _pImpl->_noOverrideElapsed += elapsed;

  _pImpl->_camera.update(elapsed);
  _pImpl->_soundManager.update(elapsed);
  _pImpl->updateCutscene(elapsed);
  _pImpl->updateFunctions(elapsed);
  _pImpl->updateSentence(elapsed);
  _pImpl->updateKeys();

  if (!_pImpl->_pRoom)
    return;

  _pImpl->updateRoomScalings();

  auto screen = _pImpl->_pWindow->getView().getSize();
  _pImpl->_pRoom->update(elapsed);
  if (_pImpl->_pFollowActor && _pImpl->_pFollowActor->isVisible() && _pImpl->_pFollowActor->getRoom() == getRoom()) {
    auto pos = _pImpl->_pFollowActor->getPosition() - sf::Vector2f(screen.x / 2, screen.y / 2);
    auto margin = screen.x / 4;
    auto cameraPos = _pImpl->_camera.getAt();
    if (_pImpl->_camera.isMoving() || (cameraPos.x > pos.x + margin) || (cameraPos.x < pos.x - margin)) {
      _pImpl->_camera.panTo(pos, sf::seconds(4), InterpolationMethod::EaseOut);
    }
  }

  _pImpl->updateActorIcons(elapsed);

  if (_pImpl->_state == EngineState::Options)
    return;

  _pImpl->_cursorDirection = CursorDirection::None;
  _pImpl->updateMouseCursor();

  auto mousePos = sf::Vector2f(_pImpl->_mousePos.x, _pImpl->_pWindow->getView().getSize().y - _pImpl->_mousePos.y);
  _pImpl->_mousePosInRoom = mousePos + _pImpl->_camera.getAt();

  _pImpl->_dialogManager.update(elapsed);

  _pImpl->_hud.setActive(_pImpl->_inputVerbsActive && _pImpl->_dialogManager.getState() == DialogManagerState::None
                             && _pImpl->_pRoom->getFullscreen() != 1);
  _pImpl->_hud.setHoveredEntity(_pImpl->getEntity(_pImpl->getHoveredEntity(_pImpl->_mousePosInRoom)));
  _pImpl->updateHoveredEntity(isRightClick);

  if (_pImpl->_pCurrentActor) {
    auto &objects = _pImpl->_pCurrentActor->getObjects();
    for (auto &object : objects) {
      object->update(elapsed);
    }
  }

  _pImpl->_hud.update(elapsed);

  if (!_pImpl->_inputActive)
    return;

  _pImpl->updateKeyboard();

  if (_pImpl->_dialogManager.getState() != DialogManagerState::None) {
    auto rightClickSkipsDialog = getPreferences().getUserPreference(PreferenceNames::RightClickSkipsDialog,
                                                                    PreferenceDefaultValues::RightClickSkipsDialog);
    auto keySkip = _pImpl->toKey(getPreferences().getUserPreference(PreferenceNames::KeySkipText,
                                                                    PreferenceDefaultValues::KeySkipText));
    if (_pImpl->isKeyPressed(keySkip) || (rightClickSkipsDialog && isRightClick)) {
      _pImpl->stopAllTalking();
    }
    return;
  }

  if (_pImpl->_actorIcons.isMouseOver())
    return;

  if (isMouseClick && _pImpl->clickedAt(_pImpl->_mousePosInRoom))
    return;

  if (!_pImpl->_pCurrentActor)
    return;

  if (!isMouseClick && !isRightClick && !_pImpl->_isMouseDown)
    return;

  stopSentence();

  const auto *pVerb = _pImpl->getHoveredVerb();
  // input click on a verb ?
  if (_pImpl->_hud.getActive() && pVerb) {
    _pImpl->onVerbClick(pVerb);
    return;
  }

  if (!isMouseClick && !isRightClick) {
    if (!pVerb && !_pImpl->_hud.getHoveredEntity())
      _pImpl->_pCurrentActor->walkTo(_pImpl->_mousePosInRoom);
    return;
  }

  if (_pImpl->_hud.getHoveredEntity()) {
    ScriptEngine::rawCall("onObjectClick", _pImpl->_hud.getHoveredEntity());
    auto pVerbOverride = _pImpl->_hud.getVerbOverride();
    if (!pVerbOverride) {
      pVerbOverride = _pImpl->_hud.getCurrentVerb();
    }
    pVerbOverride = _pImpl->overrideVerb(pVerbOverride);
    auto pObj1 = pVerbOverride->id == VerbConstants::VERB_TALKTO ? _pImpl->getEntity(_pImpl->_pObj1) : _pImpl->_pObj1;
    auto pObj2 = pVerbOverride->id == VerbConstants::VERB_GIVE ? _pImpl->getEntity(_pImpl->_pObj2) : _pImpl->_pObj2;
    if (pObj1) {
      _pImpl->_pVerbExecute->execute(pVerbOverride, pObj1, pObj2);
    }
    return;
  }

  _pImpl->_pCurrentActor->walkTo(_pImpl->_mousePosInRoom);
  setDefaultVerb();
}

void Engine::setCurrentActor(Actor *pCurrentActor, bool userSelected) {
  _pImpl->_pCurrentActor = pCurrentActor;
  if (_pImpl->_pCurrentActor) {
    follow(_pImpl->_pCurrentActor);
  }

  int currentActorIndex = _pImpl->getCurrentActorIndex();
  _pImpl->_hud.setCurrentActorIndex(currentActorIndex);
  _pImpl->_hud.setCurrentActor(_pImpl->_pCurrentActor);

  ScriptEngine::rawCall("onActorSelected", pCurrentActor, userSelected);
  auto pRoom = pCurrentActor ? pCurrentActor->getRoom() : nullptr;
  if (pRoom) {
    ScriptEngine::rawCall(pRoom, "onActorSelected", pCurrentActor, userSelected);
  }
}

void Engine::Impl::stopAllTalking() {
  for (auto &&a : _pEngine->getActors()) {
    a->stopTalking();
  }
}

void Engine::Impl::updateKeys() {
  _oldKeyDowns.clear();
  for (auto key : _newKeyDowns) {
    _oldKeyDowns.insert(key);
  }
  _newKeyDowns.clear();
}

bool Engine::Impl::isKeyPressed(int key) {
  auto wasDown = _oldKeyDowns.find(key) != _oldKeyDowns.end();
  auto isDown = _newKeyDowns.find(key) != _newKeyDowns.end();
  return wasDown && !isDown;
}

int Engine::Impl::toKey(const std::string &keyText) {
  if (keyText.length() == 1) {
    return keyText[0];
  }
  return 0;
}

void Engine::Impl::updateKeyboard() {
  ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput)
    return;

  if (_oldKeyDowns.empty())
    return;

  if (_pRoom) {
    for (auto key : _oldKeyDowns) {
      if (isKeyPressed(key)) {
        ScriptEngine::rawCall(_pRoom, "pressedKey", key);
      }
    }
  }

  int currentActorIndex = getCurrentActorIndex();
  if (currentActorIndex == -1)
    return;

  const auto &verbSlot = _hud.getVerbSlot(currentActorIndex);
  for (auto i = 0; i < 10; i++) {
    const auto &verb = verbSlot.getVerb(i);
    if (verb.key.length() == 0)
      continue;
    auto id = std::strtol(verb.key.substr(1, verb.key.length() - 1).c_str(), nullptr, 10);
    auto key = toKey(tostring(_pEngine->getText(id)));
    if (isKeyPressed(key)) {
      onVerbClick(&verb);
    }
  }
}

void Engine::Impl::onVerbClick(const Verb *pVerb) {
  _hud.setCurrentVerb(pVerb);
  _useFlag = UseFlag::None;
  _pUseObject = nullptr;
  _pObj1 = nullptr;
  _pObj2 = nullptr;

  ScriptEngine::rawCall("onVerbClick");
}

bool Engine::Impl::clickedAt(const sf::Vector2f &pos) {
  if (!_pRoom)
    return false;

  bool handled = false;
  ScriptEngine::rawCallFunc(handled, _pRoom, "clickedAt", pos.x, pos.y);
  return handled;
}

void Engine::draw(sf::RenderWindow &window) const {
  if (_pImpl->_pRoom) {
    _pImpl->_pRoom->draw(window, _pImpl->_camera.getAt());
    _pImpl->drawFade(window);

    _pImpl->drawWalkboxes(window);

    window.draw(_pImpl->_talkingState);

    window.draw(_pImpl->_dialogManager);

    _pImpl->drawHud(window);
    if ((_pImpl->_pRoom->getFullscreen() != 1) && (_pImpl->_dialogManager.getState() == DialogManagerState::None)
        && _pImpl->_inputActive) {
      window.draw(_pImpl->_actorIcons);
    }

    _pImpl->_pRoom->drawForeground(window, _pImpl->_camera.getAt());

    if (_pImpl->_state == EngineState::Options) {
      window.draw(_pImpl->_optionsDialog);
    } else if (_pImpl->_state == EngineState::StartScreen) {
      window.draw(_pImpl->_startScreenDialog);
    }
    _pImpl->drawPause(window);

    _pImpl->drawCursor(window);
    _pImpl->drawCursorText(window);
    _pImpl->drawNoOverride(window);
    _pImpl->_pDebugTools->render();
  }
}

void Engine::setWalkboxesFlags(int show) { _pImpl->_showDrawWalkboxes = show; }

int Engine::getWalkboxesFlags() const { return _pImpl->_showDrawWalkboxes; }

void Engine::Impl::drawWalkboxes(sf::RenderTarget &target) const {
  if (!_pRoom || _showDrawWalkboxes == 0)
    return;

  auto screen = target.getView().getSize();
  auto w = screen.x / 2.f;
  auto h = screen.y / 2.f;
  auto at = _camera.getAt();
  sf::Transform t;
  t.rotate(_pRoom->getRotation(), w, h);
  t.translate(-at);
  sf::RenderStates states;
  states.transform = t;

  if (_showDrawWalkboxes & 4) {
    for (const auto &walkbox : _pRoom->getWalkboxes()) {
      _WalkboxDrawable wd(walkbox);
      target.draw(wd, states);
    }
  }

  if (_showDrawWalkboxes & 1) {
    for (const auto &walkbox : _pRoom->getGraphWalkboxes()) {
      _WalkboxDrawable wd(walkbox);
      target.draw(wd, states);
    }
  }

  if (_showDrawWalkboxes & 2) {
    const auto *pGraph = _pRoom->getGraph();
    if (pGraph) {
      target.draw(*pGraph, states);
    }
  }
}

void Engine::Impl::drawPause(sf::RenderTarget &target) const {
  if (_state != EngineState::Paused)
    return;

  const auto view = target.getView();
  auto viewRect = sf::FloatRect(0, 0, 320, 176);
  target.setView(sf::View(viewRect));

  auto viewCenter = sf::Vector2f(viewRect.width / 2, viewRect.height / 2);
  auto rect = _saveLoadSheet.getRect("pause_dialog");

  sf::Sprite sprite;
  sprite.setPosition(viewCenter);
  sprite.setTexture(_saveLoadSheet.getTexture());
  sprite.setOrigin(rect.width / 2.f, rect.height / 2.f);
  sprite.setTextureRect(rect);
  target.draw(sprite);

  viewRect = sf::FloatRect(0, 0, Screen::Width, Screen::Height);
  viewCenter = sf::Vector2f(viewRect.width / 2, viewRect.height / 2);
  target.setView(sf::View(viewRect));

  auto retroFonts =
      _pEngine->getPreferences().getUserPreference(PreferenceNames::RetroFonts, PreferenceDefaultValues::RetroFonts);
  const GGFont &font = _pEngine->getTextureManager().getFont(retroFonts ? "FontRetroSheet" : "FontModernSheet");

  Text text;
  auto screen = target.getView().getSize();
  auto scale = screen.y / 512.f;
  text.setScale(scale, scale);
  text.setPosition(viewCenter);
  text.setFont(font);
  text.setFillColor(sf::Color::White);
  text.setString(_pEngine->getText(99951));
  auto bounds = text.getGlobalBounds();
  text.move(-bounds.width / 2.f, -scale * bounds.height / 2.f);
  target.draw(text);

  target.setView(view);
}

void Engine::Impl::stopThreads() {
  _threads.erase(std::remove_if(_threads.begin(), _threads.end(), [](const auto &t) -> bool {
    return !t || t->isStopped();
  }), _threads.end());
}

void Engine::Impl::drawCursor(sf::RenderWindow &window) const {
  if (!_showCursor && _dialogManager.getState() != DialogManagerState::WaitingForChoice)
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

sf::IntRect Engine::Impl::getCursorRect() const {
  if (_state == EngineState::Paused)
    return _gameSheet.getRect("cursor_pause");

  if (_state == EngineState::Options)
    return _gameSheet.getRect("cursor");

  if (_dialogManager.getState() != DialogManagerState::None)
    return _gameSheet.getRect("cursor");

  if (_cursorDirection & CursorDirection::Left) {
    return _cursorDirection & CursorDirection::Hotspot ? _gameSheet.getRect("hotspot_cursor_left")
                                                       : _gameSheet.getRect("cursor_left");
  }
  if (_cursorDirection & CursorDirection::Right) {
    return _cursorDirection & CursorDirection::Hotspot ? _gameSheet.getRect("hotspot_cursor_right")
                                                       : _gameSheet.getRect("cursor_right");
  }
  if (_cursorDirection & CursorDirection::Up) {
    return _cursorDirection & CursorDirection::Hotspot ? _gameSheet.getRect("hotspot_cursor_back")
                                                       : _gameSheet.getRect("cursor_back");
  }
  if (_cursorDirection & CursorDirection::Down) {
    return (_cursorDirection & CursorDirection::Hotspot) ? _gameSheet.getRect("hotspot_cursor_front")
                                                         : _gameSheet.getRect("cursor_front");
  }
  return (_cursorDirection & CursorDirection::Hotspot) ? _gameSheet.getRect("hotspot_cursor")
                                                       : _gameSheet.getRect("cursor");
}

std::wstring Engine::Impl::getDisplayName(const std::wstring &name) const {
  auto len = name.length();
  if (len > 2 && name[len - 2] == '#') {
    return name.substr(0, len - 2);
  }
  return name;
}

const Verb *Engine::Impl::overrideVerb(const Verb *pVerb) const {
  if (!pVerb || pVerb->id != VerbConstants::VERB_WALKTO)
    return pVerb;

  const char *dialog = nullptr;
  auto pObj1 = getEntity(_pObj1);
  if (pObj1 && ScriptEngine::rawGet(pObj1, "dialog", dialog) && dialog) {
    pVerb = _hud.getVerb(VerbConstants::VERB_TALKTO);
  }
  return pVerb;
}

void Engine::Impl::drawCursorText(sf::RenderTarget &target) const {
  if (!_showCursor || _state != EngineState::Game)
    return;

  if (_dialogManager.getState() != DialogManagerState::None)
    return;

  auto pVerb = _hud.getVerbOverride();
  if (!pVerb)
    pVerb = _hud.getCurrentVerb();
  if (!pVerb)
    return;

  pVerb = overrideVerb(pVerb);

  auto currentActorIndex = getCurrentActorIndex();
  if (currentActorIndex == -1)
    return;

  auto classicSentence = _pEngine->getPreferences().getUserPreference(PreferenceNames::ClassicSentence,
                                                                      PreferenceDefaultValues::ClassicSentence);

  const auto view = target.getView();
  target.setView(sf::View(sf::FloatRect(0, 0, Screen::Width, Screen::Height)));

  auto retroFonts =
      _pEngine->getPreferences().getUserPreference(PreferenceNames::RetroFonts, PreferenceDefaultValues::RetroFonts);
  const GGFont &font = _pEngine->getTextureManager().getFont(retroFonts ? "FontRetroSheet" : "FontModernSheet");

  std::wstring s;
  if (pVerb->id != VerbConstants::VERB_WALKTO || _hud.getHoveredEntity()) {
    auto id = std::strtol(pVerb->text.substr(1).data(), nullptr, 10);
    s.append(_pEngine->getText(id));
  }
  if (_pObj1) {
    s.append(L" ").append(getDisplayName(_pEngine->getText(_pObj1->getName())));
  }
  appendUseFlag(s);
  if (_pObj2) {
    s.append(L" ").append(getDisplayName(_pEngine->getText(_pObj2->getName())));
  }

  Text text;
  text.setFont(font);
  text.setFillColor(_hud.getVerbUiColors(currentActorIndex).sentence);
  text.setString(s);

  // do display cursor position:
  // std::wstringstream ss;
  // std::wstring txt = text.getString();
  // ss << txt << L" (" << std::fixed << std::setprecision(0) << _mousePosInRoom.x << L"," << _mousePosInRoom.y << L")";
  // text.setString(ss.str());

  auto screenSize = _pRoom->getScreenSize();
  auto pos = toDefaultView((sf::Vector2i) _mousePos, screenSize);

  auto bounds = text.getGlobalBounds();
  if (classicSentence) {
    auto y = Screen::Height - 210.f;
    auto x = Screen::HalfWidth - bounds.width / 2.f;
    text.setPosition(x, y);
  } else {
    auto y = pos.y - 30 < 60 ? pos.y + 60 : pos.y - 30;
    auto x = std::clamp<float>(pos.x - bounds.width / 2.f, 20.f, Screen::Width - 20.f - bounds.width);
    text.setPosition(x, y - bounds.height);
  }
  target.draw(text, sf::RenderStates::Default);
  target.setView(view);
}

void Engine::Impl::drawNoOverride(sf::RenderTarget &target) const {
  if (_noOverrideElapsed > sf::seconds(2))
    return;

  const auto view = target.getView();
  target.setView(sf::View(sf::FloatRect(0, 0, Screen::Width, Screen::Height)));

  sf::Color c(sf::Color::White);
  c.a = static_cast<sf::Uint8>((2.f - _noOverrideElapsed.asSeconds() / 2.f) * 255);
  sf::Sprite spriteNo;
  spriteNo.setColor(c);
  spriteNo.setPosition(sf::Vector2f(8.f, 8.f));
  spriteNo.setScale(sf::Vector2f(2.f, 2.f));
  spriteNo.setTexture(_gameSheet.getTexture());
  spriteNo.setTextureRect(_gameSheet.getRect("icon_no"));
  target.draw(spriteNo);

  target.setView(view);
}

void Engine::Impl::appendUseFlag(std::wstring &sentence) const {
  switch (_useFlag) {
  case UseFlag::UseWith:sentence.append(L" ").append(_pEngine->getText(10000));
    break;
  case UseFlag::UseOn:sentence.append(L" ").append(_pEngine->getText(10001));
    break;
  case UseFlag::UseIn:sentence.append(L" ").append(_pEngine->getText(10002));
    break;
  case UseFlag::GiveTo:sentence.append(L" ").append(_pEngine->getText(10003));
    break;
  case UseFlag::None:break;
  }
}

int Engine::Impl::getCurrentActorIndex() const {
  for (int i = 0; i < static_cast<int>(_actorsIconSlots.size()); i++) {
    const auto &selectableActor = _actorsIconSlots.at(i);
    if (selectableActor.pActor == _pCurrentActor) {
      return i;
    }
  }
  return -1;
}

void Engine::Impl::drawHud(sf::RenderWindow &window) const {
  if (_state != EngineState::Game)
    return;

  window.draw(_hud);
}

void Engine::startDialog(const std::string &dialog, const std::string &node) {
  _pImpl->_dialogManager.start(dialog, node);
}

void Engine::execute(const std::string &code) { _pImpl->_pScriptExecute->execute(code); }

SoundDefinition *Engine::getSoundDefinition(const std::string &name) {
  return _pImpl->_pScriptExecute->getSoundDefinition(name);
}

bool Engine::executeCondition(const std::string &code) { return _pImpl->_pScriptExecute->executeCondition(code); }

std::string Engine::executeDollar(const std::string &code) { return _pImpl->_pScriptExecute->executeDollar(code); }

void Engine::addSelectableActor(int index, Actor *pActor) {
  _pImpl->_actorsIconSlots.at(index - 1).selectable = true;
  _pImpl->_actorsIconSlots.at(index - 1).pActor = pActor;
}

void Engine::actorSlotSelectable(Actor *pActor, bool selectable) {
  auto it = std::find_if(_pImpl->_actorsIconSlots.begin(), _pImpl->_actorsIconSlots.end(),
                         [&pActor](auto &selectableActor) -> bool { return selectableActor.pActor == pActor; });
  if (it != _pImpl->_actorsIconSlots.end()) {
    it->selectable = selectable;
  }
}

void Engine::actorSlotSelectable(int index, bool selectable) {
  _pImpl->_actorsIconSlots.at(index - 1).selectable = selectable;
}

bool Engine::isActorSelectable(Actor *pActor) const {
  for (auto &&slot : _pImpl->_actorsIconSlots) {
    if (slot.pActor == pActor)
      return slot.selectable;
  }
  return false;
}

void Engine::setActorSlotSelectable(ActorSlotSelectableMode mode) { _pImpl->_actorIcons.setMode(mode); }

void Engine::setUseFlag(UseFlag flag, Entity *object) {
  _pImpl->_useFlag = flag;
  _pImpl->_pUseObject = object;
}

void Engine::cutsceneOverride() {
  if (!_pImpl->_pCutscene)
    return;
  _pImpl->_pCutscene->cutsceneOverride();
}

void Engine::cutscene(std::unique_ptr<Cutscene> function) {
  _pImpl->_pCutscene = function.get();
  addThread(std::move(function));
}

Cutscene *Engine::getCutscene() const { return _pImpl->_pCutscene; }

bool Engine::inCutscene() const { return _pImpl->_pCutscene && !_pImpl->_pCutscene->isElapsed(); }

HSQOBJECT &Engine::getDefaultObject() { return _pImpl->_pDefaultObject; }

void Engine::flashSelectableActor(bool on) { _pImpl->_actorIcons.flash(on); }

const Verb *Engine::getActiveVerb() const { return _pImpl->_hud.getCurrentVerb(); }

void Engine::setFadeAlpha(float fade) { _pImpl->_fadeColor.a = static_cast<uint8_t>(fade * 255); }

float Engine::getFadeAlpha() const { return static_cast<float>(_pImpl->_fadeColor.a) / 255.f; }

void Engine::fadeTo(float destination, sf::Time time, InterpolationMethod method) {
  auto get = [this]() -> float { return getFadeAlpha(); };
  auto set = [this](const float &a) { setFadeAlpha(a); };
  auto f = std::make_unique<ChangeProperty<float>>(get, set, destination, time, method);
  addFunction(std::move(f));
}

void Engine::pushSentence(int id, Entity *pObj1, Entity *pObj2) {
  const Verb *pVerb = _pImpl->_hud.getVerb(id);
  if (!pVerb)
    return;
  _pImpl->_pVerbExecute->execute(pVerb, pObj1, pObj2);
}

void Engine::setSentence(std::unique_ptr<Sentence> sentence) {
  _pImpl->_pSentence = std::move(sentence);
}

void Engine::stopSentence() {
  if (!_pImpl->_pSentence)
    return;
  _pImpl->_pSentence->stop();
  _pImpl->_pSentence.reset();
}

void Engine::keyDown(int key) {
  _pImpl->_newKeyDowns.insert(key);
}

void Engine::keyUp(int key) {
  auto it = _pImpl->_newKeyDowns.find(key);
  if (it == _pImpl->_newKeyDowns.end())
    return;
  _pImpl->_newKeyDowns.erase(it);
}

void Engine::sayLineAt(sf::Vector2i pos, sf::Color color, sf::Time duration, const std::string &text) {
  _pImpl->_talkingState.setTalkColor(color);
  auto size = getRoom()->getRoomSize();
  _pImpl->_talkingState.setPosition(toDefaultView(pos, size));
  _pImpl->_talkingState.setText(getText(text));
  _pImpl->_talkingState.setDuration(duration);
}

void Engine::sayLineAt(sf::Vector2i pos, Actor &actor, const std::string &text) {
  auto size = getRoom()->getRoomSize();
  _pImpl->_talkingState.setPosition(toDefaultView(pos, size));
  _pImpl->_talkingState.loadLip(text, &actor);
}

void Engine::showOptions(bool visible) {
  _pImpl->_state = visible ? EngineState::Options : EngineState::Game;
}

void Engine::quit() {
  _pImpl->_pWindow->close();
}

void Engine::run() {
  std::ifstream is("engge.nut");
  if (is.is_open()) {
    info("execute engge.nut");
    _pImpl->_state = EngineState::Game;
    ScriptEngine::executeScript("engge.nut");
    return;
  }

  ng::info("execute boot script");
  ScriptEngine::executeNutScript("Defines.nut");
  ScriptEngine::executeNutScript("Boot.nut");
  execute("cameraInRoom(StartScreen)");
}

Inventory &Engine::getInventory() { return _pImpl->_hud.getInventory(); }
Hud &Engine::getHud() { return _pImpl->_hud; }

} // namespace ng
