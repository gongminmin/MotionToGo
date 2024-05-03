#pragma once

namespace MotionToGo
{
    template <uint32_t Alignment>
    constexpr uint32_t Align(uint32_t size) noexcept
    {
        static_assert((Alignment & (Alignment - 1)) == 0);
        return (size + (Alignment - 1)) & ~(Alignment - 1);
    }

    [[noreturn]] inline void Unreachable()
    {
#if defined(_MSC_VER)
        __assume(false);
#else
        __builtin_unreachable();
#endif
    }
} // namespace MotionToGo
