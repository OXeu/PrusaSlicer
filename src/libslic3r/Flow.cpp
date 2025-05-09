///|/ Copyright (c) Prusa Research 2016 - 2023 Pavel Mikuš @Godrak, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) Slic3r 2014 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2014 Petr Ledvina @ledvinap
///|/
///|/ ported from lib/Slic3r/Flow.pm:
///|/ Copyright (c) Prusa Research 2022 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2012 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Flow.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>
#include <cmath>

#include "I18N.hpp"
#include "Print.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

FlowErrorNegativeSpacing::FlowErrorNegativeSpacing() : 
	FlowError("Flow::spacing() produced negative spacing. Did you set some extrusion width too small?") {}

FlowErrorNegativeFlow::FlowErrorNegativeFlow() :
    FlowError("Flow::mm3_per_mm() produced negative flow. Did you set some extrusion width too small?") {}

// This static method returns a sane extrusion width default.
float Flow::auto_extrusion_width(FlowRole role, float nozzle_diameter)
{
    switch (role) {
    case frSupportMaterial:
    case frSupportMaterialInterface:
    case frTopSolidInfill:
        return nozzle_diameter;
    default:
    case frExternalPerimeter:
    case frPerimeter:
    case frSolidInfill:
    case frInfill:
        return 1.125f * nozzle_diameter;
    }
}

// Used by the Flow::extrusion_width() funtion to provide hints to the user on default extrusion width values,
// and to provide reasonable values to the PlaceholderParser.
static inline FlowRole opt_key_to_flow_role(const std::string &opt_key)
{
 	if (opt_key == "perimeter_extrusion_width" || 
 		// or all the defaults:
 		opt_key == "extrusion_width" || opt_key == "first_layer_extrusion_width")
        return frPerimeter;
    else if (opt_key == "external_perimeter_extrusion_width")
        return frExternalPerimeter;
    else if (opt_key == "infill_extrusion_width")
        return frInfill;
    else if (opt_key == "solid_infill_extrusion_width")
        return frSolidInfill;
	else if (opt_key == "top_infill_extrusion_width")
		return frTopSolidInfill;
	else if (opt_key == "support_material_extrusion_width")
    	return frSupportMaterial;
    else 
    	throw Slic3r::RuntimeError("opt_key_to_flow_role: invalid argument");
};

static inline void throw_on_missing_variable(const std::string &opt_key, const char *dependent_opt_key) 
{
	throw FlowErrorMissingVariable((boost::format(_u8L("Cannot calculate extrusion width for %1%: Variable \"%2%\" not accessible.")) % opt_key % dependent_opt_key).str());
}

// Used to provide hints to the user on default extrusion width values, and to provide reasonable values to the PlaceholderParser.
double Flow::extrusion_width(const std::string& opt_key, const ConfigOptionFloatOrPercent* opt, const ConfigOptionResolver& config, const unsigned int first_printing_extruder)
{
	assert(opt != nullptr);

	bool first_layer = boost::starts_with(opt_key, "first_layer_");

#if 0
// This is the logic used for skit / brim, but not for the rest of the 1st layer.
	if (opt->value == 0. && first_layer) {
		// The "first_layer_extrusion_width" was set to zero, try a substitute.
		opt = config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
		if (opt == nullptr)
    		throw_on_missing_variable(opt_key, "perimeter_extrusion_width");
	}
#endif

	if (opt->value == 0.) {
		// The role specific extrusion width value was set to zero, try the role non-specific extrusion width.
		opt = config.option<ConfigOptionFloatOrPercent>("extrusion_width");
		if (opt == nullptr)
    		throw_on_missing_variable(opt_key, "extrusion_width");
    	// Use the "layer_height" instead of "first_layer_height".
    	first_layer = false;
	}

	if (opt->percent) {
		auto opt_key_layer_height = first_layer ? "first_layer_height" : "layer_height";
        auto opt_layer_height = config.option(opt_key_layer_height);
    	if (opt_layer_height == nullptr)
    		throw_on_missing_variable(opt_key, opt_key_layer_height);
        assert(! first_layer || ! static_cast<const ConfigOptionFloatOrPercent*>(opt_layer_height)->percent);
		return opt->get_abs_value(opt_layer_height->getFloat());
	}

	if (opt->value == 0.) {
        // If user left option to 0, calculate a sane default width.
    	auto opt_nozzle_diameters = config.option<ConfigOptionFloats>("nozzle_diameter");
    	if (opt_nozzle_diameters == nullptr)
    		throw_on_missing_variable(opt_key, "nozzle_diameter");
        return auto_extrusion_width(opt_key_to_flow_role(opt_key), float(opt_nozzle_diameters->get_at(first_printing_extruder)));
    }

	return opt->value;
}

// Used to provide hints to the user on default extrusion width values, and to provide reasonable values to the PlaceholderParser.
double Flow::extrusion_width(const std::string& opt_key, const ConfigOptionResolver &config, const unsigned int first_printing_extruder)
{
    return extrusion_width(opt_key, config.option<ConfigOptionFloatOrPercent>(opt_key), config, first_printing_extruder);
}

// This constructor builds a Flow object from an extrusion width config setting
// and other context properties.
Flow Flow::new_from_config_width(FlowRole role, const ConfigOptionFloatOrPercent &width, float nozzle_diameter, float height)
{
    if (height <= 0)
        throw Slic3r::InvalidArgument("Invalid flow height supplied to new_from_config_width()");

    float w;
    if (! width.percent && width.value == 0.) {
        // If user left option to 0, calculate a sane default width.
        w = auto_extrusion_width(role, nozzle_diameter);
    } else {
        // If user set a manual value, use it.
        w = float(width.get_abs_value(height));
    }
    
    return Flow(w, height, rounded_rectangle_extrusion_spacing(w, height), nozzle_diameter, false);
}

// Adjust extrusion flow for new extrusion line spacing, maintaining the old spacing between extrusions.
Flow Flow::with_spacing(float new_spacing) const
{
    Flow out = *this;
    if (m_bridge) {
        // Diameter of the rounded extrusion.
        assert(m_width == m_height);
        float gap          = m_spacing - m_width;
        auto  new_diameter = new_spacing - gap;
        out.m_width        = out.m_height = new_diameter;
    } else {
        assert(m_width >= m_height);
        out.m_width += new_spacing - m_spacing;
        if (out.m_width < out.m_height)
            throw Slic3r::InvalidArgument("Invalid spacing supplied to Flow::with_spacing()");
    }
    out.m_spacing = new_spacing;
    return out;
}

// Adjust the width / height of a rounded extrusion model to reach the prescribed cross section area while maintaining extrusion spacing.
Flow Flow::with_cross_section(float area_new) const
{
    assert(! m_bridge);
    assert(m_width >= m_height);

    // Adjust for bridge_flow_ratio, maintain the extrusion spacing.
    float area = this->mm3_per_mm();
    if (area_new > area + EPSILON) {
        // Increasing the flow rate.
        float new_full_spacing = area_new / m_height;
        if (new_full_spacing > m_spacing) {
            // Filling up the spacing without an air gap. Grow the extrusion in height.
            float height = area_new / m_spacing;
            return Flow(rounded_rectangle_extrusion_width_from_spacing(m_spacing, height), height, m_spacing, m_nozzle_diameter, false);
        } else {
            return this->with_width(rounded_rectangle_extrusion_width_from_spacing(area / m_height, m_height));
        }
    } else if (area_new < area - EPSILON) {
        // Decreasing the flow rate.
        float width_new = m_width - (area - area_new) / m_height;
        assert(width_new > 0);
        if (width_new > m_height) {
            // Shrink the extrusion width.
            return this->with_width(width_new);
        } else {
            // Create a rounded extrusion.
            auto dmr = 2.0 * float(sqrt(area_new / M_PI));
            return Flow(dmr, dmr, m_spacing, m_nozzle_diameter, false);
        }
    } else
        return *this;
}

float Flow::rounded_rectangle_extrusion_spacing(float width, float height)
{
    auto out = width - height * float(1. - 0.25 * PI);
    if (out <= 0.f)
        throw FlowErrorNegativeSpacing();
    return out;
}

float Flow::rounded_rectangle_extrusion_width_from_spacing(float spacing, float height)
{
    return float(spacing + height * (1. - 0.25 * PI));
}

float Flow::bridge_extrusion_spacing(float dmr)
{
    return dmr + BRIDGE_EXTRA_SPACING;
}

// This method returns extrusion volume per head move unit.
double Flow::mm3_per_mm() const
{
    float res = m_bridge ?
        // Area of a circle with dmr of this->width.
        float((m_width * m_width) * 0.25 * PI) :
        // Rectangle with semicircles at the ends. ~ h (w - 0.215 h)
        float(m_height * (m_width - m_height * (1. - 0.25 * PI)));
    //assert(res > 0.);
	if (res <= 0.)
		throw FlowErrorNegativeFlow();
    return res;
}

static float min_nozzle_diameter(const PrintObject &print_object)
{
    const ConfigOptionFloats &nozzle_diameters    = print_object.print()->config().nozzle_diameter;
    float                     min_nozzle_diameter = std::numeric_limits<float>::max();

    for (const double nozzle_diameter : nozzle_diameters.values) {
        min_nozzle_diameter = std::min(min_nozzle_diameter, static_cast<float>(nozzle_diameter));
    }

    return min_nozzle_diameter;
}

Flow support_material_flow(const std::shared_ptr<PrintObject> &object, float layer_height)
{
    const PrintConfig &print_config = object->print()->config();
    const int          extruder     = object->config().support_material_extruder - 1;

    // If object->config().support_material_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), use the smallest nozzle diameter.
    const float nozzle_diameter = extruder >= 0 ? static_cast<float>(print_config.nozzle_diameter.get_at(extruder)) : min_nozzle_diameter(*object);

    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config().support_material_extrusion_width.value > 0) ? object->config().support_material_extrusion_width : object->config().extrusion_width,
        nozzle_diameter,
        (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value));
}

Flow support_material_1st_layer_flow(const std::shared_ptr<PrintObject> &object, float layer_height)
{
    const PrintConfig &print_config = object->print()->config();
    const int          extruder     = object->config().support_material_extruder - 1;

    // If object->config().support_material_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), use the smallest nozzle diameter.
    const float nozzle_diameter = extruder >= 0 ? static_cast<float>(print_config.nozzle_diameter.get_at(extruder)) : min_nozzle_diameter(*object);
    const auto &width           = (print_config.first_layer_extrusion_width.value > 0) ? print_config.first_layer_extrusion_width : object->config().support_material_extrusion_width;

    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (width.value > 0) ? width : object->config().extrusion_width,
        nozzle_diameter,
        (layer_height > 0.f) ? layer_height : float(print_config.first_layer_height.get_abs_value(object->config().layer_height.value)));
}

Flow support_material_interface_flow(const std::shared_ptr<PrintObject> &object, float layer_height)
{
    const PrintConfig &print_config = object->print()->config();
    const int          extruder     = object->config().support_material_interface_extruder - 1;

    // If object->config().support_material_interface_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), use the smallest nozzle diameter.
    const float nozzle_diameter = extruder >= 0 ? static_cast<float>(print_config.nozzle_diameter.get_at(extruder)) : min_nozzle_diameter(*object);

    return Flow::new_from_config_width(
        frSupportMaterialInterface,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config().support_material_extrusion_width > 0) ? object->config().support_material_extrusion_width : object->config().extrusion_width,
        nozzle_diameter,
        (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value));
}

}
