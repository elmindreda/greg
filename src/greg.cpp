// GREG — オープンジーエルエクステンションジェネレータ
// Copyright © Camilla Berglund <dreda@dreda.org>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>

#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include <pugixml.hpp>
#include <getopt.h>

namespace
{

struct Config
{
  std::string api;
  float version;
  bool core;
  std::unordered_set<std::string> extensions;
};

struct Version
{
  std::string name;
  float number;
};

struct Manifest
{
  std::vector<Version> versions;
  std::vector<std::string> extensions;
  std::unordered_set<std::string> types;
  std::unordered_set<std::string> commands;
  std::unordered_set<std::string> enums;
};

struct Output
{
  std::string type_typedefs;
  std::string enum_definitions;
  std::string ext_macros;
  std::string ver_macros;
  std::string ext_declarations;
  std::string ver_declarations;
  std::string ext_definitions;
  std::string ver_definitions;
  std::string ver_loaders;
  std::string ext_loaders;
  std::string cmd_typedefs;
  std::string cmd_declarations;
  std::string cmd_macros;
  std::string cmd_definitions;
  std::string cmd_loaders;
};

void usage()
{
  std::puts("Usage: greg [-h]");
}

void error(const char* message) [[noreturn]]
{
  std::puts(message);
  std::exit(EXIT_FAILURE);
}

// Return a string created with the specified C string formatting
//
std::string format(const char* format, ...)
{
  va_list vl;
  static char buffer[8192];

  va_start(vl, format);
  if (std::vsnprintf(buffer, sizeof(buffer), format, vl) < 0)
    buffer[sizeof(buffer) - 1] = '\0';
  va_end(vl);

  return buffer;
}

// Return the uppercase form of the specified string
//
std::string uppercase(const char* string)
{
  std::string result;

  while (*string)
    result.append(1, std::toupper(*string++));

  return result;
}

// Return the API name of a <type> element
// Not all <type> elements have api attributes
//
const char* api_name(const pugi::xml_node tn)
{
  if (const pugi::xml_attribute aa = tn.attribute("api"))
    return aa.value();
  else
    return "gl";
}

// Return the type name of a <type> tag
// This is either a name attribute or pcdata in a <name> element
//
const char* type_name(const pugi::xml_node type)
{
  if (const pugi::xml_attribute na = type.attribute("name"))
    return na.value();
  else
    return type.child_value("name");
}

// Return all pcdata of a <type> element, ignoring <name> elements
//
std::string scrape_type_text(const pugi::xml_node node)
{
  if (node.name() == std::string("apientry"))
    return "GLAPIENTRY";

  std::string result = node.value();

  for (const pugi::xml_node child : node.children())
    result += scrape_type_text(child);

  return result;
}

// Return only the type pcdata of a <param> or <proto> element
//
std::string scrape_proto_text(const pugi::xml_node node)
{
  if (node.name() == std::string("name"))
    return "";

  std::string result = node.value();

  for (const pugi::xml_node child : node.children())
    result += scrape_proto_text(child);

  return result;
}

// Return a complete C parameter declaration from a <command> element
//
std::string command_params(const pugi::xml_node node)
{
  std::string result;

  for (const pugi::xml_node pn : node.children("param"))
  {
    if (!result.empty())
      result += ", ";

    result += scrape_proto_text(pn);
  }

  if (result.empty())
    result = "void";

  return result;
}

// Adds items from a <require> element to the specified manifest
//
void add_to_manifest(Manifest& manifest, const pugi::xml_node node)
{
  for (const pugi::xml_node child : node.children("type"))
    manifest.types.insert(child.attribute("name").value());

  for (const pugi::xml_node child : node.children("enum"))
    manifest.enums.insert(child.attribute("name").value());

  for (const pugi::xml_node child : node.children("command"))
    manifest.commands.insert(child.attribute("name").value());
}

// Removes items from a <remove> element from the specified manifest
//
void remove_from_manifest(Manifest& manifest, const pugi::xml_node node)
{
  for (const pugi::xml_node tn : node.children("type"))
    manifest.types.erase(tn.attribute("name").value());

  for (const pugi::xml_node en : node.children("enum"))
    manifest.enums.erase(en.attribute("name").value());

  for (const pugi::xml_node cn : node.children("command"))
    manifest.commands.erase(cn.attribute("name").value());
}

// Applies a <feature> or <extension> element to the specified manifest
//
void update_manifest(Manifest& manifest,
                     const Config& config,
                     const pugi::xml_node node)
{
  for (const pugi::xml_node rn : node.children("require"))
    add_to_manifest(manifest, rn);

  if (config.core)
  {
    // The core profile removes types, enums and commands added by
    // previous versions
    for (const pugi::xml_node rn : node.children("remove"))
      remove_from_manifest(manifest, rn);
  }
}

// Generates a manifest from the specified document according to the
// specified configuration
//
Manifest generate_manifest(const Config& config, const pugi::xml_document& spec)
{
  Manifest manifest;

  for (const auto ref : spec.select_nodes("/registry/feature"))
  {
    const pugi::xml_node fn = ref.node();
    const float number = fn.attribute("number").as_float();

    if (fn.attribute("api").value() == config.api && number <= config.version)
    {
      update_manifest(manifest, config, fn);

      const Version version = { fn.attribute("name").value(), number };
      manifest.versions.push_back(version);
    }
  }

  for (const auto ref : spec.select_nodes("/registry/extensions/extension"))
  {
    const pugi::xml_node en = ref.node();

    if (config.extensions.count(en.attribute("name").value()))
    {
      update_manifest(manifest, config, en);
      manifest.extensions.push_back(en.attribute("name").value());
    }
  }

  for (const auto ref : spec.select_nodes("/registry/commands/command"))
  {
    const pugi::xml_node cn = ref.node();

    if (manifest.commands.count(cn.child("proto").child_value("name")))
    {
      for (const pugi::xml_node pn : cn.children("param"))
      {
        if (const pugi::xml_node tn = pn.child("ptype"))
          manifest.types.insert(tn.child_value());
      }
    }
  }

  for (const auto ref : spec.select_nodes("/registry/types/type[@requires]"))
  {
    const pugi::xml_node tn = ref.node();

    if (manifest.types.count(type_name(tn)) && api_name(tn) == config.api)
      manifest.types.insert(tn.attribute("requires").value());
  }

  return manifest;
}

// Generates output strings from the specified document according to the
// specified manifest and configuration
//
Output generate_output(const Manifest& manifest,
                       const Config& config,
                       const pugi::xml_document& spec)
{
  Output output;

  for (const std::string& extension : manifest.extensions)
  {
    std::string boolean_name = extension;
    boolean_name.replace(0, 2, "GREG");

    output.ext_macros += format("#define %s 1\n", extension.c_str());
    output.ext_declarations += format("extern int %s;\n", boolean_name.c_str());
    output.ext_definitions += format("int %s;\n", boolean_name.c_str());
    output.ext_loaders += format("  %s = gregExtensionSupported(\"%s\");\n",
                                 boolean_name.c_str(),
                                 extension.c_str());
  }

  for (const Version& version : manifest.versions)
  {
    std::string boolean_name = version.name;
    boolean_name.replace(0, 2, "GREG");

    const int major = (int) std::floor(version.number);
    const int minor = (int) ((version.number - major) * 10);

    output.ver_macros += format("#define %s 1\n", version.name.c_str());
    output.ver_declarations += format("extern int %s;\n", boolean_name.c_str());
    output.ver_definitions += format("int %s;\n", boolean_name.c_str());
    output.ver_loaders += format("  %s = gregVersionSupported(%i, %i);\n",
                                 boolean_name.c_str(),
                                 major, minor);
  }

  for (const auto ref : spec.select_nodes("/registry/types/type"))
  {
    const pugi::xml_node tn = ref.node();

    if (!manifest.types.count(type_name(tn)) || api_name(tn) != config.api)
      continue;

    output.type_typedefs += format("%s\n", scrape_type_text(tn).c_str());
  }

  for (const auto ref : spec.select_nodes("/registry/enums/enum"))
  {
    const pugi::xml_node en = ref.node();

    if (!manifest.enums.count(en.attribute("name").value()))
      continue;

    output.enum_definitions += format("#define %s %s\n",
                                      en.attribute("name").value(),
                                      en.attribute("value").value());
  }

  for (const auto ref : spec.select_nodes("/registry/commands/command"))
  {
    const pugi::xml_node cn = ref.node();

    const char* name = cn.child("proto").child_value("name");
    if (!manifest.commands.count(name))
      continue;

    const std::string typedef_name = "PFN" + uppercase(name) + "PROC";
    const std::string pointer_name = format("greg_%s", name);

    output.cmd_typedefs += format("typedef %s (GLAPIENTRY *%s)(%s);\n",
                                  scrape_proto_text(cn.child("proto")).c_str(),
                                  typedef_name.c_str(),
                                  command_params(cn).c_str());
    output.cmd_declarations += format("extern %s %s;\n",
                                      typedef_name.c_str(),
                                      pointer_name.c_str());
    output.cmd_macros += format("#define %s %s\n", name, pointer_name.c_str());
    output.cmd_definitions += format("%s %s;\n",
                                     typedef_name.c_str(),
                                     pointer_name.c_str());
    output.cmd_loaders += format("  %s = (%s) gregGetProcAddress(\"%s\");\n",
                                 pointer_name.c_str(),
                                 typedef_name.c_str(),
                                 name);
  }

  return output;
}

// Writes the specified text to the specified path
//
void write_file(const char* path, const std::string& content)
{
  std::ofstream stream(path, std::ios::out | std::ios::trunc);
  if (stream.fail())
    error("Failed to create file");

  stream << content;
}

// Returns the text of the specified file
//
std::string read_file(const char* path)
{
  std::ifstream stream(path);
  if (stream.fail())
    error("File not found");

  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

// Replaces the specified tag with the specified text
//
void replace_tag(std::string& text,
                 const std::string& name,
                 const std::string& content)
{
  const size_t pos = text.find(name);
  if (pos == std::string::npos)
    return;

  text.replace(pos, name.length(), content);
}

// Returns the text of the specified file, with any tags replaced by the
// specified output strings
//
std::string generate_content(const Output& output, const char* path)
{
  std::string text = read_file(path);

  replace_tag(text, "@TYPE_TYPEDEFS@", output.type_typedefs);
  replace_tag(text, "@ENUM_DEFINITIONS@", output.enum_definitions);
  replace_tag(text, "@EXT_MACROS@", output.ext_macros);
  replace_tag(text, "@VER_MACROS@", output.ver_macros);
  replace_tag(text, "@EXT_DECLARATIONS@", output.ext_declarations);
  replace_tag(text, "@VER_DECLARATIONS@", output.ver_declarations);
  replace_tag(text, "@EXT_DEFINITIONS@", output.ext_definitions);
  replace_tag(text, "@VER_DEFINITIONS@", output.ver_definitions);
  replace_tag(text, "@VER_LOADERS@", output.ver_loaders);
  replace_tag(text, "@EXT_LOADERS@", output.ext_loaders);
  replace_tag(text, "@CMD_TYPEDEFS@", output.cmd_typedefs);
  replace_tag(text, "@CMD_DECLARATIONS@", output.cmd_declarations);
  replace_tag(text, "@CMD_MACROS@", output.cmd_macros);
  replace_tag(text, "@CMD_DEFINITIONS@", output.cmd_definitions);
  replace_tag(text, "@CMD_LOADERS@", output.cmd_loaders);

  return text;
}

} /* namespace */

int main(int argc, char** argv)
{
  int ch;

  while ((ch = getopt(argc, argv, "h")) != -1)
  {
    switch (ch)
    {
      case 'h':
        usage();
        std::exit(EXIT_SUCCESS);

      default:
        usage();
        std::exit(EXIT_FAILURE);
    }
  }

  std::ifstream stream("spec/gl.xml");
  if (stream.fail())
    error("File not found");

  pugi::xml_document spec;

  const pugi::xml_parse_result result = spec.load(stream);
  if (!result)
    error("Failed to parse file");

  Config config;
  config.api = "gl";
  config.version = 3.2;
  config.core = true;
  config.extensions.insert("GL_ARB_vertex_buffer_object");

  const Manifest manifest = generate_manifest(config, spec);
  const Output output = generate_output(manifest, config, spec);

  write_file("greg.h", generate_content(output, "templates/greg.h.in"));
  write_file("greg.c", generate_content(output, "templates/greg.c.in"));

  std::exit(EXIT_SUCCESS);
}

