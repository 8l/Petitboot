
#include <string.h>

#include <talloc/talloc.h>
#include <process/process.h>

#include "discover-server.h"
#include "sysinfo.h"

static struct system_info *sysinfo;
static struct discover_server *server;

static const char *sysinfo_helper = PKG_LIBEXEC_DIR "/pb-sysinfo";

const struct system_info *system_info_get(void)
{
	return sysinfo;
}

void system_info_register_interface(unsigned int hwaddr_size, uint8_t *hwaddr,
		const char *name, bool link)
{
	struct interface_info *if_info;
	unsigned int i;

	for (i = 0; i < sysinfo->n_interfaces; i++) {
		bool changed = false;

		if_info = sysinfo->interfaces[i];

		if (if_info->hwaddr_size != hwaddr_size)
			continue;

		if (memcmp(if_info->hwaddr, hwaddr, hwaddr_size))
			continue;

		/* Found an existing interface. Notify clients on any name or
		 * link changes */
		if (strcmp(if_info->name, name)) {
			talloc_free(if_info->name);
			if_info->name = talloc_strdup(if_info, name);
			changed = true;
		}

		if (if_info->link != link) {
			if_info->link = link;
			changed = true;
		}

		if (changed)
			discover_server_notify_system_info(server, sysinfo);

		return;
	}

	if_info = talloc_zero(sysinfo, struct interface_info);
	if_info->hwaddr_size = hwaddr_size;
	if_info->hwaddr = talloc_memdup(if_info, hwaddr, hwaddr_size);
	if_info->name = talloc_strdup(if_info, name);
	if_info->link = link;

	sysinfo->n_interfaces++;
	sysinfo->interfaces = talloc_realloc(sysinfo, sysinfo->interfaces,
						struct interface_info *,
						sysinfo->n_interfaces);
	sysinfo->interfaces[sysinfo->n_interfaces - 1] = if_info;

	discover_server_notify_system_info(server, sysinfo);
}

void system_info_register_blockdev(const char *name, const char *uuid,
		const char *mountpoint)
{
	struct blockdev_info *bd_info;
	unsigned int i;

	for (i = 0; i < sysinfo->n_blockdevs; i++) {
		bd_info = sysinfo->blockdevs[i];

		if (strcmp(bd_info->name, name))
			continue;

		/* update the mountpoint and UUID, and we're done */
		talloc_free(bd_info->mountpoint);
		bd_info->uuid = talloc_strdup(bd_info, uuid);
		bd_info->mountpoint = talloc_strdup(bd_info, mountpoint);
		discover_server_notify_system_info(server, sysinfo);
		return;
	}

	bd_info = talloc_zero(sysinfo, struct blockdev_info);
	bd_info->name = talloc_strdup(bd_info, name);
	bd_info->uuid = talloc_strdup(bd_info, uuid);
	bd_info->mountpoint = talloc_strdup(bd_info, mountpoint);

	sysinfo->n_blockdevs++;
	sysinfo->blockdevs = talloc_realloc(sysinfo, sysinfo->blockdevs,
						struct blockdev_info *,
						sysinfo->n_blockdevs);
	sysinfo->blockdevs[sysinfo->n_blockdevs - 1] = bd_info;

	discover_server_notify_system_info(server, sysinfo);
}

static void system_info_set_identifier(struct system_info *info)
{
	struct process *process;
	int rc;
	const char *argv[] = {
		sysinfo_helper, NULL, NULL,
	};

	process = process_create(info);
	process->path = sysinfo_helper;
	process->argv = argv;
	process->keep_stdout = true;

	argv[1] = "--type";
	rc = process_run_sync(process);

	if (!rc) {
		info->type = talloc_strndup(info, process->stdout_buf,
				process->stdout_len);
	}

	argv[1] = "--id";
	rc = process_run_sync(process);

	if (!rc) {
		info->identifier = talloc_strndup(info, process->stdout_buf,
				process->stdout_len);
	}

	process_release(process);
}

void system_info_init(struct discover_server *s)
{
	server = s;
	sysinfo = talloc_zero(server, struct system_info);
	system_info_set_identifier(sysinfo);
}
