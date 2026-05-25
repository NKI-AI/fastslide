"""Versioned py_wheel rule with Python version transitions.

The standard `py_wheel` rule only stamps the wheel tag (e.g. cp312) but does
NOT transition its dependency graph to actually compile native extensions
against the corresponding Python headers. This module provides
`versioned_py_wheel`, a macro that wraps `py_wheel` with a Starlark
configuration transition so that the entire dep tree — including cc_binary
targets from pybind_extension / cython_rules — is built against the correct
Python toolchain.

Usage in a BUILD file:

    load("//tools:versioned_py_wheel.bzl", "multi_version_py_wheel")

    multi_version_py_wheel(
        name_prefix = "my_wheel",
        python_versions = {
            "3.10": "cp310",
            "3.11": "cp311",
            "3.12": "cp312",
            "3.13": "cp313",
            "3.14": "cp314",
        },
        distribution = "my_package",
        deps = [":my_lib"],
        ...
    )
"""

load("@rules_python//python:packaging.bzl", "py_wheel")

def _python_version_transition_impl(_settings, attr):
    return {"@rules_python//python/config_settings:python_version": attr.python_version}

_python_version_transition = transition(
    implementation = _python_version_transition_impl,
    inputs = [],
    outputs = ["@rules_python//python/config_settings:python_version"],
)

def _versioned_py_wheel_rule_impl(ctx):
    inner = ctx.attr.wheel[0]
    return [DefaultInfo(files = inner[DefaultInfo].files)]

_versioned_py_wheel_rule = rule(
    implementation = _versioned_py_wheel_rule_impl,
    attrs = {
        "wheel": attr.label(mandatory = True, cfg = _python_version_transition),
        "python_version": attr.string(mandatory = True),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def versioned_py_wheel(*, name, python_version, python_tag, visibility = None, **wheel_kwargs):
    """Creates a py_wheel whose entire dep graph is built for the given Python version.

    All keyword arguments except ``name``, ``python_version``, ``python_tag``,
    and ``visibility`` are forwarded verbatim to ``py_wheel``.

    Args:
        name: Target name for the versioned wheel.
        python_version: The major.minor Python version (e.g. "3.12").
        python_tag: The wheel python tag (e.g. "cp312").
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
        visibility = visibility,
    )

def multi_version_py_wheel(*, name_prefix, python_versions, default_version = "3.11", visibility = None, **wheel_kwargs):
    """Creates versioned_py_wheel targets for multiple Python versions at once.

    This macro generates one ``versioned_py_wheel`` target per entry in
    *python_versions*, plus an alias ``<name_prefix>`` pointing at the
    *default_version* wheel, plus ``all_wheels`` / per-version filegroups.

    Because it lives in a ``.bzl`` file, it can accept ``**wheel_kwargs``
    (which BUILD files cannot).

    Args:
        name_prefix: Prefix for target names (e.g. "dlup_wheel" →
            "dlup_wheel_cp312").
        python_versions: Dict mapping ``"major.minor"`` → ``"cpXYZ"`` tag,
            e.g. ``{"3.10": "cp310", "3.11": "cp311", ...}``.
        default_version: Which version the ``<name_prefix>`` alias points to.
        visibility: Bazel visibility for the generated targets.
        **wheel_kwargs: All remaining keyword arguments are forwarded to every
            ``py_wheel`` instantiation (distribution, deps, platform, …).
    """
    wheel_targets = []
    default_tag = None

    for version, tag in python_versions.items():
        target_name = "{}_{}".format(name_prefix, tag)
        versioned_py_wheel(
            name = target_name,
            python_version = version,
            python_tag = tag,
            visibility = visibility,
            **wheel_kwargs
        )
        wheel_targets.append(target_name)
        if version == default_version:
            default_tag = tag

    native.alias(
        name = name_prefix,
        actual = ":{}_{}".format(name_prefix, default_tag or python_versions.values()[0]),
        visibility = visibility,
    )

    native.filegroup(
        name = "all_wheels",
        srcs = [":{}".format(t) for t in wheel_targets],
        visibility = visibility,
    )

    for version, tag in python_versions.items():
        native.filegroup(
            name = "wheels_python{}".format(version.replace(".", "")),
            srcs = [":{}_{}".format(name_prefix, tag)],
            visibility = visibility,
        )
