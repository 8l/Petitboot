#define _GNU_SOURCE

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "log/log.h"
#include "talloc/talloc.h"
#include "pb-protocol/pb-protocol.h"
#include "parser-conf.h"
#include "parser-utils.h"
#include "paths.h"

struct yaboot_state {
	struct boot_option *opt;
	const char *desc_image;
	char *desc_initrd;
	int found_suse;
	int globals_done;
	const char *const *known_names;
};

static void yaboot_finish(struct conf_context *conf)
{
	struct yaboot_state *state = conf->parser_info;

	if (!state->desc_image) {
		pb_log("%s: %s: no image found\n", __func__,
			conf->dc->device->id);
		return;
	}

	assert(state->opt);
	assert(state->opt->name);
	assert(state->opt->boot_args);

	state->opt->description = talloc_asprintf(state->opt, "%s %s %s",
		state->desc_image,
		(state->desc_initrd ? state->desc_initrd : ""),
		state->opt->boot_args);

	talloc_free(state->desc_initrd);
	state->desc_initrd = NULL;

	conf_strip_str(state->opt->boot_args);
	conf_strip_str(state->opt->description);

	/* opt is persistent, so must be associated with device */

	device_add_boot_option(conf->dc->device, state->opt);
	state->opt = talloc_zero(conf->dc->device, struct boot_option);
	state->opt->boot_args = talloc_strdup(state->opt, "");
}

static void yaboot_process_pair(struct conf_context *conf, const char *name,
		char *value)
{
	struct yaboot_state *state = conf->parser_info;

	/* fixup for bare values */

	if (!name)
		name = value;

	if (!state->globals_done && conf_set_global_option(conf, name, value))
		return;

	if (!conf_param_in_list(state->known_names, name))
		return;

	state->globals_done = 1;

	/* image */

	if (streq(name, "image")) {
		if (state->opt->boot_image_file)
			yaboot_finish(conf);

		state->opt->boot_image_file = resolve_path(state->opt, value,
			conf->dc->device_path);
		state->desc_image = talloc_strdup(state->opt, value);
		return;
	}

	if (streq(name, "image[32bit]") || streq(name, "image[64bit]")) {
		state->found_suse = 1;

		if (state->opt->boot_image_file)
			yaboot_finish(conf);

		if (*value == '/') {
			state->opt->boot_image_file = resolve_path(state->opt,
				value, conf->dc->device_path);
			state->desc_image = talloc_strdup(state->opt, value);
		} else {
			char *s;
			asprintf(&s, "/suseboot/%s", value);
			state->opt->boot_image_file = resolve_path(state->opt,
				s, conf->dc->device_path);
			state->desc_image = talloc_strdup(state->opt, s);
			free(s);
		}

		return;
	}

	if (!state->opt->boot_image_file) {
		pb_log("%s: unknown name: %s\n", __func__, name);
		return;
	}

	/* initrd */

	if (streq(name, "initrd")) {
		if (!state->found_suse || (*value == '/')) {
			state->opt->initrd_file = resolve_path(state->opt,
				value, conf->dc->device_path);
			state->desc_initrd = talloc_asprintf(state, "initrd=%s",
				value);
		} else {
			char *s;
			asprintf(&s, "/suseboot/%s", value);
			state->opt->initrd_file = resolve_path(state->opt,
				s, conf->dc->device_path);
			state->desc_initrd = talloc_asprintf(state, "initrd=%s",
				s);
			free(s);
		}
		return;
	}

	/* label */

	if (streq(name, "label")) {
		state->opt->id = talloc_asprintf(state->opt, "%s#%s",
			conf->dc->device->id, value);
		state->opt->name = talloc_strdup(state->opt, value);
		return;
	}

	/* args */

	if (streq(name, "append")) {
		state->opt->boot_args = talloc_asprintf_append(
			state->opt->boot_args, "%s ", value);
		return;
	}

	if (streq(name, "initrd-size")) {
		state->opt->boot_args = talloc_asprintf_append(
			state->opt->boot_args, "ramdisk_size=%s ", value);
		return;
	}

	if (streq(name, "literal")) {
		if (*state->opt->boot_args) {
			pb_log("%s: literal over writes '%s'\n", __func__,
				state->opt->boot_args);
			talloc_free(state->opt->boot_args);
		}
		talloc_asprintf(state->opt, "%s ", value);
		return;
	}

	if (streq(name, "ramdisk")) {
		state->opt->boot_args = talloc_asprintf_append(
			state->opt->boot_args, "ramdisk=%s ", value);
		return;
	}

	if (streq(name, "read-only")) {
		state->opt->boot_args = talloc_asprintf_append(
			state->opt->boot_args, "ro ");
		return;
	}

	if (streq(name, "read-write")) {
		state->opt->boot_args = talloc_asprintf_append(
			state->opt->boot_args, "rw ");
		return;
	}

	if (streq(name, "root")) {
		state->opt->boot_args = talloc_asprintf_append(
			state->opt->boot_args, "root=%s ", value);
		return;
	}

	pb_log("%s: unknown name: %s\n", __func__, name);
}

static struct conf_global_option yaboot_global_options[] = {
	{ .name = "boot" },
	{ .name = "initrd" },
	{ .name = "partition" },
	{ .name = "video" },
	{ .name = NULL },
};

static const char *const yaboot_conf_files[] = {
	"/yaboot.conf",
	"/yaboot.cnf",
	"/etc/yaboot.conf",
	"/etc/yaboot.cnf",
	"/suseboot/yaboot.cnf",
	"/YABOOT.CONF",
	"/YABOOT.CNF",
	"/ETC/YABOOT.CONF",
	"/ETC/YABOOT.CNF",
	"/SUSEBOOT/YABOOT.CNF",
	NULL
};

static const char *yaboot_known_names[] = {
	"append",
	"image",
	"image[64bit]", /* SUSE extension */
	"image[32bit]", /* SUSE extension */
	"initrd",
	"initrd-size",
	"label",
	"literal",
	"ramdisk",
	"read-only",
	"read-write",
	"root",
	NULL
};

static int yaboot_parse(struct discover_context *dc)
{
	struct conf_context *conf;
	struct yaboot_state *state;
	int rc;

	conf = talloc_zero(dc, struct conf_context);

	if (!conf)
		return -1;

	conf->dc = dc;
	conf->global_options = yaboot_global_options,
	conf_init_global_options(conf);
	conf->conf_files = yaboot_conf_files,
	conf->process_pair = yaboot_process_pair;
	conf->finish = yaboot_finish;
	conf->parser_info = state = talloc_zero(conf, struct yaboot_state);

	state->known_names = yaboot_known_names;

	/* opt is persistent, so must be associated with device */

	state->opt = talloc_zero(conf->dc->device, struct boot_option);
	state->opt->boot_args = talloc_strdup(state->opt, "");

	rc = conf_parse(conf);

	talloc_free(conf);
	return rc;
}

define_parser(yaboot, 99, yaboot_parse);
