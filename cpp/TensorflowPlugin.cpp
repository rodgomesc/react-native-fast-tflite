//
//  TensorflowPlugin.m
//  VisionCamera
//
//  Created by Marc Rousavy on 26.06.23.
//  Copyright © 2023 mrousavy. All rights reserved.
//

#include "TensorflowPlugin.h"

#include "TensorHelpers.h"
#include "jsi/Promise.h"
#include "jsi/TypedArray.h"
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

#ifdef ANDROID
#include <tensorflow/lite/c/c_api.h>
#else
#include <TensorFlowLiteC/TensorFlowLiteC.h>

#if FAST_TFLITE_ENABLE_CORE_ML
#include <TensorFlowLiteCCoreML/TensorFlowLiteCCoreML.h>
#endif
#endif

using namespace facebook;
using namespace mrousavy;

void log(std::string string...) {
  // TODO: Figure out how to log to console
}

void TensorflowPlugin::installToRuntime(jsi::Runtime& runtime,
                                        std::shared_ptr<react::CallInvoker> callInvoker,
                                        FetchURLFunc fetchURL) {

  auto func = jsi::Function::createFromHostFunction(
      runtime, jsi::PropNameID::forAscii(runtime, "__loadTensorflowModel"), 1,
      [=](jsi::Runtime& runtime, const jsi::Value& thisValue, const jsi::Value* arguments,
          size_t count) -> jsi::Value {
        auto start = std::chrono::steady_clock::now();
        auto modelPath = arguments[0].asString(runtime).utf8(runtime);

        log("Loading TensorFlow Lite Model from \"%s\"...", modelPath.c_str());

        // TODO: Figure out how to use Metal/CoreML delegates
        Delegate delegateType = Delegate::Default;
        if (count > 1 && arguments[1].isString()) {
          // user passed a custom delegate command
          auto delegate = arguments[1].asString(runtime).utf8(runtime);
          if (delegate == "core-ml") {
            delegateType = Delegate::CoreML;
          } else if (delegate == "metal") {
            delegateType = Delegate::Metal;
          } else {
            delegateType = Delegate::Default;
          }
        }

        auto promise =
            Promise::createPromise(runtime, [=, &runtime](std::shared_ptr<Promise> promise) {
              // Launch async thread
              Buffer buffer = fetchURL(modelPath);
              // Load Model into Tensorflow
              auto model = TfLiteModelCreate(buffer.data, buffer.size);
              std::async(std::launch::async, [=, &runtime]() {
                // Fetch model from URL (JS bundle)

                if (model == nullptr) {
                  callInvoker->invokeAsync([=]() {
                    promise->reject("Failed to load model from \"" + modelPath + "\"!");
                  });
                  return;
                }

                // Create TensorFlow Interpreter
                auto options = TfLiteInterpreterOptionsCreate();

                switch (delegateType) {
                  case Delegate::CoreML: {
#if FAST_TFLITE_ENABLE_CORE_ML
                    TfLiteCoreMlDelegateOptions delegateOptions;
                    auto delegate = TfLiteCoreMlDelegateCreate(&delegateOptions);
                    TfLiteInterpreterOptionsAddDelegate(options, delegate);
                    break;
#else
            callInvoker->invokeAsync([=]() {
              promise->reject("CoreML Delegate is not enabled! Set $EnableCoreMLDelegate to true in Podfile and rebuild.");
            });
            return;
#endif
                  }
                  case Delegate::Metal: {
                    callInvoker->invokeAsync(
                        [=]() { promise->reject("Metal Delegate is not supported!"); });
                    return;
                  }
                  default: {
                    // use default CPU delegate.
                  }
                }

                auto interpreter = TfLiteInterpreterCreate(model, options);

                if (interpreter == nullptr) {
                  callInvoker->invokeAsync([=]() {
                    promise->reject("Failed to create TFLite interpreter from model \"" +
                                    modelPath + "\"!");
                  });
                  return;
                }

                // Initialize Model and allocate memory buffers
                auto plugin = std::make_shared<TensorflowPlugin>(interpreter, buffer, delegateType,
                                                                 callInvoker);

                callInvoker->invokeAsync([=, &runtime]() {
                  auto result = jsi::Object::createFromHostObject(runtime, plugin);
                  promise->resolve(std::move(result));
                });

                auto end = std::chrono::steady_clock::now();
                log("Successfully loaded Tensorflow Model in %i ms!",
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
              });
            });
        return promise;
      });

  runtime.global().setProperty(runtime, "__loadTensorflowModel", func);
}

TensorflowPlugin::TensorflowPlugin(TfLiteInterpreter* interpreter, Buffer model, Delegate delegate,
                                   std::shared_ptr<react::CallInvoker> callInvoker)
    : _interpreter(interpreter), _delegate(delegate), _model(model), _callInvoker(callInvoker) {
  // Allocate memory for the model's input/output `TFLTensor`s.
  TfLiteStatus status = TfLiteInterpreterAllocateTensors(_interpreter);
  if (status != kTfLiteOk) {
    throw std::runtime_error("Failed to allocate memory for input/output tensors! Status: " +
                             std::to_string(status));
  }

  log("Successfully created Tensorflow Plugin!");
}

TensorflowPlugin::~TensorflowPlugin() {
  if (_model.data != nullptr) {
    free(_model.data);
    _model.data = nullptr;
    _model.size = 0;
  }
  if (_interpreter != nullptr) {
    TfLiteInterpreterDelete(_interpreter);
    _interpreter = nullptr;
  }
}

std::shared_ptr<TypedArrayBase>
TensorflowPlugin::getOutputArrayForTensor(jsi::Runtime& runtime, const TfLiteTensor* tensor) {
  auto name = std::string(TfLiteTensorName(tensor));
  if (_outputBuffers.find(name) == _outputBuffers.end()) {
    _outputBuffers[name] =
        std::make_shared<TypedArrayBase>(TensorHelpers::createJSBufferForTensor(runtime, tensor));
  }
  return _outputBuffers[name];
}

void TensorflowPlugin::copyInputBuffers(jsi::Runtime& runtime, jsi::Object inputValues) {
  // Input has to be array in input tensor size
  auto array = inputValues.asArray(runtime);
  size_t count = array.size(runtime);
  if (count != TfLiteInterpreterGetInputTensorCount(_interpreter)) {
    throw std::runtime_error(
        "TFLite: Input Values have different size than there are input tensors!");
  }

  for (size_t i = 0; i < count; i++) {
    TfLiteTensor* tensor = TfLiteInterpreterGetInputTensor(_interpreter, i);
    auto value = array.getValueAtIndex(runtime, i);
    auto inputBuffer = getTypedArray(runtime, value.asObject(runtime));
    TensorHelpers::updateTensorFromJSBuffer(runtime, tensor, inputBuffer);
  }
}

jsi::Value TensorflowPlugin::copyOutputBuffers(jsi::Runtime& runtime) {
  // Copy output to result process the inference results.
  int outputTensorsCount = TfLiteInterpreterGetOutputTensorCount(_interpreter);
  jsi::Array result(runtime, outputTensorsCount);
  for (size_t i = 0; i < outputTensorsCount; i++) {
    const TfLiteTensor* outputTensor = TfLiteInterpreterGetOutputTensor(_interpreter, i);
    auto outputBuffer = getOutputArrayForTensor(runtime, outputTensor);
    TensorHelpers::updateJSBufferFromTensor(runtime, *outputBuffer, outputTensor);
    result.setValueAtIndex(runtime, i, *outputBuffer);
  }
  return result;
}

void TensorflowPlugin::run() {
  // Run Model
  TfLiteStatus status = TfLiteInterpreterInvoke(_interpreter);
  if (status != kTfLiteOk) {
    throw std::runtime_error("Failed to run TFLite Model! Status: " + std::to_string(status));
  }
}

jsi::Value TensorflowPlugin::get(jsi::Runtime& runtime, const jsi::PropNameID& propNameId) {
  auto propName = propNameId.utf8(runtime);

  if (propName == "runSync") {
    return jsi::Function::createFromHostFunction(
        runtime, jsi::PropNameID::forAscii(runtime, "runModel"), 1,
        [=](jsi::Runtime& runtime, const jsi::Value& thisValue, const jsi::Value* arguments,
            size_t count) -> jsi::Value {
          // 1.
          copyInputBuffers(runtime, arguments[0].asObject(runtime));
          // 2.
          this->run();
          // 3.
          return copyOutputBuffers(runtime);
        });
  } else if (propName == "run") {
    return jsi::Function::createFromHostFunction(
        runtime, jsi::PropNameID::forAscii(runtime, "runModel"), 1,
        [=](jsi::Runtime& runtime, const jsi::Value& thisValue, const jsi::Value* arguments,
            size_t count) -> jsi::Value {
          // 1.
          copyInputBuffers(runtime, arguments[0].asObject(runtime));
          auto promise =
              Promise::createPromise(runtime, [=, &runtime](std::shared_ptr<Promise> promise) {
                std::async(std::launch::async, [=, &runtime]() {
                  // 2.
                  try {
                    this->run();

                    this->_callInvoker->invokeAsync([=, &runtime]() {
                      // 3.
                      auto result = this->copyOutputBuffers(runtime);
                      promise->resolve(std::move(result));
                    });
                  } catch (std::runtime_error error) {
                    promise->reject(error.what());
                  }
                });
              });
          return promise;
        });
  } else if (propName == "inputs") {
    int size = TfLiteInterpreterGetInputTensorCount(_interpreter);
    jsi::Array tensors(runtime, size);
    for (size_t i = 0; i < size; i++) {
      TfLiteTensor* tensor = TfLiteInterpreterGetInputTensor(_interpreter, i);
      if (tensor == nullptr) {
        throw jsi::JSError(runtime, "Failed to get input tensor " + std::to_string(i) + "!");
      }

      jsi::Object object = TensorHelpers::tensorToJSObject(runtime, tensor);
      tensors.setValueAtIndex(runtime, i, object);
    }
    return tensors;
  } else if (propName == "outputs") {
    int size = TfLiteInterpreterGetOutputTensorCount(_interpreter);
    jsi::Array tensors(runtime, size);
    for (size_t i = 0; i < size; i++) {
      const TfLiteTensor* tensor = TfLiteInterpreterGetOutputTensor(_interpreter, i);
      if (tensor == nullptr) {
        throw jsi::JSError(runtime, "Failed to get output tensor " + std::to_string(i) + "!");
      }

      jsi::Object object = TensorHelpers::tensorToJSObject(runtime, tensor);
      tensors.setValueAtIndex(runtime, i, object);
    }
    return tensors;
  } else if (propName == "delegate") {
    switch (_delegate) {
      case Delegate::Default:
        return jsi::String::createFromUtf8(runtime, "default");
      case Delegate::CoreML:
        return jsi::String::createFromUtf8(runtime, "core-ml");
      case Delegate::Metal:
        return jsi::String::createFromUtf8(runtime, "metal");
    }
  }

  return jsi::HostObject::get(runtime, propNameId);
}

std::vector<jsi::PropNameID> TensorflowPlugin::getPropertyNames(jsi::Runtime& runtime) {
  std::vector<jsi::PropNameID> result;
  result.push_back(jsi::PropNameID::forAscii(runtime, "run"));
  result.push_back(jsi::PropNameID::forAscii(runtime, "runSync"));
  result.push_back(jsi::PropNameID::forAscii(runtime, "inputs"));
  result.push_back(jsi::PropNameID::forAscii(runtime, "outputs"));
  result.push_back(jsi::PropNameID::forAscii(runtime, "delegate"));
  return result;
}
