#include "jinja2.h"

extern "C" {
#include <cJSON.h>
}

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "test_jinja2 failed: %s\n", message);
        std::exit(1);
    }
}

void require_render(const std::string &template_text,
                    const std::string &json,
                    const std::string &expected,
                    const char *message) {
    cJSON *root = cJSON_Parse(json.c_str());
    require(root != nullptr, "fixture JSON parses");
    const jinja2_tcl::engine::RenderResult result = jinja2_tcl::engine::render_template(template_text, root);
    cJSON_Delete(root);
    if (!result.ok) {
        std::fprintf(stderr, "render error: %s\n", result.error.c_str());
    }
    require(result.ok, message);
    require(result.output == expected, message);
}

struct InMemoryTemplateFileSystem final : jinja2_tcl::engine::TemplateFileSystem {
    std::map<std::string, std::string> files;

    bool read_file(const std::string &path, std::string &content_out, std::string &error_out) const override {
        const auto item = files.find(path);
        if (item == files.end()) {
            error_out = "missing test template";
            return false;
        }
        content_out = item->second;
        return true;
    }
};

void require_render_with_files(const std::string &template_text,
                               const std::string &template_path,
                               const InMemoryTemplateFileSystem &filesystem,
                               const std::string &json,
                               const std::string &expected,
                               const char *message) {
    cJSON *root = cJSON_Parse(json.c_str());
    require(root != nullptr, "fixture JSON parses");
    const jinja2_tcl::engine::RenderResult result =
            jinja2_tcl::engine::render_template(template_text, root, filesystem, template_path);
    cJSON_Delete(root);
    if (!result.ok) {
        std::fprintf(stderr, "render error: %s\n", result.error.c_str());
    }
    require(result.ok, message);
    require(result.output == expected, message);
}

void require_render_error(const std::string &template_text,
                          const std::string &json,
                          const std::string &expected_error,
                          const char *message) {
    cJSON *root = cJSON_Parse(json.c_str());
    require(root != nullptr, "fixture JSON parses");
    const jinja2_tcl::engine::RenderResult result = jinja2_tcl::engine::render_template(template_text, root);
    cJSON_Delete(root);
    require(!result.ok, message);
    require(result.error.find(expected_error) != std::string::npos, message);
}

void test_variables_and_escape_filter() {
    require_render("{{ title }}",
                   R"json({"title":"Coffee & <Tea> \"Cup\""})json",
                   "Coffee &amp; &lt;Tea&gt; &quot;Cup&quot;",
                   "plain variable output is HTML escaped");
    require_render("{{ title|escape }}",
                   R"json({"title":"Coffee & <Tea>"})json",
                   "Coffee &amp; &lt;Tea&gt;",
                   "variables and escape filter render");
    require_render("{{ html|safe }}",
                   R"json({"html":"<strong>Trusted</strong>"})json",
                   "<strong>Trusted</strong>",
                   "safe filter allows trusted HTML output");
    require_render("{% set panel %}<span>{{ title }}</span>{% endset %}{{ panel }}",
                   R"json({"title":"<script>alert(1)</script>"})json",
                   "<span>&lt;script&gt;alert(1)&lt;/script&gt;</span>",
                   "captured template markup keeps escaped dynamic output");
}

void test_root_object_keys_without_context_aliases() {
    require_render("{{ product.title }} by {{ shop.name }}",
                   R"json({"product":{"title":"Chef Knife"},"shop":{"name":"Artisan Kitchen"}})json",
                   "Chef Knife by Artisan Kitchen",
                   "top-level product and shop objects render from explicit root keys");
    require_render("{{ product.title }}|{{ this.title }}",
                   R"json({"title":"Root Product"})json",
                   "|",
                   "renderer does not create implicit product or this aliases for the root object");
    require_render("{{ this.title }}",
                   R"json({"this":{"title":"Explicit Root Key"}})json",
                   "Explicit Root Key",
                   "explicit this root key renders like any other top-level key");
}

void test_assignment_condition_and_default_filter() {
    require_render("{% set label = brand|default('House') %}{% if inventory > 0 %}{{ label }} in stock{% else %}No{% endif %}",
                   R"json({"brand":"","inventory":4})json",
                   "House in stock",
                   "set, if, comparison, and default filter render");
}

void test_block_assignment() {
    require_render("{% set greeting %}Hi {{ name }}{% endset %}{{ greeting }}",
                   R"json({"name":"Ada"})json",
                   "Hi Ada",
                   "block set captures rendered body");
    require_render("{% set stock_text %}{% if variant.inventory_count > 10 %}In Stock{% elif variant.inventory_count > 0 %}Low Stock{% else %}Out of Stock{% endif %}{% endset %}{{ stock_text|trim }}",
                   R"json({"variant":{"inventory_count":4}})json",
                   "Low Stock",
                   "block set captures nested conditional body");
    require_render("{% set stock_text | trim | upper %}\n low stock \n{% endset %}{{ stock_text }}",
                   R"json({})json",
                   "LOW STOCK",
                   "block set supports filtered capture");

    std::string error;
    require(!jinja2_tcl::engine::validate_template("{% set stock_text %}In Stock", error),
            "block set without endset is rejected");
    require(error.find("set block missing endset") != std::string::npos,
            "block set missing endset error is explicit");
    require(!jinja2_tcl::engine::validate_template("{% set stock_text | %}In Stock{% endset %}", error),
            "block set with invalid filter chain is rejected");
    require(error.find("expected filter name") != std::string::npos,
            "block set invalid filter chain error is explicit");
}

void test_conditional_expression() {
    require_render("{% for image in json_images %}<button class=\"thumb {{ 'active' if loop.first else '' }}\" data-index=\"{{ loop.index0 }}\" data-preview=\"{{ image.preview_url|escape }}\">{{ loop.index }}</button>{% endfor %}",
                   R"json({"json_images":[{"preview_url":"https://example.test/one?a=1&b=2"},{"preview_url":"https://example.test/two"}]})json",
                   "<button class=\"thumb active\" data-index=\"0\" data-preview=\"https://example.test/one?a=1&amp;b=2\">1</button><button class=\"thumb \" data-index=\"1\" data-preview=\"https://example.test/two\">2</button>",
                   "inline conditional expression renders loop.first branch");
    require_render("{{ 'yes' if enabled else 'no' }}",
                   R"json({"enabled":false})json",
                   "no",
                   "inline conditional expression renders else branch");
    require_render("{{ 'yes' if enabled }}",
                   R"json({"enabled":false})json",
                   "",
                   "inline conditional expression without else renders empty false branch");
    require_render("{{ ['a' if first else 'b', 'c']|join('|') }}",
                   R"json({"first":true})json",
                   "a|c",
                   "inline conditional expression works inside list literal");
    require_render("{{ allow_out_of_stock_orders ? 'backorder' : 'out-of-stock' }}",
                   R"json({"allow_out_of_stock_orders":true})json",
                   "backorder",
                   "c-style ternary expression renders true branch");
    require_render("{{ allow_out_of_stock_orders ? 'backorder' : 'out-of-stock' }}",
                   R"json({"allow_out_of_stock_orders":false})json",
                   "out-of-stock",
                   "c-style ternary expression renders false branch");
    require_render("{{ primary ? 'primary' : secondary ? 'secondary' : 'none' }}",
                   R"json({"primary":false,"secondary":true})json",
                   "secondary",
                   "c-style ternary expression is right associative");
    require_render("{% set stock_class = allow_out_of_stock_orders ? 'backorder' : 'out-of-stock' %}{{ stock_class }}",
                   R"json({"allow_out_of_stock_orders":false})json",
                   "out-of-stock",
                   "c-style ternary expression works in set statements");

    std::string error;
    require(!jinja2_tcl::engine::validate_template("{{ enabled ? 'yes' }}", error),
            "c-style ternary without false branch is rejected");
    require(error.find("expected `:` in ternary expression") != std::string::npos,
            "c-style ternary missing colon error is explicit");
}

void test_for_loop_and_loop_metadata() {
    require_render("{% for variant in variants %}{{ loop.index }}:{{ variant.name }}{% if not loop.last %}|{% endif %}{% endfor %}",
                   R"json({"variants":[{"name":"Small"},{"name":"Large"}]})json",
                   "1:Small|2:Large",
                   "for loop and loop metadata render");
    require_render("{% for index in range(3) %}{{ index }}{% endfor %}:"
                   "{% for index in range(1, 6, 2) %}{{ index }}{% endfor %}:"
                   "{% for index in range(start=5, stop=0, step=-2) %}{{ index }}{% endfor %}",
                   R"json({})json",
                   "012:135:531",
                   "range renders positional and keyword numeric loops");
}

void test_for_else_branch() {
    require_render("{% for item in items %}{{ item }}{% else %}empty{% endfor %}",
                   R"json({"items":[]})json",
                   "empty",
                   "for else branch renders");
}

void test_list_literals() {
    require_render("{{ []|length }}",
                   R"json({})json",
                   "0",
                   "empty list literal renders");
    require_render("{% set names = ['A', title,] %}{{ names|join('|') }}",
                   R"json({"title":"B"})json",
                   "A|B",
                   "list literal supports expressions and trailing comma");
    require_render("{% set names = ['A'] %}{% set names = names + [title] %}{{ names|join('|') }}",
                   R"json({"title":"B"})json",
                   "A|B",
                   "list concatenation preserves arrays");
    require_render("{{ ['small', 'large'][1] }}",
                   R"json({})json",
                   "large",
                   "list literal supports subscript lookup");
    require_render("{% if title in ['A', 'B'] %}yes{% else %}no{% endif %}",
                   R"json({"title":"B"})json",
                   "yes",
                   "list literal supports membership checks");
    require_render("{% if title not in ['A', 'B'] %}yes{% else %}no{% endif %}",
                   R"json({"title":"C"})json",
                   "yes",
                   "list literal supports negated membership checks");
    require_render("{% for item in [] %}{{ item }}{% else %}empty{% endfor %}",
                   R"json({})json",
                   "empty",
                   "empty list literal works in loops");
    require_render("{% for item in missing|default([]) %}{{ item }}{% else %}empty{% endfor %}",
                   R"json({})json",
                   "empty",
                   "empty list literal works as a filter argument");
}

void test_subscript_and_slice_expressions() {
    require_render("{{ title[1] }}",
                   R"json({"title":"fresh"})json",
                   "r",
                   "string subscript renders indexed character");
    require_render("{{ title[1] is defined }}",
                   R"json({"title":"fresh"})json",
                   "true",
                   "string subscript supports defined checks");
    require_render("{{ title[:20]|upper|e }}",
                   R"json({"title":"fresh & spicy things today"})json",
                   "FRESH &amp; SPICY THINGS",
                   "string slice works with upper and escape filters");
    require_render("{{ title[6:] }}",
                   R"json({"title":"hello world"})json",
                   "world",
                   "string slice supports omitted end");
    require_render("{{ title[0:5] }}",
                   R"json({"title":"hello world"})json",
                   "hello",
                   "string slice supports explicit start and end");
    require_render("{{ ['a', 'b', 'c', 'd'][1:3]|join('|') }}",
                   R"json({})json",
                   "b|c",
                   "array slice renders selected range");
}

void test_basic_filter_compatibility() {
    require_render("{{ 'hello there'|replace('there', 'world') }}",
                   R"json({})json",
                   "hello world",
                   "replace filter renders");
    require_render("{{ ['b', 'a', 'c']|sort|join('') }}",
                   R"json({})json",
                   "abc",
                   "sort filter renders");
    require_render("{{ [3, 1, 2]|min }}:{{ [3, 1, 2]|max }}:{{ [3, 1, 2]|sum }}",
                   R"json({})json",
                   "1:3:6",
                   "min, max, and sum filters render");
    require_render("{{ (-4)|abs }}:{{ '42.9'|int }}:{{ '3.5'|float }}:{{ 3.6|round }}",
                   R"json({})json",
                   "4:42:3.5:4",
                   "numeric conversion filters render");
    require_render("{{ 'alpha beta <em>gamma</em>'|wordcount }}:{{ 'a b&c'|urlencode }}",
                   R"json({})json",
                   "3:a%20b%26c",
                   "wordcount and urlencode filters render");
    require_render("{{ 'abcdef'|truncate(5) }}:{{ 'abc'|list|join('|') }}",
                   R"json({})json",
                   "ab...:a|b|c",
                   "truncate and list filters render");
    require_render("{% for pair in {'b': 2, 'a': 1}|dictsort %}{{ pair.key }}={{ pair.value }};{% endfor %}",
                   R"json({})json",
                   "a=1;b=2;",
                   "dictsort filter renders key value pairs");
    require_render("{{ [1, 2, 3, 4, 5]|batch(2, 0)|tojson }}:{{ [1, 2, 3, 4, 5]|slice(2)|tojson }}",
                   R"json({})json",
                   "[[1,2],[3,4],[5,0]]:[[1,2],[3,4],[5]]",
                   "batch and slice filters render grouped arrays");
    require_render("{{ [{'name': 'b'}, {'name': 'a'}]|sort(attribute='name')|join(d='|', attribute='name') }}",
                   R"json({})json",
                   "a|b",
                   "keyword filter arguments render");
    require_render("{{ data|attr('title') }}:{{ '%s $%.2f'|format(data.title, data.price) }}",
                   R"json({"data":{"title":"Cup","price":12.5}})json",
                   "Cup:Cup $12.50",
                   "attr and format filters render");
    require_render("{{ users|map(attribute='name')|join('|') }}:{{ users|map('attr', 'name')|join('|') }}:{{ ['a', 'b']|map('upper')|join('') }}",
                   R"json({"users":[{"name":"Ada"},{"name":"Grace"}]})json",
                   "Ada|Grace:Ada|Grace:AB",
                   "map filter supports attributes and nested filters");
    require_render("{% for group in users|groupby('role') %}{{ group.grouper }}={{ group.list|map(attribute='name')|join(',') }};{% endfor %}",
                   R"json({"users":[{"name":"Ada","role":"admin"},{"name":"Grace","role":"editor"},{"name":"Linus","role":"admin"}]})json",
                   "admin=Ada,Linus;editor=Grace;",
                   "groupby filter renders grouped lists");
    require_render("{{ [1, 2, 3, 4]|select('odd')|join('|') }}:{{ [1, 2, 3, 4]|reject('odd')|join('|') }}",
                   R"json({})json",
                   "1|3:2|4",
                   "select and reject filters render by test");
    require_render("{{ users|selectattr('active')|map(attribute='name')|join('|') }}:{{ users|rejectattr('active')|map(attribute='name')|join('|') }}:{{ users|selectattr('role', 'eq', 'admin')|map(attribute='name')|join('|') }}",
                   R"json({"users":[{"name":"Ada","active":true,"role":"admin"},{"name":"Grace","active":false,"role":"editor"},{"name":"Linus","active":true,"role":"admin"}]})json",
                   "Ada|Linus:Grace:Ada|Linus",
                   "selectattr and rejectattr filters render by attribute tests");
    require_render("{{ ['A', 'a', 'b', 'A']|unique|join('|') }}:{{ ['A', 'a', 'b', 'A']|unique(case_sensitive=true)|join('|') }}:{{ users|unique(attribute='role')|map(attribute='name')|join('|') }}",
                   R"json({"users":[{"name":"Ada","role":"admin"},{"name":"Grace","role":"editor"},{"name":"Linus","role":"admin"}]})json",
                   "A|b:A|a|b:Ada|Grace",
                   "unique filter preserves first unique items");
}

void test_basic_method_compatibility() {
    require_render("{{ '  Alpha Beta  '.strip().lower().replace(' ', '-') }}",
                   R"json({})json",
                   "alpha-beta",
                   "string method chain renders");
    require_render("{{ 'a,b,c'.split(',').join('|') }}:{{ '|'.join('a,b,c'.split(',')) }}",
                   R"json({})json",
                   "a|b|c:a|b|c",
                   "split and join methods render in chained expressions");
    require_render("{{ 'sale:wide'.startswith('sale') }}:{{ 'sale:wide'.endswith('wide') }}",
                   R"json({})json",
                   "true:true",
                   "startswith and endswith methods render booleans");
    require_render("{% for key, value in data.items() %}{{ key }}={{ value }};{% endfor %}{{ data.keys().join('|') }}:{{ data.values().join('|') }}:{{ data.get('missing', 'fallback') }}:{{ data.get('a') }}",
                   R"json({"data":{"b":2,"a":1}})json",
                   "a=1;b=2;a|b:1|2:fallback:1",
                   "object methods render keys values items and get");

    std::string error;
    require(!jinja2_tcl::engine::validate_template("{{ items.append('x') }}", error),
            "unsupported mutating method is rejected");
    require(error.find("unsupported method: append") != std::string::npos,
            "unsupported method error is explicit");
}

void test_validation_call_target_semantics() {
    std::string error;
    require(jinja2_tcl::engine::validate_template(
                    "{% macro card(title) %}<p>{{ title }}</p>{% endmacro %}{{ card(title) }}",
                    error),
            "validation accepts macro calls after definition");
    require(!jinja2_tcl::engine::validate_template(
                    "{{ card(title) }}{% macro card(title) %}<p>{{ title }}</p>{% endmacro %}",
                    error),
            "validation rejects macro calls before definition");
    require(error.find("undefined macro: card") != std::string::npos,
            "macro call before definition error is explicit");
    require(jinja2_tcl::engine::validate_template("{{ range(3)|join(',') }}", error),
            "validation accepts supported range calls");
    require(jinja2_tcl::engine::validate_template("{% for index in range(3) %}{{ index }}{% endfor %}", error),
            "validation accepts range in loop collections");
    require(!jinja2_tcl::engine::validate_template("{{ range() }}", error),
            "validation rejects range calls without arguments");
    require(error.find("range() requires at least a stop argument") != std::string::npos,
            "range argument error is explicit");

    InMemoryTemplateFileSystem filesystem;
    filesystem.files["components/card.html.jinja"] =
            "{% macro card(title) %}<p>{{ title }}</p>{% endmacro %}";
    require(jinja2_tcl::engine::validate_template(
                    "{% include 'components/card.html.jinja' %}{{ card(title) }}",
                    filesystem,
                    error,
                    "pages/product.html.jinja"),
            "validation accepts macros registered by earlier includes");
    require(!jinja2_tcl::engine::validate_template(
                    "{{ card(title) }}{% include 'components/card.html.jinja' %}",
                    filesystem,
                    error,
                    "pages/product.html.jinja"),
            "validation rejects macro calls before include registration");
    require(error.find("undefined macro: card") != std::string::npos,
            "include order macro error is explicit");
}

void test_basic_test_compatibility() {
    require_render("{% if 4 is even and 3 is odd and 'ABC' is upper and 'abc' is lower %}yes{% else %}no{% endif %}",
                   R"json({})json",
                   "yes",
                   "basic parity and case tests render");
    require_render("{% if title is string and price is number and items is iterable and data is mapping and items is sequence %}yes{% else %}no{% endif %}",
                   R"json({"title":"Cup","price":4,"items":[1],"data":{"a":1}})json",
                   "yes",
                   "basic type tests render");
    require_render("{% if 5 is gt(3) and 5 is ge(5) and 3 is lt(5) and 3 is le(3) and 'a' is eq('a') and 'a' is ne('b') %}yes{% else %}no{% endif %}",
                   R"json({})json",
                   "yes",
                   "comparison alias tests render");
    require_render("{% if missing is undefined and title is not undefined %}yes{% else %}no{% endif %}",
                   R"json({"title":"Cup"})json",
                   "yes",
                   "undefined test renders");
}

void test_basic_expression_compatibility() {
    require_render("{{ {'b': 2, 'a': 1}.a }}:{{ (1, 2, 3)|join('|') }}",
                   R"json({})json",
                   "1:1|2|3",
                   "object and tuple literals render");
    require_render("{{ 7 // 2 }}:{{ 2 ** 3 }}",
                   R"json({})json",
                   "3:8",
                   "integer division and power operators render");
    require_render("{% macro label(text, suffix='!') %}{{ text }}{{ suffix }}{% endmacro %}{{ label(suffix='?', text='Hi') }}",
                   R"json({})json",
                   "Hi?",
                   "macro keyword arguments render");
}

void test_loop_unpacking_and_filter_condition() {
    require_render("{% for name, qty in rows if qty > 1 %}{{ loop.index }}/{{ loop.length }}:{{ name }}={{ qty }}{% if not loop.last %}|{% endif %}{% else %}empty{% endfor %}",
                   R"json({"rows":[["a",1],["b",2],["c",3]]})json",
                   "1/2:b=2|2/2:c=3",
                   "for loop supports unpacking and if filters");
    require_render("{% for key, value in data %}{{ key }}={{ value }};{% endfor %}",
                   R"json({"data":{"b":2,"a":1}})json",
                   "a=1;b=2;",
                   "for loop unpacks object key value pairs");
}

void test_set_updates_nearest_existing_binding() {
    require_render("{% set found = false %}{% for item in items %}{% if item == 'B' %}{% set found = true %}{% endif %}{% endfor %}{{ found }}",
                   R"json({"items":["A","B","C"]})json",
                   "true",
                   "set inside a loop updates the nearest existing binding");
    require_render("{% set item = 'outer' %}{% for item in items %}{{ item }}{% endfor %}:{{ item }}",
                   R"json({"items":["A","B"]})json",
                   "AB:outer",
                   "for loop variables remain local when they share an outer name");
    require_render("{% set all_keys = [] %}{% for v in variants %}{% for c in v.characteristics %}{% if c.key not in all_keys %}{% set all_keys = all_keys + [c.key] %}{% endif %}{% endfor %}{% endfor %}{{ all_keys|join('|') }}",
                   R"json({"variants":[{"characteristics":[{"key":"Color"},{"key":"Size"}]},{"characteristics":[{"key":"Color"},{"key":"Material"}]}]})json",
                   "Color|Size|Material",
                   "nested loop accumulator can deduplicate list entries");
}

void test_defined_test() {
    require_render("{% if title is defined %}yes{% else %}no{% endif %}",
                   R"json({"title":null})json",
                   "yes",
                   "present null field is defined");
    require_render("{% if missing is defined %}yes{% else %}no{% endif %}",
                   R"json({})json",
                   "no",
                   "missing top-level field is not defined");
    require_render("{% if variant.inventory_count is defined %}yes{% else %}no{% endif %}",
                   R"json({"variant":{"inventory_count":0}})json",
                   "yes",
                   "present dotted field is defined");
    require_render("{% if variant.missing is not defined %}yes{% else %}no{% endif %}",
                   R"json({"variant":{"inventory_count":0}})json",
                   "yes",
                   "missing dotted field supports is not defined");
    require_render("{{ variants[1].name is defined }}",
                   R"json({"variants":[{"name":"Small"},{"name":"Large"}]})json",
                   "true",
                   "bracket lookup supports defined test");
    require_render("{% set allow_out_of_stock_orders = false %}{% for variant in variants %}{% if (variant.inventory_count == 0 and not allow_out_of_stock_orders) or (variant.inventory_count is defined and variant.inventory_count <= 0) %}empty{% else %}ok{% endif %}{% endfor %}",
                   R"json({"variants":[{"inventory_count":0}]})json",
                   "empty",
                   "compound defined expression parses and renders");
}

void test_none_test() {
    require_render("{% if inventory is none %}none{% else %}value{% endif %}",
                   R"json({"inventory":null})json",
                   "none",
                   "present null field is none");
    require_render("{% if inventory is not none %}value{% else %}none{% endif %}",
                   R"json({"inventory":{"count":3}})json",
                   "value",
                   "present object field is not none");
    require_render("{{ missing is not none }}",
                   R"json({})json",
                   "true",
                   "undefined value is distinct from none");
    require_render("{% if shipping or policies or (inventory is defined and inventory is not none) or inventory_count is defined %}show{% else %}hide{% endif %}",
                   R"json({"shipping":false,"policies":false,"inventory":{"count":3}})json",
                   "show",
                   "compound none test expression parses and renders");
}

void test_macro_and_tojson_filter() {
    require_render("{% macro chip(text, suffix='!') %}<span>{{ text|escape }}{{ suffix }}</span>{% endmacro %}{{ chip(title) }}<script type=\"application/json\">{{ payload|tojson }}</script>",
                   R"json({"title":"A \"quoted\" cup","payload":{"title":"A \"quoted\" cup"}})json",
                   "<span>A &quot;quoted&quot; cup!</span><script type=\"application/json\">{\"title\":\"A \\\"quoted\\\" cup\"}</script>",
                   "macro defaults and tojson render");

    require_render_error("{{ missing_card(title) }}",
                         R"json({"title":"Cup"})json",
                         "undefined macro: missing_card",
                         "missing macro calls fail rendering");
}

void test_bare_zero_required_macro_reference() {
    require_render("{% macro icon -%}<svg>{{ label }}</svg>{%- endmacro %}{{ icon|safe }}",
                   R"json({"label":"Mark"})json",
                   "<svg>Mark</svg>",
                   "bare zero-argument macro reference renders the macro body");
    require_render("{% macro badge(text='New') %}<span>{{ text }}</span>{% endmacro %}{{ badge }}",
                   R"json({})json",
                   "<span>New</span>",
                   "bare macro reference renders when all parameters have defaults");
    require_render("{% macro badge(text) %}<span>{{ text }}</span>{% endmacro %}{{ badge|safe }}x",
                   R"json({})json",
                   "x",
                   "bare macro reference with required parameters remains empty");
    require_render("{% macro icon -%}<svg></svg>{%- endmacro %}{% set icon = 'text' %}{{ icon }}",
                   R"json({})json",
                   "text",
                   "local variable shadows bare macro reference");

    InMemoryTemplateFileSystem filesystem;
    filesystem.files["_icons.html.jinja"] =
            "{% macro lucide_icon__chevron_left -%}<svg class=\"lucide\"></svg>{%- endmacro %}";
    require_render_with_files("{% include '_icons.html.jinja' %}{{ lucide_icon__chevron_left|safe }}",
                              "page.html.jinja",
                              filesystem,
                              R"json({})json",
                              "<svg class=\"lucide\"></svg>",
                              "included bare zero-argument icon macro reference renders");
}

void test_call_blocks() {
    require_render("{% macro panel(title) %}<section><h2>{{ title|e }}</h2>{{ caller() }}</section>{% endmacro %}{% call panel('Specs') %}<p>{{ description|e }}</p>{% endcall %}",
                   R"json({"description":"Fast & light"})json",
                   "<section><h2>Specs</h2><p>Fast &amp; light</p></section>",
                   "call block renders caller body inside target macro");
    require_render("{% macro shell() %}<div>{{ caller() }}</div>{% endmacro %}{% call shell %}plain{% endcall %}",
                   R"json({})json",
                   "<div>plain</div>",
                   "call block supports no-argument macro shorthand");
    require_render("{% macro render_list(items) %}{% for item in items %}[{{ caller(item) }}]{% endfor %}{% endmacro %}{% call(item) render_list(items) %}{{ item.name|e }}{% endcall %}",
                   R"json({"items":[{"name":"One"},{"name":"Two & More"}]})json",
                   "[One][Two &amp; More]",
                   "call block supports caller parameters");
    require_render("{% macro wrap(label) %}<{{ label }}>{{ caller() }}</{{ label }}>{% endmacro %}{% call wrap('a') %}{% call wrap('b') %}x{% endcall %}{% endcall %}",
                   R"json({})json",
                   "<a><b>x</b></a>",
                   "nested call blocks restore caller bindings");
    require_render("{% macro caller() %}outer{% endmacro %}{% macro wrap() %}{{ caller() }}{% endmacro %}{% call wrap() %}inner{% endcall %}:{{ caller() }}",
                   R"json({})json",
                   "inner:outer",
                   "call block restores an existing caller macro");
    require_render("{% macro maybe() %}{% if caller is defined %}{{ caller() }}{% else %}empty{% endif %}{% endmacro %}{% call maybe() %}body{% endcall %}:{{ maybe() }}",
                   R"json({})json",
                   "body:empty",
                   "call block exposes caller as a defined macro only during the call");

    std::string error;
    require(!jinja2_tcl::engine::validate_template("{% macro wrap() %}{{ caller() }}{% endmacro %}{% call wrap() %}x", error),
            "call block without endcall is rejected");
    require(error.find("call statement missing endcall") != std::string::npos,
            "call block missing endcall error is explicit");
    require(jinja2_tcl::engine::validate_template(
                    "{% macro wrap() %}<div>{{ caller() }}</div>{% endmacro %}{% call wrap() %}body{% endcall %}",
                    error),
            "validation accepts caller usage through call blocks");
    require(jinja2_tcl::engine::validate_template(
                    "{% macro maybe() %}{% if caller is defined %}{{ caller() }}{% else %}empty{% endif %}{% endmacro %}{% call maybe() %}body{% endcall %}:{{ maybe() }}",
                    error),
            "validation accepts optional caller usage in macros");
    require(!jinja2_tcl::engine::validate_template(
                    "{% macro wrap() %}<div>{{ caller() }}</div>{% endmacro %}{{ wrap() }}",
                    error),
            "validation rejects unguarded caller usage without a call block");
    require(error.find("undefined macro: caller") != std::string::npos,
            "unguarded caller validation error is explicit");
}

void test_include_and_template_inheritance() {
    InMemoryTemplateFileSystem filesystem;
    filesystem.files["components/card.html.jinja"] = "<p>{{ title }}</p>";
    require_render_with_files("{% include 'components/card.html.jinja' %}",
                              "pages/product.html.jinja",
                              filesystem,
                              R"json({"title":"Coffee & Tea"})json",
                              "<p>Coffee &amp; Tea</p>",
                              "include renders a filesystem template with current context");

    filesystem.files["layout/base.html.jinja"] =
            "<html><body>{% block content %}Base {{ title }}{% endblock %}</body></html>";
    filesystem.files["layout/mid.html.jinja"] =
            "{% extends 'layout/base.html.jinja' %}"
            "{% block content %}<main>Mid {{ super() }}</main>{% endblock %}";
    const std::string child =
            "{% extends 'layout/mid.html.jinja' %}"
            "{% block content %}<article>{{ super() }} Child</article>{% endblock %}";
    require_render_with_files(child,
                              "pages/product.html.jinja",
                              filesystem,
                              R"json({"title":"Cup"})json",
                              "<html><body><article><main>Mid Base Cup</main> Child</article></body></html>",
                              "extends, block override, endblock, and super render across inheritance levels");

    std::string error;
    require(jinja2_tcl::engine::validate_template(child, filesystem, error, "pages/product.html.jinja"),
            "loader-aware validation accepts valid inheritance");
    const std::string child_with_whitespace =
            "\n{% extends 'layout/mid.html.jinja' %}\n"
            "{% block content %}<article>Whitespace is okay</article>{% endblock %}\n";
    require(jinja2_tcl::engine::validate_template(child_with_whitespace,
                                              filesystem,
                                              error,
                                              "pages/product.html.jinja"),
            "extending templates may contain whitespace outside blocks");

    filesystem.files["components/macro-card.html.jinja"] =
            "{% macro macro_card(title) %}<p>{{ title }}</p>{% endmacro %}";
    const std::string child_with_root_include =
            "{% extends 'layout/base.html.jinja' %}"
            "{% include 'components/macro-card.html.jinja' %}"
            "{% block content %}{{ macro_card(title) }}{% endblock %}";
    require(!jinja2_tcl::engine::validate_template(child_with_root_include,
                                               filesystem,
                                               error,
                                               "pages/product.html.jinja"),
            "validation rejects top-level includes outside blocks in extending templates");
    require(error.find("top-level child nodes outside blocks") != std::string::npos,
            "top-level child node validation error explains inheritance behavior");

    const std::string child_with_root_macro =
            "{% extends 'layout/base.html.jinja' %}"
            "{% macro local_card(title) %}<p>{{ title }}</p>{% endmacro %}"
            "{% block content %}{{ local_card(title) }}{% endblock %}";
    require(!jinja2_tcl::engine::validate_template(child_with_root_macro,
                                               filesystem,
                                               error,
                                               "pages/product.html.jinja"),
            "validation rejects top-level macros outside blocks in extending templates");

    require(!jinja2_tcl::engine::validate_template("{% include 'missing.html.jinja' %}",
                                               filesystem,
                                               error,
                                               "pages/product.html.jinja"),
            "loader-aware validation rejects missing includes");
    require(error.find("could not read template file") != std::string::npos,
            "missing include error reports a file read failure");
    require(!jinja2_tcl::engine::validate_template("{% include '../secret.html.jinja' %}",
                                               filesystem,
                                               error,
                                               "pages/product.html.jinja"),
            "include rejects parent-directory paths");
    require(error.find("parent segments") != std::string::npos,
            "unsafe include path error is explicit");

    filesystem.files["components/a.html.jinja"] = "{% include 'components/b.html.jinja' %}";
    filesystem.files["components/b.html.jinja"] = "{% include 'components/a.html.jinja' %}";
    require(!jinja2_tcl::engine::validate_template("{% include 'components/a.html.jinja' %}",
                                               filesystem,
                                               error,
                                               "pages/product.html.jinja"),
            "validation rejects circular includes");
    require(error.find("circular template dependency") != std::string::npos,
            "circular include error is explicit");

    require(!jinja2_tcl::engine::validate_template("{% include 'components/card.html.jinja' %}", error),
            "text-only validation requires a filesystem for include dependencies");
    require(error.find("template filesystem is required") != std::string::npos,
            "text-only include validation error is explicit");
}

}  // namespace

int main() {
    test_variables_and_escape_filter();
    test_root_object_keys_without_context_aliases();
    test_assignment_condition_and_default_filter();
    test_block_assignment();
    test_conditional_expression();
    test_for_loop_and_loop_metadata();
    test_for_else_branch();
    test_list_literals();
    test_subscript_and_slice_expressions();
    test_basic_filter_compatibility();
    test_basic_method_compatibility();
    test_validation_call_target_semantics();
    test_basic_test_compatibility();
    test_basic_expression_compatibility();
    test_loop_unpacking_and_filter_condition();
    test_set_updates_nearest_existing_binding();
    test_defined_test();
    test_none_test();
    test_macro_and_tojson_filter();
    test_bare_zero_required_macro_reference();
    test_call_blocks();
    test_include_and_template_inheritance();
    return 0;
}
