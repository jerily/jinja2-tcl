/* Copyright (c) 2026 Neofytos Dimitriou. SPDX-License-Identifier: MIT */
#ifndef JINJA2_TCL_LIBRARY_H
#define JINJA2_TCL_LIBRARY_H

#include <tcl.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BUILD_jinja2
#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT
#endif
EXTERN int Jinja2_Init(Tcl_Interp *interp);

#ifdef __cplusplus
}
#endif
#endif
