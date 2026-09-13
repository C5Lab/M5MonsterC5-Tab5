FROM emscripten/emsdk:4.0.14@sha256:11d144844086982d68260867ce1fc665667f4139b73326625033ed8d16533406
RUN python3 -m pip install --no-cache-dir tree-sitter==0.25.2 tree-sitter-c==0.24.1 jsonschema==4.25.1
WORKDIR /repo
ENTRYPOINT ["bash", "/repo/tools/ui_emulator/build.sh", "--local"]
