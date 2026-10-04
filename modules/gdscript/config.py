def can_build(env, platform):
    env.module_add_dependencies("gdscript", ["jsonrpc", "websocket"], True)
    return True


def get_opts(platform):
    from SCons.Variables import BoolVariable

    return [
        BoolVariable("gdscript_compiler", "Include GDScript source/token compilation (required by the editor)", True)
    ]


def configure(env):
    if not env["gdscript_compiler"]:
        if env.editor_build:
            raise ValueError(
                "The editor requires gdscript_compiler=yes; use a runtime template for compiled-only GDScript."
            )
        env.Append(CPPDEFINES=["GDSCRIPT_NO_COMPILER"])


def get_doc_classes():
    return [
        "@GDScript",
        "GDScript",
        "GDScriptLanguageProtocol",
        "GDScriptSyntaxHighlighter",
        "GDScriptTextDocument",
        "GDScriptWorkspace",
    ]


def get_doc_path():
    return "doc_classes"
