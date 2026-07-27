"""Versioned py_wheel rule with a Python version + stable-ABI transition.

The standard `py_wheel` rule only stamps the wheel tag (e.g. cp312) but does
NOT transition its dependency graph to actually compile native extensions
against the corresponding Python toolchain. This module provides
`versioned_py_wheel`, a macro that wraps `py_wheel` with a Starlark
configuration transition so that the entire dep tree — including the
nanobind_extension cc_binary — is built against the correct Python toolchain
AND against the requested stable-ABI (abi3) level.

Pinning `@nanobind_bazel//:py-limited-api` in the transition (rather than only
in `.bazelrc`) makes the wheel correct even when fastslide is a non-root
dependency in a host workspace whose `.bazelrc` does not set the flag.

Usage in a BUILD file:

    load("//tools:versioned_py_wheel.bzl", "versioned_py_wheel")

    versioned_py_wheel(
        name = "my_wheel",
        python_version = "3.12",
        python_tag = "cp312",
        py_limited_api = "cp312",
        abi = "abi3",
        distribution = "my_package",
        deps = [":my_lib"],
        ...
    )
"""

load("@rules_python//python:packaging.bzl", "py_wheel")

def _python_version_transition_impl(_settings, attr):
    return {
        "@rules_python//python/config_settings:python_version": attr.python_version,
        "@nanobind_bazel//:py-limited-api": attr.py_limited_api,
    }

_python_version_transition = transition(
    implementation = _python_version_transition_impl,
    inputs = [],
    outputs = [
        "@rules_python//python/config_settings:python_version",
        "@nanobind_bazel//:py-limited-api",
    ],
)

def _versioned_py_wheel_rule_impl(ctx):
    inner = ctx.attr.wheel[0]
    return [DefaultInfo(files = inner[DefaultInfo].files)]

_versioned_py_wheel_rule = rule(
    implementation = _versioned_py_wheel_rule_impl,
    attrs = {
        "wheel": attr.label(mandatory = True, cfg = _python_version_transition),
        "python_version": attr.string(mandatory = True),
        "py_limited_api": attr.string(mandatory = True),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def versioned_py_wheel(
        *,
        name,
        python_version,
        python_tag,
        py_limited_api,
        visibility = None,
        **wheel_kwargs):
    """Creates a py_wheel whose dep graph is built for a Python version + stable ABI.

    All keyword arguments except ``name``, ``python_version``, ``python_tag``,
    ``py_limited_api``, and ``visibility`` are forwarded verbatim to ``py_wheel``.

    Args:
        name: Target name for the versioned wheel.
        python_version: The major.minor Python version to build against
            (e.g. "3.12").
        python_tag: The wheel python tag (e.g. "cp312").
        py_limited_api: The `@nanobind_bazel//:py-limited-api` value to pin for
            the extension build (e.g. "cp312").
        visibility: Bazel visibility for the outer target.
        **wheel_kwargs: Arguments forwarded to py_wheel.
    """
    inner_name = "_" + name + "_inner"

    py_wheel(
        name = inner_name,
        python_tag = python_tag,
        visibility = ["//visibility:private"],
        **wheel_kwargs
    )

    _versioned_py_wheel_rule(
        name = name,
        wheel = ":" + inner_name,
        python_version = python_version,
        py_limited_api = py_limited_api,
        visibility = visibility,
    )
