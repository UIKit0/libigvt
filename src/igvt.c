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

/**
 * @file igvt.c
 *
 * @author John Baboval <john.baboval@citrix.com>
 *
 */

#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "igvt.h"

#define VGT_KERNEL_PATH "/sys/kernel/vgt"
#define VGT_VM_ATTRIBUTE_FORMAT VGT_KERNEL_PATH "/vm%d/PORT_%c/%s"

int igvt_set_foreground_vm(unsigned int domid)
{
    FILE *fd;
    char path[128];
    struct stat st;
    int retval = -ENODEV;
    int r;

    snprintf(path, 128, VGT_KERNEL_PATH "/vm%d", domid);

    if (domid != 0 && stat(path, &st) != 0) {
        return -EINVAL;
    }

    fd = fopen("/sys/kernel/vgt/control/foreground_vm", "w");
    if (!fd)
        return retval;

    fprintf(fd, "%d\n", domid);

    // check that it was ready
    fd = fopen("/sys/kernel/vgt/control/foreground_vm", "r");
    if (!fd)
        return retval;

    r = -1;
    if ((fscanf(fd, "%d", &r) != 1) ||
        r != domid) {
        retval = -EAGAIN;
    }

    if (fd)
        fclose(fd);

    return retval;
}

gt_port igvt_translate_i915_port(const char *i915_port_name)
{
    int port = 2;

    // Ugliness - logic from intel's vgt_mgr script
    if (strcmp(i915_port_name, "card0-eDP-1") == 0) {
        port = 0;
    } else if (strcmp(i915_port_name, "card0-DP-1") == 0) {
        port = 1;
    } else if (strcmp(i915_port_name, "card0-HDMI-A-1") == 0) {
        port = 1;
    } else if (strcmp(i915_port_name, "card0-DP-2") == 0) {
        port = 2;
    } else if (strcmp(i915_port_name, "card0-HDMI-A-2") == 0) {
        port = 2;
    } else if (strcmp(i915_port_name, "card0-DP-3") == 0) {
        port = 3;
    } else if (strcmp(i915_port_name, "card0-HDMI-A-3") == 0) {
        port = 3;
    } else if (strcmp(i915_port_name, "card0-VGA-1") == 0) {
        port = 4;
    }

    return port;
}

const char *igvt_translate_pgt_port(gt_port pgt_port_num)
{
    switch (pgt_port_num) {
    case 0:
        return "card0-eDP-1";
    case 1:
        return "card-HDMI-A-1";
    case 2:
        return "card0-HDMI-A-2";
    case 3:
        return "card0-HDMI-A-3";
    case 4:
        return "card0-VGA-1";
    default:
        break;
    }

    return "INVALID";
}

int igvt_create_instance(unsigned int domid, unsigned int aperture_size, unsigned int gm_size, unsigned int fence_count)
{
    FILE *fd;
    char *path = VGT_KERNEL_PATH "/control/create_vgt_instance";

    fd = fopen(path, "w");
    if (!fd)
        return errno;

    if (fprintf(fd, "%d,%u,%u,%u,%d\n", domid, aperture_size, gm_size, fence_count, -1) < 0) {
        fclose(fd);
        return errno;
    }

    fclose(fd);

    return 0;
}

int igvt_destroy_instance(unsigned int domid)
{
    FILE *fd;
    char *path = VGT_KERNEL_PATH "/control/create_vgt_instance";

    fd = fopen(path, "w");
    if (!fd)
        return errno;

    if (fprintf(fd, "%d\n", -domid) < 0) {
        fclose(fd);
        return errno;
    }

    fclose(fd);

    return 0;
}

static const char *port_strings[] = {
    "PORT_A",
    "PORT_B",
    "PORT_C",
    "PORT_D",
    "PORT_E",
};

static void _filter_edid(unsigned char *edid, size_t edid_size)
{
    int i;

    /*
     * There are limits to the pixelClock EDID field
     * that the Windows GT driver will support. 
     *
     * The limits are meaningless, since the port is
     * virtual, and no clocks are actually configured.
     *
     * Override the pixelClock fields here to fit into
     * the limits.
     */

    /*
     * There are four timing descriptors. 18 bytes long,
     * starting at byte 54. The first two bytes is the
     * pixelClock.
     */
    for (i = 0; i < 4; i++) {
        unsigned char *timingDescriptor = (unsigned char *) &edid[54 + (18 * i)];
        unsigned short clock = timingDescriptor[0] + (timingDescriptor[1] << 8);

        /* Cap the pixel clock (bytes 0-1) at 160mhz */
        if (clock > 16000) {

            /* Add the old value back to the checksum */
            edid[0x7f] += timingDescriptor[0];
            edid[0x7f] += timingDescriptor[1];

            timingDescriptor[0] = 16000 & 0xff;
            timingDescriptor[1] = 16000 >> 8;

            /* Subtract the new value from the checksum */
            edid[0x7f] -= timingDescriptor[0];
            edid[0x7f] -= timingDescriptor[1];
        }
    }


}

int igvt_plug_display(unsigned int domid, gt_port vgt_port, unsigned char *edid, size_t edid_size, gt_port pgt_port)
{
    char filename[256];
    FILE *f;

    if (igvt_port_plugged_p(domid, vgt_port)) {
        igvt_unplug_display(domid, vgt_port);
    }

    sprintf(filename, VGT_VM_ATTRIBUTE_FORMAT, domid, (char)((char)vgt_port + 'A'), "port_override");
    f = fopen(filename, "w");
    if (!f) {
        //clog << logCrit << "error opening " << filename << endl;
        return errno;
    }

    fprintf(f, "%s\n", port_strings[pgt_port]);
    fclose (f);

    _filter_edid(edid, edid_size);

    sprintf(filename, VGT_VM_ATTRIBUTE_FORMAT, domid, (char)((char)vgt_port + 'A'), "edid");
    f = fopen(filename, "w");
    if (!f) {
        //clog << logCrit << "error opening " << filename << endl;
        return errno;
    }

    fwrite(edid, edid_size > 128 ? 128 : edid_size, 1, f);
    fclose (f);

    sprintf(filename, VGT_VM_ATTRIBUTE_FORMAT, domid, (char)((char)vgt_port + 'A'), "connection");
    f = fopen(filename, "w");
    if (!f) {
        //clog << logCrit << "error opening " << filename << endl;
        return errno;
    }

    fprintf(f, "%s\n", "connect");
    fclose (f);

    return (0);
}

/**
 * @brief Unplug a display from a virtual port
 *
 * @param domid The domain ID of the port to unplug
 * @param vgt_port The virtual port to unplug
 * @return 0 on success
 */
int igvt_unplug_display(unsigned int domid, gt_port vgt_port)
{
    char path[256];
    FILE *f;
    struct stat st;

    snprintf(path, 128, VGT_KERNEL_PATH "/vm%d", domid);

    if (domid == 0 || stat(path, &st) != 0) {
        //clog << debugXenGT << debugXenGTHotplug << "cowardly refusing to hotplug dom0" << endl;
        return -EINVAL;
    }

    if (vgt_port >= GVT_MAX_PORTS) {
        return (-EINVAL);
    }

    sprintf(path, VGT_VM_ATTRIBUTE_FORMAT, domid, (char)((char)vgt_port + 'A'), "connection");
    f = fopen(path, "w");
    if (!f) {
        //clog << logCrit << "error opening " << filename << endl;
        return errno;
    }

    fprintf(f, "%s\n", "disconnect");
    fclose (f);

    return (0);
}

/**
 * @brief Port connection predicate
 *
 * @param domid The domain ID of the port to check
 * @param vgt_port The port ID of the port to check
 * @return boolean; 0 = disconnected, 1 = connected
 */
int igvt_port_plugged_p(unsigned int domid, gt_port vgt_port)
{
    char path[256];
    char c[12];
    FILE *f;
    struct stat st;
    int retval = 0;

    snprintf(path, 128, VGT_KERNEL_PATH "/vm%d", domid);

    if (domid == 0 || stat(path, &st) != 0) {
        //clog << debugXenGT << debugXenGTHotplug << "cowardly refusing to hotplug dom0" << endl;
        return (0);
    }

    if (vgt_port >= GVT_MAX_PORTS) {
        return (0);
    }

    sprintf(path, VGT_VM_ATTRIBUTE_FORMAT, domid, (char)((char)vgt_port + 'A'), "connection");
    f = fopen(path, "w");
    if (!f) {
        //clog << logCrit << "error opening " << filename << endl;
        return (0);
    }

    if (fscanf(f, "%s", c) != 1) {
        retval = 0;
    } else if (strcmp("connected", c) != 0) {
        retval = 0;
    } else {
        retval = 1;
    }

    if (f)
        fclose(f);

    return retval;
}
