// atomic_ref_compat.h
//
// Minimal C++20 std::atomic_ref for Android NDK libc++ builds of V8.
//
// V8 14.6 uses std::atomic_ref throughout src/base and src/heap. The NDK libc++ that Unreal links
// (r27c = libc++ 18, and even V8's bundled r28 snapshot) doesn't ship it, and UnrealJs must build V8
// against the NDK libc++ (use_custom_libcxx=false) so std types match UE across the V8 API.
//
// atomic_ref is a header-only template and never appears in V8's public API, so providing it here
// has no ABI impact. Force-included for the Android target toolchain by Build-V8-Android.sh
// (build/config/android/BUILD.gn patch). Compiles to nothing if the stdlib already has it.

#pragma once

#if defined(__cplusplus)

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <version>

#if !defined(__cpp_lib_atomic_ref)

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct atomic_ref {
    static_assert(is_trivially_copyable_v<_Tp>, "std::atomic_ref<T> requires a trivially copyable T");

    using value_type = _Tp;
    using difference_type = conditional_t<is_pointer_v<_Tp>, ptrdiff_t, _Tp>;

    static constexpr size_t required_alignment = alignof(_Tp) > sizeof(_Tp) ? alignof(_Tp) : sizeof(_Tp);
    static constexpr bool is_always_lock_free = __atomic_always_lock_free(sizeof(_Tp), 0);

    explicit atomic_ref(_Tp& __obj) noexcept : __ptr_(std::addressof(__obj)) {}
    atomic_ref(const atomic_ref&) noexcept = default;
    atomic_ref& operator=(const atomic_ref&) = delete;

    bool is_lock_free() const noexcept { return __atomic_is_lock_free(sizeof(_Tp), __ptr_); }

    void store(_Tp __v, memory_order __m = memory_order_seq_cst) const noexcept {
        __atomic_store(__ptr_, std::addressof(__v), __order(__m));
    }
    _Tp operator=(_Tp __v) const noexcept { store(__v); return __v; }

    _Tp load(memory_order __m = memory_order_seq_cst) const noexcept {
        alignas(_Tp) unsigned char __buf[sizeof(_Tp)];
        _Tp* __r = reinterpret_cast<_Tp*>(__buf);
        __atomic_load(__ptr_, __r, __order(__m));
        return *__r;
    }
    operator _Tp() const noexcept { return load(); }

    _Tp exchange(_Tp __v, memory_order __m = memory_order_seq_cst) const noexcept {
        alignas(_Tp) unsigned char __buf[sizeof(_Tp)];
        _Tp* __r = reinterpret_cast<_Tp*>(__buf);
        __atomic_exchange(__ptr_, std::addressof(__v), __r, __order(__m));
        return *__r;
    }

    bool compare_exchange_weak(_Tp& __e, _Tp __d, memory_order __s, memory_order __f) const noexcept {
        return __atomic_compare_exchange(__ptr_, std::addressof(__e), std::addressof(__d), true, __order(__s), __order(__f));
    }
    bool compare_exchange_strong(_Tp& __e, _Tp __d, memory_order __s, memory_order __f) const noexcept {
        return __atomic_compare_exchange(__ptr_, std::addressof(__e), std::addressof(__d), false, __order(__s), __order(__f));
    }
    bool compare_exchange_weak(_Tp& __e, _Tp __d, memory_order __m = memory_order_seq_cst) const noexcept {
        return compare_exchange_weak(__e, __d, __m, __failure_order(__m));
    }
    bool compare_exchange_strong(_Tp& __e, _Tp __d, memory_order __m = memory_order_seq_cst) const noexcept {
        return compare_exchange_strong(__e, __d, __m, __failure_order(__m));
    }

    // Integral (and pointer add/sub) read-modify-write operations
    _Tp fetch_add(difference_type __v, memory_order __m = memory_order_seq_cst) const noexcept
        requires(is_integral_v<_Tp> || is_pointer_v<_Tp>) {
        return __atomic_fetch_add(__ptr_, __scale(__v), __order(__m));
    }
    _Tp fetch_sub(difference_type __v, memory_order __m = memory_order_seq_cst) const noexcept
        requires(is_integral_v<_Tp> || is_pointer_v<_Tp>) {
        return __atomic_fetch_sub(__ptr_, __scale(__v), __order(__m));
    }
    _Tp fetch_and(_Tp __v, memory_order __m = memory_order_seq_cst) const noexcept requires is_integral_v<_Tp> {
        return __atomic_fetch_and(__ptr_, __v, __order(__m));
    }
    _Tp fetch_or(_Tp __v, memory_order __m = memory_order_seq_cst) const noexcept requires is_integral_v<_Tp> {
        return __atomic_fetch_or(__ptr_, __v, __order(__m));
    }
    _Tp fetch_xor(_Tp __v, memory_order __m = memory_order_seq_cst) const noexcept requires is_integral_v<_Tp> {
        return __atomic_fetch_xor(__ptr_, __v, __order(__m));
    }

private:
    static constexpr int __order(memory_order __m) noexcept { return static_cast<int>(__m); }
    // Failure order for the single-order compare_exchange overloads (can't be release/acq_rel)
    static constexpr memory_order __failure_order(memory_order __m) noexcept {
        return __m == memory_order_acq_rel ? memory_order_acquire
             : __m == memory_order_release ? memory_order_relaxed
             : __m;
    }
    // __atomic_fetch_add on a T* adds bytes, not elements
    static constexpr auto __scale(difference_type __v) noexcept {
        if constexpr (is_pointer_v<_Tp>) {
            return __v * static_cast<ptrdiff_t>(sizeof(remove_pointer_t<_Tp>));
        } else {
            return __v;
        }
    }

    _Tp* __ptr_;
};

template <class _Tp>
atomic_ref(_Tp&) -> atomic_ref<_Tp>;

_LIBCPP_END_NAMESPACE_STD

// Advertise it like the stdlib would: V8 calls simdutf's atomic_* base64 APIs, which simdutf only
// declares when __cpp_lib_atomic_ref >= 201806L (SIMDUTF_ATOMIC_REF).
#define __cpp_lib_atomic_ref 201806L

#endif // !__cpp_lib_atomic_ref
#endif // __cplusplus
