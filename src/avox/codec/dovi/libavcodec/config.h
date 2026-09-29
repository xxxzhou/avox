// Minimal config.h shim for the vendored FFmpeg Dolby Vision RPU parser.
//
// The FFmpeg binary SDK does not ship its generated config.h, but the parser's
// internal headers (mathops.h / get_bits.h / bitstream.h) read a handful of
// feature macros from it. This shim declares only those, so the vendored subtree
// stays independent of however the host FFmpeg was configured.
//
// Architecture macros are all 0 on purpose: the only thing they gate in these
// vendored files is which arch-specific mathops.h gets included. With them off,
// mathops.h falls through to its generic C implementation, which is what we want
// (the hand-written asm adds nothing for RPU parsing and would drag in more
// internal headers). No ARCH_* is supplied by the build.
#pragma once

#define ARCH_X86 0
#define ARCH_X86_32 0
#define ARCH_X86_64 0
#define ARCH_ARM 0
#define ARCH_AARCH64 0
#define ARCH_MIPS 0
#define ARCH_PPC 0
#define ARCH_RISCV 0
#define ARCH_LOONGARCH 0

// --- x86 inline asm helpers: unused once ARCH_X86 is 0, kept for safety ---
#ifndef HAVE_6REGS
#define HAVE_6REGS 0
#endif
#ifndef HAVE_I686
#define HAVE_I686 0
#endif
#ifndef HAVE_INLINE_ASM
#define HAVE_INLINE_ASM 0
#endif

// --- generic bitstream reader ---
#ifndef HAVE_FAST_64BIT
#define HAVE_FAST_64BIT 1
#endif
#ifndef HAVE_FAST_UNALIGNED
#define HAVE_FAST_UNALIGNED 1
#endif
#ifndef HAVE_BIGENDIAN
#define HAVE_BIGENDIAN 0
#endif
#ifndef CONFIG_SAFE_BITSTREAM_READER
#define CONFIG_SAFE_BITSTREAM_READER 1
#endif
#ifndef CONFIG_SMALL
#define CONFIG_SMALL 0
#endif
