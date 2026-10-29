package require jinja2 0.1.0

set template {<h1>{{ title }}</h1><ul>{% for item in items %}<li>{{ item }}</li>{% endfor %}</ul>}
puts [::jinja2::render $template {{"title":"Hello & welcome","items":["Tcl","Jinja2"]}}]
