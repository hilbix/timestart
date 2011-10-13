/* $Header$
 *
 * This Works is placed under the terms of the Copyright Less License,
 * see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.
 *
 * This source is optimized for diet.
 *
 * $Log$
 * Revision 1.2  2011-10-13 00:14:16  tino
 * Bugfix and minor improvements.  Statistics with SIGWINCH added
 *
 */

#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "timestart_version.h"

#if 0
#define	D(X)
#endif

#ifndef D
#define	D(X) debug(X)
#endif

static void
debug(const char *s)
{
  write(1, s, strlen(s));
  write(1, "\n\r", 2);
}

#define	MAXPIDS	1024

struct childs
  {
    struct childs	*next;
    time_t		start;
    pid_t		pid;
  } *childs = 0, **lastchl= &childs;
static unsigned long nchilds;

static char	iobuf[1024];
static int	iofill;
static int	iofd;

static int	stat_forks, stat_success, stat_kill, stat_oops, stat_timeouts, stat_waits, stat_no_childs, stat_full_childs, stat_max_childs;
static time_t	stat_start;

struct stat_avg
  {
    unsigned long	accu;
    int			cnt;
  };

static struct stat_avg stat_avg_wait, stat_avg_childs, stat_avg_sleep, stat_avg_run, stat_avg_step;

#define	USAGE	"Usage: timestart [options] min max fromsec tosec program [args..] 3>log\r\n" \
  "		Version " TIMESTART_VERSION " compiled " __DATE__ "\r\n" \
  "	Start min to max programs in parallel, delay from-to seconds between calls.\r\n" \
  "	If program returns FALSE then wait the maximum delay until spawning the next one,\r\n" \
  "	else try to adjust the the spawn rate such that min programs run in parallel.\r\n" \
  "	This program currently has no options.\r\n"			\
  "	Signals:\r\n"							\
  "		SIGHUP	Wait for running childs, then terminate\r\n"	\
  "		SIGUSR1	Suspend forking childs\r\n"			\
  "		SIGUSR2	Continue forking / fork a child now\r\n"	\
  "		SIGWINCH Print statistics to stderr\r\n"		\


static void
avg(struct stat_avg *avg, int delta)
{
  if (delta<0)
    return;

  if (avg->cnt<100)
    avg->cnt++;
  else
    avg->accu = avg->accu * (avg->cnt-1) / avg->cnt;

  avg->accu += delta;
}

static void
write_all(int fd, const void *_s, size_t len)
{ 
  const unsigned char *s=_s;
  int got;

  while (len && (got=write(fd, s, len))!=0)
    if (got>0)
      s+=got, len-=got;
    else if (errno!=EINTR)
      break;
}

static void
ioflush(void)
{
  write_all(iofd, iobuf, iofill);	/* ignore error	*/
  iofill	= 0;
}

static void
ioset(int fd)
{
  if (iofd!=fd)
    ioflush();
  iofd	= fd;
}

static void
ioc(char c)
{
  if (iofill>=sizeof iobuf)
    ioflush();
  iobuf[iofill++]	= c;
}

static void
ios(const char *s)
{
  while (*s)
    ioc(*s++);
}

static void
ionl(void)
{
  ioc('\r');
  ioc('\n');
  ioflush();
}

static void
iox(int x)
{
  ioc("0123456789abcdef"[x]);
}

static void
ioul(unsigned long u)
{
  if (u>9ull)
    ioul(u/10);
  iox(u%10);
}

static void
iouln(unsigned long u, int n)
{
  if (n>1)
    {
      iouln(u/10, n-1);
      u	%= 10;
    }
  ioul(u);
}

static void
ioul2(unsigned long u)
{
  iouln(u, 2);
}

static void
ioul4(unsigned long u)
{
  iouln(u, 4);
}

static void
ioxl(unsigned long u)
{
  if (u>15ull)
    ioul(u>>4);
  iox(u&15);
}

#define IOUL(x) ioul((unsigned long)(x))

static void
iorate(unsigned long val, int div)
{
  if (div<=0)
    {
      ios("---");
      return;
    }
  ioul(val / div);
  ioc('.');
  ioul2( (val % div ) * 100 / div );
}

static void
ioavg(struct stat_avg *st)
{
  iorate(st->accu, st->cnt);
}


/****************************************************************/

static void
err(const char *s)
{
  ioset(2);
  ios(s);
}

static void
ex(const char *s)
{
  ioset(2);
  ios(s);
  ionl();
  exit(1);
}

static void
oops(const char *s)
{
  int		e=errno;

  stat_oops++;
  ioset(2);
  ios(s);
  ios(": ");
  ios(strerror(e));
  ionl();
}

static void
blog(void)
{
  time_t	tim;
  struct tm	*tm;

  ioset(3);
  time(&tim);
  tm	= localtime(&tim);
  ioul4(tm->tm_year+1900);
  ioc('-');
  ioul2(tm->tm_mon+1);
  ioc('-');
  ioul2(tm->tm_mday);
  ioc(' ');
  ioul2(tm->tm_hour);
  ioc(':');
  ioul2(tm->tm_min);
  ioc(':');
  ioul2(tm->tm_sec);
  ioc(' ');
}

static void
slog(void)
{
  ioset(2);
}

/****************************************************************/

static unsigned long
getul(const char *s)
{
  char *end;
  unsigned long	ul;

  ul	= strtoul(s, &end, 0);
  if (!end || *end)
    ex("wrong number");
  return ul;
}


/****************************************************************/

static int hup_received = 0;
static int winch_received = 0;

static void
winch_handler(int ign)
{
  winch_received	= 1;
}

static void
hup_handler(int ign)
{
  hup_received	= 1;
}

static int suspend_received = 0, immediate_received = 0;

static void
usr1_handler(int ign)
{
  immediate_received = 0;
  suspend_received = 1;
}

static void
usr2_handler(int ign)
{
  suspend_received = 0;
  immediate_received++;
}

static void
sa(int sig, void (*handler)(int))
{
  struct sigaction sa;

  memset(&sa, 0, sizeof sa);
  sa.sa_handler	= handler;
  if (sigaction(sig, &sa, NULL))
    {
      oops("sigaction");
      exit(1);
    }
}

/****************************************************************/

static void
forker(char **args)
{
  pid_t		pid;
  struct childs	*chl;

  if ((pid=fork())==0)
    {
      execvp(args[0], args);
      oops("exec");
      exit(1);
    }
  if (pid==(pid_t)-1)
    {
      oops("fork");
      return;
    }

  stat_forks++;
  nchilds++;
  if (nchilds>stat_max_childs)
    stat_max_childs = nchilds;
  chl		= malloc(sizeof *chl);
  if (!chl)
    {
      oops("malloc");
      return;
    }
  chl->pid	= pid;
  time(&chl->start);
  chl->next	= 0;
  *lastchl	= chl;
  lastchl	= &chl->next;

  blog();
  ios("forked ");
  IOUL(pid);
  ionl();
}

static void
sleeper(unsigned long delay)
{
  if (hup_received)
    return;

  blog();
  ios("sleep ");
  ioul(delay);
  ionl();

  sleep(delay);
}

static int timeout;

static void
alrm_handler(int ign)
{
  timeout = 1;
}

/* Wait a maximum time for child termination.
 * If there are no childs to wait for, this sleeps the given time.
 *
 * This returns:
 * -1	on timeout
 * 0	if a child came back without error (returns true)
 * 1	if a child came back with error (returs false or other error)
 */
static int
waiter(int seconds, int hang)
{
  int	flag, bugs;

  flag	= WNOHANG;
  bugs	= 0;
  for (;;)
    {
      struct childs	**last, *chl;
      int		st;
      pid_t		got;

      if (bugs>1000)
	ex("bug");

      if (!flag && !hang)
	{
	  static struct sigaction sa = { .sa_handler=alrm_handler, .sa_flags=SA_RESETHAND };

	  if (sigaction(SIGALRM, &sa, NULL))
	    {
	      oops("sigaction");
	      bugs++;
	      continue;
	    }
	  timeout = 0;
	  if (seconds<=0)
	    return -1;		/* artificial timeout	*/
	  alarm(seconds);
	  avg(&stat_avg_wait, seconds);
	}
      avg(&stat_avg_childs, nchilds);
      stat_waits++;
      got	= waitpid((pid_t)-1, &st, flag);
      if (!flag && !hang)
	alarm(0);

      if (got==(pid_t)0 || (got==(pid_t)-1 && errno==ECHILD))
	{
	  if (!flag)
	    {
	      if (nchilds)
		ex("internal error, lost a child");

	      stat_no_childs++;
	      /* In case of no childs, simulate a timeout
	       */
	      sleeper(seconds);
	      return -1;
	    }
	  flag	= 0;	/* now wait	*/
	  continue;
	}
      if (got==(pid_t)-1)
	{
	  if (errno!=EINTR)
	    oops("waitpid");
	  else
	    return -1;	/* timeout	*/
	  bugs++;
	  continue;
	}

      bugs = 0;

      blog();
      ios("child ");
      IOUL(got);
      ios(" ret ");
      ioxl((unsigned long)st);

      if (WIFSTOPPED(st)
#ifdef WIFCONTINUED
	  || WIFCONTINUED(st)
#endif
	  )
	{
	  ionl();
	  continue;
	}
      for (last= &childs; (chl= *last)!=0; last= &chl->next)
	if (chl->pid==got)
	  {
	    time_t now;

	    if ((*last=chl->next)==0)
	      lastchl	= last;
	    time(&now);
	    avg(&stat_avg_run, now-chl->start);
	    free(chl);
	    nchilds--;
	    ionl();
	    if (!WIFEXITED(st))
	      {
		stat_kill++;
		return 1;
	      }
	    if (WEXITSTATUS(st))
	      return 1;
	    stat_success++;
	    return 0;
	  }
      ios(" (not found)");
      ionl();
    }
}

/****************************************************************/

static void
dump_childs(time_t now)
{
  struct childs *chl;

  for (chl=childs; chl; chl=chl->next)
    {
      ioc(' ');
      IOUL(now-chl->start);
      ioc('/');
      IOUL(chl->pid);
    }
  ionl();
}

int
main(int argc, char **argv)
{
  unsigned long	min, max, from, to, step, hang;
  time_t	last, was;
  int		delta;

  if (argc<5)
    ex(USAGE);

  min	= getul(argv[1]);
  max	= getul(argv[2]);
  from	= getul(argv[3]);
  to	= getul(argv[4]);

#define	exif(X)	if (X) ex(#X)

  exif(max<min);
  exif(max<1);
  exif(to<from);
  exif(to<1);

  sa(SIGHUP, hup_handler);
  sa(SIGUSR1, usr1_handler);
  sa(SIGUSR2, usr2_handler);
  sa(SIGWINCH, winch_handler);

  was	= time(&stat_start);

  last	= 0;
  step	= 0;
  delta	= 0;
  hang	= 0;
  while (!hup_received || nchilds)
    {
      int	ret;
      time_t	now;

      /* keep step in bounds	*/
      if (step<from)
	step	= from;
      if (step>to)
	step	= to;

      time(&now);
      avg(&stat_avg_sleep, now-was);
      if (now!=was)
	{
	  avg(&stat_avg_step, step);
	}
      was = now;
      if (delta<0)
	{
	  time_t	tmp;

	  /* time_t could be an unsigned type which does not fit into delta,
	   * so we must use compare to detect the negative case.
	   * Also assume that only + is a safe operation against time_t.
	   */
	  tmp	= last+step;
	  delta	= tmp<=now ? 0 : tmp-now;
	}

      if (winch_received)
        {
	  winch_received = 0;
	  slog();
	  ionl();
	  ios("STAT");
          ios(" running: ");		IOUL(now-stat_start);
	  ios("s loops ");		IOUL(stat_waits);
	  ios(" excess ");		IOUL(stat_timeouts);
	  ios(" oops ");		IOUL(stat_oops);
	  ionl();
	  ios("STAT");
	  ios(" childs:");
	  ios(" forked ");		IOUL(stat_forks);
	  ios(" ok ");			IOUL(stat_success);
	  ios(" sig ");			IOUL(stat_kill);
	  ios(" / none ");		IOUL(stat_no_childs);
	  ios(" max ");			IOUL(stat_max_childs);
	  ios(" full ");		IOUL(stat_full_childs);
	  ionl();
	  ios("STAT");
	  ios(" avg:");
	  ios(" childs ");		ioavg(&stat_avg_childs);
	  ios(" wait ");		ioavg(&stat_avg_wait);
	  ios(" sleep ");		ioavg(&stat_avg_sleep);
	  ios(" step ");		ioavg(&stat_avg_step);
	  ios(" run ");			ioavg(&stat_avg_run);
	  ionl();
	  ios("STAT");
	  ios(" s/PID:");
	  dump_childs(now);
        }

      /* Note that for immediate_received there may be a race here.
       *
       * If SIGUSR1 is received until the wait() call, it is ignored.
       * In that case it must be send a second time - then both fire.
       */
      blog();
      ios("childs ");
      ioul(nchilds);
      ios(" step ");
      IOUL(step);
      ios(" sleep ");
      IOUL(delta);
      ios(" block ");
      IOUL(hang);
      ios(" hup/usr1/2 ");
      IOUL(hup_received);
      ioc('/');
      IOUL(suspend_received);
      ioc('/');
      IOUL(immediate_received);
      ionl();

      ret	= waiter(immediate_received && !hup_received ? 0 : delta>to ? to : delta, hang);
      hang	= 0;

      if (ret== -1)
	{
	  stat_timeouts++;

	  /* On timeout fork off a new child
	   */
	  if ((!immediate_received && nchilds>=max && ++stat_full_childs) || hup_received || suspend_received)
	    {
	      /* If max childs are reached then wait until some child comes back.
	       */
	      hang	= 1;
	      delta	= to;
	      continue;
	    }

	  /* now,last roles are reversed for a few lines to spare one assign	*/
	  now = last;
	  time(&last);

	  if (immediate_received)
	    immediate_received--;
          else if (now+delta>last)
	    {
	      /* If childs are failing delta can be higher than step
	       * so do not set it to -1, as setting it down is wrong.
	       */
	      delta -= last-now;
	      continue;
	    }
	  forker(argv+5);

	  /* Decrease the pace after forks.
	   */
	  step++;

	  /* ramp up the time to fork the next child
	   */
	  if (delta<step)
	    delta	= step;

	  continue;
	}

      if (ret)
	{
	  /* No child success
	   *
	   * Prevent ramp up on next fork.
	   */
	  step--;

	  /* wait the maximum time to fork next child
	   */
	  delta	= to;
	  continue;
	}

      /* Child with success
       *
       * If we are executing min childs keep the rate.
       * If we are executing too few childs, 
       */

      if (nchilds<=min)
	step--;		/* keep pace when running min childs	*/
      if (nchilds<min)
	{
	  step--;	/* increase pace when running too few	*/
	  delta	= 0;
	  continue;
	}

      /* Set the next waiting time according to the child status.
       *
       * On success calculate the next child on time, on error delay max.
       */
      delta	= ret ? max : -1;
    }
  blog();
  ios("terminating");
  ionl();
  return 0;
}
