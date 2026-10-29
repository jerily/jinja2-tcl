// Adapted from jerily-webserver/core/jinja2; see docs/provenance.md.
#include "jinja2.h"
#include "string_utils.h"

extern "C" {
#include <cJSON.h>
}

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace jinja2_tcl::engine {
namespace {

std::string number_to_string(double value) {
    if (std::isfinite(value) && std::fabs(value - std::round(value)) < 0.0000001) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

struct Value {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool bool_value = false;
    double number_value = 0.0;
    std::string string_value;
    std::vector<Value> array_value;
    std::map<std::string, Value> object_value;
    bool safe = false;

    static Value null() {
        return {};
    }

    static Value boolean(bool value) {
        Value result;
        result.type = Type::Bool;
        result.bool_value = value;
        return result;
    }

    static Value number(double value) {
        Value result;
        result.type = Type::Number;
        result.number_value = value;
        return result;
    }

    static Value string(std::string value, bool safe_string = false) {
        Value result;
        result.type = Type::String;
        result.string_value = std::move(value);
        result.safe = safe_string;
        return result;
    }

    static Value array(std::vector<Value> values) {
        Value result;
        result.type = Type::Array;
        result.array_value = std::move(values);
        return result;
    }

    static Value object(std::map<std::string, Value> values) {
        Value result;
        result.type = Type::Object;
        result.object_value = std::move(values);
        return result;
    }
};

Value value_from_json(const cJSON *item) {
    if (item == nullptr || cJSON_IsNull(item)) {
        return Value::null();
    }
    if (cJSON_IsBool(item)) {
        return Value::boolean(cJSON_IsTrue(item));
    }
    if (cJSON_IsNumber(item)) {
        return Value::number(item->valuedouble);
    }
    if (cJSON_IsString(item)) {
        return Value::string(item->valuestring == nullptr ? "" : item->valuestring);
    }
    if (cJSON_IsArray(item)) {
        std::vector<Value> values;
        for (const cJSON *child = item->child; child != nullptr; child = child->next) {
            values.push_back(value_from_json(child));
        }
        return Value::array(std::move(values));
    }
    if (cJSON_IsObject(item)) {
        std::map<std::string, Value> values;
        for (const cJSON *child = item->child; child != nullptr; child = child->next) {
            if (child->string != nullptr) {
                values.emplace(child->string, value_from_json(child));
            }
        }
        return Value::object(std::move(values));
    }
    return Value::null();
}

bool truthy(const Value &value) {
    switch (value.type) {
        case Value::Type::Null:
            return false;
        case Value::Type::Bool:
            return value.bool_value;
        case Value::Type::Number:
            return value.number_value != 0.0;
        case Value::Type::String:
            return !value.string_value.empty();
        case Value::Type::Array:
            return !value.array_value.empty();
        case Value::Type::Object:
            return !value.object_value.empty();
    }
    return false;
}

double to_number(const Value &value) {
    switch (value.type) {
        case Value::Type::Bool:
            return value.bool_value ? 1.0 : 0.0;
        case Value::Type::Number:
            return value.number_value;
        case Value::Type::String:
            return std::strtod(value.string_value.c_str(), nullptr);
        default:
            return 0.0;
    }
}

std::string to_string_value(const Value &value) {
    switch (value.type) {
        case Value::Type::Null:
            return "";
        case Value::Type::Bool:
            return value.bool_value ? "true" : "false";
        case Value::Type::Number:
            return number_to_string(value.number_value);
        case Value::Type::String:
            return value.string_value;
        case Value::Type::Array:
            return "[array]";
        case Value::Type::Object:
            return "[object]";
    }
    return "";
}

std::string to_html_output_value(const Value &value) {
    const std::string rendered = to_string_value(value);
    return value.safe ? rendered : jinja2_tcl::string_utils::html_escape(rendered);
}

std::string to_json_value(const Value &value);

std::string to_json_array(const std::vector<Value> &values) {
    std::string result = "[";
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result += ",";
        }
        result += to_json_value(values[index]);
    }
    result += "]";
    return result;
}

std::string to_json_object(const std::map<std::string, Value> &values) {
    std::string result = "{";
    bool first = true;
    for (const auto &entry : values) {
        if (!first) {
            result += ",";
        }
        first = false;
        result += "\"" + jinja2_tcl::string_utils::html_safe_json_escape(entry.first) + "\":" + to_json_value(entry.second);
    }
    result += "}";
    return result;
}

std::string to_json_value(const Value &value) {
    switch (value.type) {
        case Value::Type::Null:
            return "null";
        case Value::Type::Bool:
            return value.bool_value ? "true" : "false";
        case Value::Type::Number:
            return number_to_string(value.number_value);
        case Value::Type::String:
            return "\"" + jinja2_tcl::string_utils::html_safe_json_escape(value.string_value) + "\"";
        case Value::Type::Array:
            return to_json_array(value.array_value);
        case Value::Type::Object:
            return to_json_object(value.object_value);
    }
    return "null";
}

Value get_item(const Value &base, const Value &key) {
    if (base.type == Value::Type::Object) {
        const std::string name = to_string_value(key);
        const auto iterator = base.object_value.find(name);
        return iterator == base.object_value.end() ? Value::null() : iterator->second;
    }
    if (base.type == Value::Type::Array) {
        const int index = static_cast<int>(to_number(key));
        if (index < 0 || static_cast<size_t>(index) >= base.array_value.size()) {
            return Value::null();
        }
        return base.array_value[static_cast<size_t>(index)];
    }
    if (base.type == Value::Type::String) {
        const int index = static_cast<int>(to_number(key));
        if (index < 0 || static_cast<size_t>(index) >= base.string_value.size()) {
            return Value::null();
        }
        return Value::string(base.string_value.substr(static_cast<size_t>(index), 1), base.safe);
    }
    return Value::null();
}

bool has_item(const Value &base, const Value &key) {
    if (base.type == Value::Type::Object) {
        return base.object_value.find(to_string_value(key)) != base.object_value.end();
    }
    if (base.type == Value::Type::Array) {
        const int index = static_cast<int>(to_number(key));
        return index >= 0 && static_cast<size_t>(index) < base.array_value.size();
    }
    if (base.type == Value::Type::String) {
        const int index = static_cast<int>(to_number(key));
        return index >= 0 && static_cast<size_t>(index) < base.string_value.size();
    }
    return false;
}

int normalize_slice_index(int index, int length) {
    if (index < 0) {
        index += length;
    }
    if (index < 0) {
        return 0;
    }
    if (index > length) {
        return length;
    }
    return index;
}

Value slice_value(const Value &base, const Value *start_value, const Value *end_value) {
    int length = 0;
    if (base.type == Value::Type::String) {
        length = static_cast<int>(base.string_value.size());
    } else if (base.type == Value::Type::Array) {
        length = static_cast<int>(base.array_value.size());
    } else {
        return Value::null();
    }

    const int start = start_value == nullptr
                              ? 0
                              : normalize_slice_index(static_cast<int>(to_number(*start_value)), length);
    const int end = end_value == nullptr
                            ? length
                            : normalize_slice_index(static_cast<int>(to_number(*end_value)), length);
    if (end <= start) {
        return base.type == Value::Type::String ? Value::string("", base.safe) : Value::array(std::vector<Value>());
    }
    const size_t slice_start = static_cast<size_t>(start);
    const size_t slice_length = static_cast<size_t>(end - start);
    if (base.type == Value::Type::String) {
        return Value::string(base.string_value.substr(slice_start, slice_length), base.safe);
    }
    return Value::array(std::vector<Value>(
            base.array_value.begin() + static_cast<std::ptrdiff_t>(slice_start),
            base.array_value.begin() + static_cast<std::ptrdiff_t>(slice_start + slice_length)));
}

struct Node;
struct BlockNode;
struct TemplateRepository;
struct SemanticContext;
using NodePtr = std::unique_ptr<Node>;

struct MacroParam {
    std::string name;
    std::unique_ptr<struct Expression> default_value;
};

struct MacroDefinition {
    const std::vector<MacroParam> *params = nullptr;
    const std::vector<NodePtr> *body = nullptr;
};

struct BlockRenderFrame {
    const std::vector<const BlockNode *> *chain = nullptr;
    size_t index = 0;
};

struct RenderContext {
    std::vector<std::map<std::string, Value>> scopes;
    std::map<std::string, MacroDefinition> macros;
    std::map<std::string, std::vector<const BlockNode *>> block_chains;
    std::vector<BlockRenderFrame> block_stack;
    std::vector<std::string> render_stack;
    TemplateRepository *repository = nullptr;
    int depth = 0;
    std::string error;

    explicit RenderContext(const cJSON *root) {
        Value root_value = value_from_json(root);
        std::map<std::string, Value> base;
        if (root_value.type == Value::Type::Object) {
            for (const auto &entry : root_value.object_value) {
                base.emplace(entry.first, entry.second);
            }
        }
        scopes.push_back(std::move(base));
    }

    Value resolve(const std::string &name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto iterator = scope->find(name);
            if (iterator != scope->end()) {
                return iterator->second;
            }
        }
        return Value::null();
    }

    bool contains(const std::string &name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            if (scope->find(name) != scope->end()) {
                return true;
            }
        }
        return false;
    }

    void set_local(const std::string &name, Value value) {
        if (scopes.empty()) {
            scopes.emplace_back();
        }
        scopes.back()[name] = std::move(value);
    }

    void set(const std::string &name, Value value) {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto iterator = scope->find(name);
            if (iterator != scope->end()) {
                iterator->second = std::move(value);
                return;
            }
        }
        set_local(name, std::move(value));
    }

    void push_scope() {
        scopes.emplace_back();
    }

    void pop_scope() {
        if (scopes.size() > 1) {
            scopes.pop_back();
        }
    }

    bool has_super() const {
        if (block_stack.empty()) {
            return false;
        }
        const BlockRenderFrame &frame = block_stack.back();
        return frame.chain != nullptr && frame.index + 1 < frame.chain->size();
    }

    bool render_super(std::string &out);
    bool render_block_chain(const std::vector<const BlockNode *> &chain, size_t index, std::string &out);
    bool render_template_file(const std::string &path, std::string &out);
    Value resolve_name_or_macro_reference(const std::string &name);
};

struct Expression {
    virtual ~Expression() = default;
    virtual Value eval(RenderContext &context) const = 0;
    virtual bool is_defined(RenderContext &context) const {
        (void) context;
        return true;
    }
    virtual std::string variable_name() const {
        return "";
    }
};

using ExprPtr = std::unique_ptr<Expression>;

struct ConstantExpression final : Expression {
    explicit ConstantExpression(Value value_in) : value(std::move(value_in)) {}
    Value eval(RenderContext &) const override {
        return value;
    }
    Value value;
};

struct ArrayExpression final : Expression {
    explicit ArrayExpression(std::vector<ExprPtr> items_in) : items(std::move(items_in)) {}
    Value eval(RenderContext &context) const override {
        std::vector<Value> values;
        values.reserve(items.size());
        for (const ExprPtr &item : items) {
            values.push_back(item->eval(context));
        }
        return Value::array(std::move(values));
    }
    std::vector<ExprPtr> items;
};

struct ObjectExpression final : Expression {
    explicit ObjectExpression(std::vector<std::pair<std::string, ExprPtr>> items_in)
        : items(std::move(items_in)) {}
    Value eval(RenderContext &context) const override {
        std::map<std::string, Value> values;
        for (const auto &item : items) {
            values[item.first] = item.second == nullptr ? Value::null() : item.second->eval(context);
        }
        return Value::object(std::move(values));
    }
    std::vector<std::pair<std::string, ExprPtr>> items;
};

struct VariableExpression final : Expression {
    explicit VariableExpression(std::string name_in) : name(std::move(name_in)) {}
    Value eval(RenderContext &context) const override {
        return context.resolve_name_or_macro_reference(name);
    }
    bool is_defined(RenderContext &context) const override {
        if (name == "super") {
            return context.has_super();
        }
        return context.contains(name) || context.macros.find(name) != context.macros.end();
    }
    std::string variable_name() const override {
        return name;
    }
    std::string name;
};

struct SubscriptExpression final : Expression {
    SubscriptExpression(ExprPtr base_in, ExprPtr key_in)
        : base(std::move(base_in)), key(std::move(key_in)) {}
    Value eval(RenderContext &context) const override {
        return get_item(base->eval(context), key->eval(context));
    }
    bool is_defined(RenderContext &context) const override {
        return base->is_defined(context) && key->is_defined(context) &&
               has_item(base->eval(context), key->eval(context));
    }
    ExprPtr base;
    ExprPtr key;
};

struct SliceExpression final : Expression {
    SliceExpression(ExprPtr base_in, ExprPtr start_in, ExprPtr end_in)
        : base(std::move(base_in)), start(std::move(start_in)), end(std::move(end_in)) {}
    Value eval(RenderContext &context) const override {
        const Value base_value = base->eval(context);
        Value start_value;
        Value end_value;
        const Value *start_pointer = nullptr;
        const Value *end_pointer = nullptr;
        if (start != nullptr) {
            start_value = start->eval(context);
            start_pointer = &start_value;
        }
        if (end != nullptr) {
            end_value = end->eval(context);
            end_pointer = &end_value;
        }
        return slice_value(base_value, start_pointer, end_pointer);
    }
    bool is_defined(RenderContext &context) const override {
        return base->is_defined(context);
    }
    ExprPtr base;
    ExprPtr start;
    ExprPtr end;
};

struct DefinedTestExpression final : Expression {
    DefinedTestExpression(ExprPtr inner_in, bool negate_in)
        : inner(std::move(inner_in)), negate(negate_in) {}
    Value eval(RenderContext &context) const override {
        const bool defined = inner->is_defined(context);
        return Value::boolean(negate ? !defined : defined);
    }
    ExprPtr inner;
    bool negate = false;
};

struct NoneTestExpression final : Expression {
    NoneTestExpression(ExprPtr inner_in, bool negate_in)
        : inner(std::move(inner_in)), negate(negate_in) {}
    Value eval(RenderContext &context) const override {
        const bool is_none = inner->is_defined(context) && inner->eval(context).type == Value::Type::Null;
        return Value::boolean(negate ? !is_none : is_none);
    }
    ExprPtr inner;
    bool negate = false;
};

bool values_equal(const Value &left, const Value &right);
bool values_less(const Value &left, const Value &right);

struct NamedTestExpression final : Expression {
    NamedTestExpression(ExprPtr inner_in,
                        std::string name_in,
                        bool negate_in,
                        std::vector<ExprPtr> args_in,
                        std::map<std::string, ExprPtr> named_args_in)
        : inner(std::move(inner_in)),
          name(jinja2_tcl::string_utils::lowercase_ascii_copy(std::move(name_in))),
          negate(negate_in),
          args(std::move(args_in)),
          named_args(std::move(named_args_in)) {}
    Value eval(RenderContext &context) const override {
        const Value value = inner->eval(context);
        const bool matched = evaluate(context, value);
        return Value::boolean(negate ? !matched : matched);
    }
    bool evaluate(RenderContext &context, const Value &value) const {
        if (name == "even") {
            return static_cast<long long>(to_number(value)) % 2 == 0;
        }
        if (name == "odd") {
            return static_cast<long long>(to_number(value)) % 2 != 0;
        }
        if (name == "string") {
            return value.type == Value::Type::String;
        }
        if (name == "number") {
            return value.type == Value::Type::Number;
        }
        if (name == "iterable") {
            return value.type == Value::Type::Array || value.type == Value::Type::Object ||
                   value.type == Value::Type::String;
        }
        if (name == "mapping") {
            return value.type == Value::Type::Object;
        }
        if (name == "sequence") {
            return value.type == Value::Type::Array || value.type == Value::Type::String;
        }
        if (name == "lower") {
            const std::string text = to_string_value(value);
            return text == jinja2_tcl::string_utils::lowercase_ascii_copy(text);
        }
        if (name == "upper") {
            std::string upper = to_string_value(value);
            for (char &character : upper) {
                character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
            }
            return to_string_value(value) == upper;
        }
        const Value expected = first_arg(context);
        if (name == "eq" || name == "equalto") {
            return values_equal(value, expected);
        }
        if (name == "ne") {
            return !values_equal(value, expected);
        }
        if (name == "gt" || name == "greaterthan") {
            return values_less(expected, value);
        }
        if (name == "ge") {
            return values_equal(value, expected) || values_less(expected, value);
        }
        if (name == "lt" || name == "lessthan") {
            return values_less(value, expected);
        }
        if (name == "le") {
            return values_equal(value, expected) || values_less(value, expected);
        }
        return false;
    }
    Value first_arg(RenderContext &context) const {
        if (!args.empty() && args.front() != nullptr) {
            return args.front()->eval(context);
        }
        const auto iterator = named_args.find("value");
        if (iterator != named_args.end() && iterator->second != nullptr) {
            return iterator->second->eval(context);
        }
        return Value::null();
    }
    ExprPtr inner;
    std::string name;
    bool negate = false;
    std::vector<ExprPtr> args;
    std::map<std::string, ExprPtr> named_args;
};

struct UnaryExpression final : Expression {
    UnaryExpression(std::string op_in, ExprPtr inner_in)
        : op(std::move(op_in)), inner(std::move(inner_in)) {}
    Value eval(RenderContext &context) const override {
        const Value value = inner->eval(context);
        if (op == "not") {
            return Value::boolean(!truthy(value));
        }
        if (op == "-") {
            return Value::number(-to_number(value));
        }
        return Value::number(to_number(value));
    }
    std::string op;
    ExprPtr inner;
};

bool values_equal(const Value &left, const Value &right) {
    if (left.type == Value::Type::Number || right.type == Value::Type::Number) {
        return std::fabs(to_number(left) - to_number(right)) < 0.0000001;
    }
    if (left.type == Value::Type::Bool || right.type == Value::Type::Bool) {
        return truthy(left) == truthy(right);
    }
    return to_string_value(left) == to_string_value(right);
}

struct BinaryExpression final : Expression {
    BinaryExpression(std::string op_in, ExprPtr left_in, ExprPtr right_in)
        : op(std::move(op_in)), left(std::move(left_in)), right(std::move(right_in)) {}
    Value eval(RenderContext &context) const override {
        if (op == "and") {
            const Value left_value = left->eval(context);
            return truthy(left_value) ? right->eval(context) : left_value;
        }
        if (op == "or") {
            const Value left_value = left->eval(context);
            return truthy(left_value) ? left_value : right->eval(context);
        }

        const Value left_value = left->eval(context);
        const Value right_value = right->eval(context);
        if (op == "+") {
            if (left_value.type == Value::Type::Number && right_value.type == Value::Type::Number) {
                return Value::number(left_value.number_value + right_value.number_value);
            }
            if (left_value.type == Value::Type::Array && right_value.type == Value::Type::Array) {
                std::vector<Value> values;
                values.reserve(left_value.array_value.size() + right_value.array_value.size());
                values.insert(values.end(), left_value.array_value.begin(), left_value.array_value.end());
                values.insert(values.end(), right_value.array_value.begin(), right_value.array_value.end());
                return Value::array(std::move(values));
            }
            return Value::string(to_string_value(left_value) + to_string_value(right_value));
        }
        if (op == "-") {
            return Value::number(to_number(left_value) - to_number(right_value));
        }
        if (op == "*") {
            return Value::number(to_number(left_value) * to_number(right_value));
        }
        if (op == "/") {
            const double divisor = to_number(right_value);
            return Value::number(divisor == 0.0 ? 0.0 : to_number(left_value) / divisor);
        }
        if (op == "//") {
            const double divisor = to_number(right_value);
            return Value::number(divisor == 0.0 ? 0.0 : std::floor(to_number(left_value) / divisor));
        }
        if (op == "%") {
            const long long divisor = static_cast<long long>(to_number(right_value));
            return Value::number(divisor == 0 ? 0 : static_cast<long long>(to_number(left_value)) % divisor);
        }
        if (op == "**") {
            return Value::number(std::pow(to_number(left_value), to_number(right_value)));
        }
        if (op == "~") {
            return Value::string(to_string_value(left_value) + to_string_value(right_value));
        }
        if (op == "==") {
            return Value::boolean(values_equal(left_value, right_value));
        }
        if (op == "!=") {
            return Value::boolean(!values_equal(left_value, right_value));
        }
        if (op == "<") {
            return Value::boolean(to_number(left_value) < to_number(right_value));
        }
        if (op == ">") {
            return Value::boolean(to_number(left_value) > to_number(right_value));
        }
        if (op == "<=") {
            return Value::boolean(to_number(left_value) <= to_number(right_value));
        }
        if (op == ">=") {
            return Value::boolean(to_number(left_value) >= to_number(right_value));
        }
        if (op == "in" || op == "not in") {
            bool found = false;
            if (right_value.type == Value::Type::Array) {
                found = std::any_of(right_value.array_value.begin(),
                                    right_value.array_value.end(),
                                    [&](const Value &candidate) {
                                        return values_equal(left_value, candidate);
                                    });
            } else if (right_value.type == Value::Type::Object) {
                found = right_value.object_value.find(to_string_value(left_value)) !=
                        right_value.object_value.end();
            } else {
                found = to_string_value(right_value).find(to_string_value(left_value)) != std::string::npos;
            }
            return Value::boolean(op == "not in" ? !found : found);
        }
        return Value::null();
    }
    std::string op;
    ExprPtr left;
    ExprPtr right;
};

struct ConditionalExpression final : Expression {
    ConditionalExpression(ExprPtr true_expression_in, ExprPtr condition_in, ExprPtr false_expression_in)
        : true_expression(std::move(true_expression_in)),
          condition(std::move(condition_in)),
          false_expression(std::move(false_expression_in)) {}
    Value eval(RenderContext &context) const override {
        if (truthy(condition->eval(context))) {
            return true_expression->eval(context);
        }
        return false_expression == nullptr ? Value::null() : false_expression->eval(context);
    }
    bool is_defined(RenderContext &context) const override {
        if (truthy(condition->eval(context))) {
            return true_expression->is_defined(context);
        }
        return false_expression != nullptr && false_expression->is_defined(context);
    }
    ExprPtr true_expression;
    ExprPtr condition;
    ExprPtr false_expression;
};

struct FilterCall {
    std::string name;
    std::vector<ExprPtr> args;
    std::map<std::string, ExprPtr> named_args;
};

bool is_supported_method_name(const std::string &name) {
    static const std::vector<std::string> supported_methods = {
            "split", "join", "strip", "trim", "lower", "upper", "replace", "startswith",
            "endswith", "keys", "values", "items", "get"};
    return std::find(supported_methods.begin(), supported_methods.end(), jinja2_tcl::string_utils::lowercase_ascii_copy(name)) !=
           supported_methods.end();
}

const ExprPtr *filter_named_arg(const FilterCall &filter, const std::string &name) {
    const auto iterator = filter.named_args.find(name);
    return iterator == filter.named_args.end() ? nullptr : &iterator->second;
}

Value eval_filter_arg(const FilterCall &filter,
                      RenderContext &context,
                      size_t index,
                      const std::string &name,
                      Value fallback = Value::null()) {
    if (!name.empty()) {
        const ExprPtr *named = filter_named_arg(filter, name);
        if (named != nullptr && *named != nullptr) {
            return (*named)->eval(context);
        }
    }
    if (index < filter.args.size() && filter.args[index] != nullptr) {
        return filter.args[index]->eval(context);
    }
    return fallback;
}

bool has_filter_arg(const FilterCall &filter, size_t index, const std::string &name) {
    if (!name.empty() && filter_named_arg(filter, name) != nullptr) {
        return true;
    }
    return index < filter.args.size() && filter.args[index] != nullptr;
}

const ExprPtr *method_named_arg(const std::map<std::string, ExprPtr> &named_args, const std::string &name) {
    const auto iterator = named_args.find(name);
    return iterator == named_args.end() ? nullptr : &iterator->second;
}

bool has_method_arg(const std::vector<ExprPtr> &args,
                    const std::map<std::string, ExprPtr> &named_args,
                    size_t index,
                    const std::string &name) {
    if (!name.empty() && method_named_arg(named_args, name) != nullptr) {
        return true;
    }
    return index < args.size() && args[index] != nullptr;
}

Value eval_method_arg(const std::vector<ExprPtr> &args,
                      const std::map<std::string, ExprPtr> &named_args,
                      RenderContext &context,
                      size_t index,
                      const std::string &name,
                      Value fallback = Value::null()) {
    if (!name.empty()) {
        const ExprPtr *named = method_named_arg(named_args, name);
        if (named != nullptr && *named != nullptr) {
            return (*named)->eval(context);
        }
    }
    if (index < args.size() && args[index] != nullptr) {
        return args[index]->eval(context);
    }
    return fallback;
}

Value get_attribute_path(const Value &value, const std::string &path) {
    Value current = value;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t dot = path.find('.', start);
        const std::string segment = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!segment.empty()) {
            current = get_item(current, Value::string(segment));
        }
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return current;
}

bool values_less(const Value &left, const Value &right) {
    if (left.type == Value::Type::Number || right.type == Value::Type::Number) {
        return to_number(left) < to_number(right);
    }
    return to_string_value(left) < to_string_value(right);
}

Value list_from_value(const Value &input) {
    if (input.type == Value::Type::Array) {
        return input;
    }
    std::vector<Value> values;
    if (input.type == Value::Type::String) {
        values.reserve(input.string_value.size());
        for (const char character : input.string_value) {
            values.push_back(Value::string(std::string(1, character)));
        }
        return Value::array(std::move(values));
    }
    if (input.type == Value::Type::Object) {
        values.reserve(input.object_value.size());
        for (const auto &entry : input.object_value) {
            values.push_back(Value::string(entry.first));
        }
        return Value::array(std::move(values));
    }
    return Value::array(std::move(values));
}

std::vector<Value> split_whitespace(const std::string &value, int maxsplit, bool safe) {
    std::vector<Value> parts;
    size_t position = 0;
    int splits = 0;
    while (position < value.size()) {
        while (position < value.size() &&
               std::isspace(static_cast<unsigned char>(value[position])) != 0) {
            ++position;
        }
        if (position >= value.size()) {
            break;
        }
        if (maxsplit >= 0 && splits >= maxsplit) {
            std::string tail = value.substr(position);
            size_t end = tail.size();
            while (end > 0 && std::isspace(static_cast<unsigned char>(tail[end - 1])) != 0) {
                --end;
            }
            parts.push_back(Value::string(tail.substr(0, end), safe));
            return parts;
        }
        const size_t start = position;
        while (position < value.size() &&
               std::isspace(static_cast<unsigned char>(value[position])) == 0) {
            ++position;
        }
        parts.push_back(Value::string(value.substr(start, position - start), safe));
        ++splits;
    }
    return parts;
}

std::vector<Value> split_by_separator(const std::string &value,
                                      const std::string &separator,
                                      int maxsplit,
                                      bool safe) {
    std::vector<Value> parts;
    if (separator.empty()) {
        parts.push_back(Value::string(value, safe));
        return parts;
    }
    size_t start = 0;
    int splits = 0;
    while (start <= value.size()) {
        if (maxsplit >= 0 && splits >= maxsplit) {
            parts.push_back(Value::string(value.substr(start), safe));
            break;
        }
        const size_t position = value.find(separator, start);
        if (position == std::string::npos) {
            parts.push_back(Value::string(value.substr(start), safe));
            break;
        }
        parts.push_back(Value::string(value.substr(start, position - start), safe));
        start = position + separator.size();
        ++splits;
    }
    return parts;
}

bool contains_strip_character(const std::string &characters, char character) {
    return characters.find(character) != std::string::npos;
}

std::string strip_string(const std::string &value, const std::string *characters) {
    size_t begin = 0;
    while (begin < value.size()) {
        const char character = value[begin];
        const bool strip = characters == nullptr
                                   ? std::isspace(static_cast<unsigned char>(character)) != 0
                                   : contains_strip_character(*characters, character);
        if (!strip) {
            break;
        }
        ++begin;
    }
    size_t end = value.size();
    while (end > begin) {
        const char character = value[end - 1];
        const bool strip = characters == nullptr
                                   ? std::isspace(static_cast<unsigned char>(character)) != 0
                                   : contains_strip_character(*characters, character);
        if (!strip) {
            break;
        }
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string strip_tags(const std::string &value) {
    std::string stripped;
    bool in_tag = false;
    for (const char character : value) {
        if (character == '<') {
            in_tag = true;
            continue;
        }
        if (character == '>') {
            in_tag = false;
            continue;
        }
        if (!in_tag) {
            stripped.push_back(character);
        }
    }
    return stripped;
}

std::vector<Value> eval_filter_positional_args(const FilterCall &filter,
                                               RenderContext &context,
                                               size_t start_index) {
    std::vector<Value> values;
    for (size_t index = start_index; index < filter.args.size(); ++index) {
        values.push_back(filter.args[index] == nullptr ? Value::null() : filter.args[index]->eval(context));
    }
    return values;
}

std::map<std::string, Value> eval_filter_named_args(const FilterCall &filter, RenderContext &context) {
    std::map<std::string, Value> values;
    for (const auto &entry : filter.named_args) {
        values[entry.first] = entry.second == nullptr ? Value::null() : entry.second->eval(context);
    }
    return values;
}

Value first_named_or_positional_value(const std::vector<Value> &args,
                                      const std::map<std::string, Value> &named_args,
                                      const std::string &name) {
    if (!name.empty()) {
        const auto named = named_args.find(name);
        if (named != named_args.end()) {
            return named->second;
        }
    }
    return args.empty() ? Value::null() : args.front();
}

bool evaluate_filter_test(const std::string &test_name,
                          const Value &value,
                          const std::vector<Value> &args,
                          const std::map<std::string, Value> &named_args) {
    const std::string name = jinja2_tcl::string_utils::lowercase_ascii_copy(test_name);
    if (name.empty()) {
        return truthy(value);
    }
    if (name == "even") {
        return static_cast<long long>(to_number(value)) % 2 == 0;
    }
    if (name == "odd") {
        return static_cast<long long>(to_number(value)) % 2 != 0;
    }
    if (name == "string") {
        return value.type == Value::Type::String;
    }
    if (name == "number") {
        return value.type == Value::Type::Number;
    }
    if (name == "iterable") {
        return value.type == Value::Type::Array || value.type == Value::Type::Object ||
               value.type == Value::Type::String;
    }
    if (name == "mapping") {
        return value.type == Value::Type::Object;
    }
    if (name == "sequence") {
        return value.type == Value::Type::Array || value.type == Value::Type::String;
    }
    if (name == "lower") {
        const std::string text = to_string_value(value);
        return text == jinja2_tcl::string_utils::lowercase_ascii_copy(text);
    }
    if (name == "upper") {
        std::string upper = to_string_value(value);
        for (char &character : upper) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        return to_string_value(value) == upper;
    }
    if (name == "none") {
        return value.type == Value::Type::Null;
    }
    const Value expected = first_named_or_positional_value(args, named_args, "value");
    if (name == "eq" || name == "equalto") {
        return values_equal(value, expected);
    }
    if (name == "ne") {
        return !values_equal(value, expected);
    }
    if (name == "gt" || name == "greaterthan") {
        return values_less(expected, value);
    }
    if (name == "ge") {
        return values_equal(value, expected) || values_less(expected, value);
    }
    if (name == "lt" || name == "lessthan") {
        return values_less(value, expected);
    }
    if (name == "le") {
        return values_equal(value, expected) || values_less(value, expected);
    }
    return false;
}

std::string lowercase_if_needed(const std::string &value, bool case_sensitive) {
    return case_sensitive ? value : jinja2_tcl::string_utils::lowercase_ascii_copy(value);
}

std::string unique_key_for_value(const Value &value, bool case_sensitive) {
    if (value.type == Value::Type::String) {
        return "s:" + lowercase_if_needed(value.string_value, case_sensitive);
    }
    if (value.type == Value::Type::Number) {
        return "n:" + number_to_string(value.number_value);
    }
    if (value.type == Value::Type::Bool) {
        return value.bool_value ? "b:true" : "b:false";
    }
    if (value.type == Value::Type::Null) {
        return "z:null";
    }
    return "j:" + to_json_value(value);
}

std::string format_with_values(const std::string &format, const std::vector<Value> &args) {
    std::string result;
    size_t arg_index = 0;
    for (size_t index = 0; index < format.size(); ++index) {
        if (format[index] != '%' || index + 1 >= format.size()) {
            result.push_back(format[index]);
            continue;
        }
        if (format[index + 1] == '%') {
            result.push_back('%');
            ++index;
            continue;
        }

        const size_t spec_start = index;
        ++index;
        while (index < format.size() && std::string("-+ #0").find(format[index]) != std::string::npos) {
            ++index;
        }
        while (index < format.size() && std::isdigit(static_cast<unsigned char>(format[index])) != 0) {
            ++index;
        }
        if (index < format.size() && format[index] == '.') {
            ++index;
            while (index < format.size() && std::isdigit(static_cast<unsigned char>(format[index])) != 0) {
                ++index;
            }
        }
        if (index >= format.size()) {
            result += format.substr(spec_start);
            break;
        }

        const char conversion = format[index];
        const std::string spec = format.substr(spec_start, index - spec_start + 1);
        const Value arg = arg_index < args.size() ? args[arg_index++] : Value::null();
        char buffer[256];
        if (conversion == 's') {
            std::snprintf(buffer, sizeof(buffer), spec.c_str(), to_string_value(arg).c_str());
            result += buffer;
        } else if (conversion == 'd' || conversion == 'i') {
            std::snprintf(buffer, sizeof(buffer), spec.c_str(), static_cast<int>(to_number(arg)));
            result += buffer;
        } else if (conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X') {
            std::snprintf(buffer, sizeof(buffer), spec.c_str(), static_cast<unsigned int>(to_number(arg)));
            result += buffer;
        } else if (conversion == 'f' || conversion == 'F' || conversion == 'e' ||
                   conversion == 'E' || conversion == 'g' || conversion == 'G') {
            std::snprintf(buffer, sizeof(buffer), spec.c_str(), to_number(arg));
            result += buffer;
        } else {
            result += spec;
        }
    }
    return result;
}

Value apply_method(const Value &input,
                   const std::string &method_name,
                   const std::vector<ExprPtr> &args,
                   const std::map<std::string, ExprPtr> &named_args,
                   RenderContext &context) {
    const std::string name = jinja2_tcl::string_utils::lowercase_ascii_copy(method_name);
    if (name == "split") {
        const bool has_separator = has_method_arg(args, named_args, 0, "sep");
        const Value separator_value = eval_method_arg(args, named_args, context, 0, "sep");
        const int maxsplit = static_cast<int>(
                to_number(eval_method_arg(args, named_args, context, 1, "maxsplit", Value::number(-1))));
        const std::string value = to_string_value(input);
        if (!has_separator || separator_value.type == Value::Type::Null) {
            return Value::array(split_whitespace(value, maxsplit, input.safe));
        }
        return Value::array(split_by_separator(value, to_string_value(separator_value), maxsplit, input.safe));
    }
    if (name == "join") {
        if (input.type == Value::Type::Array) {
            const std::string separator = to_string_value(
                    eval_method_arg(args, named_args, context, 0, "separator", Value::string("")));
            std::string value;
            for (size_t index = 0; index < input.array_value.size(); ++index) {
                if (index > 0) {
                    value += separator;
                }
                value += to_string_value(input.array_value[index]);
            }
            return Value::string(value);
        }
        const Value iterable = eval_method_arg(args, named_args, context, 0, "iterable");
        const Value values = list_from_value(iterable);
        const std::string separator = to_string_value(input);
        std::string value;
        for (size_t index = 0; index < values.array_value.size(); ++index) {
            if (index > 0) {
                value += separator;
            }
            value += to_string_value(values.array_value[index]);
        }
        return Value::string(value);
    }
    if (name == "strip" || name == "trim") {
        std::string characters;
        const bool has_characters = has_method_arg(args, named_args, 0, "chars");
        if (has_characters) {
            characters = to_string_value(eval_method_arg(args, named_args, context, 0, "chars"));
        }
        return Value::string(strip_string(to_string_value(input), has_characters ? &characters : nullptr),
                             input.safe);
    }
    if (name == "lower") {
        return Value::string(jinja2_tcl::string_utils::lowercase_ascii_copy(to_string_value(input)), input.safe);
    }
    if (name == "upper") {
        std::string value = to_string_value(input);
        for (char &character : value) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        return Value::string(value, input.safe);
    }
    if (name == "replace") {
        const std::string old_value = to_string_value(eval_method_arg(args, named_args, context, 0, "old"));
        const std::string new_value = to_string_value(eval_method_arg(args, named_args, context, 1, "new"));
        const int count = static_cast<int>(
                to_number(eval_method_arg(args, named_args, context, 2, "count", Value::number(-1))));
        if (old_value.empty()) {
            return input;
        }
        std::string value = to_string_value(input);
        size_t position = 0;
        int replacements = 0;
        while ((position = value.find(old_value, position)) != std::string::npos &&
               (count < 0 || replacements < count)) {
            value.replace(position, old_value.size(), new_value);
            position += new_value.size();
            ++replacements;
        }
        return Value::string(value, input.safe);
    }
    if (name == "startswith" || name == "endswith") {
        const std::string value = to_string_value(input);
        const Value expected = eval_method_arg(args, named_args, context, 0, name == "startswith" ? "prefix" : "suffix");
        auto matches_one = [&](const Value &candidate) {
            const std::string text = to_string_value(candidate);
            if (name == "startswith") {
                return value.size() >= text.size() && value.compare(0, text.size(), text) == 0;
            }
            return value.size() >= text.size() &&
                   value.compare(value.size() - text.size(), text.size(), text) == 0;
        };
        if (expected.type == Value::Type::Array) {
            return Value::boolean(std::any_of(expected.array_value.begin(),
                                              expected.array_value.end(),
                                              matches_one));
        }
        return Value::boolean(matches_one(expected));
    }
    if (name == "keys") {
        std::vector<Value> values;
        if (input.type == Value::Type::Object) {
            values.reserve(input.object_value.size());
            for (const auto &entry : input.object_value) {
                values.push_back(Value::string(entry.first));
            }
        }
        return Value::array(std::move(values));
    }
    if (name == "values") {
        std::vector<Value> values;
        if (input.type == Value::Type::Object) {
            values.reserve(input.object_value.size());
            for (const auto &entry : input.object_value) {
                values.push_back(entry.second);
            }
        }
        return Value::array(std::move(values));
    }
    if (name == "items") {
        std::vector<Value> values;
        if (input.type == Value::Type::Object) {
            values.reserve(input.object_value.size());
            for (const auto &entry : input.object_value) {
                std::vector<Value> pair;
                pair.push_back(Value::string(entry.first));
                pair.push_back(entry.second);
                values.push_back(Value::array(std::move(pair)));
            }
        }
        return Value::array(std::move(values));
    }
    if (name == "get") {
        const std::string key = to_string_value(eval_method_arg(args, named_args, context, 0, "key"));
        const bool has_default = has_method_arg(args, named_args, 1, "default");
        if (input.type == Value::Type::Object) {
            const auto iterator = input.object_value.find(key);
            if (iterator != input.object_value.end()) {
                return iterator->second;
            }
        }
        return has_default ? eval_method_arg(args, named_args, context, 1, "default") : Value::null();
    }
    context.error = "unsupported method: " + method_name;
    return Value::null();
}

Value apply_filter(const Value &input, const FilterCall &filter, RenderContext &context) {
    const std::string name = jinja2_tcl::string_utils::lowercase_ascii_copy(filter.name);
    if (name == "escape" || name == "e") {
        return Value::string(jinja2_tcl::string_utils::html_escape(to_string_value(input)), true);
    }
    if (name == "safe") {
        return Value::string(to_string_value(input), true);
    }
    if (name == "default" || name == "d") {
        if (truthy(input)) {
            return input;
        }
        return eval_filter_arg(filter, context, 0, "default_value", Value::string(""));
    }
    if (name == "abs") {
        return Value::number(std::fabs(to_number(input)));
    }
    if (name == "int") {
        const double value = to_number(input);
        return Value::number(value < 0 ? std::ceil(value) : std::floor(value));
    }
    if (name == "float") {
        return Value::number(to_number(input));
    }
    if (name == "round") {
        return Value::number(std::round(to_number(input)));
    }
    if (name == "length") {
        if (input.type == Value::Type::Array) {
            return Value::number(static_cast<double>(input.array_value.size()));
        }
        if (input.type == Value::Type::Object) {
            return Value::number(static_cast<double>(input.object_value.size()));
        }
        return Value::number(static_cast<double>(to_string_value(input).size()));
    }
    if (name == "attr") {
        const std::string attribute = to_string_value(eval_filter_arg(filter, context, 0, "name"));
        return attribute.empty() ? Value::null() : get_attribute_path(input, attribute);
    }
    if (name == "format") {
        return Value::string(format_with_values(to_string_value(input),
                                                eval_filter_positional_args(filter, context, 0)),
                             input.safe);
    }
    if (name == "lower") {
        return Value::string(jinja2_tcl::string_utils::lowercase_ascii_copy(to_string_value(input)));
    }
    if (name == "upper") {
        std::string value = to_string_value(input);
        for (char &character : value) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        return Value::string(value);
    }
    if (name == "trim") {
        return Value::string(jinja2_tcl::string_utils::trim_ascii_copy(to_string_value(input)));
    }
    if (name == "replace") {
        const std::string old_value = to_string_value(eval_filter_arg(filter, context, 0, "old"));
        const std::string new_value = to_string_value(eval_filter_arg(filter, context, 1, "new"));
        if (old_value.empty()) {
            return input;
        }
        std::string value = to_string_value(input);
        size_t position = 0;
        while ((position = value.find(old_value, position)) != std::string::npos) {
            value.replace(position, old_value.size(), new_value);
            position += new_value.size();
        }
        return Value::string(value, input.safe);
    }
    if (name == "truncate") {
        const int length = static_cast<int>(to_number(eval_filter_arg(filter, context, 0, "length", Value::number(255))));
        const std::string suffix = to_string_value(eval_filter_arg(filter, context, 1, "end", Value::string("...")));
        std::string value = to_string_value(input);
        if (length < 0 || static_cast<int>(value.size()) <= length) {
            return input;
        }
        if (length <= static_cast<int>(suffix.size())) {
            return Value::string(value.substr(0, static_cast<size_t>(length)), input.safe);
        }
        return Value::string(value.substr(0, static_cast<size_t>(length - static_cast<int>(suffix.size()))) + suffix,
                             input.safe);
    }
    if (name == "urlencode") {
        return Value::string(jinja2_tcl::string_utils::percent_encode_uri_component(to_string_value(input)));
    }
    if (name == "wordcount") {
        const std::string value = strip_tags(to_string_value(input));
        bool in_word = false;
        int count = 0;
        for (const unsigned char byte : value) {
            if (std::isalnum(byte) != 0) {
                if (!in_word) {
                    ++count;
                    in_word = true;
                }
            } else {
                in_word = false;
            }
        }
        return Value::number(count);
    }
    if (name == "striptags") {
        return Value::string(strip_tags(to_string_value(input)));
    }
    if (name == "capitalize") {
        std::string value = jinja2_tcl::string_utils::lowercase_ascii_copy(to_string_value(input));
        if (!value.empty()) {
            value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
        }
        return Value::string(value);
    }
    if (name == "title") {
        std::string value = jinja2_tcl::string_utils::lowercase_ascii_copy(to_string_value(input));
        bool start_word = true;
        for (char &character : value) {
            if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
                if (start_word) {
                    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
                }
                start_word = false;
            } else {
                start_word = true;
            }
        }
        return Value::string(value);
    }
    if (name == "first") {
        if (input.type == Value::Type::Array && !input.array_value.empty()) {
            return input.array_value.front();
        }
        const std::string value = to_string_value(input);
        return value.empty() ? Value::null() : Value::string(value.substr(0, 1));
    }
    if (name == "last") {
        if (input.type == Value::Type::Array && !input.array_value.empty()) {
            return input.array_value.back();
        }
        const std::string value = to_string_value(input);
        return value.empty() ? Value::null() : Value::string(value.substr(value.size() - 1));
    }
    if (name == "list") {
        return list_from_value(input);
    }
    if (name == "map") {
        const Value values = list_from_value(input);
        std::vector<Value> mapped;
        mapped.reserve(values.array_value.size());
        const ExprPtr *attribute_arg = filter_named_arg(filter, "attribute");
        if (attribute_arg != nullptr && *attribute_arg != nullptr) {
            const std::string attribute = to_string_value((*attribute_arg)->eval(context));
            const bool has_default = has_filter_arg(filter, 0, "default");
            const Value default_value = eval_filter_arg(filter, context, 0, "default");
            for (const Value &item : values.array_value) {
                Value mapped_value = get_attribute_path(item, attribute);
                if (mapped_value.type == Value::Type::Null && has_default) {
                    mapped_value = default_value;
                }
                mapped.push_back(std::move(mapped_value));
            }
            return Value::array(std::move(mapped));
        }
        if (filter.args.empty() || filter.args.front() == nullptr) {
            return values;
        }
        const std::string mapped_filter_name = to_string_value(filter.args.front()->eval(context));
        const std::vector<Value> mapped_args = eval_filter_positional_args(filter, context, 1);
        const std::map<std::string, Value> mapped_named_args = eval_filter_named_args(filter, context);
        for (const Value &item : values.array_value) {
            FilterCall mapped_filter;
            mapped_filter.name = mapped_filter_name;
            for (const Value &arg : mapped_args) {
                mapped_filter.args.push_back(std::make_unique<ConstantExpression>(arg));
            }
            for (const auto &arg : mapped_named_args) {
                mapped_filter.named_args[arg.first] = std::make_unique<ConstantExpression>(arg.second);
            }
            mapped.push_back(apply_filter(item, mapped_filter, context));
            if (!context.error.empty()) {
                return Value::null();
            }
        }
        return Value::array(std::move(mapped));
    }
    if (name == "select" || name == "reject") {
        const Value values = list_from_value(input);
        const bool has_test = !filter.args.empty() && filter.args.front() != nullptr;
        const std::string test_name = has_test ? to_string_value(filter.args.front()->eval(context)) : "";
        const std::vector<Value> test_args = eval_filter_positional_args(filter, context, has_test ? 1 : 0);
        const std::map<std::string, Value> test_named_args = eval_filter_named_args(filter, context);
        std::vector<Value> selected;
        for (const Value &item : values.array_value) {
            const bool matched = evaluate_filter_test(test_name, item, test_args, test_named_args);
            if ((name == "select" && matched) || (name == "reject" && !matched)) {
                selected.push_back(item);
            }
        }
        return Value::array(std::move(selected));
    }
    if (name == "selectattr" || name == "rejectattr") {
        const Value values = list_from_value(input);
        const std::string attribute = to_string_value(eval_filter_arg(filter, context, 0, "attribute"));
        if (attribute.empty()) {
            return Value::array(std::vector<Value>());
        }
        const bool has_test = filter.args.size() > 1 && filter.args[1] != nullptr;
        const std::string test_name = has_test ? to_string_value(filter.args[1]->eval(context)) : "";
        std::vector<Value> test_args = eval_filter_positional_args(filter, context, has_test ? 2 : 1);
        std::map<std::string, Value> test_named_args = eval_filter_named_args(filter, context);
        test_named_args.erase("attribute");
        std::vector<Value> selected;
        for (const Value &item : values.array_value) {
            const Value attribute_value = get_attribute_path(item, attribute);
            const bool matched = evaluate_filter_test(test_name, attribute_value, test_args, test_named_args);
            if ((name == "selectattr" && matched) || (name == "rejectattr" && !matched)) {
                selected.push_back(item);
            }
        }
        return Value::array(std::move(selected));
    }
    if (name == "reverse") {
        if (input.type == Value::Type::Array) {
            std::vector<Value> values = input.array_value;
            std::reverse(values.begin(), values.end());
            return Value::array(std::move(values));
        }
        std::string value = to_string_value(input);
        std::reverse(value.begin(), value.end());
        return Value::string(value, input.safe);
    }
    if (name == "sort") {
        if (input.type != Value::Type::Array) {
            return input;
        }
        const bool reverse = truthy(eval_filter_arg(filter, context, 0, "reverse", Value::boolean(false)));
        const std::string attribute = to_string_value(eval_filter_arg(filter, context, 1, "attribute"));
        std::vector<Value> values = input.array_value;
        std::sort(values.begin(), values.end(), [&](const Value &left, const Value &right) {
            const Value left_key = attribute.empty() ? left : get_attribute_path(left, attribute);
            const Value right_key = attribute.empty() ? right : get_attribute_path(right, attribute);
            return reverse ? values_less(right_key, left_key) : values_less(left_key, right_key);
        });
        return Value::array(std::move(values));
    }
    if (name == "unique") {
        if (input.type != Value::Type::Array) {
            return list_from_value(input);
        }
        const bool case_sensitive = truthy(eval_filter_arg(filter, context, 0, "case_sensitive", Value::boolean(false)));
        const std::string attribute = to_string_value(eval_filter_arg(filter, context, 1, "attribute"));
        std::map<std::string, bool> seen;
        std::vector<Value> values;
        for (const Value &item : input.array_value) {
            const Value key_value = attribute.empty() ? item : get_attribute_path(item, attribute);
            const std::string key = unique_key_for_value(key_value, case_sensitive);
            if (seen.find(key) != seen.end()) {
                continue;
            }
            seen[key] = true;
            values.push_back(item);
        }
        return Value::array(std::move(values));
    }
    if (name == "min" || name == "max") {
        if (input.type != Value::Type::Array || input.array_value.empty()) {
            return Value::null();
        }
        const std::string attribute = to_string_value(eval_filter_arg(filter, context, 0, "attribute"));
        Value best = input.array_value.front();
        for (const Value &candidate : input.array_value) {
            const Value best_key = attribute.empty() ? best : get_attribute_path(best, attribute);
            const Value candidate_key = attribute.empty() ? candidate : get_attribute_path(candidate, attribute);
            const bool take = name == "min" ? values_less(candidate_key, best_key) : values_less(best_key, candidate_key);
            if (take) {
                best = candidate;
            }
        }
        return best;
    }
    if (name == "sum") {
        if (input.type != Value::Type::Array) {
            return Value::number(0);
        }
        const std::string attribute = to_string_value(eval_filter_arg(filter, context, 0, "attribute"));
        double total = to_number(eval_filter_arg(filter, context, 1, "start", Value::number(0)));
        for (const Value &item : input.array_value) {
            total += to_number(attribute.empty() ? item : get_attribute_path(item, attribute));
        }
        return Value::number(total);
    }
    if (name == "groupby") {
        if (input.type != Value::Type::Array) {
            return Value::array(std::vector<Value>());
        }
        const std::string attribute = to_string_value(eval_filter_arg(filter, context, 0, "attribute"));
        const bool case_sensitive = truthy(eval_filter_arg(filter, context, 1, "case_sensitive", Value::boolean(false)));
        struct Group {
            Value grouper;
            std::vector<Value> list;
        };
        std::map<std::string, Group> groups;
        for (const Value &item : input.array_value) {
            const Value grouper = attribute.empty() ? item : get_attribute_path(item, attribute);
            const std::string key = unique_key_for_value(grouper, case_sensitive);
            Group &group = groups[key];
            if (group.list.empty()) {
                group.grouper = grouper;
            }
            group.list.push_back(item);
        }
        std::vector<Value> values;
        values.reserve(groups.size());
        for (auto &entry : groups) {
            std::map<std::string, Value> group;
            group.emplace("grouper", entry.second.grouper);
            group.emplace("list", Value::array(std::move(entry.second.list)));
            values.push_back(Value::object(std::move(group)));
        }
        return Value::array(std::move(values));
    }
    if (name == "dictsort") {
        if (input.type != Value::Type::Object) {
            return Value::array(std::vector<Value>());
        }
        std::vector<Value> values;
        values.reserve(input.object_value.size());
        for (const auto &entry : input.object_value) {
            std::map<std::string, Value> pair;
            pair.emplace("key", Value::string(entry.first));
            pair.emplace("value", entry.second);
            values.push_back(Value::object(std::move(pair)));
        }
        return Value::array(std::move(values));
    }
    if (name == "batch" || name == "slice") {
        if (input.type != Value::Type::Array) {
            return Value::null();
        }
        const int size = static_cast<int>(to_number(eval_filter_arg(filter, context, 0, name == "batch" ? "linecount" : "slices")));
        if (size <= 0) {
            return Value::array(std::vector<Value>());
        }
        const Value fill = eval_filter_arg(filter, context, 1, "fill_with");
        const bool has_fill = fill.type != Value::Type::Null;
        std::vector<Value> groups;
        for (size_t index = 0; index < input.array_value.size(); index += static_cast<size_t>(size)) {
            std::vector<Value> group;
            for (int offset = 0; offset < size && index + static_cast<size_t>(offset) < input.array_value.size(); ++offset) {
                group.push_back(input.array_value[index + static_cast<size_t>(offset)]);
            }
            while (has_fill && static_cast<int>(group.size()) < size) {
                group.push_back(fill);
            }
            groups.push_back(Value::array(std::move(group)));
        }
        return Value::array(std::move(groups));
    }
    if (name == "join") {
        const std::string separator = to_string_value(eval_filter_arg(filter, context, 0, "d", Value::string("")));
        const std::string attribute = to_string_value(eval_filter_arg(filter, context, 1, "attribute"));
        if (input.type != Value::Type::Array) {
            return Value::string(to_string_value(input));
        }
        std::string value;
        for (size_t index = 0; index < input.array_value.size(); ++index) {
            if (index > 0) {
                value += separator;
            }
            value += to_string_value(attribute.empty() ? input.array_value[index]
                                                       : get_attribute_path(input.array_value[index], attribute));
        }
        return Value::string(value);
    }
    if (name == "tojson") {
        return Value::string(to_json_value(input), true);
    }
    return input;
}

struct FilteredExpression final : Expression {
    FilteredExpression(ExprPtr inner_in, std::vector<FilterCall> filters_in)
        : inner(std::move(inner_in)), filters(std::move(filters_in)) {}
    Value eval(RenderContext &context) const override {
        Value value = inner->eval(context);
        for (const FilterCall &filter : filters) {
            value = apply_filter(value, filter, context);
        }
        return value;
    }
    ExprPtr inner;
    std::vector<FilterCall> filters;
};

struct MethodCallExpression final : Expression {
    MethodCallExpression(ExprPtr base_in,
                         std::string method_name_in,
                         std::vector<ExprPtr> args_in,
                         std::map<std::string, ExprPtr> named_args_in)
        : base(std::move(base_in)),
          method_name(jinja2_tcl::string_utils::lowercase_ascii_copy(std::move(method_name_in))),
          args(std::move(args_in)),
          named_args(std::move(named_args_in)) {}
    Value eval(RenderContext &context) const override {
        return apply_method(base->eval(context), method_name, args, named_args, context);
    }
    bool is_defined(RenderContext &context) const override {
        return base->is_defined(context);
    }
    ExprPtr base;
    std::string method_name;
    std::vector<ExprPtr> args;
    std::map<std::string, ExprPtr> named_args;
};

struct CallExpression final : Expression {
    CallExpression(ExprPtr callee_in, std::vector<ExprPtr> args_in, std::map<std::string, ExprPtr> named_args_in)
        : callee(std::move(callee_in)), args(std::move(args_in)), named_args(std::move(named_args_in)) {}
    Value eval(RenderContext &context) const override;
    bool is_defined(RenderContext &context) const override {
        const std::string macro_name = callee->variable_name();
        if (macro_name == "range") {
            return true;
        }
        return !macro_name.empty() && context.macros.find(macro_name) != context.macros.end();
    }
    ExprPtr callee;
    std::vector<ExprPtr> args;
    std::map<std::string, ExprPtr> named_args;
};

struct Node {
    virtual ~Node() = default;
    virtual bool render(RenderContext &context, std::string &out) const = 0;
    virtual void collect_includes(std::vector<std::string> &) const {}
    virtual bool allowed_in_extending_template_root() const {
        return false;
    }
};

bool render_nodes(const std::vector<NodePtr> &nodes, RenderContext &context, std::string &out) {
    for (const NodePtr &node : nodes) {
        if (!node->render(context, out)) {
            return false;
        }
    }
    return true;
}

void collect_node_includes(const std::vector<NodePtr> &nodes, std::vector<std::string> &paths) {
    for (const NodePtr &node : nodes) {
        node->collect_includes(paths);
    }
}

bool macro_has_zero_required_parameters(const MacroDefinition &macro) {
    if (macro.params == nullptr) {
        return true;
    }
    for (const MacroParam &param : *macro.params) {
        if (param.default_value == nullptr) {
            return false;
        }
    }
    return true;
}

bool is_range_keyword(const std::string &name) {
    return name == "start" || name == "stop" || name == "step";
}

bool eval_number_argument(const ExprPtr &expression,
                          RenderContext &context,
                          const std::string &name,
                          double &out) {
    if (expression == nullptr) {
        context.error = name + " requires a value";
        return false;
    }
    out = to_number(expression->eval(context));
    return context.error.empty();
}

Value eval_range_call(const CallExpression &call, RenderContext &context) {
    if (call.args.empty() && call.named_args.empty()) {
        context.error = "range() requires at least a stop argument";
        return Value::null();
    }
    if (call.args.size() > 3) {
        context.error = "range() accepts at most three arguments";
        return Value::null();
    }
    for (const auto &entry : call.named_args) {
        if (!is_range_keyword(entry.first)) {
            context.error = "range() got an unexpected keyword argument: " + entry.first;
            return Value::null();
        }
    }
    if (!call.args.empty() && !call.named_args.empty()) {
        context.error = "range() does not support mixing positional and keyword arguments";
        return Value::null();
    }

    double start = 0.0;
    double stop = 0.0;
    double step = 1.0;
    if (!call.named_args.empty()) {
        const auto stop_arg = call.named_args.find("stop");
        if (stop_arg == call.named_args.end()) {
            context.error = "range() keyword form requires stop";
            return Value::null();
        }
        const auto start_arg = call.named_args.find("start");
        const auto step_arg = call.named_args.find("step");
        if (start_arg != call.named_args.end() &&
            !eval_number_argument(start_arg->second, context, "range() start", start)) {
            return Value::null();
        }
        if (!eval_number_argument(stop_arg->second, context, "range() stop", stop)) {
            return Value::null();
        }
        if (step_arg != call.named_args.end() &&
            !eval_number_argument(step_arg->second, context, "range() step", step)) {
            return Value::null();
        }
    } else if (call.args.size() == 1) {
        if (!eval_number_argument(call.args[0], context, "range() stop", stop)) {
            return Value::null();
        }
    } else {
        if (!eval_number_argument(call.args[0], context, "range() start", start) ||
            !eval_number_argument(call.args[1], context, "range() stop", stop)) {
            return Value::null();
        }
        if (call.args.size() == 3 &&
            !eval_number_argument(call.args[2], context, "range() step", step)) {
            return Value::null();
        }
    }

    if (!std::isfinite(start) || !std::isfinite(stop) || !std::isfinite(step)) {
        context.error = "range() arguments must be finite numbers";
        return Value::null();
    }
    if (std::fabs(step) < 0.0000001) {
        context.error = "range() step must not be zero";
        return Value::null();
    }

    std::vector<Value> values;
    constexpr size_t kMaxRangeItems = 10000;
    for (double value = start; step > 0.0 ? value < stop : value > stop; value += step) {
        if (values.size() >= kMaxRangeItems) {
            context.error = "range() result exceeds the maximum supported length";
            return Value::null();
        }
        values.push_back(Value::number(value));
        if (!std::isfinite(value + step)) {
            context.error = "range() arguments must produce finite numbers";
            return Value::null();
        }
    }
    return Value::array(std::move(values));
}

Value RenderContext::resolve_name_or_macro_reference(const std::string &name) {
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
        const auto iterator = scope->find(name);
        if (iterator != scope->end()) {
            return iterator->second;
        }
    }

    const auto macro_iterator = macros.find(name);
    if (macro_iterator == macros.end()) {
        return Value::null();
    }
    const MacroDefinition &macro = macro_iterator->second;
    if (!macro_has_zero_required_parameters(macro)) {
        return Value::null();
    }
    if (depth > 32) {
        error = "macro recursion limit exceeded";
        return Value::null();
    }

    push_scope();
    ++depth;
    static const std::vector<MacroParam> kNoParams;
    const std::vector<MacroParam> &params = macro.params == nullptr ? kNoParams : *macro.params;
    for (const MacroParam &param : params) {
        Value value = param.default_value == nullptr ? Value::null() : param.default_value->eval(*this);
        if (!error.empty()) {
            --depth;
            pop_scope();
            return Value::null();
        }
        set_local(param.name, std::move(value));
    }
    std::string rendered;
    if (macro.body == nullptr || !render_nodes(*macro.body, *this, rendered)) {
        --depth;
        pop_scope();
        return Value::null();
    }
    --depth;
    pop_scope();
    return Value::string(rendered, true);
}

Value CallExpression::eval(RenderContext &context) const {
    const std::string macro_name = callee->variable_name();
    if (macro_name == "range") {
        return eval_range_call(*this, context);
    }
    if (macro_name == "super") {
        if (context.depth > 32) {
            context.error = "block super recursion limit exceeded";
            return Value::null();
        }
        ++context.depth;
        std::string rendered;
        const bool ok = context.render_super(rendered);
        --context.depth;
        return ok ? Value::string(rendered, true) : Value::null();
    }
    const auto macro_iterator = context.macros.find(macro_name);
    if (macro_name.empty() || macro_iterator == context.macros.end()) {
        context.error = macro_name.empty() ? "macro call requires a macro name" : "undefined macro: " + macro_name;
        return Value::null();
    }
    if (context.depth > 32) {
        context.error = "macro recursion limit exceeded";
        return Value::null();
    }

    const MacroDefinition &macro = macro_iterator->second;
    context.push_scope();
    ++context.depth;
    static const std::vector<MacroParam> kNoParams;
    const std::vector<MacroParam> &params = macro.params == nullptr ? kNoParams : *macro.params;
    for (size_t index = 0; index < params.size(); ++index) {
        Value value = index < args.size() ? args[index]->eval(context) : Value::null();
        if (index >= args.size()) {
            const auto named = named_args.find(params[index].name);
            if (named != named_args.end() && named->second != nullptr) {
                value = named->second->eval(context);
            } else if (params[index].default_value != nullptr) {
                value = params[index].default_value->eval(context);
            }
        }
        context.set_local(params[index].name, std::move(value));
    }
    std::string rendered;
    if (macro.body == nullptr || !render_nodes(*macro.body, context, rendered)) {
        --context.depth;
        context.pop_scope();
        return Value::null();
    }
    --context.depth;
    context.pop_scope();
    return Value::string(rendered, true);
}

struct TextNode final : Node {
    explicit TextNode(std::string text_in) : text(std::move(text_in)) {}
    bool render(RenderContext &, std::string &out) const override {
        out += text;
        return true;
    }
    bool allowed_in_extending_template_root() const override {
        return jinja2_tcl::string_utils::trim_ascii_copy(text).empty();
    }
    std::string text;
};

struct OutputNode final : Node {
    explicit OutputNode(ExprPtr expression_in) : expression(std::move(expression_in)) {}
    bool render(RenderContext &context, std::string &out) const override {
        const Value value = expression->eval(context);
        out += to_html_output_value(value);
        return context.error.empty();
    }
    ExprPtr expression;
};

struct SetNode final : Node {
    SetNode(std::string name_in, ExprPtr expression_in)
        : name(std::move(name_in)), expression(std::move(expression_in)) {}
    bool render(RenderContext &context, std::string &) const override {
        context.set(name, expression->eval(context));
        return context.error.empty();
    }
    std::string name;
    ExprPtr expression;
};

struct BlockSetNode final : Node {
    bool render(RenderContext &context, std::string &) const override {
        std::string rendered;
        if (!render_nodes(body, context, rendered)) {
            return false;
        }
        Value value = Value::string(std::move(rendered), true);
        for (const FilterCall &filter : filters) {
            value = apply_filter(value, filter, context);
        }
        context.set(name, std::move(value));
        return context.error.empty();
    }
    void collect_includes(std::vector<std::string> &paths) const override {
        collect_node_includes(body, paths);
    }
    std::string name;
    std::vector<FilterCall> filters;
    std::vector<NodePtr> body;
};

struct IncludeNode final : Node {
    explicit IncludeNode(std::string path_in) : path(std::move(path_in)) {}
    bool render(RenderContext &context, std::string &out) const override {
        return context.render_template_file(path, out);
    }
    void collect_includes(std::vector<std::string> &paths) const override {
        paths.push_back(path);
    }
    std::string path;
};

struct BlockNode final : Node {
    bool render(RenderContext &context, std::string &out) const override {
        const auto chain = context.block_chains.find(name);
        if (chain != context.block_chains.end() && !chain->second.empty()) {
            return context.render_block_chain(chain->second, 0, out);
        }
        return render_body(context, out);
    }
    bool render_body(RenderContext &context, std::string &out) const {
        return render_nodes(body, context, out);
    }
    void collect_includes(std::vector<std::string> &paths) const override {
        collect_node_includes(body, paths);
    }
    bool allowed_in_extending_template_root() const override {
        return true;
    }
    std::string name;
    std::vector<NodePtr> body;
};

bool RenderContext::render_super(std::string &out) {
    if (!has_super()) {
        error = "super() is only available inside an overridden block";
        return false;
    }
    const BlockRenderFrame frame = block_stack.back();
    return render_block_chain(*frame.chain, frame.index + 1, out);
}

bool RenderContext::render_block_chain(const std::vector<const BlockNode *> &chain,
                                       size_t index,
                                       std::string &out) {
    if (index >= chain.size() || chain[index] == nullptr) {
        return true;
    }
    BlockRenderFrame frame;
    frame.chain = &chain;
    frame.index = index;
    block_stack.push_back(frame);
    const bool ok = chain[index]->render_body(*this, out);
    block_stack.pop_back();
    return ok;
}

struct IfBranch {
    ExprPtr condition;
    std::vector<NodePtr> body;
};

struct IfNode final : Node {
    bool render(RenderContext &context, std::string &out) const override {
        for (const IfBranch &branch : branches) {
            if (branch.condition == nullptr || truthy(branch.condition->eval(context))) {
                return render_nodes(branch.body, context, out);
            }
        }
        return true;
    }
    void collect_includes(std::vector<std::string> &paths) const override {
        for (const IfBranch &branch : branches) {
            collect_node_includes(branch.body, paths);
        }
    }
    std::vector<IfBranch> branches;
};

struct ForNode final : Node {
    void set_loop_variables(RenderContext &context, const Value &value) const {
        if (variables.size() == 1) {
            context.set_local(variables.front(), value);
            return;
        }
        if (value.type == Value::Type::Array) {
            for (size_t index = 0; index < variables.size(); ++index) {
                context.set_local(variables[index],
                                  index < value.array_value.size() ? value.array_value[index] : Value::null());
            }
            return;
        }
        if (value.type == Value::Type::Object &&
            value.object_value.find("key") != value.object_value.end() &&
            value.object_value.find("value") != value.object_value.end()) {
            if (!variables.empty()) {
                context.set_local(variables[0], value.object_value.at("key"));
            }
            if (variables.size() > 1) {
                context.set_local(variables[1], value.object_value.at("value"));
            }
            for (size_t index = 2; index < variables.size(); ++index) {
                context.set_local(variables[index], Value::null());
            }
            return;
        }
        for (const std::string &variable : variables) {
            context.set_local(variable, Value::null());
        }
    }

    bool render(RenderContext &context, std::string &out) const override {
        const Value iterable = collection->eval(context);
        std::vector<Value> values;
        if (iterable.type == Value::Type::Array) {
            values = iterable.array_value;
        } else if (iterable.type == Value::Type::String) {
            values = list_from_value(iterable).array_value;
        } else if (iterable.type == Value::Type::Object) {
            for (const auto &entry : iterable.object_value) {
                std::map<std::string, Value> pair;
                pair.emplace("key", Value::string(entry.first));
                pair.emplace("value", entry.second);
                values.emplace_back(Value::object(std::move(pair)));
            }
        }
        if (condition != nullptr) {
            std::vector<Value> filtered;
            for (const Value &value : values) {
                context.push_scope();
                set_loop_variables(context, value);
                const bool keep = truthy(condition->eval(context));
                context.pop_scope();
                if (keep) {
                    filtered.push_back(value);
                }
            }
            values = std::move(filtered);
        }
        if (values.empty()) {
            return render_nodes(else_body, context, out);
        }
        for (size_t index = 0; index < values.size(); ++index) {
            std::map<std::string, Value> loop;
            loop.emplace("index", Value::number(static_cast<double>(index + 1)));
            loop.emplace("index0", Value::number(static_cast<double>(index)));
            loop.emplace("first", Value::boolean(index == 0));
            loop.emplace("last", Value::boolean(index + 1 == values.size()));
            loop.emplace("length", Value::number(static_cast<double>(values.size())));
            context.push_scope();
            set_loop_variables(context, values[index]);
            context.set_local("loop", Value::object(std::move(loop)));
            const bool ok = render_nodes(body, context, out);
            context.pop_scope();
            if (!ok) {
                return false;
            }
        }
        return true;
    }
    void collect_includes(std::vector<std::string> &paths) const override {
        collect_node_includes(body, paths);
        collect_node_includes(else_body, paths);
    }
    std::vector<std::string> variables;
    ExprPtr collection;
    ExprPtr condition;
    std::vector<NodePtr> body;
    std::vector<NodePtr> else_body;
};

struct MacroNode final : Node {
    bool render(RenderContext &context, std::string &) const override {
        MacroDefinition definition;
        definition.params = &params;
        definition.body = &body;
        context.macros[name] = std::move(definition);
        return true;
    }
    void collect_includes(std::vector<std::string> &paths) const override {
        collect_node_includes(body, paths);
    }

    std::string name;
    std::vector<MacroParam> params;
    std::vector<NodePtr> body;
};

struct CallBlockNode final : Node {
    bool render(RenderContext &context, std::string &out) const override {
        if (call_expression == nullptr) {
            context.error = "call block missing macro call";
            return false;
        }

        MacroDefinition definition;
        definition.params = &params;
        definition.body = &body;

        const auto existing = context.macros.find("caller");
        const bool had_existing = existing != context.macros.end();
        MacroDefinition previous;
        if (had_existing) {
            previous = existing->second;
        }

        context.macros["caller"] = std::move(definition);
        const Value rendered = call_expression->eval(context);
        if (had_existing) {
            context.macros["caller"] = previous;
        } else {
            context.macros.erase("caller");
        }
        if (!context.error.empty()) {
            return false;
        }
        out += to_string_value(rendered);
        return true;
    }
    void collect_includes(std::vector<std::string> &paths) const override {
        collect_node_includes(body, paths);
    }

    std::vector<MacroParam> params;
    ExprPtr call_expression;
    std::vector<NodePtr> body;
};

enum class TokenType {
    End,
    Identifier,
    Number,
    String,
    Operator,
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Dot,
    Comma,
    Pipe,
};

struct Token {
    TokenType type = TokenType::End;
    std::string text;
    double number = 0.0;
};

class ExpressionLexer {
public:
    explicit ExpressionLexer(std::string input_in) : input(std::move(input_in)) {}

    Token peek() {
        if (!has_peek) {
            peeked = next_token();
            has_peek = true;
        }
        return peeked;
    }

    Token next() {
        if (has_peek) {
            has_peek = false;
            return peeked;
        }
        return next_token();
    }

private:
    Token next_token() {
        while (position < input.size() &&
               std::isspace(static_cast<unsigned char>(input[position])) != 0) {
            ++position;
        }
        if (position >= input.size()) {
            return {};
        }

        const char current = input[position];
        if (std::isalpha(static_cast<unsigned char>(current)) != 0 || current == '_') {
            const size_t start = position++;
            while (position < input.size() &&
                   (std::isalnum(static_cast<unsigned char>(input[position])) != 0 || input[position] == '_')) {
                ++position;
            }
            return {TokenType::Identifier, input.substr(start, position - start), 0.0};
        }
        if (std::isdigit(static_cast<unsigned char>(current)) != 0) {
            const size_t start = position++;
            while (position < input.size() &&
                   (std::isdigit(static_cast<unsigned char>(input[position])) != 0 || input[position] == '.')) {
                ++position;
            }
            const std::string number_text = input.substr(start, position - start);
            return {TokenType::Number, number_text, std::strtod(number_text.c_str(), nullptr)};
        }
        if (current == '\'' || current == '"') {
            const char quote = current;
            ++position;
            std::string value;
            while (position < input.size()) {
                const char character = input[position++];
                if (character == quote) {
                    return {TokenType::String, value, 0.0};
                }
                if (character == '\\' && position < input.size()) {
                    const char escaped = input[position++];
                    switch (escaped) {
                        case 'n':
                            value.push_back('\n');
                            break;
                        case 't':
                            value.push_back('\t');
                            break;
                        default:
                            value.push_back(escaped);
                            break;
                    }
                } else {
                    value.push_back(character);
                }
            }
            return {TokenType::String, value, 0.0};
        }

        ++position;
        switch (current) {
            case '(':
                return {TokenType::LParen, "(", 0.0};
            case ')':
                return {TokenType::RParen, ")", 0.0};
            case '[':
                return {TokenType::LBracket, "[", 0.0};
            case ']':
                return {TokenType::RBracket, "]", 0.0};
            case '{':
                return {TokenType::LBrace, "{", 0.0};
            case '}':
                return {TokenType::RBrace, "}", 0.0};
            case '.':
                return {TokenType::Dot, ".", 0.0};
            case ',':
                return {TokenType::Comma, ",", 0.0};
            case '|':
                return {TokenType::Pipe, "|", 0.0};
            default:
                break;
        }
        std::string op(1, current);
        if (position < input.size()) {
            const std::string two = op + input[position];
            if (two == "==" || two == "!=" || two == "<=" || two == ">=" || two == "//" || two == "**") {
                ++position;
                op = two;
            }
        }
        return {TokenType::Operator, op, 0.0};
    }

    std::string input;
    size_t position = 0;
    bool has_peek = false;
    Token peeked;
};

class ExpressionParser {
public:
    explicit ExpressionParser(std::string input_in) : lexer(std::move(input_in)) {}

    bool parse(ExprPtr &out, std::string &error_out) {
        out = parse_conditional(error_out);
        if (out == nullptr) {
            return false;
        }
        const Token token = lexer.next();
        if (token.type != TokenType::End) {
            error_out = "unexpected token in expression: " + token.text;
            return false;
        }
        return true;
    }

    bool parse_filter_chain_only(std::vector<FilterCall> &filters_out, std::string &error_out) {
        if (lexer.peek().type != TokenType::Pipe) {
            error_out = "expected filter chain";
            return false;
        }
        if (!parse_filter_chain(filters_out, error_out)) {
            return false;
        }
        const Token token = lexer.next();
        if (token.type != TokenType::End) {
            error_out = "unexpected token in filter chain: " + token.text;
            return false;
        }
        return true;
    }

private:
    bool eat_keyword(const std::string &keyword) {
        const Token token = lexer.peek();
        if (token.type == TokenType::Identifier && token.text == keyword) {
            lexer.next();
            return true;
        }
        return false;
    }

    bool eat_operator(const std::string &op) {
        const Token token = lexer.peek();
        if (token.type == TokenType::Operator && token.text == op) {
            lexer.next();
            return true;
        }
        return false;
    }

    ExprPtr parse_conditional(std::string &error_out) {
        ExprPtr expression = parse_or(error_out);
        if (expression != nullptr && eat_operator("?")) {
            ExprPtr true_expression = parse_conditional(error_out);
            if (true_expression == nullptr) {
                return nullptr;
            }
            if (!eat_operator(":")) {
                error_out = "expected `:` in ternary expression";
                return nullptr;
            }
            ExprPtr false_expression = parse_conditional(error_out);
            if (false_expression == nullptr) {
                return nullptr;
            }
            return std::make_unique<ConditionalExpression>(
                    std::move(true_expression),
                    std::move(expression),
                    std::move(false_expression));
        }
        if (expression != nullptr && eat_keyword("if")) {
            ExprPtr condition = parse_or(error_out);
            if (condition == nullptr) {
                return nullptr;
            }
            ExprPtr false_expression;
            if (eat_keyword("else")) {
                false_expression = parse_conditional(error_out);
                if (false_expression == nullptr) {
                    return nullptr;
                }
            }
            return std::make_unique<ConditionalExpression>(
                    std::move(expression),
                    std::move(condition),
                    std::move(false_expression));
        }
        return expression;
    }

    ExprPtr parse_or(std::string &error_out) {
        ExprPtr left = parse_and(error_out);
        if (left != nullptr && eat_keyword("or")) {
            ExprPtr right = parse_or(error_out);
            if (right == nullptr) {
                return nullptr;
            }
            return std::make_unique<BinaryExpression>("or", std::move(left), std::move(right));
        }
        return left;
    }

    ExprPtr parse_and(std::string &error_out) {
        ExprPtr left = parse_compare(error_out);
        if (left != nullptr && eat_keyword("and")) {
            ExprPtr right = parse_and(error_out);
            if (right == nullptr) {
                return nullptr;
            }
            return std::make_unique<BinaryExpression>("and", std::move(left), std::move(right));
        }
        return left;
    }

    ExprPtr parse_compare(std::string &error_out) {
        ExprPtr left = parse_concat(error_out);
        if (left == nullptr) {
            return nullptr;
        }
        const Token token = lexer.peek();
        std::string op;
        if (token.type == TokenType::Operator &&
            (token.text == "==" || token.text == "!=" || token.text == "<" ||
             token.text == ">" || token.text == "<=" || token.text == ">=")) {
            op = lexer.next().text;
        } else if (token.type == TokenType::Identifier && token.text == "in") {
            op = lexer.next().text;
        } else if (token.type == TokenType::Identifier && token.text == "not") {
            lexer.next();
            if (!eat_keyword("in")) {
                error_out = "expected `in` after `not` in comparison";
                return nullptr;
            }
            op = "not in";
        } else if (token.type == TokenType::Identifier && token.text == "is") {
            lexer.next();
            bool negate = false;
            if (eat_keyword("not")) {
                negate = true;
            }
            const Token test = lexer.next();
            if (test.type != TokenType::Identifier) {
                error_out = "expected test name after `is`";
                return nullptr;
            }
            if (test.text == "defined") {
                return std::make_unique<DefinedTestExpression>(std::move(left), negate);
            }
            if (test.text == "undefined") {
                return std::make_unique<DefinedTestExpression>(std::move(left), !negate);
            }
            if (test.text == "none") {
                return std::make_unique<NoneTestExpression>(std::move(left), negate);
            }
            static const std::vector<std::string> supported_tests = {
                    "even", "odd", "string", "number", "iterable", "mapping", "sequence", "lower", "upper",
                    "eq", "equalto", "ne", "gt", "greaterthan", "ge", "lt", "lessthan", "le"};
            if (std::find(supported_tests.begin(), supported_tests.end(), test.text) == supported_tests.end()) {
                error_out = "unsupported test: " + test.text;
                return nullptr;
            }
            std::vector<ExprPtr> args;
            std::map<std::string, ExprPtr> named_args;
            if (lexer.peek().type == TokenType::LParen) {
                lexer.next();
                if (!parse_argument_list(TokenType::RParen, args, named_args, error_out)) {
                    return nullptr;
                }
            }
            return std::make_unique<NamedTestExpression>(
                    std::move(left),
                    test.text,
                    negate,
                    std::move(args),
                    std::move(named_args));
        } else {
            return left;
        }
        ExprPtr right = parse_concat(error_out);
        if (right == nullptr) {
            return nullptr;
        }
        return std::make_unique<BinaryExpression>(op, std::move(left), std::move(right));
    }

    ExprPtr parse_concat(std::string &error_out) {
        ExprPtr left = parse_add(error_out);
        while (left != nullptr && eat_operator("~")) {
            ExprPtr right = parse_add(error_out);
            if (right == nullptr) {
                return nullptr;
            }
            left = std::make_unique<BinaryExpression>("~", std::move(left), std::move(right));
        }
        return left;
    }

    ExprPtr parse_add(std::string &error_out) {
        ExprPtr left = parse_mul(error_out);
        while (left != nullptr) {
            const Token token = lexer.peek();
            if (token.type != TokenType::Operator || (token.text != "+" && token.text != "-")) {
                return left;
            }
            const std::string op = lexer.next().text;
            ExprPtr right = parse_mul(error_out);
            if (right == nullptr) {
                return nullptr;
            }
            left = std::make_unique<BinaryExpression>(op, std::move(left), std::move(right));
        }
        return left;
    }

    ExprPtr parse_mul(std::string &error_out) {
        ExprPtr left = parse_power(error_out);
        while (left != nullptr) {
            const Token token = lexer.peek();
            if (token.type != TokenType::Operator ||
                (token.text != "*" && token.text != "/" && token.text != "//" && token.text != "%")) {
                return left;
            }
            const std::string op = lexer.next().text;
            ExprPtr right = parse_power(error_out);
            if (right == nullptr) {
                return nullptr;
            }
            left = std::make_unique<BinaryExpression>(op, std::move(left), std::move(right));
        }
        return left;
    }

    ExprPtr parse_power(std::string &error_out) {
        ExprPtr left = parse_unary(error_out);
        if (left != nullptr && eat_operator("**")) {
            ExprPtr right = parse_power(error_out);
            if (right == nullptr) {
                return nullptr;
            }
            return std::make_unique<BinaryExpression>("**", std::move(left), std::move(right));
        }
        return left;
    }

    ExprPtr parse_unary(std::string &error_out) {
        if (eat_keyword("not")) {
            ExprPtr inner = parse_unary(error_out);
            return inner == nullptr ? nullptr : std::make_unique<UnaryExpression>("not", std::move(inner));
        }
        if (eat_operator("+")) {
            ExprPtr inner = parse_unary(error_out);
            return inner == nullptr ? nullptr : std::make_unique<UnaryExpression>("+", std::move(inner));
        }
        if (eat_operator("-")) {
            ExprPtr inner = parse_unary(error_out);
            return inner == nullptr ? nullptr : std::make_unique<UnaryExpression>("-", std::move(inner));
        }
        return parse_postfix(error_out);
    }

    bool parse_argument_list(TokenType closing,
                             std::vector<ExprPtr> &args,
                             std::map<std::string, ExprPtr> &named_args,
                             std::string &error_out) {
        if (lexer.peek().type != closing) {
            do {
                ExprPtr arg = parse_conditional(error_out);
                if (arg == nullptr) {
                    return false;
                }
                const std::string arg_name = arg->variable_name();
                if (!arg_name.empty() && eat_operator("=")) {
                    ExprPtr value = parse_conditional(error_out);
                    if (value == nullptr) {
                        return false;
                    }
                    named_args[arg_name] = std::move(value);
                } else {
                    args.push_back(std::move(arg));
                }
                if (lexer.peek().type != TokenType::Comma) {
                    break;
                }
                lexer.next();
                if (lexer.peek().type == closing) {
                    break;
                }
            } while (true);
        }
        if (lexer.next().type != closing) {
            error_out = closing == TokenType::RParen ? "expected closing parenthesis" : "expected closing bracket";
            return false;
        }
        return true;
    }

    bool parse_filter_chain(std::vector<FilterCall> &filters, std::string &error_out) {
        do {
            lexer.next();
            const Token name = lexer.next();
            if (name.type != TokenType::Identifier) {
                error_out = "expected filter name";
                return false;
            }
            FilterCall filter;
            filter.name = name.text;
            if (lexer.peek().type == TokenType::LParen) {
                lexer.next();
                if (!parse_argument_list(TokenType::RParen, filter.args, filter.named_args, error_out)) {
                    error_out = "expected closing parenthesis in filter";
                    return false;
                }
            }
            filters.push_back(std::move(filter));
        } while (lexer.peek().type == TokenType::Pipe);
        return true;
    }

    ExprPtr parse_postfix(std::string &error_out) {
        ExprPtr expression = parse_value(error_out);
        while (expression != nullptr) {
            const Token token = lexer.peek();
            if (token.type == TokenType::Dot) {
                lexer.next();
                const Token name = lexer.next();
                if (name.type != TokenType::Identifier) {
                    error_out = "expected identifier after dot";
                    return nullptr;
                }
                if (lexer.peek().type == TokenType::LParen) {
                    if (!is_supported_method_name(name.text)) {
                        error_out = "unsupported method: " + name.text;
                        return nullptr;
                    }
                    lexer.next();
                    std::vector<ExprPtr> args;
                    std::map<std::string, ExprPtr> named_args;
                    if (!parse_argument_list(TokenType::RParen, args, named_args, error_out)) {
                        return nullptr;
                    }
                    expression = std::make_unique<MethodCallExpression>(
                            std::move(expression),
                            name.text,
                            std::move(args),
                            std::move(named_args));
                } else {
                    expression = std::make_unique<SubscriptExpression>(
                            std::move(expression),
                            std::make_unique<ConstantExpression>(Value::string(name.text)));
                }
            } else if (token.type == TokenType::LBracket) {
                lexer.next();
                if (eat_operator(":")) {
                    ExprPtr end;
                    if (lexer.peek().type != TokenType::RBracket) {
                        end = parse_conditional(error_out);
                        if (end == nullptr) {
                            return nullptr;
                        }
                    }
                    if (lexer.next().type != TokenType::RBracket) {
                        error_out = "expected closing bracket";
                        return nullptr;
                    }
                    expression = std::make_unique<SliceExpression>(std::move(expression), nullptr, std::move(end));
                } else {
                    ExprPtr key = parse_conditional(error_out);
                    if (key == nullptr) {
                        return nullptr;
                    }
                    if (eat_operator(":")) {
                        ExprPtr end;
                        if (lexer.peek().type != TokenType::RBracket) {
                            end = parse_conditional(error_out);
                            if (end == nullptr) {
                                return nullptr;
                            }
                        }
                        if (lexer.next().type != TokenType::RBracket) {
                            error_out = "expected closing bracket";
                            return nullptr;
                        }
                        expression = std::make_unique<SliceExpression>(
                                std::move(expression),
                                std::move(key),
                                std::move(end));
                    } else {
                        if (lexer.next().type != TokenType::RBracket) {
                            error_out = "expected closing bracket";
                            return nullptr;
                        }
                        expression = std::make_unique<SubscriptExpression>(std::move(expression), std::move(key));
                    }
                }
            } else if (token.type == TokenType::LParen) {
                lexer.next();
                std::vector<ExprPtr> args;
                std::map<std::string, ExprPtr> named_args;
                if (!parse_argument_list(TokenType::RParen, args, named_args, error_out)) {
                    return nullptr;
                }
                expression = std::make_unique<CallExpression>(
                        std::move(expression),
                        std::move(args),
                        std::move(named_args));
            } else if (token.type == TokenType::Pipe) {
                std::vector<FilterCall> filters;
                if (!parse_filter_chain(filters, error_out)) {
                    return nullptr;
                }
                expression = std::make_unique<FilteredExpression>(std::move(expression), std::move(filters));
            } else {
                break;
            }
        }
        return expression;
    }

    ExprPtr parse_value(std::string &error_out) {
        const Token token = lexer.next();
        if (token.type == TokenType::Identifier) {
            if (token.text == "true" || token.text == "True") {
                return std::make_unique<ConstantExpression>(Value::boolean(true));
            }
            if (token.text == "false" || token.text == "False") {
                return std::make_unique<ConstantExpression>(Value::boolean(false));
            }
            if (token.text == "none" || token.text == "None" || token.text == "null") {
                return std::make_unique<ConstantExpression>(Value::null());
            }
            return std::make_unique<VariableExpression>(token.text);
        }
        if (token.type == TokenType::Number) {
            return std::make_unique<ConstantExpression>(Value::number(token.number));
        }
        if (token.type == TokenType::String) {
            return std::make_unique<ConstantExpression>(Value::string(token.text));
        }
        if (token.type == TokenType::LParen) {
            if (lexer.peek().type == TokenType::RParen) {
                lexer.next();
                return std::make_unique<ArrayExpression>(std::vector<ExprPtr>());
            }
            ExprPtr first = parse_conditional(error_out);
            if (first == nullptr) {
                return nullptr;
            }
            if (lexer.peek().type == TokenType::Comma) {
                std::vector<ExprPtr> items;
                items.push_back(std::move(first));
                while (lexer.peek().type == TokenType::Comma) {
                    lexer.next();
                    if (lexer.peek().type == TokenType::RParen) {
                        break;
                    }
                    ExprPtr item = parse_conditional(error_out);
                    if (item == nullptr) {
                        return nullptr;
                    }
                    items.push_back(std::move(item));
                }
                if (lexer.next().type != TokenType::RParen) {
                    error_out = "expected closing parenthesis";
                    return nullptr;
                }
                return std::make_unique<ArrayExpression>(std::move(items));
            }
            if (lexer.next().type != TokenType::RParen) {
                error_out = "expected closing parenthesis";
                return nullptr;
            }
            return first;
        }
        if (token.type == TokenType::LBracket) {
            return parse_array_literal(error_out);
        }
        if (token.type == TokenType::LBrace) {
            return parse_object_literal(error_out);
        }
        error_out = "expected expression";
        return nullptr;
    }

    ExprPtr parse_array_literal(std::string &error_out) {
        std::vector<ExprPtr> items;
        if (lexer.peek().type != TokenType::RBracket) {
            do {
                ExprPtr item = parse_conditional(error_out);
                if (item == nullptr) {
                    return nullptr;
                }
                items.push_back(std::move(item));
                if (lexer.peek().type != TokenType::Comma) {
                    break;
                }
                lexer.next();
                if (lexer.peek().type == TokenType::RBracket) {
                    break;
                }
            } while (true);
        }
        if (lexer.next().type != TokenType::RBracket) {
            error_out = "expected closing bracket";
            return nullptr;
        }
        return std::make_unique<ArrayExpression>(std::move(items));
    }

    ExprPtr parse_object_literal(std::string &error_out) {
        std::vector<std::pair<std::string, ExprPtr>> items;
        if (lexer.peek().type != TokenType::RBrace) {
            do {
                const Token key = lexer.next();
                if (key.type != TokenType::String && key.type != TokenType::Identifier) {
                    error_out = "expected object key";
                    return nullptr;
                }
                if (!eat_operator(":") && !eat_operator("=")) {
                    error_out = "expected object key separator";
                    return nullptr;
                }
                ExprPtr value = parse_conditional(error_out);
                if (value == nullptr) {
                    return nullptr;
                }
                items.emplace_back(key.text, std::move(value));
                if (lexer.peek().type != TokenType::Comma) {
                    break;
                }
                lexer.next();
                if (lexer.peek().type == TokenType::RBrace) {
                    break;
                }
            } while (true);
        }
        if (lexer.next().type != TokenType::RBrace) {
            error_out = "expected closing brace";
            return nullptr;
        }
        return std::make_unique<ObjectExpression>(std::move(items));
    }

    ExpressionLexer lexer;
};

bool parse_expression(const std::string &text, ExprPtr &expression_out, std::string &error_out) {
    ExpressionParser parser(text);
    if (!parser.parse(expression_out, error_out)) {
        error_out = "invalid expression `" + jinja2_tcl::string_utils::trim_ascii_copy(text) + "`: " + error_out;
        return false;
    }
    return true;
}

bool parse_filter_chain_text(const std::string &text,
                             std::vector<FilterCall> &filters_out,
                             std::string &error_out) {
    ExpressionParser parser(text);
    if (!parser.parse_filter_chain_only(filters_out, error_out)) {
        error_out = "invalid filter chain `" + jinja2_tcl::string_utils::trim_ascii_copy(text) + "`: " + error_out;
        return false;
    }
    return true;
}

std::string first_word(const std::string &value) {
    size_t position = 0;
    while (position < value.size() && std::isspace(static_cast<unsigned char>(value[position])) != 0) {
        ++position;
    }
    const size_t start = position;
    while (position < value.size() &&
           (std::isalnum(static_cast<unsigned char>(value[position])) != 0 || value[position] == '_')) {
        ++position;
    }
    return value.substr(start, position - start);
}

std::string after_first_word(const std::string &value) {
    size_t position = 0;
    while (position < value.size() && std::isspace(static_cast<unsigned char>(value[position])) != 0) {
        ++position;
    }
    while (position < value.size() &&
           (std::isalnum(static_cast<unsigned char>(value[position])) != 0 || value[position] == '_')) {
        ++position;
    }
    return jinja2_tcl::string_utils::trim_ascii_copy(value.substr(position));
}

size_t find_top_level_keyword(const std::string &value, const std::string &keyword) {
    int depth = 0;
    char quote = '\0';
    for (size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (quote != '\0') {
            if (character == quote) {
                quote = '\0';
            } else if (character == '\\' && index + 1 < value.size()) {
                ++index;
            }
            continue;
        }
        if (character == '\'' || character == '"') {
            quote = character;
            continue;
        }
        if (character == '(' || character == '[' || character == '{') {
            ++depth;
            continue;
        }
        if (character == ')' || character == ']' || character == '}') {
            --depth;
            continue;
        }
        if (depth == 0 && index + keyword.size() <= value.size() &&
            value.compare(index, keyword.size(), keyword) == 0) {
            const bool before = index == 0 ||
                                !(std::isalnum(static_cast<unsigned char>(value[index - 1])) != 0 ||
                                  value[index - 1] == '_');
            const size_t after = index + keyword.size();
            const bool after_ok = after >= value.size() ||
                                  !(std::isalnum(static_cast<unsigned char>(value[after])) != 0 ||
                                    value[after] == '_');
            if (before && after_ok) {
                return index;
            }
        }
    }
    return std::string::npos;
}

bool find_matching_parenthesis(const std::string &value, size_t open_position, size_t &close_position) {
    if (open_position >= value.size() || value[open_position] != '(') {
        return false;
    }
    int depth = 0;
    char quote = '\0';
    for (size_t index = open_position; index < value.size(); ++index) {
        const char character = value[index];
        if (quote != '\0') {
            if (character == quote) {
                quote = '\0';
            } else if (character == '\\' && index + 1 < value.size()) {
                ++index;
            }
            continue;
        }
        if (character == '\'' || character == '"') {
            quote = character;
            continue;
        }
        if (character == '(') {
            ++depth;
            continue;
        }
        if (character == ')') {
            --depth;
            if (depth == 0) {
                close_position = index;
                return true;
            }
        }
    }
    return false;
}

bool only_ascii_space_after(const std::string &value, size_t position) {
    while (position < value.size()) {
        if (std::isspace(static_cast<unsigned char>(value[position])) == 0) {
            return false;
        }
        ++position;
    }
    return true;
}

bool stop_contains(const std::vector<std::string> &stops, const std::string &word) {
    return std::find(stops.begin(), stops.end(), word) != stops.end();
}

struct Template {
    std::string path;
    std::string extends_path;
    std::vector<NodePtr> nodes;
    std::map<std::string, const BlockNode *> blocks;
    std::vector<std::string> includes;
};

bool normalize_template_path(const std::string &path, std::string &normalized_out, std::string &error_out) {
    normalized_out.clear();
    const std::string trimmed = jinja2_tcl::string_utils::trim_ascii_copy(path);
    if (trimmed.empty()) {
        error_out = "template path must not be empty";
        return false;
    }
    if (trimmed.front() == '/' || trimmed.find('\\') != std::string::npos) {
        error_out = "template path must be a relative POSIX path";
        return false;
    }
    if (trimmed.find('\0') != std::string::npos) {
        error_out = "template path must not contain NUL bytes";
        return false;
    }
    size_t segment_start = 0;
    while (segment_start <= trimmed.size()) {
        const size_t slash = trimmed.find('/', segment_start);
        const std::string segment =
                trimmed.substr(segment_start,
                               slash == std::string::npos ? std::string::npos : slash - segment_start);
        if (segment.empty() || segment == "." || segment == "..") {
            error_out = "template path must not contain empty, dot, or parent segments";
            return false;
        }
        for (const unsigned char byte : segment) {
            if (std::iscntrl(byte) != 0) {
                error_out = "template path must not contain control characters";
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        segment_start = slash + 1;
    }
    normalized_out = trimmed;
    return true;
}

bool parse_template_path_literal(const std::string &tail,
                                 const std::string &statement,
                                 std::string &path_out,
                                 std::string &error_out) {
    const std::string trimmed = jinja2_tcl::string_utils::trim_ascii_copy(tail);
    if (trimmed.empty() || (trimmed.front() != '\'' && trimmed.front() != '"')) {
        error_out = statement + " requires a quoted constant template path";
        return false;
    }
    const char quote = trimmed.front();
    std::string value;
    size_t position = 1;
    while (position < trimmed.size()) {
        const char character = trimmed[position++];
        if (character == quote) {
            if (!only_ascii_space_after(trimmed, position)) {
                error_out = statement + " path must be followed only by whitespace";
                return false;
            }
            if (!normalize_template_path(value, path_out, error_out)) {
                error_out = statement + " " + error_out;
                return false;
            }
            return true;
        }
        if (character == '\\' && position < trimmed.size()) {
            const char escaped = trimmed[position++];
            switch (escaped) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    value.push_back(escaped);
                    break;
            }
        } else {
            value.push_back(character);
        }
    }
    error_out = statement + " path is missing its closing quote";
    return false;
}

struct ParseResult {
    std::vector<NodePtr> nodes;
    std::string closing_keyword;
    std::string closing_content;
};

class TemplateParser {
public:
    explicit TemplateParser(std::string input_in) : input(std::move(input_in)) {}

    bool parse(Template &template_out, std::string &error_out) {
        size_t position = 0;
        ParseResult result;
        if (!parse_nodes(position, {}, true, result, error_out)) {
            return false;
        }
        if (!result.closing_keyword.empty()) {
            error_out = "unexpected closing statement: " + result.closing_keyword;
            return false;
        }
        template_out.extends_path = std::move(extends_path);
        if (!template_out.extends_path.empty()) {
            for (const NodePtr &node : result.nodes) {
                if (!node->allowed_in_extending_template_root()) {
                    error_out =
                            "extending templates may contain only whitespace and block statements at template root "
                            "level; an extending template renders the parent/root template and substitutes child "
                            "blocks, but it does not execute top-level child nodes outside blocks";
                    return false;
                }
            }
        }
        template_out.nodes = std::move(result.nodes);
        template_out.blocks = std::move(blocks);
        collect_node_includes(template_out.nodes, template_out.includes);
        return true;
    }

private:
    std::string clean_tag(std::string text) const {
        text = jinja2_tcl::string_utils::trim_ascii_copy(text);
        if (!text.empty() && text.front() == '-') {
            text.erase(text.begin());
        }
        text = jinja2_tcl::string_utils::trim_ascii_copy(text);
        if (!text.empty() && text.back() == '-') {
            text.pop_back();
        }
        return jinja2_tcl::string_utils::trim_ascii_copy(text);
    }

    size_t find_next_tag(size_t position, std::string &delimiter_out) const {
        const size_t output = input.find("{{", position);
        const size_t statement = input.find("{%", position);
        const size_t comment = input.find("{#", position);
        size_t best = std::string::npos;
        delimiter_out.clear();
        if (output != std::string::npos && (best == std::string::npos || output < best)) {
            best = output;
            delimiter_out = "{{";
        }
        if (statement != std::string::npos && (best == std::string::npos || statement < best)) {
            best = statement;
            delimiter_out = "{%";
        }
        if (comment != std::string::npos && (best == std::string::npos || comment < best)) {
            best = comment;
            delimiter_out = "{#";
        }
        return best;
    }

    bool parse_nodes(size_t &position,
                     const std::vector<std::string> &stop_keywords,
                     bool root_level,
                     ParseResult &result,
                     std::string &error_out) {
        while (position < input.size()) {
            std::string delimiter;
            const size_t tag_start = find_next_tag(position, delimiter);
            if (tag_start == std::string::npos) {
                result.nodes.push_back(std::make_unique<TextNode>(input.substr(position)));
                position = input.size();
                return true;
            }
            if (tag_start > position) {
                result.nodes.push_back(std::make_unique<TextNode>(input.substr(position, tag_start - position)));
            }

            const std::string end_delimiter = delimiter == "{{" ? "}}" : (delimiter == "{%" ? "%}" : "#}");
            const size_t content_start = tag_start + 2;
            const size_t tag_end = input.find(end_delimiter, content_start);
            if (tag_end == std::string::npos) {
                error_out = "unterminated template tag";
                return false;
            }
            std::string content = clean_tag(input.substr(content_start, tag_end - content_start));
            position = tag_end + 2;

            if (delimiter == "{#") {
                continue;
            }
            if (delimiter == "{{") {
                ExprPtr expression;
                if (!parse_expression(content, expression, error_out)) {
                    return false;
                }
                result.nodes.push_back(std::make_unique<OutputNode>(std::move(expression)));
                continue;
            }

            const std::string keyword = first_word(content);
            if (stop_contains(stop_keywords, keyword)) {
                result.closing_keyword = keyword;
                result.closing_content = content;
                return true;
            }
            if (!parse_statement(content, position, root_level, result.nodes, error_out)) {
                return false;
            }
        }
        return true;
    }

    bool parse_statement(const std::string &content,
                         size_t &position,
                         bool root_level,
                         std::vector<NodePtr> &nodes,
                         std::string &error_out) {
        const std::string keyword = first_word(content);
        const std::string tail = after_first_word(content);
        if (keyword == "set") {
            return parse_set(tail, position, nodes, error_out);
        }
        if (keyword == "if") {
            return parse_if(tail, position, nodes, error_out);
        }
        if (keyword == "for") {
            return parse_for(tail, position, nodes, error_out);
        }
        if (keyword == "macro") {
            return parse_macro(tail, position, nodes, error_out);
        }
        if (keyword == "call") {
            return parse_call(tail, position, nodes, error_out);
        }
        if (keyword == "extends") {
            if (!root_level) {
                error_out = "extends is only supported at template root level";
                return false;
            }
            return parse_extends(tail, error_out);
        }
        if (keyword == "include") {
            return parse_include(tail, nodes, error_out);
        }
        if (keyword == "block") {
            return parse_block(tail, position, nodes, error_out);
        }
        if (keyword == "import") {
            error_out = "unsupported statement: " + keyword;
            return false;
        }
        error_out = "unknown statement: " + keyword;
        return false;
    }

    bool parse_extends(const std::string &tail, std::string &error_out) {
        if (!extends_path.empty()) {
            error_out = "template declares extends more than once";
            return false;
        }
        return parse_template_path_literal(tail, "extends", extends_path, error_out);
    }

    bool parse_include(const std::string &tail,
                       std::vector<NodePtr> &nodes,
                       std::string &error_out) {
        std::string path;
        if (!parse_template_path_literal(tail, "include", path, error_out)) {
            return false;
        }
        nodes.push_back(std::make_unique<IncludeNode>(std::move(path)));
        return true;
    }

    bool parse_block(const std::string &tail,
                     size_t &position,
                     std::vector<NodePtr> &nodes,
                     std::string &error_out) {
        size_t cursor = 0;
        while (cursor < tail.size() && std::isspace(static_cast<unsigned char>(tail[cursor])) != 0) {
            ++cursor;
        }
        const size_t name_start = cursor;
        while (cursor < tail.size() &&
               (std::isalnum(static_cast<unsigned char>(tail[cursor])) != 0 || tail[cursor] == '_')) {
            ++cursor;
        }
        const std::string name = tail.substr(name_start, cursor - name_start);
        if (name.empty()) {
            error_out = "block requires a name";
            return false;
        }
        if (!only_ascii_space_after(tail, cursor)) {
            error_out = "block name must be followed only by whitespace";
            return false;
        }
        if (blocks.find(name) != blocks.end()) {
            error_out = "duplicate block: " + name;
            return false;
        }
        auto block = std::make_unique<BlockNode>();
        block->name = name;

        ParseResult body;
        if (!parse_nodes(position, {"endblock"}, false, body, error_out)) {
            return false;
        }
        if (body.closing_keyword != "endblock") {
            error_out = "block statement missing endblock";
            return false;
        }
        const std::string closing_name = after_first_word(body.closing_content);
        if (!closing_name.empty() && closing_name != name) {
            error_out = "endblock name `" + closing_name + "` does not match block `" + name + "`";
            return false;
        }
        block->body = std::move(body.nodes);
        const BlockNode *block_pointer = block.get();
        nodes.push_back(std::move(block));
        blocks[name] = block_pointer;
        return true;
    }

    bool parse_set(const std::string &tail,
                   size_t &template_position,
                   std::vector<NodePtr> &nodes,
                   std::string &error_out) {
        size_t position = 0;
        while (position < tail.size() && std::isspace(static_cast<unsigned char>(tail[position])) != 0) {
            ++position;
        }
        const size_t name_start = position;
        while (position < tail.size() &&
               (std::isalnum(static_cast<unsigned char>(tail[position])) != 0 || tail[position] == '_')) {
            ++position;
        }
        const std::string name = tail.substr(name_start, position - name_start);
        while (position < tail.size() && std::isspace(static_cast<unsigned char>(tail[position])) != 0) {
            ++position;
        }
        if (name.empty()) {
            error_out = "set requires `name = expression` or `{% set name [| filter] %}...{% endset %}`";
            return false;
        }
        auto parse_block_set = [&](std::vector<FilterCall> filters) -> bool {
            auto block_set = std::make_unique<BlockSetNode>();
            block_set->name = name;
            block_set->filters = std::move(filters);
            ParseResult body;
            if (!parse_nodes(template_position, {"endset"}, false, body, error_out)) {
                return false;
            }
            if (body.closing_keyword != "endset") {
                error_out = "set block missing endset";
                return false;
            }
            block_set->body = std::move(body.nodes);
            nodes.push_back(std::move(block_set));
            return true;
        };
        if (position >= tail.size()) {
            return parse_block_set({});
        }
        if (tail[position] == '|') {
            std::vector<FilterCall> filters;
            if (!parse_filter_chain_text(tail.substr(position), filters, error_out)) {
                return false;
            }
            return parse_block_set(std::move(filters));
        }
        if (tail[position] != '=') {
            error_out = "set requires `name = expression` or `{% set name [| filter] %}...{% endset %}`";
            return false;
        }
        ExprPtr expression;
        if (!parse_expression(tail.substr(position + 1), expression, error_out)) {
            return false;
        }
        nodes.push_back(std::make_unique<SetNode>(name, std::move(expression)));
        return true;
    }

    bool parse_if(const std::string &condition_text,
                  size_t &position,
                  std::vector<NodePtr> &nodes,
                  std::string &error_out) {
        auto if_node = std::make_unique<IfNode>();
        ExprPtr condition;
        if (!parse_expression(condition_text, condition, error_out)) {
            return false;
        }
        IfBranch branch;
        branch.condition = std::move(condition);

        ParseResult body;
        if (!parse_nodes(position, {"elif", "else", "endif"}, false, body, error_out)) {
            return false;
        }
        branch.body = std::move(body.nodes);
        if_node->branches.push_back(std::move(branch));

        while (body.closing_keyword == "elif") {
            ExprPtr elif_condition;
            if (!parse_expression(after_first_word(body.closing_content), elif_condition, error_out)) {
                return false;
            }
            IfBranch elif_branch;
            elif_branch.condition = std::move(elif_condition);
            body = {};
            if (!parse_nodes(position, {"elif", "else", "endif"}, false, body, error_out)) {
                return false;
            }
            elif_branch.body = std::move(body.nodes);
            if_node->branches.push_back(std::move(elif_branch));
        }
        if (body.closing_keyword == "else") {
            IfBranch else_branch;
            body = {};
            if (!parse_nodes(position, {"endif"}, false, body, error_out)) {
                return false;
            }
            else_branch.body = std::move(body.nodes);
            if_node->branches.push_back(std::move(else_branch));
        }
        if (body.closing_keyword != "endif") {
            error_out = "if statement missing endif";
            return false;
        }
        nodes.push_back(std::move(if_node));
        return true;
    }

    bool parse_for(const std::string &tail,
                   size_t &position,
                   std::vector<NodePtr> &nodes,
                   std::string &error_out) {
        size_t cursor = 0;
        auto read_identifier = [&](std::string &identifier) {
            while (cursor < tail.size() && std::isspace(static_cast<unsigned char>(tail[cursor])) != 0) {
                ++cursor;
            }
            const size_t start = cursor;
            while (cursor < tail.size() &&
                   (std::isalnum(static_cast<unsigned char>(tail[cursor])) != 0 || tail[cursor] == '_')) {
                ++cursor;
            }
            identifier = tail.substr(start, cursor - start);
            return !identifier.empty();
        };

        std::vector<std::string> variables;
        std::string variable;
        std::string in_keyword;
        if (!read_identifier(variable)) {
            error_out = "for requires `name in expression`";
            return false;
        }
        variables.push_back(variable);
        while (cursor < tail.size()) {
            while (cursor < tail.size() && std::isspace(static_cast<unsigned char>(tail[cursor])) != 0) {
                ++cursor;
            }
            if (cursor >= tail.size() || tail[cursor] != ',') {
                break;
            }
            ++cursor;
            if (!read_identifier(variable)) {
                error_out = "for requires an identifier after comma";
                return false;
            }
            variables.push_back(variable);
        }
        if (!read_identifier(in_keyword) || in_keyword != "in") {
            error_out = "for requires `name in expression`";
            return false;
        }
        std::string collection_text = tail.substr(cursor);
        std::string condition_text;
        const size_t if_position = find_top_level_keyword(collection_text, "if");
        if (if_position != std::string::npos) {
            condition_text = jinja2_tcl::string_utils::trim_ascii_copy(collection_text.substr(if_position + 2));
            collection_text = jinja2_tcl::string_utils::trim_ascii_copy(collection_text.substr(0, if_position));
        }
        ExprPtr collection;
        if (!parse_expression(collection_text, collection, error_out)) {
            return false;
        }
        ExprPtr condition;
        if (!condition_text.empty() && !parse_expression(condition_text, condition, error_out)) {
            return false;
        }

        auto for_node = std::make_unique<ForNode>();
        for_node->variables = std::move(variables);
        for_node->collection = std::move(collection);
        for_node->condition = std::move(condition);

        ParseResult body;
        if (!parse_nodes(position, {"else", "endfor"}, false, body, error_out)) {
            return false;
        }
        for_node->body = std::move(body.nodes);
        if (body.closing_keyword == "else") {
            body = {};
            if (!parse_nodes(position, {"endfor"}, false, body, error_out)) {
                return false;
            }
            for_node->else_body = std::move(body.nodes);
        }
        if (body.closing_keyword != "endfor") {
            error_out = "for statement missing endfor";
            return false;
        }
        nodes.push_back(std::move(for_node));
        return true;
    }

    bool parse_macro_params(const std::string &params_text,
                            std::vector<MacroParam> &params,
                            std::string &error_out) {
        size_t cursor = 0;
        while (cursor < params_text.size()) {
            while (cursor < params_text.size() &&
                   (std::isspace(static_cast<unsigned char>(params_text[cursor])) != 0 ||
                    params_text[cursor] == ',')) {
                ++cursor;
            }
            if (cursor >= params_text.size()) {
                break;
            }
            const size_t name_start = cursor;
            while (cursor < params_text.size() &&
                   (std::isalnum(static_cast<unsigned char>(params_text[cursor])) != 0 ||
                    params_text[cursor] == '_')) {
                ++cursor;
            }
            MacroParam param;
            param.name = params_text.substr(name_start, cursor - name_start);
            if (param.name.empty()) {
                error_out = "macro parameter requires a name";
                return false;
            }
            while (cursor < params_text.size() &&
                   std::isspace(static_cast<unsigned char>(params_text[cursor])) != 0) {
                ++cursor;
            }
            if (cursor < params_text.size() && params_text[cursor] == '=') {
                ++cursor;
                const size_t expr_start = cursor;
                int depth = 0;
                char quote = '\0';
                while (cursor < params_text.size()) {
                    const char character = params_text[cursor];
                    if (quote != '\0') {
                        if (character == quote) {
                            quote = '\0';
                        } else if (character == '\\' && cursor + 1 < params_text.size()) {
                            ++cursor;
                        }
                    } else if (character == '\'' || character == '"') {
                        quote = character;
                    } else if (character == '(' || character == '[') {
                        ++depth;
                    } else if (character == ')' || character == ']') {
                        --depth;
                    } else if (character == ',' && depth == 0) {
                        break;
                    }
                    ++cursor;
                }
                if (!parse_expression(params_text.substr(expr_start, cursor - expr_start),
                                      param.default_value,
                                      error_out)) {
                    return false;
                }
            }
            params.push_back(std::move(param));
            if (cursor < params_text.size() && params_text[cursor] == ',') {
                ++cursor;
            }
        }
        return true;
    }

    bool parse_macro(const std::string &tail,
                     size_t &position,
                     std::vector<NodePtr> &nodes,
                     std::string &error_out) {
        size_t cursor = 0;
        while (cursor < tail.size() && std::isspace(static_cast<unsigned char>(tail[cursor])) != 0) {
            ++cursor;
        }
        const size_t name_start = cursor;
        while (cursor < tail.size() &&
               (std::isalnum(static_cast<unsigned char>(tail[cursor])) != 0 || tail[cursor] == '_')) {
            ++cursor;
        }
        auto macro = std::make_unique<MacroNode>();
        macro->name = tail.substr(name_start, cursor - name_start);
        if (macro->name.empty()) {
            error_out = "macro requires a name";
            return false;
        }
        while (cursor < tail.size() && std::isspace(static_cast<unsigned char>(tail[cursor])) != 0) {
            ++cursor;
        }
        if (cursor < tail.size() && tail[cursor] == '(') {
            const size_t params_start = ++cursor;
            int depth = 1;
            while (cursor < tail.size() && depth > 0) {
                if (tail[cursor] == '(') {
                    ++depth;
                } else if (tail[cursor] == ')') {
                    --depth;
                }
                ++cursor;
            }
            if (depth != 0) {
                error_out = "macro parameters missing closing parenthesis";
                return false;
            }
            if (!parse_macro_params(tail.substr(params_start, cursor - params_start - 1),
                                    macro->params,
                                    error_out)) {
                return false;
            }
        }
        ParseResult body;
        if (!parse_nodes(position, {"endmacro"}, false, body, error_out)) {
            return false;
        }
        if (body.closing_keyword != "endmacro") {
            error_out = "macro statement missing endmacro";
            return false;
        }
        macro->body = std::move(body.nodes);
        nodes.push_back(std::move(macro));
        return true;
    }

    bool parse_call(const std::string &tail,
                    size_t &position,
                    std::vector<NodePtr> &nodes,
                    std::string &error_out) {
        std::string remaining = jinja2_tcl::string_utils::trim_ascii_copy(tail);
        auto call_node = std::make_unique<CallBlockNode>();
        if (remaining.empty()) {
            error_out = "call requires a macro name";
            return false;
        }

        if (!remaining.empty() && remaining.front() == '(') {
            size_t close = 0;
            if (!find_matching_parenthesis(remaining, 0, close)) {
                error_out = "call parameters missing closing parenthesis";
                return false;
            }
            if (!parse_macro_params(remaining.substr(1, close - 1), call_node->params, error_out)) {
                return false;
            }
            remaining = jinja2_tcl::string_utils::trim_ascii_copy(remaining.substr(close + 1));
            if (remaining.empty()) {
                error_out = "call requires a macro name";
                return false;
            }
        }

        size_t cursor = 0;
        while (cursor < remaining.size() && std::isspace(static_cast<unsigned char>(remaining[cursor])) != 0) {
            ++cursor;
        }
        const size_t name_start = cursor;
        while (cursor < remaining.size() &&
               (std::isalnum(static_cast<unsigned char>(remaining[cursor])) != 0 ||
                remaining[cursor] == '_')) {
            ++cursor;
        }
        const std::string macro_name = remaining.substr(name_start, cursor - name_start);
        if (macro_name.empty()) {
            error_out = "call requires a macro name";
            return false;
        }
        while (cursor < remaining.size() && std::isspace(static_cast<unsigned char>(remaining[cursor])) != 0) {
            ++cursor;
        }

        std::string call_expression_text;
        if (cursor >= remaining.size()) {
            call_expression_text = macro_name + "()";
        } else if (remaining[cursor] == '(') {
            size_t close = 0;
            if (!find_matching_parenthesis(remaining, cursor, close)) {
                error_out = "call macro arguments missing closing parenthesis";
                return false;
            }
            if (!only_ascii_space_after(remaining, close + 1)) {
                error_out = "call requires `macro_name(...)` followed by endcall";
                return false;
            }
            call_expression_text = macro_name + remaining.substr(cursor, close - cursor + 1);
        } else {
            error_out = "call requires `macro_name(...)` followed by endcall";
            return false;
        }

        if (!parse_expression(call_expression_text, call_node->call_expression, error_out)) {
            return false;
        }

        ParseResult body;
        if (!parse_nodes(position, {"endcall"}, false, body, error_out)) {
            return false;
        }
        if (body.closing_keyword != "endcall") {
            error_out = "call statement missing endcall";
            return false;
        }
        call_node->body = std::move(body.nodes);
        nodes.push_back(std::move(call_node));
        return true;
    }

    std::string input;
    std::string extends_path;
    std::map<std::string, const BlockNode *> blocks;
};

bool parse_template_internal(const std::string &template_text,
                             Template &template_out,
                             std::string &error_out) {
    TemplateParser parser(template_text);
    return parser.parse(template_out, error_out);
}

std::string template_cycle_key(const Template &template_value) {
    return template_value.path.empty() ? "<inline>" : template_value.path;
}

class TemplateRepository {
public:
    explicit TemplateRepository(const TemplateFileSystem *filesystem_in) : filesystem(filesystem_in) {}

    bool parse_text_template(const std::string &template_text,
                             const std::string &template_path,
                             Template &template_out,
                             std::string &error_out) {
        Template parsed;
        if (!template_path.empty() &&
            !normalize_template_path(template_path, parsed.path, error_out)) {
            error_out = "template_path " + error_out;
            return false;
        }
        if (!parse_template_internal(template_text, parsed, error_out)) {
            return false;
        }
        template_out = std::move(parsed);
        return true;
    }

    bool load_template(const std::string &path, const Template *&template_out, std::string &error_out) {
        std::string normalized;
        if (!normalize_template_path(path, normalized, error_out)) {
            return false;
        }
        const auto cached = cache.find(normalized);
        if (cached != cache.end()) {
            template_out = cached->second.get();
            return true;
        }
        if (filesystem == nullptr) {
            error_out = "template filesystem is required to load " + normalized;
            return false;
        }
        if (std::find(loading.begin(), loading.end(), normalized) != loading.end()) {
            error_out = "circular template load involving " + normalized;
            return false;
        }

        std::string content;
        std::string read_error;
        if (!filesystem->read_file(normalized, content, read_error)) {
            error_out = read_error.empty()
                                ? "could not read template file: " + normalized
                                : "could not read template file `" + normalized + "`: " + read_error;
            return false;
        }
        loading.push_back(normalized);
        auto parsed = std::make_unique<Template>();
        parsed->path = normalized;
        if (!parse_template_internal(content, *parsed, error_out)) {
            loading.pop_back();
            error_out = "invalid template `" + normalized + "`: " + error_out;
            return false;
        }
        parsed->path = normalized;
        template_out = parsed.get();
        cache[normalized] = std::move(parsed);
        loading.pop_back();
        return true;
    }

    bool validate_template_graph(const Template &template_value, std::string &error_out) {
        std::set<std::string> visiting;
        std::set<std::string> visited;
        return validate_template_graph(template_value, visiting, visited, error_out);
    }

    bool validate_template_semantics(const Template &template_value, std::string &error_out);
    bool validate_template_semantics(const Template &template_value,
                                     SemanticContext &context,
                                     std::string &error_out);

    bool render_text_template(const std::string &template_text,
                              const std::string &template_path,
                              RenderContext &context,
                              std::string &out,
                              std::string &error_out) {
        Template parsed;
        if (!parse_text_template(template_text, template_path, parsed, error_out)) {
            return false;
        }
        if (!parsed.path.empty()) {
            context.render_stack.push_back(parsed.path);
        }
        const bool ok = render_template(parsed, context, out, error_out);
        if (!parsed.path.empty()) {
            context.render_stack.pop_back();
        }
        return ok;
    }

    bool render_template_file(const std::string &path,
                              RenderContext &context,
                              std::string &out,
                              std::string &error_out) {
        const Template *template_value = nullptr;
        if (!load_template(path, template_value, error_out)) {
            return false;
        }
        const std::string normalized = template_value->path;
        if (std::find(context.render_stack.begin(), context.render_stack.end(), normalized) !=
            context.render_stack.end()) {
            error_out = "circular include involving " + normalized;
            return false;
        }
        context.render_stack.push_back(normalized);
        const bool ok = render_template(*template_value, context, out, error_out);
        context.render_stack.pop_back();
        return ok;
    }

private:
    bool validate_template_graph(const Template &template_value,
                                 std::set<std::string> &visiting,
                                 std::set<std::string> &visited,
                                 std::string &error_out) {
        const std::string key = template_cycle_key(template_value);
        if (visiting.find(key) != visiting.end()) {
            error_out = "circular template dependency involving " + key;
            return false;
        }
        if (visited.find(key) != visited.end()) {
            return true;
        }
        visiting.insert(key);
        if (!template_value.extends_path.empty()) {
            const Template *parent = nullptr;
            if (!load_template(template_value.extends_path, parent, error_out) ||
                !validate_template_graph(*parent, visiting, visited, error_out)) {
                return false;
            }
        }
        for (const std::string &path : template_value.includes) {
            const Template *included = nullptr;
            if (!load_template(path, included, error_out) ||
                !validate_template_graph(*included, visiting, visited, error_out)) {
                return false;
            }
        }
        visiting.erase(key);
        visited.insert(key);
        return true;
    }

    bool collect_inheritance_chain(const Template &template_value,
                                   std::vector<const Template *> &chain,
                                   std::set<std::string> &seen,
                                   std::string &error_out) {
        const std::string key = template_cycle_key(template_value);
        if (seen.find(key) != seen.end()) {
            error_out = "circular template inheritance involving " + key;
            return false;
        }
        seen.insert(key);
        chain.push_back(&template_value);
        if (!template_value.extends_path.empty()) {
            const Template *parent = nullptr;
            if (!load_template(template_value.extends_path, parent, error_out) ||
                !collect_inheritance_chain(*parent, chain, seen, error_out)) {
                return false;
            }
        }
        seen.erase(key);
        return true;
    }

    bool render_template(const Template &template_value,
                         RenderContext &context,
                         std::string &out,
                         std::string &error_out) {
        std::vector<const Template *> chain;
        std::set<std::string> seen;
        if (!collect_inheritance_chain(template_value, chain, seen, error_out)) {
            return false;
        }
        std::map<std::string, std::vector<const BlockNode *>> previous_block_chains = context.block_chains;
        std::map<std::string, std::vector<const BlockNode *>> block_chains;
        for (const Template *entry : chain) {
            for (const auto &block : entry->blocks) {
                block_chains[block.first].push_back(block.second);
            }
        }
        context.block_chains = std::move(block_chains);
        const Template *render_root = chain.empty() ? &template_value : chain.back();
        const bool ok = render_nodes(render_root->nodes, context, out);
        context.block_chains = std::move(previous_block_chains);
        if (!ok) {
            error_out = context.error.empty() ? "template rendering failed" : context.error;
            return false;
        }
        return true;
    }

    const TemplateFileSystem *filesystem = nullptr;
    std::map<std::string, std::unique_ptr<Template>> cache;
    std::vector<std::string> loading;
};

struct SemanticMacroDefinition {
    const std::vector<MacroParam> *params = nullptr;
    const std::vector<NodePtr> *body = nullptr;
};

struct SemanticBlockFrame {
    const std::vector<const BlockNode *> *chain = nullptr;
    size_t index = 0;
};

struct SemanticContext {
    TemplateRepository *repository = nullptr;
    std::map<std::string, SemanticMacroDefinition> macros;
    std::map<std::string, std::vector<const BlockNode *>> block_chains;
    std::vector<SemanticBlockFrame> block_stack;
    std::vector<std::string> macro_call_stack;
    size_t macro_body_depth = 0;
    bool allow_optional_caller = false;

    bool has_super() const {
        if (block_stack.empty()) {
            return false;
        }
        const SemanticBlockFrame &frame = block_stack.back();
        return frame.chain != nullptr && frame.index + 1 < frame.chain->size();
    }
};

bool validate_expression_semantics(const Expression *expression,
                                   SemanticContext &context,
                                   std::string &error_out);
bool validate_nodes_semantics(const std::vector<NodePtr> &nodes,
                              SemanticContext &context,
                              std::string &error_out);
bool validate_block_chain_semantics(const std::vector<const BlockNode *> &chain,
                                    size_t index,
                                    SemanticContext &context,
                                    std::string &error_out);

bool is_caller_defined_condition(const Expression *expression) {
    const auto *defined_test = dynamic_cast<const DefinedTestExpression *>(expression);
    if (defined_test == nullptr || defined_test->negate) {
        return false;
    }
    const auto *variable = dynamic_cast<const VariableExpression *>(defined_test->inner.get());
    return variable != nullptr && variable->name == "caller";
}

bool validate_filter_call_semantics(const FilterCall &filter,
                                    SemanticContext &context,
                                    std::string &error_out) {
    for (const ExprPtr &arg : filter.args) {
        if (arg != nullptr && !validate_expression_semantics(arg.get(), context, error_out)) {
            return false;
        }
    }
    for (const auto &entry : filter.named_args) {
        if (entry.second != nullptr &&
            !validate_expression_semantics(entry.second.get(), context, error_out)) {
            return false;
        }
    }
    return true;
}

bool validate_filter_calls_semantics(const std::vector<FilterCall> &filters,
                                     SemanticContext &context,
                                     std::string &error_out) {
    for (const FilterCall &filter : filters) {
        if (!validate_filter_call_semantics(filter, context, error_out)) {
            return false;
        }
    }
    return true;
}

bool validate_macro_parameter_defaults(const std::vector<MacroParam> &params,
                                       const CallExpression &call,
                                       SemanticContext &context,
                                       std::string &error_out) {
    for (size_t index = 0; index < params.size(); ++index) {
        bool supplied = index < call.args.size();
        if (!supplied && call.named_args.find(params[index].name) != call.named_args.end()) {
            supplied = true;
        }
        if (!supplied && params[index].default_value != nullptr &&
            !validate_expression_semantics(params[index].default_value.get(), context, error_out)) {
            return false;
        }
    }
    return true;
}

bool validate_range_call_semantics(const CallExpression &call,
                                   SemanticContext &context,
                                   std::string &error_out) {
    if (call.args.empty() && call.named_args.empty()) {
        error_out = "range() requires at least a stop argument";
        return false;
    }
    if (call.args.size() > 3) {
        error_out = "range() accepts at most three arguments";
        return false;
    }
    for (const auto &entry : call.named_args) {
        if (!is_range_keyword(entry.first)) {
            error_out = "range() got an unexpected keyword argument: " + entry.first;
            return false;
        }
    }
    if (!call.args.empty() && !call.named_args.empty()) {
        error_out = "range() does not support mixing positional and keyword arguments";
        return false;
    }
    if (!call.named_args.empty() && call.named_args.find("stop") == call.named_args.end()) {
        error_out = "range() keyword form requires stop";
        return false;
    }
    for (const auto &entry : call.named_args) {
        if (entry.second != nullptr &&
            !validate_expression_semantics(entry.second.get(), context, error_out)) {
            return false;
        }
    }
    for (const ExprPtr &arg : call.args) {
        if (arg != nullptr && !validate_expression_semantics(arg.get(), context, error_out)) {
            return false;
        }
    }
    return true;
}

bool validate_macro_call_semantics(const std::string &macro_name,
                                   const CallExpression &call,
                                   SemanticContext &context,
                                   std::string &error_out) {
    if (macro_name.empty()) {
        error_out = "macro call requires a macro name";
        return false;
    }
    if (macro_name == "range") {
        return validate_range_call_semantics(call, context, error_out);
    }
    if (macro_name == "super") {
        if (!context.has_super()) {
            error_out = "super() is only available inside an overridden block";
            return false;
        }
        return validate_block_chain_semantics(*context.block_stack.back().chain,
                                              context.block_stack.back().index + 1,
                                              context,
                                              error_out);
    }

    const auto macro_iterator = context.macros.find(macro_name);
    if (macro_iterator == context.macros.end()) {
        if (macro_name == "caller" && context.macro_body_depth > 0 && context.allow_optional_caller) {
            return true;
        }
        error_out = "undefined macro: " + macro_name;
        return false;
    }

    for (const ExprPtr &arg : call.args) {
        if (arg != nullptr && !validate_expression_semantics(arg.get(), context, error_out)) {
            return false;
        }
    }
    for (const auto &entry : call.named_args) {
        if (entry.second != nullptr &&
            !validate_expression_semantics(entry.second.get(), context, error_out)) {
            return false;
        }
    }

    const SemanticMacroDefinition &macro = macro_iterator->second;
    static const std::vector<MacroParam> kNoParams;
    const std::vector<MacroParam> &params = macro.params == nullptr ? kNoParams : *macro.params;
    if (!validate_macro_parameter_defaults(params, call, context, error_out)) {
        return false;
    }

    if (std::find(context.macro_call_stack.begin(), context.macro_call_stack.end(), macro_name) !=
        context.macro_call_stack.end()) {
        return true;
    }
    context.macro_call_stack.push_back(macro_name);
    ++context.macro_body_depth;
    const bool ok = macro.body == nullptr || validate_nodes_semantics(*macro.body, context, error_out);
    --context.macro_body_depth;
    context.macro_call_stack.pop_back();
    return ok;
}

bool validate_expression_semantics(const Expression *expression,
                                   SemanticContext &context,
                                   std::string &error_out) {
    if (expression == nullptr) {
        return true;
    }
    if (dynamic_cast<const ConstantExpression *>(expression) != nullptr ||
        dynamic_cast<const VariableExpression *>(expression) != nullptr) {
        return true;
    }
    if (const auto *array = dynamic_cast<const ArrayExpression *>(expression)) {
        for (const ExprPtr &item : array->items) {
            if (!validate_expression_semantics(item.get(), context, error_out)) {
                return false;
            }
        }
        return true;
    }
    if (const auto *object = dynamic_cast<const ObjectExpression *>(expression)) {
        for (const auto &item : object->items) {
            if (item.second != nullptr &&
                !validate_expression_semantics(item.second.get(), context, error_out)) {
                return false;
            }
        }
        return true;
    }
    if (const auto *subscript = dynamic_cast<const SubscriptExpression *>(expression)) {
        return validate_expression_semantics(subscript->base.get(), context, error_out) &&
               validate_expression_semantics(subscript->key.get(), context, error_out);
    }
    if (const auto *slice = dynamic_cast<const SliceExpression *>(expression)) {
        return validate_expression_semantics(slice->base.get(), context, error_out) &&
               validate_expression_semantics(slice->start.get(), context, error_out) &&
               validate_expression_semantics(slice->end.get(), context, error_out);
    }
    if (const auto *defined_test = dynamic_cast<const DefinedTestExpression *>(expression)) {
        return validate_expression_semantics(defined_test->inner.get(), context, error_out);
    }
    if (const auto *none_test = dynamic_cast<const NoneTestExpression *>(expression)) {
        return validate_expression_semantics(none_test->inner.get(), context, error_out);
    }
    if (const auto *named_test = dynamic_cast<const NamedTestExpression *>(expression)) {
        if (!validate_expression_semantics(named_test->inner.get(), context, error_out)) {
            return false;
        }
        for (const ExprPtr &arg : named_test->args) {
            if (!validate_expression_semantics(arg.get(), context, error_out)) {
                return false;
            }
        }
        for (const auto &entry : named_test->named_args) {
            if (entry.second != nullptr &&
                !validate_expression_semantics(entry.second.get(), context, error_out)) {
                return false;
            }
        }
        return true;
    }
    if (const auto *unary = dynamic_cast<const UnaryExpression *>(expression)) {
        return validate_expression_semantics(unary->inner.get(), context, error_out);
    }
    if (const auto *binary = dynamic_cast<const BinaryExpression *>(expression)) {
        return validate_expression_semantics(binary->left.get(), context, error_out) &&
               validate_expression_semantics(binary->right.get(), context, error_out);
    }
    if (const auto *conditional = dynamic_cast<const ConditionalExpression *>(expression)) {
        return validate_expression_semantics(conditional->true_expression.get(), context, error_out) &&
               validate_expression_semantics(conditional->condition.get(), context, error_out) &&
               validate_expression_semantics(conditional->false_expression.get(), context, error_out);
    }
    if (const auto *filtered = dynamic_cast<const FilteredExpression *>(expression)) {
        return validate_expression_semantics(filtered->inner.get(), context, error_out) &&
               validate_filter_calls_semantics(filtered->filters, context, error_out);
    }
    if (const auto *method = dynamic_cast<const MethodCallExpression *>(expression)) {
        if (!is_supported_method_name(method->method_name)) {
            error_out = "unsupported method: " + method->method_name;
            return false;
        }
        if (!validate_expression_semantics(method->base.get(), context, error_out)) {
            return false;
        }
        for (const ExprPtr &arg : method->args) {
            if (!validate_expression_semantics(arg.get(), context, error_out)) {
                return false;
            }
        }
        for (const auto &entry : method->named_args) {
            if (entry.second != nullptr &&
                !validate_expression_semantics(entry.second.get(), context, error_out)) {
                return false;
            }
        }
        return true;
    }
    if (const auto *call = dynamic_cast<const CallExpression *>(expression)) {
        if (!validate_expression_semantics(call->callee.get(), context, error_out)) {
            return false;
        }
        return validate_macro_call_semantics(call->callee->variable_name(), *call, context, error_out);
    }
    return true;
}

bool validate_block_chain_semantics(const std::vector<const BlockNode *> &chain,
                                    size_t index,
                                    SemanticContext &context,
                                    std::string &error_out) {
    if (index >= chain.size() || chain[index] == nullptr) {
        return true;
    }
    SemanticBlockFrame frame;
    frame.chain = &chain;
    frame.index = index;
    context.block_stack.push_back(frame);
    const bool ok = validate_nodes_semantics(chain[index]->body, context, error_out);
    context.block_stack.pop_back();
    return ok;
}

bool validate_nodes_semantics(const std::vector<NodePtr> &nodes,
                              SemanticContext &context,
                              std::string &error_out) {
    for (const NodePtr &node : nodes) {
        if (dynamic_cast<const TextNode *>(node.get()) != nullptr) {
            continue;
        }
        if (const auto *output = dynamic_cast<const OutputNode *>(node.get())) {
            if (!validate_expression_semantics(output->expression.get(), context, error_out)) {
                return false;
            }
            continue;
        }
        if (const auto *set = dynamic_cast<const SetNode *>(node.get())) {
            if (!validate_expression_semantics(set->expression.get(), context, error_out)) {
                return false;
            }
            continue;
        }
        if (const auto *block_set = dynamic_cast<const BlockSetNode *>(node.get())) {
            if (!validate_nodes_semantics(block_set->body, context, error_out) ||
                !validate_filter_calls_semantics(block_set->filters, context, error_out)) {
                return false;
            }
            continue;
        }
        if (const auto *include = dynamic_cast<const IncludeNode *>(node.get())) {
            if (context.repository == nullptr) {
                error_out = "include requires a template filesystem";
                return false;
            }
            const Template *included = nullptr;
            if (!context.repository->load_template(include->path, included, error_out) ||
                !context.repository->validate_template_semantics(*included, context, error_out)) {
                return false;
            }
            continue;
        }
        if (const auto *block = dynamic_cast<const BlockNode *>(node.get())) {
            const auto chain = context.block_chains.find(block->name);
            if (chain != context.block_chains.end() && !chain->second.empty()) {
                if (!validate_block_chain_semantics(chain->second, 0, context, error_out)) {
                    return false;
                }
            } else if (!validate_nodes_semantics(block->body, context, error_out)) {
                return false;
            }
            continue;
        }
        if (const auto *if_node = dynamic_cast<const IfNode *>(node.get())) {
            for (const IfBranch &branch : if_node->branches) {
                SemanticContext branch_context = context;
                if (is_caller_defined_condition(branch.condition.get())) {
                    branch_context.allow_optional_caller = true;
                }
                if (!validate_expression_semantics(branch.condition.get(), branch_context, error_out) ||
                    !validate_nodes_semantics(branch.body, branch_context, error_out)) {
                    return false;
                }
            }
            continue;
        }
        if (const auto *for_node = dynamic_cast<const ForNode *>(node.get())) {
            if (!validate_expression_semantics(for_node->collection.get(), context, error_out) ||
                !validate_expression_semantics(for_node->condition.get(), context, error_out)) {
                return false;
            }
            SemanticContext body_context = context;
            if (!validate_nodes_semantics(for_node->body, body_context, error_out)) {
                return false;
            }
            SemanticContext else_context = context;
            if (!validate_nodes_semantics(for_node->else_body, else_context, error_out)) {
                return false;
            }
            continue;
        }
        if (const auto *macro = dynamic_cast<const MacroNode *>(node.get())) {
            SemanticMacroDefinition definition;
            definition.params = &macro->params;
            definition.body = &macro->body;
            context.macros[macro->name] = std::move(definition);
            continue;
        }
        if (const auto *call_block = dynamic_cast<const CallBlockNode *>(node.get())) {
            SemanticMacroDefinition definition;
            definition.params = &call_block->params;
            definition.body = &call_block->body;

            const auto existing = context.macros.find("caller");
            const bool had_existing = existing != context.macros.end();
            SemanticMacroDefinition previous;
            if (had_existing) {
                previous = existing->second;
            }

            context.macros["caller"] = std::move(definition);
            const bool ok = validate_expression_semantics(call_block->call_expression.get(), context, error_out);
            if (had_existing) {
                context.macros["caller"] = previous;
            } else {
                context.macros.erase("caller");
            }
            if (!ok) {
                return false;
            }
            continue;
        }
    }
    return true;
}

bool TemplateRepository::validate_template_semantics(const Template &template_value,
                                                     std::string &error_out) {
    SemanticContext context;
    context.repository = this;
    return validate_template_semantics(template_value, context, error_out);
}

bool TemplateRepository::validate_template_semantics(const Template &template_value,
                                                     SemanticContext &context,
                                                     std::string &error_out) {
    std::vector<const Template *> chain;
    std::set<std::string> seen;
    if (!collect_inheritance_chain(template_value, chain, seen, error_out)) {
        return false;
    }

    std::map<std::string, std::vector<const BlockNode *>> previous_block_chains = context.block_chains;
    std::map<std::string, std::vector<const BlockNode *>> block_chains;
    for (const Template *entry : chain) {
        for (const auto &block : entry->blocks) {
            block_chains[block.first].push_back(block.second);
        }
    }
    context.block_chains = std::move(block_chains);
    const Template *render_root = chain.empty() ? &template_value : chain.back();
    const bool ok = validate_nodes_semantics(render_root->nodes, context, error_out);
    context.block_chains = std::move(previous_block_chains);
    return ok;
}

bool RenderContext::render_template_file(const std::string &path, std::string &out) {
    if (repository == nullptr) {
        error = "include requires a template filesystem";
        return false;
    }
    std::string render_error;
    if (!repository->render_template_file(path, *this, out, render_error)) {
        error = render_error;
        return false;
    }
    return true;
}

}  // namespace

bool TemplateFileSystem::exists_file(const std::string &path) const {
    std::string content;
    std::string error;
    return read_file(path, content, error);
}

RenderResult render_template(const std::string &template_text, const cJSON *context) {
    RenderResult result;
    RenderContext render_context(context);
    TemplateRepository repository(nullptr);
    render_context.repository = &repository;
    if (!repository.render_text_template(template_text, "", render_context, result.output, result.error)) {
        return result;
    }
    result.ok = true;
    return result;
}

RenderResult render_template(const std::string &template_text,
                             const cJSON *context,
                             const TemplateFileSystem &filesystem,
                             const std::string &template_path) {
    RenderResult result;
    RenderContext render_context(context);
    TemplateRepository repository(&filesystem);
    render_context.repository = &repository;
    if (!repository.render_text_template(template_text,
                                         template_path,
                                         render_context,
                                         result.output,
                                         result.error)) {
        return result;
    }
    result.ok = true;
    return result;
}

bool validate_template(const std::string &template_text, std::string &error_out) {
    Template parsed;
    if (!parse_template_internal(template_text, parsed, error_out)) {
        return false;
    }
    if (!parsed.extends_path.empty() || !parsed.includes.empty()) {
        error_out = "template filesystem is required to validate include or extends dependencies";
        return false;
    }
    TemplateRepository repository(nullptr);
    return repository.validate_template_semantics(parsed, error_out);
}

bool validate_template(const std::string &template_text,
                       const TemplateFileSystem &filesystem,
                       std::string &error_out,
                       const std::string &template_path) {
    TemplateRepository repository(&filesystem);
    Template parsed;
    if (!repository.parse_text_template(template_text, template_path, parsed, error_out)) {
        return false;
    }
    return repository.validate_template_graph(parsed, error_out) &&
           repository.validate_template_semantics(parsed, error_out);
}

std::string supported_concepts_description() {
    return "Supported Jinja2 subset: {{ expression }} variables; dot and bracket lookups; string, number, "
           "boolean, null, list, object, and tuple literals; string and list slices `value[:end]`, "
           "`value[start:]`, and `value[start:end]`; inline conditional expressions `value if condition else fallback` "
           "and compatibility form `condition ? value : fallback`; "
           "arithmetic + - * / // % **, list concatenation with +, string concat with ~, comparisons == != < > <= >=, "
           "`in`, `not in`, `is defined`, `is not defined`, `is undefined`, `is none`, `is not none`, "
           "basic value tests, logical and/or/not, and allowlisted non-mutating value methods `split`, `join`, "
           "`strip`/`trim`, `lower`, `upper`, `replace`, `startswith`, `endswith`, `keys`, `values`, `items`, and `get`; "
           "`{% set name = expression %}` assignments that update the nearest existing binding or create a local binding; "
           "`{% set name %}...{% endset %}` block assignments with optional filters; `{% if %}`, `{% elif %}`, "
           "`{% else %}`, `{% endif %}`; `{% for item in collection %}`, loop.index, loop.index0, loop.first, "
           "loop.last, loop.length, unpacking, loop filters, optional `{% else %}`, and `{% endfor %}`; "
           "built-in `range(stop)`, `range(start, stop)`, and `range(start, stop, step)` for bounded numeric loops; "
           "macros declared with `{% macro name(param, default='value') %}` and called with positional or keyword arguments; undefined macro "
           "calls are validation and render errors; bare macro references render only when the macro has zero required parameters; call blocks "
           "`{% call macro(arg) %}...{% endcall %}` with optional caller parameters and `caller(...)`; filters `escape`/`e`, "
           "`default`/`d`, `length`, `attr`, `lower`, `upper`, `capitalize`, `title`, `trim`, `join`, `first`, `last`, "
           "`safe`, `tojson`, `replace`, `format`, `reverse`, `sort`, `min`, `max`, `sum`, `groupby`, `map`, "
           "`reject`, `rejectattr`, `select`, `selectattr`, `unique`, `int`, `float`, `abs`, `round`, "
           "`list`, `dictsort`, `batch`, `slice`, `truncate`, `urlencode`, `wordcount`, and `striptags`. "
           "Loader-backed constant-path includes, root-level extends, blocks, endblock, and block super() are supported "
           "through TemplateFileSystem overloads with strict relative POSIX path validation. Extending templates render "
           "the parent/root template and substitute child blocks without executing top-level child nodes outside blocks, "
           "so templates that use extends may contain only whitespace and block statements at template root level. "
           "Imports, raw blocks, dynamic template paths, unsupported functions, unsupported methods, and custom extensions "
           "beyond the documented tests, methods, filters, and range helper are not supported.";
}

}  // namespace jinja2_tcl::engine
