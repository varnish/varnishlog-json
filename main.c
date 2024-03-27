#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#define VOPT_DEFINITION
#define VOPT_INC "varnishlog-json_options.h"

#include "miniobj.h"
#include "vdef.h"
#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vapi/voptget.h"
#include "vas.h"
#include <varnish/vsb.h>
#include <varnish/vut.h>

bool pretty;
bool arrays;
struct VUT *vut;
struct vsb *vsb;

void
tok_init(const char **p, const char *s)
{
	AN(s);
	AN(p);
	*p = s;
}

int
tok_next(const char **p, struct vsb *vsb)
{
	const char *b;
	const char *e;
	AN(p);
	AN(*p);

	b = *p;
	VSB_clear(vsb);
	while (b && (isspace(*b)))
		b++;
	if (!*b)
		return(0);
	e = b + 1;
	while (*e && !(isspace(*e)))
		e++;
	VSB_bcat(vsb, b, e - b);
	VSB_finish(vsb);
	*p = e;
	return(1);
}

void
replaceString(cJSON *object, const char *field, const char *value)
{
	if (cJSON_GetObjectItemCaseSensitive(object, field)) {
		cJSON *temp_s = cJSON_CreateString(value);
		AN(temp_s);
		cJSON_ReplaceItemInObjectCaseSensitive(object, field, temp_s);
		temp_s = NULL;
	} else {
		cJSON_AddStringToObject(object, field, value);
	}
}

void
add_hdr(const char *s, cJSON *hdrs, struct vsb *vsb)
{
	const char *c;

	VSB_clear(vsb);

	// lowercase the header name, stash it
	// also, allow for the first char to be ':'
	for (c = s; *c && (s == c || *c != ':'); c++ )
		VSB_putc(vsb, tolower(*c));

	VSB_finish(vsb);

	// find the beginning of the header value
	*c ? c++ : NULL;
	while (isspace(*c))
		c++;

	cJSON *temp_s = cJSON_CreateString(c);
	AN(temp_s);
	cJSON *temp_a = cJSON_GetObjectItemCaseSensitive(hdrs, VSB_data(vsb));
	if (!temp_a) {
		temp_a = cJSON_AddArrayToObject(hdrs, VSB_data(vsb));
	}
	AN(temp_a);
	AN(cJSON_AddItemToArray(temp_a, temp_s));
}

// log, openout, rotateout and flushout are from varnishlog.c
static struct log
{
	/* Options */
	int	     a_opt;
	char	    *w_arg;
 
	/* State */
	FILE	    *fo;
} LOG;

static void
openout(int append)
{
	AN(LOG.w_arg);
	if (!strcmp(LOG.w_arg, "-"))
		LOG.fo = stdout;
	else
		LOG.fo = fopen(LOG.w_arg, append ? "a" : "w");
	if (LOG.fo == NULL)
		VUT_Error(vut, 2, "Cannot open output file (%s)", strerror(errno));
	vut->dispatch_priv = LOG.fo;
}

static int v_matchproto_(VUT_cb_f)
rotateout(struct VUT *v)
{
	
	assert(v == vut);
	AN(LOG.w_arg);
	AN(LOG.fo);
	(void)fclose(LOG.fo);
	openout(1);
	AN(LOG.fo);
	return (0);
}
	
static int v_matchproto_(VUT_cb_f)    
flushout(struct VUT *v)
{
		
	assert(v == vut);
	AN(LOG.fo);
	if (fflush(LOG.fo))
		return (-5);
	return (0);
}

static int process_group(struct VSL_data *vsl,
		struct VSL_transaction * const trans[], void *priv)
{
	int i;
	bool req_done, resp_done;
	const char *c;
	const char *data;
	enum VSL_tag_e tag;
	cJSON *transaction_array = NULL;
	cJSON *transaction = NULL;

	(void)priv;

	transaction_array = cJSON_CreateArray();

	// go through all transaction
	for (struct VSL_transaction *t = trans[0]; t != NULL; t = *++trans) {
		char *side;
		char *handling = "incomplete";

		switch (t->type) {
		case VSL_t_bereq:
			side = "backend";
			break;
		case VSL_t_req:
			side = "client";
			break;
		default:
			continue;
		}

		transaction = cJSON_CreateObject();
		cJSON_AddItemToArray(transaction_array, transaction);
		cJSON_AddStringToObject(transaction, "side", side);

		cJSON *req = cJSON_AddObjectToObject(transaction, "req");
		cJSON *req_hdrs = cJSON_AddObjectToObject(req, "headers");
		cJSON *req_hdrs_tmp = cJSON_AddObjectToObject(req, "headers_tmp");

		cJSON *resp = cJSON_AddObjectToObject(transaction, "resp");
		cJSON *resp_hdrs = cJSON_AddObjectToObject(resp, "headers");
		cJSON *resp_hdrs_tmp = cJSON_AddObjectToObject(resp, "headers_tmp");

		cJSON *timeline = cJSON_AddArrayToObject(transaction, "timeline");

		req_done = resp_done = false;

		// loop until told otherwise
		while (1) {
			// move the cursor to the next record
			i = VSL_Next(t->c);
			if (i < 0) {		// error occured
				cJSON_Delete(transaction_array);
				return (i);
			}
			if (i == 0)		// we've reached the last record
				break;
			// -i/-I check, notably
			if (!VSL_Match(vsl, t->c)) {
				continue;
			}

			const uint32_t *p = t->c->rec.ptr;
			// extract various fields from the record
			tag = VSL_TAG(p);
			data = VSL_CDATA(p);

			switch (tag) {
			case SLT_Begin:
				VSB_clear(vsb);
				VSB_printf(vsb, "%ld", t->vxid);
				VSB_finish(vsb);
				cJSON_AddStringToObject(transaction, "id", VSB_data(vsb));
				break;

			case SLT_VCL_call:
				if (t->type == VSL_t_req) {
					req_done = true;
				} else if (!strcmp(data, "BACKEND_RESPONSE") || !strcmp(data, "BACKEND_ERROR")) {
					resp_done = true;
				}

				// don't overwrite handling if we're already erroring
				if (!strcmp(handling, "fail") || !strcmp(handling, "abandon")) {
					break;
				} else if (!strcmp(data, "HIT")) {
					handling = "hit";
				} else if (!strcmp(data, "MISS")) {
					handling = "miss";
				} else if (!strcmp(data, "PASS")) {
					handling = "pass";
				} else if (!strcmp(data, "SYNTH")) {
					handling = "synth";
				} else if (!strcmp(data, "BACKEND_RESPONSE")) {
					handling = "fetched";
				} else if (!strcmp(data, "BACKEND_ERROR")) {
					handling = "error";
				} else if (!strcmp(data, "SYNTH")) {
					handling = "synth";
				}
				break;

			case SLT_VCL_return:
				if (t->type == VSL_t_bereq &&
				   (!strcmp(data, "fetch") || !strcmp(data, "error"))) {
					req_done = true;
				}
				if (!strcmp(data, "fail")) {
					handling = "fail";
				} else if (!strcmp(data, "abandon")) {
					handling = "abandon";
				}
				break;

#define save_data(tag, cond, obj, field) 				\
	case SLT_ ## tag:						\
		if (cond)				\
			replaceString(obj, field, data);		\
		break;							\

			save_data(ReqMethod, !req_done, req, "method");
			save_data(BereqMethod, !req_done, req, "method");

			save_data(ReqProtocol, !req_done, req, "proto");
			save_data(BereqProtocol, !req_done, req, "proto");

			save_data(ReqURL, !req_done, req, "url");
			save_data(BereqURL, !req_done, req, "url");

			save_data(RespReason, !resp_done, resp, "reason");
			save_data(BerespReason, !resp_done, resp, "reason");

			save_data(RespProtocol, !resp_done, resp, "proto");
			save_data(BerespProtocol, !resp_done, resp, "proto");

			save_data(VCL_use, true, transaction, "vcl");

			save_data(FetchError, true, transaction, "error");

			save_data(Storage, true, transaction, "storage");

			case SLT_RespStatus:
				/* passthrough */
			case SLT_BerespStatus: {
				double status = strtod(data, NULL);
				// Varnish won't accept those, we shouldn't either
				assert(status > 0);
				assert(status < 1000);
				assert(status == round(status));
				if (!resp_done)
					cJSON_AddNumberToObject(resp, "status", status);
				break;
			}
			case SLT_ReqHeader:
				/* passthrough */
			case SLT_BereqHeader:
				if (!req_done)
					cJSON_AddBoolToObject(req_hdrs_tmp, data, 1);
				break;

			case SLT_RespHeader:
				/* passthrough */
			case SLT_BerespHeader:
				if (!resp_done)
					cJSON_AddBoolToObject(resp_hdrs_tmp, data, 1);
				break;

			case SLT_ReqUnset:
				/* passthrough */
			case SLT_BereqUnset:
				if (!req_done)
					cJSON_DeleteItemFromObject(req_hdrs_tmp, data);
				break;

			case SLT_RespUnset:
				/* passthrough */
			case SLT_BerespUnset:
				if (!resp_done)
					cJSON_DeleteItemFromObject(resp_hdrs_tmp, data);
				break;

			case SLT_ReqAcct:
				/* passthrough */
			case SLT_BereqAcct: {
				int req_hdr_len, req_body_len, resp_hdr_len, resp_body_len, l1, l2;
				// we don't care about l1 and l2, but we need l1 to get to resp_hdr_len
				// and resp_body_len, so we might as well just read everything as a
				// sanity check
				assert(6 == sscanf(data, "%i %i %i %i %i %i", &req_hdr_len, &req_body_len, &l1, &resp_hdr_len, &resp_body_len, &l2));
				cJSON_AddNumberToObject(req, "hdrBytes", req_hdr_len);
				cJSON_AddNumberToObject(req, "bodyBytes", req_body_len);
				cJSON_AddNumberToObject(resp, "hdrBytes", resp_hdr_len);
				cJSON_AddNumberToObject(resp, "bodyBytes", resp_body_len);
				break;
			}

			case SLT_VCL_Log: {
				cJSON *temp_s = cJSON_CreateString(data);
				AN(temp_s);
				cJSON *temp_a = cJSON_GetObjectItemCaseSensitive(transaction, "logs");
				if (!temp_a) {
					temp_a = cJSON_AddArrayToObject(transaction, "logs");
				}
				AN(temp_a);
				AN(cJSON_AddItemToArray(temp_a, temp_s));

				break;
			}

			case SLT_Link: {
				cJSON *links = cJSON_GetObjectItemCaseSensitive(transaction, "links");
				if (!links) {
					links = cJSON_AddArrayToObject(transaction, "links");
				}
				cJSON *t = cJSON_CreateObject();
				tok_init(&c, data);
				AN(tok_next(&c, vsb));
				cJSON_AddStringToObject(t, "type", VSB_data(vsb));
				AN(tok_next(&c, vsb));
				cJSON_AddStringToObject(t, "id", VSB_data(vsb));
				AN(tok_next(&c, vsb));
				cJSON_AddStringToObject(t, "reason", VSB_data(vsb));

				cJSON_AddItemToArray(links, t);
				break;
			}
			case SLT_Timestamp:
				VSB_clear(vsb);
				cJSON *t = cJSON_CreateObject();

				tok_init(&c, data);
				AN(tok_next(&c, vsb));
				char *p = VSB_len(vsb) ? VSB_data(vsb) + VSB_len(vsb) - 1 : NULL;
				if (p != NULL) {
					assert(*p == ':');
					*p = '\0';
				}
				cJSON_AddStringToObject(t, "name", VSB_data(vsb));

				// float conversion is a mess, so we just grab the timestamp as-is,
				// copy it to a buffer, null-terminated and pass it raw to cJSON.
				AN(tok_next(&c, vsb));
				cJSON_AddRawToObject(t, "timestamp", VSB_data(vsb));

				cJSON_AddItemToArray(timeline, t);
				break;

			case SLT_BackendOpen: {
				cJSON *backend = cJSON_AddObjectToObject(transaction, "backend");

				tok_init(&c, data);
				//// skip the file descriptor
				AN(tok_next(&c, vsb));
				AN(tok_next(&c, vsb));
				cJSON_AddStringToObject(backend, "name", VSB_data(vsb));
				AN(tok_next(&c, vsb));
				cJSON_AddStringToObject(backend, "rAddr", VSB_data(vsb));
				AN(tok_next(&c, vsb));
				cJSON_AddRawToObject(backend, "rPort", VSB_data(vsb));
				AN(tok_next(&c, vsb));
				cJSON_AddBoolToObject(backend, "connReused", strcmp(VSB_data(vsb), "reused") ? 0 : 1);
				break;
			}

			case SLT_ReqStart: {
				cJSON *client = cJSON_AddObjectToObject(transaction, "client");
				tok_init(&c, data);
				AN(tok_next(&c, vsb));
				cJSON_AddStringToObject(client, "rAddr", VSB_data(vsb));
				AN(tok_next(&c, vsb));
				cJSON_AddRawToObject(client, "rPort", VSB_data(vsb));
				AN(tok_next(&c, vsb));
				cJSON_AddStringToObject(client, "sock", VSB_data(vsb));
				break;
			}
			case SLT_End:
				if (!strcmp(data, "synth"))
					replaceString(transaction,
					    "error", "truncated log");
				break;
			default:
				break;
			}
		}

		// commit handling
		cJSON_AddStringToObject(transaction, "handling", handling);

		// we still need to go through the flattened headers
		// to put them in an object
		cJSON *h = NULL;

		cJSON_ArrayForEach(h, req_hdrs_tmp)
			add_hdr(h->string, req_hdrs, vsb);
		cJSON_DeleteItemFromObject(req, "headers_tmp");

		cJSON_ArrayForEach(h, resp_hdrs_tmp)
			add_hdr(h->string, resp_hdrs, vsb);
		cJSON_DeleteItemFromObject(resp, "headers_tmp");

		if (arrays) {
		} else {
			break;
		}
	}

	if (!cJSON_GetArraySize(transaction_array))
		return (0);

	cJSON *obj = arrays ? transaction_array : cJSON_GetArrayItem(transaction_array, 0);
	AN(obj);

	// print the resulting object
	char *s;
	if (pretty)
		s = cJSON_Print(obj);
	else
		s = cJSON_PrintUnformatted(obj);
	printf("%s\n", s);
	free(s);
	cJSON_Delete(transaction_array);

	// all went well, get out
	return (0);
}

int main(int argc, char **argv)
{
	int opt;
	bool bc_set = false;
	vut = VUT_InitProg(argc, argv, &vopt_spec);

	while ((opt = getopt(argc, argv, vopt_spec.vopt_optstring)) != -1) {
		switch (opt) {
		case 'a':
			LOG.a_opt = 1;
			break;
		case 'b': /* backend mode */
			/* passthrough */
		case 'c': /* client mode */
			bc_set = true;
			/* fallthrough */
			AN(VUT_Arg(vut, opt, NULL));
			break;
		case 'g':
			if (!strcmp("vxid", optarg))
				arrays = false;
			else if (!strcmp("request", optarg))
				arrays = true;
			else {
				printf("Error: -g only supports \"vxid\" and \"request\"\n\n");
				VUT_Usage(vut, &vopt_spec, 1);
			}
			arrays = strcmp("vxid", optarg);
			VUT_Arg(vut, opt, optarg);
			break;
		case 'h':
			VUT_Usage(vut, &vopt_spec, 0);
			break;
		case 'p':
			pretty = 1;
			break;
		case 'w':
			REPLACE(LOG.w_arg, optarg);
			break;
		default:
			if (!VUT_Arg(vut, opt, optarg))
				VUT_Usage(vut, &vopt_spec, 1);
		}

	}

	/* default is client mode: */
	if (!bc_set && !arrays)
		AN(VUT_Arg(vut, 'c', NULL));

	if (optind != argc)
		VUT_Usage(vut, &vopt_spec, 1);

	if (vut->D_opt && !LOG.w_arg)
		VUT_Error(vut, 1, "Missing -w option");

	if (vut->D_opt && !strcmp(LOG.w_arg, "-"))
		VUT_Error(vut, 1, "Daemon cannot write to stdout");

	if (LOG.w_arg) {
		openout(LOG.a_opt);
		AN(LOG.fo);
		if (vut->D_opt)
			vut->sighup_f = rotateout;
	} else
		LOG.fo = stdout;


	vut->dispatch_f = process_group;
	vut->idle_f = flushout;

	vsb = VSB_new_auto();;

	// prepare and loop forever (or until told to stop), then clean up
	VUT_Setup(vut);
	(void)VUT_Main(vut);
	VUT_Fini(&vut);

	VSB_destroy(&vsb);

	(void)flushout(NULL);

	return (0);
}
