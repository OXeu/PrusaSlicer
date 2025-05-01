//
// Created by 谢雨 on 25-4-22.
//

#ifndef SERIALIZE_H
#define SERIALIZE_H
#include "../Print.hpp"
#include <nlohmann/json.hpp>
#include "libslic3r/Fill/Lightning/Layer.hpp"

namespace Slic3r {
    using json = nlohmann::json;
    class Serialize {
    public:
        static json print_regions_to_json(const PrintRegionPtrs & vector);

        static json print_statistics_to_json(const Slic3r::PrintStatistics & print_statistics);

        static void print_to_json(const Print& print);
        static json print_objects_to_json(const std::vector<PrintObject*> &print_objects);
        static json transform3d_to_json(const Transform3d& v);
        static json vec3crd_to_json(const Vec3crd* vec);

        static json model_object_to_json(Slic3r::ModelObject * object);

        static json model_instance_to_json(const ModelInstance* v);
        static json print_instances_to_json(const std::vector<PrintInstance>& vec);
        static json vec2crd_to_json(const Vec2crd& vec);
        static json print_region_to_json(const PrintRegion * v);

        static json bounding_box_to_json(const PrintObjectRegions::BoundingBox& box);

        static json layer_ranges_to_json(const PrintObjectRegions::LayerRangeRegions & v);

        static json print_object_regions_to_json(PrintObjectRegions& v);
        static json slicing_parameters_to_json(SlicingParameters& v);
        static json layer_to_json(const Layer* layer);
        static json layers_to_json(const LayerPtrs & v);
        static json points_to_json(const Points & v);
        static json support_layers_to_json(const SupportLayerPtrs & vector);
        static json print_object_to_json(PrintObject* print_object);
    };
} // Slic3r

#endif //SERIALIZE_H
