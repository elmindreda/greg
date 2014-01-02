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

#include <vector>
#include <unordered_set>
#include <sstream>
#include <fstream>

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
  std::ostringstream primitive_types;
  std::ostringstream enums;
  std::ostringstream cmd_types;
  std::ostringstream cmd_pointers;
  std::ostringstream cmd_macros;
  std::ostringstream cmd_definitions;
  std::ostringstream cmd_loaders;
};

Config config;

Manifest required;
Manifest removed;

Output output;

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

std::string toupper(const char* string)
{
  std::string result;

  while (*string)
    result.append(1, std::toupper(*string++));

  return result;
}

const char* type_name(pugi::xml_node type)
{
  if (auto na = type.attribute("name"))
    return na.value();
  else
    return type.child_value("name");
}

const char* return_type(pugi::xml_node node)
{
  if (auto tn = node.child("ptype"))
    return tn.child_value();
  else
    return node.child_value();
}

const char* param_type(pugi::xml_node node)
{
  if (auto tn = node.child("ptype"))
    return tn.child_value();
  else
    return node.child_value();
}

void output_text(std::ostringstream& stream, pugi::xml_node node)
{
  for (auto child : node.children())
  {
    if (child.text())
      stream << child.value();

    output_text(stream, child);
  }
}

std::string command_params(pugi::xml_node node)
{
  std::string result;
  unsigned int count = 0;

  for (auto pn : node.children("param"))
  {
    if (count++)
      result += ", ";

    result += format("%s %s", param_type(pn), pn.child_value("name"));
  }

  if (!count)
    result = "void";

  return result;
}

void append_to_manifest(Manifest& manifest, pugi::xml_node node)
{
  for (auto child : node.children("type"))
    manifest.types.insert(child.attribute("name").value());

  for (auto child : node.children("enum"))
    manifest.enums.insert(child.attribute("name").value());

  for (auto child : node.children("command"))
    manifest.commands.insert(child.attribute("name").value());
}

void append_to_manifests(pugi::xml_node node)
{
  for (auto child : node.children("require"))
    append_to_manifest(required, child);

  for (auto child : node.children("remove"))
    append_to_manifest(removed, child);
}

void generate_manifests(pugi::xml_node root)
{
  for (auto ref : root.select_nodes("/registry/feature"))
  {
    if (ref.node().attribute("api").value() != config.api)
      continue;
    if (ref.node().attribute("number").as_float() > config.version)
      continue;

    append_to_manifests(ref.node());
  }

  for (auto ref : root.select_nodes("/registry/extensions/extension"))
  {
    if (!config.extensions.count(ref.node().attribute("name").value()))
      continue;

    append_to_manifests(ref.node());
  }

  for (auto ref : root.select_nodes("/registry/commands/command"))
  {
    auto node = ref.node();

    if (required.commands.count(node.child("proto").child_value("name")))
    {
      for (auto ref : node.select_nodes("param/ptype"))
        required.types.insert(ref.node().child_value());
    }
  }

  for (auto ref : root.select_nodes("/registry/types/type[@requires]"))
  {
    if (required.types.count(type_name(ref.node())))
      required.types.insert(ref.node().attribute("requires").value());
  }
}

void generate_output(pugi::xml_node root)
{
  for (auto ref : root.select_nodes("/registry/types/type"))
  {
    const char* name = type_name(ref.node());

    if (!required.types.count(name) || removed.types.count(name))
      continue;

    output_text(output.primitive_types, ref.node());
    output.primitive_types << '\n';
  }

  for (auto ref : root.select_nodes("/registry/enums/enum"))
  {
    const char* name = ref.node().attribute("name").value();

    if (!required.enums.count(name) || removed.enums.count(name))
      continue;

    output.enums << format("#define %s %s\n",
                           ref.node().attribute("name").value(),
                           ref.node().attribute("value").value());
  }

  for (auto ref : root.select_nodes("/registry/commands/command"))
  {
    const char* function_name = ref.node().child("proto").child_value("name");

    if (!required.commands.count(function_name) ||
        removed.commands.count(function_name))
    {
      continue;
    }

    std::string typedef_name = "PFN" + toupper(function_name);

    std::string pointer_name = function_name;
    pointer_name.replace(0, 2, "greg");

    output.cmd_types << format("typedef %s (GLAPIENTRY *%s)(%s);\n",
                               return_type(ref.node().child("proto")),
                               typedef_name.c_str(),
                               command_params(ref.node()).c_str());

    output.cmd_pointers << format("extern %s %s;\n",
                                  typedef_name.c_str(),
                                  pointer_name.c_str());

    output.cmd_macros << format("#define %s %s\n",
                                function_name,
                                pointer_name.c_str());

    output.cmd_definitions << format("%s %s = NULL;\n",
                                     typedef_name.c_str(),
                                     pointer_name.c_str());

    output.cmd_loaders << format("  %s = (%s) gregGetProcAddress(\"%s\");\n",
                                 pointer_name.c_str(),
                                 typedef_name.c_str(),
                                 function_name);
  }
}

void replace_tag(std::string& text, const char* tag, const char* content)
{
  const size_t pos = text.find(tag);
  if (pos == std::string::npos)
    return;

  text.replace(pos, std::strlen(tag), content);
}

std::string generate_file(const char* path)
{
  std::ifstream stream(path);
  if (stream.fail())
    error("File not found");

  std::string text;

  stream.seekg(0, std::ios::end);
  text.resize((size_t) stream.tellg());

  stream.seekg(0, std::ios::beg);
  stream.read(&text[0], text.size());

  replace_tag(text, "@PRIMITIVE_TYPES@", output.primitive_types.str().c_str());
  replace_tag(text, "@ENUMS@", output.enums.str().c_str());
  replace_tag(text, "@CMD_TYPES@", output.cmd_types.str().c_str());
  replace_tag(text, "@CMD_POINTERS@", output.cmd_pointers.str().c_str());
  replace_tag(text, "@CMD_MACROS@", output.cmd_macros.str().c_str());
  replace_tag(text, "@CMD_DEFINITIONS@", output.cmd_definitions.str().c_str());
  replace_tag(text, "@CMD_LOADERS@", output.cmd_loaders.str().c_str());

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

  pugi::xml_document document;

  const pugi::xml_parse_result result = document.load(stream);
  if (!result)
    error("Failed to parse file");

  config.api = "gl";
  config.version = 3.2;
  config.extensions.insert("GL_ARB_vertex_buffer_object");

  generate_manifests(document.root());
  generate_output(document.root());

  write_file("greg.h", generate_file("templates/greg.h.in").c_str());
  write_file("greg.c", generate_file("templates/greg.c.in").c_str());

  std::exit(EXIT_SUCCESS);
}

