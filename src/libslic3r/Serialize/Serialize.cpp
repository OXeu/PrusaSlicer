//
// Created by 谢雨 on 25-4-22.
//

#include "Serialize.h"
#include <fstream>

namespace Slic3r {

    void Serialize::print_to_json(const Print& print) {
        json j;
        j["print"] = print_objects_to_json(print.m_objects);
        std::ofstream f("example.json");
        const std::string serialized_string = j.dump();
        f << serialized_string ;
    }

    json Serialize::print_objects_to_json(const std::vector<PrintObject*> &print_objects) {
        json jj = json::array();
        for (const auto & obj: print_objects) {
            jj.push_back(print_object_to_json(obj));
        }
        return jj;
    }

    json Serialize::vec3crd_to_json(Vec3crd* vec) {
        json j = json::array({vec[0], vec[1], vec[2]});
        return j;
    }

    json Serialize::vec2crd_to_json(Vec2crd* vec) {
        json j = json::array({vec[0], vec[1]});
        return j;
    }

    json Serialize::print_object_to_json(PrintObject *print_object) {
        json jj;
        jj["m_size"] = vec3crd_to_json(&print_object->m_size);
        jj["m_center_offset"] = vec2crd_to_json(&print_object->m_center_offset);
        jj["m_typed_slices"] = print_object->m_typed_slices;
        return jj;
    }


} // Slic3r