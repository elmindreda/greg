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
#include <set>

#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include <wire.hpp>
#include <pugixml.hpp>
#include <getopt.h>

namespace
{

struct Config
{
  wire::string api;
  float version;
  bool core;
  std::set<wire::string> extensions;
};

struct Version
{
  wire::string name;
  float number;
};

struct Manifest
{
  std::vector<Version> versions;
  std::vector<wire::string> extensions;
  std::set<wire::string> types;
  std::set<wire::string> commands;
  std::set<wire::string> enums;
};

struct Output
{
  wire::string type_typedefs;
  wire::string enum_definitions;
  wire::string ext_macros;
  wire::string ver_macros;
  wire::string ext_declarations;
  wire::string ver_declarations;
  wire::string ext_definitions;
  wire::string ver_definitions;
  wire::string ver_loaders;
  wire::string ext_loaders;
  wire::string cmd_typedefs;
  wire::string cmd_declarations;
  wire::string cmd_macros;
  wire::string cmd_definitions;
  wire::string cmd_loaders;
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
wire::string scrape_type_text(const pugi::xml_node node)
{
  if (node.name() == wire::string("apientry"))
    return "GLAPIENTRY";

  wire::string result = node.value();

  for (const pugi::xml_node child : node.children())
    result += scrape_type_text(child);

  return result;
}

// Return only the type pcdata of a <param> or <proto> element
//
wire::string scrape_proto_text(const pugi::xml_node node)
{
  if (node.name() == wire::string("name"))
    return "";

  wire::string result = node.value();

  for (const pugi::xml_node child : node.children())
    result += scrape_proto_text(child);

  return result;
}

// Return a complete C parameter declaration from a <command> element
//
wire::string command_params(const pugi::xml_node node)
{
  wire::string result;

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

  for (const wire::string& extension : manifest.extensions)
  {
    wire::string boolean_name = extension;
    if (boolean_name.starts_with("GL_"))
        boolean_name.replace(0, 3, "GREG_");

    output.ext_macros += wire::string("#define \1 1\n", extension);
    output.ext_declarations += wire::string("extern int \1;\n", boolean_name);
    output.ext_definitions += wire::string("int \1;\n", boolean_name);
    output.ext_loaders += wire::string("  \1 = gregExtensionSupported(\"\2\");\n",
                                       boolean_name,
                                       extension);
  }

  for (const Version& version : manifest.versions)
  {
    wire::string boolean_name = version.name;
    if (boolean_name.starts_with("GL_"))
        boolean_name.replace(0, 3, "GREG_");

    const int major = (int) std::floor(version.number);
    const int minor = (int) ((version.number - (float) major) * 10.f);

    output.ver_macros += wire::string("#define \1 1\n", version.name);
    output.ver_declarations += wire::string("extern int \1;\n", boolean_name);
    output.ver_definitions += wire::string("int \1;\n", boolean_name);
    output.ver_loaders += wire::string("  \1 = gregVersionSupported(\2, \3);\n",
                                       boolean_name,
                                       major, minor);
  }

  for (const auto ref : spec.select_nodes("/registry/types/type"))
  {
    const pugi::xml_node tn = ref.node();

    if (!manifest.types.count(type_name(tn)) || api_name(tn) != config.api)
      continue;

    output.type_typedefs += wire::string("\1\n", scrape_type_text(tn));
  }

  for (const auto ref : spec.select_nodes("/registry/enums/enum"))
  {
    const pugi::xml_node en = ref.node();

    if (!manifest.enums.count(en.attribute("name").value()))
      continue;

    output.enum_definitions += wire::string("#define \1 \2\n",
                                            en.attribute("name").value(),
                                            en.attribute("value").value());
  }

  for (const auto ref : spec.select_nodes("/registry/commands/command"))
  {
    const pugi::xml_node cn = ref.node();

    const wire::string function_name = cn.child("proto").child_value("name");
    if (!manifest.commands.count(function_name))
      continue;

    const wire::string typedef_name("PFN\1PROC", function_name.uppercase());
    const wire::string pointer_name("greg_\1", function_name);

    output.cmd_typedefs += wire::string("typedef \1 (GLAPIENTRY *\2)(\3);\n",
                                        scrape_proto_text(cn.child("proto")),
                                        typedef_name,
                                        command_params(cn));
    output.cmd_declarations += wire::string("extern \1 \2;\n",
                                            typedef_name,
                                            pointer_name);
    output.cmd_macros += wire::string("#define \1 \2\n",
                                      function_name,
                                      pointer_name);
    output.cmd_definitions += wire::string("\1 \2;\n",
                                           typedef_name,
                                           pointer_name);
    output.cmd_loaders += wire::string("  \1 = (\2) gregGetProcAddress(\"\3\");\n",
                                       pointer_name,
                                       typedef_name,
                                       function_name);
  }

  return output;
}

// Writes the specified text to the specified path
//
void write_file(const char* path, const wire::string& content)
{
  std::ofstream stream(path, std::ios::out | std::ios::trunc);
  if (stream.fail())
    error("Failed to create file");

  stream << content;
}

// Returns the text of the specified file
//
wire::string read_file(const char* path)
{
  std::ifstream stream(path);
  if (stream.fail())
    error("File not found");

  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

// Returns the text of the specified file, with any tags replaced by the
// specified output strings
//
wire::string generate_content(const Output& output, const char* path)
{
  wire::string text = read_file(path);

  text = text.replace("@TYPE_TYPEDEFS@", output.type_typedefs);
  text = text.replace("@ENUM_DEFINITIONS@", output.enum_definitions);
  text = text.replace("@EXT_MACROS@", output.ext_macros);
  text = text.replace("@VER_MACROS@", output.ver_macros);
  text = text.replace("@EXT_DECLARATIONS@", output.ext_declarations);
  text = text.replace("@VER_DECLARATIONS@", output.ver_declarations);
  text = text.replace("@EXT_DEFINITIONS@", output.ext_definitions);
  text = text.replace("@VER_DEFINITIONS@", output.ver_definitions);
  text = text.replace("@VER_LOADERS@", output.ver_loaders);
  text = text.replace("@EXT_LOADERS@", output.ext_loaders);
  text = text.replace("@CMD_TYPEDEFS@", output.cmd_typedefs);
  text = text.replace("@CMD_DECLARATIONS@", output.cmd_declarations);
  text = text.replace("@CMD_MACROS@", output.cmd_macros);
  text = text.replace("@CMD_DEFINITIONS@", output.cmd_definitions);
  text = text.replace("@CMD_LOADERS@", output.cmd_loaders);

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

