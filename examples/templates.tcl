package require jinja2 0.1.0

set templates [dict create \
    layout.html {<html><body>{% block content %}{% endblock %}</body></html>} \
    parts/greeting.html {<h1>Hello {{ name }}</h1>}]
set template {{% extends 'layout.html' %}{% block content %}{% include 'parts/greeting.html' %}{% endblock %}}
::jinja2::validate $template -templates $templates -path greeting.html
puts [::jinja2::render $template {{"name":"Neo"}} -templates $templates -path greeting.html]
