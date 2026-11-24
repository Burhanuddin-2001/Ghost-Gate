#pragma once          // tell the compiler: include this file only once (important)

#include <windows.h>  // gives us HANDLE, CloseHandle, INVALID_HANDLE_VALUE
#include <memory>     // gives us std::unique_ptr

namespace gh {        //A namespace keeps our names tidy.

// The custom deleter: a tiny object whose only job is "close this handle MY way".
// operator() means "this object can be called like a function".
struct HandleDeleter {
    void operator()(HANDLE h) const {
        // Only close a REAL ticket. Closing a blank/junk one can crash.
        if (h && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};

// Our short, clear nickname for the smart handle type.
// A HANDLE is secretly a void*, so unique_ptr<void, ...> fits it.
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

} // ending namespace gh
