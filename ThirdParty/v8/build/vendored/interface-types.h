// Vendored subset of V8's src/debug/interface-types.h for UnrealJs.
//
// V8 >= 8 removed the public include/interface-types.h; the v8::debug console
// types (used by UnrealConsoleDelegate to route console.* to the UE log) now
// live in the internal src/debug/interface-types.h. This file reproduces only
// the console types + SetConsoleDelegate declaration, with the two internal
// src/base includes replaced by small shims, so ConsoleDelegate.h keeps
// compiling against the public header set. The symbols are provided by the
// statically-linked v8_monolith. Update this if the upstream layout changes.

#ifndef V8_VENDORED_DEBUG_INTERFACE_TYPES_H_
#define V8_VENDORED_DEBUG_INTERFACE_TYPES_H_

#include <cstdint>

#include "v8-function-callback.h"
#include "v8-isolate.h"
#include "v8-local-handle.h"
#include "v8-primitive.h"
#include "v8-internal.h"

// Shim for src/base/logging.h (DCHECK_NOT_NULL) — not shipped in public headers.
#ifndef DCHECK_NOT_NULL
#define DCHECK_NOT_NULL(x) ((void)0)
#endif

namespace v8 {

namespace internal {
class Isolate;
class BuiltinArguments;
}  // namespace internal

namespace debug {

class ConsoleCallArguments {
 public:
  int Length() const { return length_; }
  /**
   * Accessor for the available arguments. Returns `undefined` if the index
   * is out of bounds.
   */
  V8_INLINE v8::Local<v8::Value> operator[](int i) const {
    // values_ points to the first argument.
    if (i < 0 || length_ <= i) return Undefined(GetIsolate());
    DCHECK_NOT_NULL(values_);
    return Local<Value>::FromSlot(values_ + i);
  }

  V8_INLINE v8::Isolate* GetIsolate() const { return isolate_; }

  explicit ConsoleCallArguments(const v8::FunctionCallbackInfo<v8::Value>&);
  explicit ConsoleCallArguments(internal::Isolate* isolate,
                                const internal::BuiltinArguments&);

 private:
  v8::Isolate* isolate_;
  internal::Address* values_;
  int length_;
};

class ConsoleContext {
 public:
  ConsoleContext(int id, v8::Local<v8::String> name) : id_(id), name_(name) {}
  ConsoleContext() : id_(0) {}

  int id() const { return id_; }
  v8::Local<v8::String> name() const { return name_; }

 private:
  int id_;
  v8::Local<v8::String> name_;
};

class ConsoleDelegate {
 public:
  virtual void Debug(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Error(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Info(const ConsoleCallArguments& args,
                    const ConsoleContext& context) {}
  virtual void Log(const ConsoleCallArguments& args,
                   const ConsoleContext& context) {}
  virtual void Warn(const ConsoleCallArguments& args,
                    const ConsoleContext& context) {}
  virtual void Dir(const ConsoleCallArguments& args,
                   const ConsoleContext& context) {}
  virtual void DirXml(const ConsoleCallArguments& args,
                      const ConsoleContext& context) {}
  virtual void Table(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Trace(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Group(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void GroupCollapsed(const ConsoleCallArguments& args,
                              const ConsoleContext& context) {}
  virtual void GroupEnd(const ConsoleCallArguments& args,
                        const ConsoleContext& context) {}
  virtual void Clear(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Count(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void CountReset(const ConsoleCallArguments& args,
                          const ConsoleContext& context) {}
  virtual void Assert(const ConsoleCallArguments& args,
                      const ConsoleContext& context) {}
  virtual void Profile(const ConsoleCallArguments& args,
                       const ConsoleContext& context) {}
  virtual void ProfileEnd(const ConsoleCallArguments& args,
                          const ConsoleContext& context) {}
  virtual void Time(const ConsoleCallArguments& args,
                    const ConsoleContext& context) {}
  virtual void TimeLog(const ConsoleCallArguments& args,
                       const ConsoleContext& context) {}
  virtual void TimeEnd(const ConsoleCallArguments& args,
                       const ConsoleContext& context) {}
  virtual void TimeStamp(const ConsoleCallArguments& args,
                         const ConsoleContext& context) {}
  virtual ~ConsoleDelegate() = default;
};

// Declared in V8's src/debug/debug-interface.h; symbol provided by v8_monolith.
void SetConsoleDelegate(Isolate* isolate, ConsoleDelegate* delegate);

}  // namespace debug
}  // namespace v8

#endif  // V8_VENDORED_DEBUG_INTERFACE_TYPES_H_
