#ifndef libslic3r_SeamPerimeters_hpp_
#define libslic3r_SeamPerimeters_hpp_

#include <tcbspan/span.hpp>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "libslic3r/GCode/SeamPainting.hpp"
#include "libslic3r/KDTreeIndirect.hpp"
#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/GCode/SeamGeometry.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {
    class Layer;
}

namespace Slic3r::Seams::ModelInfo {
class Painting;
}

namespace Slic3r::Seams::Perimeters {
enum class AngleType { convex, concave, smooth };
enum class PointType { enforcer, blocker, common };
enum class PointClassification { overhang, embedded, common };
struct Perimeter;
struct PerimeterParams;

/**
 * When previous_index == next_index, the point is at the point.
 * Otherwise the point is at the edge.
 */
struct PointOnPerimeter
{
    std::size_t previous_index{};
    std::size_t next_index{};
    Vec2d position{Vec2d::Zero()};
};

struct LayerInfo
{
    static LayerInfo create(
        const Slic3r::Layer &object_layer, std::size_t index, const double elephant_foot_compensation
    );

    AABBTreeLines::LinesDistancer<Linef> distancer;
    std::optional<AABBTreeLines::LinesDistancer<Linef>> previous_distancer;
    std::size_t index;
    double height{};
    double slice_z{};
    double elephant_foot_compensation;
};

using LayerInfos = std::vector<LayerInfo>;

/**
 * @brief Construct LayerInfo for each of the provided layers.
 */
LayerInfos get_layer_infos(
    tcb::span<std::shared_ptr<Layer>> object_layers, const double elephant_foot_compensation
);

struct PerimeterPoint {
    Vec2d position{Vec2d::Zero()};
    double angle{};
    PointType type{PointType::common};
    PointClassification classification{PointClassification::common};
    AngleType angle_type{AngleType::smooth};
};

using PerimeterPoints = std::vector<PerimeterPoint>;


} // namespace Slic3r::Seams::Perimeters

namespace Slic3r::Seams::Perimeters::Impl {


/**
 * @brief Split edges between points into multiple points if there is a painted point anywhere on
 * the edge.
 *
 * The edge will be split by points no more than max_distance apart.
 * Smaller max_distance -> more points.
 *
 * @return All the points (original and added) in order along the edges.
 */
PerimeterPoints oversample_painted(
    PerimeterPoints &points,
    const std::function<bool(Vec3f, double)> &is_painted,
    const double slice_z,
    const double max_distance
);

/**
 * @brief Call Duglas-Peucker for consecutive points of the same type.
 *
 * It never removes the first point and last point.
 *
 * @param tolerance Douglas-Peucker epsilon.
 */
PerimeterPoints remove_redundant_points(
    const PerimeterPoints &points,
    const double tolerance
);

} // namespace Slic3r::Seams::Perimeters::Impl

namespace Slic3r::Seams::Perimeters {

struct PerimeterParams
{
    double elephant_foot_compensation{};
    double oversampling_max_distance{};
    double embedding_threshold{};
    double overhang_threshold{};
    double convex_threshold{};
    double concave_threshold{};
    double painting_radius{};
    double simplification_epsilon{};
    double smooth_angle_arm_length{};
    double sharp_angle_arm_length{};
};

struct Perimeter
{
    struct IndexToCoord
    {
        IndexToCoord(const tcb::span<const Vec2d> positions): positions(positions) {}
        IndexToCoord() = default;
        double operator()(const size_t index, size_t dim) const;

        tcb::span<const Vec2d> positions;
    };

    using PointTree = KDTreeIndirect<2, double, IndexToCoord>;
    using OptionalPointTree = std::optional<PointTree>;

    struct PointTrees
    {
        OptionalPointTree embedded_points;
        OptionalPointTree common_points;
        OptionalPointTree overhanging_points;
    };

    Perimeter() = default;

    Perimeter(
        const double slice_z,
        const std::size_t layer_index,
        const bool is_hole,
        std::vector<Vec2d> &&positions,
        std::vector<double> &&angles,
        std::vector<PointType> &&point_types,
        std::vector<PointClassification> &&point_classifications,
        std::vector<AngleType> &&angle_types
    );

    static Perimeter create(
        const Polygon &polygon,
        const Geometry::Overhangs &overhangs,
        const ModelInfo::Painting &painting,
        const LayerInfo &layer_info,
        const PerimeterParams &params
    );

    static Perimeter create_degenerate(
        std::vector<Vec2d> &&points, const double slice_z, const std::size_t layer_index
    );

    bool is_degenerate{false};
    double slice_z{};
    bool is_hole{false};
    std::size_t layer_index{};
    std::vector<Vec2d> positions{};
    std::vector<double> angles{};
    IndexToCoord index_to_coord{};
    std::vector<PointType> point_types{};
    std::vector<PointClassification> point_classifications{};
    std::vector<AngleType> angle_types{};

    PointTrees enforced_points{};
    PointTrees common_points{};
    PointTrees blocked_points{};
};

using Perimeters = std::vector<Perimeter>;

struct BoundedPerimeter {
    Perimeter perimeter;
    BoundingBox bounding_box;
};

using BoundedPerimeters = std::vector<BoundedPerimeter>;
using LayerPerimeters = std::vector<BoundedPerimeters>;

LayerPerimeters create_perimeters(
    const std::vector<Geometry::BoundedPolygons> &polygons,
    const std::vector<LayerInfo> &layer_infos,
    const ModelInfo::Painting &painting,
    const PerimeterParams &params
);

inline std::vector<Vec2d> extract_points(
    const Perimeter &perimeter, const PointType point_type
) {
    std::vector<Vec2d> result;
    for (std::size_t i{0}; i < perimeter.positions.size(); ++i) {
        if (perimeter.point_types[i] == point_type) {
            result.push_back(perimeter.positions[i]);
        }
    }
    return result;
}


using Seams::Geometry::PointOnLine;

std::optional<PointOnPerimeter> offset_along_perimeter(
    const PointOnPerimeter &point,
    const Perimeter& perimeter,
    const double offset,
    const Seams::Geometry::Direction1D direction,
    const std::function<bool(const Perimeter&, const std::size_t)> &early_stop_condition
);

unsigned get_point_value(const PointType point_type, const PointClassification point_classification);

} // namespace Slic3r::Seams::Perimeters

#endif // libslic3r_SeamPerimeters_hpp_
