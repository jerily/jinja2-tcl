/* Copyright (c) 2026 Neofytos Dimitriou. SPDX-License-Identifier: MIT */
#include "library.h"
#include "jinja2.h"
#include "cJSON.h"

#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

#if TCL_MAJOR_VERSION < 9
using TclSize = int;
#else
using TclSize = Tcl_Size;
#endif

namespace engine = jinja2_tcl::engine;

struct DString {
    Tcl_DString value;
    DString() { Tcl_DStringInit(&value); }
    ~DString() { Tcl_DStringFree(&value); }
    DString(const DString &) = delete;
    DString &operator=(const DString &) = delete;
};

// Tcl 8.6 strings use modified UTF-8; cJSON and the renderer use standard UTF-8.
class Utf8 {
public:
    explicit Utf8(Tcl_Interp *interp) : encoding_(Tcl_GetEncoding(interp, "utf-8")) {
        if (encoding_ == nullptr) {
            throw std::runtime_error("UTF-8 encoding is unavailable");
        }
    }
    ~Utf8() { Tcl_FreeEncoding(encoding_); }
    Utf8(const Utf8 &) = delete;
    Utf8 &operator=(const Utf8 &) = delete;

    std::string get(Tcl_Obj *object) const {
        TclSize length = 0;
        const char *text = Tcl_GetStringFromObj(object, &length);
        DString converted;
        Tcl_UtfToExternalDString(encoding_, text, length, &converted.value);
        return {Tcl_DStringValue(&converted.value),
                static_cast<size_t>(Tcl_DStringLength(&converted.value))};
    }

    void result(Tcl_Interp *interp, const std::string &text) const {
        // Conversion may expand bytes into Tcl's internal encoding.
        if (text.size() > static_cast<size_t>(std::numeric_limits<TclSize>::max()) / 3) {
            throw std::length_error("rendered result is too large for Tcl");
        }
        DString converted;
        Tcl_ExternalToUtfDString(encoding_, text.data(), static_cast<TclSize>(text.size()), &converted.value);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_DStringValue(&converted.value),
                                                Tcl_DStringLength(&converted.value)));
    }

private:
    Tcl_Encoding encoding_;
};

int error(Tcl_Interp *interp, const char *kind, const char *message) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(message, -1));
    Tcl_SetErrorCode(interp, "JINJA2", kind, nullptr);
    return TCL_ERROR;
}

int engine_error(Tcl_Interp *interp, const Utf8 &utf8,
                 const char *kind, const std::string &message) {
    utf8.result(interp, message);
    Tcl_SetErrorCode(interp, "JINJA2", kind, nullptr);
    return TCL_ERROR;
}

struct Templates final : engine::TemplateFileSystem {
    std::map<std::string, std::string> files;

    bool read_file(const std::string &path, std::string &content, std::string &message) const override {
        const auto entry = files.find(path);
        if (entry == files.end()) {
            message = "template not supplied in -templates: " + path;
            return false;
        }
        content = entry->second;
        return true;
    }
};

struct DictSearch {
    Tcl_DictSearch search{};
    bool active = false;
    ~DictSearch() {
        if (active) {
            Tcl_DictObjDone(&search);
        }
    }
};

struct Options {
    Templates templates;
    std::string path;
    bool has_templates = false;
};

int read_options(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[],
                 int start, const Utf8 &utf8, Options &options) {
    if ((objc - start) % 2 != 0) {
        return error(interp, "OPTION", "options must be -templates dictionary and/or -path name");
    }
    bool has_path = false;
    for (int i = start; i < objc; i += 2) {
        const std::string option = utf8.get(objv[i]);
        if (option == "-path") {
            if (has_path) {
                return error(interp, "OPTION", "duplicate -path option");
            }
            has_path = true;
            options.path = utf8.get(objv[i + 1]);
        } else if (option == "-templates") {
            if (options.has_templates) {
                return error(interp, "OPTION", "duplicate -templates option");
            }
            options.has_templates = true;
            DictSearch dict;
            Tcl_Obj *key = nullptr;
            Tcl_Obj *value = nullptr;
            int done = 0;
            if (Tcl_DictObjFirst(interp, objv[i + 1], &dict.search, &key, &value, &done) != TCL_OK) {
                Tcl_SetErrorCode(interp, "JINJA2", "OPTION", nullptr);
                return TCL_ERROR;
            }
            dict.active = !done;
            while (!done) {
                options.templates.files.emplace(utf8.get(key), utf8.get(value));
                Tcl_DictObjNext(&dict.search, &key, &value, &done);
                dict.active = !done;
            }
        } else {
            return error(interp, "OPTION", "unknown option: expected -templates or -path");
        }
    }
    if (has_path && !options.has_templates) {
        return error(interp, "OPTION", "-path requires -templates");
    }
    return TCL_OK;
}

bool contains_json_nul(const std::string &text) {
    if (text.find('\0') != std::string::npos) {
        return true;
    }
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\') {
            if (text.compare(i, 6, "\\u0000") == 0) {
                return true;
            }
            ++i; // Skip escaped backslashes: "\\\\u0000" represents literal text.
        }
    }
    return false;
}

bool finite_numbers(const cJSON *node) {
    if (cJSON_IsNumber(node) && !std::isfinite(node->valuedouble)) {
        return false;
    }
    for (const cJSON *child = node->child; child != nullptr; child = child->next) {
        if (!finite_numbers(child)) {
            return false;
        }
    }
    return true;
}

// cJSON maintains a global parse-error location even when return_parse_end is used.
// Serialize parsing across Tcl threads; no allocator hooks are changed by this module.
Tcl_Mutex json_mutex;

int render(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "template json ?-templates dictionary? ?-path name?");
        return TCL_ERROR;
    }
    const Utf8 utf8(interp);
    Options options;
    if (read_options(interp, objc, objv, 3, utf8, options) != TCL_OK) {
        return TCL_ERROR;
    }
    const std::string json = utf8.get(objv[2]);
    if (contains_json_nul(json)) {
        return error(interp, "JSON", "JSON context must not contain NUL characters (including \\u0000)");
    }
    const char *end = nullptr;
    Tcl_MutexLock(&json_mutex);
    cJSON *parsed = cJSON_ParseWithLengthOpts(json.c_str(), json.size() + 1, &end, 1);
    Tcl_MutexUnlock(&json_mutex);
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> context(parsed, cJSON_Delete);
    if (!context) {
        const std::string message = "invalid JSON context at UTF-8 byte " +
                                    std::to_string(end == nullptr ? 0 : end - json.c_str());
        return error(interp, "JSON", message.c_str());
    }
    if (!cJSON_IsObject(context.get())) {
        return error(interp, "JSON", "JSON context must be an object");
    }
    if (!finite_numbers(context.get())) {
        return error(interp, "JSON", "JSON context numbers must be finite");
    }
    const std::string text = utf8.get(objv[1]);
    const engine::RenderResult result = options.has_templates
        ? engine::render_template(text, context.get(), options.templates, options.path)
        : engine::render_template(text, context.get());
    if (!result.ok) {
        return engine_error(interp, utf8, "RENDER", result.error);
    }
    utf8.result(interp, result.output);
    return TCL_OK;
}

int validate(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "template ?-templates dictionary? ?-path name?");
        return TCL_ERROR;
    }
    const Utf8 utf8(interp);
    Options options;
    if (read_options(interp, objc, objv, 2, utf8, options) != TCL_OK) {
        return TCL_ERROR;
    }
    std::string message;
    const std::string text = utf8.get(objv[1]);
    const bool valid = options.has_templates
        ? engine::validate_template(text, options.templates, message, options.path)
        : engine::validate_template(text, message);
    if (!valid) {
        return engine_error(interp, utf8, "VALIDATE", message);
    }
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
    return TCL_OK;
}

int supported(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }
    Utf8(interp).result(interp, engine::supported_concepts_description());
    return TCL_OK;
}

template<int (*Command)(Tcl_Interp *, int, Tcl_Obj *const[])>
int dispatch(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) noexcept {
    try {
        return Command(interp, objc, objv);
    } catch (const std::bad_alloc &) {
        return error(interp, "MEMORY", "unable to allocate memory for template processing");
    } catch (const std::exception &exception) {
        return error(interp, "INTERNAL", exception.what());
    } catch (...) {
        return error(interp, "INTERNAL", "unexpected C++ exception during template processing");
    }
}

} // namespace

extern "C" int Jinja2_Init(Tcl_Interp *interp) {
#if TCL_MAJOR_VERSION < 9
    const char *minimum = "8.6";
#else
    const char *minimum = "9.0";
#endif
    if (Tcl_InitStubs(interp, minimum, 0) == nullptr) {
        return TCL_ERROR;
    }
    if (Tcl_PkgProvide(interp, "jinja2", JINJA2_VERSION) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_FindNamespace(interp, "::jinja2", nullptr, TCL_GLOBAL_ONLY) == nullptr &&
        Tcl_CreateNamespace(interp, "::jinja2", nullptr, nullptr) == nullptr) {
        return TCL_ERROR;
    }
    Tcl_CreateObjCommand(interp, "::jinja2::render", dispatch<render>, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::jinja2::validate", dispatch<validate>, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::jinja2::supported_concepts", dispatch<supported>, nullptr, nullptr);
    return TCL_OK;
}
