#pragma once

#include "ge/core/assert.hpp"
#include "ge/core/hash.hpp"
#include "ge/ecs/entity.hpp"
#include "ge/ecs/entity_allocator.hpp"
#include "ge/ecs/sparse_set.hpp"
#include "ge/ecs/type_id.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ge::ecs {
namespace detail {

template <typename C>
using Raw = std::remove_const_t<C>;

/// True if any of the pointers is null.
template <typename... Ps>
[[nodiscard]] constexpr bool any_null(const Ps*... pointers) noexcept {
    bool found = false;
    ((found = found || pointers == nullptr), ...);
    return found;
}

template <typename...>
inline constexpr bool all_distinct_v = true;

template <typename T, typename... Rest>
inline constexpr bool all_distinct_v<T, Rest...> =
    (!std::is_same_v<T, Rest> && ...) && all_distinct_v<Rest...>;

inline std::span<const Entity> smaller(std::span<const Entity> a,
                                       std::span<const Entity> b) noexcept {
    return b.size() < a.size() ? b : a;
}

class ResourceBase {
public:
    ResourceBase() = default;
    ResourceBase(const ResourceBase&) = delete;
    ResourceBase& operator=(const ResourceBase&) = delete;
    ResourceBase(ResourceBase&&) = delete;
    ResourceBase& operator=(ResourceBase&&) = delete;
    virtual ~ResourceBase() = default;

    /// An independent copy, or null when the resource is not copyable.
    [[nodiscard]] virtual std::unique_ptr<ResourceBase> clone() const = 0;

    /// Hash of the value, or nothing when the type is not `Hashable`.
    [[nodiscard]] virtual std::optional<std::uint64_t> digest() const = 0;
};

template <typename R>
struct ResourceBox final : ResourceBase {
    template <typename... Args>
    explicit ResourceBox(Args&&... args) : value(std::forward<Args>(args)...) {}

    [[nodiscard]] std::unique_ptr<ResourceBase> clone() const override {
        if constexpr (std::is_copy_constructible_v<R>) {
            return std::make_unique<ResourceBox>(value);
        } else {
            return nullptr;
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> digest() const override {
        if constexpr (Hashable<R>) {
            return hash_of(value);
        } else {
            return std::nullopt;
        }
    }

    R value;
};

} // namespace detail

/// Container for every entity, component and resource of a simulation.
///
/// Any movable type is a component or a resource: there is nothing to derive
/// from, register or declare. Adding a component type to a game is defining a
/// struct and inserting it.
///
/// Structural changes (adding or removing components, destroying entities) are
/// not allowed from inside an `each` callback, because they would invalidate
/// the iteration. This is checked by `GE_ASSERT` in debug builds.
class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;
    ~World() = default;

    // ---- snapshots -----------------------------------------------------------

    /// An independent copy of the world: entities, components and resources.
    ///
    /// This is what replays, rollback and "what if" simulation are built on.
    /// Returns nothing when a component type is not copyable, because the copy
    /// would silently miss it. Resources that are not copyable (such as the
    /// pending `Commands` queue) are left out of the copy.
    [[nodiscard]] std::optional<World> clone() const {
        World copy;
        copy.allocator_ = allocator_;
        copy.storages_.resize(storages_.size());
        for (std::size_t id = 0; id < storages_.size(); ++id) {
            if (storages_[id] == nullptr) {
                continue;
            }
            copy.storages_[id] = storages_[id]->clone();
            if (copy.storages_[id] == nullptr) {
                return std::nullopt;
            }
        }
        copy.resources_.resize(resources_.size());
        for (std::size_t id = 0; id < resources_.size(); ++id) {
            if (resources_[id] != nullptr) {
                copy.resources_[id] = resources_[id]->clone();
            }
        }
        return copy;
    }

    /// A 64-bit hash of the whole state: which entities exist, their
    /// components and the resources. Two worlds that went through the same
    /// deterministic simulation have the same fingerprint; a divergence
    /// changes it, which makes it the cheap way to compare runs or to detect a
    /// desync between peers.
    ///
    /// It covers every component and resource of a `Hashable` type (see
    /// `fingerprint_is_complete`); for the others only the entities that carry
    /// them count. It does not depend on the order types were first used, but
    /// it does not tell two component types apart either: stable type names
    /// will come with the module descriptors.
    [[nodiscard]] std::uint64_t fingerprint() const {
        std::uint64_t storages_sum = 0;
        for (const auto& storage : storages_) {
            if (storage != nullptr) {
                storages_sum += storage->digest();
            }
        }
        std::uint64_t resources_sum = 0;
        for (const auto& resource : resources_) {
            if (resource != nullptr) {
                resources_sum += resource->digest().value_or(0);
            }
        }
        Hasher hasher;
        hasher.add(allocator_);
        hasher.add(storages_sum);
        hasher.add(resources_sum);
        return hasher.value();
    }

    /// True when `fingerprint` covers every component and resource value.
    [[nodiscard]] bool fingerprint_is_complete() const noexcept {
        const auto covered = [](const auto& item) { return item == nullptr || item->hashable(); };
        for (const auto& storage : storages_) {
            if (!covered(storage)) {
                return false;
            }
        }
        for (const auto& resource : resources_) {
            if (resource != nullptr && !resource->digest().has_value()) {
                return false;
            }
        }
        return true;
    }

    // ---- entities ------------------------------------------------------------

    /// Creates an entity with no components.
    Entity spawn() { return allocator_.allocate(); }

    /// Creates an entity carrying the given components.
    template <typename... Cs>
    Entity spawn_with(Cs&&... components) {
        const Entity entity = spawn();
        (static_cast<void>(emplace<std::remove_cvref_t<Cs>>(entity, std::forward<Cs>(components))),
         ...);
        return entity;
    }

    /// Destroys an entity and all its components. Returns false if the handle
    /// was already stale.
    bool despawn(Entity entity) {
        GE_ASSERT(iterating_ == 0);
        if (!allocator_.release(entity)) {
            return false;
        }
        for (const auto& storage : storages_) {
            if (storage != nullptr) {
                storage->remove_entity(entity.index());
            }
        }
        return true;
    }

    [[nodiscard]] bool is_alive(Entity entity) const noexcept {
        return allocator_.contains(entity);
    }

    [[nodiscard]] std::size_t entity_count() const noexcept { return allocator_.size(); }

    /// Snapshot of every live entity.
    [[nodiscard]] std::vector<Entity> entities() const { return allocator_.alive(); }

    // ---- components ----------------------------------------------------------

    /// Constructs a component of type `T` on the entity, replacing the one it
    /// had. Returns null if the entity is not alive.
    template <typename T, typename... Args>
    [[nodiscard]] T* emplace(Entity entity, Args&&... args) {
        GE_ASSERT(iterating_ == 0);
        if (!allocator_.contains(entity)) {
            return nullptr;
        }
        return &ensure_storage<T>().emplace(entity, std::forward<Args>(args)...);
    }

    /// Attaches a component, replacing the one of the same type. Returns false
    /// if the entity is not alive.
    template <typename T>
    [[nodiscard]] bool insert(Entity entity, T&& component) {
        return emplace<std::remove_cvref_t<T>>(entity, std::forward<T>(component)) != nullptr;
    }

    /// Detaches a component. Returns false if the entity did not have one.
    template <typename T>
    bool remove(Entity entity) {
        GE_ASSERT(iterating_ == 0);
        auto* storage = find_storage<T>();
        return storage != nullptr && storage->remove(entity);
    }

    /// The entity's component of type `T`, or null.
    template <typename T>
    [[nodiscard]] T* get(Entity entity) noexcept {
        auto* storage = find_storage<T>();
        return storage != nullptr ? storage->get(entity) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* get(Entity entity) const noexcept {
        const auto* storage = find_storage<T>();
        return storage != nullptr ? storage->get(entity) : nullptr;
    }

    template <typename T>
    [[nodiscard]] bool has(Entity entity) const noexcept {
        return get<T>(entity) != nullptr;
    }

    /// Number of entities carrying a component of type `T`.
    template <typename T>
    [[nodiscard]] std::size_t count() const noexcept {
        const auto* storage = find_storage<T>();
        return storage != nullptr ? storage->size() : 0;
    }

    /// Calls `callback(entity, components...)` for every entity that has all
    /// the requested component types. Use `const T` for read-only access.
    ///
    ///     world.each<Position, const Velocity>(
    ///         [](Entity, Position& p, const Velocity& v) { p.x += v.x; });
    template <typename... Cs, typename F>
    void each(F&& callback) { // NOLINT(cppcoreguidelines-missing-std-forward): called repeatedly
        static_assert(sizeof...(Cs) > 0, "each needs at least one component type");
        static_assert(detail::all_distinct_v<detail::Raw<Cs>...>,
                      "each needs distinct component types");

        std::tuple<SparseSet<detail::Raw<Cs>>*...> sets{find_storage<detail::Raw<Cs>>()...};
        if (detail::any_null(std::get<SparseSet<detail::Raw<Cs>>*>(sets)...)) {
            return;
        }

        // Walk the rarest component, then look the others up by entity.
        std::span<const Entity> pivot = std::get<0>(sets)->entities();
        ((pivot = detail::smaller(pivot, std::get<SparseSet<detail::Raw<Cs>>*>(sets)->entities())),
         ...);

        const IterationGuard guard{iterating_};
        for (const Entity entity : pivot) {
            std::tuple<Cs*...> components{
                std::get<SparseSet<detail::Raw<Cs>>*>(sets)->get(entity)...};
            if (detail::any_null(std::get<Cs*>(components)...)) {
                continue;
            }
            callback(entity, *std::get<Cs*>(components)...);
        }
    }

    /// Read-only variant of `each`: every component type must be `const`.
    template <typename... Cs, typename F>
    void each(F&& callback) const {
        static_assert((std::is_const_v<Cs> && ...),
                      "each on a const World needs const component types");
        // The callback only receives const references, so the world is not modified.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        const_cast<World&>(*this).template each<Cs...>(std::forward<F>(callback));
    }

    // ---- resources -----------------------------------------------------------

    /// Constructs the resource of type `R`, replacing the previous one.
    template <typename R, typename... Args>
    R& emplace_resource(Args&&... args) {
        const std::size_t id = type_id<R>();
        if (id >= resources_.size()) {
            resources_.resize(id + 1);
        }
        auto box = std::make_unique<detail::ResourceBox<R>>(std::forward<Args>(args)...);
        R& value = box->value;
        resources_[id] = std::move(box);
        return value;
    }

    /// Stores a resource, replacing the previous one of the same type.
    template <typename R>
    R& insert_resource(R&& resource) {
        return emplace_resource<std::remove_cvref_t<R>>(std::forward<R>(resource));
    }

    /// The resource of type `R`, or null.
    template <typename R>
    [[nodiscard]] R* resource() noexcept {
        const std::size_t id = type_id<R>();
        if (id >= resources_.size() || resources_[id] == nullptr) {
            return nullptr;
        }
        // The id is unique to R, so the stored box is a ResourceBox<R>.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        return &static_cast<detail::ResourceBox<R>&>(*resources_[id]).value;
    }

    template <typename R>
    [[nodiscard]] const R* resource() const noexcept {
        const std::size_t id = type_id<R>();
        if (id >= resources_.size() || resources_[id] == nullptr) {
            return nullptr;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        return &static_cast<const detail::ResourceBox<R>&>(*resources_[id]).value;
    }

    /// Destroys the resource of type `R`. Returns false if there was none.
    template <typename R>
    bool remove_resource() {
        const std::size_t id = type_id<R>();
        if (id >= resources_.size() || resources_[id] == nullptr) {
            return false;
        }
        resources_[id].reset();
        return true;
    }

private:
    // Marks the world as being iterated for the lifetime of the guard.
    class IterationGuard {
    public:
        explicit IterationGuard(int& counter) noexcept : counter_(counter) { ++counter_; }
        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;
        IterationGuard(IterationGuard&&) = delete;
        IterationGuard& operator=(IterationGuard&&) = delete;
        ~IterationGuard() { --counter_; }

    private:
        int& counter_;
    };

    template <typename T>
    [[nodiscard]] SparseSet<T>* find_storage() noexcept {
        const std::size_t id = type_id<T>();
        if (id >= storages_.size() || storages_[id] == nullptr) {
            return nullptr;
        }
        // The id is unique to T, so the stored storage is a SparseSet<T>.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        return static_cast<SparseSet<T>*>(storages_[id].get());
    }

    template <typename T>
    [[nodiscard]] const SparseSet<T>* find_storage() const noexcept {
        const std::size_t id = type_id<T>();
        if (id >= storages_.size() || storages_[id] == nullptr) {
            return nullptr;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        return static_cast<const SparseSet<T>*>(storages_[id].get());
    }

    template <typename T>
    SparseSet<T>& ensure_storage() {
        if (auto* existing = find_storage<T>(); existing != nullptr) {
            return *existing;
        }
        const std::size_t id = type_id<T>();
        if (id >= storages_.size()) {
            storages_.resize(id + 1);
        }
        auto storage = std::make_unique<SparseSet<T>>();
        SparseSet<T>& reference = *storage;
        storages_[id] = std::move(storage);
        return reference;
    }

    EntityAllocator allocator_;
    std::vector<std::unique_ptr<StorageBase>> storages_;
    std::vector<std::unique_ptr<detail::ResourceBase>> resources_;
    int iterating_ = 0;
};

} // namespace ge::ecs
