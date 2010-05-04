/*
 * Copyright (C) 2009 Frank Morgner
 *
 * This file is part of ccid.
 *
 * ccid is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * ccid is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ccid.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <opensc/log.h>

void print_usage(const char *app_name, const struct option options[],
	const char *option_help[])
{
    int i = 0;
    printf("Usage: %s [OPTIONS]\nOptions:\n", app_name);

    while (options[i].name) {
        char buf[40], tmp[5];
        const char *arg_str;

        /* Skip "hidden" options */
        if (option_help[i] == NULL) {
            i++;
            continue;
        }

        if (options[i].val > 0 && options[i].val < 128)
            sprintf(tmp, "-%c", options[i].val);
        else
            tmp[0] = 0;
        switch (options[i].has_arg) {
            case 1:
                arg_str = " <arg>";
                break;
            case 2:
                arg_str = " [arg]";
                break;
            default:
                arg_str = "";
                break;
        }
        sprintf(buf, "--%-13s%s%s", options[i].name, tmp, arg_str);
        if (strlen(buf) > 24) {
            printf("  %s\n", buf);
            buf[0] = '\0';
        }
        printf("  %-24s %s\n", buf, option_help[i]);
        i++;
    }
}

void parse_error(const char *app_name, const struct option options[],
        const char *option_help[], const char *optarg, int opt_ind)
{
    printf("Could not parse %s ('%s').\n", options[opt_ind].name, optarg);
    print_usage(app_name , options, option_help);
}

int initialize(int reader_id, const char *cdriver, int verbose,
        sc_context_t **ctx, sc_reader_t **reader)
{
    unsigned int i, reader_count;

    if (!ctx || !reader)
        return SC_ERROR_INVALID_ARGUMENTS;

    int r = sc_context_create(ctx, NULL);
    if (r < 0) {
        printf("Failed to create initial context: %s", sc_strerror(r));
        return r;
    }

    if (cdriver != NULL) {
        r = sc_set_card_driver(*ctx, cdriver);
        if (r < 0) {
            sc_error(*ctx, "Card driver '%s' not found!\n", cdriver);
            return r;
        }
    }

    (*ctx)->debug = verbose;

    reader_count = sc_ctx_get_reader_count(*ctx);

    if (reader_count == 0)
        SC_FUNC_RETURN((*ctx), SC_LOG_TYPE_ERROR, SC_ERROR_NO_READERS_FOUND);

    if (reader_id < 0) {
        /* Automatically try to skip to a reader with a card if reader not specified */
        for (i = 0; i < reader_count; i++) {
            *reader = sc_ctx_get_reader(*ctx, i);
            if (sc_detect_card_presence(*reader, 0) & SC_SLOT_CARD_PRESENT) {
                reader_id = i;
                sc_debug(*ctx, "Using reader with a card: %s", (*reader)->name);
                break;
            }
        }
        if (reader_id >= reader_count) {
            /* no reader found, use the first */
            reader_id = 0;
        }
    }

    if (reader_id >= reader_count)
        SC_FUNC_RETURN((*ctx), SC_LOG_TYPE_ERROR, SC_ERROR_NO_READERS_FOUND);

    *reader = sc_ctx_get_reader(*ctx, reader_id);

    SC_FUNC_RETURN((*ctx), SC_LOG_TYPE_ERROR, SC_SUCCESS);
}

static int list_readers(sc_context_t *ctx)
{
	unsigned int i, rcount = sc_ctx_get_reader_count(ctx);
	
	if (rcount == 0) {
		printf("No smart card readers found.\n");
		return 0;
	}
	printf("Readers known about:\n");
	printf("Nr.    Driver     Name\n");
	for (i = 0; i < rcount; i++) {
		sc_reader_t *screader = sc_ctx_get_reader(ctx, i);
		printf("%-7d%-11s%s\n", i, screader->driver->short_name,
		       screader->name);
	}

	return 0;
}

static int list_drivers(sc_context_t *ctx)
{
	int i;
	
	if (ctx->card_drivers[0] == NULL) {
		printf("No card drivers installed!\n");
		return 0;
	}
	printf("Configured card drivers:\n");
	for (i = 0; ctx->card_drivers[i] != NULL; i++) {
		printf("  %-16s %s\n", ctx->card_drivers[i]->short_name,
		       ctx->card_drivers[i]->name);
	}

	return 0;
}

int print_avail(int verbose)
{
	sc_context_t *ctx = NULL;

	int r;
	r = sc_context_create(&ctx, NULL);
	if (r) {
		fprintf(stderr, "Failed to establish context: %s\n", sc_strerror(r));
		return 1;
	}
	ctx->debug = verbose;

	r = list_readers(ctx)|list_drivers(ctx);

	if (ctx)
		sc_release_context(ctx);

	return r;
}

int build_apdu(sc_context_t *ctx, const u8 *buf, size_t len, sc_apdu_t *apdu)
{
	const u8 *p;
	size_t len0;

        if (!buf || !apdu)
            SC_FUNC_RETURN(ctx, SC_LOG_TYPE_VERBOSE, SC_ERROR_INVALID_ARGUMENTS);

	len0 = len;
	if (len < 4) {
                sc_error(ctx, "APDU too short (must be at least 4 bytes)");
                SC_FUNC_RETURN(ctx, SC_LOG_TYPE_VERBOSE, SC_ERROR_INVALID_DATA);
	}

	memset(apdu, 0, sizeof(*apdu));
	p = buf;
	apdu->cla = *p++;
	apdu->ins = *p++;
	apdu->p1 = *p++;
	apdu->p2 = *p++;
	len -= 4;
	if (len > 1) {
		apdu->lc = *p++;
		len--;
		apdu->data = p;
		apdu->datalen = apdu->lc;
		if (len < apdu->lc) {
                        sc_error(ctx, "APDU too short (need %lu bytes)\n",
                                (unsigned long) apdu->lc - len);
                        SC_FUNC_RETURN(ctx, SC_LOG_TYPE_VERBOSE, SC_ERROR_INVALID_DATA);
		}
		len -= apdu->lc;
		p += apdu->lc;
		if (len) {
			apdu->le = *p++;
			if (apdu->le == 0)
				apdu->le = 256;
			len--;
			apdu->cse = SC_APDU_CASE_4_SHORT;
		} else {
			apdu->cse = SC_APDU_CASE_3_SHORT;
		}
		if (len) {
			sc_error(ctx, "APDU too long (%lu bytes extra)\n",
				(unsigned long) len);
                        SC_FUNC_RETURN(ctx, SC_LOG_TYPE_VERBOSE, SC_ERROR_INVALID_DATA);
		}
	} else if (len == 1) {
		apdu->le = *p++;
		if (apdu->le == 0)
			apdu->le = 256;
		len--;
		apdu->cse = SC_APDU_CASE_2_SHORT;
	} else {
		apdu->cse = SC_APDU_CASE_1;
	}

        apdu->flags = SC_APDU_FLAGS_NO_GET_RESP|SC_APDU_FLAGS_NO_RETRY_WL;

        sc_debug(ctx, "APDU, %d bytes:\tins=%02x p1=%02x p2=%02x",
                (unsigned int) len0, apdu->ins, apdu->p1, apdu->p2);

        SC_FUNC_RETURN(ctx, SC_LOG_TYPE_VERBOSE, SC_SUCCESS);
}
