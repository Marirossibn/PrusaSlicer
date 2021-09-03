#ifndef slic3r_Emboss_hpp_
#define slic3r_Emboss_hpp_

#include <vector>
#include <set>
#include <optional>
#include <memory>
#include <admesh/stl.h> // indexed_triangle_set
#include "Polygon.hpp"

namespace Slic3r {

/// <summary>
/// class with only static function add ability to engraved OR raised
/// text OR polygons onto model surface
/// </summary>
class Emboss
{
public:
    Emboss() = delete;

    /// <summary>
    /// Collect fonts registred inside OS
    /// </summary>
    static void get_font_list();

    /// <summary>
    /// OS dependent function to get location of font by its name
    /// </summary>
    /// <param name="font_face_name">Unique identificator for font</param>
    /// <returns>File path to font when found</returns>
    static std::optional<std::wstring> get_font_path(const std::wstring &font_face_name);

    /// <summary>
    /// keep information from file about font
    /// </summary>
    struct Font
    {
        // loaded data from font file
        std::vector<unsigned char> buffer;

        unsigned int index=0; // index of actual file info in collection
        unsigned int count=0; // count of fonts in file collection

        // vertical position is "scale*(ascent - descent + lineGap)"
        int ascent=0, descent=0, linegap=0;
        // user defined unscaled char space
        int extra_char_space = 0;

        // TODO: add enum class Align: center/left/right
    };

    /// <summary>
    /// Load font file into buffer
    /// </summary>
    /// <param name="file_path">Location of .ttf or .ttc font file</param>
    /// <returns>Font object when loaded.</returns>
    static std::optional<Font> load_font(const char *file_path);

    /// <summary>
    /// convert letter into polygons
    /// </summary>
    /// <param name="font">Define fonts</param>
    /// <param name="letter">One character to convert</param>
    /// <param name="flatness">Precision of lettter outline curve in conversion to lines</param>
    /// <returns>inner polygon ccw(outer cw)</returns>
    static Polygons letter2polygons(const Font &font, char letter, float flatness);

    /// <summary>
    /// Convert text into polygons
    /// </summary>
    /// <param name="font">Define fonts</param>
    /// <param name="text">Characters to convert</param>
    /// <param name="flatness">Precision of lettter outline curve in conversion to lines</param>
    /// <returns>Inner polygon ccw(outer cw)</returns>
    static Polygons text2polygons(const Font &font, const char *text, float flatness);

    /// <summary>
    /// Project 2d point into space
    /// Could be plane, sphere, cylindric, ...
    /// </summary>
    class IProject
    {
    public:
        virtual ~IProject() = default;
        /// <summary>
        /// convert 2d point to 3d point
        /// </summary>
        /// <param name="p">2d coordinate</param>
        /// <returns>
        /// first - front spatial point
        /// second - back spatial point
        /// </returns>
        virtual std::pair<Vec3f, Vec3f> project(const Point &p) const = 0;
    };

    /// <summary>
    /// Create triangle model for text
    /// </summary>
    /// <param name="shape2d">text or image</param>
    /// <param name="projection">Define transformation from 2d to 3d(orientation, position, scale, ...)</param>
    /// <returns>Projected shape into space</returns>
    static indexed_triangle_set polygons2model(const Polygons &shape2d, const IProject& projection);

    // define oriented connection of 2 vertices(defined by its index)
    using HalfEdge = std::pair<uint32_t, uint32_t>;
    using HalfEdges = std::set<HalfEdge>;
    using Indices = std::vector<Vec3i>;
    /// <summary>
    /// Connect points by triangulation to create filled surface by triangle indices
    /// </summary>
    /// <param name="points">Points to connect</param>
    /// <param name="edges">Constraint for edges, pair is from point(first) to point(second)</param>
    /// <returns>Triangles</returns>
    static Indices triangulate(const Points &points, const HalfEdges &half_edges);
    static Indices triangulate(const Polygon &polygon);
    static Indices triangulate(const Polygons &polygons);

    /// <summary>
    /// Filter out triagles without both side edge or inside half edges
    /// Main purpose: Filter out triangles which lay outside of ExPolygon given to triangulation
    /// </summary>
    /// <param name="indices">Triangles</param>
    /// <param name="half_edges">Only outer edges</param>
    static void remove_outer(Indices &indices, const HalfEdges &half_edges);

    class ProjectZ : public IProject
    {
    public:
        ProjectZ(float depth) : m_depth(depth) {}
        // Inherited via IProject
        virtual std::pair<Vec3f, Vec3f> project(const Point &p) const override;
        float m_depth;
    };

    class ProjectScale : public IProject
    {
        std::unique_ptr<IProject> core;
    public:
        ProjectScale(std::unique_ptr<IProject> core, float scale)
            : m_scale(scale)
            , core(std::move(core))
        {}

        // Inherited via IProject
        virtual std::pair<Vec3f, Vec3f> project(const Point &p) const override
        {
            auto res = core->project(p);
            return std::make_pair(res.first * m_scale, res.second * m_scale);
        }

        float                           m_scale;
    };
};

} // namespace Slic3r
#endif // slic3r_Emboss_hpp_
