/*
 * mavis.h
 * (C)1998-2015 by Marc Huber <Marc.Huber@web.de>
 * All rights reserved.
 *
 * $Id: mavis.h,v 1.86 2022/04/24 11:57:55 marc Exp marc $
 *
 */

#ifndef __MAVIS_H_
#define __MAVIS_H_
#define MAVIS_API_VERSION "5"

#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <stdarg.h>
#include <setjmp.h>

#include "misc/sysconf.h"

#include "scm.h"
#include "debug.h"
#include "token.h"

#include "misc/io_sched.h"
#include "misc/io_child.h"
#include "misc/pid_write.h"
#include "misc/rb.h"

/* Response codes for MAVIS modules */

/* External + internal interface: */
#define MAVIS_FINAL	0	/* response available */
#define MAVIS_DEFERRED	1	/* response deferred */
#define MAVIS_IGNORE	2	/* ignore response */
#define MAVIS_TIMEOUT	3	/* query timed out */
#define MAVIS_FINAL_DEFERRED	4	/* final response, but don't run scripts twice */

/* Internal interface: */
#define MAVIS_DOWN	16	/* pass request to lower module */

/* Module configuration return codes: */
#define MAVIS_CONF_OK	0	/* configuration succeeded */
#define MAVIS_CONF_ERR	1	/* configuration failed */

/* Initialization return codes: */
#define MAVIS_INIT_OK	0	/* initialization succeeded */
#define MAVIS_INIT_ERR	1	/* initialization failed */

#define BUFSIZE_MAVIS 65000

#ifdef __MAVIS_MAIN__
#define AV_CHAR_START char *av_char[] = {
AV_CHAR_START
#undef AV_CHAR_START
#define AV_CHAR(A,B) A,B
#else
#define AV_CHAR(A,B)
extern char *av_char[];
#endif


#define AV_A_TYPE               0
AV_CHAR("TYPE",)
#define AV_A_MEMBEROF		1
    AV_CHAR("MEMBEROF",)
#define AV_A_SSHKEYHASH		2
    AV_CHAR("SSHKEYHASH",)
#define AV_A_TIMESTAMP		3
    AV_CHAR("TIMESTAMP",)
#define AV_A_USER		4
    AV_CHAR("USER",)
#define AV_A_DN			5
    AV_CHAR("DN",)
#define AV_A_RESULT             6
    AV_CHAR("RESULT",)
#define AV_A_PATH               7
    AV_CHAR("PATH",)
#define AV_A_PASSWORD           8
    AV_CHAR("PASSWORD",)
#define AV_A_UID                9
    AV_CHAR("UID",)
#define AV_A_GID                10
    AV_CHAR("GID",)
#define AV_A_LIMIT		11
    AV_CHAR("LIMIT",)
#define AV_A_TRANSPORT		12
    AV_CHAR("TRANSPORT",)
#define AV_A_TRAFFICSHAPING	13
    AV_CHAR("TRAFFICSHAPING",)
#define AV_A_IPADDR             14
    AV_CHAR("IPADDR",)
#define AV_A_QUOTA_LIMIT        15
    AV_CHAR("QUOTA_LIMIT",)
#define AV_A_QUOTA_PATH         16
    AV_CHAR("QUOTA_PATH",)
#define AV_A_COMMENT            17
    AV_CHAR("COMMENT",)
#define AV_A_DIGEST_MD5_CNONCE	18
    AV_CHAR("CNONCE",)
#define AV_A_HOME               19
    AV_CHAR("HOME",)
#define AV_A_ROOT               20
    AV_CHAR("ROOT",)
#define AV_A_SERIAL             21
    AV_CHAR("SERIAL",)
#define AV_A_FTP_ANONYMOUS	22
    AV_CHAR("FTP_ANONYMOUS",)
#define AV_A_EMAIL		23
    AV_CHAR("EMAIL",)
#define AV_A_GIDS		24	/* supplemental groups */
    AV_CHAR("GIDS",)
#define AV_A_SERVERIP		25
    AV_CHAR("SERVERIP",)
#define AV_A_TARIFF		26
    AV_CHAR("TARIFF",)
#define AV_A_REALM		27
    AV_CHAR("REALM",)
#define AV_A_DIGEST_MD5_URI	28
    AV_CHAR("DIGEST_URI",)
#define AV_A_ANON_INCOMING	29
    AV_CHAR("ANON_INCOMING",)
#define AV_A_VHOST		30
    AV_CHAR("VHOST",)
#define AV_A_UMASK		31
    AV_CHAR("UMASK",)
#define AV_A_USER_RESPONSE	32
    AV_CHAR("USER_RESPONSE",)
#define AV_A_AUTH_METHOD	33
    AV_CHAR("AUTH_METHOD",)
#define AV_A_CLASS		34
    AV_CHAR("CLASS",)
#define AV_A_DIGEST_MD5_RSPAUTH	35
    AV_CHAR("DIGEST_MD5_RESPONSE",)
#define AV_A_DBPASSWORD		36
    AV_CHAR("DBPASSWORD",)
#define AV_A_SCRAM_CLIENT_CHALLENGE	37
    AV_CHAR("SCRAM_CLIENT_CHALLENGE",)
#define AV_A_SCRAM_SERVER_CHALLENGE	38
    AV_CHAR("SCRAM_SERVER_CHALLENGE",)
#define AV_A_SCRAM_SERVER_PROOF		39
    AV_CHAR("SCRAM_SERVER_PROOF",)
#define AV_A_SCRAM_CLIENT_PROOF		40
    AV_CHAR("SCRAM_CLIENT_PROOF",)
#define AV_A_MAILCHECK_POST		41
    AV_CHAR("MAILCHECK_POST",)
#define AV_A_MAILCHECK_PRE		42
    AV_CHAR("MAILCHECK_PRE",)
#define AV_A_TUNNEL			43
    AV_CHAR("TUNNEL",)
#define AV_A_CERTSUBJ			44
    AV_CHAR("CERTSUBJ",)
#define AV_A_DBCERTSUBJ			45
    AV_CHAR("DBCERTSUBJ",)
#define AV_A_TACCLIENT			46
    AV_CHAR("TACCLIENT",)
#define AV_A_TACMEMBER			47
    AV_CHAR("TACMEMBER",)
#define AV_A_TACPROFILE			48
    AV_CHAR("TACPROFILE",)
#define AV_A_TACTYPE			49
    AV_CHAR("TACTYPE",)
#define AV_A_PASSWORD_NEW		50
    AV_CHAR("PASSWDNEW",)
#define AV_A_CHALLENGE			51
    AV_CHAR("CHALLENGE",)
#define AV_A_PASSWORD_ONESHOT		52
    AV_CHAR("PASSWORD_ONESHOT",)
#define AV_A_PASSWORD_MUSTCHANGE	53
    AV_CHAR("PASSWORD_MUSTCHANGE",)
#define AV_A_SHELL			54
    AV_CHAR("SHELL",)
#define AV_A_CURRENT_MODULE		55
    AV_CHAR("CURRENT_MODULE",)
#define AV_A_ARRAYSIZE          	56
#ifdef __MAVIS_MAIN__
#define AV_CHAR_END };
    AV_CHAR_END
#undef AV_CHAR_END
#endif
#define AV_V_TYPE_WWW           	"WWW"
#define AV_V_TYPE_FTP           	"FTP"
#define AV_V_TYPE_POP3          	"POP3"
#define AV_V_TYPE_POP3PATH		"POP3PATH"
#define AV_V_TYPE_LOGIN         	"LOGIN"
#define AV_V_TYPE_CANONICAL		"CANONICAL"
#define AV_V_TYPE_TRANSPORT		"TRANSPORT"
#define AV_V_TYPE_VIRTUAL		"VIRTUAL"
#define AV_V_TYPE_RADIUS		"RADIUS"
#define AV_V_TYPE_TACPLUS		"TACPLUS"
/* private query types/commands, may not be used in client queries */
#define AV_V_TYPE_PRIVATE_PREFIX	"PRIV_"
#define AV_V_TYPE_PRIVATE_PREFIX_LEN	5
#define AV_V_TYPE_LOGSTATS		"PRIV_LOGSTATS"
#define AV_V_BOOL_TRUE          	"TRUE"
#define AV_V_BOOL_FALSE         	"FALSE"
#define AV_V_RESULT_OK          	"ACK"
#define AV_V_RESULT_FAIL        	"NAK"
#define AV_V_RESULT_ERROR       	"ERR"
#define AV_V_RESULT_NOTFOUND    	"NFD"
#define AV_V_TACTYPE_AUTH		"AUTH"
#define AV_V_TACTYPE_INFO		"INFO"
#define AV_V_TACTYPE_CHPW		"CHPW"
#define AV_V_TACTYPE_CHAL		"CHAL"
#define AV_V_AUTH_METHOD_APOP		"APOP"
#define AV_V_AUTH_METHOD_USER		"USER"
#define AV_V_AUTH_METHOD_CRAM_MD5	"CRAM-MD5"
#define AV_V_AUTH_METHOD_SCRAM_MD5	"SCRAM-MD5"
#define AV_V_AUTH_METHOD_DIGEST_MD5	"DIGEST-MD5"
#define AV_V_TRANSPORT_POP3		"pop3"
#define AV_V_TRANSPORT_QUOTA_TEMP	"quota-t"
#define AV_V_TRANSPORT_QUOTA_PERM	"quota-p"
#define AV_V_TRANSPORT_BOUNCE		"bounce"
#define AV_V_TRANSPORT_DEFER		"defer"
#define AV_V_TRANSPORT_FORWARD		"forward"
#define AV_V_TRANSPORT_LIST		"list"
typedef struct av_ctx av_ctx;

struct av_ctx {
    char *arr[AV_A_ARRAYSIZE];
    void *app_cb;
    void *app_ctx;
};

typedef struct mavis_ctx mavis_ctx;
struct sym;
struct mavis_action;

struct mavis_ctx {
    void *handle;
    int (*append)(mavis_ctx *, void *);
    int (*init)(mavis_ctx *);
    int (*parse)(mavis_ctx *, struct sym *, char *);
    int (*send)(mavis_ctx *, av_ctx **);
    int (*recv)(mavis_ctx *, av_ctx **, void *);
    int (*cancel)(mavis_ctx *, void *);
    void *(*drop)(mavis_ctx *);
    mavis_ctx *down;
    mavis_ctx *top;
    av_ctx *ac_bak;
    int ac_bak_required;
    int last_result;
    struct mavis_action *script_in;
    struct mavis_action *script_out;
    struct io_context *io;
#ifdef MAVIS_CTX_PRIVATE
     MAVIS_CTX_PRIVATE
#endif
    char identifier[1];
};

/* Module handling: */
int mavis_method_add(mavis_ctx **, struct io_context *ctx, char *, char *);
int mavis_init(mavis_ctx *, char *);
int mavis_cancel(mavis_ctx *, void *);
int mavis_drop(mavis_ctx *);
int mavis_send(mavis_ctx *, av_ctx **);
int mavis_recv(mavis_ctx *, av_ctx **, void *);
int mavis_parse(mavis_ctx *, struct sym *, char *);

int get_syslog_level(char *);
int get_syslog_facility(char *);

/* Attribute-value handling: */
av_ctx *av_new(void *, void *);
void av_setcb(av_ctx *, void *, void *);
void av_free(av_ctx *);
void av_free_private(av_ctx *);
char *av_get(av_ctx *, int);
void av_set(av_ctx *, int, char *);
void av_setf(av_ctx *, int, char *, ...)
    __attribute__((format(printf, 3, 4)));
#define av_unset(A,B) av_set(A,B, NULL)
void av_clear(av_ctx *);
void av_dump(av_ctx *);
void av_move(av_ctx *, av_ctx *);
void av_merge(av_ctx *, av_ctx *);
void av_copy(av_ctx *, av_ctx *);
int av_array_to_char(av_ctx *, char *, size_t, fd_set *);
int av_char_to_array(av_ctx *, char *, fd_set *);
int av_attribute_to_i(char *);

char *av_addserial(av_ctx *);

#define MAX_INPUT_LINE_LEN 4096

struct token_chain;

struct sym {
    char *filename;
    char buf[MAX_INPUT_LINE_LEN];	/* parse buffer */
    char *start;
    char *raw;
    int pos;			/* current place in buf */
    u_int line:29;		/* current line number for parsing */
    u_int flag_parse_pcre:1;
    u_int flag_prohibit_include:1;
    u_int quoted:1;
    char ch[4];			/* current parse character */
    char chlen;			/* current parse character length */
    enum token code;		/* parser output */
    char *in;			/* input buffer start */
    int len;			/* input buffer length */
    char *tin;			/* pointer to remaining input buffer */
    int tlen;			/* length of remaining input buffer */
    jmp_buf env;		/* saved stack context for parse errors */
    int env_valid;
    struct token_chain *token_chain;
    struct sym *next;
};

void parse_error(struct sym *, char *, ...);
void parse_error_expect(struct sym *, enum token, ...);
enum token parse_permission(struct sym *);
u_int parse_bool(struct sym *);
void parse(struct sym *, enum token);
void getsym(struct sym *);
void buf_add(struct sym *, char);
void sym_get(struct sym *);
enum token sym_peek(struct sym *);
void cfg_read_config(char *, void (*)(struct sym *), char *);
enum token keycode(char *);
int parse_int(struct sym *);
u_int parse_uint(struct sym *);
int parse_seconds(struct sym *);
void sym_init(struct sym *);
void report_cfg_error(int, int, char *, ...)
    __attribute__((format(printf, 3, 4)));
void parse_debug(struct sym *, u_int *);
int parse_comma(struct sym *);

void parse_userid(struct sym *, uid_t *, gid_t *);
void parse_groupid(struct sym *, gid_t *);
void parse_umask(struct sym *, mode_t *);

void parse_mavispath(struct sym *);
int parse_mavismodule(mavis_ctx **, struct io_context *, struct sym *);

struct common_data {
    struct io_context *io;
    char *progname;
    char *progpath;
    char *version;
    u_int version_only;
    u_int parse_only;
    u_int debug;
    u_int debug_redirected;
    int syslog_level;
    int syslog_facility;
    u_int syslog_dflt:1;
    unsigned long long regex_match_case;
#if defined(WITH_PCRE) || defined(WITH_PCRE2)
    int regex_pcre_flags;
#endif
    int regex_posix_flags;
    char *syslog_ident;
    char *proctitle;
    char *coredumpdir;
    char *gcorepath;
    char *debug_cmd;
    int debugtty;
    pid_t pid;
    int users_min;
    int users_max;
    int users_cur;
    int users_max_total;
    int servers_min;
    int servers_max;
    int servers_cur;
    char *font_black;
    char *font_red;
    char *font_green;
    char *font_yellow;
    char *font_blue;
    char *font_magenta;
    char *font_cyan;
    char *font_white;
    char *font_plain;
    char *font_bold;
    u_long ipc_key;
    char *ipc_url;
    char **argv;
    char **envp;
    struct pidfile *pidfile;
    int singleprocess;
    char *conffile;
    char *id;
    int (*scm_send_msg)(int, struct scm_data *, int);
    int (*scm_recv_msg)(int, struct scm_data_accept *, size_t, int *);
    void (*scm_accept)(int, struct scm_data_accept *);
};

extern struct common_data common_data;

#define case_CC_Tokens \
        case S_trace:\
        case S_debug:\
        case S_syslog:\
        case S_proctitle:\
        case S_coredump: parse_common(sym); continue

void init_common_data(void);
void parse_common(struct sym *);
void common_usage(void);

void mavis_script_parse(mavis_ctx *, struct sym *);
enum token mavis_script_eval(mavis_ctx *, av_ctx *, struct mavis_action *);
void mavis_script_drop(struct mavis_action **);

struct mavis_tm {
    struct mavis_tm *next;
    char *string;
    unsigned long long min;	/* minute, 0-59 */
    unsigned long hour;		/* hour, 0-23 */
    unsigned long mday;		/* day of month, 1-31 */
    unsigned long mon;		/* month, 1-12 */
    unsigned long wday;		/* day of week, 1-7 */
};

struct mavis_timespec {
    struct mavis_tm *tm;
    char *string;
    int matched;		/* 0 == no match */
    time_t valid_until;
    char name[1];
};

int eval_timespec(struct mavis_timespec *, char **);

int parse_cron(struct mavis_tm *, char *);
void parse_timespec(rb_tree_t *, struct sym *);
struct mavis_timespec *find_timespec(rb_tree_t *, char *);
rb_tree_t *init_timespec(void);

int cfg_open_and_read(char *, char **, int *);
int cfg_close(char *, char *, int);
int ipc_create(char *, int);
void ipc_delete(void);

int mavis_check_version(char *);

void mavis_detach(void);

#if defined(MAVIS_name) && defined(DEBUG)
#undef DebugIn
#undef DebugOut
#define DebugIn(A) Debug  ((A, "+ "MAVIS_name":%s\n", __func__))
#define DebugOut(A) Debug ((A, "- "MAVIS_name":%s\n", __func__))
#endif

#define CHAREOF (char)EOF

#endif				/* __MAVIS_H_ */
