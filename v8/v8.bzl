def _repo_path(repository_ctx):
    return repository_ctx.path(".")

def _get_depot_tools(repository_ctx):
    # Clone Chrome Depot Tools.
    git = repository_ctx.which("git")
    git_command = [git, "clone", "https://chromium.googlesource.com/chromium/tools/depot_tools.git"]
    repository_ctx.report_progress("Cloning depot_tools into %s with command %s..." % (_repo_path(repository_ctx), git_command))
    repository_ctx.execute(git_command, quiet = False, working_directory = "%s" % _repo_path(repository_ctx))
    repository_ctx.report_progress("Cloning depot_tools is complete")
    return repository_ctx.path("depot_tools")

def _gclient_metrics_opt_out(repository_ctx, depot_tools):
    repository_ctx.report_progress("Opting out of gclient metrics...")
    gclient = depot_tools.get_child("gclient")
    gclient_metrics_opt_out_command = [gclient, "metrics", "--opt-out"]
    repository_ctx.execute(gclient_metrics_opt_out_command, quiet = False)

def _fetch_v8_source(repository_ctx, depot_tools):
    # Fetch V8 source code.
    v8_destination_path = repository_ctx.path("v8")
    fetch = depot_tools.get_child("fetch")
    fetch_v8_command = [fetch, "v8"]
    repository_ctx.report_progress("Fetching v8 codebase into %s..." % v8_destination_path)
    repository_ctx.execute(fetch_v8_command, quiet = False, working_directory = "%s" % _repo_path(repository_ctx))
    repository_ctx.report_progress("Fetching v8 codebase is complete")
    return v8_destination_path

def _checkout_v8_branch(repository_ctx, v8_path):
    # Checkout V8 branch with the specified name
    git = repository_ctx.which("git")
    git_fetch_command = [git, "fetch", "origin", "refs/heads/%s" % repository_ctx.attr.branch]
    repository_ctx.execute(git_fetch_command, working_directory = "%s" % v8_path)
    git_checkout_command = [git, "checkout", "-b", "refs/heads/%s" % repository_ctx.attr.branch]
    repository_ctx.execute(git_checkout_command, working_directory = "%s" % v8_path)

def _gclient_sync(repository_ctx, depot_tools, v8_path):
    repository_ctx.report_progress("Executing gclient sync...")
    gclient = depot_tools.get_child("gclient")
    gclient_sync_command = [gclient, "sync"]
    repository_ctx.execute(gclient_sync_command, quiet = False, working_directory = "%s" % v8_path)

def _clear_bazel_build_files(repository_ctx, v8_path):
    # Remove all BUILD.bazel files throughout the V8 source tree.
    # We're not yet using Bazel to build V8, as Bazel support in V8 is still
    # very early and needs more work.
    bash = repository_ctx.which("bash")
    rm = repository_ctx.which("rm")
    find = repository_ctx.which("find")
    grep = repository_ctx.which("grep")
    xargs = repository_ctx.which("xargs")
    remove_build_bazel_command = [bash, "-c", "%s %s | %s BUILD.bazel | %s %s -f " % (find, v8_path, grep, xargs, rm)]
    repository_ctx.execute(remove_build_bazel_command, quiet = False, working_directory = "%s" % repository_ctx.path("."))

def _v8_sources_impl(repository_ctx):
    depot_tools = _get_depot_tools(repository_ctx)
    _gclient_metrics_opt_out(repository_ctx, depot_tools = depot_tools)
    v8_destination_path = _fetch_v8_source(repository_ctx, depot_tools = depot_tools)
    _checkout_v8_branch(repository_ctx, v8_path = v8_destination_path)
    _gclient_sync(repository_ctx, depot_tools = depot_tools, v8_path = v8_destination_path)
    _clear_bazel_build_files(repository_ctx, v8_path = v8_destination_path)

    # Write the build shell script.
    build_script = """
set -e
OUTPUT_DIR=$(readlink -f "$1")
SOURCE_ROOT=$(readlink -f $(dirname "$0"))/v8
DEPOT_TOOLS=$(readlink -f $(dirname "$0"))/depot_tools
# Configure and build V8 as a static library.
# Use local Linux toolchain, as opposed to the toolchain packaged with V8,
# to avoid dependencies on standard libraries packaged with V8.
export CC=gcc
export CXX=g++
export CXXFLAGS="-Wno-unknown-warning-option -Wno-implicit-int-float-conversion -Wno-builtin-assume-aligned-alignment -Wno-final-dtor-non-final-class"
export AR=${AR:-ar}
export NM=${NM:-nm}
export LD=${LD:-ld}
cd ${SOURCE_ROOT}
echo SOURCE_ROOT=${SOURCE_ROOT}
./buildtools/linux64/gn gen out/x64.static --args="custom_toolchain=\\"//build/toolchain/linux/unbundle:default\\" use_sysroot=false linux_use_bundled_binutils=false use_lld=false strip_debug_info=false symbol_level=0 use_allocator_shim=false is_cfi=false use_gold=false v8_static_library=true v8_enable_gdbjit=false v8_monolithic=true clang_use_chrome_plugins=false v8_enable_shared_ro_heap=false v8_use_external_startup_data=false is_debug=false v8_symbol_level=1 v8_enable_handle_zapping=false use_glib=false v8_use_external_startup_data=false v8_enable_i18n_support=false v8_enable_webassembly=false is_clang=false use_custom_libcxx=false"
${DEPOT_TOOLS}/ninja -C out/x64.static v8_monolith

# Copy V8 static library to the output location
cp out/x64.static/obj/libv8_monolith.a "${OUTPUT_DIR}/libv8_monolith.a"

# Copy V8 public headers to the output location.
cd include
find -L . -type f | grep -i \\.h$ | xargs -I {} cp --parents {} ${OUTPUT_DIR}
"""
    repository_ctx.file(repository_ctx.path("build_v8.sh"), build_script)

    # Write the Bazel BUILD file.
    v8_build_file = """
filegroup(
    name = "depot_tools",
    srcs = glob(["depot_tools/**/*"]),
)

V8_HEADERS = [
    "cppgc/allocation.h",
    "cppgc/common.h",
    "cppgc/cross-thread-persistent.h",
    "cppgc/custom-space.h",
    "cppgc/default-platform.h",
    "cppgc/ephemeron-pair.h",
    "cppgc/garbage-collected.h",
    "cppgc/heap-consistency.h",
    "cppgc/heap.h",
    "cppgc/internal/api-constants.h",
    "cppgc/internal/atomic-entry-flag.h",
    "cppgc/internal/caged-heap-local-data.h",
    "cppgc/internal/compiler-specific.h",
    "cppgc/internal/finalizer-trait.h",
    "cppgc/internal/gc-info.h",
    "cppgc/internal/logging.h",
    "cppgc/internal/name-trait.h",
    "cppgc/internal/persistent-node.h",
    "cppgc/internal/pointer-policies.h",
    "cppgc/internal/prefinalizer-handler.h",
    "cppgc/internal/write-barrier.h",
    "cppgc/liveness-broker.h",
    "cppgc/macros.h",
    "cppgc/member.h",
    "cppgc/name-provider.h",
    "cppgc/object-size-trait.h",
    "cppgc/persistent.h",
    "cppgc/platform.h",
    "cppgc/prefinalizer.h",
    "cppgc/source-location.h",
    "cppgc/trace-trait.h",
    "cppgc/type-traits.h",
    "cppgc/visitor.h",
    "libplatform/libplatform-export.h",
    "libplatform/libplatform.h",
    "libplatform/v8-tracing.h",
    "v8config.h",
    "v8-cppgc.h",
    "v8-fast-api-calls.h",
    "v8.h",
    "v8-inspector.h",
    "v8-inspector-protocol.h",
    "v8-internal.h",
    "v8-metrics.h",
    "v8-platform.h",
    "v8-profiler.h",
    "v8-unwinder-state.h",
    "v8-util.h",
    "v8-value-serializer-version.h",
    "v8-version.h",
    "v8-version-string.h",
    "v8-wasm-trap-handler-posix.h",
    "v8-wasm-trap-handler-win.h",
]

filegroup(
    name = "build_v8",
    srcs = ["build_v8.sh"],
)

genrule(
    name = "compile_v8",
    srcs = glob(["v8/**"], exclude = ["v8/tools/swarming_client/**"]) + [":depot_tools"],
    outs = [
        "libv8_monolith.a",
    ] + V8_HEADERS,
    cmd = "$(location //:build_v8) $(@D)",
    tools = [":build_v8"],
)

filegroup(
    name = "libv8",
    srcs = ["libv8_monolith.a"],
    data = [":compile_v8"],
)

filegroup(
    name = "v8_headers",
    srcs = V8_HEADERS,
)

cc_import(
    name = "v8",
    hdrs = [":v8_headers"],
    static_library = ":libv8",
    visibility = ["//visibility:public"],
)
"""
    repository_ctx.file(repository_ctx.path("BUILD.bazel"), v8_build_file)

v8_sources = repository_rule(
    implementation = _v8_sources_impl,
    local = True,
    attrs = {"branch": attr.string(mandatory = True)},
)
