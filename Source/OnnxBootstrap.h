#pragma once

/**
    Bind ONNX Runtime to the DLL that ships beside this module.

    Windows resolves an import-table DLL from the *host process* search order:
    application folder, then System32, then PATH. FL Studio has no
    onnxruntime.dll of its own, so a stock Windows copy in System32 wins --
    and that copy is older than the SDK this plugin is built against. The
    C++ wrapper then asks for an API version the old DLL does not provide and
    crashes on the first Ort::Env.

    The plugin is linked with /DELAYLOAD:onnxruntime.dll, so this runs before
    any ONNX symbol is bound and loads the sibling copy by absolute path.
    Once loaded, the delay-load helper reuses it.
*/
namespace btp
{
    void preloadBundledOnnxRuntime();
}
