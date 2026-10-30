#include "rwpng.h"
#include <cassert>
#include <string>
#include <vector>

static std::wstring wide(const char* s) { return std::wstring(s, s + std::char_traits<char>::length(s)); }
int main(int argc, char** argv) {
    assert(argc == 4);
    std::wstring good = wide(argv[1]), bomb = wide(argv[2]), huge = wide(argv[3]);
    progimage_info image;
    assert(readpng(&image, &good[0], LCT_RGBA) == 0);
    assert(image.width == 1 && image.height == 1 && image.image_data.size() == 4);
    image.image_data.clear();
    assert(readpng(&image, &good[0], LCT_RGB) == 0);
    assert(image.image_data.size() == 3);
    image.image_data.clear();
    assert(readpng(&image, &bomb[0], LCT_RGBA) == 109);
    assert(image.width == 0 && image.height == 0);
    assert(readpng(&image, &huge[0], LCT_RGBA) == 92);
    std::vector<unsigned char> encoded;
    assert(lodepng::load_file(encoded, good) == 0);
    lodepng::State state;
    state.decoder.zlibsettings.max_output_size = 4;
    unsigned w, h;
    std::vector<unsigned char> raw;
    assert(lodepng::decode(raw, w, h, state, encoded) == 109);
    for (int interlace = 0; interlace <= 1; interlace++) {
        lodepng::State encoder;
        encoder.info_png.interlace_method = (unsigned)interlace;
        std::vector<unsigned char> pixels(5 * 7 * 4, 127), png, decoded;
        assert(lodepng::encode(png, pixels, 5, 7, encoder) == 0);
        assert(lodepng::decode(decoded, w, h, png) == 0);
        assert(w == 5 && h == 7 && decoded == pixels);
    }
    lodepng::State encoder16;
    encoder16.info_raw.bitdepth = 16;
    encoder16.info_png.interlace_method = 1;
    std::vector<unsigned char> pixels16(5 * 7 * 8), png16;
    for (size_t i = 0; i < pixels16.size(); i++) pixels16[i] = (unsigned char)i;
    assert(lodepng::encode(png16, pixels16, 5, 7, encoder16) == 0);
    std::wstring path16 = good + L".16.png";
    assert(lodepng::save_file(png16, path16) == 0);
    image.image_data.clear();
    assert(readpng(&image, &path16[0], LCT_RGBA) == 0);
    assert(image.width == 5 && image.height == 7 && image.image_data.size() == 5 * 7 * 4);
    for (auto color : { LCT_GREY, LCT_RGB, LCT_PALETTE, LCT_GREY_ALPHA, LCT_RGBA })
    for (unsigned depth : { 1U, 2U, 4U, 8U, 16U })
    for (unsigned interlace : { 0U, 1U }) {
        if ((color == LCT_PALETTE && depth == 16) ||
            (color != LCT_PALETTE && color != LCT_GREY && depth < 8)) continue;
        lodepng::State control;
        control.encoder.auto_convert = 0;
        control.info_png.interlace_method = interlace;
        control.info_raw.colortype = control.info_png.color.colortype = color;
        control.info_raw.bitdepth = control.info_png.color.bitdepth = depth;
        if (color == LCT_PALETTE) {
            assert(lodepng_palette_add(&control.info_raw, 1, 2, 3, 255) == 0);
            assert(lodepng_palette_add(&control.info_png.color, 1, 2, 3, 255) == 0);
        }
        std::vector<unsigned char> source(lodepng_get_raw_size(3, 5, &control.info_raw), 0), file;
        assert(lodepng::encode(file, source, 3, 5, control) == 0);
        assert(lodepng::save_file(file, good) == 0);
        image.image_data.clear();
        assert(readpng(&image, &good[0], LCT_RGBA) == 0);
        assert(image.width == 3 && image.height == 5 && image.image_data.size() == 60);
        assert(image.image_data[0] == (color == LCT_PALETTE ? 1 : 0));
        assert(image.image_data[3] == ((color == LCT_GREY_ALPHA || color == LCT_RGBA) ? 0 : 255));
    }
}
