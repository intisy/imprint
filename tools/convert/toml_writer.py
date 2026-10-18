import re

BARE_KEY = re.compile(r"^[A-Za-z0-9_-]+$")
INLINE_LIMIT = 80
ESCAPES = {'"': '\\"', "\\": "\\\\", "\n": "\\n", "\t": "\\t", "\r": "\\r", "\b": "\\b", "\f": "\\f"}


def dumps(table):
    lines = []
    emit_table(lines, (), table, None)
    return "\n".join(lines) + "\n"


def is_table_array(value):
    return isinstance(value, list) and len(value) > 0 and all(isinstance(item, dict) for item in value)


def emit_table(lines, path, table, header):
    if header is not None:
        if lines:
            lines.append("")
        lines.append(header)
    for key, value in table.items():
        if not isinstance(value, dict) and not is_table_array(value):
            lines.append(f"{format_key(key)} = {format_value(value)}")
    for key, value in table.items():
        if isinstance(value, dict):
            child_path = path + (key,)
            emit_table(lines, child_path, value, "[" + format_path(child_path) + "]")
    for key, value in table.items():
        if is_table_array(value):
            child_path = path + (key,)
            for item in value:
                emit_table(lines, child_path, item, "[[" + format_path(child_path) + "]]")


def format_value(value):
    # bool before int: True is an int in Python.
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, str):
        return format_string(value)
    if isinstance(value, list):
        return format_list(value)
    if value is None:
        raise ValueError("TOML has no null; drop the key or give it a value")
    raise ValueError(f"cannot write {type(value).__name__} as TOML")


def format_list(values):
    if any(isinstance(item, dict) for item in values):
        raise ValueError("a list mixes tables and values")
    items = [format_value(item) for item in values]
    inline = "[" + ", ".join(items) + "]"
    if len(inline) <= INLINE_LIMIT:
        return inline
    return "[\n" + "".join(f"    {item},\n" for item in items) + "]"


def format_string(text):
    escaped = []
    for character in text:
        if character in ESCAPES:
            escaped.append(ESCAPES[character])
        elif ord(character) < 0x20 or ord(character) == 0x7F:
            escaped.append(f"\\u{ord(character):04X}")
        else:
            escaped.append(character)
    return '"' + "".join(escaped) + '"'


def format_key(key):
    return key if BARE_KEY.match(key) else format_string(key)


def format_path(path):
    return ".".join(format_key(key) for key in path)
