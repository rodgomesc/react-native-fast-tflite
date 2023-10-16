#include <exception>
#include <fbjni/fbjni.h>
#include <jni.h>
#include <jsi/jsi.h>
#include <memory>

// TODO: Uncomment this when tensorflow-lite C/C++ API can be successfully built/linked here
#include "TensorflowPlugin.h"
#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/CallInvokerHolder.h>
#include <TensorflowPlugin.h>

namespace mrousavy {

using namespace facebook;
using namespace facebook::jni;

// Java Insaller
struct TfliteModule : public jni::JavaClass<TfliteModule> {
public:
  static constexpr auto kJavaDescriptor = "Lcom/tflite/TfliteModule;";

  static jboolean
  nativeInstall(jni::alias_ref<jni::JClass>, jlong runtimePtr,
                jni::alias_ref<react::CallInvokerHolder::javaobject> jsCallInvokerHolder) {
    auto runtime = reinterpret_cast<jsi::Runtime*>(runtimePtr);
    if (runtime == nullptr) {
      // Runtime was null!
      return false;
    }
    auto jsCallInvoker = jsCallInvokerHolder->cthis()->getCallInvoker();

    auto fetchByteDataFromUrl = [](std::string url) {
      static const auto cls = javaClassStatic();
      static const auto method =
          cls->getStaticMethod<jbyteArray(std::string)>("fetchByteDataFromUrl");
      try {
        auto byteData = method(cls, url);
        auto size = byteData->size();
        auto bytes = byteData->getRegion(0, size);
        void* data = malloc(size);
        memcpy(data, bytes.get(), size);
        return Buffer{.data = data, .size = size};
      } catch (const std::exception& e) {
        throw std::runtime_error("Failed to fetch byte data from URL \"" + url + "\"! " + e.what());
      }
    };

    try {
      // TODO: Uncomment this when tensorflow-lite C/C++ API can be successfully built/linked here
      TensorflowPlugin::installToRuntime(*runtime, jsCallInvoker, fetchByteDataFromUrl);

    } catch (std::exception& exc) {
      return false;
    }

    TensorflowPlugin::installToRuntime(*runtime, jsCallInvoker, fetchByteDataFromUrl);
    return true;
  }

  static void registerNatives() {
    javaClassStatic()->registerNatives({
        makeNativeMethod("nativeInstall", TfliteModule::nativeInstall),
    });
  }
};

} // namespace mrousavy

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  return facebook::jni::initialize(vm, [] { mrousavy::TfliteModule::registerNatives(); });
}
