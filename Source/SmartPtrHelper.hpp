#pragma once

#ifdef _WINDOWS
#include <windows.h>
#endif

#include <memory>

namespace MotionToGo
{
#ifdef _WINDOWS
    class Win32HandleDeleter final
    {
    public:
        void operator()(HANDLE handle)
        {
            if (handle != INVALID_HANDLE_VALUE)
            {
                ::CloseHandle(handle);
            }
        }
    };
    using Win32UniqueHandle = std::unique_ptr<void, Win32HandleDeleter>;

    inline Win32UniqueHandle MakeWin32UniqueHandle(HANDLE handle)
    {
        return Win32UniqueHandle(handle);
    }
#endif
} // namespace MotionToGo
