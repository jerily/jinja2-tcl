// Adapted from jerily-webserver/core/jinja2.
#ifndef JINJA2_TCL_ENGINE_H
#define JINJA2_TCL_ENGINE_H

#include <string>
#include <memory>
#include <vector>

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

// One cache per template environment. The caller owns the filesystem and keeps
// it alive until this cache is destroyed. Request contexts are never retained.
class TemplateCache {
public:
    explicit TemplateCache(const TemplateFileSystem &filesystem);
    ~TemplateCache();
    RenderResult render(const std::string &name, const cJSON *context);
    bool load(const std::string &name, std::string &error);
    bool validate(const std::string &name, std::string &error);
    void clear();
    std::vector<std::string> names() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
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
