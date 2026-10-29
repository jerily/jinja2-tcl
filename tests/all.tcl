package require tcltest 2

# CTest passes the build directory; an installed package can be tested without it.
if {[llength $argv] && ![string match -* [lindex $argv 0]]} {
    lappend auto_path [file normalize [lindex $argv 0]]
    set argv [lrange $argv 1 end]
}
package require jinja2 0.1.0
::tcltest::configure {*}$argv -singleproc true -testdir [file dirname [info script]]
exit [::tcltest::runAllTests]
