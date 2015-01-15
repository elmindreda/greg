// GREG - an OpenGL extension loader generator
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

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>

#include <cstring>
#include <cstdio>
#include <cstdlib>

#include <wire.hpp>
#include <pugixml.hpp>
#include <getopt.h>

// WTF, GCC?!
#undef major
#undef minor

namespace
{

class Version
{
public:
  Version() { }
  Version(unsigned int major, unsigned int minor): major(major), minor(minor) { }
  Version(const char* string)
  {
    std::sscanf(string, "%u.%u", &major, &minor);
  }
  bool operator <= (const Version& other) const
  {
    return major < other.major || (major == other.major && minor <= other.minor);
  }
  unsigned int major;
  unsigned int minor;
};

struct Target
{
  wire::string api;
  wire::string profile;
  Version version;
  std::set<wire::string> extensions;
};

struct Feature
{
  wire::string name;
  Version version;
};

struct Manifest
{
  std::vector<Feature> features;
  std::vector<wire::string> extensions;
  std::set<wire::string> types;
  std::set<wire::string> commands;
  std::set<wire::string> enums;
};

struct Output
{
  wire::string api_name;
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
  std::puts("Usage: greg [OPTION]...");
  std::puts("Options:");
  std::puts("  --api=API                client API to generate loader for");
  std::puts("  --core                   use the core profile (OpenGL only)");
  std::puts("  --version=VERSION        highest API version to generate for");
  std::puts("  --extensions=EXTENSIONS  list of extensions to generate for");
  std::puts("  -h, --help               show this help");
}

void error(const char* message)
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

// Return only the type pcdata, recursively, of a <type> element
// This is used to scrape C type text for the type header section
// Any <apientry/> elements are replaced with the standard GLAPIENTRY,
// which is defined by the template file
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

// Return all pcdata of a <param> or <proto> element, recursively,
// ignoring <name> elements
// This is used to scrape C type text for a parameter or return type
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
                     const Target& target,
                     const pugi::xml_node node)
{
  for (const pugi::xml_node rn : node.children("require"))
    add_to_manifest(manifest, rn);

  // Apply <remove> tags for the selected profile
  for (const pugi::xml_node rn : node.children("remove"))
  {
    if (rn.attribute("profile").value() == target.profile)
      remove_from_manifest(manifest, rn);
  }
}

// Generates a manifest from the specified document according to the
// specified target
//
Manifest generate_manifest(const Target& target, const pugi::xml_document& spec)
{
  Manifest manifest;

  for (const auto ref : spec.select_nodes("/registry/feature"))
  {
    const pugi::xml_node fn = ref.node();
    const Version version(fn.attribute("number").as_string());

    if (fn.attribute("api").value() == target.api && version <= target.version)
    {
      update_manifest(manifest, target, fn);

      const Feature feature = { fn.attribute("name").value(), version };
      manifest.features.push_back(feature);
    }
  }

  for (const auto ref : spec.select_nodes("/registry/extensions/extension"))
  {
    const pugi::xml_node en = ref.node();
    const wire::string name(en.attribute("name").value());

    if (target.extensions.count(name))
    {
      const wire::string n = target.api + target.profile;
      const wire::strings p =
        wire::string(en.attribute("supported").value()).split("|");

      if (std::find(p.begin(), p.end(), n) == p.end())
      {
        std::cout << wire::string("Excluding unsupported extension \1\n", name);
        continue;
      }

      update_manifest(manifest, target, en);
      manifest.extensions.push_back(name);
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

    if (manifest.types.count(type_name(tn)) && api_name(tn) == target.api)
      manifest.types.insert(tn.attribute("requires").value());
  }

  return manifest;
}

// Generates output strings from the specified document according to the
// specified manifest and target
//
Output generate_output(const Manifest& manifest,
                       const Target& target,
                       const pugi::xml_document& spec)
{
  Output output;

  if (target.api == "gl")
      output.api_name = "OpenGL";
  else if (target.api == "gles1" || target.api == "gles2")
      output.api_name = "OpenGL ES";

  for (const wire::string& extension : manifest.extensions)
  {
    wire::string boolean_name = extension;
    if (boolean_name.starts_with("GL_"))
        boolean_name.replace(0, 3, "GREG_");

    output.ext_macros += wire::string("#define \1 1\n", extension);
    output.ext_declarations += wire::string("extern int \1;\n", boolean_name);
    output.ext_definitions += wire::string("GREGDEF int \1 = 0;\n", boolean_name);
    output.ext_loaders += wire::string("    \1 = gregExtensionSupported(\"\2\");\n",
                                       boolean_name,
                                       extension);
  }

  for (const Feature& feature : manifest.features)
  {
    wire::string boolean_name = feature.name;
    if (boolean_name.starts_with("GL_"))
        boolean_name.replace(0, 3, "GREG_");

    output.ver_macros += wire::string("#define \1 1\n", feature.name);
    output.ver_declarations += wire::string("extern int \1;\n", boolean_name);
    output.ver_definitions += wire::string("GREGDEF int \1 = 0;\n", boolean_name);
    output.ver_loaders += wire::string("    \1 = gregVersionSupported(\2, \3);\n",
                                       boolean_name,
                                       feature.version.major,
                                       feature.version.minor);
  }

  for (const auto ref : spec.select_nodes("/registry/types/type"))
  {
    const pugi::xml_node tn = ref.node();

    if (!manifest.types.count(type_name(tn)) || api_name(tn) != target.api)
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
    output.cmd_definitions += wire::string("GREGDEF \1 \2 = NULL;\n",
                                           typedef_name,
                                           pointer_name);
    output.cmd_loaders += wire::string("    \1 = (\2) gregGetProcAddress(\"\3\");\n",
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
  std::ofstream stream(path, std::ios::out | std::ios::trunc | std::ios::binary);
  if (stream.fail())
    error("Failed to create file");

  stream << content;
}

// Returns the text of the specified file
//
wire::string read_file(const char* path)
{
  std::ifstream stream(path, std::ios::in | std::ios::binary);
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

  text = text.replace("@API_NAME@", output.api_name);
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
  enum Option { API, CORE, VERSION, EXTENSIONS, HELP };

  int ch;
  Target target = { "gl", "", { 4, 5 } };
  const option options[] =
  {
    { "api", 1, NULL, Option::API },
    { "core", 0, NULL, Option::CORE },
    { "version", 1, NULL, Option::VERSION },
    { "extensions", 1, NULL, Option::EXTENSIONS },
    { "help", 0, NULL, Option::HELP },
    { NULL, 0, NULL, 0 }
  };

  while ((ch = getopt_long(argc, argv, "h", options, NULL)) != -1)
  {
    switch (ch)
    {
      case Option::API:
        target.api = optarg;
        break;

      case Option::CORE:
        target.profile = "core";
        break;

      case Option::VERSION:
        target.version = Version(optarg);
        break;

      case Option::EXTENSIONS:
        for (auto e : wire::string(optarg).split(","))
          target.extensions.insert(e);
        break;

      case 'h':
      case Option::HELP:
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

  const Manifest manifest = generate_manifest(target, spec);
  const Output output = generate_output(manifest, target, spec);

  write_file("output/greg.h", generate_content(output, "templates/greg.h.in"));

  std::exit(EXIT_SUCCESS);
}

