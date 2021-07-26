// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "function/bidding_function.h"

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "google/protobuf/util/json_util.h"
#include "util/status_macros.h"
#include "v8.h"

ABSL_FLAG(::absl::Duration, bidding_function_async_wait, absl::Milliseconds(50),
          "Deadline for waiting for an async bidding function to resolve.");

namespace aviary::function {
namespace {

using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::util::JsonStringToMessage;
using ::google::protobuf::util::MessageToJsonString;

constexpr int kBiddingFunctionWarmUpIterations = 10;
constexpr char kInternalBiddingFunctionName[] = "__GenerateBid_Internal__";
constexpr char kFledgeBiddingFunctionName[] = "generateBid";
constexpr char kFledgeScoreAdFunctionName[] = "scoreAd";

template <typename T>
absl::StatusOr<v8::Local<T>> ToLocalChecked(absl::string_view error_message,
                                            v8::MaybeLocal<T> maybe_value) {
  if (maybe_value.IsEmpty()) {
    return absl::Status(absl::StatusCode::kInternal, error_message);
  }
  return maybe_value.ToLocalChecked();
}

template <typename T>
absl::StatusOr<v8::Local<T>> ToLocalChecked(v8::MaybeLocal<T> maybe_value) {
  return ToLocalChecked("Missing expected V8 value.", maybe_value);
}

absl::StatusOr<v8::Local<v8::String>> NewString(v8::Isolate* isolate,
                                                absl::string_view str) {
  return ToLocalChecked(v8::String::NewFromUtf8(isolate, str.data(),
                                                v8::NewStringType::kNormal,
                                                static_cast<int>(str.size())));
}

template <typename T>
absl::StatusOr<v8::Local<T>> ToLocalTryCatch(
    std::function<v8::MaybeLocal<T>()> get_value, absl::StatusCode code,
    absl::string_view message, v8::Local<v8::Context> context) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::TryCatch try_catch(isolate);
  {
    v8::MaybeLocal<T> maybe_value = get_value();
    if (maybe_value.IsEmpty()) {
      return absl::Status(
          code, absl::StrCat(message,
                             try_catch.Message().IsEmpty()
                                 ? ""
                                 : *v8::String::Utf8Value(
                                       isolate, try_catch.Message()->Get())));
    }
    return maybe_value.ToLocalChecked();
  }
}

absl::Status SetFunctionValue(v8::Local<v8::Value> function_value,
                              v8::Local<v8::Context> context) {
  ASSIGN_OR_RETURN(v8::Local<v8::String> name,
                   ToLocalChecked(v8::String::NewFromUtf8(
                       context->GetIsolate(), kInternalBiddingFunctionName)));
  v8::Maybe<bool> success =
      context->Global()->Set(context, name, function_value);
  if (success.IsNothing() || !success.ToChecked()) {
    return absl::InternalError("Could not set global GenerateBid.");
  }
  return absl::OkStatus();
}

absl::StatusOr<v8::Local<v8::Value>> GetFunctionValue(
    v8::Local<v8::Context> context) {
  v8::Isolate* isolate = context->GetIsolate();
  ASSIGN_OR_RETURN(v8::Local<v8::String> name,
                   ToLocalChecked(v8::String::NewFromUtf8(
                       isolate, kInternalBiddingFunctionName)));
  ASSIGN_OR_RETURN(
      auto function_value,
      ToLocalTryCatch<v8::Value>(
          [&]() -> auto { return context->Global()->Get(context, name); },
          absl::StatusCode::kInternal, "Cannot load the function: ", context));
  if (!function_value->IsFunction()) {
    return ::absl::InternalError("Script did not return a function.");
  }
  return function_value;
}

absl::StatusOr<v8::Local<v8::Value>> WaitForPromise(
    v8::Promise* promise, v8::Local<v8::Context> context) {
  absl::Time start = absl::Now();
  const auto wait_duration = absl::GetFlag(FLAGS_bidding_function_async_wait);
  while (promise->State() == v8::Promise::PromiseState::kPending &&
         absl::Now() - start < wait_duration) {
    context->GetIsolate()->PerformMicrotaskCheckpoint();
  }
  switch (promise->State()) {
    case v8::Promise::PromiseState::kFulfilled:
      return promise->Result();
    case v8::Promise::PromiseState::kRejected:
      return absl::InvalidArgumentError(absl::StrCat(
          "Async javascript function failed: ",
          *v8::String::Utf8Value(context->GetIsolate(), promise->Result())));
    case v8::Promise::PromiseState::kPending:
      return absl::InvalidArgumentError("Async javascript function timed out.");
    default:
      return absl::InternalError(
          absl::StrCat("Unexpected PromiseState: ", promise->State()));
  }
}

template <typename T>
absl::StatusOr<T> ConvertOutput(const v8::Local<v8::Value> function_output,
                                const v8::Local<v8::Context>& context) {
  T converted_output;
  v8::Isolate* isolate = context->GetIsolate();
  ASSIGN_OR_RETURN(
      v8::Local<v8::String> json_string,
      ToLocalChecked("Unable to serialize function output to JSON.",
                     v8::JSON::Stringify(context, function_output)));
  const google::protobuf::util::Status conversion_status = JsonStringToMessage(
      *v8::String::Utf8Value(isolate, json_string), &converted_output);
  if (!conversion_status.ok()) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Unable to convert the bidding function output from JSON: ",
        conversion_status.message().ToString()));
  }
  return converted_output;
}

template <typename Input>
absl::StatusOr<v8::Local<v8::Value>> ConvertArgument(
    const Input& function_input, v8::Local<v8::Context> context) {
  v8::Isolate* isolate = context->GetIsolate();
  std::string json_string;
  if (!MessageToJsonString(function_input, &json_string).ok()) {
    return ::absl::InternalError("Unable to convert a bidding function input.");
  }
  ASSIGN_OR_RETURN(auto json_string_value, NewString(isolate, json_string));
  return ToLocalChecked(v8::JSON::Parse(context, json_string_value));
}

absl::StatusOr<v8::Local<v8::Value>> InvokeFunctionWithJsonInput(
    v8::Local<v8::Context> context,
    std::vector<v8::Local<v8::Value>>& arguments) {
  ASSIGN_OR_RETURN(v8::Local<v8::Value> function_value,
                   GetFunctionValue(context));

  ASSIGN_OR_RETURN(
      auto return_value,
      ToLocalTryCatch<v8::Value>(
          [&]() -> auto {
            return v8::Local<v8::Function>::Cast(function_value)
                ->Call(context, context->Global(),
                       static_cast<int>(arguments.size()), arguments.data());
          },
          absl::StatusCode::kInternal, "Function execution failed: ", context));

  return return_value->IsPromise()
             ? WaitForPromise(v8::Promise::Cast(*return_value), context)
             : return_value;
}

template <typename Input>
absl::StatusOr<v8::Local<v8::Object>> GetMapArgument(
    const Input& input, const FieldDescriptor* field_descriptor,
    v8::Local<v8::Context> context) {
  const auto* reflection = Input::GetReflection();
  std::vector<v8::Local<v8::Name>> field_names;
  std::vector<v8::Local<v8::Value>> field_values;
  const auto* key_field_descriptor =
      field_descriptor->message_type()->map_key();
  const auto* value_field_descriptor =
      field_descriptor->message_type()->map_value();
  for (int map_entry_index = 0;
       map_entry_index < reflection->FieldSize(input, field_descriptor);
       map_entry_index++) {
    const auto& map_entry = reflection->GetRepeatedMessage(
        input, field_descriptor, map_entry_index);
    ASSIGN_OR_RETURN(
        auto field_name,
        NewString(context->GetIsolate(), map_entry.GetReflection()->GetString(
                                             map_entry, key_field_descriptor)));
    ASSIGN_OR_RETURN(auto converted_map_value,
                     ConvertArgument(map_entry.GetReflection()->GetMessage(
                                         map_entry, value_field_descriptor),
                                     context));
    field_names.emplace_back(field_name);
    field_values.push_back(converted_map_value);
  }
  return v8::Object::New(context->GetIsolate(), v8::Null(context->GetIsolate()),
                         field_names.data(), field_values.data(),
                         field_names.size());
}

template <typename Input>
absl::StatusOr<v8::Local<v8::Value>> InvokeFunctionOnce(
    const Input& input, v8::Local<v8::Context> context,
    const FunctionOptions& options) {
  std::vector<v8::Local<v8::Value>> arguments;

  if (options.flatten_function_arguments) {
    const auto* descriptor = Input::GetDescriptor();
    const auto* reflection = Input::GetReflection();
    for (int field_index = 0; field_index < descriptor->field_count();
         field_index++) {
      const auto* field_descriptor = descriptor->field(field_index);
      switch (field_descriptor->type()) {
        case FieldDescriptor::Type::TYPE_MESSAGE: {
          if (field_descriptor->is_map()) {
            ASSIGN_OR_RETURN(auto map_argument,
                             GetMapArgument(input, field_descriptor, context));
            arguments.emplace_back(map_argument);
          } else {
            ASSIGN_OR_RETURN(
                auto converted_argument,
                ConvertArgument(reflection->GetMessage(input, field_descriptor),
                                context));
            arguments.push_back(converted_argument);
          }
        } break;
        case FieldDescriptor::Type::TYPE_DOUBLE: {
          arguments.push_back(
              v8::Number::New(context->GetIsolate(),
                              reflection->GetDouble(input, field_descriptor)));
        } break;
        default:
          return absl::FailedPreconditionError(
              "Only message, map or double arguments are supported");
      }
    }
  } else {
    // Convert bidding function input proto -> json.
    ASSIGN_OR_RETURN(auto converted_argument, ConvertArgument(input, context));
    arguments = {converted_argument};
  }
  return InvokeFunctionWithJsonInput(context, arguments);
}

// TODO: Implement alternative where a function is explicitly initialized;
// remove `IgnoreError()` and have the status of `InvokeFunctionOnce()` returned
// if there is an error.
template <typename Input>
absl::Status WarmUpBiddingFunction(v8::Local<v8::Context> context,
                                   const FunctionOptions& options) {
  Input input;
  // Invoking 10 times during the Create() flow reduced the future Invoke()
  // runtime significantly for a large, complex bidding function.
  for (int i = 0; i < kBiddingFunctionWarmUpIterations; i++) {
    InvokeFunctionOnce(input, context, options).status().IgnoreError();
  }
  return absl::OkStatus();
}
}  // namespace

namespace internal {

IsolateHolder::IsolateHolder(v8::Isolate* isolate) : isolate_(isolate) {}

IsolateHolder::operator v8::Isolate*() { return isolate_; }

IsolateHolder::IsolateHolder(IsolateHolder&& other) {
  isolate_ = other.isolate_;
  other.isolate_ = nullptr;
}

IsolateHolder::~IsolateHolder() {
  if (isolate_ != nullptr) {
    isolate_->Dispose();
    isolate_ = nullptr;
  }
}
}  // namespace internal

template <>
std::string
BiddingFunction<AdScoringFunctionInput,
                AdScoringFunctionOutput>::GetFunctionDeclarationName() {
  return kFledgeScoreAdFunctionName;
}

template <>
std::string BiddingFunction<
    BiddingFunctionInput, BiddingFunctionOutput>::GetFunctionDeclarationName() {
  return kFledgeBiddingFunctionName;
}

template <typename Input, typename Output>
absl::StatusOr<std::unique_ptr<BiddingFunctionInterface<Input, Output>>>
BiddingFunction<Input, Output>::Create(absl::string_view script_source,
                                       const FunctionOptions& options) {
  v8::SnapshotCreator snapshot_creator;
  v8::Isolate* isolate = snapshot_creator.GetIsolate();
  {
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::Context::Scope context_scope(context);
    std::string script_source_str = std::string(script_source);

    ASSIGN_OR_RETURN(v8::Local<v8::String> source,
                     ToLocalChecked("Unable to create a script source string.",
                                    v8::String::NewFromUtf8(
                                        isolate, script_source_str.c_str())));
    ASSIGN_OR_RETURN(
        auto script,
        ToLocalTryCatch<v8::Script>(
            [&]() -> auto { return v8::Script::Compile(context, source); },
            absl::StatusCode::kInvalidArgument,
            "Unable to compile the script: ", context));

    ASSIGN_OR_RETURN(auto function_value,
                     ToLocalTryCatch<v8::Value>(
                         [&]() -> auto { return script->Run(context); },
                         absl::StatusCode::kInvalidArgument,
                         "Cannot run the script: ", context));

    if (!function_value->IsFunction()) {
      std::string function_name =
          BiddingFunction<Input, Output>::GetFunctionDeclarationName();
      ASSIGN_OR_RETURN(
          function_value,
          ToLocalTryCatch<v8::Value>(
              [&]() -> auto {
                return context->Global()->Get(
                    context,
                    v8::String::NewFromUtf8(isolate, function_name.c_str())
                        .ToLocalChecked());
              },
              absl::StatusCode::kInvalidArgument,
              "Cannot get function named according to FLEDGE API conventions: ",
              context));
      if (!function_value->IsFunction()) {
        return ::absl::InvalidArgumentError(
            "Globally-declared object with the expected FLEDGE function name "
            "was not a function.");
      }
    }
    RETURN_IF_ERROR(SetFunctionValue(function_value, context));

    RETURN_IF_ERROR(WarmUpBiddingFunction<Input>(context, options));

    snapshot_creator.SetDefaultContext(context);
  }
  v8::StartupData startup_data = snapshot_creator.CreateBlob(
      v8::SnapshotCreator::FunctionCodeHandling::kKeep);

  // v8::StartupData does not own its data blob. Copy the blob into a regular
  // std::string to avoid memory leaks.
  std::string startup_internal_data(startup_data.data, startup_data.raw_size);
  delete[] startup_data.data;

  return absl::WrapUnique(
      new BiddingFunction(std::move(startup_internal_data), options));
}

template <typename Input, typename Output>
BiddingFunction<Input, Output>::BiddingFunction(
    std::string startup_internal_data, const FunctionOptions& options)
    : options_(options),
      allocator_(v8::ArrayBuffer::Allocator::NewDefaultAllocator()),
      startup_internal_data_(std::move(startup_internal_data)),
      startup_data_{startup_internal_data_.data(),
                    static_cast<int>(startup_internal_data_.size())},
      // Assigns `isolate_` to v8::CreateParams.
      isolate_(v8::Isolate::New([&]() -> auto {
        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = allocator_.get();
        create_params.snapshot_blob = &startup_data_;
        return create_params;
      }())) {}

template <typename Input, typename Output>
absl::StatusOr<std::vector<Output>> BiddingFunction<Input, Output>::BatchInvoke(
    const std::vector<Input>& bidding_function_inputs) const {
  v8::Locker locker(isolate_);
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Local<v8::Context> context = v8::Context::New(isolate_, nullptr);
  v8::Context::Scope context_scope(context);

  std::vector<Output> outputs;
  for (const Input& input : bidding_function_inputs) {
    // If any invocation fails, we immediately return the failing status.
    // As a result, in the case of a failing invocation, no prices are returned
    // at all.
    ASSIGN_OR_RETURN(v8::Local<v8::Value> return_value,
                     InvokeFunctionOnce(input, context, options_));
    ASSIGN_OR_RETURN(auto output, ConvertOutput<Output>(return_value, context));
    outputs.push_back(output);
  }
  return outputs;
}

template class BiddingFunction<BiddingFunctionInput, BiddingFunctionOutput>;
template class BiddingFunction<AdScoringFunctionInput, AdScoringFunctionOutput>;

}  // namespace aviary::function
