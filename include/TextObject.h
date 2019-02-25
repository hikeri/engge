#pragma once
#include "Object.h"
#include "FntFont.h"
#include "SFML/Graphics.hpp"

namespace ng
{
enum TextAlignment
{
  Left = 0x10000000,
  Center = 0x20000000,
  Right = 0x40000000,
  Horizontal = Left|Center|Right,
  Top = 0x80000000,
  Bottom = 0x01000000,
  Vertical = Top|Bottom,
  All = Horizontal | Vertical
};

class TextObject : public Object
{
public:
  explicit TextObject();
  FntFont &getFont() { return _font; }
  void setText(const std::string &text) { _text = text; }
  void setAlignment(TextAlignment alignment) { _alignment = alignment; }

private:
  void draw(sf::RenderTarget &target, sf::RenderStates states) const override;

private:
  FntFont _font;
  std::string _text;
  TextAlignment _alignment;
};
} // namespace ng