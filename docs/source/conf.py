# Configuration file for the Sphinx documentation builder.

import os
import sys

# Add the Python API path
sys.path.insert(0, os.path.abspath("../../"))

# -- Project information -----------------------------------------------------
project = "FastSlide"
html_title = "FastSlide Documentation"
copyright = "2025, AI for Oncology"
author = "AI for Oncology"
release = "0.1.0"
version = "2.0"

# -- General configuration ---------------------------------------------------
extensions = [
    "sphinx.ext.autodoc",
    "sphinx.ext.autosummary",
    "sphinx.ext.viewcode",
    "sphinx.ext.napoleon",
    "sphinx.ext.intersphinx",
    "sphinx.ext.mathjax",
    "sphinx.ext.githubpages",
    "sphinx.ext.extlinks",
    "sphinx.ext.inheritance_diagram",
    "sphinx.ext.graphviz",
    "breathe",
    "myst_parser",
    "sphinx_design",
    "sphinx_copybutton",
    "sphinx_inline_tabs",
    "sphinxcontrib.mermaid",
    "sphinxext.opengraph",
    "sphinx_autodoc_typehints",
]

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

# Suppress specific warnings
suppress_warnings = [
    "autodoc.import_object",  # Suppress autodoc import errors
    "autodoc.mocked_object",  # Suppress mocked object warnings (expected)
    "autodoc",  # Suppress other autodoc warnings for missing module
    "ref.ref",  # Suppress undefined label warnings
    "ref.doc",  # Suppress unknown document warnings
    "toc.not_readable",  # Suppress toctree not readable warnings
]

# Don't be strict about nitpicky warnings
nitpicky = False

# -- Read the Docs Theme Configuration ----------------------------------------
html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "logo_only": False,
    "prev_next_buttons_location": "bottom",
    "style_external_links": True,
    "vcs_pageview_mode": "",
    "style_nav_header_background": "#2980B9",
    # Toc options
    "collapse_navigation": False,
    "sticky_navigation": True,
    "navigation_depth": 4,
    "includehidden": True,
    "titles_only": False,
}

# Add custom CSS and JS
html_static_path = ["_static"]
html_css_files = ["custom.css"]
html_js_files = []

# Logo configuration
html_logo = None  # Add your logo path here if you have one
html_favicon = None  # Add your favicon path here if you have one

# -- Breathe Configuration (Enhanced) ---------------------------------------
# For Make builds: ../build/doxygen/xml
# For Bazel builds: _doxygen/xml (provided via sphinx_docs_library)
import os

if os.path.exists(os.path.join(os.path.dirname(__file__), "_doxygen", "xml")):
    breathe_projects = {"fastslide": "_doxygen/xml"}
else:
    breathe_projects = {"fastslide": "../build/doxygen/xml"}
breathe_default_project = "fastslide"
breathe_default_members = ("members", "undoc-members", "protected-members")
breathe_show_enumvalue_initializer = True
breathe_show_define_initializer = True
breathe_show_include = True
breathe_order_parameters_first = True

# Enhanced Breathe formatting with inheritance diagrams
breathe_implementation_filename_extensions = [".cpp", ".cc", ".cxx"]
breathe_use_project_refids = True
breathe_separate_member_pages = False
breathe_domain_by_extension = {"h": "cpp", "hpp": "cpp"}

# Enable inheritance diagrams in Breathe
breathe_show_inheritance_diagram = True
breathe_default_include_files = True

# Don't fail on Breathe/Doxygen warnings
breathe_debug_trace_directives = False
breathe_debug_trace_doxygen_ids = False
breathe_debug_trace_qualification = False

# Add "View in Doxygen" links to all Breathe directives
breathe_doxygen_config_options = {}
breathe_doxygen_url = "../doxygen/"

# -- Inheritance Diagram Configuration --------------------------------------
inheritance_graph_attrs = dict(rankdir="TB", size='"6.0, 8.0"', fontsize=14, ratio="compress")
inheritance_node_attrs = dict(shape="rect", fontsize=14, color="#667eea", style="filled,rounded", fillcolor="#f0f4ff")
inheritance_edge_attrs = dict(penwidth=2, color="#667eea")

# -- Graphviz Configuration ---------------------------------------------
graphviz_output_format = "svg"
graphviz_dot_args = [
    "-Nfontname=Inter",
    "-Nfontsize=12",
    '-Ncolor="#667eea"',
    '-Nstyle="filled,rounded"',
    '-Nfillcolor="#f0f4ff"',
    "-Efontname=Inter",
    "-Efontsize=10",
    '-Ecolor="#764ba2"',
    "-Gfontname=Inter",
    "-Gfontsize=14",
    "-Grankdir=TB",
    "-Gbgcolor=transparent",
    "-Gmargin=0.2",
]

# -- External Links ----------------------------------------------------------
extlinks = {
    "doxygen": ("../doxygen/%s.html", "%s"),
    "github": ("https://github.com/AI-for-Oncology/fastslide/%s", "%s"),
    "issue": ("https://github.com/AI-for-Oncology/fastslide/issues/%s", "Issue %s"),
}

# -- Copy Button Configuration -----------------------------------------------
copybutton_prompt_text = r">>> |\.\.\. |\$ |In \[\d*\]: | {2,5}\.\.\.: | {5,8}: "
copybutton_prompt_is_regexp = True
copybutton_only_copy_prompt_lines = True
copybutton_remove_prompts = True

# -- Napoleon settings (Enhanced) -------------------------------------------
napoleon_google_docstring = True
napoleon_numpy_docstring = True
napoleon_include_init_with_doc = False
napoleon_include_private_with_doc = False
napoleon_include_special_with_doc = True
napoleon_use_admonition_for_examples = True
napoleon_use_admonition_for_notes = True
napoleon_use_admonition_for_references = False
napoleon_use_ivar = False
napoleon_use_param = True
napoleon_use_rtype = True
napoleon_preprocess_types = False
napoleon_type_aliases = None
napoleon_attr_annotations = True

# -- AutoDoc settings (Enhanced) --------------------------------------------
autodoc_default_options = {
    "members": True,
    "member-order": "bysource",
    "special-members": "__init__",
    "undoc-members": True,
    "exclude-members": "__weakref__",
}
autodoc_typehints = "both"
autodoc_typehints_description_target = "documented"
autodoc_preserve_defaults = True
# Note: For Bazel builds, fastslide module is available via sphinx_build deps.
# For Make builds without the module installed, it will use the mock from suppress_warnings.

# -- MyST Parser settings (Enhanced) ----------------------------------------
myst_enable_extensions = [
    "deflist",
    "tasklist",
    "colon_fence",
    "fieldlist",
    "html_admonition",
    "html_image",
    "linkify",
    "replacements",
    "smartquotes",
    "strikethrough",
    "substitution",
    "attrs_inline",
    "attrs_block",
]

# -- Intersphinx mapping ---------------------------------------------------
intersphinx_mapping = {
    "python": ("https://docs.python.org/3", None),
    "numpy": ("https://numpy.org/doc/stable/", None),
    "torch": ("https://pytorch.org/docs/stable/", None),
    "sklearn": ("https://scikit-learn.org/stable/", None),
}

# -- OpenGraph settings -----------------------------------------------------
ogp_site_url = "https://docs.aifo.dev/fastslide/"
ogp_description_length = 200
ogp_image = "https://docs.aifo.dev/fastslide/_static/fastslide-social.png"
ogp_image_alt = "FastSlide - High-Performance Slide Reader"
ogp_type = "website"
ogp_custom_meta_tags = [
    '<meta name="twitter:card" content="summary_large_image">',
    '<meta name="twitter:site" content="@aiforoncology">',
]

# Source file parsers
source_suffix = {
    ".rst": None,
    ".md": "myst_parser",
}

# Master document
master_doc = "index"

# -- Custom HTML Context ---------------------------------------------------
html_context = {
    "display_github": True,
    "github_user": "NKI-AI",
    "github_repo": "fastslide",
    "github_version": "main",
    "conf_py_path": "/docs/source/",
    "doxygen_url": "../doxygen/",
}

# Create directories if needed
for path in ["_static", "_templates"]:
    if not os.path.exists(path):
        os.makedirs(path)
