#include <squirrel.h>
#include "engge/Engine/Engine.hpp"
#include "engge/Engine/ActorIconSlot.hpp"
#include "engge/Engine/ActorIcons.hpp"
#include "engge/Engine/Camera.hpp"
#include "engge/Engine/Cutscene.hpp"
#include "engge/Engine/Hud.hpp"
#include "engge/Input/InputConstants.hpp"
#include "engge/Dialog/DialogManager.hpp"
#include "engge/Graphics/GGFont.hpp"
#include "engge/Math/PathFinding/Graph.hpp"
#include "engge/Engine/Inventory.hpp"
#include "engge/UI/OptionsDialog.hpp"
#include "engge/UI/StartScreenDialog.hpp"
#include "engge/Engine/Preferences.hpp"
#include "engge/Room/Room.hpp"
#include "engge/Room/RoomScaling.hpp"
#include "engge/Graphics/Screen.hpp"
#include "engge/Scripting/ScriptEngine.hpp"
#include "engge/Scripting/ScriptExecute.hpp"
#include "engge/Engine/Sentence.hpp"
#include "engge/Audio/SoundDefinition.hpp"
#include "engge/Audio/SoundManager.hpp"
#include "engge/Graphics/SpriteSheet.hpp"
#include "engge/Engine/TextDatabase.hpp"
#include "engge/Engine/Thread.hpp"
#include "engge/Engine/Verb.hpp"
#include "engge/Graphics/Text.hpp"
#include "engge/Scripting/VerbExecute.hpp"
#include "../System/_DebugTools.hpp"
#include "../Entities/Actor/_TalkingState.hpp"
#include "engge/System/Logger.hpp"
#include "../Math/PathFinding/_WalkboxDrawable.hpp"
#include "engge/Parsers/GGPackValue.hpp"
#include "engge/Parsers/SavegameManager.hpp"
#include <cmath>
#include <ctime>
#include <cctype>
#include <cwchar>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include "engge/Input/CommandManager.hpp"
#include "engge/Engine/EngineCommands.hpp"
#include "_DebugFeatures.hpp"
namespace fs = std::filesystem;

namespace ng {

static const char *const _objectKey = "_objectKey";
static const char *const _roomKey = "_roomKey";
static const char *const _actorKey = "_actorKey";
static const char *const _idKey = "_id";
static const char *const _pseudoObjectsKey = "_pseudoObjects";
static const auto _clickedAtCallback = "clickedAt";

enum class CursorDirection : unsigned int {
  None = 0,
  Left = 1,
  Right = 1u << 1u,
  Up = 1u << 2u,
  Down = 1u << 3u,
  Hotspot = 1u << 4u
};

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
  class _SaveGameSystem {
  public:
    explicit _SaveGameSystem(Engine::Impl *pImpl) : _pImpl(pImpl) {}

    void saveGame(const std::string &path) {
      ScriptEngine::call("preSave");

      GGPackValue actorsHash;
      saveActors(actorsHash);

      GGPackValue callbacksHash;
      saveCallbacks(callbacksHash);

      GGPackValue dialogHash;
      saveDialogs(dialogHash);

      GGPackValue gameSceneHash;
      saveGameScene(gameSceneHash);

      GGPackValue globalsHash;
      saveGlobals(globalsHash);

      GGPackValue inventoryHash;
      saveInventory(inventoryHash);

      GGPackValue objectsHash;
      saveObjects(objectsHash);

      GGPackValue roomsHash;
      saveRooms(roomsHash);

      time_t now;
      time(&now);

      SQObjectPtr g;
      _table(ScriptEngine::getVm()->_roottable)->Get(ScriptEngine::toSquirrel("g"), g);
      SQObjectPtr easyMode;
      _table(g)->Get(ScriptEngine::toSquirrel("easy_mode"), easyMode);

      GGPackValue saveGameHash;
      saveGameHash.type = 2;
      saveGameHash.hash_value = {
          {"actors", actorsHash},
          {"callbacks", callbacksHash},
          {"currentRoom", GGPackValue::toGGPackValue(_pImpl->_pRoom->getName())},
          {"dialog", dialogHash},
          {"easy_mode", GGPackValue::toGGPackValue(static_cast<int>(_integer(easyMode)))},
          {"gameGUID", GGPackValue::toGGPackValue(std::string())},
          {"gameScene", gameSceneHash},
          {"gameTime", GGPackValue::toGGPackValue(_pImpl->_time.asSeconds())},
          {"globals", globalsHash},
          {"inputState", GGPackValue::toGGPackValue(_pImpl->_pEngine->getInputState())},
          {"inventory", inventoryHash},
          {"objects", objectsHash},
          {"rooms", roomsHash},
          {"savebuild", GGPackValue::toGGPackValue(958)},
          {"savetime", GGPackValue::toGGPackValue(static_cast<int>(now))},
          {"selectedActor", GGPackValue::toGGPackValue(_pImpl->_pEngine->getCurrentActor()->getKey())},
          {"version", GGPackValue::toGGPackValue(2)},
      };

      SavegameManager::saveGame(path, saveGameHash);

      ScriptEngine::call("postSave");
    }

    void loadGame(const std::string &path) {
      GGPackValue hash;
      SavegameManager::loadGame(path, hash);

      std::ofstream os(path + ".json");
      os << hash;
      os.close();

      loadGame(hash);
    }

    static std::string getSlotPath(int slot) {
      std::ostringstream path;
      path << "Savegame" << slot << ".save";
      return path.str();
    }

    static void getSlot(SavegameSlot &slot) {
      GGPackValue hash;
      SavegameManager::loadGame(slot.path, hash);

      slot.easyMode = hash["easy_mode"].getInt() != 0;
      slot.savetime = (time_t) hash["savetime"].getInt();
      slot.gametime = sf::seconds(static_cast<float>(hash["gameTime"].getDouble()));
    }

  private:
    static std::string getValue(const GGPackValue &property) {
      std::ostringstream s;
      if (property.isInteger()) {
        s << property.int_value;
      } else if (property.isDouble()) {
        s << property.double_value;
      } else if (property.isString()) {
        s << property.string_value;
      }
      return s.str();
    }

    SQObjectPtr toSquirrel(const GGPackValue &value) {
      if (value.isString()) {
        return ScriptEngine::toSquirrel(value.getString());
      }
      if (value.isInteger()) {
        return static_cast<SQInteger>(value.getInt());
      }
      if (value.isDouble()) {
        return static_cast<SQFloat>(value.getDouble());
      }
      if (value.isArray()) {
        auto array = SQArray::Create(_ss(ScriptEngine::getVm()), value.array_value.size());
        SQInteger i = 0;
        for (auto &item : value.array_value) {
          array->Set(i++, toSquirrel(item));
        }
        return array;
      }
      if (value.isHash()) {
        auto itActor = value.hash_value.find(_actorKey);
        auto itEnd = value.hash_value.cend();
        if (itActor != itEnd) {
          auto pActor = getActor(itActor->second.getString());
          return pActor->getTable();
        }
        auto itObject = value.hash_value.find(_objectKey);
        auto itRoom = value.hash_value.find(_roomKey);
        if (itObject != itEnd) {
          Object *pObject;
          if (itRoom != itEnd) {
            auto pRoom = getRoom(itRoom->second.getString());
            pObject = getObject(pRoom, itObject->second.getString());
            if (!pObject) {
              warn("load: object {} not found", itObject->second.getString());
              return SQObjectPtr();
            }
            return pObject->getTable();
          }
          pObject = getObject(itObject->second.getString());
          if (!pObject) {
            warn("load: object {} not found", itObject->second.getString());
            return SQObjectPtr();
          }
          return pObject->getTable();
        }

        if (itRoom != itEnd) {
          auto pRoom = getRoom(itRoom->second.getString());
          return pRoom->getTable();
        }

        auto table = SQTable::Create(_ss(ScriptEngine::getVm()), 0);
        for (const auto&[key, value] : value.hash_value) {
          table->NewSlot(ScriptEngine::toSquirrel(key), toSquirrel(value));
        }
        return table;
      }
      if (!value.isNull()) {
        warn("trying to convert an unknown value (type={}) to squirrel", static_cast<int >(value.type));
      }
      return SQObjectPtr();
    }

    void loadGameScene(const GGPackValue &hash) {
      auto actorsSelectable = hash["actorsSelectable"].getInt();
      auto actorsTempUnselectable = hash["actorsTempUnselectable"].getInt();
      auto mode = actorsSelectable ? ActorSlotSelectableMode::On : ActorSlotSelectableMode::Off;
      if (actorsTempUnselectable) {
        mode |= ActorSlotSelectableMode::TemporaryUnselectable;
      }
      _pImpl->_pEngine->setActorSlotSelectable(mode);
      auto forceTalkieText = hash["forceTalkieText"].getInt() != 0;
      _pImpl->_pEngine->getPreferences().setTempPreference(TempPreferenceNames::ForceTalkieText, forceTalkieText);
      for (auto &selectableActor : hash["selectableActors"].array_value) {
        auto pActor = getActor(selectableActor[_actorKey].getString());
        auto selectable = selectableActor["selectable"].getInt() != 0;
        _pImpl->_pEngine->actorSlotSelectable(pActor, selectable);
      }
    }

    void loadDialog(const GGPackValue &hash) {
      auto &states = _pImpl->_dialogManager.getStates();
      states.clear();
      for (auto &property : hash.hash_value) {
        auto dialog = property.first;
        // dialog format: mode dialog number actor
        // example: #ChetAgentStreetDialog14reyes
        // mode:
        // ?: once
        // #: showonce
        // &: onceever
        // $: showonceever
        // ^: temponce
        auto state = parseState(dialog);
        states.push_back(state);
        // TODO: what to do with this dialog value ?
        //auto value = property.second.getInt();
      }
    }

    [[nodiscard]] static DialogConditionState parseState(const std::string &dialog) {
      DialogConditionState state;
      switch (dialog[0]) {
      case '?':state.mode = DialogConditionMode::Once;
        break;
      case '#':state.mode = DialogConditionMode::ShowOnce;
        break;
      case '&':state.mode = DialogConditionMode::OnceEver;
        break;
      case '$':state.mode = DialogConditionMode::ShowOnceEver;
        break;
      case '^':state.mode = DialogConditionMode::TempOnce;
        break;
      }
      std::string dialogName;
      int i;
      for (i = 1; i < static_cast<int>(dialog.length()) && !isdigit(dialog[i]); i++) {
        dialogName.append(1, dialog[i]);
      }
      auto &settings = Locator<EngineSettings>::get();
      while (!settings.hasEntry(dialogName + ".byack")) {
        dialogName.append(1, dialog.at(i++));
      }
      std::string num;
      state.dialog = dialogName;
      for (; i < static_cast<int>(dialog.length()) && isdigit(dialog[i]); i++) {
        num.append(1, dialog[i]);
      }
      state.line = atol(num.data());
      state.actorKey = dialog.substr(i);
      return state;
    }

    void loadCallbacks(const GGPackValue &hash) {
      _pImpl->_callbacks.clear();
      for (auto &callBackHash : hash["callbacks"].array_value) {
        auto name = callBackHash["function"].getString();
        auto id = callBackHash["guid"].getInt();
        auto time = sf::seconds(static_cast<float>(callBackHash["time"].getInt()) / 1000.f);
        auto arg = toSquirrel(callBackHash["param"]);
        auto callback = std::make_unique<Callback>(id, time, name, arg);
        _pImpl->_callbacks.push_back(std::move(callback));
      }
      Locator<EntityManager>::get().setCallbackId(hash["nextGuid"].getInt());
    }

    void loadActors(const GGPackValue &hash) {
      for (auto &pActor : _pImpl->_actors) {
        if (pActor->getKey().empty())
          continue;

        auto &actorHash = hash[pActor->getKey()];
        loadActor(pActor.get(), actorHash);
      }
    }

    void loadActor(Actor *pActor, const GGPackValue &actorHash) {
      sf::Color color{sf::Color::White};
      getValue(actorHash, "_color", color);
      pActor->setColor(color);

      sf::Vector2f pos;
      getValue(actorHash, "_pos", pos);
      pActor->setPosition(pos);

      std::string costume;
      getValue(actorHash, "_costume", costume);
      std::string costumesheet;
      getValue(actorHash, "_costumeSheet", costumesheet);
      pActor->getCostume().loadCostume(costume, costumesheet);

      std::string room;
      getValue(actorHash, _roomKey, room);
      auto *pRoom = getRoom(room.empty() ? "Void" : room);
      pActor->setRoom(pRoom);

      int dir = static_cast<int>(Facing::FACE_FRONT);
      getValue(actorHash, "_dir", dir);
      pActor->getCostume().setFacing(static_cast<Facing>(dir));

      int useDirValue = static_cast<int>(UseDirection::Front);
      getValue(actorHash, "_useDir", useDirValue);
      pActor->setUseDirection(static_cast<UseDirection>(dir));

      int lockFacing = 0;
      getValue(actorHash, "_lockFacing", lockFacing);
      if (lockFacing == 0) {
        pActor->getCostume().unlockFacing();
      } else {
        pActor->getCostume().lockFacing(static_cast<Facing>(lockFacing),
                                        static_cast<Facing>(lockFacing),
                                        static_cast<Facing>(lockFacing),
                                        static_cast<Facing>(lockFacing));
      }
      float volume = 0;
      getValue(actorHash, "_volume", volume);
      pActor->setVolume(volume);

      sf::Vector2f usePos;
      getValue(actorHash, "_usePos", usePos);
      pActor->setUsePosition(usePos);

      sf::Vector2f renderOffset = sf::Vector2f(0, 45);
      getValue(actorHash, "_renderOffset", renderOffset);
      pActor->setRenderOffset((sf::Vector2i) renderOffset);

      sf::Vector2f offset;
      getValue(actorHash, "_offset", offset);
      pActor->setOffset(offset);

      for (auto &property : actorHash.hash_value) {
        if (property.first.empty() || property.first[0] == '_') {
          if (property.first == "_animations") {
            std::vector<std::string> anims;
            for (auto &value : property.second.array_value) {
              anims.push_back(value.getString());
            }
            // TODO: _animations
            trace("load: actor {} property '{}' not loaded (type={}) size={}",
                  pActor->getKey(),
                  property.first,
                  static_cast<int>(property.second.type), anims.size());
          } else if ((property.first == "_pos") || (property.first == "_costume") || (property.first == "_costumeSheet")
              || (property.first == _roomKey) ||
              (property.first == "_color") || (property.first == "_dir") || (property.first == "_useDir")
              || (property.first == "_lockFacing") || (property.first == "_volume") || (property.first == "_usePos")
              || (property.first == "_renderOffset") || (property.first == "_offset")) {
          } else {
            // TODO: other types
            auto s = getValue(property.second);
            trace("load: actor {} property '{}' not loaded (type={}): {}",
                  pActor->getKey(),
                  property.first,
                  static_cast<int>(property.second.type), s);
          }
          continue;
        }

        _table(pActor->getTable())->Set(ScriptEngine::toSquirrel(property.first), toSquirrel(property.second));
      }
      if (ScriptEngine::rawExists(pActor, "postLoad")) {
        ScriptEngine::objCall(pActor, "postLoad");
      }
    }

    void loadInventory(const GGPackValue &hash) {
      for (auto i = 0; i < static_cast<int>(_pImpl->_actorsIconSlots.size()); ++i) {
        auto *pActor = _pImpl->_actorsIconSlots[i].pActor;
        if (!pActor)
          continue;
        auto &slot = hash["slots"].array_value.at(i);
        pActor->clearInventory();
        int jiggleCount = 0;
        for (auto &obj : slot["objects"].array_value) {
          auto pObj = getInventoryObject(obj.getString());
          // TODO: why we don't find the inventory object here ?
          if (!pObj)
            continue;
          const auto jiggle = slot["jiggle"].isArray() && slot["jiggle"].array_value[jiggleCount++].getInt() != 0;
          pObj->setJiggle(jiggle);
          pActor->pickupObject(pObj);
        }
        auto scroll = slot["scroll"].getInt();
        pActor->setInventoryOffset(scroll);
      }
    }

    void loadObjects(const GGPackValue &hash) {
      for (auto &obj :  hash.hash_value) {
        auto objName = obj.first;
        if (objName.empty())
          continue;
        auto pObj = getObject(objName);
        // TODO: if the object does not exist creates it
        if (!pObj) {
          trace("load: object '{}' not loaded because it has not been found", objName);
          continue;
        }
        loadObject(pObj, obj.second);
      }
    }

    static void getValue(const GGPackValue &hash, const std::string &key, int &value) {
      auto it = hash.hash_value.find(key);
      if (it != hash.hash_value.end()) {
        value = it->second.getInt();
      }
    }

    static void getValue(const GGPackValue &hash, const std::string &key, std::string &value) {
      auto it = hash.hash_value.find(key);
      if (it != hash.hash_value.end()) {
        value = it->second.getString();
      }
    }

    static void getValue(const GGPackValue &hash, const std::string &key, float &value) {
      auto it = hash.hash_value.find(key);
      if (it != hash.hash_value.end()) {
        value = static_cast<float>(it->second.getDouble());
      }
    }

    static void getValue(const GGPackValue &hash, const std::string &key, bool &value) {
      auto it = hash.hash_value.find(key);
      if (it != hash.hash_value.end()) {
        value = it->second.getInt() != 0;
      }
    }

    static void getValue(const GGPackValue &hash, const std::string &key, sf::Vector2f &value) {
      auto it = hash.hash_value.find(key);
      if (it != hash.hash_value.end()) {
        value = _parsePos(it->second.getString());
      }
    }

    static void getValue(const GGPackValue &hash, const std::string &key, sf::Color &value) {
      auto it = hash.hash_value.find(key);
      if (it != hash.hash_value.end()) {
        value = _toColor(it->second.getInt());
      }
    }

    void loadObject(Object *pObj, const GGPackValue &hash) {
      auto state = 0;
      ScriptEngine::rawGet(pObj, "initState", state);
      getValue(hash, "_state", state);
      pObj->setStateAnimIndex(state);
      auto touchable = true;
      ScriptEngine::rawGet(pObj, "initTouchable", touchable);
      getValue(hash, "_touchable", touchable);
      pObj->setTouchable(touchable);
      sf::Vector2f offset;
      getValue(hash, "_offset", offset);
      pObj->setOffset(offset);
      bool hidden = false;
      getValue(hash, "_hidden", hidden);
      pObj->setVisible(!hidden);
      float rotation = 0;
      getValue(hash, "_rotation", rotation);
      pObj->setRotation(rotation);
      sf::Color color{sf::Color::White};
      getValue(hash, "_color", color);
      pObj->setColor(color);

      for (auto &property :  hash.hash_value) {
        if (property.first.empty() || property.first[0] == '_') {
          if (property.first == "_state" || property.first == "_touchable" || property.first == "_offset"
              || property.first == "_hidden" || property.first == "_rotation" || property.first == "_color")
            continue;

          // TODO: other types
          auto s = getValue(property.second);
          warn("load: object {} property '{}' not loaded (type={}): {}",
               pObj->getKey(),
               property.first,
               static_cast<int>(property.second.type), s);
          continue;
        }

        _table(pObj->getTable())->Set(ScriptEngine::toSquirrel(property.first), toSquirrel(property.second));
      }
    }

    void loadPseudoObjects(Room *pRoom, const std::map<std::string, GGPackValue> &hash) {
      for (auto &[objName, objValue] :  hash) {
        auto pObj = getObject(pRoom, objName);
        if (!pObj) {
          trace("load: room '{}' object '{}' not loaded because it has not been found", pRoom->getName(), objName);
          continue;
        }
        loadObject(pObj, objValue);
      }
    }

    void loadRooms(const GGPackValue &hash) {
      for (auto &roomHash :  hash.hash_value) {
        auto roomName = roomHash.first;
        auto pRoom = getRoom(roomName);
        if (!pRoom) {
          trace("load: room '{}' not loaded because it has not been found", roomName);
          continue;
        }

        for (auto &property : roomHash.second.hash_value) {
          if (property.first.empty() || property.first[0] == '_') {
            if (property.first == _pseudoObjectsKey) {
              loadPseudoObjects(pRoom, property.second.hash_value);
            } else {
              trace("load: room '{}' property '{}' (type={}) not loaded",
                    roomName,
                    property.first,
                    static_cast<int>(property.second.type));
              continue;
            }
          }

          _table(pRoom->getTable())->Set(ScriptEngine::toSquirrel(property.first), toSquirrel(property.second));
          if (ScriptEngine::rawExists(pRoom, "postLoad")) {
            ScriptEngine::objCall(pRoom, "postLoad");
          }
        }
      }
    }

    void loadGame(const GGPackValue &hash) {
      auto version = hash["version"].getInt();
      if (version != 2) {
        warn("Cannot load savegame version {}", version);
        return;
      }

      ScriptEngine::call("preLoad");

      loadGameScene(hash["gameScene"]);
      loadDialog(hash["dialog"]);
      loadCallbacks(hash["callbacks"]);
      loadGlobals(hash["globals"]);
      loadActors(hash["actors"]);
      loadInventory(hash["inventory"]);
      loadRooms(hash["rooms"]);

      _pImpl->_time = sf::seconds(static_cast<float>(hash["gameTime"].getDouble()));
      _pImpl->_pEngine->setInputState(hash["inputState"].getInt());

      setActor(hash["selectedActor"].getString());
      setCurrentRoom(hash["currentRoom"].getString());
      loadObjects(hash["objects"]);

      ScriptEngine::set("SAVEBUILD", hash["savebuild"].getInt());

      ScriptEngine::call("postLoad");
    }

    void setActor(const std::string &name) {
      auto *pActor = getActor(name);
      _pImpl->_pEngine->setCurrentActor(pActor, false);
    }

    Actor *getActor(const std::string &name) {
      return dynamic_cast<Actor *>(_pImpl->_pEngine->getEntity(name));
    }

    Room *getRoom(const std::string &name) {
      auto &rooms = _pImpl->_pEngine->getRooms();
      auto it = std::find_if(rooms.begin(), rooms.end(), [&name](auto &pRoom) { return pRoom->getName() == name; });
      if (it != rooms.end())
        return it->get();
      return nullptr;
    }

    Object *getInventoryObject(const std::string &name) {
      auto v = ScriptEngine::getVm();
      SQObjectPtr obj;
      if (!_table(v->_roottable)->Get(ScriptEngine::toSquirrel(name), obj)) {
        return nullptr;
      }
      SQObjectPtr id;
      if (!_table(obj)->Get(ScriptEngine::toSquirrel(_idKey), id)) {
        return nullptr;
      }
      return EntityManager::getObjectFromId(static_cast<int>(_integer(id)));
    }

    Object *getObject(const std::string &name) {
      for (auto &pRoom : _pImpl->_rooms) {
        for (auto &pObj : pRoom->getObjects()) {
          if (pObj->getKey() == name)
            return pObj.get();
        }
      }
      return nullptr;
    }

    static Object *getObject(Room *pRoom, const std::string &name) {
      for (auto &pObj : pRoom->getObjects()) {
        if (pObj->getKey() == name)
          return pObj.get();
      }
      return nullptr;
    }

    void setCurrentRoom(const std::string &name) {
      _pImpl->_pEngine->setRoom(getRoom(name));
    }

    void saveActors(GGPackValue &actorsHash) {
      actorsHash.type = 2;
      for (auto &pActor : _pImpl->_actors) {
        // TODO: find why this entry exists...
        if (pActor->getKey().empty())
          continue;

        auto table = pActor->getTable();
        auto actorHash = GGPackValue::toGGPackValue(table);
        auto costume = fs::path(pActor->getCostume().getPath()).filename();
        if (costume.has_extension())
          costume.replace_extension();
        actorHash.hash_value["_costume"] = GGPackValue::toGGPackValue(costume.u8string());
        actorHash.hash_value["_dir"] = GGPackValue::toGGPackValue(static_cast<int>(pActor->getCostume().getFacing()));
        auto lockFacing = pActor->getCostume().getLockFacing();
        actorHash.hash_value["_lockFacing"] =
            GGPackValue::toGGPackValue(lockFacing.has_value() ? static_cast<int>(lockFacing.value()) : 0);
        actorHash.hash_value["_pos"] = GGPackValue::toGGPackValue(toString(pActor->getPosition()));
        if (pActor->getVolume().has_value()) {
          actorHash.hash_value["_volume"] = GGPackValue::toGGPackValue(pActor->getVolume().value());
        }
        auto useDir = pActor->getUseDirection();
        if (useDir.has_value()) {
          actorHash.hash_value["_useDir"] = GGPackValue::toGGPackValue(static_cast<int>(useDir.value()));
        }
        auto usePos = pActor->getUsePosition();
        if (useDir.has_value()) {
          actorHash.hash_value["_usePos"] = GGPackValue::toGGPackValue(toString(usePos.value()));
        }
        auto renderOffset = pActor->getRenderOffset();
        if (renderOffset != sf::Vector2i(0, 45)) {
          actorHash.hash_value["_renderOffset"] = GGPackValue::toGGPackValue(toString(renderOffset));
        }
        if (pActor->getColor() != sf::Color::White) {
          actorHash.hash_value["_color"] = GGPackValue::toGGPackValue(static_cast<int>(pActor->getColor().toInteger()));
        }
        auto costumeSheet = pActor->getCostume().getSheet();
        if (!costumeSheet.empty()) {
          actorHash.hash_value["_costumeSheet"] = GGPackValue::toGGPackValue(costumeSheet);
        }
        if (pActor->getRoom()) {
          actorHash.hash_value[_roomKey] = GGPackValue::toGGPackValue(pActor->getRoom()->getName());
        } else {
          actorHash.hash_value[_roomKey] = GGPackValue::toGGPackValue(nullptr);
        }

        actorsHash.hash_value[pActor->getKey()] = actorHash;
      }
    }

    void saveGlobals(GGPackValue &globalsHash) {
      auto v = ScriptEngine::getVm();
      auto top = sq_gettop(v);
      sq_pushroottable(v);
      sq_pushstring(v, _SC("g"), -1);
      sq_get(v, -2);
      HSQOBJECT g;
      sq_getstackobj(v, -1, &g);

      globalsHash = GGPackValue::toGGPackValue(g);
      sq_settop(v, top);
    }

    void saveDialogs(GGPackValue &hash) {
      hash.type = 2;
      const auto &states = _pImpl->_dialogManager.getStates();
      for (const auto &state : states) {
        std::ostringstream s;
        switch (state.mode) {
        case DialogConditionMode::TempOnce:continue;
        case DialogConditionMode::OnceEver:s << "&";
          break;
        case DialogConditionMode::ShowOnce:s << "#";
          break;
        case DialogConditionMode::Once:s << "?";
          break;
        case DialogConditionMode::ShowOnceEver:s << "$";
          break;
        }
        s << state.dialog << state.line << state.actorKey;
        // TODO: value should be 1 or another value ?
        hash.hash_value[s.str()] = GGPackValue::toGGPackValue(state.mode == DialogConditionMode::ShowOnce ? 2 : 1);
      }
    }

    void saveGameScene(GGPackValue &hash) const {
      auto actorsSelectable =
          ((_pImpl->_actorIcons.getMode() & ActorSlotSelectableMode::On) == ActorSlotSelectableMode::On);
      auto actorsTempUnselectable = ((_pImpl->_actorIcons.getMode() & ActorSlotSelectableMode::TemporaryUnselectable)
          == ActorSlotSelectableMode::TemporaryUnselectable);

      GGPackValue selectableActors;
      selectableActors.type = 3;
      for (auto &slot : _pImpl->_actorsIconSlots) {
        GGPackValue selectableActor;
        selectableActor.type = 2;
        if (slot.pActor) {
          selectableActor.hash_value = {
              {_actorKey, GGPackValue::toGGPackValue(slot.pActor->getKey())},
              {"selectable", GGPackValue::toGGPackValue(slot.selectable)},
          };
        } else {
          selectableActor.hash_value = {
              {"selectable", GGPackValue::toGGPackValue(0)},
          };
        }
        selectableActors.array_value.push_back(selectableActor);
      }

      hash.type = 2;
      hash.hash_value = {
          {"actorsSelectable", GGPackValue::toGGPackValue(actorsSelectable)},
          {"actorsTempUnselectable", GGPackValue::toGGPackValue(actorsTempUnselectable)},
          {"forceTalkieText",
           GGPackValue::toGGPackValue(_pImpl->_pEngine->getPreferences().getTempPreference(TempPreferenceNames::ForceTalkieText,
                                                                                           TempPreferenceDefaultValues::ForceTalkieText))},
          {"selectableActors", selectableActors}
      };
    }

    void saveInventory(GGPackValue &hash) const {
      GGPackValue slots;
      slots.type = 3;
      for (auto &slot : _pImpl->_actorsIconSlots) {
        GGPackValue actorSlot;
        actorSlot.type = 2;
        if (slot.pActor) {
          std::vector<int> jiggleArray(slot.pActor->getObjects().size());
          GGPackValue objects;
          objects.type = 3;
          int jiggleCount = 0;
          for (auto &obj : slot.pActor->getObjects()) {
            jiggleArray[jiggleCount++] = obj->getJiggle() ? 1 : 0;
            objects.array_value.push_back(GGPackValue::toGGPackValue(obj->getKey()));
          }
          actorSlot.hash_value = {
              {"objects", objects},
              {"scroll", GGPackValue::toGGPackValue(slot.pActor->getInventoryOffset())},
          };
          const auto saveJiggle = std::any_of(jiggleArray.cbegin(),
                                              jiggleArray.cend(), [](int value) { return value == 1; });
          if (saveJiggle) {
            GGPackValue jiggle;
            jiggle.type = 3;
            std::transform(jiggleArray.cbegin(),
                           jiggleArray.cend(),
                           std::back_inserter(jiggle.array_value),
                           [](int value) { return GGPackValue::toGGPackValue(value); });
            actorSlot.hash_value.insert({"jiggle", jiggle});
          }
        } else {
          actorSlot.hash_value = {
              {"scroll", GGPackValue::toGGPackValue(0)},
          };
        }
        slots.array_value.push_back(actorSlot);
      }

      hash.type = 2;
      hash.hash_value = {
          {"slots", slots},
      };
    }

    void saveObjects(GGPackValue &hash) const {
      hash.type = 2;
      for (auto &room : _pImpl->_rooms) {
        for (auto &object : room->getObjects()) {
          if (object->getType() != ObjectType::Object)
            continue;
          auto pRoom = object->getRoom();
          if (pRoom && pRoom->isPseudoRoom())
            continue;
          GGPackValue hashObject;
          saveObject(object.get(), hashObject);
          hash.hash_value[object->getKey()] = hashObject;
        }
      }
    }

    static void savePseudoObjects(const Room *pRoom, GGPackValue &hash) {
      if (!pRoom->isPseudoRoom())
        return;
      GGPackValue hashObjects;
      hashObjects.type = 2;
      for (const auto &pObj : pRoom->getObjects()) {
        GGPackValue hashObject;
        saveObject(pObj.get(), hashObject);
        hashObjects.hash_value[pObj->getKey()] = hashObject;
      }
      hash.hash_value[_pseudoObjectsKey] = hashObjects;
    }

    static void saveObject(const Object *pObject, GGPackValue &hashObject) {
      hashObject = GGPackValue::toGGPackValue(pObject->getTable());
      if (pObject->getState() != 0) {
        hashObject.hash_value["_state"] = GGPackValue::toGGPackValue(pObject->getState());
      }
      if (!pObject->isTouchable()) {
        hashObject.hash_value["_touchable"] = GGPackValue::toGGPackValue(pObject->isTouchable());
      }
      if (pObject->getOffset() != sf::Vector2f()) {
        hashObject.hash_value["_offset"] = GGPackValue::toGGPackValue(toString(pObject->getOffset()));
      }
    }

    void saveRooms(GGPackValue &hash) const {
      hash.type = 2;
      for (auto &room : _pImpl->_rooms) {
        auto hashRoom = GGPackValue::toGGPackValue(room->getTable());
        savePseudoObjects(room.get(), hashRoom);
        hash.hash_value[room->getName()] = hashRoom;
      }
    }

    void saveCallbacks(GGPackValue &callbacksHash) {
      callbacksHash.type = 2;
      GGPackValue callbacksArray;
      callbacksArray.type = 3;

      for (auto &callback : _pImpl->_callbacks) {
        GGPackValue callbackHash;
        callbackHash.type = 2;
        callbackHash.hash_value = {
            {"function", GGPackValue::toGGPackValue(callback->getMethod())},
            {"guid", GGPackValue::toGGPackValue(callback->getId())},
            {"time", GGPackValue::toGGPackValue(callback->getElapsed().asMilliseconds())}
        };
        auto arg = callback->getArgument();
        if (arg._type != OT_NULL) {
          callbackHash.hash_value["param"] = GGPackValue::toGGPackValue(arg);
        }
        callbacksArray.array_value.push_back(callbackHash);
      }

      auto &resourceManager = Locator<EntityManager>::get();
      auto id = resourceManager.getCallbackId();
      resourceManager.setCallbackId(id);

      callbacksHash.hash_value = {
          {"callbacks", callbacksArray},
          {"nextGuid", GGPackValue::toGGPackValue(id)},
      };
    }

    void loadGlobals(const GGPackValue &hash) {
      SQTable *pRootTable = _table(ScriptEngine::getVm()->_roottable);
      SQObjectPtr gObject;
      pRootTable->Get(ScriptEngine::toSquirrel("g"), gObject);
      SQTable *gTable = _table(gObject);
      for (const auto &variable : hash.hash_value) {
        gTable->Set(ScriptEngine::toSquirrel(variable.first), toSquirrel(variable.second));
      }
    }

    static std::string toString(const sf::Vector2f &pos) {
      std::ostringstream os;
      os << "{" << static_cast<int>(pos.x) << "," << static_cast<int>(pos.y) << "}";
      return os.str();
    }

    static std::string toString(const sf::Vector2i &pos) {
      std::ostringstream os;
      os << "{" << pos.x << "," << pos.y << "}";
      return os.str();
    }

  private:
    Impl *_pImpl{nullptr};
  };

  Engine *_pEngine{nullptr};
  std::unique_ptr<_DebugTools> _pDebugTools;
  ResourceManager &_textureManager;
  Room *_pRoom{nullptr};
  std::vector<std::unique_ptr<Actor>> _actors;
  std::vector<std::unique_ptr<Room>> _rooms;
  std::vector<std::unique_ptr<Function>> _newFunctions;
  std::vector<std::unique_ptr<Function>> _functions;
  std::vector<std::unique_ptr<Callback>> _callbacks;
  Cutscene *_pCutscene{nullptr};
  sf::RenderWindow *_pWindow{nullptr};
  Actor *_pCurrentActor{nullptr};
  bool _inputHUD{false};
  bool _inputActive{false};
  bool _showCursor{true};
  bool _inputVerbsActive{false};
  Actor *_pFollowActor{nullptr};
  Entity *_pUseObject{nullptr};
  int _objId1{0};
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
  sf::Time _time;
  bool _isMouseDown{false};
  sf::Time _mouseDownTime;
  bool _isMouseRightDown{false};
  int _frameCounter{0};
  HSQOBJECT _pDefaultObject{};
  Camera _camera;
  sf::Color _fadeColor{sf::Color::Transparent};
  std::unique_ptr<Sentence> _pSentence{};
  std::unordered_set<Input, InputHash> _oldKeyDowns;
  std::unordered_set<Input, InputHash> _newKeyDowns;
  EngineState _state{EngineState::StartScreen};
  _TalkingState _talkingState;
  int _showDrawWalkboxes{0};
  OptionsDialog _optionsDialog;
  StartScreenDialog _startScreenDialog;
  bool _run{false};
  sf::Time _noOverrideElapsed{sf::seconds(2)};
  Hud _hud;
  bool _autoSave{true};
  bool _cursorVisible{true};

  Impl();

  void drawHud(sf::RenderTarget &target) const;
  void drawCursor(sf::RenderTarget &target) const;
  void drawCursorText(sf::RenderTarget &target) const;
  void drawNoOverride(sf::RenderTarget &target) const;
  int getCurrentActorIndex() const;
  sf::IntRect getCursorRect() const;
  void appendUseFlag(std::wstring &sentence) const;
  bool clickedAt(const sf::Vector2f &pos) const;
  void updateCutscene(const sf::Time &elapsed);
  void updateFunctions(const sf::Time &elapsed);
  void updateActorIcons(const sf::Time &elapsed);
  void updateSentence(const sf::Time &elapsed) const;
  void updateMouseCursor();
  void updateHoveredEntity(bool isRightClick);
  SQInteger enterRoom(Room *pRoom, Object *pObject) const;
  SQInteger exitRoom(Object *pObject);
  void updateScreenSize() const;
  void updateRoomScalings() const;
  void setCurrentRoom(Room *pRoom);
  uint32_t getFlags(int id) const;
  uint32_t getFlags(Entity *pEntity) const;
  Entity *getHoveredEntity(const sf::Vector2f &mousPos);
  void actorEnter() const;
  void actorExit() const;
  static void onLanguageChange(const std::string &lang);
  void drawFade(sf::RenderTarget &target) const;
  void onVerbClick(const Verb *pVerb);
  void updateKeyboard();
  bool isKeyPressed(const Input &key);
  void updateKeys();
  static InputConstants toKey(const std::string &keyText);
  void drawPause(sf::RenderTarget &target) const;
  void stopThreads();
  void drawWalkboxes(sf::RenderTarget &target) const;
  const Verb *getHoveredVerb() const;
  static std::wstring getDisplayName(const std::wstring &name);
  void run(bool state);
  void stopTalking() const;
  void stopTalkingExcept(Entity *pEntity) const;
  Entity *getEntity(Entity *pEntity) const;
  const Verb *overrideVerb(const Verb *pVerb) const;
  void captureScreen(const std::string &path) const;
  void skipText() const;
  void skipCutscene();
  void pauseGame();
  void selectActor(int index);
  void selectPreviousActor();
  void selectNextActor();
  void selectChoice(int index);
  bool hasFlag(int id, uint32_t flagToTest);
};

Engine::Impl::Impl()
    : _textureManager(Locator<ResourceManager>::get()),
      _preferences(Locator<Preferences>::get()),
      _soundManager(Locator<SoundManager>::get()),
      _actorIcons(_actorsIconSlots, _hud, _pCurrentActor) {
  _hud.setTextureManager(&_textureManager);
  sq_resetobject(&_pDefaultObject);

  Locator<CommandManager>::get().registerCommands(
      {
          {EngineCommands::SkipText, [this]() { skipText(); }},
          {EngineCommands::SkipCutscene, [this] { skipCutscene(); }},
          {EngineCommands::PauseGame, [this] { pauseGame(); }},
          {EngineCommands::SelectActor1, [this] { selectActor(1); }},
          {EngineCommands::SelectActor2, [this] { selectActor(2); }},
          {EngineCommands::SelectActor3, [this] { selectActor(3); }},
          {EngineCommands::SelectActor4, [this] { selectActor(4); }},
          {EngineCommands::SelectActor5, [this] { selectActor(5); }},
          {EngineCommands::SelectActor6, [this] { selectActor(6); }},
          {EngineCommands::SelectPreviousActor, [this] { selectPreviousActor(); }},
          {EngineCommands::SelectNextActor, [this] { selectNextActor(); }},
          {EngineCommands::SelectChoice1, [this] { _dialogManager.choose(1); }},
          {EngineCommands::SelectChoice2, [this] { _dialogManager.choose(2); }},
          {EngineCommands::SelectChoice3, [this] { _dialogManager.choose(3); }},
          {EngineCommands::SelectChoice4, [this] { _dialogManager.choose(4); }},
          {EngineCommands::SelectChoice5, [this] { _dialogManager.choose(5); }},
          {EngineCommands::SelectChoice6, [this] { _dialogManager.choose(6); }},
          {EngineCommands::ShowOptions, [this] { _pEngine->showOptions(true); }},
          {EngineCommands::ToggleHud, [this] {
            _hud.setVisible(!_cursorVisible);
            _actorIcons.setVisible(!_cursorVisible);
            _cursorVisible = !_cursorVisible;
          }}
      });
  Locator<CommandManager>::get().registerPressedCommand(EngineCommands::ShowHotspots, [this](bool down) {
    _preferences.setTempPreference(TempPreferenceNames::ShowHotspot, down);
  });
}

void Engine::Impl::pauseGame() {
  _state = _state == EngineState::Game ? EngineState::Paused : EngineState::Game;
  if (_state == EngineState::Paused) {
    _soundManager.pauseAllSounds();
  } else {
    _soundManager.resumeAllSounds();
  }
}

void Engine::Impl::selectActor(int index) {
  if (index <= 0 || index > static_cast<int>(_actorsIconSlots.size()))
    return;
  const auto &slot = _actorsIconSlots[index - 1];
  if (!slot.selectable)
    return;
  _pEngine->setCurrentActor(slot.pActor, true);
}

void Engine::Impl::selectPreviousActor() {
  auto currentActorIndex = getCurrentActorIndex();
  if (currentActorIndex == -1)
    return;
  auto size = static_cast<int>(_actorsIconSlots.size());
  for (auto i = 0; i < size; i++) {
    auto index = currentActorIndex - i - 1;
    if (index < 0)
      index += size - 1;
    if (index == currentActorIndex)
      return;
    const auto &slot = _actorsIconSlots[index];
    if (slot.selectable) {
      _pEngine->setCurrentActor(slot.pActor, true);
      return;
    }
  }
}

void Engine::Impl::selectNextActor() {
  auto currentActorIndex = getCurrentActorIndex();
  if (currentActorIndex == -1)
    return;
  auto size = static_cast<int>(_actorsIconSlots.size());
  for (auto i = 0; i < size; i++) {
    auto index = (currentActorIndex + i + 1) % size;
    if (index == currentActorIndex)
      return;
    const auto &slot = _actorsIconSlots[index];
    if (slot.selectable) {
      _pEngine->setCurrentActor(slot.pActor, true);
      return;
    }
  }
}

void Engine::Impl::skipCutscene() {
  if (_pEngine->inCutscene()) {
    if (_pCutscene && _pCutscene->hasCutsceneOverride()) {
      _pEngine->cutsceneOverride();
    } else {
      _noOverrideElapsed = sf::seconds(0);
    }
  }
}

void Engine::Impl::skipText() const {
  if (_dialogManager.getState() == DialogManagerState::Active) {
    stopTalking();
  }
}

void Engine::Impl::onLanguageChange(const std::string &lang) {
  std::stringstream ss;
  ss << "ThimbleweedText_" << lang << ".tsv";
  Locator<TextDatabase>::get().load(ss.str());

  ScriptEngine::call("onLanguageChange");
}

void Engine::Impl::drawFade(sf::RenderTarget &target) const {
  sf::RectangleShape fadeShape;
  auto screen = target.getView().getSize();
  fadeShape.setSize(sf::Vector2f(screen.x, screen.y));
  fadeShape.setFillColor(_fadeColor);
  target.draw(fadeShape);
}

SQInteger Engine::Impl::exitRoom(Object *pObject) {
  _pEngine->setDefaultVerb();
  _talkingState.stop();

  if (!_pRoom)
    return 0;

  auto pOldRoom = _pRoom;

  actorExit();

  // call exit room function
  auto nparams = ScriptEngine::getParameterCount(pOldRoom, "exit");
  trace("call exit room function of {} ({} params)", pOldRoom->getName(), nparams);

  if (nparams == 2) {
    auto pRoom = pObject ? pObject->getRoom() : nullptr;
    ScriptEngine::rawCall(pOldRoom, "exit", pRoom);
  } else {
    ScriptEngine::rawCall(pOldRoom, "exit");
  }

  pOldRoom->exit();

  ScriptEngine::rawCall("exitedRoom", pOldRoom);

  // stop all local threads
  std::for_each(_threads.begin(), _threads.end(), [](auto &pThread) {
    if (!pThread->isGlobal())
      pThread->stop();
  });

  return 0;
}

void Engine::Impl::updateScreenSize() const {
  if (!_pRoom)
    return;

  sf::Vector2i screen;
  if (_pRoom->getFullscreen() == 1) {
    screen = _pRoom->getRoomSize();
    if (_pRoom->getScreenHeight() != 0) {
      screen.y = _pRoom->getScreenHeight();
    }
  } else {
    screen = _pRoom->getScreenSize();
  }
  sf::View view(sf::FloatRect(0, 0, screen.x, screen.y));
  _pWindow->setView(view);
}

void Engine::Impl::actorEnter() const {
  if (!_pCurrentActor)
    return;

  _pCurrentActor->stopWalking();
  ScriptEngine::rawCall("actorEnter", _pCurrentActor);

  if (!_pRoom)
    return;

  if (ScriptEngine::rawExists(_pRoom, "actorEnter")) {
    ScriptEngine::rawCall(_pRoom, "actorEnter", _pCurrentActor);
  }
}

void Engine::Impl::actorExit() const {
  if (!_pCurrentActor || !_pRoom)
    return;

  if (ScriptEngine::rawExists(_pRoom, "actorExit")) {
    ScriptEngine::rawCall(_pRoom, "actorExit", _pCurrentActor);
  }
}

SQInteger Engine::Impl::enterRoom(Room *pRoom, Object *pObject) const {
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

    if (ScriptEngine::rawExists(obj.get(), "enter")) {
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
      ScriptEngine::objCall(_pCurrentActor, "run", state);
    }
  }
}

void Engine::Impl::setCurrentRoom(Room *pRoom) {
  if (pRoom) {
    ScriptEngine::set("currentRoom", pRoom);
  }
  _camera.resetBounds();
  _camera.at(sf::Vector2f(0, 0));
  _pRoom = pRoom;
  updateScreenSize();
}

void Engine::Impl::updateCutscene(const sf::Time &elapsed) {
  if (_pCutscene) {
    (*_pCutscene)(elapsed);
    if (_pCutscene->isElapsed()) {
      _pCutscene = nullptr;
    }
  }
}

void Engine::Impl::updateSentence(const sf::Time &elapsed) const {
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

  std::vector<std::unique_ptr<Callback>> callbacks;
  std::move(_callbacks.begin(), _callbacks.end(), std::back_inserter(callbacks));
  _callbacks.clear();
  for (auto &callback : callbacks) {
    (*callback)(elapsed);
  }
  callbacks.erase(std::remove_if(callbacks.begin(),
                                 callbacks.end(),
                                 [](auto &f) { return f->isElapsed(); }),
                  callbacks.end());
  std::move(callbacks.begin(), callbacks.end(), std::back_inserter(_callbacks));
}

void Engine::Impl::updateActorIcons(const sf::Time &elapsed) {
  auto screenSize = _pRoom->getScreenSize();
  auto screenMouse = toDefaultView((sf::Vector2i) _mousePos, screenSize);
  _actorIcons.setMousePosition(screenMouse);
  _actorIcons.update(elapsed);
}

void Engine::Impl::updateMouseCursor() {
  auto flags = getFlags(_objId1);
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
  if ((_cursorDirection == CursorDirection::None) && _objId1)
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
    if (!pCurrentObject || pObj->getZOrder() <= pCurrentObject->getZOrder())
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
    _objId1 = _pUseObject ? _pUseObject->getId() : 0;
    _pObj2 = _hud.getHoveredEntity();
  } else {
    _objId1 = _hud.getHoveredEntity() ? _hud.getHoveredEntity()->getId() : 0;
    _pObj2 = nullptr;
  }

  // abort some invalid actions
  if (!_objId1 || !_hud.getCurrentVerb()) {
    return;
  }

  if (_pObj2 && _pObj2->getId() == _objId1) {
    _pObj2 = nullptr;
  }

  if (_objId1 && isRightClick) {
    _hud.setVerbOverride(_hud.getVerb(EntityManager::getScriptObjectFromId<Entity>(_objId1)->getDefaultVerb(VerbConstants::VERB_LOOKAT)));
  }

  auto verbId = _hud.getCurrentVerb()->id;
  switch (verbId) {
  case VerbConstants::VERB_WALKTO: {
    auto pObj1 = EntityManager::getScriptObjectFromId<Entity>(_objId1);
    if (pObj1 && pObj1->isInventoryObject()) {
      _hud.setVerbOverride(_hud.getVerb(EntityManager::getScriptObjectFromId<Entity>(_objId1)->getDefaultVerb(
          VerbConstants::VERB_LOOKAT)));
    }
    break;
  }
  case VerbConstants::VERB_TALKTO:
    // select actor/object only if talkable flag is set
    if (!hasFlag(_objId1, ObjectFlagConstants::TALKABLE)) {
      _objId1 = 0;
    }
    break;
  case VerbConstants::VERB_GIVE: {
    auto pObj1 = EntityManager::getScriptObjectFromId<Entity>(_objId1);
    if (!pObj1->isInventoryObject())
      _objId1 = 0;

    // select actor/object only if giveable flag is set
    if (_pObj2 && !hasFlag(_pObj2->getId(), ObjectFlagConstants::GIVEABLE))
      _pObj2 = nullptr;
    break;
  }
  default: {
    auto pActor = EntityManager::getScriptObjectFromId<Actor>(_objId1);
    if (pActor) {
      _objId1 = 0;
    }
    break;
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

bool Engine::Impl::hasFlag(int id, uint32_t flagToTest) {
  auto pObj = EntityManager::getScriptObjectFromId<Entity>(id);
  auto flags = getFlags(pObj);
  if (flags & flagToTest)
    return true;
  auto pActor = getEntity(pObj);
  flags = getFlags(pActor);
  return flags & flagToTest;
}

uint32_t Engine::Impl::getFlags(int id) const {
  auto pEntity = EntityManager::getScriptObjectFromId<Entity>(id);
  return getFlags(pEntity);
}

uint32_t Engine::Impl::getFlags(Entity *pEntity) const {
  if (pEntity)
    return pEntity->getFlags();
  return 0;
}

void Engine::Impl::updateRoomScalings() const {
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

void Engine::Impl::stopTalking() const {
  for (auto &&a : _pEngine->getActors()) {
    a->stopTalking();
  }
  for (auto &&a : _pEngine->getRoom()->getObjects()) {
    a->stopTalking();
  }
}

void Engine::Impl::stopTalkingExcept(Entity *pEntity) const {
  for (auto &&a : _pEngine->getActors()) {
    if (a.get() == pEntity)
      continue;
    a->stopTalking();
  }

  for (auto &&a : _pEngine->getRoom()->getObjects()) {
    if (a.get() == pEntity)
      continue;
    a->stopTalking();
  }
}

void Engine::Impl::updateKeys() {
  ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput)
    return;

  const auto &cmdMgr = Locator<CommandManager>::get();
  for (auto &key : _oldKeyDowns) {
    if (isKeyPressed(key)) {
      cmdMgr.execute(key);
      cmdMgr.execute(key, false);
    }
  }

  for (auto &key : _newKeyDowns) {
    if (_oldKeyDowns.find(key) != _oldKeyDowns.end()) {
      cmdMgr.execute(key, true);
    }
  }

  _oldKeyDowns.clear();
  for (auto key : _newKeyDowns) {
    _oldKeyDowns.insert(key);
  }
}

bool Engine::Impl::isKeyPressed(const Input &key) {
  auto wasDown = _oldKeyDowns.find(key) != _oldKeyDowns.end();
  auto isDown = _newKeyDowns.find(key) != _newKeyDowns.end();
  return wasDown && !isDown;
}

InputConstants Engine::Impl::toKey(const std::string &keyText) {
  if (keyText.length() == 1) {
    return static_cast<InputConstants>(keyText[0]);
  }
  return InputConstants::NONE;
}

void Engine::Impl::updateKeyboard() {
  if (_oldKeyDowns.empty())
    return;

  if (_pRoom) {
    for (auto key : _oldKeyDowns) {
      if (isKeyPressed(key) && ScriptEngine::rawExists(_pRoom, "pressedKey")) {
        ScriptEngine::rawCall(_pRoom, "pressedKey", static_cast<int>(key.input));
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
  _objId1 = 0;
  _pObj2 = nullptr;

  ScriptEngine::rawCall("onVerbClick");
}

bool Engine::Impl::clickedAt(const sf::Vector2f &pos) const {
  if (!_pRoom)
    return false;

  bool handled = false;
  if (ScriptEngine::rawExists(_pRoom, _clickedAtCallback)) {
    ScriptEngine::rawCallFunc(handled, _pRoom, _clickedAtCallback, pos.x, pos.y);
    if (handled)
      return true;
  }

  if (!_pCurrentActor)
    return false;

  if (!ScriptEngine::rawExists(_pCurrentActor, _clickedAtCallback))
    return false;

  ScriptEngine::rawCallFunc(handled, _pCurrentActor, _clickedAtCallback, pos.x, pos.y);
  return handled;
}

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

  auto &saveLoadSheet = Locator<ResourceManager>::get().getSpriteSheet("SaveLoadSheet");
  auto viewCenter = sf::Vector2f(viewRect.width / 2, viewRect.height / 2);
  auto rect = saveLoadSheet.getRect("pause_dialog");

  sf::Sprite sprite;
  sprite.setPosition(viewCenter);
  sprite.setTexture(saveLoadSheet.getTexture());
  sprite.setOrigin(rect.width / 2.f, rect.height / 2.f);
  sprite.setTextureRect(rect);
  target.draw(sprite);

  viewRect = sf::FloatRect(0, 0, Screen::Width, Screen::Height);
  viewCenter = sf::Vector2f(viewRect.width / 2, viewRect.height / 2);
  target.setView(sf::View(viewRect));

  auto retroFonts =
      _pEngine->getPreferences().getUserPreference(PreferenceNames::RetroFonts, PreferenceDefaultValues::RetroFonts);
  const GGFont &font = _pEngine->getResourceManager().getFont(retroFonts ? "FontRetroSheet" : "FontModernSheet");

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

void Engine::Impl::drawCursor(sf::RenderTarget &target) const {
  if (!_cursorVisible)
    return;
  if (!_showCursor && _dialogManager.getState() != DialogManagerState::WaitingForChoice)
    return;

  auto screen = _pWindow->getView().getSize();
  auto cursorSize = sf::Vector2f(68.f * screen.x / 1284, 68.f * screen.y / 772);
  auto &gameSheet = Locator<ResourceManager>::get().getSpriteSheet("GameSheet");

  sf::RectangleShape shape;
  shape.setPosition(_mousePos);
  shape.setOrigin(cursorSize / 2.f);
  shape.setSize(cursorSize);
  shape.setTexture(&gameSheet.getTexture());
  shape.setTextureRect(getCursorRect());
  target.draw(shape);
}

sf::IntRect Engine::Impl::getCursorRect() const {
  auto &gameSheet = Locator<ResourceManager>::get().getSpriteSheet("GameSheet");
  if (_state == EngineState::Paused)
    return gameSheet.getRect("cursor_pause");

  if (_state == EngineState::Options)
    return gameSheet.getRect("cursor");

  if (_dialogManager.getState() != DialogManagerState::None)
    return gameSheet.getRect("cursor");

  if (_cursorDirection & CursorDirection::Left) {
    return _cursorDirection & CursorDirection::Hotspot ? gameSheet.getRect("hotspot_cursor_left")
                                                       : gameSheet.getRect("cursor_left");
  }
  if (_cursorDirection & CursorDirection::Right) {
    return _cursorDirection & CursorDirection::Hotspot ? gameSheet.getRect("hotspot_cursor_right")
                                                       : gameSheet.getRect("cursor_right");
  }
  if (_cursorDirection & CursorDirection::Up) {
    return _cursorDirection & CursorDirection::Hotspot ? gameSheet.getRect("hotspot_cursor_back")
                                                       : gameSheet.getRect("cursor_back");
  }
  if (_cursorDirection & CursorDirection::Down) {
    return (_cursorDirection & CursorDirection::Hotspot) ? gameSheet.getRect("hotspot_cursor_front")
                                                         : gameSheet.getRect("cursor_front");
  }
  return (_cursorDirection & CursorDirection::Hotspot) ? gameSheet.getRect("hotspot_cursor")
                                                       : gameSheet.getRect("cursor");
}

std::wstring Engine::Impl::getDisplayName(const std::wstring &name) {
  std::wstring displayName(name);
  auto len = displayName.length();
  if (len > 1 && displayName[0] == '^') {
    displayName = name.substr(1, len - 1);
  }
  if (len > 2 && displayName[len - 2] == '#') {
    displayName = name.substr(0, len - 2);
  }
  return displayName;
}

const Verb *Engine::Impl::overrideVerb(const Verb *pVerb) const {
  if (!pVerb || pVerb->id != VerbConstants::VERB_WALKTO)
    return pVerb;

  auto pObj1 = EntityManager::getScriptObjectFromId<Entity>(_objId1);
  if (!pObj1)
    return pVerb;
  return _hud.getVerb(pObj1->getDefaultVerb(VerbConstants::VERB_WALKTO));
}

void Engine::Impl::drawCursorText(sf::RenderTarget &target) const {
  if (!_cursorVisible)
    return;
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
  const GGFont &font = _pEngine->getResourceManager().getFont(retroFonts ? "FontRetroSheet" : "FontModernSheet");

  std::wstring s;
  if (pVerb->id != VerbConstants::VERB_WALKTO || _hud.getHoveredEntity()) {
    auto id = std::strtol(pVerb->text.substr(1).data(), nullptr, 10);
    s.append(_pEngine->getText(id));
  }
  auto pObj1 = EntityManager::getScriptObjectFromId<Entity>(_objId1);
  if (pObj1) {
    s.append(L" ").append(getDisplayName(_pEngine->getText(pObj1->getName())));
    if (_DebugFeatures::showHoveredObject) {
      if (pObj1) {
        s.append(L"(").append(towstring(pObj1->getKey())).append(L")");
      }
    }
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
  if (_DebugFeatures::showCursorPosition) {
    std::wstringstream ss;
    std::wstring txt = text.getString();
    ss << txt << L" (" << std::fixed << std::setprecision(0) << _mousePosInRoom.x << L"," << _mousePosInRoom.y << L")";
    text.setString(ss.str());
  }

  auto screenSize = _pRoom->getScreenSize();
  auto pos = toDefaultView((sf::Vector2i) _mousePos, screenSize);

  auto bounds = text.getGlobalBounds();
  if (classicSentence) {
    auto y = Screen::Height - 210.f;
    auto x = Screen::HalfWidth - bounds.width / 2.f;
    text.setPosition(x, y);
  } else {
    auto y = pos.y - 30 < 60 ? pos.y + 60 : pos.y - 60;
    auto x = std::clamp<float>(pos.x - bounds.width / 2.f, 20.f, Screen::Width - 20.f - bounds.width);
    text.setPosition(x, y - bounds.height);
  }
  target.draw(text, sf::RenderStates::Default);
  target.setView(view);
}

void Engine::Impl::drawNoOverride(sf::RenderTarget &target) const {
  if (_noOverrideElapsed > sf::seconds(2))
    return;

  auto &gameSheet = Locator<ResourceManager>::get().getSpriteSheet("GameSheet");
  const auto view = target.getView();
  target.setView(sf::View(sf::FloatRect(0, 0, Screen::Width, Screen::Height)));

  sf::Color c(sf::Color::White);
  c.a = static_cast<sf::Uint8>((2.f - _noOverrideElapsed.asSeconds() / 2.f) * 255);
  sf::Sprite spriteNo;
  spriteNo.setColor(c);
  spriteNo.setPosition(sf::Vector2f(8.f, 8.f));
  spriteNo.setScale(sf::Vector2f(2.f, 2.f));
  spriteNo.setTexture(gameSheet.getTexture());
  spriteNo.setTextureRect(gameSheet.getRect("icon_no"));
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

void Engine::Impl::drawHud(sf::RenderTarget &target) const {
  if (_state != EngineState::Game)
    return;

  target.draw(_hud);
}

void Engine::Impl::captureScreen(const std::string &path) const {
  sf::RenderTexture target;
  target.create(static_cast<unsigned int>(Screen::Width), static_cast<unsigned int>(Screen::Height));
  target.setView(_pEngine->getWindow().getView());
  _pEngine->draw(target, true);
  target.display();

  sf::Sprite s(target.getTexture());
  s.scale(1.f / 4.f, 1.f / 4.f);

  sf::RenderTexture rt;
  rt.create(320u, 180u);
  rt.draw(s);
  rt.display();

  auto screenshot = rt.getTexture().copyToImage();
  screenshot.saveToFile(path);
}

}