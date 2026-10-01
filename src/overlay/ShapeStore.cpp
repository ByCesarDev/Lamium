#include "overlay/ShapeStore.h"
#include "app/AtomicFile.h"
#include <fstream>

namespace lamium::overlay {
std::vector<ShapeDefinition> readShapes(std::filesystem::path const& path) {
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("Could not open shape document");
    std::string text(1024*1024+1,'\0');
    file.read(text.data(),static_cast<std::streamsize>(text.size()));
    if (file.bad()) throw std::runtime_error("Could not read shape document");
    text.resize(static_cast<size_t>(file.gcount()));
    return decodeShapes(text);
}
void writeShapes(std::filesystem::path const& path, std::vector<ShapeDefinition> const& definitions) {
    writeFileReplacing(path, encodeShapes(definitions), "shapes");
}
}
