find . -name "*.h" ! -path "./libs/*" ! -path "./build/*" | xargs clang-format -i -style=file
find . -name "*.cpp" ! -path "./libs/*" ! -path "./build/*" | xargs clang-format -i -style=file
find . -name "*.c" ! -path "./libs/*" ! -path "./build/*" | xargs clang-format -i -style=file
find shading/shaders -name "*.*" ! -path "./libs/*" ! -path "./build/*" | xargs clang-format -i -style=file