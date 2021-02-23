#include <engge/Entities/TextObject.hpp>
#include <ngf/Graphics/Text.h>
#include <engge/Room/Room.hpp>
#include "Util/Util.hpp"

namespace ng {
TextAlignment operator|=(TextAlignment &lhs, TextAlignment rhs) {
  lhs = static_cast<TextAlignment>(
      static_cast<std::underlying_type<TextAlignment>::type>(lhs) |
          static_cast<std::underlying_type<TextAlignment>::type>(rhs));
  return lhs;
}

bool operator&(TextAlignment lhs, TextAlignment rhs) {
  return static_cast<TextAlignment>(
      static_cast<std::underlying_type<TextAlignment>::type>(lhs) &
          static_cast<std::underlying_type<TextAlignment>::type>(rhs)) > TextAlignment::None;
}

TextObject::TextObject() {
  setTemporary(true);
}

TextObject::~TextObject() = default;

void TextObject::setText(const std::string &text) {
  m_text = towstring(text);
}

void TextObject::draw(ngf::RenderTarget &target, ngf::RenderStates states) const {
  if (!isVisible())
    return;

  const auto view = target.getView();
  if (getScreenSpace() == ScreenSpace::Object) {
    target.setView(ngf::View(ngf::frect::fromPositionSize({0, 0}, {Screen::Width, Screen::Height})));
  }

  ngf::Text txt;
  txt.setFont(*m_font);
  txt.setColor(getColor());
  txt.setWideString(m_text);
  txt.setMaxWidth(static_cast<float>(m_maxWidth));
  auto bounds = txt.getLocalBounds();
  glm::vec2 offset{0, 0};
  if (m_alignment & TextAlignment::Center) {
    offset.x = getScale() * -bounds.getWidth() / 2;
  } else if (m_alignment & TextAlignment::Right) {
    offset.x = getScale() * bounds.getWidth() / 2;
  }
  if (m_alignment & TextAlignment::Top) {
    offset.y = 0;
  } else if (m_alignment & TextAlignment::Bottom) {
    offset.y = getScale() * bounds.getHeight();
  } else {
    offset.y = getScale() * bounds.getHeight() / 2;
  }
  auto height = target.getView().getSize().y;
  auto transformable = getTransform();
  transformable.move(offset);
  transformable.setPosition({transformable.getPosition().x, height - transformable.getPosition().y});

  if (getScreenSpace() == ScreenSpace::Object) {
    ngf::RenderStates s;
    s.transform = transformable.getTransform();
    txt.draw(target, s);
    target.setView(view);
  } else {
    states.transform = transformable.getTransform() * states.transform;
    txt.draw(target, states);
  }
}

} // namespace ng
