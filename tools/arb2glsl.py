#!/usr/bin/env python3
"""Translates ARB vertex and fragment programs (.vfp, .vp, .fp) to GLSL for uam.

The registers keep the meaning they have in the renderer (src/renderer/deko3d):
program.env, program.local and the state matrices are uniform blocks, and the
attributes follow idDrawVert.
"""

import re

VS_HEADER = """#version 460

layout(location = 0) in vec3 inPosition3;
layout(location = 1) in vec2 inTexCoord2;
layout(location = 2) in vec3 inNormal3;
layout(location = 3) in vec3 inTangent3;
layout(location = 4) in vec3 inBitangent3;
layout(location = 5) in vec4 inColor;

layout(std140, binding = 0) uniform Matrices {
	vec4 mvpRow[4];
	vec4 modelViewRow[4];
	vec4 projectionRow[4];
	vec4 textureRow[4];
};
layout(std140, binding = 1) uniform VsEnv {
	vec4 env[24];
};
layout(std140, binding = 2) uniform VsLocal {
	vec4 local[16];
};
"""

FS_HEADER = """#version 460

layout(std140, binding = 0) uniform FsEnv {
	vec4 env[8];
};
layout(std140, binding = 1) uniform FsLocal {
	vec4 local[16];
};
"""

VS_INPUTS = {
    "vertex.position": "vec4( inPosition3, 1.0 )",
    "vertex.normal": "vec4( inNormal3, 1.0 )",
    "vertex.color": "inColor",
    "vertex.color.primary": "inColor",
    "vertex.texcoord": "vec4( inTexCoord2, 0.0, 1.0 )",
    "vertex.texcoord[0]": "vec4( inTexCoord2, 0.0, 1.0 )",
    "vertex.attrib[0]": "vec4( inPosition3, 1.0 )",
    "vertex.attrib[2]": "vec4( inNormal3, 1.0 )",
    "vertex.attrib[3]": "inColor",
    "vertex.attrib[8]": "vec4( inTexCoord2, 0.0, 1.0 )",
    "vertex.attrib[9]": "vec4( inTangent3, 1.0 )",
    "vertex.attrib[10]": "vec4( inBitangent3, 1.0 )",
    "vertex.attrib[11]": "vec4( inNormal3, 1.0 )",
}

STATE_MATRICES = {
    "state.matrix.mvp": "mvpRow",
    "state.matrix.modelview": "modelViewRow",
    "state.matrix.projection": "projectionRow",
    "state.matrix.texture": "textureRow",
}

# name -> (GLSL template, result is scalar and replicated)
OPCODES = {
    "MOV": "{0}",
    "ADD": "{0} + {1}",
    "SUB": "{0} - {1}",
    "MUL": "{0} * {1}",
    "MAD": "{0} * {1} + {2}",
    "ABS": "abs( {0} )",
    "FLR": "floor( {0} )",
    "FRC": "fract( {0} )",
    "MAX": "max( {0}, {1} )",
    "MIN": "min( {0}, {1} )",
    "SGE": "vec4( greaterThanEqual( {0}, {1} ) )",
    "SLT": "vec4( lessThan( {0}, {1} ) )",
    "LRP": "mix( {2}, {1}, {0} )",
    "CMP": "mix( {2}, {1}, vec4( greaterThanEqual( {0}, vec4( 0.0 ) ) ) )",
    "DP3": "vec4( dot( {0}.xyz, {1}.xyz ) )",
    "DP4": "vec4( dot( {0}, {1} ) )",
    "DPH": "vec4( dot( vec4( {0}.xyz, 1.0 ), {1} ) )",
    "XPD": "vec4( cross( {0}.xyz, {1}.xyz ), 1.0 )",
    "RSQ": "vec4( inversesqrt( abs( {0}.x ) ) )",
    "RCP": "vec4( 1.0 / {0}.x )",
    "POW": "vec4( pow( abs( {0}.x ), {1}.x ) )",
    "EXP": "vec4( exp2( {0}.x ) )",
    "LOG": "vec4( log2( abs( {0}.x ) ) )",
    "SIN": "vec4( sin( {0}.x ) )",
    "COS": "vec4( cos( {0}.x ) )",
}

SAMPLER_TYPES = {"1D": "sampler2D", "2D": "sampler2D", "3D": "sampler3D", "CUBE": "samplerCube", "RECT": "sampler2D"}
SAMPLER_COORDS = {"1D": "vec2( {0}.x, 0.5 )", "2D": "{0}.xy", "3D": "{0}.xyz", "CUBE": "{0}.xyz", "RECT": "{0}.xy"}


class TranslationError(Exception):
    pass


def split_programs(text):
    """Returns the vertex and fragment program bodies of a file."""
    parts = {}
    for match in re.finditer(r"!!ARB(vp|fp)1\.0(.*?)\bEND\b", text, re.S):
        parts[match.group(1)] = match.group(2)
    return parts.get("vp"), parts.get("fp")


def strip_comments(source):
    return re.sub(r"#[^\n]*", "", source)


def split_statements(source):
    return [s.strip() for s in strip_comments(source).split(";") if s.strip()]


class Translator:
    def __init__(self, source, stage):
        self.stage = stage  # "vert" or "frag"
        self.source = source
        self.temps = []
        self.params = {}
        self.aliases = {}
        self.body = []
        self.outputs = {}
        self.inputs = {}
        self.samplers = {}
        self.position_invariant = False
        self.writes_depth = False
        self.statements = split_statements(source)

    # --- operands ---

    def var_name(self, base):
        return re.sub(r"\W", "_", base)

    def operand_base(self, name):
        name = self.aliases.get(name, name)
        if name in self.params:
            return self.params[name]
        if name in self.temps:
            return "t_" + name

        if self.stage == "vert" and name in VS_INPUTS:
            return VS_INPUTS[name]

        match = re.fullmatch(r"program\.(env|local)\[(\d+)\]", name)
        if match:
            return "%s[%s]" % (match.group(1), match.group(2))

        match = re.fullmatch(r"(state\.matrix\.\w+)\.row\[(\d+)\]", name)
        if match:
            return "%s[%s]" % (STATE_MATRICES[match.group(1)], match.group(2))

        if self.stage == "frag":
            if name == "fragment.position":
                # the programs measure y from the bottom of the screen, as OpenGL does
                return "vec4( gl_FragCoord.x, env[7].x - gl_FragCoord.y, gl_FragCoord.zw )"
            match = re.fullmatch(r"fragment\.texcoord(?:\[(\d+)\])?", name)
            if match:
                return self.input_var("vTexCoord%s" % (match.group(1) or "0"), self.texcoord_location(match.group(1) or "0"))
            if name in ("fragment.color", "fragment.color.primary"):
                return self.input_var("vColor", 8)
            if name == "fragment.color.secondary":
                return self.input_var("vColor2", 9)

        raise TranslationError("unknown operand " + name)

    @staticmethod
    def texcoord_location(index):
        return int(index)

    def input_var(self, name, location):
        self.inputs[name] = location
        return name

    def literal(self, text):
        values = [v.strip() for v in text.strip("{} ").split(",")]
        while len(values) < 4:
            values.append("1.0" if len(values) == 3 else "0.0")
        return "vec4( %s )" % ", ".join(self.number(v) for v in values)

    @staticmethod
    def number(text):
        value = float(text)
        return ("%.9g" % value) if "." in ("%.9g" % value) or "e" in ("%.9g" % value) else ("%.9g.0" % value)

    def source_operand(self, text):
        text = text.strip()
        negate = text.startswith("-")
        if negate:
            text = text[1:].strip()
        if text.startswith("+"):
            text = text[1:].strip()

        swizzle = ""
        if not text.startswith("{") and not re.fullmatch(r"[\d.eE+\-]+", text):
            match = re.fullmatch(r"(.+?)\.([xyzwrgba]{1,4})", text)
            if match:
                text, swizzle = match.group(1), match.group(2)

        if text.startswith("{"):
            base = self.literal(text)
        elif re.fullmatch(r"[\d.eE+\-]+", text):
            base = "vec4( %s )" % self.number(text)
        else:
            base = self.operand_base(text)

        if swizzle:
            swizzle = swizzle.translate(str.maketrans("rgba", "xyzw"))
            if len(swizzle) == 1:
                swizzle = swizzle * 4
            base = "%s.%s" % (base, swizzle)
        return "-" + base if negate else base

    def destination(self, text):
        text = text.strip()
        mask = ""
        match = re.fullmatch(r"(.+?)\.([xyzwrgba]{1,4})", text)
        if match:
            text, mask = match.group(1), match.group(2).translate(str.maketrans("rgba", "xyzw"))
        text = self.aliases.get(text, text)

        if text in self.temps:
            return "t_" + text, mask

        if text in ("result.position",):
            return "gl_Position", mask
        if text in ("result.color", "result.color.primary"):
            if self.stage == "vert":
                self.outputs["vColor"] = 8
                return "vColor", mask
            self.outputs["outColor"] = 0
            return "outColor", mask
        if text == "result.color.secondary":
            self.outputs["vColor2"] = 9
            return "vColor2", mask
        if text == "result.depth":
            self.writes_depth = True
            return "gl_FragDepth", ""
        match = re.fullmatch(r"result\.texcoord(?:\[(\d+)\])?", text)
        if match:
            index = match.group(1) or "0"
            name = "vTexCoord" + index
            self.outputs[name] = self.texcoord_location(index)
            return name, mask
        raise TranslationError("unknown destination " + text)

    # --- statements ---

    def split_args(self, text):
        args, depth, current = [], 0, ""
        for ch in text:
            if ch in "{[":
                depth += 1
            elif ch in "}]":
                depth -= 1
            if ch == "," and depth == 0:
                args.append(current.strip())
                current = ""
            else:
                current += ch
        if current.strip():
            args.append(current.strip())
        return args

    def emit(self, dest, mask, value, saturate):
        if saturate:
            value = "clamp( %s, 0.0, 1.0 )" % value
        if dest == "gl_FragDepth":
            self.body.append("\tgl_FragDepth = ( %s ).z;" % value)
            return
        if mask:
            self.body.append("\t%s.%s = ( %s ).%s;" % (dest, mask, value, mask))
        else:
            self.body.append("\t%s = %s;" % (dest, value))

    def texture_op(self, opcode, args, saturate):
        dest, mask = self.destination(args[0])
        coord = self.source_operand(args[1])
        unit_match = re.fullmatch(r"texture\[(\d+)\]", args[2].strip())
        if not unit_match:
            raise TranslationError("bad texture unit " + args[2])
        unit = int(unit_match.group(1))
        target = args[3].strip()
        if target not in SAMPLER_TYPES:
            raise TranslationError("bad texture target " + target)
        sampler = "map%d" % unit
        if unit in self.samplers and self.samplers[unit] != SAMPLER_TYPES[target]:
            raise TranslationError("unit %d used with two targets" % unit)
        self.samplers[unit] = SAMPLER_TYPES[target]

        if opcode == "TXP":
            if target == "2D":
                value = "textureProj( %s, ( %s ).xyw )" % (sampler, coord)
            elif target == "CUBE":
                value = "texture( %s, ( %s ).xyz )" % (sampler, coord)
            else:
                value = "textureProj( %s, ( %s ).xyw )" % (sampler, coord)
        elif opcode == "TXB":
            value = "texture( %s, %s, ( %s ).w )" % (sampler, SAMPLER_COORDS[target].format("( %s )" % coord), coord)
        else:
            value = "texture( %s, %s )" % (sampler, SAMPLER_COORDS[target].format("( %s )" % coord))
        self.emit(dest, mask, value, saturate)

    def statement(self, text):
        parts = text.split(None, 1)
        keyword = parts[0]
        rest = parts[1] if len(parts) > 1 else ""

        if keyword == "OPTION":
            if "position_invariant" in rest:
                self.position_invariant = True
            return
        if keyword in ("TEMP", "ADDRESS"):
            self.temps.extend(n.strip() for n in rest.split(","))
            return
        if keyword == "ATTRIB":
            name, value = [s.strip() for s in rest.split("=", 1)]
            self.aliases[name] = value
            return
        if keyword == "OUTPUT":
            name, value = [s.strip() for s in rest.split("=", 1)]
            self.aliases[name] = value
            return
        if keyword == "ALIAS":
            name, value = [s.strip() for s in rest.split("=", 1)]
            self.aliases[name] = value
            return
        if keyword == "PARAM":
            match = re.fullmatch(r"(\w+)(\[\d*\])?\s*=\s*(.+)", rest, re.S)
            if not match or match.group(2):
                raise TranslationError("unsupported PARAM " + rest)
            value = match.group(3).strip()
            self.params[match.group(1)] = self.source_operand(value)
            return

        opcode = keyword
        saturate = False
        if opcode.endswith("_SAT"):
            opcode = opcode[:-4]
            saturate = True

        args = self.split_args(rest)
        if opcode in ("TEX", "TXP", "TXB"):
            self.texture_op(opcode, args, saturate)
            return
        if opcode == "KIL":
            self.body.append("\tif ( any( lessThan( %s, vec4( 0.0 ) ) ) ) {\n\t\tdiscard;\n\t}" % self.source_operand(args[0]))
            return
        if opcode not in OPCODES:
            raise TranslationError("unsupported opcode " + opcode)

        dest, mask = self.destination(args[0])
        sources = [self.source_operand(a) for a in args[1:]]
        sources = [s if re.fullmatch(r"[\w\[\].]+", s) else "( %s )" % s for s in sources]
        self.emit(dest, mask, OPCODES[opcode].format(*sources), saturate)

    def translate(self):
        for text in self.statements:
            self.statement(text)

        lines = [VS_HEADER if self.stage == "vert" else FS_HEADER]

        for name, location in sorted(self.inputs.items(), key=lambda item: item[1]):
            lines.append("layout(location = %d) in vec4 %s;" % (location, name))

        for unit, sampler in sorted(self.samplers.items()):
            lines.append("layout(binding = %d) uniform %s map%d;" % (unit, sampler, unit))

        for name, location in sorted(self.outputs.items(), key=lambda item: item[1]):
            lines.append("layout(location = %d) out vec4 %s;" % (location, name))
        if self.stage == "frag" and "outColor" not in self.outputs:
            lines.append("layout(location = 0) out vec4 outColor;")

        lines.append("")
        lines.append("void main() {")
        for name in self.temps:
            lines.append("\tvec4 t_%s = vec4( 0.0 );" % name)
        if self.position_invariant:
            lines.append("\tconst vec4 inPos = vec4( inPosition3, 1.0 );")
            lines.append("\tgl_Position = vec4( dot( inPos, mvpRow[0] ), dot( inPos, mvpRow[1] ), dot( inPos, mvpRow[2] ), dot( inPos, mvpRow[3] ) );")
        if self.stage == "vert":
            for name in self.outputs:
                lines.append("\t%s = vec4( 0.0, 0.0, 0.0, 1.0 );" % name)
        if self.stage == "frag" and "outColor" in self.outputs:
            lines.append("\toutColor = vec4( 0.0, 0.0, 0.0, 1.0 );")
        lines.extend(self.body)
        lines.append("}")
        return "\n".join(lines) + "\n"


def translate_file(text):
    """Returns (vertex GLSL or None, fragment GLSL or None)."""
    vp, fp = split_programs(text)
    return (Translator(vp, "vert").translate() if vp else None,
            Translator(fp, "frag").translate() if fp else None)
