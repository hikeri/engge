#include <utility>
#include "SoundDefinition.h"

namespace ng
{
SoundDefinition::SoundDefinition(std::string path)
    : _pSettings(nullptr), _path(std::move(path)), _isLoaded(false)
{
}

SoundDefinition::~SoundDefinition()
{
    std::cout << "delete SoundDefinition " << _path << " " << std::hex << this << std::endl;
}

void SoundDefinition::setSettings(EngineSettings &settings)
{
    _pSettings = &settings;
}

void SoundDefinition::load()
{
    if (_isLoaded)
        return;
    std::vector<char> buffer;
    _pSettings->readEntry(_path, buffer);
    _isLoaded = _buffer.loadFromMemory(buffer.data(), buffer.size());
    if (!_isLoaded)
    {
        std::cerr << "Can't load the sound " << _path << std::endl;
    }
}

} // namespace ng
