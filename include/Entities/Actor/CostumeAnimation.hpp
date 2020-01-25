#pragma once
#include <memory>
#include <sstream>
#include "SFML/Graphics.hpp"
#include "CostumeLayer.hpp"

namespace ng
{
enum class AnimationState
{
  Pause,
  Play
};

class CostumeAnimation : public sf::Drawable
{
public:
  explicit CostumeAnimation(std::string name);
  ~CostumeAnimation();

  const std::string &getName() const { return _name; }
  std::vector<std::unique_ptr<CostumeLayer>> &getLayers() { return _layers; }

  void play(bool loop = false);
  void pause() { _state = AnimationState::Pause; }
  bool isPlaying() const { return _state == AnimationState::Play; }
  void setFps(int fps);

  bool contains(const sf::Vector2f& pos) const;

  void update(const sf::Time &elapsed);

private:
  void draw(sf::RenderTarget &target, sf::RenderStates states) const override;

private:
  std::string _name;
  std::vector<std::unique_ptr<CostumeLayer>> _layers;
  AnimationState _state;
  bool _loop{false};
};
} // namespace ng
