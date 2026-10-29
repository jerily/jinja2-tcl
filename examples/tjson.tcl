package require jinja2 0.1.0
package require tjson

set context [::tjson::create [list M [list name {S Neo} items {L {{S Tcl} {S Jinja2}}}]]]
try {
    puts [::jinja2::render {Hello {{ name }}: {{ items|join(', ') }}} [::tjson::to_json $context]]
} finally {
    ::tjson::destroy $context
}
