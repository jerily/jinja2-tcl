package require tcltest 2

# CTest passes the build directory; an installed package can be tested without it.
if {[llength $argv] && ![string match -* [lindex $argv 0]]} {
    set build_dir [file normalize [lindex $argv 0]]
    set auto_path [linsert $auto_path 0 $build_dir]
    if {[file exists [file join $build_dir pkgIndex.tcl]]} {
        apply {{dir} {source [file join $dir pkgIndex.tcl]}} $build_dir
    }
    set argv [lrange $argv 1 end]
}
package require jinja2 0.1.0
::tcltest::configure {*}$argv -singleproc true -testdir [file dirname [info script]]
exit [::tcltest::runAllTests]
