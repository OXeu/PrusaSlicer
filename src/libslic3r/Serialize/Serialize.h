//
// Created by 谢雨 on 25-4-22.
//

#ifndef SERIALIZE_H
#define SERIALIZE_H
#include "libslic3r/Print.hpp"
#include <nlohmann/json.hpp>

namespace Slic3r {
    using json = nlohmann::json;
    class Serialize {
    public:
        static void print_to_json(const Print& print);

        static json print_objects_to_json(const std::vector<PrintObject*> &print_objects);

        static json vec3crd_to_json(Vec3crd* vec);
        static json vec2crd_to_json(Vec2crd* vec);
        static json print_object_to_json(PrintObject* print_object);
    };
} // Slic3r

#endif //SERIALIZE_H
