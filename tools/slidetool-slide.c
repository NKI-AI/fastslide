/*
 *  OpenSlide, a library for reading whole slide image files
 *
 *  Copyright (c) 2007-2012 Carnegie Mellon University
 *  Copyright (c) 2023      Benjamin Gilbert
 *  All rights reserved.
 *
 *  OpenSlide is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, version 2.1.
 *
 *  OpenSlide is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with OpenSlide. If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <glib.h>
#include "openslide.h"
#include "openslide-common.h"
#include "slidetool.h"

static int do_open(int narg, char **args) {
  int ret = 0;
  for (int i = 0; i < narg; i++) {
    const char *file = args[i];
    g_autoptr(openslide_t) osr = openslide_open(file);
    if (common_warn_on_error(osr, "%s", file)) {
      ret = 1;
    }
  }
  return ret;
}

static int do_vendor(int narg, char **args) {
  int ret = 0;
  for (int i = 0; i < narg; i++) {
    const char *file = args[i];
    const char *vendor = openslide_detect_vendor(file);
    if (vendor) {
      if (narg > 1) {
        printf("%s: %s\n", file, vendor);
      } else {
        printf("%s\n", vendor);
      }
    } else {
      common_warn("%s: No vendor detected", file);
      ret = 1;
    }
  }
  return ret;
}

static const struct command slide_subcmds[] = {
  {
    .name = "open",
    .parameter_string = "<FILE...>",
    .summary = "Try opening a slide",
    .description = "Check whether OpenSlide can open a slide.",
    .min_positional = 1,
    .handler = do_open,
  },
  {
    .name = "vendor",
    .parameter_string = "<FILE...>",
    .summary = "Get slide vendor",
    .description = "Print the detected OpenSlide vendor name for a slide.",
    .min_positional = 1,
    .handler = do_vendor,
  },
  {}
};

const struct command slide_cmd = {
  .name = "slide",
  .summary = "Commands related to slide files",
  .subcommands = slide_subcmds,
};
