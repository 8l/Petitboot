
#include <assert.h>

#include <talloc/talloc.h>

#include <discover/resource.h>
#include <discover/parser.h>
#include <discover/parser-utils.h>

#include "grub2.h"
#include "parser.h"
#include "lexer.h"

static const char *const grub2_conf_files[] = {
	"/grub.cfg",
	"/menu.lst",
	"/grub/grub.cfg",
	"/grub2/grub.cfg",
	"/grub/menu.lst",
	"/boot/grub/grub.cfg",
	"/boot/grub2/grub.cfg",
	"/boot/grub/menu.lst",
	"/GRUB.CFG",
	"/MENU.LST",
	"/GRUB/GRUB.CFG",
	"/GRUB2/GRUB.CFG",
	"/GRUB/MENU.LST",
	"/BOOT/GRUB/GRUB.CFG",
	"/BOOT/GRUB/MENU.LST",
	NULL
};

struct grub2_resource_info {
	struct grub2_root *root;
	char *path;
};

/* we use slightly different resources for grub2 */
struct resource *create_grub2_resource(void *ctx,
		struct discover_device *orig_device,
		struct grub2_root *root, const char *path)
{
	struct grub2_resource_info *info;
	struct resource *res;

	res = talloc(ctx, struct resource);

	if (root) {
		info = talloc(res, struct grub2_resource_info);
		info->root = root;
		talloc_reference(info, root);
		info->path = talloc_strdup(info, path);

		res->resolved = false;
		res->info = info;

	} else
		resolve_resource_against_device(res, orig_device, path);

	return res;
}

bool resolve_grub2_resource(struct device_handler *handler,
		struct resource *res)
{
	struct grub2_resource_info *info = res->info;
	struct discover_device *dev;

	assert(!res->resolved);

	dev = device_lookup_by_uuid(handler, info->root->uuid);

	if (!dev)
		return false;

	resolve_resource_against_device(res, dev, info->path);
	talloc_free(info);

	return true;
}

static int grub2_parse(struct discover_context *dc, char *buf, int len)
{
	struct grub2_parser *parser;

	parser = grub2_parser_create(dc);

	grub2_parser_parse(parser, buf, len);

	talloc_free(parser);

	return 1;
}

static struct parser grub2_parser = {
	.name			= "grub2",
	.method			= CONF_METHOD_LOCAL_FILE,
	.parse			= grub2_parse,
	.filenames		= grub2_conf_files,
	.resolve_resource	= resolve_grub2_resource,
};

register_parser(grub2_parser);