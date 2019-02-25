#include <fstream>
#include <memory>
#include "SoundManager.h"

namespace ng
{
SoundManager::SoundManager(EngineSettings &settings)
    : _settings(settings)
{
}

std::shared_ptr<SoundDefinition> SoundManager::defineSound(const std::string &name)
{
    if (!_settings.hasEntry(name))
        return nullptr;

    auto sound = std::make_shared<SoundDefinition>(name);
    sound->setSettings(_settings);
    _sounds.push_back(sound);
    return sound;
}

std::shared_ptr<SoundId> SoundManager::playSound(SoundDefinition &soundDefinition, bool loop)
{
    auto soundId = std::make_shared<SoundId>(soundDefinition);
    _soundIds.push_back(soundId);
    soundId->play(loop);
    return soundId;
}

std::shared_ptr<SoundId> SoundManager::loopMusic(SoundDefinition &soundDefinition)
{
    auto soundId = std::make_shared<SoundId>(soundDefinition);
    _soundIds.push_back(soundId);
    soundId->play(true);
    return soundId;
}

void SoundManager::stopSound(SoundId &sound)
{
    std::cout << "stopSound" << std::endl;
    sound.stop();
    auto it = std::find_if(_soundIds.begin(), _soundIds.end(), [&sound](const std::shared_ptr<SoundId> &id) {
        return id.get() == &sound;
    });
    if (it == _soundIds.end())
        return;
    _soundIds.erase(it);
}
} // namespace ng