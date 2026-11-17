#pragma once

#include "ge/ecs/world.hpp"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ge::ecs {

/// A queue of world changes to apply later.
///
/// Systems cannot add or remove components, or destroy entities, while they
/// iterate with `World::each`. They queue those changes here instead:
///
///     world.each<const Health>([&](Entity entity, const Health& health) {
///         if (health.points <= 0) {
///             world.resource<Commands>()->despawn(entity);
///         }
///     });
///
/// `App` stores a `Commands` resource and applies it after every system, so a
/// change takes effect as soon as the system that queued it returns. Changes
/// apply in the order they were queued. A change aimed at an entity that no
/// longer exists is skipped silently.
class Commands {
public:
    Commands() = default;
    Commands(const Commands&) = delete;
    Commands& operator=(const Commands&) = delete;
    Commands(Commands&&) noexcept = default;
    Commands& operator=(Commands&&) noexcept = default;
    ~Commands() = default;

    /// Attaches `component` (replacing one of the same type) when applied.
    template <typename T>
    void insert(
        Entity entity,
        T&& component) { // NOLINT(cppcoreguidelines-missing-std-forward): forwarded in the capture
        push([entity,
              value = std::remove_cvref_t<T>(std::forward<T>(component))](World& world) mutable {
            static_cast<void>(world.insert(entity, std::move(value)));
        });
    }

    /// Detaches the component of type `T` when applied.
    template <typename T>
    void remove(Entity entity) {
        push([entity](World& world) { static_cast<void>(world.remove<T>(entity)); });
    }

    /// Destroys the entity when applied.
    void despawn(Entity entity) {
        push([entity](World& world) { static_cast<void>(world.despawn(entity)); });
    }

    /// Creates an entity now, so it can be referred to right away, and queues
    /// its components. Creating an entity is safe even during iteration; the
    /// components arrive when the queue is applied.
    template <typename... Cs>
    Entity spawn_with(World& world, Cs&&... components) {
        const Entity entity = world.spawn();
        (insert(entity, std::forward<Cs>(components)), ...);
        return entity;
    }

    /// Queues any callable taking `World&`, for changes the methods above do
    /// not cover. It may queue more commands: they run in the same `apply`.
    template <typename F>
    void push(F&& command) {
        queue_.push_back(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(command)));
    }

    /// Runs every queued command, then any queued by those, until none is left.
    void apply(World& world);

    [[nodiscard]] std::size_t size() const noexcept { return queue_.size(); }
    [[nodiscard]] bool empty() const noexcept { return queue_.empty(); }

private:
    class Concept {
    public:
        Concept() = default;
        Concept(const Concept&) = delete;
        Concept& operator=(const Concept&) = delete;
        Concept(Concept&&) = delete;
        Concept& operator=(Concept&&) = delete;
        virtual ~Concept() = default;
        virtual void run(World& world) = 0;
    };

    template <typename F>
    class Model final : public Concept {
    public:
        explicit Model(F function) : function_(std::move(function)) {}

        void run(World& world) override { function_(world); }

    private:
        F function_;
    };

    std::vector<std::unique_ptr<Concept>> queue_;
};

} // namespace ge::ecs
