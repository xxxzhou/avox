zhouxin@DESKTOP-1QLM5EA:~/work/webrtc/src$ gn args --list /home/zhouxin/work/webrtc/build/android/debug
WARNING at build arg file (use "gn args <out_dir>" to edit):9:22: Build argument has no effect.
use_libcxx_modules = false
                     ^----
The variable "use_libcxx_modules" was set as a build argument
but never appeared in a declare_args() block in any buildfile.

To view all possible args, run "gn args --list <out_dir>"

The build continued as if that argument was unspecified.

absl_build_tests
    Current value (from the default) = false
      From //third_party/abseil-cpp/absl.gni:23

action_pool_depth
    Current value (from the default) = -1
      From //build/toolchain/BUILD.gn:10

    Pool for non remote tasks.

added_rust_stdlib_libs
    Current value (from the default) = []
      From //build/config/rust.gni:82

    Any extra std rlibs in your Rust toolchain, relative to the standard
    Rust toolchain. Typically used with 'rust_sysroot_absolute'

android_channel
    Current value (from the default) = "default"
      From //build/config/android/channel.gni:8

    The channel to build on Android: stable, beta, dev, canary, work, or
    default. "default" should be used on non-official builds.

android_custom_env
    Current value (from the default) = ""
      From //build/config/android/config.gni:105

    Embed a wrap.sh file within apks to set custom environment variables.
    Use a comma space-separated list.
    E.g.: LD_PRELOAD=foo:bar LD_DEBUG=statistics

android_default_version_code
    Current value (from the default) = "1"
      From //build/config/android/config.gni:245

    Android versionCode for android_apk()s that don't explicitly set one.

android_default_version_name
    Current value (from the default) = "Developer Build"
      From //build/config/android/config.gni:248

    Android versionName for android_apk()s that don't explicitly set one.

android_full_debug
    Current value = true
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:6
    Overridden from the default = false
      From //build/config/compiler/BUILD.gn:57

    Normally, Android builds are lightly optimized, even for debug builds, to
    keep binary size down. Setting this flag to true disables such optimization

android_keystore_name
    Current value (from the default) = "chromiumdebugkey"
      From //build/config/android/config.gni:260

    The name of the keystore to use for signing builds.

android_keystore_password
    Current value (from the default) = "chromium"
      From //build/config/android/config.gni:263

    The password for the keystore to use for signing builds.

android_keystore_path
    Current value (from the default) = "//build/android/chromium-debug.keystore"
      From //build/config/android/config.gni:257

    The path to the keystore to use for signing builds.

android_libcpp_lib_dir
    Current value (from the default) = ""
      From //build/config/android/config.gni:242

    Libc++ library directory. Override to use a custom libc++ binary.

android_ndk_api_level
    Current value = 23
      From //.gn:58
    Overridden from the default = 28
      From //build/config/android/config.gni:227

android_ndk_root
    Current value (from the default) = "//third_party/android_toolchain/ndk"
      From //build/config/android/config.gni:224

android_ndk_version
    Current value (from the default) = "r28"
      From //build/config/android/config.gni:225

android_override_version_code
    Current value (from the default) = ""
      From //build/config/android/config.gni:251

    Forced Android versionCode

android_override_version_name
    Current value (from the default) = ""
      From //build/config/android/config.gni:254

    Forced Android versionName

android_sdk_build_tools_version
    Current value (from the default) = "36.0.0"
      From //build/config/android/config.gni:235

android_sdk_platform_version
    Current value (from the default) = "36"
      From //build/config/android/config.gni:236

android_sdk_release
    Current value (from the default) = "b"
      From //build/config/android/config.gni:162

    Which Android SDK to use.

android_sdk_root
    Current value (from the default) = "//third_party/android_sdk/public"
      From //build/config/android/config.gni:234

android_sdk_tools_bundle_aapt2_dir
    Current value (from the default) = "//third_party/android_build_tools/aapt2/cipd"
      From //build/config/android/config.gni:281

android_static_analysis
    Current value = "off"
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:12
    Overridden from the default = "default"
      From //build/config/android/config.gni:100

    Static analysis can be either "on" or "off" or "build_server". This
    controls how android lint, error-prone, bytecode checks are run. This
    needs to be in a separate declare_args as it determines some of the args
    in the main declare_args block below.
    "build_server" (default)
        Runs static analysis jobs via a daemon process and does not block
        on the results. Does not work on bots (requires autoninja.py).
        See: https://chromium.googlesource.com/chromium/src/+/main/build/android/docs/static_analysis.md#autoninja-integration
    "on"
        Runs static analysis as build steps, blocking until they complete.
        This mode will fail builds when analysis steps fail, unless
        treat_warnings_as_errors=false is set.
    "off"
        Disables static analysis. Many bots set this so as to not duplicate
        the same analysis steps as other bots.

android_unstripped_runtime_outputs
    Current value (from the default) = true
      From //build/toolchain/android/BUILD.gn:19

    Whether unstripped binaries, i.e. compiled with debug symbols, should be
    considered runtime_deps rather than stripped ones.

apm_debug_dump
    Current value (from the default) = false
      From //webrtc.gni:113

    Selects whether debug dumps for the audio processing module
    should be generated.

apple_mobile_app_bundle_id_suffix
    Current value (from the default) = ".dev"
      From //build/config/apple/mobile_config.gni:46

    Suffix for CFBundleIdentifier property of iOS Chrome signed bundles
    (main bundle and extensions). Code signing will fail if no mobile
    provisioning for the selected code signing identify support that suffix.
    For extension, the suffix will be added before the extension identifier.
    The suffix is not added to test applications.
    No dot is added before the suffix, so add one if needed.

archive_seed_corpus
    Current value (from the default) = true
      From //build/config/sanitizers/sanitizers.gni:121

    When true, seed corpora archives are built.

arm_control_flow_integrity
    Current value (from the default) = "standard"
      From //build/config/arm.gni:138

    Enable PAC and BTI on AArch64 Linux/Android systems.
    target_cpu == "arm64" filters out some cases (e.g. the ChromeOS x64
    MSAN build) where the target platform is x64, but V8 is configured to
    use the arm64 simulator.

auto_profile_path
    Current value (from the default) = ""
      From //build/config/compiler/BUILD.gn:79

    AFDO (Automatic Feedback Directed Optimizer) is a form of profile-guided
    optimization that GCC supports. It used by ChromeOS in their official
    builds. To use it, set auto_profile_path to the path to a file containing
    the needed gcov profiling data.

branding_file_path
    Current value (from the default) = "//chrome/app/theme/chromium/BRANDING"
      From //build/config/chrome_build.gni:102

    The path to the BRANDING file in chrome/app/theme.

branding_path_component
    Current value (from the default) = "chromium"
      From //build/config/chrome_build.gni:95

branding_path_product
    Current value (from the default) = "chromium"
      From //build/config/chrome_build.gni:96

build_hwasan_splits
    Current value (from the default) = false
      From //build/config/android/abi.gni:27

    Build additional browser splits with HWASAN instrumentation enabled.

build_libsrtp_tests
    Current value (from the default) = false
      From //third_party/libsrtp/BUILD.gn:11

    Tests may not be appropriate for some build environments, e.g. Windows.
    Rather than enumerate valid options, we just let clients ask for them.

build_mojo_proxy
    Current value (from the default) = true
      From //build/config/chromeos/args.gni:43

    Build Mojo Proxy binary, to be used as a IPCZ <=> Mojo Core translation layer.

build_tflite_with_nnapi
    Current value (from the default) = false
      From //third_party/tflite/features.gni:20

    This enables building TFLite's NNAPI delegate, currently experimental.

build_tflite_with_opencl
    Current value (from the default) = false
      From //third_party/tflite/features.gni:24

    This enables building TFLite's GPU delegate with OpenCL, currently
    experimental.

build_tflite_with_ruy
    Current value (from the default) = true
      From //third_party/tflite/features.gni:17

    Turns on TFLITE_WITH_RUY, using ruy as the gemm backend instead of gemmlowp.

build_tflite_with_xnnpack
    Current value (from the default) = true
      From //third_party/tflite/features.gni:14

    This enables building TFLite with XNNPACK.

build_with_mediapipe_lib
    Current value (from the default) = false
      From //third_party/mediapipe/features.gni:11

    This should only be changed in a local args.gn file for now. This library is
    not ready to be built into Chromium yet.
   
    MediaPipe support is under development, but should work on Linux and Windows
    Intel chips.

build_with_mozilla
    Current value (from the default) = false
      From //webrtc.gni:137

    Enable to use the Mozilla internal settings.

cc_wrapper
    Current value (from the default) = ""
      From //build/toolchain/cc_wrapper.gni:33

    Set to "ccache", "sccache", "icecc" or "distcc".

chrome_orderfile_path
    Current value (from the default) = ""
      From //build/config/compiler/BUILD.gn:225

chrome_pgo_phase
    Current value (from the default) = 0
      From //build/config/compiler/pgo/pgo.gni:26

    Specify the current PGO phase.
    Here's the different values that can be used:
        0 : Means that PGO is turned off.
        1 : Used during the PGI (instrumentation) phase.
        2 : Used during the PGO (optimization) phase.
    PGO profiles are generated from `dcheck_always_on = false` builds. Mixing
    those profiles with `dcheck_always_on = true` builds can cause the compiler
    to think some code is hotter than it actually is, potentially causing very
    bad compile times.

chromeos_afdo_platform
    Current value (from the default) = "atom"
      From //build/config/compiler/BUILD.gn:104

    This configuration is used to select a default profile in Chrome OS based on
    the microarchitectures we are using. This is only used if
    clang_use_default_sample_profile is true and clang_sample_profile_path is
    empty.

chromeos_is_browser_only
    Current value (from the default) = false
      From //build/config/chromeos/ui_mode.gni:8

clang_base_path
    Current value (from the default) = "//third_party/llvm-build/Release+Asserts"
      From //build/config/clang/clang.gni:75

clang_diagnostic_dir
    Current value (from the default) = "../../../src/out/clang-crashreports"
      From //build/config/compiler/compiler.gni:126

    Where to redirect clang crash diagnoses

clang_embed_bitcode
    Current value (from the default) = false
      From //build/config/clang/clang.gni:79

    Specifies whether or not bitcode should be embedded during compilation.
    This is used for creating a MLGO corpus from Chromium in the non-ThinLTO case.

clang_emit_debug_info_for_profiling
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:108

    Emit debug information for profiling wile building with clang.
    Only enable this for ChromeOS official builds for AFDO.

clang_sample_profile_path
    Current value (from the default) = ""
      From //build/config/compiler/BUILD.gn:87

    Path to an AFDO profile to use while building with clang, if any. Empty
    implies none.

clang_unsafe_buffers_paths
    Current value (from the default) = ""
      From //build/config/BUILDCONFIG.gn:183

    Unsafe buffers. Location of file used by plugins to track portions of
    the codebase which have been made manifestly safe.

clang_use_chrome_plugins
    Current value (from the default) = true
      From //build/config/clang/clang.gni:53

    Indicates if the build should use the Chrome-specific plugins for enforcing
    coding guidelines, etc. Only used when compiling with Chromium's Clang.
    Setting this enables all of the functionality of the plugin.

clang_use_default_sample_profile
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:96

clang_use_raw_ptr_plugin
    Current value (from the default) = false
      From //build/config/clang/clang.gni:57

    Use this instead of clang_use_chrome_plugins to enable just the raw-ptr
    functionality of the plugin.

clang_use_unsafe_buffers_plugin
    Current value (from the default) = false
      From //build/config/clang/clang.gni:73

    Use this instead of clang_use_chrome_plugins to enable just the unsafe
    buffers functionality of the plugin

clang_version
    Current value (from the default) = "22"
      From //build/toolchain/toolchain.gni:44

clang_warning_suppression_file
    Current value (from the default) = ""
      From //build/config/BUILDCONFIG.gn:184

compiler_timing
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:111

    Turn this on to have the compiler output extra timing information.

compute_inputs_for_analyze
    Current value (from the default) = false
      From //build/config/compute_inputs_for_analyze.gni:13

    Enable this flag when running "gn analyze".
   
    This causes some gn actions to compute inputs immediately (via exec_script)
    where they would normally compute them only when executed (and write them to
    a depfile).
   
    This flag will slow down GN, but is required for analyze to work properly.

concurrent_links
    Current value (from the default) = -1
      From //build/toolchain/concurrent_links.gni:23

    Limit the number of concurrent links; we often want to run fewer
    links at once than we do compiles, because linking is memory-intensive.
    The default to use varies by platform and by the amount of memory
    available, so we call out to a script to get the right value.

coverage_instrumentation_input_file
    Current value (from the default) = ""
      From //build/config/coverage/coverage.gni:36

    The path to the coverage instrumentation input file should be a source root
    absolute path (e.g. //out/Release/coverage_instrumentation_input.txt), and
    the file consists of multiple lines where each line represents a path to a
    source file, and the paths must be relative to the root build directory.
    e.g. ../../base/task/post_task.cc for build directory 'out/Release'.
   
    NOTE that this arg will be non-op if use_clang_coverage is false.

cros_board
    Current value (from the default) = ""
      From //build/config/chromeos/args.gni:8

    This is used only by Simple Chrome to bind its value to test-runner scripts
    generated at build-time.

cros_sdk_version
    Current value (from the default) = ""
      From //build/config/chromeos/args.gni:12

    Similar to cros_board above, this used only by test-runner scripts in
    Simple Chrome.

current_cpu
    Current value (from the default) = ""
      (Internally set; try `gn help current_cpu`.)

current_os
    Current value (from the default) = ""
      (Internally set; try `gn help current_os`.)

custom_toolchain
    Current value (from the default) = ""
      From //build/config/BUILDCONFIG.gn:144

    Allows the path to a custom target toolchain to be injected as a single
    argument, and set as the default toolchain.

dcheck_always_on
    Current value (from the default) = false
      From //build/config/dcheck_always_on.gni:25

dcheck_is_configurable
    Current value (from the default) = false
      From //build/config/dcheck_always_on.gni:14

    Enables DCHECKs to be built-in, but to default to being non-fatal/log-only.
    DCHECKS can then be set as fatal/non-fatal via the "DcheckIsFatal" feature.
    See https://bit.ly/dcheck-albatross for details on how this is used.

debuggable_apks
    Current value (from the default) = true
      From //build/config/android/config.gni:266

    Mark APKs as android:debuggable="true".

default_min_sdk_version
    Current value = 28
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:2
    Overridden from the default = 29
      From //build/config/android/config.gni:78

    The default to use for android:minSdkVersion for targets that do
    not explicitly set it.

default_target_sdk_version
    Current value (from the default) = "36"
      From //build/config/android/config.gni:350

    Default value for targetSdkVersion for APK and bundle targets.

devtools_grd_location
    Current value (from the default) = ""
      From //build/config/devtools.gni:28

devtools_instrumentation_dumping
    Current value (from the default) = false
      From //build/config/android/abi.gni:24

    Only effective if use_order_profiling = true. When this is true,
    instrumentation switches from startup profiling after a delay, and
    then waits for a devtools memory dump request to dump all
    profiling information. When false, the same delay is used to switch from
    startup, and then after a second delay all profiling information is dumped.
    See base::android::orderfile::StartDelayedDump for more information.

devtools_location
    Current value (from the default) = ""
      From //build/config/devtools.gni:26

    DevTools is building a standalone version

devtools_root_location
    Current value (from the default) = ""
      From //build/config/devtools.gni:27

diagnostics_print_source_range_info
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:63

    How clang should report warnings, either false to report in the usual
    manner, or true to add source range information to the diagnostics in
    a form suitable for subsequent processing with scripts. With GCC, or
    on Windows, the warning format is not affected.

disable_android_lint
    Current value (from the default) = true
      From //build/config/android/config.gni:276

    Turns off android lint.

disable_libfuzzer
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:99

    Helper variable for testing builds with disabled libfuzzer.
    Not for client use.

disable_unknown_warning_option
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:57

    Ignore unknown warning flags when using an older Clang version.

enable_android_secondary_abi
    Current value (from the default) = false
      From //build/config/android/abi.gni:33

    Enable (webview) APKs that support multiple architectures. Generally
    needed only for release builds or for webview testing. Slows down "gn gen"
    and ninja parse time due to having to write rules for most native targets
    a second time. Applicable only when target_cpu is 64-bit.

enable_arsc_obfuscation
    Current value (from the default) = true
      From //build/config/android/config.gni:294

    Controls whether |short_resource_paths| and |strip_resource_names| are
    respected. Useful when trying to analyze APKs using tools that do not
    support mapping these names.

enable_baseline_profiles
    Current value (from the default) = false
      From //build/config/android/config.gni:305

    Controls whether specifying |art_profile_path| automatically adds a binary
    baseline profile to the APK/AAB.
    Currently disabled while bundletool does not support baseline profiles in
    non-base splits.

enable_call_graph_profile_sort
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:264

enable_cast_audio_renderer
    Current value (from the default) = false
      From //build/config/cast.gni:24

    True to enable the cast audio renderer.
   
    TODO(crbug.com/1293520): Remove this buildflag.

enable_cast_receiver
    Current value (from the default) = false
      From //build/config/cast.gni:42

    Set this true for a Chromecast build. Chromecast builds are supported on
    Linux, Android, ChromeOS, and Fuchsia.

enable_cast_renderer
    Current value (from the default) = false
      From //build/config/cast.gni:51

enable_cet_shadow_stack
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:133

    Mark binaries as compatible with Shadow Stack of Control-flow Enforcement
    Technology (CET). If Windows version and hardware supports the feature and
    it's enabled by OS then additional validation of return address will be
    performed as mitigation against Return-oriented programming (ROP).
    https://chromium.googlesource.com/chromium/src/+/main/docs/design/sandbox.md#cet-shadow-stack

enable_check_raw_ptr_fields
    Current value (from the default) = false
      From //build/config/clang/clang.gni:60

enable_check_raw_ref_fields
    Current value (from the default) = false
      From //build/config/clang/clang.gni:67

enable_chrome_android_internal
    Current value (from the default) = false
      From //build/config/android/config.gni:74

enable_chromium_prelude
    Current value = true
      From //.gn:90
    Overridden from the default = false
      From //build/config/rust.gni:48

    The chromium prelude crate provides the `chromium::import!` macro which
    is needed to depend on first-party rust libraries. Third-party libraries
    are specified with cargo_crate and do not get imported through this macro.
   
    The macro requires //third_party/rust for syn, quote, and proc_macro2.
    Downstream projects that want to use //build for the rust GN templates but
    don't want to enable the chromium prelude can disable it here, and should
    specify a globally unique `crate_name` in their rust library GN rules
    instead. Note that using a `crate_name` is strongly discouraged inside
    Chromium, and is also discouraged for downstream projects when possible.
   
    We do not support disabling this flag in Chromium code.

enable_dsyms
    Current value (from the default) = false
      From //build/config/apple/symbols.gni:17

    Produce dSYM files for targets that are configured to do so. dSYM
    generation is controlled globally as it is a linker output (produced via
    the //build/toolchain/apple/linker_driver.py. Enabling this will result in
    all shared library, loadable module, and executable targets having a dSYM
    generated.

enable_expensive_dchecks
    Current value (from the default) = true
      From //build/config/dcheck_always_on.gni:33

enable_freetype
    Current value (from the default) = false
      From //build/config/freetype/freetype.gni:29

enable_full_stack_frames_for_profiling
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:64

    Compile in such a way as to make it possible for the profiler to unwind full
    stack frames. Setting this flag has a large effect on the performance of the
    generated code than just setting profiling, but gives the profiler more
    information to analyze.
    Requires profiling to be set to true.

enable_fuzztest_fuzz
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:169

enable_incremental_d8
    Current value (from the default) = true
      From //build/config/android/config.gni:356

    Reduce build time by using d8 incremental build.

enable_iterator_debugging
    Current value (from the default) = false
      From //build/config/c++/c++.gni:44

    When set, enables libc++ debug mode with iterator debugging.
   
    Iterator debugging is generally useful for catching bugs. But it can
    introduce extra locking to check the state of an iterator against the state
    of the current object. For iterator- and thread-heavy code, this can
    significantly slow execution - two orders of magnitude slowdown has been
    seen (crbug.com/903553) and iterator debugging also slows builds by making
    generation of snapshot_blob.bin take ~40-60 s longer. Therefore this
    defaults to off.

enable_java_asserts
    Current value (from the default) = true
      From //build/config/android/config.gni:353

    Whether java assertions and Preconditions checks are enabled.

enable_java_location_rewrite
    Current value (from the default) = false
      From //build/config/android/config.gni:328

    Enables rewriting of certain method invocations with overloads that take an
    additional argument of type org.chromium.base.task.Location (see
    //build/android/location_rewriter)

enable_javaless_renderers
    Current value (from the default) = true
      From //build/config/android/config.gni:22

    Component build breaks javaless renderers - see
    go/javaless-renderers-component-build

enable_jni_multiplexing
    Current value = false
      From //.gn:82
    Overridden from the default = false
      From //third_party/jni_zero/jni_zero.gni:21

enable_js_protobuf
    Current value = false
      From //.gn:93
    Overridden from the default = true
      From //third_party/protobuf/proto_library.gni:148

    Allows subprojects to omit javascript dependencies (e.g.) closure_compiler
    and google-closure-library.

enable_kythe_annotations
    Current value (from the default) = false
      From //build/toolchain/kythe.gni:10

    Enables Kythe annotations necessary to build cross references.

enable_libaom
    Current value = true
      From //.gn:64
    Overridden from the default = true
      From //third_party/libaom/options.gni:7

    Enable encoding AV1 video files.

enable_modular_updater
    Current value (from the default) = false
      From //build/config/cast.gni:19

    Set true to enable modular_updater.

enable_new_standalone_webview_settings
    Current value (from the default) = true
      From //build/config/chrome_build.gni:48

    Set to true to enable new logic for setting the package name and version
    code for standalone WebView targets.

enable_opengl_apitrace
    Current value (from the default) = false
      From //build/config/ozone.gni:33

    Enable explicit apitrace (https://apitrace.github.io) loading.
    This requires apitrace library with additional bindings.
    See ChromeOS package for details:
    https://chromium-review.googlesource.com/c/chromiumos/overlays/chromiumos-overlay/+/2659419
    Chrome will not start without an apitrace.so library.
    Trace will be saved to /tmp/gltrace.dat file by default. You can
    override it at run time with TRACE_FILE=<path> environment variable.

enable_perfetto_android_java_sdk
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:216

enable_perfetto_benchmarks
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:218

enable_perfetto_etm_importer
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:389

enable_perfetto_fuzzers
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:221

enable_perfetto_grpc
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:383

    Enables gRPC in the Perfetto codebase. gRPC significantly increases build
    times and the general footprint of Perfetto. As it only required for
    BigTrace and even then only to build the final ready-to-ship binary, don't
    enable this by default.

enable_perfetto_heapprofd
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:177

enable_perfetto_integration_tests
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:213

enable_perfetto_ipc
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:170

enable_perfetto_llvm_demangle
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:377

enable_perfetto_llvm_symbolizer
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:393

    Enables the use of the LLVM symbolizer in trace_processor.

enable_perfetto_lockfree_taskrunner
    Current value (from the default) = true
      From //third_party/perfetto/gn/perfetto.gni:294

    This flag is used for the migration of UnixTaskRunner -> LockFreeTaskRunner.
    It determines whether MaybeLockFreeTaskRunner is backed by UnixTaskRunner
    or the newer LockFreeTaskRunner.
    UnixTaskRunner is the battle-tested original TaskRunner implementaiton used
    from 2017 -> 2025. LockFreeTaskRunner is the new improved TaskRunner which
    is planning to sunset UnixTaskRunner.
    Note that on Android platform (non-standalone) builds this flag is ignored
    and the Android flag "use_lockfree_taskrunner" is used instead (see
    perfetto_flags.aconfig)

enable_perfetto_merged_protos_check
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:421

    Check that the merged perfetto_trace.proto can be translated to a C++ lite
    proto and compiled. This is disabled by default because it's expensive (it
    can take a couple of minutes).

enable_perfetto_platform_services
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:161

enable_perfetto_rt_mutex
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:282

enable_perfetto_site
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:416

    Allows to build the perfetto.dev website.
    WARNING: if this flag is enabled, the build performs globbing at generation
    time. Incremental builds that add/remove files will not be supported without
    rerunning gn.

enable_perfetto_stderr_crash_dump
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:259

enable_perfetto_system_consumer
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:301

enable_perfetto_tools
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:207

enable_perfetto_trace_processor_httpd
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:361

enable_perfetto_trace_processor_json
    Current value (from the default) = true
      From //third_party/perfetto/gn/perfetto.gni:346

enable_perfetto_trace_processor_linenoise
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:340

enable_perfetto_trace_processor_mac_instruments
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:352

enable_perfetto_trace_processor_percentile
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:335

enable_perfetto_trace_processor_sqlite
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:330

enable_perfetto_traceconv
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:405

enable_perfetto_traced_perf
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:185

enable_perfetto_traced_probes
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:318

enable_perfetto_traced_relay
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:322

    The relay service is enabled when platform services are enabled.
    TODO(chinglinyu) check if we can enable on Windows.

enable_perfetto_ui
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:409

enable_perfetto_unittests
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:209

enable_perfetto_version_gen
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:227

enable_perfetto_watchdog
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:202

enable_perfetto_winscope
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:398

enable_perfetto_x64_cpu_opt
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:267

enable_perfetto_zlib
    Current value (from the default) = true
      From //third_party/perfetto/gn/perfetto.gni:369

enable_precompiled_headers
    Current value (from the default) = false
      From //build/config/pch.gni:19

enable_profiling
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:82

    Compile in such a way as to enable profiling of the generated code. For
    example, don't omit the frame pointer and leave in symbols.

enable_proguard_obfuscation
    Current value (from the default) = true
      From //build/config/android/config.gni:289

    Controls whether proguard obfuscation is enabled for targets
    configured to use it.

enable_r8_tracerefs
    Current value (from the default) = false
      From //build/config/android/config.gni:114

    Controls whether TraceReferences checks are done (for targets with
    proguard_enabled=true).

enable_resource_allowlist_generation
    Current value (from the default) = false
      From //build/toolchain/gcc_toolchain.gni:22

enable_rust
    Current value = true
      From //.gn:88
    Overridden from the default = false
      From //build/config/rust.gni:34

    Rust is available in the Chromium build but 3p repos that use //build may
    not use Rust and thus won't want to depend on having the Rust toolchain
    present, so this defaults to off in those cases.
   
    Chromium-based projects that are built for for architectures Chrome does not
    support may need to disable this as well, though they may need to replace
    code with C/C++ to get a functional product.
   
    Based on the above:
   
    * `enable_rust` may be consulted under `//build` and `//testing` directories
      (which may be used outside of Chromium build)
    * `enable_rust` should *not* be consulted in other Chromium directories
      (including `//base`, `//net`, etc.)

enable_rust_cxx
    Current value = true
      From //.gn:89
    Overridden from the default = true
      From //build/config/rust.gni:105

    The CXX tool is in //third_party/rust which is not shared with downstream
    projects yet. So they need to copy the required dependencies and GN files
    into their project to enable CXX there.
   
    Currently, cxx is needed to use Rust with an allocator since the Chromium
    allocator shim uses cxx

enable_segment_heap
    Current value (from the default) = false
      From //build/config/win/manifest.gni:46

enable_shadow_call_stack
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:192

    Enable ShadowCallStack for compiled binaries. SCS stores a pointer to a
    shadow call stack in register x18. Hence, x18 must not be used by the OS
    or libraries. We assume that to be the case for high end Android
    configurations. For more details see
    https://clang.llvm.org/docs/ShadowCallStack.html

enable_src_internal
    Current value (from the default) = false
      From //build/config/chrome_build.gni:16

enable_startup_profiles
    Current value (from the default) = false
      From //build/config/android/config.gni:311

    Controls whether specifying |art_profile_path| automatically applies it as
    a startup profile to the APK/AAB.
    Currently disabled while R8 causes checkdiscard errors due to
    methods/classes not being inlined correctly.

enable_stripping
    Current value (from the default) = false
      From //build/config/apple/symbols.gni:24

    Strip symbols from linked targets by default. If this is enabled, the
    //build/config/apple:strip_all config will be applied to all linked targets.
    If custom stripping parameters are required, remove that config from a
    linked target and apply custom -Wcrl,strip flags. See
    //build/toolchain/apple/linker_driver.py for more information.

enable_trace_event_bytecode_rewriting
    Current value (from the default) = false
      From //build/config/android/config.gni:362

enable_unused_resource_stripping
    Current value (from the default) = true
      From //build/config/android/config.gni:299

    Controls whether |strip_unused_resources| is respected. Useful when trying
    to analyze APKs using tools that do not support missing resources from
    resources.arsc.

exclude_unwind_tables
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:123

    Exclude unwind tables by default for official builds as unwinding can be
    done from stack dumps produced by Crashpad at a later time "offline" in the
    crash server. Since this increases binary size, we don't recommend including
    them in shipping builds.
    For unofficial (e.g. development) builds and non-Chrome branded (e.g. Cronet
    which doesn't use Crashpad, crbug.com/479283) builds it's useful to be able
    to unwind at runtime.
    Include the unwind tables on Android even for official builds, as otherwise
    the crash dumps generated by Android's debuggerd are largely useless, and
    having this additional mechanism to understand issues is particularly helpful
    to WebView.

expectations_failure_dir
    Current value (from the default) = "/home/zhouxin/work/webrtc/build/android/debug/failed_expectations"
      From //build/config/android/config.gni:323

    Where to write failed expectations for bots to read.

extra_sysroot_libs
    Current value (from the default) = []
      From //build/config/rust.gni:90

    Non-rlib libs provided in the toolchain sysroot. Usually this is empty, but
    e.g. the Android Rust Toolchain provides a libunwind.a that rustc expects.

fail_on_android_expectations
    Current value (from the default) = false
      From //build/config/android/config.gni:285

    Causes expectation failures to break the build, otherwise, just warns on
    stderr and writes a failure file to $android_configuration_failure_dir:

fail_on_san_warnings
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:266

    When true, sanitizer warnings will cause test case failures.

fatal_linker_warnings
    Current value (from the default) = true
      From //build/config/compiler/BUILD.gn:68

    Enable fatal linker warnings. Building Chromium with certain versions
    of binutils can cause linker warning.

forbid_non_component_debug_builds
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:110

    Whether an error should be raised on attempts to make debug builds with
    is_component_build=false. Very large debug symbols can have unwanted side
    effects so this is enforced by default for chromium.

force_rustc_color_output
    Current value (from the default) = false
      From //build/config/rust.gni:95

    Force-enable `--color=always` for rustc, even when it would be disabled for
    a platform. Mostly applicable to Windows, where new versions can handle ANSI
    escape sequences but it's not reliable in general.

gcc_target_rpath
    Current value (from the default) = ""
      From //build/config/gcc/BUILD.gn:19

    When non empty, overrides the target rpath value. This allows a user to
    make a Chromium build where binaries and shared libraries are meant to be
    installed into separate directories, like /usr/bin/chromium and
    /usr/lib/chromium for instance. It is useful when a build system that
    generates a whole target root filesystem (like Yocto) is used on top of gn,
    especially when cross-compiling.
    Note: this gn arg is similar to gyp target_rpath generator flag.

generate_fuzzer_owners
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:270

    Generates an owners file for each fuzzer test.
    TODO(crbug.com/40175535): Remove this arg when finding OWNERS is faster.

generate_linker_map
    Current value (from the default) = false
      From //build/toolchain/toolchain.gni:24

    Used for binary size analysis.

gtest_enable_absl_printers
    Current value = true
      From //.gn:66
    Overridden from the default = true
      From //build_overrides/build.gni:67

    If true, it assumes that //third_party/abseil-cpp is an available
    dependency for googletest.

high_end_fuzzer_targets
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:129

    When true, only builds fuzzer targets that require high end machines to run.
    Otherwise, builds all the targets.
    TODO(paulsemel): once we have everything implemented on the recipe side, we
    can change the behaviour for the false case, and only build the non high-end
    jobs, so that they do not appear in the zip. As for now, this behaviour
    ensures nothing breaks.

host_byteorder
    Current value (from the default) = "undefined"
      From //build/config/host_byteorder.gni:9

host_cpu
    Current value (from the default) = "x64"
      (Internally set; try `gn help host_cpu`.)

host_os
    Current value (from the default) = "linux"
      (Internally set; try `gn help host_os`.)

host_pkg_config
    Current value (from the default) = ""
      From //build/config/linux/pkg_config.gni:39

    A optional pkg-config wrapper to use for tools built on the host.

host_toolchain
    Current value (from the default) = ""
      From //build/config/BUILDCONFIG.gn:148

    This should not normally be set as a build argument.  It's here so that
    every toolchain can pass through the "global" value via toolchain_args().

host_toolchain_is_msan
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:143

    These are set for some non-host toolchain which requires to run built binary
    from host toolchain with msan config but in non-host toolchain context.

host_toolchain_msan_track_origins
    Current value (from the default) = 2
      From //build/config/sanitizers/sanitizers.gni:144

incremental_install
    Current value (from the default) = false
      From //build/config/android/config.gni:12

    Build incremental targets whenever possible.
    See //build/android/incremental_install/README.md for more details.

init_stack_vars
    Current value (from the default) = true
      From //build/config/compiler/BUILD.gn:143

    Initialize all local variables with a pattern. This flag will fill
    uninitialized floating-point types (and 32-bit pointers) with 0xFF and the
    rest with 0xAA. This makes behavior of uninitialized memory bugs consistent,
    recognizable in the debugger, and crashes on memory accesses through
    uninitialized pointers.
   
    Flag discussion: https://crbug.com/977230
   
    TODO(crbug.com/40721698): This regresses binary size by ~1MB on Android and
    needs to be evaluated before enabling it there as well.

init_stack_vars_zero
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:152

    Specialization of `init_stack_vars` where locals are initialized with
    zeroes instead of patterns. Zero init is generally faster and can lead to
    better binary size due to being able to use instructions taking
    immediates. Patterns potentially have better signaling behavior though. We
    thus enable zero-init for all official builds that don't also enable
    DCHECKs explicitly or implicitly. Note that this intentionally also keeps
    the patterns for DCHECK Canary releases.

ios_allow_asan_for_official_to_debug_371135823
    Current value (from the default) = false
      From //build/config/sanitizers/BUILD.gn:20

    Allow building official with ASAN enabled to help with
    debugging https://crbug.com/371135823.

ios_app_bundle_id_prefix
    Current value (from the default) = "org.chromium.ost"
      From //build/config/apple/mobile_config.gni:38

    Prefix for CFBundleIdentifier property of iOS bundles (correspond to the
    "Organization Identifier" in Xcode). Code signing will fail if no mobile
    provisioning for the selected code signing identify support that prefix.
    TODO(crbug.com/378918882): Prefix with apple_mobile_ instead of ios_.

ios_chrome_generate_order_file
    Current value (from the default) = false
      From //build/config/ios/config.gni:12

    Generate orderfile at application startup and then exit.
    NOTE: This flag adds runtime tooling to capture function call details,
    writes out an orderfile to the documents directory, then terminates the
    application. It should generally NOT be enabled.

ios_code_signing_identity
    Current value (from the default) = ""
      From //build/config/apple/mobile_config.gni:26

    Explicitly select the identity to use for codesigning. If defined, must
    be set to a non-empty string that will be passed to codesigning. Can be
    left unspecified if ios_code_signing_identity_description is used instead.
    TODO(crbug.com/378918882): Prefix with apple_mobile_ instead of ios_.

ios_code_signing_identity_description
    Current value (from the default) = "Apple Development"
      From //build/config/apple/mobile_config.gni:32

    Pattern used to select the identity to use for codesigning. If defined,
    must be a substring of the description of exactly one of the identities by
    `security find-identity -v -p codesigning`.
    TODO(crbug.com/378918882): Prefix with apple_mobile_ instead of ios_.

ios_deployment_target
    Current value = "14.0"
      From //.gn:55
    Overridden from the default = "26.0"
      From //build/config/ios/ios_sdk_overrides.gni:14

ios_enable_code_signing
    Current value (from the default) = true
      From //build/config/apple/mobile_config.gni:20

    Control whether codesiging is enabled (ignored for simulator builds).
    TODO(crbug.com/378918882): Prefix with apple_mobile_ instead of ios_.

ios_mobileprovision_files
    Current value (from the default) = []
      From //build/config/apple/mobile_config.gni:51

    Paths to the mobileprovision files for the chosen code signing
    identity description and app bundle id prefix.
    TODO(crbug.com/378918882): Prefix with apple_mobile_ instead of ios_.

is_asan
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:14

    Compile for Address Sanitizer to find memory bugs.

is_cast_android
    Current value (from the default) = false
      From //build/config/cast.gni:32

    Set this to true to build for Android-based Cast devices.
    Set this to false to use the defaults for Android.

is_cast_audio_only
    Current value (from the default) = false
      From //build/config/cast.gni:12

    Set this true for an audio-only Chromecast build.
    TODO(crbug.com/41489655): Remove this arg as CastOS builds are no
    longer supported.

is_castos
    Current value (from the default) = false
      From //build/config/cast.gni:28

    Set this to true to build for Nest hardware running Linux (aka "CastOS").
    Set this to false to use the defaults for Linux.

is_cfi
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:59

    Compile with Control Flow Integrity to protect virtual calls and casts.
    See http://clang.llvm.org/docs/ControlFlowIntegrity.html
   
    TODO(pcc): Remove this flag if/when CFI is enabled in all official builds.

is_chrome_branded
    Current value (from the default) = false
      From //build/config/chrome_build.gni:12

    Select the desired branding flavor. False means normal Chromium branding,
    true means official Google Chrome branding (requires extra Google-internal
    resources).

is_chrome_for_testing
    Current value (from the default) = false
      From //build/config/chrome_build.gni:23

    Whether to enable the Chrome for Testing (CfT) flavor. This arg is not
    compatible with `is_chrome_branded`.
   
    Design document: https://goo.gle/chrome-for-testing

is_chrome_for_testing_branded
    Current value (from the default) = false
      From //build/config/chrome_build.gni:29

    Whether to use internal Chrome for Testing (CfT).
    Requires `src-internal/` and `is_chrome_for_testing = true`.
   
    When true, use Google-internal icons, otherwise fall back to Chromium icons.

is_chromeos_device
    Current value (from the default) = false
      From //build/config/chromeos/args.gni:26

    Determines if we're building for a Chrome OS device (or VM) and not just
    linux-chromeos. NOTE: Most test targets in Chrome expect to run under
    linux-chromeos, so some have compile-time asserts that intentionally fail
    when this build flag is set. Build and run the tests for linux-chromeos
    instead.
    https://chromium.googlesource.com/chromium/src/+/main/docs/chromeos_build_instructions.md
    https://chromium.googlesource.com/chromiumos/docs/+/main/simple_chrome_workflow.md

is_chromeos_with_hw_details
    Current value (from the default) = false
      From //build/config/chromeos/args.gni:34

    Determines if we collect hardware information in chrome://system and
    feedback logs. A similar build flag "hw_details" is defined in Chrome OS
    (see https://crrev.com/c/3123455).

is_clang
    Current value (from the default) = true
      From //build/config/BUILDCONFIG.gn:139

    Set to true when compiling with the Clang compiler.

is_component_build
    Current value = false
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:13
    Overridden from the default = true
      From //build/config/BUILDCONFIG.gn:171

is_cronet_build
    Current value (from the default) = false
      From //build/config/cronet/config.gni:31

    Signals that Cronet is being built. Building within Android always implies
    that Cronet is being built.

is_cronet_for_aosp_build
    Current value (from the default) = false
      From //build/config/cronet/config.gni:20

    Signals that Cronet is being built within Android.
   
    Note: attempting to build directly with GN, while this arg is set to true,
    is wrong and will not work. Instead, this arg is used internally by the
    scripts within //components/cronet/android/gn2bp to generate Soong build
    rules to build within the Android repository.

is_debug
    Current value = true
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:4
    Overridden from the default = true
      From //build/config/BUILDCONFIG.gn:160

    Debug build. Enabling official builds automatically sets is_debug to false.

is_desktop_android
    Current value (from the default) = false
      From //build/config/chrome_build.gni:37

    Set to true to set defaults that enable features on Android that are more
    typically available on desktop.

is_high_end_android
    Current value (from the default) = true
      From //build/config/chrome_build.gni:33

    Set to true to enable settings for high end Android devices, typically
    enhancing speed at the expense of resources such as binary sizes and memory.

is_high_end_android_secondary_toolchain
    Current value (from the default) = false
      From //build/config/chrome_build.gni:70

    Whether to apply size->speed trade-offs to the secondary toolchain.
    Relevant only for 64-bit target_cpu.

is_hwasan
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:19

    Compile for Hardware-Assisted Address Sanitizer to find memory bugs
    (android/arm64 only).
    See http://clang.llvm.org/docs/HardwareAssistedAddressSanitizerDesign.html

is_java_debug
    Current value (from the default) = true
      From //build/config/android/config.gni:18

    Java debug on Android. Having this on enables multidexing, and turning it
    off will enable proguard.

is_lsan
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:22

    Compile for Leak Sanitizer to find leaks.

is_msan
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:25

    Compile for Memory Sanitizer to find uninitialized reads.

is_official_build
    Current value (from the default) = false
      From //build/config/BUILDCONFIG.gn:136

    Set to enable the official build level of optimization. This has nothing
    to do with branding, but enables an additional level of optimization above
    release (!is_debug). This might be better expressed as a tri-state
    (debug, release, official) but for historical reasons there are two
    separate flags.
   
    IMPORTANT NOTE: (!is_debug) is *not* sufficient to get satisfying
    performance. In particular, DCHECK()s are still enabled for release builds,
    which can halve overall performance, and do increase memory usage. Always
    set "is_official_build" to true for any build intended to ship to end-users.

is_perfetto_build_generator
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:94

    All the tools/gen_* scripts set this to true. This is mainly used to locate
    .gni files from //gn rather than //build.

is_perfetto_embedder
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:99

    This is for override via `gn args` (e.g. for tools/gen_xxx). Embedders
    based on GN (e.g. v8) should NOT set this and instead directly sets
    perfetto_build_with_embedder=true in their GN files.

is_reven
    Current value (from the default) = false
      From //build/config/chromeos/args.gni:40

    Refers to the separate branding required for the reven build.

is_robolectric
    Current value (from the default) = false
      From //build/config/BUILDCONFIG.gn:153

    Do not set this directly.
    It should be set only by //build/toolchains/android:robolectric_x64.
    True when compiling native code for use with robolectric_binary().

is_skylab
    Current value (from the default) = false
      From //build/config/chromeos/args.gni:29

    Determines if we run the test in skylab, aka the CrOS labs.

is_tsan
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:28

    Compile for Thread Sanitizer to find threading bugs.

is_ubsan
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:32

    Compile for Undefined Behaviour Sanitizer to find various types of
    undefined behaviour (excludes vptr checks).

is_ubsan_no_recover
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:35

    Halt the program if a problem is detected.

is_ubsan_security
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:95

    Enables core ubsan security features. Will later be removed once it matches
    is_ubsan.

is_ubsan_vptr
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:139

    Compile for Undefined Behaviour Sanitizer's vptr checks.

is_wexit_time_destructors_default
    Current value (from the default) = true
      From //build/config/compiler/BUILD.gn:210

    This switch is used to enable -Wexit-time-destructors by default. This
    warning serves as a flip switch to allow a gradual migration of targets
    away from the opt-in wexit_time_destructors config, and into the default
    compiler config.

ldso_path
    Current value (from the default) = ""
      From //build/config/gcc/BUILD.gn:20

libcxx_is_shared
    Current value (from the default) = false
      From //build/config/c++/c++.gni:64

    WARNING: Setting this to a non-default value is highly discouraged.
    If true, libc++ will be built as a shared library; otherwise libc++ will be
    linked statically. Setting this to something other than the default is
    unsupported and can be broken by libc++ rolls. Note that if this is set to
    true, you must also set libcxx_abi_unstable=false, which is bad for
    performance and memory use.

libcxx_natvis_include
    Current value (from the default) = true
      From //build/config/c++/c++.gni:33

    Builds libcxx Natvis into the symbols for type visualization.
    Set to false to workaround http://crbug.com/966676 and
    http://crbug.com/966687.

libcxx_revision
    Current value (from the default) = "07572e7b169225ef3a999584cba9d9004631ae66"
      From //buildtools/deps_revisions.gni:8

    Used to cause full rebuilds on libc++ rolls. This should be kept in sync
    with the libcxx_revision var in //DEPS.

libsrtp_build_boringssl
    Current value (from the default) = true
      From //third_party/libsrtp/options.gni:8

    Build with BoringSSL by default, allow WebRTC to override
    this and the include path from rtc_build_ssl and rtc_ssl_root

libsrtp_ssl_root
    Current value (from the default) = ""
      From //third_party/libsrtp/options.gni:9

libyuv_disable_jpeg
    Current value (from the default) = false
      From //third_party/libyuv/libyuv.gni:16

libyuv_disable_rvv
    Current value (from the default) = false
      From //third_party/libyuv/libyuv.gni:17

libyuv_include_tests
    Current value (from the default) = true
      From //third_party/libyuv/libyuv.gni:15

libyuv_symbols_visible
    Current value (from the default) = false
      From //third_party/libyuv/BUILD.gn:20

    When building a shared library using a target in WebRTC or
    Chromium projects that depends on libyuv, setting this flag
    to true makes libyuv symbols visible inside that library.

libyuv_use_absl_flags
    Current value (from the default) = true
      From //third_party/libyuv/BUILD.gn:15

    Set to false to disable building with absl flags.

libyuv_use_lasx
    Current value (from the default) = false
      From //third_party/libyuv/libyuv.gni:33

libyuv_use_lsx
    Current value (from the default) = false
      From //third_party/libyuv/libyuv.gni:32

libyuv_use_mmi
    Current value (from the default) = false
      From //third_party/libyuv/libyuv.gni:31

libyuv_use_msa
    Current value (from the default) = false
      From //third_party/libyuv/libyuv.gni:29

libyuv_use_neon
    Current value (from the default) = true
      From //third_party/libyuv/libyuv.gni:19

libyuv_use_sme
    Current value (from the default) = true
      From //third_party/libyuv/libyuv.gni:27

    Restrict to (is_linux || is_android) to work around undefined symbol linker
    errors on Fuchsia, macOS, and compilation errors on Windows.
    TODO: bug 359006069 - Remove the restriction after the linker and
    compilation errors are fixed.

libyuv_use_sve
    Current value (from the default) = true
      From //third_party/libyuv/libyuv.gni:21

limit_android_deps
    Current value (from the default) = false
      From //build_overrides/build.gni:63

    Limits the defined //third_party/android_deps targets to only "buildCompile"
    and "buildCompileNoDeps" targets. This is useful for third-party
    repositories which do not use JUnit tests. For instance,
    limit_android_deps == true removes "gn gen" requirement for
    //third_party/robolectric .

lint_android_sdk_root
    Current value (from the default) = "//third_party/android_sdk/public"
      From //build/config/android/config.gni:238

lint_android_sdk_version
    Current value (from the default) = "36"
      From //build/config/android/config.gni:239

lld_emit_indexes_and_imports
    Current value (from the default) = false
      From //build/config/clang/clang.gni:83

    Set to true to enable output of ThinLTO index and import files used for
    creating a Chromium MLGO corpus in the ThinLTO case.

lld_path
    Current value (from the default) = ""
      From //build/config/compiler/BUILD.gn:51

    This allows overriding the location of lld.

llvm_force_head_revision
    Current value (from the default) = false
      From //build/toolchain/toolchain.gni:16

    If this is set to true, we use the revision in the llvm repo to determine
    the CLANG_REVISION to use, instead of the version hard-coded into
    //tools/clang/scripts/update.py. This should only be used in
    conjunction with setting the llvm_force_head_revision DEPS variable when
    `gclient runhooks` is run as well.

mac_sdk_min
    Current value = "10.12"
      From //.gn:53
    Overridden from the default = "15"
      From //build/config/mac/mac_sdk_overrides.gni:10

    Minimum supported version of the Mac SDK.

mips_use_mmi
    Current value (from the default) = false
      From //build/config/mips.gni:13

    MIPS MultiMedia Instruction compilation flag.

monolithic_binaries
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:238

msan_check_use_after_dtor
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:49

    TODO(crbug.com/40222690): Enable everywhere.

msan_eager_checks
    Current value (from the default) = true
      From //build/config/sanitizers/sanitizers.gni:46

    Enables "param-retval" mode, which finds more uses of uninitialized data and
    reduces code size. Behind a flag as there are a number of previously
    undetected violations that still need to be fixed.
    TODO(crbug.com/40240570): Default this to true and remove.

msan_track_origins
    Current value (from the default) = 2
      From //build/config/sanitizers/sanitizers.gni:40

    Track where uninitialized memory originates from. From fastest to slowest:
    0 - no tracking, 1 - track only the initial allocation site, 2 - track the
    chain of stores leading from allocation site to use site.

optimize_for_fuzzing
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:83

    Optimize for coverage guided fuzzing (balance between speed and number of
    branches)

optimize_for_size
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:38

ozone_auto_platforms
    Current value (from the default) = false
      From //build/config/ozone.gni:20

    Select platforms automatically. Turn this off for manual control.

ozone_extra_path
    Current value (from the default) = "//build/config/ozone_extra.gni"
      From //build/config/ozone.gni:17

    Ozone extra platforms file path. Can be overridden to build out of
    tree ozone platforms.

ozone_platform
    Current value (from the default) = ""
      From //build/config/ozone.gni:39

    The platform that will used at runtime by default. This can be overridden
    with the command line flag --ozone-platform=<platform>.

ozone_platform_cast
    Current value (from the default) = false
      From //build/config/ozone.gni:42

    Compile the 'cast' platform.

ozone_platform_drm
    Current value (from the default) = false
      From //build/config/ozone.gni:45

    Compile the 'drm' platform.

ozone_platform_flatland
    Current value (from the default) = false
      From //build/config/ozone.gni:51

    Compile the 'flatland' platform.

ozone_platform_gbm
    Current value (from the default) = -1
      From //build/config/ozone.gni:24

    TODO(petermcneeley): Backwards compatiblity support for VM images.
    Remove when deprecated. (https://crbug.com/1122009)

ozone_platform_headless
    Current value (from the default) = false
      From //build/config/ozone.gni:48

    Compile the 'headless' platform.

ozone_platform_wayland
    Current value (from the default) = false
      From //build/config/ozone.gni:57

    Compile the 'wayland' platform.

ozone_platform_x11
    Current value (from the default) = false
      From //build/config/ozone.gni:54

    Compile the 'x11' platform.

perfetto_build_with_android
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:90

    The Android blueprint file generator set this to true (as well as
    is_perfetto_build_generator). This is just about being built in the
    Android tree (AOSP and internal) and is NOT related with the target OS.
    In standalone Android builds and Chromium Android builds, this is false.

perfetto_enable_git_rev_version_header
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:307

perfetto_force_dcheck
    Current value (from the default) = ""
      From //third_party/perfetto/gn/perfetto.gni:253

    Whether DCHECKs should be enabled or not. Values: "on" | "off" | "".
    By default ("") DCHECKs are enabled only:
    - If DCHECK_ALWAYS_ON is defined (which is mainly a Chromium-ism).
    - On debug builds (i.e. if NDEBUG is NOT defined) but only in Chromium,
      Android and standalone builds.
    - On all other builds (e.g., SDK) it's off regardless of NDEBUG (unless
      DCHECK_ALWAYS_ON is defined).
    See base/logging.h for the implementation of all this.

perfetto_force_dlog
    Current value (from the default) = ""
      From //third_party/perfetto/gn/perfetto.gni:243

    Whether DLOG should be enabled on debug builds (""), all builds ("on"), or
    none ("off"). We disable it by default for embedders to avoid spamming their
    console.

perfetto_thread_safety_annotations
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:272

perfetto_use_pkgconfig
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:431

    Used by CrOS builds. Uses pkg-config to determine the appropriate flags
    for including and linking system libraries.
      set `host_pkg_config` to the `BUILD_PKG_CONFIG` and
      set `pkg_config` to the target `PKG_CONFIG`.
    Note: that if this is enabled `perfetto_use_system_protobuf` should be also.

perfetto_use_system_protobuf
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:435

    Used by CrOS system builds. Uses the system version of protobuf
    from /usr/include instead of the hermetic one.

perfetto_use_system_sqlite
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:439

    Used by CrOS system builds. Uses the system version of sqlite
    from /usr/include instead of the hermetic one.

perfetto_use_system_zlib
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:441

perfetto_verbose_logs_enabled
    Current value (from the default) = true
      From //third_party/perfetto/gn/perfetto.gni:326

pgo_data_path
    Current value (from the default) = ""
      From //build/config/compiler/pgo/pgo.gni:38

    When using chrome_pgo_phase = 2, read profile data from this path.

pgo_gs_bucket
    Current value (from the default) = ""
      From //build/config/compiler/pgo/pgo.gni:46

pgo_gs_bucket_path
    Current value (from the default) = ""
      From //build/config/compiler/pgo/pgo.gni:47

pgo_override_filename
    Current value (from the default) = ""
      From //build/config/compiler/pgo/pgo.gni:45

    Flags to override the pgo profile with a freshly created one (newer than
    the sha1 in the repo).

pkg_config
    Current value (from the default) = ""
      From //build/config/linux/pkg_config.gni:36

    A pkg-config wrapper to call instead of trying to find and call the right
    pkg-config directly. Wrappers like this are common in cross-compilation
    environments.
    Leaving it blank defaults to searching PATH for 'pkg-config' and relying on
    the sysroot mechanism to find the right .pc files.

proprietary_codecs
    Current value (from the default) = false
      From //build/config/features.gni:31

    Enables proprietary codecs and demuxers; e.g. H264, AAC, MP3, and MP4.
    We always build Google Chrome and Chromecast with proprietary codecs.
   
    Note: this flag is used by WebRTC which is DEPSed into Chrome. Moving it
    out of //build will require using the build_overrides directory.
   
    Do not add any other conditions to the following line.
   
    TODO(crbug.com/1314528): Remove chromecast-related conditions and force
    builds to explicitly specify this.

protobuf_abseil_dir
    Current value (from the default) = "//third_party/abseil-cpp"
      From //third_party/protobuf/proto_library.gni:155

rbe_bin_dir
    Current value (from the default) = ""
      From //build/toolchain/rbe.gni:11

    Deprecated: Please use reclient_bin_dir instead.

rbe_cfg_dir
    Current value (from the default) = ""
      From //build/toolchain/rbe.gni:14

    Deprecated: Please use reclient_cfg_dir instead.

rbe_cros_cc_wrapper
    Current value (from the default) = ""
      From //build/toolchain/rbe.gni:17

    Deprecated: Please use reclient_cros_cc_wrapper instead.

rbe_exec_root
    Current value (from the default) = "/home/zhouxin/work/webrtc/src/"
      From //build/toolchain/rbe.gni:23

    Execution root - this should be the root of the source tree.
    This is defined here instead of in the config file because
    this will vary depending on where the user has placed the
    chromium source on their system.

reclient_bin_dir
    Current value (from the default) = "../../../src/buildtools/reclient"
      From //build/toolchain/rbe.gni:41

reclient_cc_cfg_file
    Current value (from the default) = ""
      From //build/toolchain/rbe.gni:100

reclient_cfg_dir
    Current value (from the default) = "//buildtools/reclient_cfgs"
      From //build/toolchain/rbe.gni:52

reclient_cros_cc_wrapper
    Current value (from the default) = ""
      From //build/toolchain/rbe.gni:135

reclient_py_cfg_file
    Current value (from the default) = ""
      From //build/toolchain/rbe.gni:101

register_fuzztests_in_test_suites
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:183

removed_rust_stdlib_libs
    Current value (from the default) = []
      From //build/config/rust.gni:86

    Any removed std rlibs in your Rust toolchain, relative to the standard
    Rust toolchain. Typically used with 'rust_sysroot_absolute'

rtc_audio_device_plays_sinus_tone
    Current value (from the default) = false
      From //webrtc.gni:200

    When set to true, replace the audio output with a sinus tone at 440Hz.
    The ADM will ask for audio data from WebRTC but instead of reading real
    audio samples from NetEQ, a sinus tone will be generated and replace the
    real audio samples.

rtc_build_dcsctp
    Current value (from the default) = true
      From //webrtc.gni:329

    Enable the dcsctp backend for DataChannels and related unittests

rtc_build_examples
    Current value (from the default) = true
      From //webrtc.gni:119

    Set this to false to skip building examples.

rtc_build_json
    Current value (from the default) = true
      From //webrtc.gni:277

    Disable these to not build components which can be externally provided.

rtc_build_libsrtp
    Current value (from the default) = true
      From //webrtc.gni:278

rtc_build_libvpx
    Current value (from the default) = true
      From //webrtc.gni:279

rtc_build_opus
    Current value (from the default) = true
      From //webrtc.gni:281

rtc_build_ssl
    Current value (from the default) = true
      From //webrtc.gni:282

rtc_build_tools
    Current value = false
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:11
    Overridden from the default = true
      From //webrtc.gni:122

    Set this to false to skip building tools.

rtc_build_with_neon
    Current value (from the default) = true
      From //webrtc.gni:158

rtc_builtin_ssl_root_certificates
    Current value (from the default) = true
      From //webrtc.gni:87

    Setting this to false will require the API user to pass in their own
    SSLCertificateVerifier to verify the certificates presented from a
    TLS-TURN server. In return disabling this saves around 100kb in the binary.

rtc_common_public_deps
    Current value (from the default) = []
      From //webrtc.gni:256

    Embedders can define dependencies needed by WebRTC. Dependencies can be
    configs or targets. This can be defined in their `.gn` file.
   
    In practise, this is use by Chromium: Targets from
    `//third_party/webrtc_overrides` are depending on Chrome's `//base`, but
    WebRTC does not declare its public dependencies. See webrtc:8603. Instead
    WebRTC is using a global common dependencies.

rtc_disable_check_msg
    Current value (from the default) = false
      From //webrtc.gni:315

    Set this to true to disable detailed error message and logging for
    RTC_CHECKs.

rtc_disable_logging
    Current value (from the default) = false
      From //webrtc.gni:308

    Set this to true to fully remove logging from WebRTC.

rtc_disable_metrics
    Current value (from the default) = false
      From //webrtc.gni:318

    Set this to true to disable webrtc metrics.

rtc_disable_trace_events
    Current value (from the default) = false
      From //webrtc.gni:311

    Set this to true to disable trace events.

rtc_dlog_always_on
    Current value (from the default) = false
      From //webrtc.gni:59

    Setting this to true, will make RTC_DLOG() expand to log statements instead
    of being removed by the preprocessor.
    This is useful for example to be able to get RTC_DLOGs on a release build.

rtc_enable_android_aaudio
    Current value (from the default) = false
      From //webrtc.gni:141

    Experimental: enable use of Android AAudio which requires Android SDK 26 or above
    and NDK r16 or above.

rtc_enable_avx2
    Current value (from the default) = true
      From //webrtc.gni:294

rtc_enable_external_auth
    Current value (from the default) = false
      From //webrtc.gni:109

    Enable when an external authentication mechanism is used for performing
    packet authentication for RTP packets instead of libsrtp.

rtc_enable_google_benchmarks
    Current value (from the default) = true
      From //webrtc.gni:63

    Enables additional build targets that rely on
    //third_party/google_benchmarks.

rtc_enable_grpc
    Current value (from the default) = false
      From //webrtc.gni:332

    Enable gRPC used for negotiation in multiprocess tests

rtc_enable_objc_symbol_export
    Current value (from the default) = false
      From //webrtc.gni:70

    Setting this to true will make RTC_OBJC_EXPORT expand to code that will
    manage symbols visibility. By default, Obj-C/Obj-C++ symbols are exported
    if C++ symbols are but setting this arg to true while keeping
    rtc_enable_symbol_export=false will only export RTC_OBJC_EXPORT
    annotated symbols.

rtc_enable_protobuf
    Current value (from the default) = true
      From //webrtc.gni:271

    Enables the use of protocol buffers for debug recordings.

rtc_enable_sctp
    Current value (from the default) = true
      From //webrtc.gni:274

    Set this to disable building with support for SCTP data channels.

rtc_enable_symbol_export
    Current value (from the default) = false
      From //webrtc.gni:52

    Setting this to true will make RTC_EXPORT (see rtc_base/system/rtc_export.h)
    expand to code that will manage symbols visibility.

rtc_enable_win_wgc
    Current value (from the default) = false
      From //webrtc.gni:228

    When set to true, a capturer implementation that uses the
    Windows.Graphics.Capture APIs will be available for use. This introduces a
    dependency on the Win 10 SDK v10.0.17763.0.

rtc_exclude_audio_processing_module
    Current value (from the default) = false
      From //webrtc.gni:116

    Selects whether the audio processing module should be excluded.

rtc_exclude_metrics_default
    Current value (from the default) = false
      From //webrtc.gni:76

    Setting this to true will define WEBRTC_EXCLUDE_METRICS_DEFAULT which
    will tell the pre-processor to remove the default definition of symbols
    needed to use metrics. In that case a new implementation needs to be
    provided.

rtc_exclude_system_time
    Current value (from the default) = false
      From //webrtc.gni:82

    Setting this to true will define WEBRTC_EXCLUDE_SYSTEM_TIME which
    will tell the pre-processor to remove the default definition of the
    SystemTimeNanos() which is defined in rtc_base/system_time.cc. In
    that case a new implementation needs to be provided.

rtc_include_builtin_audio_codecs
    Current value (from the default) = true
      From //webrtc.gni:215

    When set to false, builtin audio encoder/decoder factories and all the
    audio codecs they depend on will not be included in libwebrtc.{a|lib}
    (they will still be included in libjingle_peerconnection_so.so and
    WebRTC.framework)

rtc_include_dav1d_in_internal_decoder_factory
    Current value (from the default) = true
      From //webrtc.gni:231

    Includes the dav1d decoder in the internal decoder factory when set to true.

rtc_include_internal_audio_device
    Current value (from the default) = true
      From //webrtc.gni:289

    Chromium uses its own IO handling, so the internal ADM is only built for
    standalone WebRTC.

rtc_include_opus
    Current value (from the default) = true
      From //webrtc.gni:90

    Disable this to avoid building the Opus audio codec.

rtc_include_pulse_audio
    Current value (from the default) = true
      From //webrtc.gni:285

    Excluded in Chromium since its prerequisites don't require Pulse Audio.

rtc_include_tests
    Current value = false
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:10
    Overridden from the default = true
      From //webrtc.gni:301

    Set this to true to build the unit tests.
    Disabled when building with Chromium or Mozilla.

rtc_ios_use_opengl_rendering
    Current value (from the default) = false
      From //webrtc.gni:209

    Determines whether OpenGL is available on iOS.

rtc_jsoncpp_root
    Current value (from the default) = "//third_party/jsoncpp/source/include"
      From //webrtc.gni:101

    Used to specify an external Jsoncpp include path when not compiling the
    library that comes with WebRTC (i.e. rtc_build_json == 0).

rtc_libvpx_build_vp9
    Current value (from the default) = true
      From //webrtc.gni:280

rtc_link_pipewire
    Current value (from the default) = false
      From //webrtc.gni:134

    Set this to link PipeWire and required libraries directly instead of using the dlopen.

rtc_objc_prefix
    Current value (from the default) = ""
      From //webrtc.gni:247

    If different from "", symbols exported with RTC_OBJC_EXPORT will be prefixed
    with this string.
    See the definition of RTC_OBJC_TYPE_PREFIX in the code.

rtc_opus_support_120ms_ptime
    Current value (from the default) = true
      From //webrtc.gni:94

    Enable this if the Opus version upon which WebRTC is built supports direct
    encoding of 120 ms packets.

rtc_opus_variable_complexity
    Current value (from the default) = false
      From //webrtc.gni:97

    Enable this to let the Opus audio codec change complexity on the fly.

rtc_prefer_fixed_point
    Current value (from the default) = true
      From //webrtc.gni:153

rtc_rusty_base64
    Current value (from the default) = false
      From //webrtc.gni:321

    Enables an experimental rust version of base64 for building and testing.

rtc_sanitize_coverage
    Current value (from the default) = ""
      From //webrtc.gni:148

    Set to "func", "block", "edge" for coverage generation.
    At unit test runtime set UBSAN_OPTIONS="coverage=1".
    It is recommend to set include_examples=0.
    Use llvm's sancov -html-report for human readable reports.
    See http://clang.llvm.org/docs/SanitizerCoverage.html .

rtc_ssl_root
    Current value (from the default) = ""
      From //webrtc.gni:105

    Used to specify an external OpenSSL include path when not compiling the
    library that comes with WebRTC (i.e. rtc_build_ssl == 0).

rtc_strict_field_trials
    Current value (from the default) = ""
      From //webrtc.gni:242

    When enabled, a run-time check will make sure that all field trial keys have
    been registered in accordance with the field trial policy, see
    g3doc/field-trials.md. The value can be set to the following:
   
     "dcheck": RTC_DCHECKs that the field trial has been registered. RTC_DCHECK
               must be enabled separately.
   
     "warn": RTC_LOGs a message with LS_WARNING severity if the field trial
             hasn't been registered.

rtc_system_openh264
    Current value (from the default) = false
      From //webrtc.gni:180

    Use system OpenH264

rtc_use_absl_mutex
    Current value (from the default) = false
      From //webrtc.gni:190

    Enable this flag to make webrtc::Mutex be implemented by absl::Mutex.

rtc_use_dummy_audio_file_devices
    Current value (from the default) = false
      From //webrtc.gni:194

    By default, use normal platform audio support or dummy audio, but don't
    use file-based audio playout and record.

rtc_use_h264
    Current value (from the default) = false
      From //webrtc.gni:176

rtc_use_h265
    Current value (from the default) = false
      From //webrtc.gni:186

rtc_use_perfetto
    Current value (from the default) = false
      From //webrtc.gni:259

    When true, include the Perfetto library.

rtc_use_pipewire
    Current value (from the default) = false
      From //webrtc.gni:131

    Set this to use PipeWire on the Wayland display server.
    By default it's only enabled on desktop Linux (excludes ChromeOS) and
    only when using the sysroot as PipeWire is not available in older and
    supported Ubuntu and Debian distributions.

rtc_use_x11
    Current value (from the default) = false
      From //webrtc.gni:125

    Set this to false to skip building code that requires X11.

rtc_use_x11_extensions
    Current value (from the default) = false
      From //webrtc.gni:305

    Set this to false to skip building code that also requires X11 extensions
    such as Xdamage, Xfixes.

rtc_video_psnr
    Current value (from the default) = true
      From //webrtc.gni:324

    Enables PSNR calculation for video getStats.

rtc_win_undef_unicode
    Current value (from the default) = false
      From //webrtc.gni:223

    When set to true and in a standalone build, it will undefine UNICODE and
    _UNICODE (which are always defined globally by the Chromium Windows
    toolchain).
    This is only needed for testing purposes, WebRTC wants to be sure it
    doesn't assume /DUNICODE and /D_UNICODE but that it explicitly uses
    wide character functions.

running_modularize
    Current value (from the default) = false
      From //build/config/c++/modules.gni:10

    Set to true when being run by build/modules/modularize.py

rust_bindgen_root
    Current value (from the default) = "//third_party/rust-toolchain"
      From //build/config/rust.gni:64

    Directory under which to find `bin/bindgen` (a `bin` directory containing
    the bindgen exectuable).

rust_force_head_revision
    Current value (from the default) = false
      From //build/toolchain/toolchain.gni:21

    Equivalent to llvm_force_head_revision, but for rust. When true, we expect
    to find a locally-build version of rust rather than the version specified
    in //tools/clang/scripts/update.py.

rust_sysroot_absolute
    Current value (from the default) = ""
      From //build/config/rust.gni:60

    Chromium provides a Rust toolchain in //third_party/rust-toolchain.
   
    To use a custom toolchain instead, specify an absolute path to the root of
    a Rust sysroot, which will have a 'bin' directory and others. Commonly
    <home dir>/.rustup/toolchains/nightly-<something>-<something>
    Using a custom toolchain should work, but this build configuration is
    community-supported and not tested by Chromium CQ.  In particular, Chromium
    build may depend on using a *nightly* version of Rust toolchain (see
    `tools/rust/unstable_rust_feature_usage.md`) although other projects (e.g. V8)
    may be compatible with the stable version of Rust toolchain.

rustc_version
    Current value (from the default) = ""
      From //build/config/rust.gni:70

    If you're using a Rust toolchain as specified by rust_sysroot_absolute,
    set this to the output of `rustc -V`. Changing this string will cause all
    Rust targets to be rebuilt, which allows you to update your toolchain and
    not break incremental builds.

sample_profile_is_accurate
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:144

    Whether we should consider the profile we're using to be accurate. Accurate
    profiles have the benefit of (potentially substantial) binary size
    reductions, by instructing the compiler to optimize cold and uncovered
    functions heavily for size. This often comes at the cost of performance.

sanitizer_coverage_allowlist
    Current value (from the default) = ""
      From //build/config/sanitizers/sanitizers.gni:113

    A sanitizer coverage allowlist, specifying exactly which
    files or symbol names should be instrumented, rather than all of them.

sanitizer_coverage_flags
    Current value (from the default) = ""
      From //build/config/sanitizers/sanitizers.gni:109

    Value for -fsanitize-coverage flag. Setting this causes
    use_sanitizer_coverage to be enabled.
    This flag is not used for libFuzzer (use_libfuzzer=true). Instead, we use:
        -fsanitize=fuzzer-no-link
    Default value when unset and use_fuzzing_engine=true:
        trace-pc-guard
    Default value when unset and use_sanitizer_coverage=true:
        trace-pc-guard,indirect-calls

save_reproducers_on_lld_crash
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:185

    If true, linker crashes will be rerun with `--reproduce` which causes
    a reproducer file to be saved.

show_includes
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:176

    Enable -H, which prints the include tree during compilation.
    For use by tools/clang/scripts/analyze_includes.py

simple_template_names
    Current value (from the default) = true
      From //build/config/compiler/BUILD.gn:204

    Use DWARF simple template names, with the following exceptions:
   
    * Windows is not supported as it doesn't use DWARF.
    * Apple platforms (e.g. MacOS, iPhone, iPad) aren't supported because xcode
      lldb doesn't have the needed changes yet.
    TODO(crbug.com/40244196): Remove if the upstream default ever changes.
   
    This greatly reduces the size of debug builds, at the cost of
    debugging information which is required by some specialized
    debugging tools.

skip_buildtools_check
    Current value (from the default) = false
      From //third_party/perfetto/gn/perfetto.gni:424

    Skip buildtools dependency checks (needed for ChromeOS).

strip_debug_info
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:78

    Android-only: Strip the debug info of libraries within lib.unstripped to
    reduce size. As long as symbol_level > 0, this will still allow stacks to be
    symbolized.

symbol_level
    Current value = 2
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:7
    Overridden from the default = -1
      From //build/config/compiler/compiler.gni:73

    How many symbols to include in the build. This affects the performance of
    the build since the symbols are large and dealing with them is slow.
      2 means regular build with symbols.
      1 means minimal symbols, usually enough for backtraces only. Symbols with
    internal linkage (static functions or those in anonymous namespaces) may not
    appear when using this level.
      0 means no symbols.
      -1 means auto-set according to debug/release and platform.

sysroot
    Current value (from the default) = ""
      From //build/config/sysroot.gni:20

    The path of the sysroot for the current toolchain. If empty, default
    sysroot is used.

system_headers_in_deps
    Current value (from the default) = false
      From //build/toolchain/toolchain.gni:40

    Use -MD instead of -MMD for compiler commands. This is useful for tracking
    the comprehensive set of dependencies.  It's also required when building
    without the sysroot so that updates to system header files trigger a
    rebuild (when using the sysroot, the CR_SYSROOT_KEY define takes care of
    this already).

system_libdir
    Current value (from the default) = "lib"
      From //build/config/linux/pkg_config.gni:50

    CrOS systemroots place pkgconfig files at <systemroot>/usr/share/pkgconfig
    and one of <systemroot>/usr/lib/pkgconfig or <systemroot>/usr/lib64/pkgconfig
    depending on whether the systemroot is for a 32 or 64 bit architecture.
   
    When build under GYP, CrOS board builds specify the 'system_libdir' variable
    as part of the GYP_DEFINES provided by the CrOS emerge build or simple
    chrome build scheme. This variable permits controlling this for GN builds
    in similar fashion by setting the `system_libdir` variable in the build's
    args.gn file to 'lib' or 'lib64' as appropriate for the target architecture.

system_webview_apk_target
    Current value (from the default) = "//android_webview:system_webview_64_apk"
      From //build/config/android/config.gni:317

target_cpu
    Current value = "arm64"
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:3
    Overridden from the default = ""
      (Internally set; try `gn help target_cpu`.)

target_environment
    Current value (from the default) = ""
      From //build/config/apple/mobile_config.gni:10

    Configure the environment for which to build. Could be either "device",
    "simulator" or "catalyst". Must be specified.

target_os
    Current value = "android"
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:1
    Overridden from the default = ""
      (Internally set; try `gn help target_os`.)

target_platform
    Current value (from the default) = "iphoneos"
      From //build/config/apple/mobile_config.gni:16

    Valid values: "iphoneos" (default), "tvos", "watchos".
    Indicates the kind of iOS or iOS-based platform that is being targeted.
    Note that this value is available only when is_ios is also true (i.e. it
    cannot be used with the host toolchain).

target_rpath
    Current value (from the default) = ""
      From //build/config/cast.gni:16

    If non empty, rpath of executables is set to this.
    If empty, default value is used.

target_sysroot
    Current value (from the default) = ""
      From //build/config/sysroot.gni:13

    The path of the sysroot that is applied when compiling using the target
    toolchain.

target_sysroot_dir
    Current value (from the default) = "//build/linux"
      From //build/config/sysroot.gni:16

    The path to directory containing linux sysroot images.

temporal_pgo_profile
    Current value (from the default) = false
      From //build/config/compiler/pgo/pgo.gni:41

    Whether to enable temporal pgo or not (experimental).

tests_have_location_tags
    Current value (from the default) = true
      From //testing/test.gni:22

    Some component repos (e.g. ANGLE) import //testing but do not have
    "location_tags.json", and so we don't want to try and upload the tags
    for their tests.
    And, some build configs may simply turn off generation altogether.

tflite_target_types
    Current value (from the default) = ["_standalone"]
      From //third_party/tflite/tflite_target.gni:64

thin_lto_enable_cache
    Current value (from the default) = true
      From //build/config/compiler/BUILD.gn:131

    Whether to enable thin lto incremental builds.
    See: https://clang.llvm.org/docs/ThinLTO.html#incremental
    The cache can lead to non-determinism: https://crbug.com/1486045

thin_lto_enable_optimizations
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:125

toolchain_allows_use_partition_alloc_as_malloc
    Current value (from the default) = true
      From //build/toolchain/toolchain.gni:33

    If false, the toolchain overrides `use_partition_alloc_as_malloc` in
    PartitionAlloc, to allow use of the system allocator.

toolchain_for_rust_host_build_tools
    Current value (from the default) = false
      From //build/toolchain/toolchain.gni:29

    Whether this toolchain is to be used for building host tools that are
    consumed during the build process. That includes proc macros and Cargo build
    scripts.

toolchain_supports_rust_thin_lto
    Current value (from the default) = true
      From //build/config/rust.gni:78

    Whether artifacts produced by the Rust compiler can participate in ThinLTO.
   
    One important consideration is whether the linker uses the same LLVM
    version as `rustc` (i.e. if it can understand the LLVM-IR from the
    compilation artifacts produced by `rustc`).  For more context please see
    older bugs like b/299483903, https://crbug.com/40281834, or b/300937673.

toolkit_views
    Current value (from the default) = false
      From //build/config/ui.gni:33

    True means the UI is built using the "views" framework.

treat_warnings_as_errors
    Current value = false
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:14
    Overridden from the default = true
      From //build/config/compiler/compiler.gni:54

    Default to warnings as errors for default workflow, where we catch
    warnings with known toolchains. Allow overriding this e.g. for Chromium
    builds on Linux that could use a different version of the compiler.
    With GCC, warnings in no-Chromium code are always not treated as errors.

update_android_aar_prebuilts
    Current value (from the default) = false
      From //build/config/android/config.gni:273

    When true, updates all android_aar_prebuilt() .info files during gn gen.
    Refer to android_aar_prebuilt() for more details.

use_afl
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:85

    Compile for fuzzing with AFL.

use_aura
    Current value (from the default) = false
      From //build/config/ui.gni:28

    Indicates if Aura is enabled. Aura is a low-level windowing library, sort
    of a replacement for GDI or GTK.

use_autogenerated_modules
    Current value (from the default) = true
      From //build/config/c++/modules.gni:19

    It's a nontrivial switch, with differing APIs, so it's somewhat difficult
    to migrate over one platform at a time.
    In general, this should be true unless we have specifically supported
    a platform with manual modules and are in the process of migrating.
    Eg. linux arm64 and windows were never supported with manual clang modules,
    so they should use_autogenerated_modules = true despite not having
    autogenerated modules because they're guarded by use_clang_modules.

use_blink
    Current value (from the default) = true
      From //build/config/features.gni:41

use_centipede
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:82

    Compile for fuzzing with centipede.
    See https://github.com/google/centipede

use_cfi_cast
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:136

    Enable checks for bad casts: derived cast and unrelated cast.
    TODO(krasin): remove this, when we're ready to add these checks by default.
    https://crbug.com/626794

use_cfi_diag
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:70

    Print detailed diagnostics when Control Flow Integrity detects a violation.

use_cfi_icall
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:67

use_cfi_recover
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:74

    Let Control Flow Integrity continue execution instead of crashing when
    printing diagnostics (use_cfi_diag = true).

use_clang_coverage
    Current value (from the default) = false
      From //build/config/coverage/coverage.gni:23

use_clang_modules
    Current value (from the default) = true
      From //build/config/clang/clang.gni:96

    Clang modules doesn't work with translation_unit used in codesearch
    pipeline http://b/436082487.

use_clang_profiling
    Current value (from the default) = false
      From //build/config/profiling/profiling.gni:10

use_clang_profiling_inside_sandbox
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:280

use_custom_libcxx
    Current value = false
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:5
    Overridden from the default = true
      From //build/config/c++/c++.gni:17

    Use in-tree libc++ (buildtools/third_party/libc++ and
    buildtools/third_party/libc++abi) instead of the system C++ library for C++
    standard library support.
   
    WARNING: Bringing your own C++ standard library is deprecated and will not
    be supported in the future. This flag will be removed.

use_custom_libcxx_for_host
    Current value (from the default) = true
      From //build/config/c++/c++.gni:28

    Use libc++ instead of stdlibc++ when using the host_cpu toolchain, even if
    use_custom_libcxx is false. This is useful for cross-compiles where a custom
    toolchain for the target_cpu has been set as the default toolchain, but
    use_custom_libcxx should still be true when building for the host.  The
    expected usage is to set use_custom_libcxx=false and
    use_custom_libcxx_for_host=true in the passed in buildargs.
   
    WARNING: Bringing your own C++ standard library is deprecated and will not
    be supported in the future. This flag will be removed.

use_custom_libunwind
    Current value (from the default) = true
      From //build/config/unwind.gni:6

    Use in-tree libunwind (buildtools/third_party/libunwind) instead of whatever
    system library provides unwind symbols (e.g. libgcc).

use_cxx23
    Current value = false
      From //.gn:99
    Overridden from the default = false
      From //build/config/compiler/compiler.gni:45

    If false, build using C++20; otherwise, build using C++23.

use_dbus
    Current value (from the default) = false
      From //build/config/features.gni:37

use_debug_fission
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:87

use_dummy_lastchange
    Current value (from the default) = true
      From //build/util/lastchange.gni:9

use_dwarf5
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:164

    Enable DWARF v5.

use_errorprone_java_compiler
    Current value (from the default) = false
      From //build/config/android/config.gni:269

    Set to false to disable the Errorprone compiler.

use_external_fuzzing_engine
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:91

    Compile for fuzzing with an external engine (e.g., Grammarinator).

use_full_pdb_paths
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:172

    Override this to put full paths to PDBs in Windows PE files. This helps
    windbg and Windows Performance Analyzer with finding the PDBs in some local-
    build scenarios. This is never needed for bots or official builds. Because
    this puts the output directory in the DLLs/EXEs it breaks build determinism.
    Bugs have been reported to the windbg/WPA teams and this workaround will be
    removed when they are fixed.

use_fuzzilli
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:88

    Compile for fuzzing with Fuzzilli.

use_fuzztest_wrapper
    Current value = false
      From //.gn:85
    Overridden from the default = true
      From //testing/test.gni:27

    Build individual_fuzztest_wrapper if use_fuzztest_wrapper is set.
    Some projects doesn't have //base and cannot build
    individual_fuzztest_wrapper.

use_ghash
    Current value (from the default) = true
      From //build/config/compiler/BUILD.gn:115

    Turn this on to use ghash feature of lld for faster debug link on Windows.
    http://blog.llvm.org/2018/01/improving-link-time-on-windows-with.html

use_gio
    Current value (from the default) = false
      From //build/config/features.gni:39

use_glib
    Current value (from the default) = false
      From //build/config/ui.gni:36

use_hashed_jni_names
    Current value (from the default) = false
      From //third_party/jni_zero/jni_zero.gni:27

use_icf
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:215

    Set to true to use icf, Identical Code Folding.

use_jacoco_coverage
    Current value (from the default) = false
      From //build/config/coverage/coverage.gni:27

    Enables JaCoCo Java code coverage.

use_javascript_coverage
    Current value (from the default) = false
      From //build/config/coverage/coverage.gni:39

    Enables TypeScript/JavaScript code coverage.

use_libfuzzer
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:78

    Compile for fuzzing with LLVM LibFuzzer.
    See http://www.chromium.org/developers/testing/libfuzzer

use_libinput
    Current value (from the default) = false
      From //build/config/chromeos/args.gni:37

    Determines if we're willing to link against libinput

use_libjpeg_turbo
    Current value (from the default) = true
      From //third_party/libjpeg.gni:11

    Uses libjpeg_turbo as the jpeg implementation. Has no effect if
    use_system_libjpeg is set.

use_lld
    Current value (from the default) = true
      From //build/config/compiler/compiler.gni:33

    Set to true to use lld, the LLVM linker.
    In late bring-up on macOS (see docs/mac_lld.md).
    Tentatively used on iOS.
    The default linker everywhere else.

use_llvm_libatomic
    Current value (from the default) = true
      From //build/config/c++/c++.gni:51

    Build atomic support from in-tree compiler-rt.
   
    Apple platforms provide the intrinsics from a different library, and the
    implementations in compiler-rt are outdated, so avoid building them in this
    case.

use_locally_built_instrumented_libraries
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:53

    Use dynamic libraries instrumented by one of the sanitizers instead of the
    standard system libraries. Set this flag to build the libraries from source.

use_ml_inliner
    Current value (from the default) = true
      From //build/config/compiler/compiler.gni:138

    Set to true to enable using the ML inliner in LLVM. This currently only
    enables the ML inliner when targeting Android.
    Currently the ML inliner is only supported on linux hosts

use_order_profiling
    Current value (from the default) = false
      From //build/config/android/abi.gni:16

    Adds intrumentation to each function. Writes a file with the order that
    functions are called at startup.

use_ozone
    Current value (from the default) = false
      From //build/config/ozone.gni:11

    Indicates if Ozone is enabled. Ozone is a low-level library layer for Linux
    that does not require X11.

use_profi
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:181

    Enable Profi algorithm. Profi can infer block and edge counts.
    https://clang.llvm.org/docs/UsersManual.html#using-sampling-profilers
    TODO(crbug.com/1375958i:) Possibly enable this for Android too.

use_reclient
    Current value (from the default) = false
      From //build/toolchain/rbe.gni:31

    Set to true to use re-client with ninja.

use_relative_vtables_abi
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:149

    Use offsets rather than pointers in vtables in order to reduce the number of
    relocations. This is safe to enable only when all C++ code is built with the
    flag set to the same value.

use_remoteexec
    Current value (from the default) = false
      From //build/toolchain/rbe.gni:26

    Set to true to enable remote executions.

use_rts
    Current value (from the default) = false
      From //build/config/rts.gni:6

    Regression Test Selection (RTS) will use a ML model
    to predict what test cases can be excluded from a
    given test suite based on historical data when using
    a .filter file.

use_rtti
    Current value = true
      From /home/zhouxin/work/webrtc/build/android/debug/args.gn:8
    Overridden from the default = false
      From //build/config/compiler/BUILD.gn:73

    Build with C++ RTTI enabled. Chromium builds without RTTI by default,
    but some sanitizers are known to require it, like CFI diagnostics
    and UBsan variants.

use_safe_libstdcxx
    Current value (from the default) = false
      From //build/config/c++/c++.gni:72

    In case the C++ standard library implementation used is libstdc++, then
    enable its own hardening checks. As we cannot determine in GN if libstdc++
    is used or not, by default enable it for Linux without the custom libc++.
   
    WARNING: Bringing your own C++ standard library is deprecated and will not
    be supported in the future. This flag will be removed.

use_sanitizer_configs_without_instrumentation
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:118

    When enabled, only relevant sanitizer defines are set, but compilation
    happens with no extra flags. This is useful when in component build
    enabling sanitizers only in some of the components.

use_sanitizer_coverage
    Current value (from the default) = false
      From //build/config/sanitizers/sanitizers.gni:189

use_siso
    Current value = true
      From //.gn:96
    Overridden from the default = false
      From //build/toolchain/siso.gni:21

    Placeholder to allow having use_siso in args.gn file.
    Explicit `use_siso` in args.gn can override default.
    This is used only for autoninja (to dispatch siso or ninja),
    and for use_reclient's default.

use_sized_deallocation
    Current value (from the default) = true
      From //build/config/compiler/compiler.gni:156

    Enable C++14 sized global deallocation functions.
    Enable only on ASan or platforms without strict binary size requirements
    because of binary size increase.
    TODO(crbug.com/345541122): investigate the fuchsia binary size increase.

use_stable_package_name_for_trichrome
    Current value (from the default) = false
      From //build/config/chrome_build.gni:44

    By default, Trichrome channels are compiled using separate package names.
    Set this to 'true' to compile Trichrome channels using the Stable channel's
    package name. This currently only affects builds with `android_channel =
    "beta"`.

use_sysroot
    Current value (from the default) = true
      From //build/toolchain/sysroot.gni:11

use_system_freetype
    Current value (from the default) = false
      From //build/config/freetype/freetype.gni:15

    Blink needs a recent and properly build-configured FreeType version to
    support OpenType variations, color emoji and avoid security bugs. By default
    we ship and link such a version as part of Chrome. For distributions that
    prefer to keep linking to the version the system, FreeType must be newer
    than version 2.7.1 and have color bitmap support compiled in. WARNING:
    System FreeType configurations other than as described WILL INTRODUCE TEXT
    RENDERING AND SECURITY REGRESSIONS.

use_system_harfbuzz
    Current value (from the default) = false
      From //third_party/harfbuzz-ng/harfbuzz.gni:11

    Blink uses a cutting-edge version of Harfbuzz; most Linux distros do not
    contain a new enough version of the code to work correctly. However,
    ChromeOS chroots (i.e, real ChromeOS builds for devices) do contain a
    new enough version of the library, and so this variable exists so that
    ChromeOS can build against the system lib and keep binary sizes smaller.

use_system_libjpeg
    Current value (from the default) = false
      From //third_party/libjpeg.gni:7

    Uses system libjpeg. If true, overrides use_libjpeg_turbo.

use_text_section_splitting
    Current value (from the default) = false
      From //build/config/compiler/BUILD.gn:161

    This argument is to control whether enabling text section splitting in the
    final binary. When enabled, the separated text sections with prefix
    '.text.hot', '.text.unlikely', '.text.startup' and '.text.exit' will not be
    merged to '.text' section. This allows us to identify the hot code section
    ('.text.hot') in the binary, which allows our data collection pipelines to
    more easily identify code that we assume to be hot/cold that doesn't turn
    out to be such in the field.

use_thin_lto
    Current value (from the default) = false
      From //build/config/compiler/compiler.gni:95

use_udev
    Current value (from the default) = false
      From //build/config/features.gni:35

    libudev usage. This currently only affects the content layer.

using_mismatched_sample_profile
    Current value (from the default) = true
      From //build/config/compiler/compiler.gni:105

    Whether we're using a sample profile collected on an architecture different
    than the one we're compiling for.
   
    It's currently not possible to collect AFDO profiles on anything but
    x86{,_64}.

v8_current_cpu
    Current value (from the default) = "arm64"
      From //build/config/v8_target_cpu.gni:63

    This argument is declared here so that it can be overridden in toolchains.
    It should never be explicitly set by the user.

v8_target_cpu
    Current value (from the default) = ""
      From //build/config/v8_target_cpu.gni:33

    This arg is used when we want to tell the JIT-generating v8 code
    that we want to have it generate for an architecture that is different
    than the architecture that v8 will actually run on; we then run the
    code under an emulator. For example, we might run v8 on x86, but
    generate arm code and run that under emulation.
   
    This arg is defined here rather than in the v8 project because we want
    some of the common architecture-specific args (like arm_float_abi or
    mips_arch_variant) to be set to their defaults either if the current_cpu
    applies *or* if the v8_current_cpu applies.
   
    As described below, you can also specify the v8_target_cpu to use
    indirectly by specifying a `custom_toolchain` that contains v8_$cpu in the
    name after the normal toolchain.
   
    For example, `gn gen --args="custom_toolchain=...:clang_x64_v8_arm64"`
    is equivalent to setting --args=`v8_target_cpu="arm64"`. Setting
    `custom_toolchain` is more verbose but makes the toolchain that is
    (effectively) being used explicit.
   
    v8_target_cpu can only be used to target one architecture in a build,
    so if you wish to build multiple copies of v8 that are targeting
    different architectures, you will need to do something more
    complicated involving multiple toolchains along the lines of
    custom_toolchain, above.

zlib_symbols_visible
    Current value (from the default) = false
      From //third_party/zlib/BUILD.gn:11

    Expose zlib's symbols, used by Node.js to provide zlib APIs for its native
    modules.

