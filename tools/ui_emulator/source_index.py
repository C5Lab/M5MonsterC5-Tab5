"""Read C syntax without modifying firmware. Requires pinned tree-sitter packages."""
from pathlib import Path
import re
from tree_sitter import Language, Parser
import tree_sitter_c

ROOT = Path(__file__).resolve().parents[2]

def walk(node):
    yield node
    for child in node.named_children:
        yield from walk(child)

class Source:
    def __init__(self, path, *, mask_embedded_js=False):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        parsed=self.data
        if mask_embedded_js:
            # Runtime EM_JS declarations end at a line-ending });. Preserve
            # byte offsets/newlines so evidence still points into original C.
            parsed=re.sub(rb'^EM_JS\(.*?\}\);[ \t]*(?=\r?$)',
                          lambda m:re.sub(rb'[^\r\n]',b' ',m.group()),parsed,
                          flags=re.M|re.S)
        self.tree = Parser(Language(tree_sitter_c.language())).parse(parsed)
        self.functions = {}
        self.types = {}
        for node in walk(self.tree.root_node):
            if node.type == 'function_definition':
                dec = node.child_by_field_name('declarator')
                while dec and dec.type != 'identifier':
                    dec = dec.child_by_field_name('declarator')
                if dec:
                    self.functions[self.text(dec)] = node
            elif node.type == 'type_definition':
                dec = node.child_by_field_name('declarator')
                if dec:
                    self.types[self.text(dec)] = node

    def text(self, node):
        return self.data[node.start_byte:node.end_byte].decode('utf-8')

    def function(self, name):
        return self.text(self.functions[name])

    def span(self, start, end):
        """Extract an exact source span, failing on non-unique anchors."""
        text = self.data.decode('utf-8')
        assert text.count(start) == 1, start
        a = text.index(start)
        b = text.index(end, a)
        return text[a:b]
