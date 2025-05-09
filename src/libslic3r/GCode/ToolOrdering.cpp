///|/ Copyright (c) Prusa Research 2017 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Model.hpp"
#include "tcbspan/span.hpp"

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
#endif

#include <boost/log/trivial.hpp>
#include <libslic3r/libslic3r.h>
#include <cassert>
#include <limits>
#include <cmath>
#include <cstring>

namespace Slic3r {


// Returns true in case that extruder a comes before b (b does not have to be present). False otherwise.
bool LayerTools::is_extruder_order(unsigned int a, unsigned int b) const
{
    if (a == b)
        return false;

    for (auto extruder : extruders) {
        if (extruder == a)
            return true;
        if (extruder == b)
            return false;
    }

    return false;
}

// Return a zero based extruder from the region, or extruder_override if overriden.
unsigned int LayerTools::perimeter_extruder(const PrintRegion &region) const
{
	assert(region.config().perimeter_extruder.value > 0);
	return ((this->extruder_override == 0) ? region.config().perimeter_extruder.value : this->extruder_override) - 1;
}

unsigned int LayerTools::infill_extruder(const PrintRegion &region) const
{
	assert(region.config().infill_extruder.value > 0);
	return ((this->extruder_override == 0) ? region.config().infill_extruder.value : this->extruder_override) - 1;
}

unsigned int LayerTools::solid_infill_extruder(const PrintRegion &region) const
{
	assert(region.config().solid_infill_extruder.value > 0);
	return ((this->extruder_override == 0) ? region.config().solid_infill_extruder.value : this->extruder_override) - 1;
}

// Returns a zero based extruder this eec should be printed with, according to PrintRegion config or extruder_override if overriden.
unsigned int LayerTools::extruder(const ExtrusionEntityCollection &extrusions, const PrintRegion &region) const
{
	assert(region.config().perimeter_extruder.value > 0);
	assert(region.config().infill_extruder.value > 0);
	assert(region.config().solid_infill_extruder.value > 0);
	// 1 based extruder ID.
	unsigned int extruder = this->extruder_override == 0 ?
	    (extrusions.role().is_infill() ?
	    	(extrusions.entities.front()->role().is_solid_infill() ? region.config().solid_infill_extruder : region.config().infill_extruder) :
			region.config().perimeter_extruder.value) :
		this->extruder_override;
	return (extruder == 0) ? 0 : extruder - 1;
}

static double calc_max_layer_height(const PrintConfig &config, double max_object_layer_height)
{
    double max_layer_height = std::numeric_limits<double>::max();
    for (size_t i = 0; i < config.nozzle_diameter.values.size(); ++ i) {
        double mlh = config.max_layer_height.get_at(i);
        if (mlh == 0.)
            mlh = 0.75 * config.nozzle_diameter.values[i];
        max_layer_height = std::min(max_layer_height, mlh);
    }
    // The Prusa3D Fast (0.35mm layer height) print profile sets a higher layer height than what is normally allowed
    // by the nozzle. This is a hack and it works by increasing extrusion width. See GH #3919.
    return std::max(max_layer_height, max_object_layer_height);
}

// For the use case when each object is printed separately
// (print.config().complete_objects is true).
ToolOrdering::ToolOrdering(const PrintObject &object, unsigned int first_extruder, bool prime_multi_material)
{
    if (object.layers().empty())
        return;

    // Initialize the print layers for just a single object.
    {
        std::vector<coordf_t> zs;
        zs.reserve(zs.size() + object.layers().size() + object.support_layers().size());
        for (auto layer : object.layers())
            zs.emplace_back(layer->print_z);
        for (auto layer : object.support_layers())
            zs.emplace_back(layer->print_z);
        this->initialize_layers(zs);
    }
    double max_layer_height = calc_max_layer_height(object.print()->config(), object.config().layer_height);

    // Collect extruders required to print the layers.
    this->collect_extruders(object, std::vector<std::pair<double, unsigned int>>(), std::vector<std::pair<double, unsigned int>>());

    // Reorder the extruders to minimize tool switches.
    this->reorder_extruders(first_extruder);

    this->fill_wipe_tower_partitions(object.print()->config(), object.layers().front()->print_z - object.layers().front()->height, max_layer_height);

    this->collect_extruder_statistics(prime_multi_material);

    this->mark_skirt_layers(object.print()->config(), max_layer_height);
}

// For the use case when all objects are printed at once.
// (print.config().complete_objects is false).
ToolOrdering::ToolOrdering(const Print &print, unsigned int first_extruder, bool prime_multi_material)
{
    m_print_config_ptr = &print.config();

    // Initialize the print layers for all objects and all layers.
    coordf_t object_bottom_z = 0.;
    coordf_t max_layer_height = 0.;
    {
        std::vector<coordf_t> zs;
        for (auto object : print.objects()) {
            zs.reserve(zs.size() + object->layers().size() + object->support_layers().size());
            for (auto layer : object->layers())
                zs.emplace_back(layer->print_z);
            for (auto layer : object->support_layers())
                zs.emplace_back(layer->print_z);

            // Find first object layer that is not empty and save its print_z
            for (const auto & layer : object->layers())
                if (layer->has_extrusions()) {
                    object_bottom_z = layer->print_z - layer->height;
                    break;
                }

            max_layer_height = std::max(max_layer_height, object->config().layer_height.value);
        }
        this->initialize_layers(zs);
    }
    max_layer_height = calc_max_layer_height(print.config(), max_layer_height);

	// Use the extruder switches from Model::custom_gcode_per_print_z to override the extruder to print the object.
	// Do it only if all the objects were configured to be printed with a single extruder.
	std::vector<std::pair<double, unsigned int>> per_layer_extruder_switches;
    auto num_extruders = unsigned(print.config().nozzle_diameter.size());
	if (num_extruders > 1 && print.object_extruders().size() == 1 && // the current Print's configuration is CustomGCode::MultiAsSingle
		print.model().custom_gcode_per_print_z().mode == CustomGCode::MultiAsSingle) {
		// Printing a single extruder platter on a printer with more than 1 extruder (or single-extruder multi-material).
		// There may be custom per-layer tool changes available at the model.
		per_layer_extruder_switches = custom_tool_changes(print.model().custom_gcode_per_print_z(), num_extruders);
	}

    // Color changes for each layer to determine which extruder needs to be picked before color change.
    // This is done just for multi-extruder printers without enabled Single Extruder Multi Material (tool changer printers).
    std::vector<std::pair<double, unsigned int>> per_layer_color_changes;
    if (num_extruders > 1 && print.model().custom_gcode_per_print_z().mode == CustomGCode::MultiExtruder && !print.config().single_extruder_multi_material) {
        per_layer_color_changes = custom_color_changes(print.model().custom_gcode_per_print_z(), num_extruders);
    }

    // Collect extruders required to print the layers.
    for (auto object : print.objects())
        this->collect_extruders(*object, per_layer_extruder_switches, per_layer_color_changes);

    // Reorder the extruders to minimize tool switches.
    this->reorder_extruders(first_extruder);

    this->fill_wipe_tower_partitions(print.config(), object_bottom_z, max_layer_height);

    if (this->insert_wipe_tower_extruder()) {
        // Now convert the 0-based list to 1-based again, because that is what reorder_extruder expects.
        for (LayerTools& lt : m_layer_tools) {
            for (auto& extruder : lt.extruders)
                    ++extruder;
        }
        this->reorder_extruders(first_extruder);
        this->fill_wipe_tower_partitions(print.config(), object_bottom_z, max_layer_height);
    }

    this->collect_extruder_statistics(prime_multi_material);

    this->mark_skirt_layers(print.config(), max_layer_height);
}

void ToolOrdering::initialize_layers(std::vector<coordf_t> &zs)
{
    sort_remove_duplicates(zs);
    // Merge numerically very close Z values.
    for (size_t i = 0; i < zs.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        coordf_t zmax = zs[i] + EPSILON;
        for (; j < zs.size() && zs[j] <= zmax; ++ j) ;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        m_layer_tools.emplace_back(LayerTools(0.5 * (zs[i] + zs[j-1])));
        i = j;
    }
}

// Decides whether this entity could be overridden
[[nodiscard]] static bool is_overriddable(const ExtrusionEntityCollection& eec, const LayerTools& lt, const PrintConfig& print_config, const PrintObject& object, const PrintRegion& region)
{
    if (print_config.filament_soluble.get_at(lt.extruder(eec, region)))
        return false;

    if (object.config().wipe_into_objects)
        return true;

    if (!region.config().wipe_into_infill || eec.role() != ExtrusionRole::InternalInfill)
        return false;

    return true;
}

// Collect extruders reuqired to print layers.
void ToolOrdering::collect_extruders(
    const PrintObject                                  &object,
    const std::vector<std::pair<double, unsigned int>> &per_layer_extruder_switches,
    const std::vector<std::pair<double, unsigned int>> &per_layer_color_changes
) {
    // Collect the support extruders.
    for (auto support_layer : object.support_layers()) {
        LayerTools   &layer_tools = this->tools_for_layer(support_layer->print_z);
        ExtrusionRole role = support_layer->support_fills.role();
        bool         has_support        = role == ExtrusionRole::Mixed || role == ExtrusionRole::SupportMaterial;
        bool         has_interface      = role == ExtrusionRole::Mixed || role == ExtrusionRole::SupportMaterialInterface;
        unsigned int extruder_support   = object.config().support_material_extruder.value;
        unsigned int extruder_interface = object.config().support_material_interface_extruder.value;
        if (has_support)
            layer_tools.extruders.push_back(extruder_support);
        if (has_interface)
            layer_tools.extruders.push_back(extruder_interface);
        if (has_support || has_interface)
            layer_tools.has_support = true;
    }

    // Extruder overrides are ordered by print_z.
    std::vector<std::pair<double, unsigned int>>::const_iterator it_per_layer_extruder_override = per_layer_extruder_switches.begin();
    unsigned int extruder_override = 0;

    std::vector<std::pair<double, unsigned int>>::const_iterator it_per_layer_color_changes = per_layer_color_changes.begin();

    // Collect the object extruders.
    for (auto layer : object.layers()) {
        LayerTools &layer_tools = this->tools_for_layer(layer->print_z);

        // Override extruder with the next 
    	for (; it_per_layer_extruder_override != per_layer_extruder_switches.end() && it_per_layer_extruder_override->first < layer->print_z + EPSILON; ++ it_per_layer_extruder_override)
    		extruder_override = (int)it_per_layer_extruder_override->second;

        // Store the current extruder override (set to zero if no overriden), so that layer_tools.wiping_extrusions().is_overridable_and_mark() will use it.
        layer_tools.extruder_override = extruder_override;

        // Append the extruder needed to be picked before performing the color change.
        for (; it_per_layer_color_changes != per_layer_color_changes.end() && it_per_layer_color_changes->first < layer->print_z + EPSILON; ++it_per_layer_color_changes) {
            if (std::abs(it_per_layer_color_changes->first - layer->print_z) < EPSILON) {
                assert(layer_tools.extruder_needed_for_color_changer == 0); // Just on color change per layer is allowed.
                layer_tools.extruder_needed_for_color_changer = it_per_layer_color_changes->second;
                layer_tools.extruders.emplace_back(it_per_layer_color_changes->second);
            }
        }

        // What extruders are required to print this object layer?
        for (const LayerRegion *layerm : layer->regions()) {
            const PrintRegion &region = layerm->region();

            if (! layerm->perimeters().empty()) {
                bool something_nonoverriddable = true;

                if (m_print_config_ptr) { // in this case complete_objects is false (see ToolOrdering constructors)
                    something_nonoverriddable = false;
                    for (const ExtrusionEntity *eec : layerm->perimeters()) // let's check if there are nonoverriddable entities
                        if (is_overriddable(dynamic_cast<const ExtrusionEntityCollection&>(*eec), layer_tools, *m_print_config_ptr, object, region))
                            layer_tools.wiping_extrusions_nonconst().set_something_overridable();
                        else
                            something_nonoverriddable = true;
                }

                if (something_nonoverriddable)
               		layer_tools.extruders.emplace_back(extruder_override == 0 ? region.config().perimeter_extruder.value : extruder_override);

                layer_tools.has_object = true;
            }

            bool has_infill       = false;
            bool has_solid_infill = false;
            bool something_nonoverriddable = false;
            for (const ExtrusionEntity *ee : layerm->fills()) {
                // fill represents infill extrusions of a single island.
                const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                ExtrusionRole role = fill->entities.empty() ? ExtrusionRole::None : fill->entities.front()->role();
                if (role.is_solid_infill())
                    has_solid_infill = true;
                else if (role != ExtrusionRole::None)
                    has_infill = true;

                if (m_print_config_ptr) {
                    if (is_overriddable(*fill, layer_tools, *m_print_config_ptr, object, region))
                        layer_tools.wiping_extrusions_nonconst().set_something_overridable();
                    else
                        something_nonoverriddable = true;
                }
            }

            if (something_nonoverriddable || !m_print_config_ptr) {
            	if (extruder_override == 0) {
	                if (has_solid_infill)
	                    layer_tools.extruders.emplace_back(region.config().solid_infill_extruder);
	                if (has_infill)
	                    layer_tools.extruders.emplace_back(region.config().infill_extruder);
            	} else if (has_solid_infill || has_infill)
            		layer_tools.extruders.emplace_back(extruder_override);
            }
            if (has_solid_infill || has_infill)
                layer_tools.has_object = true;
        }
    }

    for (auto& layer : m_layer_tools) {
        // Sort and remove duplicates
        sort_remove_duplicates(layer.extruders);

        // make sure that there are some tools for each object layer (e.g. tall wiping object will result in empty extruders vector)
        if (layer.extruders.empty() && layer.has_object)
            layer.extruders.emplace_back(0); // 0="dontcare" extruder - it will be taken care of in reorder_extruders
    }
}

// Reorder extruders to minimize layer changes.
void ToolOrdering::reorder_extruders(unsigned int last_extruder_id)
{
    if (m_layer_tools.empty())
        return;

    if (last_extruder_id == (unsigned int)-1) {
        // The initial print extruder has not been decided yet.
        // Initialize the last_extruder_id with the first non-zero extruder id used for the print.
        last_extruder_id = 0;
        for (size_t i = 0; i < m_layer_tools.size() && last_extruder_id == 0; ++ i) {
            const LayerTools &lt = m_layer_tools[i];
            for (unsigned int extruder_id : lt.extruders)
                if (extruder_id > 0) {
                    last_extruder_id = extruder_id;
                    break;
                }
        }
        if (last_extruder_id == 0)
            // Nothing to extrude.
            return;
    } else
        // 1 based index
        ++ last_extruder_id;

    for (LayerTools &lt : m_layer_tools) {
        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++ i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }

            // On first layer with wipe tower, prefer a soluble extruder
            // at the beginning, so it is not wiped on the first layer.
            if (lt == m_layer_tools[0] && m_print_config_ptr && m_print_config_ptr->wipe_tower) {
                for (size_t i = 0; i<lt.extruders.size(); ++i)
                    if (m_print_config_ptr->filament_soluble.get_at(lt.extruders[i]-1)) { // 1-based...
                        std::swap(lt.extruders[i], lt.extruders.front());
                        break;
                    }
            } else if (lt.extruder_needed_for_color_changer != 0) {
                // Put the extruder needed for performing the color change at the beginning.
                auto it = std::find(lt.extruders.begin(), lt.extruders.end(), lt.extruder_needed_for_color_changer);
                assert(it != lt.extruders.end());
                std::rotate(lt.extruders.begin(), it, it + 1);
            }
        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools &lt : m_layer_tools)
        for (unsigned int &extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            -- extruder_id;
        }    
}

void ToolOrdering::fill_wipe_tower_partitions(const PrintConfig &config, coordf_t object_bottom_z, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    // Count the minimum number of tool changes per layer.
    size_t last_extruder = size_t(-1);
    for (LayerTools &lt : m_layer_tools) {
        lt.wipe_tower_partitions = lt.extruders.size();
        if (! lt.extruders.empty()) {
            if (last_extruder == size_t(-1) || last_extruder == lt.extruders.front())
                // The first extruder on this layer is equal to the current one, no need to do an initial tool change.
                -- lt.wipe_tower_partitions;
            last_extruder = lt.extruders.back();
        }
    }

    // Propagate the wipe tower partitions down to support the upper partitions by the lower partitions.
    for (int i = int(m_layer_tools.size()) - 2; i >= 0; -- i)
        m_layer_tools[i].wipe_tower_partitions = std::max(m_layer_tools[i + 1].wipe_tower_partitions, m_layer_tools[i].wipe_tower_partitions);

    //FIXME this is a hack to get the ball rolling.
    for (LayerTools &lt : m_layer_tools)
        lt.has_wipe_tower = (lt.has_object && lt.wipe_tower_partitions > 0) || lt.print_z < object_bottom_z + EPSILON;

    // Test for a raft, insert additional wipe tower layer to fill in the raft separation gap.
    for (size_t i = 0; i + 1 < m_layer_tools.size(); ++ i) {
        const LayerTools &lt      = m_layer_tools[i];
        const LayerTools &lt_next = m_layer_tools[i + 1];
        if (lt.print_z < object_bottom_z + EPSILON && lt_next.print_z >= object_bottom_z + EPSILON) {
            // lt is the last raft layer. Find the 1st object layer.
            size_t j = i + 1;
            for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_wipe_tower; ++ j);
            if (j < m_layer_tools.size()) {
                const LayerTools &lt_object = m_layer_tools[j];
                coordf_t gap = lt_object.print_z - lt.print_z;
                assert(gap > 0.f);
                if (gap > max_layer_height + EPSILON) {
                    // Insert one additional wipe tower layer between lh.print_z and lt_object.print_z.
                    LayerTools lt_new(0.5f * (lt.print_z + lt_object.print_z));
                    // Find the 1st layer above lt_new.
                    for (j = i + 1; j < m_layer_tools.size() && m_layer_tools[j].print_z < lt_new.print_z - EPSILON; ++ j);
                    if (std::abs(m_layer_tools[j].print_z - lt_new.print_z) < EPSILON) {
						m_layer_tools[j].has_wipe_tower = true;
					} else {
						LayerTools &lt_extra = *m_layer_tools.insert(m_layer_tools.begin() + j, lt_new);
                        //LayerTools &lt_prev  = m_layer_tools[j];
                        LayerTools &lt_next  = m_layer_tools[j + 1];
                        assert(! m_layer_tools[j - 1].extruders.empty() && ! lt_next.extruders.empty());
                        // FIXME: Following assert tripped when running combine_infill.t. I decided to comment it out for now.
                        // If it is a bug, it's likely not critical, because this code is unchanged for a long time. It might
                        // still be worth looking into it more and decide if it is a bug or an obsolete assert.
                        //assert(lt_prev.extruders.back() == lt_next.extruders.front());
                        lt_extra.has_wipe_tower = true;
                        lt_extra.extruders.push_back(lt_next.extruders.front());
                        lt_extra.wipe_tower_partitions = lt_next.wipe_tower_partitions;
                    }
                }
            }
            break;
        }
    }

    // If the model contains empty layers (such as https://github.com/prusa3d/Slic3r/issues/1266), there might be layers
    // that were not marked as has_wipe_tower, even when they should have been. This produces a crash with soluble supports
    // and maybe other problems. We will therefore go through layer_tools and detect and fix this.
    // So, if there is a non-object layer starting with different extruder than the last one ended with (or containing more than one extruder),
    // we'll mark it with has_wipe tower.
    for (unsigned int i=0; i+1<m_layer_tools.size(); ++i) {
        LayerTools& lt = m_layer_tools[i];
        LayerTools& lt_next = m_layer_tools[i+1];
        if (lt.extruders.empty() || lt_next.extruders.empty())
            break;
        if (!lt_next.has_wipe_tower && (lt_next.extruders.front() != lt.extruders.back() || lt_next.extruders.size() > 1))
            lt_next.has_wipe_tower = true;
        // We should also check that the next wipe tower layer is no further than max_layer_height:
        unsigned int j = i+1;
        double last_wipe_tower_print_z = lt_next.print_z;
        while (++j < m_layer_tools.size()-1 && !m_layer_tools[j].has_wipe_tower)
            if (m_layer_tools[j+1].print_z - last_wipe_tower_print_z > max_layer_height + EPSILON) {
                m_layer_tools[j].has_wipe_tower = true;
                last_wipe_tower_print_z = m_layer_tools[j].print_z;
            }
    }

    // Calculate the wipe_tower_layer_height values.
    coordf_t wipe_tower_print_z_last = 0.;
    for (LayerTools &lt : m_layer_tools)
        if (lt.has_wipe_tower) {
            lt.wipe_tower_layer_height = lt.print_z - wipe_tower_print_z_last;
            wipe_tower_print_z_last = lt.print_z;
        }
}

bool ToolOrdering::insert_wipe_tower_extruder()
{
    if (!m_print_config_ptr->wipe_tower)
        return false;

    // In case that wipe_tower_extruder is set to non-zero, we must make sure that the extruder will be in the list.
    bool changed = false;
    if (m_print_config_ptr->wipe_tower_extruder != 0) {
        for (LayerTools& lt : m_layer_tools) {
            if (lt.wipe_tower_partitions > 0) {
                lt.extruders.emplace_back(m_print_config_ptr->wipe_tower_extruder - 1);
                sort_remove_duplicates(lt.extruders);
                changed = true;
            }
        }  
    }
    return changed;
}

void ToolOrdering::collect_extruder_statistics(bool prime_multi_material)
{
    m_first_printing_extruder = (unsigned int)-1;
    for (const auto &lt : m_layer_tools)
        if (! lt.extruders.empty()) {
            m_first_printing_extruder = lt.extruders.front();
            break;
        }

    m_last_printing_extruder = (unsigned int)-1;
    for (auto lt_it = m_layer_tools.rbegin(); lt_it != m_layer_tools.rend(); ++ lt_it)
        if (! lt_it->extruders.empty()) {
            m_last_printing_extruder = lt_it->extruders.back();
            break;
        }

    m_all_printing_extruders.clear();
    for (const auto &lt : m_layer_tools) {
        append(m_all_printing_extruders, lt.extruders);
        sort_remove_duplicates(m_all_printing_extruders);
    }

    if (prime_multi_material && ! m_all_printing_extruders.empty()) {
        // Reorder m_all_printing_extruders in the sequence they will be primed, the last one will be m_first_printing_extruder.
        // Then set m_first_printing_extruder to the 1st extruder primed.
        m_all_printing_extruders.erase(
            std::remove_if(m_all_printing_extruders.begin(), m_all_printing_extruders.end(),
                [ this ](const unsigned int eid) { return eid == m_first_printing_extruder; }),
            m_all_printing_extruders.end());
        m_all_printing_extruders.emplace_back(m_first_printing_extruder);
        m_first_printing_extruder = m_all_printing_extruders.front();
    }
}

// Layers are marked for infinite skirt aka draft shield. Not all the layers have to be printed.
void ToolOrdering::mark_skirt_layers(const PrintConfig &config, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    if (m_layer_tools.front().extruders.empty()) {
        // Empty first layer, no skirt will be printed.
        //FIXME throw an exception?
        return;
    }

    size_t i = 0;
    for (;;) {
        m_layer_tools[i].has_skirt = true;
        size_t j = i + 1;
        for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_object; ++ j);
        // i and j are two successive layers printing an object.
        if (j == m_layer_tools.size())
            // Don't print skirt above the last object layer.
            break;
        // Mark some printing intermediate layers as having skirt.
        double last_z = m_layer_tools[i].print_z;
        for (size_t k = i + 1; k < j; ++ k) {
            if (m_layer_tools[k + 1].print_z - last_z > max_layer_height + EPSILON) {
                // Layer k is the last one not violating the maximum layer height.
                // Don't extrude skirt on empty layers.
                while (m_layer_tools[k].extruders.empty())
                    -- k;
                if (m_layer_tools[k].has_skirt) {
                    // Skirt cannot be generated due to empty layers, there would be a missing layer in the skirt.
                    //FIXME throw an exception?
                    break;
                }
                m_layer_tools[k].has_skirt = true;
                last_z = m_layer_tools[k].print_z;
            }
        }
        i = j;
    }
}

// Assign a pointer to a custom G-code to the respective ToolOrdering::LayerTools.
// Ignore color changes, which are performed on a layer and for such an extruder, that the extruder will not be printing above that layer.
// If multiple events are planned over a span of a single layer, use the last one.
void ToolOrdering::assign_custom_gcodes(const Print &print)
{
	// Only valid for non-sequential print.
	assert(! print.config().complete_objects.value);

	const CustomGCode::Info	&custom_gcode_per_print_z = print.model().custom_gcode_per_print_z();
	if (custom_gcode_per_print_z.gcodes.empty())
		return;

	auto 						num_extruders = unsigned(print.config().nozzle_diameter.size());
	CustomGCode::Mode 			mode          =
		(num_extruders == 1) ? CustomGCode::SingleExtruder :
		print.object_extruders().size() == 1 ? CustomGCode::MultiAsSingle : CustomGCode::MultiExtruder;
	CustomGCode::Mode           model_mode    = print.model().custom_gcode_per_print_z().mode;
	std::vector<unsigned char> 	extruder_printing_above(num_extruders, false);
	auto 						custom_gcode_it = custom_gcode_per_print_z.gcodes.rbegin();
	// Tool changes and color changes will be ignored, if the model's tool/color changes were entered in mm mode and the print is in non mm mode
	// or vice versa.
	bool 						ignore_tool_and_color_changes = (mode == CustomGCode::MultiExtruder) != (model_mode == CustomGCode::MultiExtruder);
	// If printing on a single extruder machine, make the tool changes trigger color change (M600) events.
	bool 						tool_changes_as_color_changes = mode == CustomGCode::SingleExtruder && model_mode == CustomGCode::MultiAsSingle;

	// From the last layer to the first one:
	for (auto it_lt = m_layer_tools.rbegin(); it_lt != m_layer_tools.rend(); ++ it_lt) {
		LayerTools &lt = *it_lt;
		// Add the extruders of the current layer to the set of extruders printing at and above this print_z.
		for (unsigned int i : lt.extruders)
			extruder_printing_above[i] = true;
		// Skip all custom G-codes above this layer and skip all extruder switches.
		for (; custom_gcode_it != custom_gcode_per_print_z.gcodes.rend() && (custom_gcode_it->print_z > lt.print_z + EPSILON || custom_gcode_it->type == CustomGCode::ToolChange); ++ custom_gcode_it);
		if (custom_gcode_it == custom_gcode_per_print_z.gcodes.rend())
			// Custom G-codes were processed.
			break;
		// Some custom G-code is configured for this layer or a layer below.
		const CustomGCode::Item &custom_gcode = *custom_gcode_it;
		// print_z of the layer below the current layer.
		coordf_t print_z_below = 0.;
		if (auto it_lt_below = it_lt; ++ it_lt_below != m_layer_tools.rend())
			print_z_below = it_lt_below->print_z;
		if (custom_gcode.print_z > print_z_below + 0.5 * EPSILON) {
			// The custom G-code applies to the current layer.
			bool color_change = custom_gcode.type == CustomGCode::ColorChange;
			bool tool_change  = custom_gcode.type == CustomGCode::ToolChange;
			bool pause_or_custom_gcode = ! color_change && ! tool_change;
			bool apply_color_change = ! ignore_tool_and_color_changes &&
				// If it is color change, it will actually be useful as the exturder above will print.
				(color_change ? 
					mode == CustomGCode::SingleExtruder || 
						(custom_gcode.extruder <= int(num_extruders) && extruder_printing_above[unsigned(custom_gcode.extruder - 1)]) :
				 	tool_change && tool_changes_as_color_changes);
			if (pause_or_custom_gcode || apply_color_change)
        		lt.custom_gcode = &custom_gcode;
			// Consume that custom G-code event.
			++ custom_gcode_it;
		}
	}
}

const LayerTools& ToolOrdering::tools_for_layer(coordf_t print_z) const
{
    auto it_layer_tools = std::lower_bound(m_layer_tools.begin(), m_layer_tools.end(), LayerTools(print_z - EPSILON));
    assert(it_layer_tools != m_layer_tools.end());
    coordf_t dist_min = std::abs(it_layer_tools->print_z - print_z);
    for (++ it_layer_tools; it_layer_tools != m_layer_tools.end(); ++ it_layer_tools) {
        coordf_t d = std::abs(it_layer_tools->print_z - print_z);
        if (d >= dist_min)
            break;
        dist_min = d;
    }
    -- it_layer_tools;
    assert(dist_min < EPSILON);
    return *it_layer_tools;
}

// This function is called from Print::mark_wiping_extrusions and sets extruder this entity should be printed with (-1 .. as usual)
void WipingExtrusions::set_extruder_override(const ExtrusionEntity* entity, size_t copy_id, int extruder, size_t num_of_copies)
{
    m_something_overridden = true;

    auto entity_map_it = (m_entity_map.emplace(entity, ExtruderPerCopy())).first; // (add and) return iterator
    ExtruderPerCopy& copies_vector = entity_map_it->second;
    copies_vector.resize(num_of_copies, -1);

    assert(copies_vector[copy_id] == -1);
    if (copies_vector[copy_id] != -1)
        BOOST_LOG_TRIVIAL(error) << "ERROR: Entity extruder overriden multiple times!!!";

    copies_vector[copy_id] = extruder;
}

// Finds first non-soluble extruder on the layer
[[nodiscard]] static int first_nonsoluble_extruder_on_layer(const PrintConfig& print_config, const LayerTools& layer_tools)
{
    for (auto extruders_it = layer_tools.extruders.begin(); extruders_it != layer_tools.extruders.end(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Finds last non-soluble extruder on the layer
[[nodiscard]] static int last_nonsoluble_extruder_on_layer(const PrintConfig& print_config, const LayerTools& layer_tools)
{
    for (auto extruders_it = layer_tools.extruders.rbegin(); extruders_it != layer_tools.extruders.rend(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Following function iterates through all extrusions on the layer, remembers those that could be used for wiping after toolchange
// and returns volume that is left to be wiped on the wipe tower.
// Switching from old_extruder to new_extruder, trying to wipe volume_to_wipe into not yet extruded extrusions, that may change material (overridable).
float WipingExtrusions::mark_wiping_extrusions(const Print& print, const LayerTools &lt, unsigned int old_extruder, unsigned int new_extruder, float volume_to_wipe)
{
    const float min_infill_volume = 0.f; // ignore infill with smaller volume than this

    if (! m_something_overridable || volume_to_wipe <= 0. || 
        // Don't wipe a soluble filament into another object.
        print.config().filament_soluble.get_at(old_extruder) || 
        // Don't prime a soluble filament into another object.
        print.config().filament_soluble.get_at(new_extruder))
        // Soluble filament cannot be wiped in a random infill, neither the filament after it
        return std::max(0.f, volume_to_wipe);

    // we will sort objects so that dedicated for wiping are at the beginning:
    ConstPrintObjectPtrs object_list(print.objects().begin(), print.objects().end());
    std::sort(object_list.begin(), object_list.end(), [](const std::shared_ptr<PrintObject> a, const std::shared_ptr<PrintObject> b) { return a->config().wipe_into_objects && ! b->config().wipe_into_objects; });

    // We will now iterate through
    //  - first the dedicated objects to mark perimeters or infills (depending on infill_first)
    //  - second through the dedicated ones again to mark infills or perimeters (depending on infill_first)
    //  - then all the others to mark infills (in case that !infill_first, we must also check that the perimeter is finished already
    // this is controlled by the following variable:
    bool perimeters_done = false;

    for (int i=0 ; i<(int)object_list.size() + (perimeters_done ? 0 : 1); ++i) {
        if (!perimeters_done && (i==(int)object_list.size() || !object_list[i]->config().wipe_into_objects)) { // we passed the last dedicated object in list
            perimeters_done = true;
            i=-1;   // let's go from the start again
            continue;
        }

        const auto & object = object_list[i];

        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;
        size_t num_of_copies = object->instances().size();

        // iterate through copies (aka PrintObject instances) first, so that we mark neighbouring infills to minimize travel moves
        for (unsigned int copy = 0; copy < num_of_copies; ++copy) {
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();
                if (!region.config().wipe_into_infill && !object->config().wipe_into_objects)
                    continue;

                bool wipe_into_infill_only = ! object->config().wipe_into_objects && region.config().wipe_into_infill;
                if (print.config().infill_first != perimeters_done || wipe_into_infill_only) {
                    for (const ExtrusionEntity* ee : layerm->fills()) { // iterate through all infill Collections
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                        if (!is_overriddable(*fill, lt, print.config(), *object, region))
                            continue;

                        if (wipe_into_infill_only && ! print.config().infill_first)
                            // In this case we must check that the original extruder is used on this layer before the one we are overridding
                            // (and the perimeters will be finished before the infill is printed):
                            if (!lt.is_extruder_order(lt.perimeter_extruder(region), new_extruder))
                                continue;

                        if ((!is_entity_overridden(fill, copy) && fill->total_volume() > min_infill_volume)) {     // this infill will be used to wipe this extruder
                            set_extruder_override(fill, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                if (object->config().wipe_into_objects && print.config().infill_first == perimeters_done)
                {
                    for (const ExtrusionEntity* ee : layerm->perimeters()) {
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                        if (is_overriddable(*fill, lt, print.config(), *object, region) && !is_entity_overridden(fill, copy) && fill->total_volume() > min_infill_volume) {
                            set_extruder_override(fill, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }
            }
        }
    }
	// Some purge remains to be done on the Wipe Tower.
    assert(volume_to_wipe > 0.);
    return volume_to_wipe;
}



// Called after all toolchanges on a layer were mark_infill_overridden. There might still be overridable entities,
// that were not actually overridden. If they are part of a dedicated object, printing them with the extruder
// they were initially assigned to might mean violating the perimeter-infill order. We will therefore go through
// them again and make sure we override it.
void WipingExtrusions::ensure_perimeters_infills_order(const Print& print, const LayerTools &lt)
{
	if (! m_something_overridable)
		return;

    unsigned int first_nonsoluble_extruder = first_nonsoluble_extruder_on_layer(print.config(), lt);
    unsigned int last_nonsoluble_extruder = last_nonsoluble_extruder_on_layer(print.config(), lt);

    for (const auto & object : print.objects()) {
        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;
        size_t num_of_copies = object->instances().size();

        for (size_t copy = 0; copy < num_of_copies; ++copy) {    // iterate through copies first, so that we mark neighbouring infills to minimize travel moves
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();
                if (!region.config().wipe_into_infill && !object->config().wipe_into_objects)
                    continue;

                for (const ExtrusionEntity* ee : layerm->fills()) { // iterate through all infill Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                    assert(fill);

                    if (!is_overriddable(*fill, lt, print.config(), *object, region)
                     || is_entity_overridden(fill, copy) )
                        continue;

                    // This infill could have been overridden but was not - unless we do something, it could be
                    // printed before its perimeter, or not be printed at all (in case its original extruder has
                    // not been added to LayerTools
                    // Either way, we will now force-override it with something suitable:
                    if (print.config().infill_first
                    || object->config().wipe_into_objects  // in this case the perimeter is overridden, so we can override by the last one safely
                    || lt.is_extruder_order(lt.perimeter_extruder(region), last_nonsoluble_extruder)    // !infill_first, but perimeter is already printed when last extruder prints
                    || ! lt.has_extruder(lt.infill_extruder(region))) // we have to force override - this could violate infill_first (FIXME)
                        set_extruder_override(fill, copy, (print.config().infill_first ? first_nonsoluble_extruder : last_nonsoluble_extruder), num_of_copies);
                    else {
                        // In this case we can (and should) leave it to be printed normally.
                        // Force overriding would mean it gets printed before its perimeter.
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                for (const ExtrusionEntity* ee : layerm->perimeters()) { // iterate through all perimeter Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                    assert(fill);
                    if (is_overriddable(*fill, lt, print.config(), *object, region) && ! is_entity_overridden(fill, copy))
                        set_extruder_override(fill, copy, (print.config().infill_first ? last_nonsoluble_extruder : first_nonsoluble_extruder), num_of_copies);
                }
            }
        }
    }
}


int ToolOrdering::toolchanges_count() const
{
    std::vector<unsigned int> tools_in_order;
    for (const LayerTools& lt : m_layer_tools)
        tools_in_order.insert(tools_in_order.end(), lt.extruders.begin(), lt.extruders.end());
    assert(std::find(tools_in_order.begin(), tools_in_order.end(), (unsigned int)(-1)) == tools_in_order.end());
    for (size_t i=1; i<tools_in_order.size(); ++i)
        if (tools_in_order[i] == tools_in_order[i-1])
            tools_in_order[i-1] = (unsigned int)(-1);
    tools_in_order.erase(std::remove(tools_in_order.begin(), tools_in_order.end(), (unsigned int)(-1)), tools_in_order.end());
    if (tools_in_order.size() > 1 && tools_in_order.back() == tools_in_order[tools_in_order.size()-2])
        tools_in_order.pop_back();
    return std::max(0, int(tools_in_order.size())-1); // 5 tools = 4 toolchanges
}

} // namespace Slic3r
