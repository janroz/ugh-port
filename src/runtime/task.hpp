#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

// The coroutine protocol calls these hooks on instances (co_await / promise); making stateless ones static
// only moves the diagnostic to every co_await site.
// NOLINTBEGIN(readability-convert-member-functions-to-static)

namespace ugh {

template <typename T = void>
class Task;

namespace detail {

struct PromiseBase {
    std::coroutine_handle<> continuation;

    struct FinalAwaiter {
        [[nodiscard]] bool await_ready() const noexcept { return false; }

        template <typename Promise>
        [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> finished) const noexcept
        {
            const std::coroutine_handle<> next = finished.promise().continuation;
            return next ? next : std::noop_coroutine();
        }

        void await_resume() const noexcept {}
    };

    [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
    [[nodiscard]] FinalAwaiter final_suspend() const noexcept { return {}; }

    // The game does not use exceptions; reaching this is a bug.
    [[noreturn]] void unhandled_exception() const noexcept { std::terminate(); }
};

template <typename T>
struct Promise : PromiseBase {
    std::optional<T> value;

    Task<T> get_return_object();
    void return_value(T result) { value = std::move(result); }
};

template <>
struct Promise<void> : PromiseBase {
    Task<void> get_return_object();
    void return_void() const noexcept {}
};

} // namespace detail

/// Lazily started coroutine owning its frame (RAII). Awaiting it runs it to completion and yields its result;
/// a root task is started with start() and then driven by FrameClock::tick().
template <typename T>
class [[nodiscard]] Task {
public:
    using promise_type = detail::Promise<T>;
    using Handle = std::coroutine_handle<promise_type>;

    explicit Task(Handle handle) : handle_(handle) {}
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { reset(); }

    [[nodiscard]] bool await_ready() const noexcept { return false; }

    [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
    {
        handle_.promise().continuation = caller;
        return handle_;
    }

    T await_resume()
    {
        if constexpr (!std::is_void_v<T>) {
            std::optional<T>& result = handle_.promise().value;
            if (!result) {
                std::terminate(); // resumed before the coroutine produced its value: a runtime bug
            }
            return std::move(*result);
        }
    }

    /// Runs a root task until its first suspension.
    void start() { handle_.resume(); }

    [[nodiscard]] bool done() const { return !handle_ || handle_.done(); }

private:
    void reset()
    {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    Handle handle_;
};

namespace detail {

template <typename T>
Task<T> Promise<T>::get_return_object()
{
    return Task<T>(Task<T>::Handle::from_promise(*this));
}

inline Task<void> Promise<void>::get_return_object()
{
    return Task<void>(Task<void>::Handle::from_promise(*this));
}

} // namespace detail

/// Vertical-retrace clock: coroutines `co_await clock.nextFrame()`, the host calls tick() once per 70 Hz frame.
class FrameClock {
public:
    struct Awaiter {
        FrameClock* clock;

        [[nodiscard]] bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> waiting) const noexcept { clock->waiting_ = waiting; }
        void await_resume() const noexcept {}
    };

    [[nodiscard]] Awaiter nextFrame() { return Awaiter{this}; }

    /// Resumes whatever is waiting for the frame boundary; it runs until its next nextFrame() or completion.
    void tick()
    {
        if (const std::coroutine_handle<> waiting = std::exchange(waiting_, {})) {
            waiting.resume();
        }
    }

private:
    std::coroutine_handle<> waiting_;
};

} // namespace ugh

// NOLINTEND(readability-convert-member-functions-to-static)
