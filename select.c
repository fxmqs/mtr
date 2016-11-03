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

#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "mtr.h"
#include "dns.h"
#include "net.h"
#ifdef IPINFO
#include "asn.h"
#endif
#include "display.h"

extern int Interactive;
extern int DisplayMode;
extern int MaxPing;
extern int ForceMaxPing;
extern float WaitTime;
double dnsinterval;
extern int mtrtype;
#if defined(CURSES) || defined(GRAPHCAIRO)
extern int display_mode;
extern int display_mode_max;
#endif

static struct timeval intervaltime;
int display_offset = 0;

fd_set tcp_fds;
int maxfd;

#define GRACETIME (5 * 1000*1000)

void select_loop(void) {
  fd_set readfd;
  fd_set writefd, *p_writefd = NULL;
  int anyset = 0;
  int dnsfd, netfd;
#ifdef ENABLE_IPV6
  int dnsfd6;
#endif
#ifdef IPINFO
  int iifd;
#endif
  int NumPing = 0;
  int paused = 0;
  struct timeval lasttime, thistime, selecttime;
  struct timeval startgrace;
  int dt;
  int rv; 
  int graceperiod = 0;

  memset(&startgrace, 0, sizeof(startgrace));
  FD_ZERO(&tcp_fds);
  gettimeofday(&lasttime, NULL);

  if (mtrtype == IPPROTO_TCP)
    p_writefd = &writefd;

  while(1) {
    dt = calc_deltatime (WaitTime);
    intervaltime.tv_sec  = dt / 1000000;
    intervaltime.tv_usec = dt % 1000000;

    FD_ZERO(&readfd);

    if(Interactive) {
      FD_SET(0, &readfd);
      if (maxfd == 0)
        maxfd++;
    }

#ifdef ENABLE_IPV6
    if (dns) {
      dnsfd6 = dns_waitfd6();
      if (dnsfd6 >= 0) {
        FD_SET(dnsfd6, &readfd);
        if(dnsfd6 >= maxfd) maxfd = dnsfd6 + 1;
      } else {
        dnsfd6 = 0;
      }
    } else
      dnsfd6 = 0;
#endif
    if (dns) {
      dnsfd = dns_waitfd();
      FD_SET(dnsfd, &readfd);
      if(dnsfd >= maxfd) maxfd = dnsfd + 1;
    } else
      dnsfd = 0;

#ifdef IPINFO
    if (enable_ipinfo) {
      iifd = ii_waitfd();
      FD_SET(iifd, &readfd);
      if (iifd >= maxfd)
        maxfd = iifd + 1;
    } else
      iifd = 0;
#endif

    netfd = net_waitfd();
    FD_SET(netfd, &readfd);
    if(netfd >= maxfd) maxfd = netfd + 1;

    do {
      if(anyset || paused) {
	/* Set timeout to 0.1s.
	 * While this is almost instantaneous for human operators,
	 * it's slow enough for computers to go do something else;
	 * this prevents mtr from hogging 100% CPU time on one core.
	 */
	selecttime.tv_sec = 0;
	selecttime.tv_usec = paused?100000:0;

	if (mtrtype == IPPROTO_TCP)
	  writefd = tcp_fds;
	rv = select(maxfd, &readfd, p_writefd, NULL, &selecttime);

      } else {
	if(Interactive) display_redraw();
#ifdef IPINFO
	if (enable_ipinfo)
	  if (DisplayMode == DisplayReport)
            query_ipinfo();
#endif

	gettimeofday(&thistime, NULL);

	if(thistime.tv_sec > lasttime.tv_sec + intervaltime.tv_sec ||
	   (thistime.tv_sec == lasttime.tv_sec + intervaltime.tv_sec &&
	    thistime.tv_usec >= lasttime.tv_usec + intervaltime.tv_usec)) {
	  lasttime = thistime;

	  if (!graceperiod) {
	    if (NumPing >= MaxPing && (!Interactive || ForceMaxPing)) {
	      graceperiod = 1;
	      startgrace = thistime;
	    }

	    /* do not send out batch when we've already initiated grace period */
	    if (!graceperiod && net_send_batch())
	      NumPing++;
	  }
	}

	if (graceperiod) {
	  dt = (thistime.tv_usec - startgrace.tv_usec) +
		    1000000 * (thistime.tv_sec - startgrace.tv_sec);
	  if (dt > GRACETIME)
	    return;
	}

	selecttime.tv_usec = (thistime.tv_usec - lasttime.tv_usec);
	selecttime.tv_sec = (thistime.tv_sec - lasttime.tv_sec);
	if (selecttime.tv_usec < 0) {
	  --selecttime.tv_sec;
	  selecttime.tv_usec += 1000000;
	}
	selecttime.tv_usec = intervaltime.tv_usec - selecttime.tv_usec;
	selecttime.tv_sec = intervaltime.tv_sec - selecttime.tv_sec;
	if (selecttime.tv_usec < 0) {
	  --selecttime.tv_sec;
	  selecttime.tv_usec += 1000000;
	}

	if (dns) {
	  if ((selecttime.tv_sec > (time_t)dnsinterval) ||
	      ((selecttime.tv_sec == (time_t)dnsinterval) &&
	       (selecttime.tv_usec > ((time_t)(dnsinterval * 1000000) % 1000000)))) {
	    selecttime.tv_sec = (time_t)dnsinterval;
	    selecttime.tv_usec = (time_t)(dnsinterval * 1000000) % 1000000;
	  }
	}

	if (mtrtype == IPPROTO_TCP)
	  writefd = tcp_fds;
	rv = select(maxfd, &readfd, p_writefd, NULL, &selecttime);
      }
    } while ((rv < 0) && (errno == EINTR));

    if (rv < 0) {
      perror ("Select failed");
      exit (1);
    }
    anyset = 0;

    /*  Have we got new packets back?  */
    if(FD_ISSET(netfd, &readfd)) {
      net_process_return();
      anyset = 1;
    }

    if (dns) {
      /* Handle any pending resolver events */
      dnsinterval = WaitTime;
      dns_events(&dnsinterval);
    }

    /*  Have we finished a nameservice lookup?  */
#ifdef ENABLE_IPV6
    if(dns && dnsfd6 && FD_ISSET(dnsfd6, &readfd)) {
      dns_ack6();
      anyset = 1;
    }
#endif
    if(dns && dnsfd && FD_ISSET(dnsfd, &readfd)) {
      dns_ack();
      anyset = 1;
    }

#ifdef IPINFO
    if (enable_ipinfo && iifd && FD_ISSET(iifd, &readfd)) {
      ii_ack();
      anyset = 1;
    }
#endif

    /*  Has a key been pressed?  */
    if(FD_ISSET(0, &readfd)) {
      switch (display_keyaction()) {
      case ActionQuit: 
	return;
	break;
      case ActionReset:
	net_reset();
	break;
#if defined(CURSES) || defined(GRAPHCAIRO)
      case ActionDisplay:
        display_mode = (display_mode + 1) % display_mode_max;
	break;
#endif
      case ActionClear:
	display_clear();
	break;
      case ActionPause:
	paused=1;
	break;
      case  ActionResume:
	paused=0;
	break;
      case ActionMPLS:
	   enablempls = !enablempls;
	   display_clear();
	break;
      case ActionDNS:
	if (dns) {
	  use_dns = !use_dns;
	  display_clear();
	}
	break;
#ifdef IPINFO
	case ActionII:
		ii_action(0);
		break;
	case ActionAS:
		ii_action(1);
		break;
#endif
      case ActionScrollDown:
        display_offset += 5;
	break;
      case ActionScrollUp:
        display_offset -= 5;
	if (display_offset < 0) {
	  display_offset = 0;
	}
	break;
      }
      anyset = 1;
    }

    /* Check for activity on open sockets */
    if (mtrtype == IPPROTO_TCP)
      anyset = net_process_tcp_fds();
  }
  return;
}

