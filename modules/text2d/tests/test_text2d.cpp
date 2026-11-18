#include "ge/app/app.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/render.hpp"
#include "ge/app/runtime_lists.hpp"
#include "ge/app/text.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <memory>
#include <string>
#include <vector>

namespace {

namespace text2d = ge::mod::text2d;
namespace transform2d = ge::mod::transform2d;

/// An 8x8 font showing "ab 0123456789.=n" in that order, and a few texts.
ge::app::TextTable table(std::vector<std::string> texts) {
    ge::app::TextTable result;
    result.texts = std::move(texts);
    result.fonts.push_back(ge::app::FontInfo{U"ab 0123456789.=n", 8, 8});
    return result;
}

struct Scene {
    std::shared_ptr<ge::app::RecordingRenderer> renderer =
        std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app;

    explicit Scene(std::vector<std::string> texts) {
        transform2d::install(app);
        text2d::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::RenderBackend{renderer});
        app.world().insert_resource(table(std::move(texts)));
    }

    void show(float x, float y, const text2d::Text& text) {
        const ge::ecs::Entity entity = app.world().spawn();
        CHECK(app.world().insert(entity, transform2d::Position{x, y}));
        CHECK(app.world().insert(entity, text));
        app.run_frames(1);
    }

    [[nodiscard]] const std::vector<ge::app::DrawCommand>& drawn() const {
        return renderer->commands();
    }
};

text2d::Text text_of(std::uint32_t string) {
    text2d::Text text;
    text.string = string;
    return text;
}

} // namespace

TEST_CASE("text2d: every letter is a cell of the font, side by side from the position") {
    Scene scene({"ab"});
    scene.show(100.0F, 50.0F, text_of(0));
    REQUIRE(scene.drawn().size() == 2U);
    CHECK(scene.drawn()[0].what == "sprite");
    CHECK(scene.drawn()[0].atlas == 0);
    CHECK(scene.drawn()[0].frame == 0U); // a
    CHECK(scene.drawn()[1].frame == 1U); // b
    CHECK(scene.drawn()[0].x == 104.0F); // the left edge is the position
    CHECK(scene.drawn()[1].x == 112.0F);
    CHECK(scene.drawn()[0].y == 54.0F); // the top edge is the position
}

TEST_CASE("text2d: a letter is drawn at the font's cell size, white, in the world") {
    Scene scene({"ab"});
    scene.show(100.0F, 50.0F, text_of(0));
    REQUIRE(scene.drawn().size() == 2U);
    CHECK(scene.drawn()[0].width == 8.0F);
    CHECK(scene.drawn()[0].height == 8.0F);
    CHECK(scene.drawn()[0].color == 0xFFFFFFFFU);
    CHECK(!scene.drawn()[0].screen);
}

TEST_CASE("text2d: size scales the letters, spacing separates them") {
    Scene big({"ab"});
    text2d::Text text = text_of(0);
    text.size = 16.0F; // twice the font's own 8
    big.show(100.0F, 50.0F, text);
    REQUIRE(big.drawn().size() == 2U);
    CHECK(big.drawn()[0].width == 16.0F);
    CHECK(big.drawn()[0].x == 108.0F);
    CHECK(big.drawn()[1].x == 124.0F);
    CHECK(big.drawn()[0].y == 58.0F);

    Scene spaced({"ab"});
    text.size = 0.0F;
    text.spacing = 2.0F;
    spaced.show(100.0F, 50.0F, text);
    REQUIRE(spaced.drawn().size() == 2U);
    CHECK(spaced.drawn()[1].x == 114.0F); // 100 + 10 + 4
}

TEST_CASE("text2d: the pivot says which point of the text sits on the position") {
    Scene scene({"ab"});
    text2d::Text text = text_of(0);
    text.pivot_x = 0.5F; // the text is 16 wide: it starts 8 to the left
    text.pivot_y = 1.0F; // and it is 8 tall: it ends on the position
    scene.show(100.0F, 50.0F, text);
    REQUIRE(scene.drawn().size() == 2U);
    CHECK(scene.drawn()[0].x == 96.0F);
    CHECK(scene.drawn()[1].x == 104.0F);
    CHECK(scene.drawn()[0].y == 46.0F);
}

TEST_CASE("text2d: a newline starts a new line, and lines share the pivot") {
    Scene scene({"a\nbb"});
    text2d::Text text = text_of(0);
    text.line_spacing = 2.0F;
    text.pivot_x = 0.5F;
    scene.show(100.0F, 0.0F, text);
    REQUIRE(scene.drawn().size() == 3U);
    CHECK(scene.drawn()[0].y == 4.0F);   // first line
    CHECK(scene.drawn()[1].y == 14.0F);  // 8 + 2 lower
    CHECK(scene.drawn()[0].x == 100.0F); // the short line is centred over the long one
    CHECK(scene.drawn()[1].x == 96.0F);
    CHECK(scene.drawn()[2].x == 104.0F);
}

TEST_CASE("text2d: {value} is replaced by the number, without useless decimals") {
    Scene whole({"n={value}"});
    text2d::Text text = text_of(0);
    text.value = 12.0F;
    whole.show(0.0F, 0.0F, text);
    REQUIRE(whole.drawn().size() == 4U); // n = 1 2
    CHECK(whole.drawn()[2].frame == 4U); // '1' is the fifth glyph of the font
    CHECK(whole.drawn()[3].frame == 5U); // '2'

    Scene decimals({"{value}"});
    text.value = 2.5F;
    decimals.show(0.0F, 0.0F, text);
    CHECK(decimals.drawn().size() == 4U); // 2 . 5 0
}

TEST_CASE("text2d: letters the font lacks leave a gap, and bad references draw nothing") {
    Scene scene({"a?b"});
    scene.show(0.0F, 0.0F, text_of(0));
    REQUIRE(scene.drawn().size() == 2U);
    CHECK(scene.drawn()[0].x == 4.0F);
    CHECK(scene.drawn()[1].x == 20.0F); // the '?' still takes room: b is third

    Scene bad({"ab"});
    bad.show(0.0F, 0.0F, text_of(5)); // no such text
    text2d::Text other_font = text_of(0);
    other_font.font = 3; // no such font
    bad.show(0.0F, 0.0F, other_font);
    CHECK(bad.drawn().empty());
}

TEST_CASE("text2d: tint, screen space and the missing resources") {
    Scene scene({"a"});
    text2d::Text text = text_of(0);
    text.red = 1000U;
    text.green = 0U;
    text.blue = 0U;
    text.alpha = 128U;
    text.screen = true;
    scene.show(0.0F, 0.0F, text);
    REQUIRE(scene.drawn().size() == 1U);
    CHECK(scene.drawn()[0].color == 0xFF000080U);
    CHECK(scene.drawn()[0].screen);

    // Without a renderer or without a table, nothing is drawn and nothing breaks.
    ge::app::App bare;
    transform2d::install(bare);
    text2d::install(bare);
    const ge::ecs::Entity entity = bare.world().spawn();
    CHECK(bare.world().insert(entity, transform2d::Position{}));
    CHECK(bare.world().insert(entity, text2d::Text{}));
    bare.run_frames(1);
}

namespace {

/// A text that shows item `index` of the run-time list "names" (the game's runtime_list item 0).
void show_item(Scene& scene, std::uint32_t index) {
    ge::app::ListNames names;
    names.lists.emplace_back("runtime_list", std::vector<std::string>{"names"});
    scene.app.world().insert_resource(std::move(names));
    const ge::ecs::Entity entity = scene.app.world().spawn();
    CHECK(scene.app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    CHECK(scene.app.world().insert(entity, text_of(0)));
    CHECK(scene.app.world().insert(entity, text2d::Dynamic{0, index}));
    scene.app.run_frames(1);
}

} // namespace

TEST_CASE("text2d: a Dynamic shows an item of a list filled while the game runs") {
    Scene scene({"zz"});
    scene.app.world().resource<ge::app::RuntimeLists>()->set("names", {"ab", "ba."});
    show_item(scene, 1);
    REQUIRE(scene.drawn().size() == 3U); // "ba.", not the game's "zz"
    CHECK(scene.drawn()[0].frame == 1U);
    CHECK(scene.drawn()[1].frame == 0U);
    CHECK(scene.drawn()[2].frame == 13U);
}

TEST_CASE("text2d: an item that does not exist, or a list never filled, shows nothing") {
    Scene scene({"ab"});
    scene.app.world().resource<ge::app::RuntimeLists>()->set("names", {"ab"});
    show_item(scene, 5);
    CHECK(scene.drawn().empty());

    Scene empty({"ab"});
    show_item(empty, 0);
    CHECK(empty.drawn().empty());
}

TEST_CASE("text2d: a list that is replaced is shown again at the next frame") {
    Scene scene({"ab"});
    auto* lists = scene.app.world().resource<ge::app::RuntimeLists>();
    lists->set("names", {"a"});
    show_item(scene, 0);
    CHECK(scene.drawn().size() == 1U);
    lists->set("names", {"abab"});
    scene.app.run_frames(1);
    CHECK(scene.drawn().size() == 5U); // the frame before (1 letter) plus 4 now
}
