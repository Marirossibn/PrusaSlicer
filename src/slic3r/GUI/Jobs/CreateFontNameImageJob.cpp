#include "CreateFontNameImageJob.hpp"

#include "libslic3r/Emboss.hpp"
// rasterization of ExPoly
#include "libslic3r/SLA/AGGRaster.hpp"

#include "slic3r/Utils/WxFontUtils.hpp"
#include "slic3r/GUI/3DScene.hpp" // ::glsafe

#include "wx/fontenum.h"

#include <boost/log/trivial.hpp>

using namespace Slic3r;
using namespace Slic3r::GUI;

CreateFontImageJob::CreateFontImageJob(FontImageData &&input)
    : m_input(std::move(input))
{
    assert(!m_input.text.empty());
    assert(wxFontEnumerator::IsValidFacename(m_input.font_name));
    assert(m_input.gray_level > 0 && m_input.gray_level < 255);
    assert(m_input.texture_id != 0);
}

void CreateFontImageJob::process(Ctl &ctl)
{
    if (!wxFontEnumerator::IsValidFacename(m_input.font_name)) return;
    // Select font
    wxFont wx_font(
        wxFontInfo().FaceName(m_input.font_name).Encoding(m_input.encoding));
    if (!wx_font.IsOk()) return;

    std::unique_ptr<Emboss::FontFile> font_file =
        WxFontUtils::create_font_file(wx_font);
    if (font_file == nullptr) return;

    Emboss::FontFileWithCache font_file_with_cache(std::move(font_file));
    FontProp                  fp;
    // use only first line of text
    std::string text = m_input.text;
    size_t enter_pos = text.find('\n');
    if (enter_pos < text.size()) {
        // text start with enter
        if (enter_pos == 0) return;
        // exist enter, soo delete all after enter
        text = text.substr(0, enter_pos);        
    }    

    std::function<bool()> was_canceled = [&ctl, cancel = m_input.cancel]() -> bool {
        if (ctl.was_canceled()) return true;
        if (cancel->load()) return true;
        return false; 
    };
    ExPolygons shapes = Emboss::text2shapes(font_file_with_cache, text.c_str(), fp, was_canceled);
    // normalize height of font
    BoundingBox bounding_box;
    for (ExPolygon &shape : shapes)
        bounding_box.merge(BoundingBox(shape.contour.points));
    if (bounding_box.size().x() < 1 || bounding_box.size().y() < 1) return;
    double       scale = m_input.size.y() / (double) bounding_box.size().y();
    BoundingBoxf bb2(bounding_box.min.cast<double>(),
                     bounding_box.max.cast<double>());
    bb2.scale(scale);
    Vec2d size_f = bb2.size();
    m_tex_size   = Point(std::ceil(size_f.x()), std::ceil(size_f.y()));
    // crop image width
    if (m_tex_size.x() > m_input.size.x()) m_tex_size.x() = m_input.size.x();

    // Set up result
    m_result = std::vector<unsigned char>(m_tex_size.x() * m_tex_size.y() * 4, {255});

    sla::Resolution resolution(m_tex_size.x(), m_tex_size.y());
    double pixel_dim = SCALING_FACTOR / scale;
    sla::PixelDim dim(pixel_dim, pixel_dim);
    double gamma = 1.;
    std::unique_ptr<sla::RasterBase> r =
        sla::create_raster_grayscale_aa(resolution, dim, gamma);
    for (ExPolygon &shape : shapes) shape.translate(-bounding_box.min);
    for (const ExPolygon &shape : shapes) r->draw(shape);
        
    // copy rastered data to pixels
    sla::RasterEncoder encoder =
        [&pix = m_result, w = m_tex_size.x(), h = m_tex_size.y(),
         gray_level = m_input.gray_level]
    (const void *ptr, size_t width, size_t height, size_t num_components) {        
        size_t size {static_cast<size_t>(w*h)};
        const unsigned char *ptr2 = (const unsigned char *) ptr;
        for (size_t x = 0; x < width; ++x)
            for (size_t y = 0; y < height; ++y) { 
                size_t index = y*w + x;
                assert(index < size);
                if (index >= size) continue;
                pix[3+4*index] = ptr2[y * width + x] / gray_level;
            }
        return sla::EncodedRaster();
    };
    r->encode(encoder);
}

// ability to request new frame after finish rendering
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

void CreateFontImageJob::finalize(bool canceled, std::exception_ptr &)
{
    if (m_input.count_opened_font_files)
        --(*m_input.count_opened_font_files);
    if (canceled || m_input.cancel->load()) return;
    
    *m_input.is_created = true;

    // upload texture on GPU
    const GLenum target = GL_TEXTURE_2D;
    glsafe(::glBindTexture(target, m_input.texture_id));

    GLint 
        w = m_tex_size.x(), h = m_tex_size.y(),
        xoffset = m_input.size.x() - m_tex_size.x(), // arrange right
        yoffset = m_input.size.y() * m_input.index;
    glsafe(::glTexSubImage2D(target, m_input.level, xoffset, yoffset, w, h,
                             m_input.format, m_input.type, m_result.data()));

    // bind default texture
    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));

    // show rendered texture
    //wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0);

    BOOST_LOG_TRIVIAL(info) 
        << "Generate Preview font('" << m_input.font_name << "' id:" << m_input.index << ") "
        << "with text: '" << m_input.text << "' "
        << "texture_size " << m_input.size.x() << " x " << m_input.size.y();
}