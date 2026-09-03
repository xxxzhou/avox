// MSVC STL ABI compatibility shim
//
// webrtc.lib was compiled with MSVC 14.44+ which introduced
// __std_find_first_of_trivial_pos_1 (returns size_t position).
// MSVC 14.43 only has __std_find_first_of_trivial_1 (returns const void*).
// This shim bridges the gap by implementing the new API in terms of the old one.
//
// When upgrading to MSVC 14.44+, this file can be removed.

#include <cstddef>

extern "C" {

// Old API from MSVC 14.43 CRT (libcpmt.lib)
const void* __stdcall __std_find_first_of_trivial_1(
    const void* _First1, const void* _Last1,
    const void* _First2, const void* _Last2) noexcept;

// New API expected by webrtc.lib (MSVC 14.44+)
__declspec(noalias) size_t __stdcall __std_find_first_of_trivial_pos_1(
    const void* const _Haystack,
    const size_t _Haystack_length,
    const void* const _Needle,
    const size_t _Needle_length) noexcept {

    const auto* const first1 = static_cast<const char*>(_Haystack);
    const auto* const last1 = first1 + _Haystack_length;
    const auto* const first2 = static_cast<const char*>(_Needle);
    const auto* const last2 = first2 + _Needle_length;

    const void* const result = __std_find_first_of_trivial_1(first1, last1, first2, last2);

    if (result == last1) {
        return static_cast<size_t>(-1); // npos
    }
    return static_cast<size_t>(static_cast<const char*>(result) - first1);
}

} // extern "C"
