// Inspect tool: dumps the input/output tensor names, types and shapes of the
// RVC ONNX models so the engine can bind them correctly.
#include "onnxruntime_cxx_api.h"

#include <cstdio>

static const char* elemTypeName (ONNXTensorElementDataType t)
{
    switch (t)
    {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:   return "float32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:   return "int64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:   return "int32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:  return "float64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return "float16";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:    return "bool";
        default:                                    return "other";
    }
}

int main (int argc, char** argv)
{
    Ort::Env env (ORT_LOGGING_LEVEL_WARNING, "inspect");
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads (1);
    opts.SetGraphOptimizationLevel (ORT_DISABLE_ALL);

    const char* models[] = {
        "D:/Dev/rvc-models/hubert/vec-768-layer-12.onnx",
        "D:/Dev/rvc-models/voices/GuraTalkV2.onnx",
    };

    auto toWide = [] (const char* s)
    {
        std::wstring w;
        for (; *s; ++s)
            w += (wchar_t) (unsigned char) *s;
        return w;
    };

    for (int m = 0; m < 2; ++m)
    {
        printf ("=== %s ===\n", models[m]);

        try
        {
            Ort::Session session (env, toWide (models[m]).c_str(), opts);
            Ort::AllocatorWithDefaultOptions alloc;

            const size_t nIn  = session.GetInputCount();
            const size_t nOut = session.GetOutputCount();
            printf ("inputs=%zu outputs=%zu\n", nIn, nOut);

            for (size_t i = 0; i < nIn; ++i)
            {
                auto name = session.GetInputNameAllocated (i, alloc);
                auto info = session.GetInputTypeInfo (i);
                auto shapeInfo = info.GetTensorTypeAndShapeInfo();
                auto shape = shapeInfo.GetShape();

                printf ("  in[%zu]  %-24s %-8s [", i, name.get(), elemTypeName (shapeInfo.GetElementType()));
                for (size_t d = 0; d < shape.size(); ++d)
                    printf ("%s%lld", d ? "," : "", (long long) shape[d]);
                printf ("]\n");
            }

            for (size_t i = 0; i < nOut; ++i)
            {
                auto name = session.GetOutputNameAllocated (i, alloc);
                auto info = session.GetOutputTypeInfo (i);
                auto shapeInfo = info.GetTensorTypeAndShapeInfo();
                auto shape = shapeInfo.GetShape();

                printf ("  out[%zu] %-24s %-8s [", i, name.get(), elemTypeName (shapeInfo.GetElementType()));
                for (size_t d = 0; d < shape.size(); ++d)
                    printf ("%s%lld", d ? "," : "", (long long) shape[d]);
                printf ("]\n");
            }
        }
        catch (const Ort::Exception& e)
        {
            printf ("  ERROR: %s\n", e.what());
        }
    }

    return 0;
}
