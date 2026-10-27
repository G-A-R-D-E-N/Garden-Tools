#include <fo4/avm2/class.hpp>
#include <fo4/avm2/display.hpp>
#include <fo4/avm2/display_snapshot.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/text.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>

namespace {

using fo4::avm2::Class;
using fo4::avm2::DisplayObject;
using fo4::avm2::DisplayObjectContainer;
using fo4::avm2::display_snapshot_hash;
using fo4::avm2::display_snapshot_text;
using fo4::avm2::fnv1a64;
using fo4::avm2::FontRegistry;
using fo4::avm2::TextFieldObject;

struct StageTree {
  Class stage_class;
  Class sprite_class;
  Class text_class;
  FontRegistry registry;
  std::shared_ptr<DisplayObjectContainer> root;
  std::shared_ptr<DisplayObject> sprite;
  std::shared_ptr<TextFieldObject> field;
  std::shared_ptr<DisplayObjectContainer> holder;
  std::shared_ptr<DisplayObject> inner;

  StageTree() {
    stage_class.name = "flash.display::Stage";
    sprite_class.name = "flash.display::Sprite";
    text_class.name = "flash.text::TextField";
    root = std::make_shared<DisplayObjectContainer>(&stage_class);
    sprite = std::make_shared<DisplayObject>(&sprite_class);
    sprite->set_character_id(7);
    sprite->set_x(10.0);
    sprite->set_y(20.0);
    sprite->set_width(30.0);
    sprite->set_height(40.0);
    sprite->set_alpha(0.5);
    field = std::make_shared<TextFieldObject>(&text_class, &registry);
    field->set_character_id(9);
    field->set_x(5.0);
    field->set_visible(false);
    field->set_text("Hi there");
    holder = std::make_shared<DisplayObjectContainer>(&sprite_class);
    holder->set_scale_x(2.0);
    inner = std::make_shared<DisplayObject>(&sprite_class);
    inner->set_character_id(3);
    inner->set_x(3.0);
    holder->add_child(inner);
    root->add_child(sprite);
    root->add_child(field);
    root->add_child(holder);
  }
};

const char* const kExpectedText =
    "class=flash.display::Stage depth=0 id=0 bounds=0 0 0 0 matrix=10000 0 0 10000 0 0 "
    "visible=1 alpha=10000 children=3\n"
    "  class=flash.display::Sprite depth=0 id=7 bounds=0 0 300000 400000 "
    "matrix=10000 0 0 10000 100000 200000 visible=1 alpha=5000 children=0\n"
    "  class=flash.text::TextField depth=1 id=9 bounds=0 0 0 0 matrix=10000 0 0 10000 50000 0 "
    "visible=0 alpha=10000 children=0 text=8:Hi there\n"
    "  class=flash.display::Sprite depth=2 id=0 bounds=0 0 0 0 matrix=20000 0 0 10000 0 0 "
    "visible=1 alpha=10000 children=1\n"
    "    class=flash.display::Sprite depth=0 id=3 bounds=0 0 0 0 "
    "matrix=20000 0 0 10000 60000 0 visible=1 alpha=10000 children=0\n";

TEST(Avm2DisplaySnapshot, TextListsEachNodeWithItsComposedMatrixAndTextFieldText) {
  StageTree tree;
  EXPECT_EQ(display_snapshot_text(*tree.root), kExpectedText);
}

TEST(Avm2DisplaySnapshot, HashIsTheFnv1aOfTheText) {
  StageTree tree;
  EXPECT_EQ(display_snapshot_hash(*tree.root), fnv1a64(kExpectedText));
}

TEST(Avm2DisplaySnapshot, AChangedAlphaChangesTheHash) {
  StageTree tree;
  const std::uint64_t before = display_snapshot_hash(*tree.root);
  tree.sprite->set_alpha(1.0);
  EXPECT_NE(display_snapshot_hash(*tree.root), before);
}

TEST(Avm2DisplaySnapshot, NonFiniteAndOutOfRangeValuesPrintAsWords) {
  Class klass;
  klass.name = "flash.display::Sprite";
  const auto object = std::make_shared<DisplayObject>(&klass);
  object->set_width(1e300);
  object->set_height(std::numeric_limits<double>::quiet_NaN());
  object->set_alpha(-std::numeric_limits<double>::infinity());
  const std::string text = display_snapshot_text(*object);
  EXPECT_NE(text.find(" bounds=0 0 inf nan matrix="), std::string::npos) << text;
  EXPECT_NE(text.find(" alpha=-inf children=0"), std::string::npos) << text;
}

TEST(Fnv1a64, MatchesThePublishedVectors) {
  EXPECT_EQ(fnv1a64(""), 0xcbf29ce484222325ULL);
  EXPECT_EQ(fnv1a64("a"), 0xaf63dc4c8601ec8cULL);
}

}  // namespace
