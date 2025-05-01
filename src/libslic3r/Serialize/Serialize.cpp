//
// Created by 谢雨 on 25-4-22.
//

#include "Serialize.hpp"
#include <fstream>

#include "libslic3r/Fill/Lightning/Generator.hpp"

namespace Slic3r {
    json Serialize::print_regions_to_json(const PrintRegionPtrs &vector) {
        json j = json::array();
        for (const auto& region: vector) {
            json jj;
            jj["m_config"] = region->m_config.to_json();
            jj["m_config_hash"] = region->m_config_hash;
            jj["m_print_region_id"] = region->m_print_region_id;
            jj["m_print_object_region_id"] = region->m_print_object_region_id;
            jj["m_ref_cnt"] = region->m_ref_cnt;
            j.push_back(jj);
        }
        return j;
    }

    json Serialize::print_statistics_to_json(const Slic3r::PrintStatistics &print_statistics) {
        json j;
        j["normal_print_time_seconds"] = print_statistics.normal_print_time_seconds;
        j["silent_print_time_seconds"] = print_statistics.silent_print_time_seconds;
        j["estimated_normal_print_time"] = print_statistics.estimated_normal_print_time;
        j["estimated_silent_print_time"] = print_statistics.estimated_silent_print_time;
        j["total_used_filament"] = print_statistics.total_used_filament;
        j["total_extruded_volume"] = print_statistics.total_extruded_volume;
        j["total_cost"] = print_statistics.total_cost;
        j["total_toolchanges"] = print_statistics.total_toolchanges;
        j["total_weight"] = print_statistics.total_weight;
        j["total_wipe_tower_cost"] = print_statistics.total_wipe_tower_cost;
        j["total_wipe_tower_filament"] = print_statistics.total_wipe_tower_filament;
        j["total_wipe_tower_filament_weight"] = print_statistics.total_wipe_tower_filament_weight;
        j["printing_extruders"] = print_statistics.printing_extruders;
        j["initial_extruder_id"] = print_statistics.initial_extruder_id;
        j["initial_filament_type"] = print_statistics.initial_filament_type;
        j["printing_filament_types"] = print_statistics.printing_filament_types;
        j["filament_stats"] = print_statistics.filament_stats;
        return j;
    }

    void Serialize::print_to_json(const Print &print) {
        json j;
        j["m_config"] = print.m_config.to_json();
        j["m_default_object_config"] = print.m_default_object_config.to_json();
        j["m_default_region_config"] = print.m_default_region_config.to_json();
        j["m_objects"] = print_objects_to_json(print.m_objects);
        j["m_print_regions"] = print_regions_to_json(print.m_print_regions);
        // m_skirt, m_brim, m_first_layer_convex_hull,m_skirt_convex_hull, m_conflict_result 在 process 中构建，无需序列化
        // m_wipe_tower_data 需要观察
        j["m_print_regions"] = print_statistics_to_json(print.m_print_statistics);
        std::ofstream f("example.json");
        const std::string serialized_string = j.dump();
        f << serialized_string;
    }

    json Serialize::print_object_to_json(PrintObject *print_object) {
        json jj;
        jj["m_size"] = vec3crd_to_json(&print_object->m_size);
        jj["m_config"] = print_object->m_config.to_json();
        jj["m_trafo"] = transform3d_to_json(print_object->m_trafo);
        jj["m_instances"] = print_instances_to_json(print_object->m_instances);
        jj["m_center_offset"] = vec2crd_to_json(print_object->m_center_offset);
        jj["m_shared_regions"] = print_object_regions_to_json(*print_object->m_shared_regions);
        jj["m_slicing_params"] = slicing_parameters_to_json(print_object->m_slicing_params);
        jj["m_layers"] = layers_to_json(print_object->m_layers);
        jj["m_support_layers"] = support_layers_to_json(print_object->m_support_layers);
        jj["m_typed_slices"] = print_object->m_typed_slices;
        // 八叉树在生成 GCode 时填充无需传输
        // jj["m_adaptive_fill_octrees"] = adaptive_fill_octrees_to_json(print_object->m_adaptive_fill_octrees);
        // 生成器也在生成时构造，无需传输
        // jj["m_lightning_generator"] = generators_to_json(print_object->m_lightning_generator.get());
        return jj;
    }

    json Serialize::print_objects_to_json(const std::vector<PrintObject *> &print_objects) {
        json jj = json::array();
        for (const auto &obj: print_objects) {
            jj.push_back(print_object_to_json(obj));
        }
        return jj;
    }

    json Serialize::vec3crd_to_json(const Vec3crd *vec) {
        json j = json::array({vec[0], vec[1], vec[2]});
        return j;
    }

    json Serialize::model_object_to_json(Slic3r::ModelObject *object) {
        nlohmann::json j = nlohmann::json::parse(object->to_json());
        // todo
        return j;
    }

    json Serialize::print_instances_to_json(const std::vector<PrintInstance> &vec) {
        json j = json::array();
        for (const auto &d: vec) {
            json jj;
            jj["shift"] = vec2crd_to_json(d.shift);
            jj["model_instance"] = model_instance_to_json(d.model_instance);
            j.push_back(jj);
        }
        return j;
    }

    json Serialize::model_instance_to_json(const ModelInstance *v) {
        json j;
        j["timestamp"] = v->timestamp();
        j["id"] = v->id().id;
        j["printable"] = v->printable;
        j["print_volume_state"] = v->print_volume_state;
        j["print_volume_state"] = transform3d_to_json(v->m_transformation.get_matrix());
        j["object"] = model_object_to_json(v->object);
        return j;
    }

    json Serialize::transform3d_to_json(const Transform3d &v) {
        double data[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            2, 3, 4, 1
        };
        std::memcpy(data, v.data(), sizeof(double) * 16);

        json j = json::array();
        for (const auto d: data) {
            j.push_back(d);
        }
        return j;
    }

    json Serialize::vec2crd_to_json(const Vec2crd &vec) {
        json j = json::array({vec[0], vec[1]});
        return j;
    }

    json Serialize::print_region_to_json(const PrintRegion *v) {
        json j;
        if (v == nullptr) return nullptr;
        j["m_config"] = v->m_config.to_json();
        j["m_config_hash"] = v->m_config_hash;
        j["m_print_region_id"] = v->m_print_region_id;
        j["m_print_object_region_id"] = v->m_print_object_region_id;
        j["m_ref_cnt"] = v->m_ref_cnt;
        return j;
    }

    json Serialize::bounding_box_to_json(const PrintObjectRegions::BoundingBox &box) {
        json j = {
            {"min", {box.min().x(), box.min().y(), box.min().z()}},
            {"max", {box.max().x(), box.max().y(), box.max().z()}}
        };
        return j;
    }

    json Serialize::layer_ranges_to_json(const PrintObjectRegions::LayerRangeRegions &v) {
        json j;
        json layer_height_range;
        layer_height_range["key"] = v.layer_height_range.first;
        layer_height_range["value"] = v.layer_height_range.second;
        j["layer_height_range"] = layer_height_range;
        if (v.config != nullptr) {
            const auto& options = v.config->options;
            json options_json;
            for (const auto &[key, value]: options) {
                options_json[key] = value->serialize();
            }
            j["config"] = options_json;
        }
        json volumes = json::array();
        for (const auto &[volume_id, bbox]: v.volumes) {
            json jj;
            jj["id"] = volume_id.id;
            jj["bbox"] = bounding_box_to_json(bbox);
            volumes.push_back(jj);
        }
        j["volumes"] = volumes;
        return j;
    }

    json Serialize::print_object_regions_to_json(PrintObjectRegions &v) {
        json j;
        json all_regions = json::array();
        for (const auto &d: v.all_regions) {
            all_regions.push_back(print_region_to_json(d.get()));
        }
        j["all_regions"] = all_regions;

        json layer_ranges = json::array();
        for (const auto &d: v.layer_ranges) {
            layer_ranges.push_back(layer_ranges_to_json(d));
        }
        j["layer_ranges"] = layer_ranges;

        return j;
    }

    json Serialize::slicing_parameters_to_json(SlicingParameters &v) {
        json j;
        j["valid"] = v.valid;
        j["base_raft_layers"] = v.base_raft_layers;
        j["interface_raft_layers"] = v.interface_raft_layers;
        j["base_raft_layer_height"] = v.base_raft_layer_height;
        j["interface_raft_layer_height"] = v.interface_raft_layer_height;
        j["contact_raft_layer_height"] = v.contact_raft_layer_height;
        j["layer_height"] = v.layer_height;
        j["min_layer_height"] = v.min_layer_height;
        j["max_layer_height"] = v.max_layer_height;
        j["max_suport_layer_height"] = v.max_suport_layer_height;
        j["first_print_layer_height"] = v.first_print_layer_height;
        j["first_object_layer_height"] = v.first_object_layer_height;
        j["first_object_layer_bridging"] = v.first_object_layer_bridging;
        j["soluble_interface"] = v.soluble_interface;
        j["gap_raft_object"] = v.gap_raft_object;
        j["gap_object_support"] = v.gap_object_support;
        j["gap_support_object"] = v.gap_support_object;
        j["raft_base_top_z"] = v.raft_base_top_z;
        j["raft_interface_top_z"] = v.raft_interface_top_z;
        j["raft_contact_top_z"] = v.raft_contact_top_z;
        j["object_print_z_min"] = v.object_print_z_min;
        j["object_print_z_max"] = v.object_print_z_max;
        j["object_print_z_uncompensated_max"] = v.object_print_z_uncompensated_max;
        j["object_shrinkage_compensation_z"] = v.object_shrinkage_compensation_z;
        return j;
    }

    json Serialize::layer_to_json(const Layer *layer) {
        json j;
        j["slice_z"] = layer->slice_z;
        j["print_z"] = layer->print_z;
        j["height"] = layer->height;
        j["m_id"] = layer->m_id;
        if (layer->upper_layer != nullptr) {
            j["upper_layer"] = layer->upper_layer->m_id;
        }
        if (layer->lower_layer != nullptr) {
            j["lower_layer"] = layer->lower_layer->m_id;
        }
        return j;
    }

    json Serialize::layers_to_json(const LayerPtrs &v) {
        json jj = json::array();
        for (const auto &layer: v) {
            jj.push_back(layer_to_json(layer));
        }
        return jj;
    }

    json Serialize::points_to_json(const Points &v) {
        json jj = json::array();
        for (const auto &point: v) {
            auto j = vec2crd_to_json(point);
            jj.push_back(j);
        }
        return jj;
    }

    json Serialize::support_layers_to_json(const SupportLayerPtrs &vector) {
        json jj = json::array();
        for (const auto &layer: vector) {
            json j = layer_to_json(layer);
            j["m_interface_id"] = layer->m_interface_id;
            jj.push_back(j);
        }
        return jj;
    }
} // Slic3r
