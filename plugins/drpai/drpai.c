
#include <linux/drpai.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <libwebsockets.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdbool.h>

#include <sys/mman.h>

#include "drpai.h"
#include "models.h"

#define min(a, b) ((a) > (b) ? (b) : (a))

#ifndef DRPAI_MODELS_ROOT_DIR
#error "Must define DRPAI_MODELS_ROOT_DIR, for the location of the DRPAI models"
#endif

#define ADDRMAP_INTM_TXT_FILTER	"addrmap_intm.txt"

struct drpai_param_map {
	const char *key;   /* key in the ADDRMAP_INTM_TXT file*/
	int idx;           /* DRPAPI_INDEX_ in the kernel driver */
	const char *fname_filter; /* filename filter the file that needs to be loaded for this DRPAPI_INDEX_ */
};

struct drpai {
	drpai_data_t input_data[DRPAI_INDEX_NUM];
	drpai_data_t base;
	int fd;
	struct {
		const struct drpai_model_ops *ops;
		void *priv;
	} model;
	struct {
		int fd;
		uint32_t base;
		uint32_t input;
		void *usrptr;
	} udmabuf;
};

static const struct drpai_param_map drpai_param_map[] = {
	{ "drp_config", DRPAI_INDEX_DRP_CFG,    "drpcfg.mem" },
	{ "desc_aimac", DRPAI_INDEX_AIMAC_DESC, "aimac_desc.bin" },
	{ "desc_drp",   DRPAI_INDEX_DRP_DESC,   "drp_desc.bin" },
	{ "drp_param",  DRPAI_INDEX_DRP_PARAM,  "drp_param.bin" },
	{ "weight",     DRPAI_INDEX_WEIGHT,     "weight.dat" },
	{ "data_in",    DRPAI_INDEX_INPUT,      NULL },
	{ "data_out",   DRPAI_INDEX_OUTPUT,     NULL },
	{ }
};

static bool str_endswith(const char *str, const char *substr)
{
	const char *s = strstr(str, substr);
	return s && s[strlen(substr)] == '\0';
}

static int drpai_assign(struct drpai *d, const drpai_data_t* data)
{
	int rc;
	if (!d)
		return -EINVAL;

	rc = ioctl(d->fd, DRPAI_ASSIGN, data);
	return rc ? -errno : 0;
}

static int drpai_read_addrmap_intm_txt(struct drpai *d, const char *dir, const char *fname)
{
	drpai_data_t *addrs = d->input_data;
	char full_path[768];
	char *line = NULL;
	size_t len = 0;
	uint32_t base;
	FILE *fp;
	int i;

	snprintf(full_path, sizeof(full_path), "%s/%s", dir, fname);

	fp = fopen(full_path, "r");
	if (!fp)
		return -errno;

	while (getline(&line, &len, fp) != -1) {
		const char *tok, *skey = NULL, *saddr = NULL, *ssize = NULL;
		char *rest = line;

		while ((tok = strtok_r(rest, " ", &rest))) {
			if (skey == NULL)
				skey = tok;
			else if (saddr == NULL)
				saddr = tok;
			else if (ssize == NULL)
				ssize = tok;
			else
				break;
		}
		if (!ssize || !saddr || !skey)
			continue;

		for (i = 0; drpai_param_map[i].key; i++) {
			int idx;
			if (strcmp(drpai_param_map[i].key, skey))
				continue;

			idx = drpai_param_map[i].idx;
			addrs[idx].address = strtoul(saddr, NULL, 16);
			addrs[idx].size = strtoul(ssize, NULL, 16);
			break;
		}
	}

	fclose(fp);
	free(line);

	/* A bit of magic to support DRP AI v1 absolute addresses,
	 * and v2 relative addresses; if the 'data_in' address is non-zero,
	 * we take that as the base offset, we subtract it, and add the
	 * base address which the driver gave us.
	 * This works with v2 (only).
	 */
	base = addrs[DRPAI_INDEX_INPUT].address;
	for (i = 0; i < DRPAI_INDEX_NUM; i++) {
		addrs[i].address -= base;
		addrs[i].address += d->base.address;
	}

	return 0;
}

static int drpai_find_index(const char *name)
{
	int i;

	for (i = 0; drpai_param_map[i].key; i++) {
		const char *flt = drpai_param_map[i].fname_filter;
		if (!flt)
			continue;

		if (str_endswith(name, flt))
			return drpai_param_map[i].idx;
	}

	return -1;
}

static int drpai_load_file_to_mem(struct drpai *d, const char *model,
				  const char *fname, int idx)
{
	const drpai_data_t* addr = &d->input_data[idx];
	char buf[1024];
	struct stat st;
	size_t left_to_read;
	int fd, rc;

	rc = drpai_assign(d, addr);
	if (rc)
		return rc;

	snprintf(buf, sizeof(buf), "%s/%s/%s", DRPAI_MODELS_ROOT_DIR, model, fname);
	if (stat(buf, &st))
		return -errno;

	/* We have a small issue with 2 sources of truth.
	 * The file ADDRMAP_INTM_TXT_FILTER defines the addresses and sizes of the
	 * memory regions. But the same sizes are expected to be the same for the files
	 * that we load here. We can probably allow files that are larger than the
	 * entry defined (and we just read the amount we need). But for now,
	 * we'll be a bit strict about the file being the exact size as defined in the
	 * memory region.
	 */
	if (st.st_size != addr->size)
		return -EIO;

	fd = open(buf, O_RDONLY);
	if (fd < 0)
		return -errno;

	left_to_read = addr->size;
	while (left_to_read > 0) {
		size_t to_read = min(sizeof(buf), left_to_read);
		rc = read(fd, buf, to_read);
		if (rc < 0)
			goto err_close;
		left_to_read -= rc;

		rc = write(d->fd, buf, rc);
		if (rc < 0)
			goto err_close;
	}
	rc = -left_to_read;

err_close:
	close(fd);
	return rc;
}

static int drpai_load_model_config(struct drpai *d, const char *model)
{
	const struct drpai_model_ops *ops;
	char model_file[512];
	json_object *c;
	const char *s;
	struct stat st;
	int rc = 0;

	snprintf(model_file, sizeof(model_file), "%s/%s.json", DRPAI_MODELS_ROOT_DIR, model);

	if (stat(model_file, &st))
		return 0;

	c = json_object_from_file(model_file);
	if (!c)
		return -errno;

	s = json_object_get_string(json_object_object_get(c, "model_type"));
	if (!s) {
		rc = -EINVAL;
		goto out;
	}

	ops = d->model.ops = drpai_model_type_to_ops(s);
	if (!ops)
		goto out;

	if (!ops->init)
		goto out;

	d->model.priv = ops->init(c, &rc);
out:
	json_object_put(c);
	return rc;
}

static int __drpai_load_model(struct drpai *d, const char *model)
{
	char model_dir[512];
	struct dirent *ep;
	bool loaded_addmap;
	int rc = 0;
	DIR *dp;

	if (!d || !model)
		return -EINVAL;

	snprintf(model_dir, sizeof(model_dir), "%s/%s", DRPAI_MODELS_ROOT_DIR, model);

	dp = opendir(model_dir);
	if (!dp)
		return -errno;

	/* Do a pass first to try to load the address map file and any label files */
	loaded_addmap = false;
	while ((ep = readdir(dp))) {
		if (!str_endswith(ep->d_name, ADDRMAP_INTM_TXT_FILTER))
			continue;

		rc = drpai_read_addrmap_intm_txt(d, model_dir, ep->d_name);
		if (rc)
			goto out_closedir;
		loaded_addmap = true;
		break;
	}

	if (!loaded_addmap) {
		rc = -ENOENT;
		goto out_closedir;
	}

	rewinddir(dp);
	while ((ep = readdir(dp))) {
		int idx = drpai_find_index(ep->d_name);
		if (idx < 0)
			continue;

		rc = drpai_load_file_to_mem(d, model, ep->d_name, idx);
		if (rc)
			break;
	}

	rc = drpai_load_model_config(d, model);

out_closedir:
	closedir(dp);
	return rc;
}

int drpai_load_model(struct drpai *d, json_object *req)
{
	const char *model;
	json_object *jval;
	int rc;

	jval = json_object_object_get(req, "value");
	model = json_object_get_string(json_object_object_get(jval, "model"));
	if (!model) {
		rc = -EINVAL;
		goto err;
	}

	rc = __drpai_load_model(d, model);
err:
	if (rc) {
		const char *err = strerror(-rc);
		json_object_object_add(req, "error", json_object_new_string(err));
		lwsl_err("%s: %s\n", __func__, err);
	}

	return rc;
}

static int drpai_get_base_addr(struct drpai *d)
{
	int rc;
	if (!d)
		return -EINVAL;

	rc = ioctl(d->fd, DRPAI_GET_DRPAI_AREA, &d->base);
	return rc ? -errno : 0;
}

/**
 * This uses the u-dma-buf driver found here:
 *    https://github.com/ikwzm/udmabuf
 * For the current DRP AI driver, we need to know the
 * physical address of the where we place the input data,
 * as well as an mmap()-ed pointer to the same location,
 * to be able to memcpy() data.
 * This driver works fine, because most embedded systems
 * have less than 4 GB of RAM.
 *
 * FIXME: we could do a zero-copy version here using the same
 *        u-dma-buf, but since the u-dma-buf is not part of the
 *        standard/base kernel source code: ¯\_(ツ)_/¯
 */
static int drpai_get_input_mem_addr(struct drpai *d)
{
	/* FIXME: 'input_mem_offset is chosen arbitrarily at this point */
	const uint32_t input_mem_offset = 0x10000;
	char buf[32];
	int fd, rc;

	fd = open("/sys/class/u-dma-buf/udmabuf0/phys_addr", O_RDONLY);
	if (fd < 0)
		return -errno;

	rc = read(fd, buf, sizeof(buf));
	close(fd);
	if (rc < 0)
		return -errno;

	d->udmabuf.base = strtoul(buf, NULL, 16) & 0xffffffff;
	d->udmabuf.input = d->udmabuf.base + input_mem_offset;

	fd = open("/dev/udmabuf0", O_RDWR);
	if (fd < 0)
		return -errno;

	d->udmabuf.fd = fd;
	/* FIXME: size is hard-coded */
	d->udmabuf.usrptr = mmap(NULL, 640 * 480 * 2, PROT_READ | PROT_WRITE,
				 MAP_SHARED, fd, input_mem_offset);
	if (d->udmabuf.usrptr == MAP_FAILED) {
		close(fd);
		return -errno;
	}

	return 0;
}

struct drpai *drpai_init(int *err)
{
	struct drpai *d;
	int lerr = 0;

	d = calloc(1, sizeof(*d));
	if (!d) {
		lerr = -ENOMEM;
		goto err_assign_err_code;
	}

	d->fd = open("/dev/drpai0", O_RDWR);
	if (d->fd < 0) {
		lerr = -errno;
		goto err_free_work_data;
	}

	if ((lerr = drpai_get_base_addr(d)))
		goto err_close;

	if ((lerr = drpai_get_input_mem_addr(d)))
		goto err_close;

	return d;
err_close:
	close(d->fd);
err_free_work_data:
	free(d);
err_assign_err_code:
	if (err)
		*err = lerr;
	return NULL;
}

void drpai_free(struct drpai *d)
{
	const struct drpai_model_ops *ops;

	if (!d)
		return;

	close(d->udmabuf.fd);
	close(d->fd);
	ops = d->model.ops;
	if (ops && ops->cleanup)
		d->model.ops->cleanup(d->model.priv);

	free(d);
}

static int drpai_load(struct drpai *d, void *addr, size_t len)
{
	if (!d)
		return -EINVAL;

	/* FIXME: this provides the physical address */
	d->input_data[DRPAI_INDEX_INPUT].address = d->udmabuf.input;
	memcpy(d->udmabuf.usrptr, addr, len); /* copy the data */

	return 0;
}

static int drpai_start(struct drpai *d)
{
	if (!d)
		return -EINVAL;

	if (ioctl(d->fd, DRPAI_START, d->input_data))
		return -errno;

	return 0;
}

int drpai_is_running(struct drpai *d)
{
	drpai_status_t drp_status;
	int rc;

	if (!d)
		return 0;

	rc = ioctl(d->fd, DRPAI_GET_STATUS, &drp_status);
	if (rc)
		return (errno == EBUSY);

	return !drp_status.err && drp_status.status == DRPAI_STATUS_RUN;
}

static float *drpai_get_result_raw(struct drpai *d, int *err)
{
	const drpai_data_t* addr;
	float *output;
	size_t left_to_read, total_read;
	int lerr;

	if (!d) {
		lerr = -EINVAL;
		goto err_store;
	}

	addr = &d->input_data[DRPAI_INDEX_OUTPUT];
	if ((lerr = drpai_assign(d, addr)))
		goto err_store;

	output = malloc(addr->size);
	if (!output) {
		lerr = -ENOMEM;
		goto err_store;
	}

	total_read = 0;
	left_to_read = addr->size;
	while (left_to_read > 0) {
		int rc = read(d->fd, &output[total_read], left_to_read);
		if (rc == 0)
			break;
		if (rc < 0) {
			lerr = -errno;
			goto err_free;
		}

		left_to_read -= rc;
		total_read += rc;
	}

	return output;
err_free:
	free(output);
err_store:
	if (err)
		*err = lerr;
	return NULL;
}

const char *drpai_model_load_input(struct drpai *d, void *addr, int len)
{
	int rc;

	if (!d) {
		return "DRP AI object not initialized";
	}

	rc = drpai_load(d, addr, len);
	if (rc) {
		lwsl_warn("%s %d err %s\n", __func__, __LINE__, strerror(-rc));
		return "DRP AI load error";
	}

	return NULL;
}

const char *drpai_model_start(struct drpai *d)
{
	int rc;

	if (!d) {
		return "DRP AI object not initialized";
	}

	rc = drpai_start(d);
	if (rc) {
		lwsl_warn("%s %d err %s\n", __func__, __LINE__, strerror(-rc));
		return "DRP AI start error";
	}

	return NULL;
}

const char *drpai_model_get_result(struct drpai *d, json_object* result)
{
	const struct drpai_model_ops *ops;
	float *raw = NULL;
	int rc = 0;

	/* Yep, a bit weird to run DRP AI and not do any post-processing */
	ops = d->model.ops;
	if (!ops || !ops->postprocessing)
		return NULL;

	raw = drpai_get_result_raw(d, &rc);
	if (!raw || rc) {
		return "DRP AI error retrieving result";
	}

	/* FIXME: find a neat way to pass width, height */
	rc = ops->postprocessing(d->model.priv, raw, 640, 480, result);
	free(raw);
	if (rc) {
		return "DRP AI post-processing error";
	}

	return NULL;
}

static bool drpai_model_has_required_files(const char *name)
{
	bool required_files[DRPAI_INDEX_NUM];
	bool have_addrmap_file = false;
	struct dirent *ep;
	char path[512];
	DIR *dp;

	snprintf(path, sizeof(path), "%s/%s", DRPAI_MODELS_ROOT_DIR, name);

	dp = opendir(path);
	if (!dp)
		return false;

	while ((ep = readdir(dp))) {
		int idx;
		if (strcmp(ep->d_name, ".") == 0)
			continue;

		if (strcmp(ep->d_name, "..") == 0)
			continue;

		if (str_endswith(ep->d_name, ADDRMAP_INTM_TXT_FILTER)) {
			have_addrmap_file = true;
			continue;
		}

		idx = drpai_find_index(ep->d_name);
		if (idx < 0)
			continue;

		required_files[idx] = true;
	}

	closedir(dp);

	return required_files[DRPAI_INDEX_DRP_CFG] &&
		required_files[DRPAI_INDEX_AIMAC_DESC] &&
		required_files[DRPAI_INDEX_DRP_DESC] &&
		required_files[DRPAI_INDEX_DRP_PARAM] &&
		required_files[DRPAI_INDEX_WEIGHT] &&
		have_addrmap_file;
}

int drpai_models_get(json_object *req)
{
	json_object *val = NULL;
	json_object *models;
	struct dirent *ep;
	const char *err;
	DIR *dp;

	dp = opendir(DRPAI_MODELS_ROOT_DIR);
	if (!dp) {
		err = "could not open: " DRPAI_MODELS_ROOT_DIR;
		goto err;
	}

	val = json_object_new_object();
	if (!val) {
		err = "error allocating JSON object";
		goto err;
	}

	/* allocate JSON array for model names */
	models = json_object_new_array();
	if (!models) {
		err = "error creating JSON array for model names";
		goto err;
	}
	json_object_object_add(val, "models", models);

	while ((ep = readdir(dp))) {
		if (ep->d_type != DT_DIR)
			continue;

		if (strcmp(ep->d_name, ".") == 0)
			continue;

		if (strcmp(ep->d_name, "..") == 0)
			continue;

		if (!drpai_model_has_required_files(ep->d_name))
			continue;

		json_object_array_add(models, json_object_new_string(ep->d_name));
	}

	json_object_object_add(req, "value", val);

	closedir(dp);

	return 0;
err:
	json_object_put(val);
	closedir(dp);
	json_object_object_add(req, "error", json_object_new_string(err));
	lwsl_err("%s: %s\n", __func__, err);
	return -1;
}
