#include "ge/app/render.hpp"
#include "ge/app/time.hpp"
#include "ge/core/hash.hpp"
#include "ge/mod/particles2d/particles2d.gen.hpp"
#include "ge/mod/sprite2d/layered.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numbers>
#include <vector>

namespace ge::mod::particles2d {

namespace {

struct Particle {
    float x = 0.0F;
    float y = 0.0F;
    float before_x = 0.0F; ///< Where it was a tick ago, for smooth drawing.
    float before_y = 0.0F;
    float speed_x = 0.0F;
    float speed_y = 0.0F;
    float age = 0.0F;
    float life = 1.0F;
    float phase = 0.0F; ///< Where its sway starts.
};

/// A small generator of its own (splitmix64), so particles never touch the game's Random.
struct Chances {
    std::uint64_t state = 0;

    std::uint64_t next() {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    /// A number in [0, 1).
    float unit() { return static_cast<float>(next() >> 40U) / 16777216.0F; }

    /// A number in [-1, 1).
    float signed_unit() { return (unit() * 2.0F) - 1.0F; }
};

/// The particles of one emitter, with what is needed to draw them once the emitter is gone.
struct Cloud {
    Emitter emitter;
    Chances chances;
    std::vector<Particle> particles;
    float owed = 0.0F; ///< Particles due but not let out yet (a fraction of one, usually).
    bool started = false;
    bool seen = false; ///< Its emitter was found this tick.
};

/// Every cloud, by emitter. Presentation only: left out of the world fingerprint.
struct Clouds {
    std::map<std::uint64_t, Cloud> clouds;

    void hash(Hasher& /*hasher*/) const noexcept {}
};

Clouds& clouds_of(ecs::World& world) {
    if (auto* clouds = world.resource<Clouds>(); clouds != nullptr) {
        return *clouds;
    }
    return world.insert_resource(Clouds{});
}

constexpr float degrees = std::numbers::pi_v<float> / 180.0F;

void let_out(Cloud& cloud, const transform2d::Position& at) {
    const Emitter& e = cloud.emitter;
    Chances& chances = cloud.chances;
    Particle particle;
    particle.x = at.x + (chances.signed_unit() * e.area_width / 2.0F);
    particle.y = at.y + (chances.signed_unit() * e.area_height / 2.0F);
    particle.before_x = particle.x;
    particle.before_y = particle.y;
    const float direction = (e.angle + (chances.signed_unit() * e.spread / 2.0F)) * degrees;
    const float speed = std::max(e.speed + (chances.signed_unit() * e.speed_spread), 0.0F);
    particle.speed_x = std::cos(direction) * speed;
    particle.speed_y = std::sin(direction) * speed;
    particle.life = std::max(e.lifetime + (chances.signed_unit() * e.lifetime_spread), 0.01F);
    particle.phase = chances.unit() * 2.0F * std::numbers::pi_v<float>;
    cloud.particles.push_back(particle);
}

/// Lets out what the emitter owes this tick: its burst when it starts, then its rate.
void emit(Cloud& cloud, const transform2d::Position& at, float step) {
    const Emitter& e = cloud.emitter;
    std::uint32_t count = 0;
    if (!cloud.started) {
        cloud.started = true;
        count = e.burst;
    }
    if (e.active && e.rate > 0.0F) {
        cloud.owed += e.rate * step;
        const auto whole = static_cast<std::uint32_t>(cloud.owed);
        cloud.owed -= static_cast<float>(whole);
        count += whole;
    }
    for (std::uint32_t i = 0; i < count && cloud.particles.size() < e.max; ++i) {
        let_out(cloud, at);
    }
}

/// One tick of every particle of a cloud; the dead are removed.
void move(Cloud& cloud, float step) {
    const Emitter& e = cloud.emitter;
    const float keep = std::clamp(1.0F - (e.drag * step), 0.0F, 1.0F);
    for (Particle& p : cloud.particles) {
        p.age += step;
        p.before_x = p.x;
        p.before_y = p.y;
        p.speed_x = (p.speed_x + (e.gravity_x * step)) * keep;
        p.speed_y = (p.speed_y + (e.gravity_y * step)) * keep;
        if (e.wander != 0.0F) {
            p.speed_x += std::sin((p.age * 2.3F) + p.phase) * e.wander * step;
            p.speed_y += std::cos((p.age * 1.7F) + (p.phase * 1.3F)) * e.wander * step;
        }
        p.x += p.speed_x * step;
        p.y += p.speed_y * step;
    }
    std::erase_if(cloud.particles, [](const Particle& p) { return p.age >= p.life; });
}

/// How visible a particle is at `t` (0 at its birth, 1 at its end): it fades in over the first
/// tenth of its life and out over the last third.
float visibility(bool fade, float t) {
    if (!fade) {
        return 1.0F;
    }
    return std::clamp(std::min(t / 0.1F, (1.0F - t) / 0.35F), 0.0F, 1.0F);
}

std::uint32_t channel(std::uint32_t value) {
    return std::min(value, 255U);
}

app::DrawCommand command_for(const Emitter& e, const Particle& p, float alpha) {
    const float t = std::clamp(p.age / p.life, 0.0F, 1.0F);
    const float size = e.size + ((e.end_size - e.size) * t);
    const float opacity = static_cast<float>(channel(e.alpha)) * visibility(e.fade, t);
    const std::uint32_t color = (channel(e.red) << 24U) | (channel(e.green) << 16U) |
                                (channel(e.blue) << 8U) |
                                static_cast<std::uint32_t>(std::lround(opacity));
    const float x = p.before_x + ((p.x - p.before_x) * alpha);
    const float y = p.before_y + ((p.y - p.before_y) * alpha);
    app::DrawCommand command{"glow", x, y, size / 2.0F, color};
    if (e.atlas != 0xFFFFFFFFU) {
        command.what = "sprite";
        command.atlas = static_cast<int>(std::min<std::uint32_t>(e.atlas, 0x7FFFFFFFU));
        command.frame = e.frame;
        command.size = 0.0F;
        command.width = size;
        command.height = size;
    }
    if (e.additive) {
        command.blend = app::Blend::Add;
    }
    command.lit = !e.unlit;
    command.light = e.light;
    return command;
}

} // namespace

void advance(ecs::World& world) {
    Clouds& all = clouds_of(world);
    const float step = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    for (auto& [key, cloud] : all.clouds) {
        cloud.seen = false;
    }
    world.each<const Emitter, const transform2d::Position>(
        [&](ecs::Entity entity, const Emitter& emitter, const transform2d::Position& position) {
            const std::uint64_t key = entity.to_bits();
            auto [found, created] = all.clouds.try_emplace(key);
            Cloud& cloud = found->second;
            if (created) {
                cloud.chances.state = key * 0xD1B54A32D192ED03ULL;
            }
            cloud.emitter = emitter;
            cloud.seen = true;
            move(cloud, step);
            emit(cloud, position, step);
        });
    // Clouds whose emitter is gone live out their particles, then go.
    std::erase_if(all.clouds, [step](auto& entry) {
        Cloud& cloud = entry.second;
        if (!cloud.seen) {
            move(cloud, step);
        }
        return !cloud.seen && cloud.particles.empty();
    });
}

void draw(ecs::World& world) {
    const auto* all = world.resource<Clouds>();
    if (all == nullptr || world.resource<app::RenderBackend>() == nullptr) {
        return; // nothing draws them: sprite2d would never empty the list
    }
    const auto* time = world.resource<app::Time>();
    const float alpha = time != nullptr && time->interpolate ? time->alpha : 1.0F;
    sprite2d::LayeredDraws& draws = sprite2d::layered_draws(world);
    for (const auto& [key, cloud] : all->clouds) {
        for (const Particle& particle : cloud.particles) {
            draws.add(cloud.emitter.layer, command_for(cloud.emitter, particle, alpha));
        }
    }
}

} // namespace ge::mod::particles2d
