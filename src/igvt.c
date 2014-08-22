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

#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "igvt.h"

typedef enum {
    IGVT_ERROR = 0,
    IGVT_WARNING = 1,
    IGVT_NUM_LOGGERS
} igvt_log_type;

static int igvt_printf(igvt_log_type log_type, const char *format, ...);

#define VGT_KERNEL_PATH "/sys/kernel/vgt"
#define VGT_VM_ATTRIBUTE_FORMAT VGT_KERNEL_PATH "/vm%d/%s/%s"

static const char *port_strings[] = {
    "PORT_A",
    "PORT_B",
    "PORT_C",
    "PORT_D",
    "PORT_E",
};

static inline int
igvt_is_valid_port_p(gt_port port)
{
    switch (port) {
        case PORT_EDP:
        case PORT_B:
        case PORT_C:
        case PORT_D:
        case PORT_VGA:
            return 1;
        break;

        default:
            return 0;
        break;
    }

    return 0;
}

int igvt_available_p(void)
{
    struct stat st;

    /*
     * If the top level path to the igvt info is missing
     * then igvt isn't supported on this machine.
     */
    if (stat(VGT_KERNEL_PATH, &st) != 0) {
        return 0;
    }

    return 1;
}

int igvt_enabled_p(unsigned int domid)
{
    char path[128];
    struct stat st;

    /* Dom0 is never a valid igvt domain */
    if (domid == 0)
	return 0;

    snprintf(path, sizeof(path), VGT_KERNEL_PATH "/vm%d", domid);

    if (stat(path, &st) != 0) {
	igvt_printf(IGVT_ERROR, "%s::cannot stat %s: %s\n",
		    __func__, path, strerror(errno));

        return 0;
    }

    return 1;
}


/**
 * @brief Set the foreground VM
 *
 * @param domid The domain ID of the port to put in the foreground
 * @return 0 on success
 */
int igvt_set_foreground_vm(unsigned int domid)
{
    FILE *fd;
    char path[128];
    struct stat st;
    int retval = 0;
    int n, r = -1;
    int status;

    if (domid != 0) {
        snprintf(path, sizeof(path), VGT_KERNEL_PATH "/vm%d", domid);

        if (stat(path, &st) != 0) {
	    igvt_printf(IGVT_WARNING, "%s::VM %d at %s doesn't exist\n",
			__func__, domid, path);

            return -EINVAL;
        }
    }

    /* Check to see if the fg vm needs to change */
    fd = fopen(VGT_KERNEL_PATH "/control/foreground_vm", "r");

    if (!fd) {
	igvt_printf(IGVT_WARNING, "::%s Foreground VM file " 
		    VGT_KERNEL_PATH
		    "/control/foreground_vm can't be open for read\n",
		    __func__);

        return -ENODEV;
    }

    n = fscanf(fd, "%d", &r);

    fclose(fd);

    if (n == 1 && r == domid) {
	/* No change required. */
        return 0;
    }

    /* We need to change the fg vm. */
    fd = fopen(VGT_KERNEL_PATH "/control/foreground_vm", "w");

    if (!fd) {
	igvt_printf(IGVT_WARNING, "::%s Foreground VM file "
		    VGT_KERNEL_PATH
		    "/control/foreground_vm can't be open for write\n",
		    __func__);

        return -ENODEV;
    }

    status = fprintf(fd, "%d", domid);

    if (status <= 0) {
	igvt_printf(IGVT_WARNING, "%s::fprintf returned %d, error: %s\n",
		    __func__, status, strerror(errno));
    }

    status = fclose(fd);

    if (status < 0) {
	igvt_printf(IGVT_WARNING, "%s::fclose returned %d, error: %s\n",
		    __func__, status, strerror(errno));
    }

    /* check that it was actually set. */
    fd = fopen(VGT_KERNEL_PATH "/control/foreground_vm", "r");

    if (!fd) {
	igvt_printf(IGVT_WARNING, "%s::Foreground VM file "
		    VGT_KERNEL_PATH 
		    "/control/foreground_vm can't be open for re-read\n",
		    __func__);

        return -ENODEV;
    }

    n = fscanf(fd, "%d", &r);

    if (n != 1 || r != domid) {
        igvt_printf(IGVT_WARNING,
		    "%s:: set DomID %d does not match "
		    "returned DomID: %d nRead: %d\n",
	             __func__, domid, r, n);

        retval = -EAGAIN;
    }

    fclose(fd);

    return retval;
}

/**
 * @brief Given a port name return the associated gt_port ID
 *
 * @param domid The domain ID
 * @param i915_port_name
 * @return the gt_port on success or PORT_ILLEGAL on failure
 * Note that the same port ID is returned for DP and HDMI-A
 * devices.
 */
gt_port igvt_translate_i915_port(const char *i915_port_name)
{
    gt_port port = PORT_ILLEGAL;

    /* Ugliness - logic from intel's vgt_mgr script */
    if (strcmp(i915_port_name, "card0-eDP-1") == 0) {
        port = PORT_EDP;
    } else if (strcmp(i915_port_name, "card0-DP-1") == 0) {
        port = PORT_B;
    } else if (strcmp(i915_port_name, "card0-HDMI-A-1") == 0) {
        port = PORT_B;
    } else if (strcmp(i915_port_name, "card0-DP-2") == 0) {
        port = PORT_C;
    } else if (strcmp(i915_port_name, "card0-HDMI-A-2") == 0) {
        port = PORT_C;
    } else if (strcmp(i915_port_name, "card0-DP-3") == 0) {
        port = PORT_D;
    } else if (strcmp(i915_port_name, "card0-HDMI-A-3") == 0) {
        port = PORT_D;
    } else if (strcmp(i915_port_name, "card0-VGA-1") == 0) {
        port = PORT_VGA;
    } else {
	igvt_printf(IGVT_ERROR, "%s::Invalid vgt_port %s\n",
		    __func__, i915_port_name);
    }

    return port;
}

/**
 * @brief Given a gt_port ID return the associated port name
 *
 * @param pgt_port_num
 * @return the port name on success, or "INVALID" on failure.
 * Note that only the HDMI-A name is returned for ports that
 * might actually be DP ports.
 */
const char *igvt_translate_pgt_port(gt_port pgt_port_num)
{
    switch (pgt_port_num) {
    case PORT_EDP:
        return "card0-eDP-1";
    case PORT_B:
        return "card-HDMI-A-1";
    case PORT_C:
        return "card0-HDMI-A-2";
    case PORT_D:
        return "card0-HDMI-A-3";
    case PORT_VGA:
        return "card0-VGA-1";
    default:
        /* Should there be an error here? */
        break;
    }

    return "INVALID";
}

/**
 * @brief Create an igvt instance
 *
 * @param domid The domain ID
 * @param aperture_size
 * @param gm_size
 * @param fence_count
 * @return 0 on success
 */
int igvt_create_instance(unsigned int domid, unsigned int aperture_size,
			 unsigned int gm_size, unsigned int fence_count)
{
    FILE *fd;
    char *path = VGT_KERNEL_PATH "/control/create_vgt_instance";
    int retval = 0;

    fd = fopen(path, "w");

    if (!fd)
        return -ENODEV;

    if (fprintf(fd, "%d,%u,%u,%u,%d\n", domid, aperture_size,
				        gm_size, fence_count, 1) < 0) {
        retval = -errno;
    }

    fclose(fd);

    return retval;
}

/**
 * @brief Destroy an igvt instance
 *
 * @param domid The domain ID
 * @return 0 on success
 */
int igvt_destroy_instance(unsigned int domid)
{
    FILE *fd;
    char *path = VGT_KERNEL_PATH "/control/create_vgt_instance";
    int retval = 0;

    fd = fopen(path, "w");

    if (!fd)
        return -ENODEV;

    if (fprintf(fd, "%d\n", -domid) < 0) {
        retval = -errno;
    }

    fclose(fd);

    return retval;
}

static void write_edid_byte(unsigned char *edid, size_t byte, const unsigned char value)
{
	/* Add the old value back to the checksum */
	edid[0x7f] += edid[byte];

	edid[byte] = value;

	/* Subtract the new value from the checksum */
	edid[0x7f] -= edid[byte];
}

static void _filter_edid(unsigned char *edid, size_t edid_size, int analog_port)
{
    int i;

    /*
     * The virtual port is unaware of the dongle status,
     * so we must make certain that the digital/analog input
     * bit matches the port.
     *
     * The digital/analog bit is in the Video Input Parameters
     * bitmap. EDID byte 20, bit 7.
     * 
     * Toggling this bit changes the definition of the 
     * Supported Features Bitmap (byte 24) bits 3:4. When digital,
     * 0x0 == RGB 4:4:4 color support. When analog, 0x1 == RGB color.
     */

    if (analog_port && (edid[20] & 0x80) ) {

        write_edid_byte(edid, 20, 0x00);
        write_edid_byte(edid, 24, (edid[24] & 0xE7) | 0x08);

    } else if (!analog_port && !(edid[20] & 0x80)) {

        write_edid_byte(edid, 20, 0x80);
        write_edid_byte(edid, 24, (edid[24] & 0xE7));

    } 

    /*
     * Funny things happen when the Windows graphics driver
     * invokes DPMS. (Stale images, off-screen buffers visible).
     *
     * Clear the DPMS bits so Windows doesn't use it.
     * 
     * DPMS support bits are in the Supported Features Bitmap.
     * Byte 24, bits 5:7
     */
    write_edid_byte(edid, 24, edid[24] & 0x1F);

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

            write_edid_byte(edid, (&timingDescriptor[0] - edid), 16000 & 0xff);
            write_edid_byte(edid, (&timingDescriptor[1] - edid), 16000 >> 8);
        }
    }
}

static int is_port_digital(gt_port port) {

    return port < PORT_E;

}

/**
 * @brief Plug in a display
 *
 * @param domid The domain ID
 * @param vgt_port
 * @param edid
 * @param edid_size
 * @param pgt_port
 * @return 0 on success
 */
int igvt_plug_display(unsigned int domid, gt_port vgt_port,
		      unsigned char *edid, size_t edid_size,
		      gt_port pgt_port)
{
    char filename[256];
    FILE *fd;

    if (!igvt_enabled_p(domid)) {
	igvt_printf(IGVT_ERROR, "%s::Invalid domain %d\n",
		    __func__, domid);
	return -EINVAL;
    }

    if (!igvt_is_valid_port_p(vgt_port)) {
	igvt_printf(IGVT_ERROR, "%s::Invalid vgt_port %s\n",
		    __func__, vgt_port);

        return -EINVAL;
    }

    if (!igvt_is_valid_port_p(pgt_port)) {
	igvt_printf(IGVT_ERROR, "%s::Invalid pgt_port %s\n",
		    __func__, pgt_port);

        return -EINVAL;
    }

    if (igvt_port_plugged_p(domid, vgt_port)) {
        igvt_unplug_display(domid, vgt_port);
    }

    snprintf(filename, sizeof(filename),
	     VGT_VM_ATTRIBUTE_FORMAT, domid,
	     port_strings[vgt_port], "port_override");

    fd = fopen(filename, "w");

    if (!fd) {
	igvt_printf(IGVT_ERROR, "%s::error opening %s: %s\n",
		    __func__, filename, strerror(errno));

        return -ENODEV;
    }

    fprintf(fd, "%s\n", port_strings[pgt_port]);
    fclose (fd);

    _filter_edid(edid, edid_size, is_port_digital(vgt_port));

    snprintf(filename, sizeof(filename),
	     VGT_VM_ATTRIBUTE_FORMAT, domid,
	     port_strings[vgt_port], "edid");

    fd = fopen(filename, "w");

    if (!fd) {
	igvt_printf(IGVT_ERROR, "%s::error opening %s: %s\n",
		    __func__, filename, strerror(errno));

        return -ENODEV;
    }

    /* Writing more than 128 EDID bytes currently hangs the system */
    if (edid_size > 128) {
        edid_size = 128;
    }

    if (fwrite(edid, 1, edid_size, fd) != edid_size) {
        fprintf(stderr, "%s::failed to write EDID: %s\n",
                __func__, strerror(errno));
    }

    fclose (fd);

    snprintf(filename, sizeof(filename),
	     VGT_VM_ATTRIBUTE_FORMAT, domid, 
	     port_strings[vgt_port], "connection");

    fd = fopen(filename, "w");

    if (!fd) {
	igvt_printf(IGVT_ERROR, "%s::error opening %s: %s\n",
		    __func__, filename, strerror(errno));

        return -ENODEV;
    }

    fprintf(fd, "%s\n", "connect");
    fclose (fd);

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

    if (!igvt_enabled_p(domid)) {
	igvt_printf(IGVT_ERROR, "%s::Invalid domain %d\n",
		    __func__, domid);
	return -EINVAL;
    }

    if (!igvt_is_valid_port_p(vgt_port)) {
	igvt_printf(IGVT_ERROR, "%s::Invalid vgt_port %s\n",
		    __func__, vgt_port);

        return -EINVAL;
    }

    snprintf(path, sizeof(path),
	     VGT_VM_ATTRIBUTE_FORMAT, domid, 
	     port_strings[vgt_port], "connection");

    f = fopen(path, "w");

    if (!f) {
	igvt_printf(IGVT_ERROR, "%s::error opening %s: %s\n",
		    __func__, path, strerror(errno));

        return -ENODEV;
    }

    fprintf(f, "%s\n", "disconnect");
    fclose (f);

    return (0);
}

/**
 * @brief Predicate that returns whether a port is plugged in or not
 *
 * @param domid The domain ID
 * @param vgt_port
 * @return 1 if plugged in
 */
int igvt_port_plugged_p(unsigned int domid, gt_port vgt_port)
{
    char path[256];
    char c[12];
    FILE *f;
    struct stat st;
    int retval = 0;

    if (!igvt_enabled_p(domid)) {
	igvt_printf(IGVT_ERROR, "%s::Invalid domain %d\n",
		    __func__, domid);
	return 0;
    }

    if (!igvt_is_valid_port_p(vgt_port)) {
	igvt_printf(IGVT_ERROR, "%s::Invalid vgt_port %s\n",
		    __func__, vgt_port);

        return (0);
    }

    if (domid == 0) {
        return (0);
    }

    snprintf(path, sizeof(path), VGT_KERNEL_PATH "/vm%d", domid);

    if (stat(path, &st) != 0) {
	igvt_printf(IGVT_ERROR, "%s::error opening %s: %s\n",
		    __func__, path, strerror(errno));

        return (0);
    }

    snprintf(path, sizeof(path),
	     VGT_VM_ATTRIBUTE_FORMAT, domid, 
	     port_strings[vgt_port], "connection");

    f = fopen(path, "r");

    if (!f) {
	igvt_printf(IGVT_ERROR, "%s::error opening %s: %s\n",
		    __func__, path, strerror(errno));

        return 0;
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

/**
 * @brief Predicate that returns whether a port is present or not
 *
 * @param domid The domain ID
 * @param vgt_port
 * @return 1 if present, else 0
 */
int igvt_port_present_p(gt_port vgt_port)
{
    char path[256];
    char c[12];
    FILE *f;
    int retval = 0;

    if (!igvt_is_valid_port_p(vgt_port)) {
	igvt_printf(IGVT_ERROR, "%s::Invalid vgt_port %s\n",
		    __func__, vgt_port);

        return (0);
    }

    snprintf(path, sizeof(path),
	     "%s/control/%s/presence",
	     VGT_KERNEL_PATH,
	     port_strings[vgt_port]);

    f = fopen(path, "r");

    if (!f) {
	igvt_printf(IGVT_ERROR, "%s::error opening %s: %s\n",
		    __func__, path, strerror(errno));

        return 0;
    }

    if (fscanf(f, "%s", c) != 1) {
        retval = 0;
    } else if (strcmp("present", c) != 0) {
        retval = 0;
    } else {
        retval = 1;
    }

    if (f)
        fclose(f);

    return retval;
}


/**
 * @brief Predicate that returns whether a port is hot-pluggable
 *
 * @param vmid The ID of the vm
 * @param vgt_port
 * @return 1 if plugged in
 */
int igvt_port_hotpluggable(unsigned int vmid, gt_port vgt_port)
{

    if (!igvt_enabled_p(vmid)) {
	igvt_printf(IGVT_ERROR, "%s::Invalid domain %d\n",
		    __func__, vmid);
	return 0;
    }

    switch (vgt_port) {

    /* the EDP port is not hotpluggable */
    case PORT_EDP:
        return 0;
    
    /* All other legal ports are hotpluggable */
    case PORT_B:
    case PORT_C:
    case PORT_D:
    case PORT_VGA:
        return 1;

    /* Not a legal port... */
    default:
	igvt_printf(IGVT_ERROR, "%s::Invalid vgt_port %s\n",
		    __func__, vgt_port);

        return 0;
    }

    return 0;
}

static int (*loggers[IGVT_NUM_LOGGERS])(const char *text);

/**
 * @brief Set error/warning loggers
 *
 * @param logger function to be called to log errors
 * @return previous logger function
 */
int (*igvt_set_warning_logger(int (*new_logger)(const char *text)))(const char *)
{
    int (*old_logger)(const char *text) = loggers[IGVT_WARNING];

    loggers[IGVT_WARNING] = new_logger;

    return old_logger;
}

int (*igvt_set_error_logger(int (*new_logger)(const char *text)))(const char *)
{
    int (*old_logger)(const char *text) = loggers[IGVT_ERROR];

    loggers[IGVT_WARNING] = new_logger;

    return old_logger;
}

static int
igvt_printf(igvt_log_type log_type, const char *format, ...)
{
    va_list arg;
    int done = 0;
    int (*logger)(const char *text) = loggers[log_type];

    if (logger) {
        char buffer[256];

        va_start (arg, format);
        done = vsnprintf (buffer, sizeof(buffer), format, arg);
        va_end (arg);

	logger(buffer);
    } else {
        va_start (arg, format);
        done = vfprintf(stdout, format, arg);
        va_end (arg);
    }

    return done;
}
