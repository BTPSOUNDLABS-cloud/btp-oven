#include "OnnxBootstrap.h"

#if defined (_WIN32)

 #include <windows.h>

 #ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
  #define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR  0x00000100
 #endif
 #ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
  #define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS  0x00001000
 #endif

namespace
{
    void loadSiblingOnnxRuntime()
    {
        HMODULE self = nullptr;

        if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR> (&loadSiblingOnnxRuntime), &self) == 0)
            return;

        wchar_t path[MAX_PATH] = {};

        if (GetModuleFileNameW (self, path, MAX_PATH) == 0)
            return;

        // Strip the module file name, keeping the trailing separator.
        for (int i = (int) lstrlenW (path) - 1; i >= 0; --i)
        {
            if (path[i] == L'\\' || path[i] == L'/')
            {
                path[i + 1] = 0;
                break;
            }
        }

        wchar_t dll[MAX_PATH] = {};
        lstrcpynW (dll, path, MAX_PATH);
        lstrcatW (dll, L"onnxruntime.dll");

        if (GetFileAttributesW (dll) == INVALID_FILE_ATTRIBUTES)
            return;

        // Absolute path + DLL_LOAD_DIR means its own dependencies also resolve
        // from this folder, not from wherever the host process looks first.
        LoadLibraryExW (dll, nullptr,
                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }

    struct Bootstrap
    {
        Bootstrap() { loadSiblingOnnxRuntime(); }
    };

    // Runs when the module is loaded, before any delayed import is bound.
    Bootstrap bootstrapInstance;
}

namespace btp
{
    void preloadBundledOnnxRuntime()
    {
        loadSiblingOnnxRuntime();
    }
}

#else

namespace btp
{
    void preloadBundledOnnxRuntime() {}
}

#endif
