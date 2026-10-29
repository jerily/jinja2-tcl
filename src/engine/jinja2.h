// Adapted from jerily-webserver/core/jinja2.
#ifndef JINJA2_TCL_ENGINE_H
#define JINJA2_TCL_ENGINE_H

#include <string>

struct cJSON;

namespace jinja2_tcl::engine {

struct RenderResult {
    bool ok = false;
    std::string output;
    std::string error;
};

struct TemplateFileSystem {
    virtual ~TemplateFileSystem() = default;
    virtual bool read_file(const std::string &path, std::string &content_out, std::string &error_out) const = 0;
    virtual bool exists_file(const std::string &path) const;
};

RenderResult render_template(const std::string &template_text, const cJSON *context);
RenderResult render_template(const std::string &template_text,
                             const cJSON *context,
                             const TemplateFileSystem &filesystem,
                             const std::string &template_path = "");

bool validate_template(const std::string &template_text, std::string &error_out);
bool validate_template(const std::string &template_text,
                       const TemplateFileSystem &filesystem,
                       std::string &error_out,
                       const std::string &template_path = "");

std::string supported_concepts_description();

}  // namespace jinja2_tcl::engine

#endif
