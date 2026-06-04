/*
 * osc_star_printer.c
 *
 * OSC-to-Star-printer listener + web/Supabase enable control for Teltonika RUT240.
 *
 * Adds hardware output support:
 *   Web enabled  -> DOUT1 output ON
 *   Web disabled -> DOUT1 output OFF
 *
 * Supported OSC print:
 *   /print "message"
 *   /print "name" "message"
 *
 * Supported OSC control:
 *   /web/enable
 *   /web/disable
 *   /web/toggle
 *   /web/status
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_LISTEN_PORT 9000
#define DEFAULT_PRINTER_PORT 9100
#define DEFAULT_PRINTER_HOST "192.168.178.121"
#define DEFAULT_NAME "OSC"
#define DEFAULT_WEB_ENABLE_FILE "/tmp/star_web_enabled"
#define DEFAULT_DOUT_NAME "DOUT1"
#define PRINT_WIDTH 32

#define OSC_BUF_SIZE 4096
#define PRINT_BUF_SIZE 8192

static volatile sig_atomic_t g_running = 1;

static const uint8_t ESC = 0x1b;
static const uint8_t FS  = 0x1c;
static const uint8_t GS  = 0x1d;
static const uint8_t RS  = 0x1e;
static const uint8_t LF  = 0x0a;

typedef struct { uint8_t data[PRINT_BUF_SIZE]; size_t len; } ByteBuf;
typedef struct { const uint8_t *data; size_t len; size_t off; } OscReader;

typedef enum {
    OSC_IGNORE = 0,
    OSC_PRINT,
    OSC_WEB_ENABLE,
    OSC_WEB_DISABLE,
    OSC_WEB_TOGGLE,
    OSC_WEB_STATUS
} OscAction;

static const char *g_web_enable_file = DEFAULT_WEB_ENABLE_FILE;
static const char *g_dout_name = DEFAULT_DOUT_NAME;
static int g_use_hardware_output = 1;

static void on_signal(int sig) { (void)sig; g_running = 0; }

static void log_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[osc_star] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/*
 * Set the RUT240 hardware output.
 *
 * Newer RutOS often supports:
 *   ubus call ioman.gpio.dout1 update '{"value":"1"}'
 *
 * Older/legacy RUT240 firmware often supports:
 *   gpio.sh get DOUT1
 *   gpio.sh invert DOUT1
 *
 * We try ubus first, then fall back to gpio.sh get/invert.
 */
static int set_hardware_output(int enabled) {
    if (!g_use_hardware_output) return 0;

    char cmd[512];
    int rc;

    snprintf(
        cmd,
        sizeof(cmd),
        "ubus call ioman.gpio.dout1 update '{\"value\":\"%d\"}' >/dev/null 2>&1",
        enabled ? 1 : 0
    );

    rc = system(cmd);
    if (rc == 0) {
        log_msg("hardware output DOUT1 set %s via ubus", enabled ? "ON" : "OFF");
        return 0;
    }

    /* Fallback: gpio.sh. Some firmware has only get + invert. */
    snprintf(cmd, sizeof(cmd), "gpio.sh get %s 2>/dev/null", g_dout_name);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        log_msg("hardware output set failed: no ubus and gpio.sh read failed");
        return -1;
    }

    char buf[64];
    if (!fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        log_msg("hardware output set failed: gpio.sh returned no value");
        return -1;
    }
    pclose(fp);

    int current = atoi(buf) ? 1 : 0;
    int target = enabled ? 1 : 0;

    if (current == target) {
        log_msg("hardware output %s already %s", g_dout_name, enabled ? "ON" : "OFF");
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "gpio.sh invert %s >/dev/null 2>&1", g_dout_name);
    rc = system(cmd);

    if (rc == 0) {
        log_msg("hardware output %s set %s via gpio.sh invert", g_dout_name, enabled ? "ON" : "OFF");
        return 0;
    }

    log_msg("hardware output set failed: gpio.sh invert failed");
    return -1;
}

static int web_enabled(void) {
    FILE *f = fopen(g_web_enable_file, "r");
    if (!f) return 1; /* default enabled */

    int c = fgetc(f);
    fclose(f);

    return c != '0';
}

static int set_web_enabled(int enabled) {
    FILE *f = fopen(g_web_enable_file, "w");
    if (!f) {
        log_msg("could not write %s: %s", g_web_enable_file, strerror(errno));
        return -1;
    }

    fputc(enabled ? '1' : '0', f);
    fputc('\n', f);
    fclose(f);

    log_msg("web/Supabase prints %s", enabled ? "ENABLED" : "DISABLED");

    set_hardware_output(enabled);
    return 0;
}

static int bb_put(ByteBuf *b, uint8_t v) {
    if (b->len >= sizeof(b->data)) return -1;
    b->data[b->len++] = v;
    return 0;
}

static int bb_write(ByteBuf *b, const void *src, size_t n) {
    if (b->len + n > sizeof(b->data)) return -1;
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return 0;
}

static int bb_cstr(ByteBuf *b, const char *s) { return bb_write(b, s, strlen(s)); }

static void star_init(ByteBuf *b) { bb_put(b, ESC); bb_put(b, '@'); }
static void star_two_colour_on(ByteBuf *b) { bb_put(b, ESC); bb_put(b, RS); bb_put(b, 'C'); bb_put(b, 1); }
static void star_align(ByteBuf *b, int n) { bb_put(b, ESC); bb_put(b, GS); bb_put(b, 'a'); bb_put(b, (uint8_t)n); }
static void star_black(ByteBuf *b) { bb_put(b, ESC); bb_put(b, '5'); }
static void star_bold(ByteBuf *b, int on) { bb_put(b, ESC); bb_put(b, on ? 'E' : 'F'); }
static void star_double_width(ByteBuf *b, int on) { bb_put(b, ESC); bb_put(b, 'W'); bb_put(b, on ? 1 : 0); }
static void star_lf(ByteBuf *b) { bb_put(b, LF); }
static void star_logo(ByteBuf *b, int logo_number, int logo_mode) {
    bb_put(b, ESC); bb_put(b, FS); bb_put(b, 'p'); bb_put(b, (uint8_t)logo_number); bb_put(b, (uint8_t)logo_mode);
}
static void star_feed_lines(ByteBuf *b, int n) {
    if (n < 1) n = 1;
    if (n > 127) n = 127;
    bb_put(b, ESC); bb_put(b, 'a'); bb_put(b, (uint8_t)n);
}
static void star_cut_partial_after_feed(ByteBuf *b) { bb_put(b, ESC); bb_put(b, 'd'); bb_put(b, 3); }

static void normalise_text(const char *in, char *out, size_t out_sz) {
    size_t oi = 0;
    const unsigned char *p = (const unsigned char *)in;
    if (out_sz == 0) return;

    while (*p && oi + 1 < out_sz) {
        if (p[0] == 0xE2 && p[1] == 0x80 &&
            (p[2] == 0x98 || p[2] == 0x99 || p[2] == 0x9A || p[2] == 0x9B)) {
            out[oi++] = '\''; p += 3; continue;
        }
        if (p[0] == 0xE2 && p[1] == 0x80 &&
            (p[2] == 0x9C || p[2] == 0x9D || p[2] == 0x9E)) {
            out[oi++] = '"'; p += 3; continue;
        }
        if (p[0] == 0xE2 && p[1] == 0x80 &&
            (p[2] == 0x93 || p[2] == 0x94)) {
            out[oi++] = '-'; p += 3; continue;
        }
        if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0xA6) {
            if (oi + 3 < out_sz) { out[oi++]='.'; out[oi++]='.'; out[oi++]='.'; }
            p += 3; continue;
        }
        if (p[0] == 0xC2 && p[1] == 0xA0) { out[oi++] = ' '; p += 2; continue; }
        if (*p == '\n' || *p == '\r' || *p == '\t' || (*p >= 0x20 && *p <= 0x7E)) {
            char c = (char)*p++;
            if (c == '\r' || c == '\t') c = ' ';
            out[oi++] = c;
            continue;
        }
        out[oi++] = '?'; p++;
    }
    out[oi] = '\0';
}

static void append_line(ByteBuf *b, const char *s) { bb_cstr(b, s); star_lf(b); }

static void append_wrapped_text(ByteBuf *b, const char *text) {
    char norm[2048];
    normalise_text(text, norm, sizeof(norm));

    char line[PRINT_WIDTH + 1];
    int line_len = 0;
    char word[256];
    int word_len = 0;

    for (size_t i = 0;; i++) {
        char c = norm[i];
        int at_end = (c == '\0');
        int is_space = (c == ' ' || c == '\n' || at_end);

        if (!is_space) {
            if (word_len < (int)sizeof(word) - 1) word[word_len++] = c;
            continue;
        }

        word[word_len] = '\0';

        if (word_len > 0) {
            int needed = word_len + (line_len > 0 ? 1 : 0);
            if (line_len > 0 && line_len + needed > PRINT_WIDTH) {
                line[line_len] = '\0'; append_line(b, line); line_len = 0;
            }
            if (line_len > 0 && line_len < PRINT_WIDTH) line[line_len++] = ' ';
            for (int wi = 0; wi < word_len && line_len < PRINT_WIDTH; wi++) line[line_len++] = word[wi];

            int wi = PRINT_WIDTH;
            while (wi < word_len) {
                line[line_len] = '\0'; append_line(b, line); line_len = 0;
                for (; wi < word_len && line_len < PRINT_WIDTH; wi++) line[line_len++] = word[wi];
            }
        }

        word_len = 0;

        if (c == '\n') {
            if (line_len > 0) { line[line_len] = '\0'; append_line(b, line); line_len = 0; }
            else star_lf(b);
        }

        if (at_end) break;
    }

    if (line_len > 0) { line[line_len] = '\0'; append_line(b, line); }
}

static int build_print_job(ByteBuf *b, const char *name, const char *message) {
    char clean_name[256];
    normalise_text(name && *name ? name : DEFAULT_NAME, clean_name, sizeof(clean_name));

    b->len = 0;

    star_init(b);
    star_two_colour_on(b);

    star_align(b, 1);
    star_logo(b, 1, 0);
    star_lf(b);

    star_black(b);
    star_bold(b, 1);
    star_double_width(b, 1);
    append_line(b, "NEW MESSAGE");
    star_double_width(b, 0);
    star_bold(b, 0);
    star_lf(b);

    star_align(b, 0);
    star_black(b);
    star_bold(b, 1);
    bb_cstr(b, "From: ");
    append_line(b, clean_name);
    star_bold(b, 0);
    append_line(b, "--------------------------------");

    append_wrapped_text(b, message);

    star_black(b);
    star_align(b, 0);
    star_feed_lines(b, 3);
    star_cut_partial_after_feed(b);

    return 0;
}

static int send_to_printer(const char *host, int port, const uint8_t *data, size_t len) {
    int sock = -1;
    struct sockaddr_in addr;
    struct hostent *he = gethostbyname(host);

    if (!he) { log_msg("gethostbyname failed for %s", host); return -1; }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { log_msg("socket failed: %s", strerror(errno)); return -1; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr, (size_t)he->h_length);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_msg("connect to printer failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) { log_msg("send failed: %s", strerror(errno)); close(sock); return -1; }
        sent += (size_t)n;
    }

    usleep(1000 * 1000);
    shutdown(sock, SHUT_WR);
    usleep(200 * 1000);
    close(sock);
    log_msg("sent %lu bytes to printer", (unsigned long)len);
    return 0;
}

static int osc_align(size_t n) { return (int)((n + 3u) & ~3u); }

static int osc_read_string(OscReader *r, char *out, size_t out_sz) {
    if (r->off >= r->len) return -1;
    size_t start = r->off, p = start;
    while (p < r->len && r->data[p] != 0) p++;
    if (p >= r->len) return -1;
    size_t slen = p - start;
    if (slen >= out_sz) slen = out_sz - 1;
    memcpy(out, r->data + start, slen);
    out[slen] = '\0';
    r->off = (size_t)osc_align(p + 1);
    if (r->off > r->len) return -1;
    return 0;
}

static OscAction parse_osc(const uint8_t *packet, size_t len, char *name, size_t name_sz, char *msg, size_t msg_sz) {
    OscReader r = { packet, len, 0 };
    char addr[128], types[64];
    name[0] = '\0'; msg[0] = '\0';

    if (osc_read_string(&r, addr, sizeof(addr)) < 0) return OSC_IGNORE;
    if (osc_read_string(&r, types, sizeof(types)) < 0) return OSC_IGNORE;

    if (strcmp(addr, "/web/enable") == 0 || strcmp(addr, "/star/web/enable") == 0) return OSC_WEB_ENABLE;
    if (strcmp(addr, "/web/disable") == 0 || strcmp(addr, "/star/web/disable") == 0) return OSC_WEB_DISABLE;
    if (strcmp(addr, "/web/toggle") == 0 || strcmp(addr, "/star/web/toggle") == 0) return OSC_WEB_TOGGLE;
    if (strcmp(addr, "/web/status") == 0 || strcmp(addr, "/star/web/status") == 0) return OSC_WEB_STATUS;

    if (strcmp(addr, "/print") != 0 && strcmp(addr, "/star/print") != 0) {
        log_msg("ignored OSC address: %s", addr);
        return OSC_IGNORE;
    }

    if (strcmp(types, ",s") == 0) {
        snprintf(name, name_sz, "%s", DEFAULT_NAME);
        if (osc_read_string(&r, msg, msg_sz) < 0) return OSC_IGNORE;
        return OSC_PRINT;
    }

    if (strcmp(types, ",ss") == 0) {
        if (osc_read_string(&r, name, name_sz) < 0) return OSC_IGNORE;
        if (osc_read_string(&r, msg, msg_sz) < 0) return OSC_IGNORE;
        return OSC_PRINT;
    }

    log_msg("unsupported OSC typetag for print: %s", types);
    return OSC_IGNORE;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [-l listen_port] [-h printer_ip] [-p printer_port] [-f web_enable_file] [-o dout_name] [-n]\n"
        "\n"
        "  -l  OSC listen UDP port, default 9000\n"
        "  -h  Star printer IP, default 192.168.178.121\n"
        "  -p  Star printer TCP port, default 9100\n"
        "  -f  web enable flag file, default /tmp/star_web_enabled\n"
        "  -o  output name for gpio.sh fallback, default DOUT1\n"
        "  -n  no hardware output control\n"
        "\n"
        "Example:\n"
        "  %s -l 9000 -h 192.168.178.121 -p 9100 -f /tmp/star_web_enabled\n",
        argv0, argv0);
}

int main(int argc, char **argv) {
    int listen_port = DEFAULT_LISTEN_PORT;
    int printer_port = DEFAULT_PRINTER_PORT;
    const char *printer_host = DEFAULT_PRINTER_HOST;

    int opt;
    while ((opt = getopt(argc, argv, "l:h:p:f:o:n")) != -1) {
        switch (opt) {
            case 'l': listen_port = atoi(optarg); break;
            case 'h': printer_host = optarg; break;
            case 'p': printer_port = atoi(optarg); break;
            case 'f': g_web_enable_file = optarg; break;
            case 'o': g_dout_name = optarg; break;
            case 'n': g_use_hardware_output = 0; break;
            default: usage(argv[0]); return 1;
        }
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    /* Default enabled on boot, and output ON. */
    if (set_web_enabled(1) < 0) log_msg("warning: could not initialise web enable flag");

    int udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp < 0) { log_msg("UDP socket failed: %s", strerror(errno)); return 1; }

    int reuse = 1;
    setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((uint16_t)listen_port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(udp, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        log_msg("bind UDP %d failed: %s", listen_port, strerror(errno));
        close(udp);
        return 1;
    }

    log_msg("listening OSC UDP %d, printer %s:%d, web flag %s, output control %s",
            listen_port, printer_host, printer_port, g_web_enable_file,
            g_use_hardware_output ? "ON" : "OFF");

    while (g_running) {
        uint8_t packet[OSC_BUF_SIZE];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

        ssize_t n = recvfrom(udp, packet, sizeof(packet), 0, (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            log_msg("recvfrom failed: %s", strerror(errno));
            continue;
        }

        char name[256], msg[2048];
        OscAction action = parse_osc(packet, (size_t)n, name, sizeof(name), msg, sizeof(msg));

        if (action == OSC_WEB_ENABLE) { set_web_enabled(1); continue; }
        if (action == OSC_WEB_DISABLE) { set_web_enabled(0); continue; }
        if (action == OSC_WEB_TOGGLE) { set_web_enabled(!web_enabled()); continue; }
        if (action == OSC_WEB_STATUS) {
            log_msg("web/Supabase prints currently %s", web_enabled() ? "ENABLED" : "DISABLED");
            set_hardware_output(web_enabled());
            continue;
        }
        if (action != OSC_PRINT) continue;

        log_msg("OSC print from '%s': %s", name, msg);

        ByteBuf job;
        build_print_job(&job, name, msg);

        if (send_to_printer(printer_host, printer_port, job.data, job.len) < 0) {
            log_msg("print failed");
        }
    }

    close(udp);
    log_msg("stopped");
    return 0;
}
