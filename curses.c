/*
    mtr  --  a network diagnostic tool
    Copyright (C) 1997,1998  Matt Kimball

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "config.h"

#include <strings.h>
#include <unistd.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* MacOSX may need this before scoket.h...*/
#if defined(HAVE_SYS_TYPES_H)
#include <sys/types.h>
#else
/* If a system doesn't have sys/types.h, lets hope that time_t is an int */
#define time_t int
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#if defined(UNICODE)
#define _XOPEN_SOURCE_EXTENDED
#if defined (HAVE_NCURSESW_NCURSES_H)
#  include <ncursesw/ncurses.h>
#elif defined (HAVE_NCURSESW_CURSES_H)
#  include <ncursesw/curses.h>
#elif defined (HAVE_CURSES_H)
#  include <curses.h>
#else
#  error No ncursesw header file available
#endif
#if defined(HAVE_WCHAR_H)
#  include <wchar.h>
#endif
#ifdef NETBSD_CURSES
#define CCHAR_attr attributes
#define CCHAR_chars vals
/*
#elif defined OPENSOLARIS_CURSES
#define CCHAR_attr _at
#define CCHAR_chars _wc
*/
#else
#define CCHAR_attr attr
#define CCHAR_chars chars
#endif
#else

#if defined(HAVE_NCURSES_H)
#  include <ncurses.h>
#elif defined(HAVE_NCURSES_CURSES_H)
#  include <ncurses/curses.h>
#elif defined(HAVE_CURSES_H)
#  include <curses.h>
#elif defined(HAVE_CURSESX_H)
#  include <cursesX.h>
#else
#  error No curses header file available
#endif

#endif // UNICODE endif

#ifndef HAVE_ATTRON
#define attron(x)
#define attroff(x)
#endif

#ifndef getmaxyx
#  define getmaxyx(win,y,x)	((y) = (win)->_maxy + 1, (x) = (win)->_maxx + 1)
#endif

#include "mtr.h"
#include "mtr-curses.h"
#include "net.h"
#include "dns.h"
#ifdef IPINFO
#include "ipinfo.h"
#endif
#include "display.h"

#include "version.h"

#include <time.h>

extern char LocalHostname[];
extern char mtr_args[];
extern unsigned iargs;
extern int fstTTL;
extern int cpacketsize;
extern int bitpattern;
extern int tos;
extern float WaitTime;

static int __unused_int;

void mtr_fill_buf(int y, char *buf) {
	move(2, y);
	int curs = curs_set(1);
	refresh();
	int c;
	int i = 0;
	while ((c=getch()) != '\n' && i < MAXFLD) {
		addch(c | A_BOLD); refresh();
		buf[i++] = c;   /* need more checking on 'c' */
	}
	if (curs != ERR)
		curs_set(curs);
	buf[i] = '\0';
}

#define CURSES_HELP_MESSAGE \
  "Command:\n" \
  "  ?|h     help\n" \
  "  b <c>   set ping bit pattern to c(0..255) or random(c<0)\n" \
  "  c <n>   report cycle n, default n=infinite\n" \
  "  d       switching display mode\n" \
  "  e       toggle MPLS information on/off\n" \
  "  f <n>   set the initial time-to-live(ttl), default n=1\n" \
  "  i <n>   set the ping interval to n seconds, default n=1\n" \
  "  j       toggle latency(LS NABWV)/jitter(DR AGJMXI) stats\n" \
  "  m <n>   set the max time-to-live, default n= # of hops\n" \
  "  n       toggle DNS on/off\n" \
  "  o str   set the columns to display, default str='LRS N BAWV'\n" \
  "  p|SPACE pause/resume)\n" \
  "  Q <t>   set ping packet's TOS to t\n" \
  "  r       reset all counters\n" \
  "  s <n>   set the packet size to n or random(n<0)\n" \
  "  t       switch between ICMP ECHO and TCP SYN\n" \
  "  u       switch between ICMP ECHO and UDP datagrams\n"

int mtr_curses_keyaction(void)
{
  int c = getch();
  float f = 0.0;
  char buf[MAXFLD+1];

  switch (tolower(c)) {
    case '?':
    case 'h':
      erase();
      mvprintw(2, 0, "%s", CURSES_HELP_MESSAGE);
#ifdef IPINFO
      printw("  y       switching IP Info\n");
      printw("  Y       show hops on GoogleMaps (in -y[6-9] modes)\n");
      printw("  z       toggle ASN Lookup on/off\n");
#endif
      addch('\n');
      printw(" press any key to go back...");
      getch();
      return ActionNone;
    case 'b':
      mvprintw(2, 0, "Ping Bit Pattern: %d\n", bitpattern);
      mvprintw(3, 0, "Pattern Range: 0(0x00)-255(0xff), <0 random.\n");
      mtr_fill_buf(18, buf);
      bitpattern = atoi(buf);
      if (bitpattern > 255)
        bitpattern = -1;
      return ActionNone;
    case 'd':
      return ActionDisplay;
    case 'e':
      return ActionMPLS;
    case 'f': {
      mvprintw(2, 0, "First TTL: %d\n\n", fstTTL);
      mtr_fill_buf(11, buf);
      int i = atoi(buf);
      if ((i < 1) || (i > maxTTL))
        return ActionNone;
      fstTTL = i;
      return ActionNone;
    }
    case 'i':
      mvprintw(2, 0, "Interval: %0.0f\n\n", WaitTime);
      mtr_fill_buf(11, buf);
      f = atof(buf);
      if (f <= 0.0)
        return ActionNone;
      if (getuid() && (f < 1.0))
        return ActionNone;
      WaitTime = f;
      return ActionNone;
    case 'j':
      TGLBIT(iargs, 6);	// 6th bit: latency/jitter
      strcpy(fld_active, index(fld_active, 'N') ? "DR AGJMXI" /* jitter */ : "LS NABWV" /* default */);
      return ActionNone;
    case 'm': {
      mvprintw(2, 0, "Max TTL: %d\n\n", maxTTL);
      mtr_fill_buf(9, buf);
      int i = atoi(buf);
      if ((i < fstTTL) || (i > (MaxHost - 1)))
        return ActionNone;
      maxTTL = i;
      return ActionNone;
    }
    case 'n':
      return ActionDNS;
    case 'o': {	// fields to display & their ordering
      mvprintw(2, 0, "Fields: %s\n\n", fld_active);
      int i;
      for (i = 0; i < MAXFLD; i++)
        if (data_fields[i].descr)
        printw("  %s\n", data_fields[i].descr);
      addch('\n');
      move(2,8);	// length of "Fields: "
      refresh();

      i = 0;
      int curs = curs_set(1);
      while ((c = getch()) != '\n' && (i < MAXFLD))
        if (strchr(available_options, c)) {
          addch(c | A_BOLD);
          refresh();
          buf[i++] = c;	// Only permit values in "available_options" be entered
        } else
          printf("\a");	// Illegal character. Beep, ring the bell.
      if (curs != ERR)
        curs_set(curs);
      buf[i] = '\0';
      if (i)
        strcpy(fld_active, buf);
      return ActionNone;
    }
    case 'p':
      return ActionPauseResume;
    case 'r':
      return ActionReset;
    case 's':
      mvprintw(2, 0, "Change Packet Size: %d\n", cpacketsize);
      mvprintw(3, 0, "Size Range: %d-%d, < 0:random.\n", MINPACKET, MAXPACKET);
      mtr_fill_buf(20, buf);
      cpacketsize = atoi(buf);
      return ActionNone;
    case 't':
      return ActionTCP;
    case 'u':
      iargs &= ~3;
      if (mtrtype != IPPROTO_UDP) {
        mtrtype = IPPROTO_UDP;
        SETBIT(iargs, 0);	// 1st bit: UDP mode
      } else
        mtrtype = IPPROTO_ICMP;
      return ActionNone;
#ifdef IPINFO
    case 'z':
      return ActionAS;
#endif
  }
  if(c == 'q')
    return ActionQuit;
  if(c == 3)
     return ActionQuit;
  if (c == 12)
     return ActionClear;
  if ((c == 17) || (c == 19) || (c == ' '))
     return ActionPauseResume;
#ifdef IPINFO
  if (c == 'y')
    return ActionII;
  if (c == 'Y')
    return ActionII_Map;
#endif
  if (c == '+')
    return ActionScrollDown;
  if (c == '-')
    return ActionScrollUp;

 if ( c == 'Q') {    /* can not be tolower(c) */
    mvprintw(2, 0, "Type of Service(tos): %d\n", tos );
    mvprintw(3, 0, "default 0x00, min cost 0x02, rel 0x04,, thr 0x08, low del 0x10...\n");
    mtr_fill_buf(22, buf);
    tos = atoi(buf);
    if( tos > 255 || tos <0 ) {
      tos = 0;
    }
    return ActionNone;
  }
  return ActionNone;          /* ignore unknown input */
}

void mtr_fill_data(int at, char *buf) {
  int hd_len = 0;
  int i;
  /* net_xxx returns times in usecs. Just display millisecs */
  for (i = 0; i < MAXFLD; i++) {
	/* Ignore options that don't exist */
	/* On the other hand, we now check the input side. Shouldn't happen,
	   can't be careful enough. */
    int j = fld_index[fld_active[i]];
    if (j < 0)
      continue;

	/* temporay hack for stats usec to ms... */
#define CURSES_FLD(factor) { sprintf(buf + hd_len, data_fields[j].format, net_elem(at, data_fields[j].key) factor); }
    if (index(data_fields[j].format, 'f'))
      CURSES_FLD(/1000.0)
    else
      CURSES_FLD();
    hd_len += data_fields[j].length;
  }
  buf[hd_len] = 0;
}

void printw_mpls(struct mplslen *mpls) {
  int i;
  for (i = 0; i < mpls->labels; i++)
    printw("    [MPLS: Lbl %lu Exp %u S %u TTL %u]\n", mpls->label[i], mpls->exp[i], mpls->s[i], mpls->ttl[i]);
}

void printw_addr(ip_t *addr, int up) {
#ifdef IPINFO
  if (ii_ready())
    printw(fmt_ipinfo(addr));
#endif
  if (!up)
    attron(A_BOLD);
  const char *name = dns_lookup(addr);
  if (name) {
    printw("%s", name);
    if (show_ips)
      printw(" (%s)", strlongip(addr));
  } else
    printw("%s", strlongip(addr));
  if (!up)
    attroff(A_BOLD);
}

void mtr_curses_hosts(int startstat) {
  int max = net_max();
  int at;
  for (at = net_min() + display_offset; at < max; at++) {
    printw("%2d. ", at + 1);
    ip_t *addr = &host[at].addr;
    if (!unaddrcmp(addr)) {
      printw("???\n");
      continue;
    }

    printw_addr(addr, host[at].up);
    { // print stat
      char buf[1024];
      mtr_fill_data(at, buf);
      int y;
      getyx(stdscr, y, __unused_int);
      move(y, startstat);
      printw("%s", buf);
    }
    addch('\n');
    if (enablempls)
      printw_mpls(&host[at].mpls);

    /* Multi path */
    int i;
    for (i = 0; i < MAXPATH; i++) {
      ip_t *addrs = &(host[at].addrs[i]);
      if (!addrcmp(addrs, addr))
        continue;
      if (!unaddrcmp(addrs))
        break;

      printw("    ");
      printw_addr(addrs, host[at].up);
      addch('\n');
      if (enablempls)
        printw_mpls(&(host[at].mplss[i]));
    }
  }
  move(2, 0);
}

static int low_ms, high_ms;

static chtype map1[] = {'.' | A_NORMAL, '>' | COLOR_PAIR(1)};
#define NUM_FACTORS2	8
static chtype map2[NUM_FACTORS2];
static double factors2[NUM_FACTORS2];
static int scale2[NUM_FACTORS2];
static int dm2_color_base;
static chtype map_na2[] = { ' ', '?', '>' | A_BOLD};
#ifdef UNICODE
#define NUM_FACTORS3_MONO	7	// without trailing char
#define NUM_FACTORS3		22
static cchar_t map3[NUM_FACTORS3];
static double factors3[NUM_FACTORS3];
static int scale3[NUM_FACTORS3];
static int dm3_color_base;
static cchar_t map_na3[3];
#endif

void mtr_gen_scale(int num_factors, int *scale, double *factors) {
	low_ms = 1000000;
	high_ms = -1;
	int max = net_max();
	int at;
	for (at = display_offset; at < max; at++) {
		int i;
		for (i = 0; i < SAVED_PINGS; i++) {
			int saved = host[at].saved[i];
			if (saved < 0)
				continue;
			if (saved < low_ms)
				low_ms = saved;
			if (saved > high_ms)
				high_ms = saved;
		}
	}
	int range = high_ms - low_ms;
	int i;
	for (i = 0; i < num_factors; i++)
		scale[i] = low_ms + ((double)range * factors[i]);
}

void mtr_gen_scale_gc(void) {
#ifdef UNICODE
    if (display_mode == 3)
      mtr_gen_scale(color_mode ? NUM_FACTORS3 : (NUM_FACTORS3_MONO + 1), scale3, factors3);
    else
#endif
      mtr_gen_scale(NUM_FACTORS2, scale2, factors2);
}

void mtr_curses_factors_init(int num_factors, double *factors) {
	/* Initialize factors to a log scale. */
	int i;
	for (i = 0; i < num_factors; i++) {
		factors[i] = ((double)1 / num_factors) * (i + 1);
		factors[i] *= factors[i]; /* Squared. */
	}
}

void mtr_curses_init(void) {
	// display mode 2
	mtr_curses_factors_init(NUM_FACTORS2, factors2);
#ifdef UNICODE
	// display mode 3
	mtr_curses_factors_init(color_mode ? NUM_FACTORS3 : (NUM_FACTORS3_MONO + 1), factors3);
#endif

	/* Initialize block_map. */
	int block_split;
	block_split = (NUM_FACTORS2 - 2) / 2;
	if (block_split > 9) {
		block_split = 9;
	}
	int i;
	for (i = 1; i <= block_split; i++) {
		map2[i] = '0' + i;
	}
	for (i = block_split + 1; i < NUM_FACTORS2 - 1; i++) {
		map2[i] = 'a' + i - block_split - 1;
	}
	map2[0] = map1[0];
	for (i = 1; i < NUM_FACTORS2 - 1; i++)
		map2[i] |= COLOR_PAIR(dm2_color_base + i - 1);
	map2[NUM_FACTORS2 - 1] = map1[1] & A_CHARTEXT;
	map2[NUM_FACTORS2 - 1] |= (map2[NUM_FACTORS2 - 2] & A_ATTRIBUTES) | A_BOLD;

	map_na2[1] |= color_mode ? (map2[NUM_FACTORS2 - 1] & A_ATTRIBUTES) : A_BOLD;

#ifdef UNICODE
	for (i = 0; i < NUM_FACTORS3_MONO; i++)
		map3[i].CCHAR_chars[0] = L'▁' + i;

	if (color_mode) {
		for (i = 0; i < NUM_FACTORS3 - 1; i++) {
			int base = i / NUM_FACTORS3_MONO;
			map3[i].CCHAR_attr = COLOR_PAIR(dm3_color_base + base);
			if (i >= NUM_FACTORS3_MONO)
				map3[i].CCHAR_chars[0] = map3[i % NUM_FACTORS3_MONO].CCHAR_chars[0];
		}
		map3[NUM_FACTORS3 - 1].CCHAR_chars[0] = map1[1] & A_CHARTEXT;
		map3[NUM_FACTORS3 - 1].CCHAR_attr = map3[NUM_FACTORS3 - 2].CCHAR_attr | A_BOLD;
	} else
		map3[NUM_FACTORS3_MONO].CCHAR_chars[0] = map1[1] & A_CHARTEXT;

	for (i = 0; i < (sizeof(map_na2) / sizeof(map_na2[0])); i++)
		map_na3[i].CCHAR_chars[0] = map_na2[i] & A_CHARTEXT;
	map_na3[1].CCHAR_attr = color_mode ? map3[NUM_FACTORS3 - 1].CCHAR_attr : A_BOLD;
	map_na3[2].CCHAR_attr = A_BOLD;
#endif
}

chtype mtr_saved_ch(int saved_int) {
	if (saved_int == -2)
		return map_na2[0];	// unsent
	if (saved_int == -1)
		return map_na2[1];	// has not responded
	if (display_mode == 1) {
		return map1[(saved_int <= scale2[NUM_FACTORS2 - 2]) ? 0 : 1];
	} else if (display_mode == 2) {
		int i;
		for (i = 0; i < NUM_FACTORS2; i++)
			if (saved_int <= scale2[i])
				return map2[i];
	}
	return map_na2[2];	// ???
}

#ifdef UNICODE
cchar_t* mtr_saved_cc(int saved_int) {
	if (saved_int == -2)
		return &map_na3[0];
	if (saved_int == -1)
		return &map_na3[1];
	int num_factors = color_mode ? NUM_FACTORS3 : (NUM_FACTORS3_MONO + 1);
	int i;
	for (i = 0; i < num_factors; i++)
		if (saved_int <= scale3[i])
			return &map3[i];
	return &map_na3[2];	// ???
}
#endif

void mtr_curses_graph(int startstat, int cols)
{
	int max, at, y;
	ip_t * addr;

	max = net_max();
	for (at = net_min() + display_offset; at < max; at++) {
		printw("%2d. ", at+1);

		addr = &host[at].addr;
		if (!addr || !unaddrcmp(addr)) {
			printw("???\n");
			continue;
		}

		if (!host[at].up)
			attron(A_BOLD);
		if (unaddrcmp(addr)) {
#ifdef IPINFO
		if (ii_ready())
			printw(fmt_ipinfo(addr));
#endif
			const char *name = dns_lookup(addr);
			printw("%s", name?name:strlongip(addr));
		} else
			printw("???");
		attroff(A_BOLD);

		getyx(stdscr, y, __unused_int);
		move(y, startstat);

		addch(' ');
		int i;
#ifdef UNICODE
		if (display_mode == 3)
			for (i = SAVED_PINGS - cols; i < SAVED_PINGS; i++)
				add_wch(mtr_saved_cc(host[at].saved[i]));
		else
#endif
			for (i = SAVED_PINGS - cols; i < SAVED_PINGS; i++)
				addch(mtr_saved_ch(host[at].saved[i]));
		addch('\n');
	}
}

int mtr_curses_data_fields(char *buf) {
	int hd_len = 0;
	char fmt[16];

	int i;
	for (i = 0; i < MAXFLD; i++ ) {
		int j = fld_index[fld_active[i]];
		if (j < 0)
			continue;
		sprintf(fmt, "%%%ds", data_fields[j].length);
		sprintf(buf + hd_len, fmt, data_fields[j].title);
		hd_len += data_fields[j].length;
	}
	return hd_len;
}

#ifdef UNICODE
void mtr_print_scale3(int num_factors, int i0, int di) {
	int i;
	for (i = i0; i < num_factors; i += di) {
//		printw("  %lc:%dms", map3[i].CCHAR_chars[0], scale3[i] / 1000);
		addstr("  ");
		add_wch(&map3[i]);
		int sc = scale3[i] / 1000;
		if (sc)
			printw(":%dms", sc);
		else
			printw(":%.1fms", (double)scale3[i] / 1000);
	}
	addstr("  ");
	add_wch(&map3[num_factors]);
}
#endif

int chk_n_print(int bit, char *buf, int sz, const char *msg) {
  int l = 0;
  if (CHKBIT(iargs, bit)) {
    if (iargs & ((1 << bit) - 1))	// is there smth before?
      l = snprintf(buf, sz, ", ");
    l += snprintf(buf + l, sz - l, "%s", msg);
  }
  return l;
}

void mtr_curses_redraw(void) {
  int maxx, maxy;
  int startstat;
  int rowstat;
  time_t t;

  int i;
  int  hd_len;
  char buf[1024];


  erase();
  getmaxyx(stdscr, maxy, maxx);

  rowstat = 5;

  // title
  move(0, 0);
  attron(A_BOLD);
  int l = snprintf(buf, sizeof(buf), "mtr-%s%s", MTR_VERSION, mtr_args);
  if (iargs) {
    l += snprintf(buf + l, sizeof(buf) - l, " (");

    l += chk_n_print(0, buf + l, sizeof(buf) - l, "UDP-mode");	// udp mode
    l += chk_n_print(1, buf + l, sizeof(buf) - l, "TCP-mode");	// tcp mode
    l += chk_n_print(2, buf + l, sizeof(buf) - l, "MPLS");	// mpls
#ifdef IPINFO
    l += chk_n_print(3, buf + l, sizeof(buf) - l, "ASN-Lookup");	// asn lookup
    l += chk_n_print(4, buf + l, sizeof(buf) - l, "IP-Info");	// ip info
#endif
    l += chk_n_print(5, buf + l, sizeof(buf) - l, "DNS-off");	// dns
    l += chk_n_print(6, buf + l, sizeof(buf) - l, "Jitter");	// jitter
    if ((iargs >> 7) & 3) {	// 7,8 bits: display type
      if (iargs & ((1 << 7) - 1))	// is there smth before?
        l += snprintf(buf + l, sizeof(buf) - l, ", ");
      l += snprintf(buf + l, sizeof(buf) - l, "Display-Type: %d", (iargs >> 7) & 3);
    }

    snprintf(buf + l, sizeof(buf) - l, ")");
  }
  printw("%*s", (getmaxx(stdscr) + strlen(buf)) / 2, buf);
  attroff(A_BOLD);

  mvprintw(1, 0, "%s (%s)", LocalHostname, localaddr);
  time(&t);
  mvprintw(1, maxx-25, ctime(&t));

  printw("Keys:  ");
  attron(A_BOLD); addch('H'); attroff(A_BOLD); printw("elp   ");
  attron(A_BOLD); addch('D'); attroff(A_BOLD); printw("isplay mode   ");
  attron(A_BOLD); addch('R'); attroff(A_BOLD); printw("estart statistics   ");
  attron(A_BOLD); addch('O'); attroff(A_BOLD); printw("rder of fields   ");
  attron(A_BOLD); addch('q'); attroff(A_BOLD); printw("uit\n");

  if (display_mode == 0) {
    startstat = 1;
    hd_len = mtr_curses_data_fields(buf);
    attron(A_BOLD);
#ifdef IPINFO
    if (ii_ready()) {
        char *header = ii_getheader();
        startstat = 4;	// `NN. '
        if (header)
            mvprintw(rowstat - 1, startstat, "%s", header);
        startstat += ii_getwidth(); // `NN. ' + IPINFO
    }
#endif
    mvprintw(rowstat - 1, startstat, "Host");
    mvprintw(rowstat - 1, maxx-hd_len-1, "%s", buf);
    mvprintw(rowstat - 2, maxx-hd_len-1, "   Packets               Pings");
    attroff(A_BOLD);

    move(rowstat, 0);
    mtr_curses_hosts(maxx-hd_len-1);

  } else {
    char msg[80];
    startstat = STARTSTAT;
#ifdef IPINFO
    if (ii_ready())
      startstat += ii_getwidth();
#endif
    int max_cols = (maxx <= (SAVED_PINGS + startstat)) ? (maxx - startstat) : SAVED_PINGS;
    startstat -= 2;

    sprintf(msg, " Last %3d pings", max_cols);
    mvprintw(rowstat - 1, startstat, msg);

    attroff(A_BOLD);
    move(rowstat, 0);

    mtr_gen_scale_gc();
    mtr_curses_graph(startstat, max_cols);

    addch('\n');
    attron(A_BOLD);
    printw("Scale:");
    attroff(A_BOLD);

    if (display_mode == 1) {
      addstr("  ");
      addch(map1[0] | A_BOLD);
      printw(" less than %dms   ", scale2[NUM_FACTORS2 - 2] / 1000);
      addch(map1[1] | (color_mode ? 0 : A_BOLD));
      addstr(" greater than ");
      addch(map1[0] | A_BOLD);
      addstr("   ");
      addch('?' | (color_mode ? (map2[NUM_FACTORS2 - 1] & A_ATTRIBUTES) : A_BOLD));
      addstr(" Unknown");
    } else if (display_mode == 2) {
      if (color_mode) {
        for (i = 0; i < NUM_FACTORS2 - 1; i++) {
          addstr("  ");
          addch(map2[i]);
          printw(":%dms", scale2[i] / 1000);
        }
        addstr("  ");
        addch(map2[NUM_FACTORS2 - 1]);
      } else {
        for (i = 0; i < NUM_FACTORS2 - 1; i++)
          printw("  %c:%dms", map2[i] & A_CHARTEXT, scale2[i] / 1000);
        printw("  %c", map2[NUM_FACTORS2 - 1] & A_CHARTEXT);
      }
    }
#ifdef UNICODE
    else if (display_mode == 3) {
      if (color_mode)
        mtr_print_scale3(NUM_FACTORS3 - 1, 1, 2);
      else
        mtr_print_scale3(NUM_FACTORS3_MONO, 0, 1);
    }
#endif
  }

  refresh();
  move(maxy - 3, 0);
}

void mtr_curses_open(void)
{
  initscr();
  raw();
  noecho();

  if (color_mode)
    if (!has_colors())
      color_mode = 0;

  if (color_mode) {
    start_color();
    int bg_col = 0;
#ifdef HAVE_USE_DEFAULT_COLORS
    use_default_colors();
    if (use_default_colors() == OK)
      bg_col = -1;
#endif
    int i = 1;
    // display_mode 1
    init_pair(i++, COLOR_YELLOW, bg_col);
    // display_mode 2
    dm2_color_base = i;
    init_pair(i++, COLOR_GREEN, bg_col);
    init_pair(i++, COLOR_CYAN, bg_col);
    init_pair(i++, COLOR_BLUE, bg_col);
    init_pair(i++, COLOR_YELLOW, bg_col);
    init_pair(i++, COLOR_MAGENTA, bg_col);
    init_pair(i++, COLOR_RED, bg_col);
#ifdef UNICODE
    // display mode 3
    dm3_color_base = i;
    init_pair(i++, COLOR_GREEN, bg_col);
//    init_pair(i++, COLOR_YELLOW, COLOR_GREEN);
    init_pair(i++, COLOR_YELLOW, bg_col);
//    init_pair(i++, COLOR_RED, COLOR_YELLOW);
    init_pair(i++, COLOR_RED, bg_col);
    init_pair(i++, COLOR_RED, bg_col);
#endif
  }

  mtr_curses_init();
  mtr_curses_redraw();
  curs_set(0);
}


void mtr_curses_close(void)
{
  addch('\n');
  endwin();
}


void mtr_curses_clear(void)
{
  mtr_curses_close();
  mtr_curses_open();
}

#ifdef GRAPHCAIRO

void mtr_curses_scale_desc(char *buf) {
	static char scale_hs[] = "Scale:";
	if (display_mode == 1)
		sprintf(buf, "%s  %c less than %dms   %c greater than %c   %c Unknown", scale_hs,
			(char)(map1[0] & A_CHARTEXT), scale2[NUM_FACTORS2 - 2] / 1000, (char)(map1[1] & A_CHARTEXT),
			(char)(map1[0] & A_CHARTEXT), (char)(map_na2[1] & A_CHARTEXT));
	else if (display_mode == 2) {
		int len = sprintf(buf, "%s", scale_hs);
		int i;
		for (i = 0; i < NUM_FACTORS2 - 1; i++)
			len += sprintf(buf + len, "  %c:%dms", (char)(map2[i] & A_CHARTEXT), scale2[i] / 1000);
		sprintf(buf + len, "  %c", (char)(map2[NUM_FACTORS2 - 1] & A_CHARTEXT));
	}
#ifdef UNICODE
	else if (display_mode == 3) {
		int len = swprintf((wchar_t*)buf, 16, L"%s", scale_hs);
		int i;
		for (i = 0; i < NUM_FACTORS3_MONO; i++)
			len += swprintf(((wchar_t*)buf) + len, 16, L"  %lc:%dms", map3[i].CCHAR_chars[0], scale3[i] / 1000);
		swprintf(((wchar_t*)buf) + len, 4, L"  %c", map3[NUM_FACTORS3_MONO].CCHAR_chars[0]);
	}
#endif
}

#ifdef UNICODE
wchar_t mtr_curses_saved_wch(int saved_int) {
	cchar_t *cc = mtr_saved_cc(saved_int);
	return cc->CCHAR_chars[0];
}
#endif

char mtr_curses_saved_ch(int saved_int) {
	return (char)(mtr_saved_ch(saved_int) & A_CHARTEXT);
}

#endif
