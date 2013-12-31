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

#include <cstring>
#include <cstdio>
#include <cstdlib>

#include <pugixml.hpp>

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
  std::ostringstream types;
  std::ostringstream enums;
  std::ostringstream pointers;
  std::ostringstream defines;
  std::ostringstream loaders;
};

Config config;

Manifest required;
Manifest removed;

Output output;

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

void output_text(std::ostringstream& output, pugi::xml_node node)
{
  for (auto child : node.children())
  {
    if (child.text())
      output << child.value();

    output_text(output, child);
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

    result += format("%s %s", pn.child_value("ptype"), pn.child_value("name"));
  }

  if (!count)
    result += "void";

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

    if (!required.types.count(name))
      continue;
    if (removed.types.count(name))
      continue;

    output_text(output.types, ref.node());
    output.types << '\n';
  }

  for (auto ref : root.select_nodes("/registry/enums/enum"))
  {
    const char* name = ref.node().attribute("name").value();

    if (!required.enums.count(name))
      continue;
    if (removed.enums.count(name))
      continue;

    output.enums << format("#define %s %s\n",
                           ref.node().attribute("name").value(),
                           ref.node().attribute("value").value());
  }

  for (auto ref : root.select_nodes("/registry/commands/command"))
  {
    const char* name = ref.node().child("proto").child_value("name");

    if (!required.commands.count(name))
      continue;
    if (removed.commands.count(name))
      continue;

    std::string pointer_name = name;
    pointer_name.replace(0, 2, "greg");

    output.pointers << format("%s (GLAPIENTRY *%s)(%s);\n",
                              return_type(ref.node().child("proto")),
                              pointer_name.c_str(),
                              command_params(ref.node()).c_str());

    output.defines << format("#define %s %s\n", name, pointer_name.c_str());

    output.loaders << format("  %s = gregGetProcAddress(\"%s\");\n",
                             pointer_name.c_str(),
                             name);
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

  replace_tag(text, "@TYPES@", output.types.str().c_str());
  replace_tag(text, "@ENUMS@", output.enums.str().c_str());
  replace_tag(text, "@POINTERS@", output.pointers.str().c_str());
  replace_tag(text, "@DEFINES@", output.defines.str().c_str());
  replace_tag(text, "@LOADERS@", output.loaders.str().c_str());

  return text;
}

} /* namespace */

int main(int argc, char** argv)
{
  config.api = "gl";
  config.version = 3.2;
  config.extensions.insert("GL_ARB_vertex_buffer_object");

  std::ifstream stream("spec/gl.xml");
  if (stream.fail())
    error("File not found");

  pugi::xml_document document;

  const pugi::xml_parse_result result = document.load(stream);
  if (!result)
    error("Failed to parse file");

  generate_manifests(document.root());
  generate_output(document.root());

  std::puts(generate_file("templates/greg.h.in").c_str());
  std::puts(generate_file("templates/greg.c.in").c_str());

  std::exit(EXIT_SUCCESS);
}

