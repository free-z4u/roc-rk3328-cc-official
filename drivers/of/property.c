/*
 * drivers/of/property.c - Procedures for accessing and interpreting
 *			   Devicetree properties and graphs.
 *
 * Initially created by copying procedures from drivers/of/base.c. This
 * file contains the OF property as well as the OF graph interface
 * functions.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com
 *
 *  Adapted for sparc and sparc64 by David S. Miller davem@davemloft.net
 *
 *  Reconsolidated from arch/x/kernel/prom.c by Stephen Rothwell and
 *  Grant Likely.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt)	"OF: " fmt

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/string.h>

#include "of_private.h"

static void of_fwnode_get(struct fwnode_handle *fwnode)
{
	of_node_get(to_of_node(fwnode));
}

static void of_fwnode_put(struct fwnode_handle *fwnode)
{
	of_node_put(to_of_node(fwnode));
}

static bool of_fwnode_device_is_available(struct fwnode_handle *fwnode)
{
	return of_device_is_available(to_of_node(fwnode));
}

static bool of_fwnode_property_present(struct fwnode_handle *fwnode,
				       const char *propname)
{
	return of_property_read_bool(to_of_node(fwnode), propname);
}

static int of_fwnode_property_read_int_array(struct fwnode_handle *fwnode,
					     const char *propname,
					     unsigned int elem_size, void *val,
					     size_t nval)
{
	struct device_node *node = to_of_node(fwnode);

	if (!val)
		return of_property_count_elems_of_size(node, propname,
						       elem_size);

	switch (elem_size) {
	case sizeof(u8):
		return of_property_read_u8_array(node, propname, val, nval);
	case sizeof(u16):
		return of_property_read_u16_array(node, propname, val, nval);
	case sizeof(u32):
		return of_property_read_u32_array(node, propname, val, nval);
	case sizeof(u64):
		return of_property_read_u64_array(node, propname, val, nval);
	}

	return -ENXIO;
}

static int of_fwnode_property_read_string_array(struct fwnode_handle *fwnode,
						const char *propname,
						const char **val, size_t nval)
{
	struct device_node *node = to_of_node(fwnode);

	return val ?
		of_property_read_string_array(node, propname, val, nval) :
		of_property_count_strings(node, propname);
}

static struct fwnode_handle *of_fwnode_get_parent(struct fwnode_handle *fwnode)
{
	return of_fwnode_handle(of_get_parent(to_of_node(fwnode)));
}

static struct fwnode_handle *
of_fwnode_get_next_child_node(struct fwnode_handle *fwnode,
			      struct fwnode_handle *child)
{
	return of_fwnode_handle(of_get_next_available_child(to_of_node(fwnode),
							    to_of_node(child)));
}

static struct fwnode_handle *
of_fwnode_get_named_child_node(struct fwnode_handle *fwnode,
			       const char *childname)
{
	struct device_node *node = to_of_node(fwnode);
	struct device_node *child;

	for_each_available_child_of_node(node, child)
		if (!of_node_cmp(child->name, childname))
			return of_fwnode_handle(child);

	return NULL;
}

static int
of_fwnode_get_reference_args(struct fwnode_handle *fwnode,
			     const char *prop, const char *nargs_prop,
			     unsigned int nargs, unsigned int index,
			     struct fwnode_reference_args *args)
{
	struct of_phandle_args of_args;
	unsigned int i;
	int ret;

	if (nargs_prop)
		ret = of_parse_phandle_with_args(to_of_node(fwnode), prop,
						 nargs_prop, index, &of_args);
	else
		ret = of_parse_phandle_with_fixed_args(to_of_node(fwnode), prop,
						       nargs, index, &of_args);
	if (ret < 0)
		return ret;
	if (!args)
		return 0;

	args->nargs = of_args.args_count;
	args->fwnode = of_fwnode_handle(of_args.np);

	for (i = 0; i < NR_OF_FWNODE_REFERENCE_ARGS; i++)
		args->args[i] = i < of_args.args_count ? of_args.args[i] : 0;

	return 0;
}

static struct fwnode_handle *
of_fwnode_graph_get_next_endpoint(struct fwnode_handle *fwnode,
				  struct fwnode_handle *prev)
{
	return of_fwnode_handle(of_graph_get_next_endpoint(to_of_node(fwnode),
							   to_of_node(prev)));
}

static struct fwnode_handle *
of_fwnode_graph_get_remote_endpoint(struct fwnode_handle *fwnode)
{
	return of_fwnode_handle(of_parse_phandle(to_of_node(fwnode),
						 "remote-endpoint", 0));
}

static struct fwnode_handle *
of_fwnode_graph_get_port_parent(struct fwnode_handle *fwnode)
{
	struct device_node *np;

	/* Get the parent of the port */
	np = of_get_next_parent(to_of_node(fwnode));
	if (!np)
		return NULL;

	/* Is this the "ports" node? If not, it's the port parent. */
	if (of_node_cmp(np->name, "ports"))
		return of_fwnode_handle(np);

	return of_fwnode_handle(of_get_next_parent(np));
}

static int of_fwnode_graph_parse_endpoint(struct fwnode_handle *fwnode,
					  struct fwnode_endpoint *endpoint)
{
	struct device_node *node = to_of_node(fwnode);
	struct device_node *port_node = of_get_parent(node);

	endpoint->local_fwnode = fwnode;

	of_property_read_u32(port_node, "reg", &endpoint->port);
	of_property_read_u32(node, "reg", &endpoint->id);

	of_node_put(port_node);

	return 0;
}

const struct fwnode_operations of_fwnode_ops = {
	.get = of_fwnode_get,
	.put = of_fwnode_put,
	.device_is_available = of_fwnode_device_is_available,
	.property_present = of_fwnode_property_present,
	.property_read_int_array = of_fwnode_property_read_int_array,
	.property_read_string_array = of_fwnode_property_read_string_array,
	.get_parent = of_fwnode_get_parent,
	.get_next_child_node = of_fwnode_get_next_child_node,
	.get_named_child_node = of_fwnode_get_named_child_node,
	.get_reference_args = of_fwnode_get_reference_args,
	.graph_get_next_endpoint = of_fwnode_graph_get_next_endpoint,
	.graph_get_remote_endpoint = of_fwnode_graph_get_remote_endpoint,
	.graph_get_port_parent = of_fwnode_graph_get_port_parent,
	.graph_parse_endpoint = of_fwnode_graph_parse_endpoint,
};
