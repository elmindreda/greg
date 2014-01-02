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
#include <string>
#include <vector>
#include <unordered_set>

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

struct Manifest
{
  std::unordered_set<std::string> types;
  std::unordered_set<std::string> commands;
  std::unordered_set<std::string> enums;
};

struct Output
{
  std::string primitive_types;
  std::string enums;
  std::string cmd_types;
  std::string cmd_pointers;
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

std::string ucase(const char* string)
{
  std::string result;

  while (*string)
    result.append(1, std::toupper(*string++));

  return result;
}

const char* type_name(const pugi::xml_node type)
{
  if (const pugi::xml_attribute na = type.attribute("name"))
    return na.value();
  else
    return type.child_value("name");
}

const char* cmd_type_name(const pugi::xml_node node)
{
  if (const pugi::xml_node tn = node.child("ptype"))
    return tn.child_value();
  else
    return node.child_value();
}

std::string type_text(const pugi::xml_node node)
{
  std::string result;

  for (const pugi::xml_node child : node.children())
  {
    if (child.text())
      result += child.value();

    result += type_text(child);
  }

  return result;
}

std::string command_params(const pugi::xml_node node)
{
  std::string result;
  unsigned int count = 0;

  for (const pugi::xml_node pn : node.children("param"))
  {
    if (count++)
      result += ", ";

    result += format("%s %s", cmd_type_name(pn), pn.child_value("name"));
  }

  if (!count)
    result = "void";

  return result;
}

void add_to_manifest(Manifest& manifest, const pugi::xml_node node)
{
  for (const pugi::xml_node child : node.children("type"))
    manifest.types.insert(child.attribute("name").value());

  for (const pugi::xml_node child : node.children("enum"))
    manifest.enums.insert(child.attribute("name").value());

  for (const pugi::xml_node child : node.children("command"))
    manifest.commands.insert(child.attribute("name").value());
}

void remove_from_manifest(Manifest& manifest, const pugi::xml_node node)
{
  for (const pugi::xml_node tn : node.children("type"))
    manifest.types.erase(tn.attribute("name").value());

  for (const pugi::xml_node en : node.children("enum"))
    manifest.enums.erase(en.attribute("name").value());

  for (const pugi::xml_node cn : node.children("command"))
    manifest.commands.erase(cn.attribute("name").value());
}

void update_manifest(Manifest& manifest,
                     const Config& config,
                     const pugi::xml_node node)
{
  for (const pugi::xml_node rn : node.children("require"))
    add_to_manifest(manifest, rn);

  if (config.core)
  {
    for (const pugi::xml_node rn : node.children("remove"))
      remove_from_manifest(manifest, rn);
  }
}

Manifest generate_manifest(const Config& config, const pugi::xml_document& spec)
{
  Manifest manifest;

  for (const auto ref : spec.select_nodes("/registry/feature"))
  {
    const pugi::xml_node fn = ref.node();

    if (fn.attribute("api").value() == config.api &&
        fn.attribute("number").as_float() <= config.version)
    {
      update_manifest(manifest, config, fn);
    }
  }

  for (const auto ref : spec.select_nodes("/registry/extensions/extension"))
  {
    const pugi::xml_node en = ref.node();

    if (config.extensions.count(en.attribute("name").value()))
      update_manifest(manifest, config, en);
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

    if (manifest.types.count(type_name(tn)))
      manifest.types.insert(tn.attribute("requires").value());
  }

  return manifest;
}

Output generate_output(const Manifest& manifest, const pugi::xml_document& spec)
{
  Output output;

  for (const auto ref : spec.select_nodes("/registry/types/type"))
  {
    const pugi::xml_node tn = ref.node();

    if (!manifest.types.count(type_name(tn)))
      continue;

    output.primitive_types += format("%s\n", type_text(tn).c_str());
  }

  for (const auto ref : spec.select_nodes("/registry/enums/enum"))
  {
    const pugi::xml_node en = ref.node();

    if (!manifest.enums.count(en.attribute("name").value()))
      continue;

    output.enums += format("#define %s %s\n",
                           en.attribute("name").value(),
                           en.attribute("value").value());
  }

  for (const auto ref : spec.select_nodes("/registry/commands/command"))
  {
    const pugi::xml_node cn = ref.node();

    const char* function_name = cn.child("proto").child_value("name");
    if (!manifest.commands.count(function_name))
      continue;

    std::string typedef_name = "PFN" + ucase(function_name);

    std::string pointer_name = function_name;
    pointer_name.replace(0, 2, "greg");

    output.cmd_types += format("typedef %s (GLAPIENTRY *%s)(%s);\n",
                               cmd_type_name(cn.child("proto")),
                               typedef_name.c_str(),
                               command_params(cn).c_str());

    output.cmd_pointers += format("extern %s %s;\n",
                                  typedef_name.c_str(),
                                  pointer_name.c_str());

    output.cmd_macros += format("#define %s %s\n",
                                function_name,
                                pointer_name.c_str());

    output.cmd_definitions += format("%s %s = NULL;\n",
                                     typedef_name.c_str(),
                                     pointer_name.c_str());

    output.cmd_loaders += format("  %s = (%s) gregGetProcAddress(\"%s\");\n",
                                 pointer_name.c_str(),
                                 typedef_name.c_str(),
                                 function_name);
  }

  return output;
}

void replace_tag(std::string& text, const char* tag, const char* content)
{
  const size_t pos = text.find(tag);
  if (pos == std::string::npos)
    return;

  text.replace(pos, std::strlen(tag), content);
}

std::string generate_file(const Output& output, const char* path)
{
  std::ifstream stream(path);
  if (stream.fail())
    error("File not found");

  std::string text;

  stream.seekg(0, std::ios::end);
  text.resize((size_t) stream.tellg());

  stream.seekg(0, std::ios::beg);
  stream.read(&text[0], text.size());

  replace_tag(text, "@PRIMITIVE_TYPES@", output.primitive_types.c_str());
  replace_tag(text, "@ENUMS@", output.enums.c_str());
  replace_tag(text, "@CMD_TYPES@", output.cmd_types.c_str());
  replace_tag(text, "@CMD_POINTERS@", output.cmd_pointers.c_str());
  replace_tag(text, "@CMD_MACROS@", output.cmd_macros.c_str());
  replace_tag(text, "@CMD_DEFINITIONS@", output.cmd_definitions.c_str());
  replace_tag(text, "@CMD_LOADERS@", output.cmd_loaders.c_str());

  return text;
}

void write_file(const char* path, const char* content)
{
  std::ofstream stream(path, std::ios::out | std::ios::trunc);
  if (stream.fail())
    error("Failed to create file");

  stream << content;
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
  const Output output = generate_output(manifest, spec);

  write_file("greg.h", generate_file(output, "templates/greg.h.in").c_str());
  write_file("greg.c", generate_file(output, "templates/greg.c.in").c_str());

  std::exit(EXIT_SUCCESS);
}

