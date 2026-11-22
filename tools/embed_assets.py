import pathlib
import struct
import os
import subprocess
import sys
import tempfile
root = pathlib.Path(__file__).resolve().parent.parent
firmware_dir = root / "Firmware"
def compile_shader(name, workdir):
    sdk = os.environ.get("VULKAN_SDK", "")
    candidates = [pathlib.Path(sdk) / "Bin" / "glslc.exe"] if sdk else []
    candidates += sorted(pathlib.Path("C:/VulkanSDK").glob("*/Bin/glslc.exe"), reverse=True)
    for glslc in candidates:
        if glslc.exists():
            target = pathlib.Path(workdir) / (name + ".spv")
            subprocess.check_call([str(glslc), "-O", str(root / "shaders" / name), "-o", str(target)])
            return target
    raise SystemExit("glslc.exe not found; set VULKAN_SDK")
workdir = tempfile.mkdtemp()
vert = compile_shader("presentation.vert", workdir)
frag = compile_shader("presentation.frag", workdir)
def ident(name):
    return "k" + "".join(part.capitalize() for part in name.replace(".", "_").split("_"))
def rows(values, per_line, fmt):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + ",".join(fmt % v for v in values[i:i + per_line]) + ",")
    return lines
def write_firmware():
    files = sorted(p for p in firmware_dir.glob("*.rom"))
    out = ['#include "embedded_firmware.hpp"', "#include <cstring>", "namespace {"]
    names = []
    for p in files:
        data = p.read_bytes()
        out.append("const uint8_t %s[] = {" % ident(p.name))
        out.extend(rows(list(data), 24, "%d"))
        out.append("};")
        names.append((p.name, ident(p.name), len(data)))
    out.append("const EmbeddedBlob kBlobs[] = {")
    for name, var, size in names:
        out.append('    {"%s", %s, %d},' % (name, var, size))
    out.append("};")
    out.append("}")
    out.append("const EmbeddedBlob* findEmbeddedFirmware(const char* name) {")
    out.append("    for (const auto& blob : kBlobs)")
    out.append("        if (std::strcmp(blob.name, name) == 0) return &blob;")
    out.append("    return nullptr;")
    out.append("}")
    (root / "src" / "core" / "embedded_firmware.cpp").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
def write_shaders():
    out = ['#include "embedded_shaders.hpp"']
    for var, path in (("kPresentationVertSpv", vert), ("kPresentationFragSpv", frag)):
        data = path.read_bytes()
        words = list(struct.unpack("<%dI" % (len(data) // 4), data))
        out.append("const uint32_t %s[] = {" % var)
        out.extend(rows(words, 8, "0x%08X"))
        out.append("};")
        out.append("const std::size_t %sWords = %d;" % (var, len(words)))
    (root / "src" / "frontend" / "embedded_shaders.cpp").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
write_firmware()
write_shaders()
