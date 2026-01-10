#pragma once
#include "plexus/context.h"
#include "plexus/node.h"
#include <utility>

namespace Plexus {

    /**
     * @brief A type-safe wrapper that ties data structures to their resource IDs.
     *
     * Resource<T> bundles data with its ResourceID, preventing accidental access
     * without proper dependency declaration. It provides convenience methods for
     * creating dependency declarations and accessing the underlying data.
     *
     * @tparam T The type of data being managed.
     */
    template <typename T>
    class Resource {
    public:
        /**
         * @brief Constructs a resource and automatically registers it with the context.
         *
         * @param ctx The context to register this resource with.
         * @param name The unique name for this resource.
         * @param initial Initial value for the data (default-constructed if omitted).
         */
        Resource(Context& ctx, const std::string& name, T initial = T{})
            : m_id(ctx.register_resource(name))
            , m_data(std::move(initial)) {}

        /**
         * @brief Gets the ResourceID for this resource.
         * @return ResourceID The unique identifier.
         */
        ResourceID id() const { return m_id; }

        /**
         * @brief Creates a READ dependency for this resource.
         * @return Dependency A dependency declaration with READ access.
         */
        Dependency read() const { return {m_id, Access::READ}; }

        /**
         * @brief Creates a WRITE dependency for this resource.
         * @return Dependency A dependency declaration with WRITE access.
         */
        Dependency write() const { return {m_id, Access::WRITE}; }

        /**
         * @brief Gets read-only access to the underlying data.
         * @return const T& Const reference to the data.
         */
        const T& get() const { return m_data; }

        /**
         * @brief Gets mutable access to the underlying data.
         * @return T& Mutable reference to the data.
         */
        T& get_mut() { return m_data; }

    private:
        ResourceID m_id;
        T m_data;
    };

    /**
     * @brief Wrapper for read-only access to a Resource.
     *
     * Used with the explicit Read()/Write() API to tag resources with their
     * intended access type at the call site.
     */
    template <typename T>
    class ReadAccess {
    public:
        explicit ReadAccess(const Resource<T>& res) : m_resource(res) {}

        Dependency dependency() const { return m_resource.read(); }
        const T& get() const { return m_resource.get(); }

    private:
        const Resource<T>& m_resource;
    };

    /**
     * @brief Wrapper for write access to a Resource.
     *
     * Used with the explicit Read()/Write() API to tag resources with their
     * intended access type at the call site.
     */
    template <typename T>
    class WriteAccess {
    public:
        explicit WriteAccess(Resource<T>& res) : m_resource(res) {}

        Dependency dependency() const { return m_resource.write(); }
        T& get() const { return m_resource.get_mut(); }

    private:
        Resource<T>& m_resource;
    };

    /**
     * @brief Creates a ReadAccess wrapper for a resource.
     * @param res The resource to wrap.
     * @return ReadAccess<T> A read-only access wrapper.
     */
    template <typename T>
    ReadAccess<T> Read(const Resource<T>& res) {
        return ReadAccess<T>(res);
    }

    /**
     * @brief Creates a WriteAccess wrapper for a resource.
     * @param res The resource to wrap.
     * @return WriteAccess<T> A write access wrapper.
     */
    template <typename T>
    WriteAccess<T> Write(Resource<T>& res) {
        return WriteAccess<T>(res);
    }

} // namespace Plexus
