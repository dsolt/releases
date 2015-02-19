/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2014-2015 Intel, Inc.  All rights reserved.
 * Copyright (c) 2014      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2014      Artem Y. Polyakov <artpol84@gmail.com>.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "pmix_config.h"
#include "src/api/pmix.h"
#include "src/include/types.h"
#include "src/include/pmix_globals.h"
#include "src/include/pmix_stdint.h"
#include "src/include/pmix_socket_errno.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <event.h>

#include "src/class/pmix_list.h"
#include "src/buffer_ops/buffer_ops.h"
#include "src/util/argv.h"
#include "src/util/error.h"
#include "src/util/output.h"
#include "src/util/progress_threads.h"
#include "src/usock/usock.h"
#include "src/sec/pmix_sec.h"

#include "pmix_client_hash.h"
#include "pmix_client_ops.h"

/* callback for wait completion */
static void wait_cbfunc(int sd, pmix_usock_hdr_t *hdr,
                        pmix_buffer_t *buf, void *cbdata);
static void op_cbfunc(int status, void *cbdata);

int PMIx_Connect(const pmix_range_t ranges[], size_t nranges)
{
    int rc;
    pmix_cb_t *cb;
    
    pmix_output_verbose(2, pmix_globals.debug_output,
                        "pmix: connect called");

    /* create a callback object as we need to pass it to the
     * recv routine so we know which callback to use when
     * the return message is recvd */
    cb = PMIX_NEW(pmix_cb_t);
    cb->active = true;

    /* push the message into our event base to send to the server */
    if (PMIX_SUCCESS != (rc = PMIx_Connect_nb(ranges, nranges, op_cbfunc, cb))) {
        PMIX_RELEASE(cb);
        return rc;
    }

    /* wait for the connect to complete */
    PMIX_WAIT_FOR_COMPLETION(cb->active);
    rc = cb->status;
    PMIX_RELEASE(cb);

    pmix_output_verbose(2, pmix_globals.debug_output,
                        "pmix: connect completed");

    return rc;
}

int PMIx_Connect_nb(const pmix_range_t ranges[], size_t nranges,
                    pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    pmix_buffer_t *msg;
    pmix_cmd_t cmd = PMIX_CONNECTNB_CMD;
    int rc;
    pmix_cb_t *cb;
    
    pmix_output_verbose(2, pmix_globals.debug_output,
                        "pmix: connect called");

    if (pmix_client_globals.init_cntr <= 0) {
        return PMIX_ERR_INIT;
    }

    /* check for bozo input */
    if (NULL == ranges || 0 >= nranges) {
        return PMIX_ERR_BAD_PARAM;
    }

    msg = PMIX_NEW(pmix_buffer_t);
    /* pack the cmd */
    if (PMIX_SUCCESS != (rc = pmix_bfrop.pack(msg, &cmd, 1, PMIX_CMD))) {
        PMIX_ERROR_LOG(rc);
        return rc;
    }

    /* pack the number of ranges */
    if (PMIX_SUCCESS != (rc = pmix_bfrop.pack(msg, &nranges, 1, PMIX_SIZE))) {
        PMIX_ERROR_LOG(rc);
        return rc;
    }
    if (PMIX_SUCCESS != (rc = pmix_bfrop.pack(msg, ranges, nranges, PMIX_RANGE))) {
        PMIX_ERROR_LOG(rc);
        return rc;
    }

    /* create a callback object as we need to pass it to the
     * recv routine so we know which callback to use when
     * the return message is recvd */
    cb = PMIX_NEW(pmix_cb_t);
    cb->op_cbfunc = cbfunc;
    cb->cbdata = cbdata;

    /* push the message into our event base to send to the server */
    PMIX_ACTIVATE_SEND_RECV(&pmix_client_globals.myserver, msg, wait_cbfunc, cb);

    return PMIX_SUCCESS;
}

int PMIx_Disconnect(const pmix_range_t ranges[], size_t nranges)
{
    int rc;
    pmix_cb_t *cb;
     
    /* create a callback object as we need to pass it to the
     * recv routine so we know which callback to use when
     * the return message is recvd */
    cb = PMIX_NEW(pmix_cb_t);
    cb->active = true;

    if (PMIX_SUCCESS != (rc = PMIx_Disconnect_nb(ranges, nranges, op_cbfunc, cb))) {
        PMIX_RELEASE(cb);
        return rc;
    }

    /* wait for the connect to complete */
    PMIX_WAIT_FOR_COMPLETION(cb->active);
    rc = cb->status;
    PMIX_RELEASE(cb);

    pmix_output_verbose(2, pmix_globals.debug_output,
                        "pmix: disconnect completed");

    return rc;
}

int PMIx_Disconnect_nb(const pmix_range_t ranges[], size_t nranges,
                       pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    pmix_buffer_t *msg;
    pmix_cmd_t cmd = PMIX_DISCONNECTNB_CMD;
    int rc;
    pmix_cb_t *cb;
    
    pmix_output_verbose(2, pmix_globals.debug_output,
                        "pmix: disconnect called");

    if (pmix_client_globals.init_cntr <= 0) {
        return PMIX_ERR_INIT;
    }

    /* check for bozo input */
    if (NULL == ranges || 0 >= nranges) {
        return PMIX_ERR_BAD_PARAM;
    }

    msg = PMIX_NEW(pmix_buffer_t);
    /* pack the cmd */
    if (PMIX_SUCCESS != (rc = pmix_bfrop.pack(msg, &cmd, 1, PMIX_CMD))) {
        PMIX_ERROR_LOG(rc);
        return rc;
    }

    /* pack the number of ranges */
    if (PMIX_SUCCESS != (rc = pmix_bfrop.pack(msg, &nranges, 1, PMIX_SIZE))) {
        PMIX_ERROR_LOG(rc);
        return rc;
    }
    if (PMIX_SUCCESS != (rc = pmix_bfrop.pack(msg, ranges, nranges, PMIX_RANGE))) {
        PMIX_ERROR_LOG(rc);
        return rc;
    }

    /* create a callback object as we need to pass it to the
     * recv routine so we know which callback to use when
     * the return message is recvd */
    cb = PMIX_NEW(pmix_cb_t);
    cb->active = true;
    cb->op_cbfunc = cbfunc;
    cb->cbdata = cbdata;
    
    /* push the message into our event base to send to the server */
    PMIX_ACTIVATE_SEND_RECV(&pmix_client_globals.myserver, msg, wait_cbfunc, cb);

    pmix_output_verbose(2, pmix_globals.debug_output,
                        "pmix: disconnect completed");

    return PMIX_SUCCESS;
}

static void wait_cbfunc(int sd, pmix_usock_hdr_t *hdr,
                        pmix_buffer_t *buf, void *cbdata)
{
    pmix_cb_t *cb = (pmix_cb_t*)cbdata;
    int rc, ret;
    int32_t cnt;
    
    pmix_output_verbose(2, pmix_globals.debug_output,
                        "pmix:client recv callback activated with %d bytes",
                        (NULL == buf) ? -1 : (int)buf->bytes_used);

    /* unpack the returned status */
    cnt = 1;
    if (PMIX_SUCCESS != (rc = pmix_bfrop.unpack(buf, &ret, &cnt, PMIX_INT))) {
        PMIX_ERROR_LOG(rc);
        ret = rc;
    }
    if (NULL != cb->op_cbfunc) {
        cb->op_cbfunc(ret, cb->cbdata);
    }
}

static void op_cbfunc(int status, void *cbdata)
{
    pmix_cb_t *cb = (pmix_cb_t*)cbdata;

    cb->status = status;
    cb->active = false;
}
