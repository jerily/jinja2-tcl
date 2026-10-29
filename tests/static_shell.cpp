#include "library.h"

static int app_init(Tcl_Interp *interp) {
    if (Tcl_Init(interp) != TCL_OK || Jinja2_Init(interp) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "Jinja2", Jinja2_Init, nullptr);
    return TCL_OK;
}

int main(int argc, char **argv) {
#if TCL_MAJOR_VERSION >= 9
    // Tcl 9 may embed its script library in the shared library's ZIP filesystem.
    TclZipfs_AppHook(&argc, &argv);
#endif
    Tcl_Main(argc, argv, app_init);
    return 0;
}
