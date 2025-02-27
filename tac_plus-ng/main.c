/*
   Copyright (C) 1999-2022 Marc Huber (Marc.Huber@web.de)
   All rights reserved.

   Redistribution and use in source and binary  forms,  with or without
   modification, are permitted provided  that  the following conditions
   are met:

   1. Redistributions of source code  must  retain  the above copyright
      notice, this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions  and  the following disclaimer in
      the  documentation  and/or  other  materials  provided  with  the
      distribution.

   3. The end-user documentation  included with the redistribution,  if
      any, must include the following acknowledgment:

          This product includes software developed by Marc Huber
	  (Marc.Huber@web.de).

      Alternately,  this  acknowledgment  may  appear  in  the software
      itself, if and wherever such third-party acknowledgments normally
      appear.

   THIS SOFTWARE IS  PROVIDED  ``AS IS''  AND  ANY EXPRESSED OR IMPLIED
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
   IN NO EVENT SHALL  ITS  AUTHOR  BE  LIABLE FOR ANY DIRECT, INDIRECT,
   INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
   BUT NOT LIMITED  TO,  PROCUREMENT OF  SUBSTITUTE  GOODS OR SERVICES;
   LOSS OF USE,  DATA,  OR PROFITS;  OR  BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY,  WHETHER IN CONTRACT,  STRICT
   LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE OR OTHERWISE)  ARISING IN
   ANY WAY OUT OF THE  USE  OF  THIS  SOFTWARE,  EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
 */

#include "headers.h"
#include "misc/version.h"
#include <sys/resource.h>
#include <signal.h>
#include <netinet/tcp.h>
#include "misc/buffer.h"
#include "misc/strops.h"
#include "mavis/log.h"

#ifdef SO_BINDTODEVICE
#include <net/if.h>
#endif
#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

static const char rcsid[] __attribute__((used)) = "$Id$";

struct config config;		/* configuration data */

static void die(int signum)
{
    report(NULL, LOG_INFO, ~0, "Received signal %d, shutting down.", signum);
    tac_exit(EX_OK);
}

int die_when_idle = 0;
static struct context *ctx_spawnd = NULL;

static void cleanup_spawnd(struct context *ctx __attribute__((unused)), int cur __attribute__((unused)))
{
    if (ctx_spawnd) {
	io_close(ctx->io, ctx_spawnd->sock);
	ctx_spawnd = NULL;
    }

    if (common_data.users_cur == 0 /*&& logs_flushed(config.default_realm) FIXME */ ) {
	drop_mcx(config.default_realm);
	if (!(common_data.debug & DEBUG_TACTRACE_FLAG))
	    report(NULL, LOG_INFO, ~0, "Exiting.");
	exit(EX_OK);
    }

    die_when_idle = -1;
    set_proctitle(ACCEPT_NEVER);
}

static int compare_session(const void *a, const void *b)
{
    return ((tac_session *) a)->session_id - ((tac_session *) b)->session_id;
}

static u_int context_id = 0;

struct context *new_context(struct io_context *io, tac_realm * r)
{
    struct context *c = calloc(1, sizeof(struct context));
    c->io = io;
    c->pool = mempool_create();
    RB_insert(c->pool, c);
    if (r) {
	c->sessions = RB_tree_new(compare_session, NULL);
	c->id = context_id++;
	c->realm = r;
	c->debug = r->debug;
    } else {
	c->debug = common_data.debug;
    }
    return c;
}

static sigset_t master_set;
static void process_signals(void)
{
    sigprocmask(SIG_UNBLOCK, &master_set, NULL);
    sigprocmask(SIG_SETMASK, &master_set, NULL);
}

radixtree_t *dns_tree_ptr_static = NULL;

static void periodics(struct context *ctx, int cur __attribute__((unused)))
{
    struct scm_data sd;

    io_sched_renew_proc(ctx->io, ctx, (void *) periodics);
    process_signals();
    io_child_reap();

    if (!die_when_idle) {
	if (config.suicide && (config.suicide < io_now.tv_sec)) {
	    report(NULL, LOG_INFO, ~0, "Retire timeout is up. Told parent about this.");
	    sd.type = SCM_DYING;
	    if (ctx_spawnd)
		common_data.scm_send_msg(ctx_spawnd->sock, &sd, -1);
	    die_when_idle = -1;
	} else {
	    sd.type = SCM_KEEPALIVE;
	    if (ctx_spawnd && common_data.scm_send_msg(ctx_spawnd->sock, &sd, -1))
		die_when_idle = -1;
	}
    }

    if (die_when_idle)
	cleanup_spawnd(ctx, -1 /* unused */ );

    expire_dynamic_users(config.default_realm);
}

static void periodics_ctx(struct context *ctx, int cur __attribute__((unused)))
{
    rb_node_t *rbn, *rbnext;

    if (!ctx->out && !ctx->delayed && (ctx->host->tcp_timeout || ctx->dying) && (ctx->last_io + ctx->host->tcp_timeout < io_now.tv_sec)) {
	cleanup(ctx, ctx->sock);
	return;
    }

    for (rbn = RB_first(ctx->sessions); rbn; rbn = rbnext) {
	tac_session *s = RB_payload(rbn, tac_session *);
	rbnext = RB_next(rbn);
	if (s->session_timeout < io_now.tv_sec)
	    cleanup_session(s);
    }

    tac_script_expire_exec_context(ctx);

    if (ctx->cleanup_when_idle && !ctx->out && !ctx->delayed && !RB_first(ctx->sessions) && !RB_first(ctx->shellctxcache))
	cleanup(ctx, ctx->sock);
    else
	io_sched_renew_proc(ctx->io, ctx, (void *) periodics_ctx);
}

static void accept_control(struct context *, int __attribute__((unused)));
static void accept_control_final(struct context *);
static void accept_control_common(int, struct scm_data_accept *, sockaddr_union *);
static void accept_control_singleprocess(int, struct scm_data_accept *);
static void accept_control_raw(int, struct scm_data_accept *);
static void accept_control_px(int, struct scm_data_accept *);
#if WITH_TLS
static void accept_control_tls(struct context *, int);
#endif
static void setup_signals(void);

static struct pwdat pwdat_unknown;

int main(int argc, char **argv, char **envp)
{
    int nfds_max;
    struct rlimit rlim;
    struct scm_data_max sd;

    scm_main(argc, argv, envp);

    cfg_init();

    memset(&pwdat_unknown, 0, sizeof(struct pwdat));
    pwdat_unknown.type = S_unknown;

    buffer_setsize(0x8000, 0x10);

    dns_tree_ptr_static = radix_new(NULL /* never freed */ , NULL);

#ifdef WITH_TLS
    if (tls_init()) {
	report(NULL, LOG_ERR, ~0, "tls_init failed at %s:%d\n", __FILE__, __LINE__);
	tac_exit(EX_OSERR);
    }
#endif

    if (!common_data.conffile) {
	common_data.conffile = argv[optind];
	common_data.id = argv[optind + 1];
    }
    cfg_read_config(common_data.conffile, parse_decls, common_data.id ? common_data.id : common_data.progname);
    complete_realm(config.default_realm);

    if (common_data.parse_only)
	tac_exit(EX_OK);

    signal(SIGTERM, die);
    signal(SIGPIPE, SIG_IGN);

    if (!(common_data.debug & DEBUG_TACTRACE_FLAG))
	report(NULL, LOG_INFO, ~0, "Version " VERSION " initialized");

    umask(022);

    mavis_detach();

    setup_sig_segv(common_data.coredumpdir, common_data.gcorepath, common_data.debug_cmd);

    if (common_data.singleprocess) {
	common_data.scm_accept = accept_control_singleprocess;
    } else {
	common_data.io = io_init();
	setproctitle_init(argv, envp);
	setup_signals();
	ctx_spawnd = new_context(common_data.io, NULL);
	ctx_spawnd->sock = dup(0);
	dup2(2, 0);
	io_register(common_data.io, ctx_spawnd->sock, ctx_spawnd);
	io_set_cb_i(common_data.io, ctx_spawnd->sock, (void *) accept_control);
	io_clr_cb_o(common_data.io, ctx_spawnd->sock);
	io_set_cb_h(common_data.io, ctx_spawnd->sock, (void *) cleanup_spawnd);
	io_set_cb_e(common_data.io, ctx_spawnd->sock, (void *) cleanup_spawnd);
	io_set_i(common_data.io, ctx_spawnd->sock);
    }

    if (getrlimit(RLIMIT_NOFILE, &rlim)) {
	report(NULL, LOG_ERR, ~0, "rlimit: %s", strerror(errno));
	exit(EX_SOFTWARE);
    }

    nfds_max = (int) rlim.rlim_cur;
    sd.type = SCM_MAX;
    sd.max = nfds_max / 4;
    if (ctx_spawnd)
	common_data.scm_send_msg(ctx_spawnd->sock, (struct scm_data *) &sd, -1);

    io_sched_add(common_data.io, new_context(common_data.io, NULL), (void *) periodics, 60, 0);
    init_mcx(config.default_realm);

    set_proctitle(ACCEPT_YES);
    io_main(common_data.io);
}

void cleanup(struct context *ctx, int cur)
{
    rb_node_t *t, *u;

    if (ctx == ctx_spawnd) {
	cleanup_spawnd(ctx, cur);
	return;
    }
#ifdef WITH_TLS
    if (ctx->tls_ctx) {
	int res = io_TLS_shutdown(ctx->tls_ctx, ctx->io, cur, cleanup);
	if (res < 0 && errno == EAGAIN)
	    return;
	tls_free(ctx->tls_ctx);
	ctx->tls_ctx = NULL;
    }
#endif

    if (!ctx->msgid) {
	ctx->msgid = "CONN-STOP";
	ctx->msgid_len = 9;
	ctx->acct_type = "stop";
	ctx->acct_type_len = 4;
    }

    log_exec(NULL, ctx, S_connection, io_now.tv_sec);

    while (io_sched_pop(ctx->io, ctx));
    io_close(ctx->io, ctx->sock);

    for (t = RB_first(ctx->sessions); t; t = u) {
	u = RB_next(t);
	cleanup_session(RB_payload(t, tac_session *));
    }

    if (ctx->sessions)
	RB_tree_delete(ctx->sessions);

    if (ctx->shellctxcache)
	RB_tree_delete(ctx->shellctxcache);

    mempool_destroy(ctx->pool);

    if (ctx_spawnd) {
	struct scm_data sd;
	sd.type = SCM_DONE;
	if (common_data.scm_send_msg(ctx_spawnd->sock, &sd, -1) < 0)
	    die_when_idle = 1;;
    }
    common_data.users_cur--;
    if (common_data.debug & DEBUG_TACTRACE_FLAG)
	die_when_idle = 1;

    if (ctx_spawnd && die_when_idle)
	cleanup_spawnd(ctx_spawnd, 0);
    set_proctitle(die_when_idle ? ACCEPT_NEVER : ACCEPT_YES);
}

// Proxy structs from Willy Tarreau/HAProxy Technologies
// https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt

struct proxy_hdr_v2 {
    uint8_t sig[12];		/* hex 0D 0A 0D 0A 00 0D 0A 51 55 49 54 0A */
    uint8_t ver_cmd;		/* protocol version and command */
    uint8_t fam;		/* protocol family and address */
    uint16_t len;		/* number of following bytes part of the header */
} __attribute__((__packed__));

union proxy_addr {
    struct {			/* for TCP/UDP over IPv4, len = 12 */
	uint32_t src_addr;
	uint32_t dst_addr;
	uint16_t src_port;
	uint16_t dst_port;
    } ipv4_addr;
    struct {			/* for TCP/UDP over IPv6, len = 36 */
	uint8_t src_addr[16];
	uint8_t dst_addr[16];
	uint16_t src_port;
	uint16_t dst_port;
    } ipv6_addr;
    struct {			/* for AF_UNIX sockets, len = 216 */
	uint8_t src_addr[108];
	uint8_t dst_addr[108];
    } unix_addr;
} __attribute__((__packed__));

//

struct context_px {
    int sock;			/* socket for this connection */
    int type;
    io_context_t *io;
    struct scm_data_accept sd;
};

static void cleanup_px(struct context_px *ctx, int cur)
{
    struct scm_data sd;

    while (io_sched_pop(ctx->io, ctx));
    io_close(ctx->io, ctx->sock);

    sd.type = SCM_DONE;
    if (ctx_spawnd && common_data.scm_send_msg(ctx_spawnd->sock, &sd, -1) < 0)
	die_when_idle = 1;
    free(ctx);
    common_data.users_cur--;
    if (ctx_spawnd && die_when_idle)
	cleanup_spawnd(ctx_spawnd, cur);
    set_proctitle(die_when_idle ? ACCEPT_NEVER : ACCEPT_YES);
}

static void try_raw(struct context_px *ctx, int cur __attribute__((unused)))
{
    while (io_sched_pop(ctx->io, ctx));
    io_unregister(ctx->io, ctx->sock);
    accept_control_raw(ctx->sock, &ctx->sd);
    free(ctx);
}

static void catchhup(int i __attribute__((unused)))
{
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    if (ctx_spawnd)
	cleanup(ctx_spawnd, 0);
    die_when_idle = -1;
    report(NULL, LOG_INFO, ~0, "SIGHUP: No longer accepting new connections.");
}

static void setup_signals()
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, catchhup);
    signal(SIGTERM, catchhup);
    sigfillset(&master_set);
    sigdelset(&master_set, SIGSEGV);
    sigprocmask(SIG_SETMASK, &master_set, NULL);
}

static void accept_control_singleprocess(int s, struct scm_data_accept *sd)
{
    common_data.users_cur++;
    if (sd->haproxy)
	accept_control_px(s, sd);
    else
	accept_control_raw(s, sd);
}

static void accept_control_raw(int s, struct scm_data_accept *sd)
{
    accept_control_common(s, sd, NULL);
}

static struct context_px *new_context_px(struct io_context *io, struct scm_data_accept *sd)
{
    struct context_px *c = calloc(1, sizeof(struct context_px));
    c->io = io;
    memcpy(&c->sd, sd, sizeof(struct scm_data_accept));
    return c;
}

static void read_px(struct context_px *ctx, int cur)
{
    ssize_t len;
    uint16_t hlen;
    sockaddr_union from;
    char tmp[240];
    struct proxy_hdr_v2 *hdr = (struct proxy_hdr_v2 *) tmp;
    union proxy_addr *addr = (union proxy_addr *) &tmp[sizeof(struct proxy_hdr_v2)];
    memset(&tmp, 0, sizeof(tmp));
    len = recv(cur, &tmp, sizeof(tmp), MSG_PEEK);
    if ((len < (ssize_t) sizeof(struct proxy_hdr_v2))
	|| ((hdr->ver_cmd >> 4) != 2)
	|| (memcmp(hdr->sig, "\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A", 12))
	|| ((uint16_t) len < (hlen = ntohs(hdr->len)) + sizeof(struct proxy_hdr_v2))
	|| (hdr->fam == 0x11 && hlen != 12)
	|| (hdr->fam == 0x21 && hlen != 36)) {
	try_raw(ctx, cur);
	return;
    }
    read(cur, &tmp, sizeof(struct proxy_hdr_v2) + hlen);

    switch (hdr->fam) {
    case 0x11:
	from.sin.sin_family = AF_INET;
	from.sin.sin_addr.s_addr = addr->ipv4_addr.src_addr;
	break;
    case 0x21:
	from.sin6.sin6_family = AF_INET6;
	memcpy(&from.sin6.sin6_addr, addr->ipv6_addr.src_addr, 16);
	break;
    case 0x00:
	try_raw(ctx, cur);
	return;
    default:
	cleanup_px(ctx, cur);
	return;
    }

    while (io_sched_pop(ctx->io, ctx));
    io_unregister(ctx->io, ctx->sock);
    accept_control_common(ctx->sock, &ctx->sd, &from);
    free(ctx);
}

static void reject_conn(struct context *ctx, char *hint, char *tls)
{
    if (ctx->proxy_addr_ascii) {
	if (!(common_data.debug & DEBUG_TACTRACE_FLAG))
	    report(NULL, LOG_INFO, ~0, "proxied %sconnection request from %s for %s (realm: %s%s%s) rejected%s%s", tls, ctx->proxy_addr_ascii,
		   ctx->peer_addr_ascii, ctx->realm->name, ctx->vrf ? ", vrf: " : "", ctx->vrf ? ctx->vrf : "", hint ? ": " : "", hint);
    } else
	report(NULL, LOG_INFO, ~0, "%sconnection request from %s (realm: %s%s%s) rejected%s%s", tls, ctx->peer_addr_ascii, ctx->realm->name,
	       ctx->vrf ? ", vrf: " : "", ctx->vrf ? ctx->vrf : "", hint ? ": " : "", hint);

    ctx->msgid = "CONN-REJECT";
    ctx->msgid_len = 11;
    ctx->acct_type = "reject";
    ctx->acct_type_len = 6;
    cleanup(ctx, ctx->sock);
}

#ifdef WITH_TLS
static void complete_host(tac_host *);

static void accept_control_tls(struct context *ctx, int cur)
{
    const char *hint = "";
    io_clr_i(ctx->io, cur);
    io_clr_o(ctx->io, cur);

    switch (tls_handshake(ctx->tls_ctx)) {
    default:
	hint = tls_error(ctx->tls_ctx);
	goto bye;
    case TLS_WANT_POLLIN:
	io_set_i(ctx->io, cur);
	return;
    case TLS_WANT_POLLOUT:
	io_set_o(ctx->io, cur);
	return;
    case 0:
	io_unregister(ctx->io, ctx->sock);
	break;
    }

    if (tls_peer_cert_provided(ctx->tls_ctx)) {
	int valid;
	char buf[40];
	time_t notafter;
	ctx->tls_conn_version = tls_conn_version(ctx->tls_ctx);
	ctx->tls_conn_version_len = strlen(ctx->tls_conn_version);
	ctx->tls_conn_cipher = tls_conn_cipher(ctx->tls_ctx);
	ctx->tls_conn_cipher_len = strlen(ctx->tls_conn_cipher);
	snprintf(buf, sizeof(buf), "%d", tls_conn_cipher_strength(ctx->tls_ctx));
	ctx->tls_conn_cipher_strength = mempool_strdup(ctx->pool, buf);
	ctx->tls_conn_cipher_strength_len = strlen(ctx->tls_conn_cipher_strength);

	ctx->tls_peer_cert_subject = tls_peer_cert_subject(ctx->tls_ctx);
	if (ctx->tls_peer_cert_subject) {
	    while (*ctx->tls_peer_cert_subject == '/')	// libtls bug?
		ctx->tls_peer_cert_subject++;
	    ctx->tls_peer_cert_subject_len = strlen(ctx->tls_peer_cert_subject);
	}

	ctx->tls_peer_cert_issuer = (char *) tls_peer_cert_issuer(ctx->tls_ctx);
	if (ctx->tls_peer_cert_issuer) {
	    while (*ctx->tls_peer_cert_issuer == '/')	// libtls bug?
		ctx->tls_peer_cert_issuer++;
	    ctx->tls_peer_cert_issuer_len = strlen(ctx->tls_peer_cert_issuer);
	}

	notafter = tls_peer_cert_notafter(ctx->tls_ctx);
	valid = (ctx->realm->tls_accept_expired == TRISTATE_YES)
	    || (tls_peer_cert_notbefore(ctx->tls_ctx) < io_now.tv_sec && notafter > io_now.tv_sec);
	if (ctx->realm->tls_accept_expired != TRISTATE_YES && notafter > io_now.tv_sec + 30 * 86400)
	    report(NULL, LOG_INFO, ~0, "peer certificate for %s will expire in " TIME_T_PRINTF " days", ctx->peer_addr_ascii,
		   (io_now.tv_sec - notafter) / 86400);

	if (ctx->tls_peer_cert_subject && valid) {
	    size_t i;
	    char *cn = alloca(ctx->tls_peer_cert_subject_len + 1);
	    char *t;

	    // normalize subject
	    cn[ctx->tls_peer_cert_subject_len] = 0;
	    for (i = 0; i < ctx->tls_peer_cert_subject_len; i++)
		cn[i] = tolower(ctx->tls_peer_cert_subject[i]);

	    // set cn
	    while (cn) {
		char *e = strchr(cn, ',');
		if (e)
		    *e = 0;
		while (isspace(*cn))
		    cn++;
		if (cn[0] == 'c' && cn[1] == 'n' && cn[2] == '=') {
		    cn += 3;
		    while (*cn && isspace(*cn))
			cn++;
		    e = cn;
		    while (*e && !isspace(*e))
			e++;
		    *e = 0;
		    ctx->tls_peer_cn = mempool_strdup(ctx->pool, cn);
		    ctx->tls_peer_cn_len = strlen(cn);
		    break;
		}

		if (e)
		    cn = e + 1;
		else
		    break;
	    }

	    // check for dn match:
	    t = (char *) ctx->tls_peer_cert_subject;
	    while (t) {
		tac_host *h = lookup_host(t, ctx->realm);
		if (h) {
		    ctx->host = h;
		    complete_host(ctx->host);
		    accept_control_final(ctx);
		    return;
		}
		t = strchr(t, ',');
		if (t)
		    t++;
	    }

	    if (ctx->tls_peer_cn) {	// check for cn match:
		t = ctx->tls_peer_cn;
		while (t) {
		    tac_host *h = lookup_host(t, ctx->realm);
		    if (h) {
			ctx->host = h;
			complete_host(ctx->host);
			accept_control_final(ctx);
			return;
		    }
		    t = strchr(t, '.');
		    if (t)
			t++;
		}
	    }
	}
    }
    // host not found by DN or CN, but in address tree.
    if (ctx->host) {
	accept_control_final(ctx);
	return;
    }

  bye:
    if (!hint)
	hint = "";

    reject_conn(ctx, (char *) hint, "TLS ");
}
#endif

static void accept_control_px(int s, struct scm_data_accept *sd)
{
    struct context_px *ctx = new_context_px(common_data.io, sd);
    ctx->sock = s;
    io_register(ctx->io, ctx->sock, ctx);
    io_set_cb_i(ctx->io, ctx->sock, (void *) read_px);
    io_set_cb_h(ctx->io, ctx->sock, (void *) cleanup_px);
    io_set_cb_e(ctx->io, ctx->sock, (void *) cleanup_px);
    io_set_i(ctx->io, ctx->sock);
    io_sched_add(ctx->io, ctx, (void *) try_raw, 2, 0);
}

static void complete_host(tac_host * h)
{
    if (!h->complete && h->parent) {
	int priv_lvl, enable_implied[TAC_PLUS_PRIV_LVL_MAX + 1];
	tac_host *hp = h->parent;;
	complete_host(hp);

	if (h->anon_enable == TRISTATE_DUNNO)
	    h->anon_enable = hp->anon_enable;

	if (h->anon_enable == TRISTATE_DUNNO)
	    h->augmented_enable = hp->augmented_enable;

	if (h->anon_enable == TRISTATE_DUNNO)
	    h->anon_enable = hp->anon_enable;

	if (h->single_connection == TRISTATE_DUNNO)
	    h->single_connection = hp->single_connection;

	if (h->authfallback == TRISTATE_DUNNO)
	    h->authfallback = hp->authfallback;

	if (h->cleanup_when_idle == TRISTATE_DUNNO)
	    h->cleanup_when_idle = hp->cleanup_when_idle;

	if (h->authz_if_authc == TRISTATE_DUNNO)
	    h->authz_if_authc = hp->authz_if_authc;

	if (h->map_pap_to_login == TRISTATE_DUNNO)
	    h->map_pap_to_login = hp->map_pap_to_login;

#ifdef TPNG_EXPERIMENTAL
	if (h->ssh_key_check_all == TRISTATE_DUNNO)
	    h->ssh_key_check_all = hp->ssh_key_check_all;
#endif

	if (!h->welcome_banner)
	    h->welcome_banner = hp->welcome_banner;

	if (!h->welcome_banner_fallback)
	    h->welcome_banner_fallback = hp->welcome_banner_fallback;

	if (!h->reject_banner)
	    h->reject_banner = hp->reject_banner;

	if (!h->motd)
	    h->motd = hp->motd;

	if (!h->key)
	    h->key = hp->key;

	h->debug |= hp->debug;

	if (h->tcp_timeout < 0)
	    h->tcp_timeout = hp->tcp_timeout;

	if (h->session_timeout < 0)
	    h->session_timeout = hp->session_timeout;

	if (h->context_timeout < 0)
	    h->context_timeout = hp->context_timeout;

	if (h->authen_max_attempts < 0)
	    h->authen_max_attempts = hp->authen_max_attempts;

	if (!h->rewrite_user)
	    h->rewrite_user = hp->rewrite_user;

	h->bug_compatibility |= hp->bug_compatibility;

	/* copy enable passwords to context */

	for (priv_lvl = TAC_PLUS_PRIV_LVL_MIN; priv_lvl <= TAC_PLUS_PRIV_LVL_MAX; priv_lvl++)
	    enable_implied[priv_lvl] = TAC_PLUS_PRIV_LVL_MAX + 1;

	for (priv_lvl = TAC_PLUS_PRIV_LVL_MIN; priv_lvl <= TAC_PLUS_PRIV_LVL_MAX; priv_lvl++)
	    if (hp->enable[priv_lvl]) {
		if (!hp->enable_implied[priv_lvl]) {
		    h->enable[priv_lvl] = hp->enable[priv_lvl];
		    enable_implied[priv_lvl] = hp->enable_implied[priv_lvl];
		    break;
		}
		if (hp->enable_implied[priv_lvl] < enable_implied[priv_lvl]) {
		    h->enable[priv_lvl] = hp->enable[priv_lvl];
		    enable_implied[priv_lvl] = hp->enable_implied[priv_lvl];
		}
	    }

	/* make sure all enable password pointers are set */
	for (priv_lvl = TAC_PLUS_PRIV_LVL_MIN; priv_lvl <= TAC_PLUS_PRIV_LVL_MAX; priv_lvl++)
	    if (!h->enable[priv_lvl])
		h->enable[priv_lvl] = &pwdat_unknown;
	h->complete = 1;
    }
}


static void accept_control_common(int s, struct scm_data_accept *sd, sockaddr_union * nad_address)
{
    char afrom[256];
    char pfrom[256];
    char *hint = "", *peer = NULL;
    tac_host *h = NULL;
    struct in6_addr addr;
    tac_realm *r;
    radixtree_t *rxt;
    struct context *ctx = NULL;
    char vrf[IFNAMSIZ + 1];
    size_t vrf_len = 0;

    fcntl(s, F_SETFD, FD_CLOEXEC);
    fcntl(s, F_SETFL, O_NONBLOCK);

    sockaddr_union from;
    socklen_t from_len = (socklen_t) sizeof(from);
    memset(&from, 0, sizeof(from));

    if (getpeername(s, &from.sa, &from_len)) {
	struct scm_data d;
	report(NULL, LOG_DEBUG, DEBUG_PACKET_FLAG, "getpeername: %s", strerror(errno));
	close(s);

	common_data.users_cur--;
	set_proctitle(die_when_idle ? ACCEPT_NEVER : ACCEPT_YES);

	d.type = SCM_DONE;
	if (ctx_spawnd && common_data.scm_send_msg(ctx_spawnd->sock, &d, -1) < 0)
	    die_when_idle = 1;
	if (ctx_spawnd && die_when_idle)
	    cleanup_spawnd(ctx_spawnd, -1);
	return;
    }

    if (nad_address)
	peer = su_ntop(&from, pfrom, sizeof(pfrom)) ? pfrom : "<unknown>";
    else
	nad_address = &from;

    su_convert(nad_address, AF_INET);
    su_ptoh(nad_address, &addr);

#ifdef SO_BINDTODEVICE		// Linux.
    {
	// Reminder to myself:
	//      sysctl -w net.ipv4.tcp_l3mdev_accept=1 
	// is the "vrf-also" variant in case the spawnd configuration wasn't adjusted to use VRFs.
	socklen_t opt_len = sizeof(vrf);;
	*vrf = 0;
	if (getsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, (u_char *) vrf, &opt_len))
	    report(NULL, LOG_ERR, ~0, "getsockopt(SO_BINDTODEVICE) failed at %s:%d: %s", __FILE__, __LINE__, strerror(errno));
	else if (opt_len > 0) {
	    if (!vrf[opt_len - 1])
		opt_len--;
	    vrf_len = opt_len;
	}
    }
#endif
#ifdef SO_RTABLE		// OpenBSD, untested.
    {
	unsigned int opt;
	socklen_t optlen = sizeof(opt);
	if (getsockopt(s, SOL_SOCKET, SO_RTABLE, &opt, &optlen))
	    report(NULL, LOG_ERR, ~0, "getsockopt(SO_RTABLE) failed at %s:%d: %s", __FILE__, __LINE__, strerror(errno));
	else
	    vrf_len = snprintf(vrf, sizeof(vrf), "%u", opt);
    }
#endif

    r = config.default_realm;
    if (*sd->realm)
	r = lookup_realm(sd->realm, r);
    if (!r)
	r = config.default_realm;

    // Still at the default realm? Try the VRF name:
    if (vrf_len && (r == config.default_realm))
	r = lookup_realm(vrf, r);

    if (!r)
	r = config.default_realm;

    rxt = lookup_hosttree(r);
    if (rxt)
	h = radix_lookup(rxt, &addr, NULL);

    if (h)
	complete_host(h);

    if (!sd->use_tls) {
	if (h) {
	    if (!h->key)
		hint = ": no encryption key found";
	} else
	    hint = ": host unknown";
    }

    ctx = new_context(common_data.io, r);
    ctx->sock = s;
    ctx->peer_addr_ascii = mempool_strdup(ctx->pool, su_ntop(nad_address, afrom, sizeof(afrom)) ? afrom : "<unknown>");
    ctx->peer_addr_ascii_len = strlen(ctx->peer_addr_ascii);
    if (h)
	ctx->key = h->key;
    ctx->host = h;
    if (peer) {
	ctx->proxy_addr_ascii = mempool_strdup(ctx->pool, peer);
	ctx->proxy_addr_ascii_len = strlen(peer);
    }

    ctx->nas_address = addr;	// FIXME, use origin
    ctx->nas_dns_name = radix_lookup(dns_tree_ptr_static, &addr, NULL);	// FIXME
    if (vrf_len)
	ctx->vrf = mempool_strndup(ctx->pool, (u_char *) vrf, vrf_len);
    ctx->vrf_len = vrf_len;

#ifdef WITH_TLS
    if (h && sd->use_tls) {
	if (!r->tls_ctx) {
	    report(NULL, LOG_ERR, ~0, "spawnd set TLS flag but realm %s isn't configured suitably", r->name);
	    cleanup(ctx, ctx->sock);
	    return;
	}
	io_register(ctx->io, ctx->sock, ctx);
	io_set_cb_i(ctx->io, ctx->sock, (void *) accept_control_tls);
	io_set_cb_o(ctx->io, ctx->sock, (void *) accept_control_tls);
	io_set_cb_h(ctx->io, ctx->sock, (void *) cleanup);
	io_set_cb_e(ctx->io, ctx->sock, (void *) cleanup);
	io_sched_add(ctx->io, ctx, (void *) periodics_ctx, 60, 0);
	tls_accept_socket(r->tls_ctx, &ctx->tls_ctx, ctx->sock);
	accept_control_tls(ctx, ctx->sock);
	return;
    }
#endif
    if (!h || !h->key)
	reject_conn(ctx, hint, "");
    else
	accept_control_final(ctx);
}

static void accept_control_final(struct context *ctx)
{
    tac_session session;
    static int count = 0;
    memset(&session, 0, sizeof(tac_session));
    session.ctx = ctx;

    if (ctx->proxy_addr_ascii) {
	if (!(common_data.debug & DEBUG_TACTRACE_FLAG))
	    report(&session, LOG_DEBUG, DEBUG_PACKET_FLAG, "proxied connection request from %s for %s (realm: %s%s%s)", ctx->proxy_addr_ascii,
		   ctx->peer_addr_ascii, ctx->realm->name, ctx->vrf ? ", vrf: " : "", ctx->vrf ? ctx->vrf : "");
    } else
	report(&session, LOG_DEBUG, DEBUG_PACKET_FLAG, "connection request from %s (realm: %s%s%s)", ctx->peer_addr_ascii, ctx->realm->name,
	       ctx->vrf ? ", vrf: " : "", ctx->vrf ? ctx->vrf : "");

    ctx->nas_address_ascii = ctx->peer_addr_ascii;	//  FIXME, use origin
    ctx->nas_address_ascii_len = strlen(ctx->peer_addr_ascii);	// FIXME, use origin

    io_register(ctx->io, ctx->sock, ctx);
    io_set_cb_i(ctx->io, ctx->sock, (void *) tac_read);
    io_set_cb_o(ctx->io, ctx->sock, (void *) tac_write);
    io_set_cb_h(ctx->io, ctx->sock, (void *) cleanup);
    io_set_cb_e(ctx->io, ctx->sock, (void *) cleanup);
    io_set_i(ctx->io, ctx->sock);
    io_sched_add(ctx->io, ctx, (void *) periodics_ctx, 60, 0);
    if (config.retire && (++count == config.retire) && !common_data.singleprocess) {
	struct scm_data d;
	report(&session, LOG_INFO, ~0, "Retire limit reached. Told parent about this.");
	d.type = SCM_DYING;
	if (ctx_spawnd)
	    common_data.scm_send_msg(ctx_spawnd->sock, &d, -1);
    }
    ctx->msgid = "CONN-START";
    ctx->msgid_len = 10;
    ctx->acct_type = "start";
    ctx->acct_type_len = 5;
    log_exec(NULL, ctx, S_connection, io_now.tv_sec);
    ctx->msgid = NULL;
    ctx->msgid_len = 0;
}

static void accept_control(struct context *ctx, int cur)
{
    int s, one = 1;
    struct scm_data_accept sd;

    if (common_data.scm_recv_msg(cur, &sd, sizeof(sd), &s)) {
	cleanup_spawnd(ctx, cur);
	return;
    }
    switch (sd.type) {
    case SCM_MAY_DIE:
	cleanup_spawnd(ctx, cur);
	return;
    case SCM_ACCEPT:
	setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char *) &one, (socklen_t) sizeof(one));
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *) &one, (socklen_t) sizeof(one));
	common_data.users_cur++;
	set_proctitle(die_when_idle ? ACCEPT_NEVER : ACCEPT_YES);
	if (sd.haproxy)
	    accept_control_px(s, &sd);
	else
	    accept_control_raw(s, &sd);
	return;
    default:
	if (s > -1)
	    close(s);
    }
}
