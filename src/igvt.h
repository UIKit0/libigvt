/* Copyright (C) Citrix Systems
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */
#ifndef __IGVT_H_
#define __IGVT_H_

/**
 * @file igvt.h
 *
 * @author John Baboval <john.baboval@citrix.com>
 *
 * @brief C bindings for the Intel Graphics Virtualization Technology
 * (Intel GVT) sysfs API.
 *
 */

typedef enum {
    PORT_A = 0,
    PORT_EDP = PORT_A,
    PORT_B,
    PORT_C,
    PORT_D,
    PORT_E,
    PORT_VGA = PORT_E,
    GVT_MAX_PORTS,
} gt_port;

/**
 * @brief Set's which domain is directly displayed.
 *
 * @param domid
 * @return 0 on success, -EINVAL for invalid domains
 */
int igvt_set_foreground_vm(unsigned int domid);

/**
 * @brief Translates a port name from the i915 DRM driver to a gt_port.
 *
 * @param i915_port_name is the name of the port in the same format as
 *        found in /sys/class/drm/i915
 * @return gt_port enum, or MAX_PORTS on error
 */
gt_port igvt_translate_i915_port(const char *i915_port_name);

/**
 * @brief translates from a gt_port enum to an i915 DRM port name.
 *
 * @param pgt_port_num The ID of the physical GT port
 * @return The name of the port to the i915 driver in the same format
 *         as found in /sys/class/drm/i915
 */
const char *igvt_translate_pgt_port(gt_port pgt_port_num);

/**
 * @brief Creates a virtual GT instance for a domain.
 *
 * @param domid The domain ID to create an instance for
 * @param aperture_size The size of the virtual graphics aperture in MiB (Suggest 64)
 * @param gm_size The size of the virtual graphics memory in MiB (Suggest 512)
 * @param fence_count The number of fence registers to reserve (Suggest 4)
 * @return 0 on success
 */
int igvt_create_instance(unsigned int domid, unsigned int aperture_size, unsigned int gm_size, unsigned int fence_count);

/**
 * @brief Destroy a virtual GT instance for a domain.
 *
 * @param domid The domain ID to destroy the instance of
 * @return 0 on success
 */
int igvt_destroy_instance(unsigned int domid);

/**
 * @brief Plug a display into a virtual port
 *
 * @param domid The domain ID of the port to plug
 * @param vgt_port The ID of the virtual port
 * @param edid Pointer to the EDID data for the virtual display
 * @param edid_size Size of the EDID data (Multiples of 128; max 256)
 * @param pgt_port The ID of the physical port to map the virtual display to
 *        when display ownership is assigned to domid
 * @return 0 on success
 */
int igvt_plug_display(unsigned int domid, gt_port vgt_port, unsigned char *edid, size_t edid_size, gt_port pgt_port);

/**
 * @brief Unplug a display from a virtual port
 *
 * @param domid The domain ID of the port to unplug
 * @param vgt_port The virtual port to unplug
 * @return 0 on success
 */
int igvt_unplug_display(unsigned int domid, gt_port vgt_port);

/**
 * @brief Port connection predicate
 *
 * @param domid The domain ID of the port to check
 * @param vgt_port The port ID of the port to check
 * @return boolean; 0 = disconnected, 1 = connected
 */
int igvt_port_plugged_p(unsigned int vmid, gt_port vgt_port);

/**
 * @brief Port hotpluggable
 *
 * @param domid The domain of the port to check
 * @param vgt_port The port ID of the port to check
 * @return boolean; 0 = not hotpluggable, 1 = hotpluggable
 */
int igvt_port_hotpluggable(unsigned int vmid, gt_port vgt_port);

#endif
