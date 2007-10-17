#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "error.h"
#include "stralloc.h"
#include "str.h"
#include "env.h"
#include "sig.h"
#include "getconf.h"
#include "open.h"
#include "scan.h"
#include "strerr.h"
#include "byte.h"
#include "getln.h"
#include "case.h"
#include "qmail.h"
#include "sgetopt.h"
#include "subfd.h"
#include "substdio.h"
#include "readwrite.h"
#include "quote.h"
#include "now.h"
#include "uint32.h"
#include "subhash.h"
#include "fmt.h"
#include "msgtxt.h"
#include "die.h"
#include "config.h"
#include "idx.h"
#include "auto_version.h"

const char FATAL[] = "ezmlm-split: fatal: ";
const char INFO[] = "ezmlm-split: info: ";
const char USAGE[] =
"ezmlm-split: usage: ezmlm-split [-dD] dir [splitfile]";

int flagdo = 1;		/* default is manager function */

const char *sender;
const char *split;
stralloc target = {0};
stralloc lctarget = {0};
stralloc line = {0};
stralloc domain = {0};
stralloc name = {0};
stralloc from = {0};
stralloc to = {0};
char strnum[FMT_ULONG];
unsigned long lineno;
int flagfound;

void die_syntax(void)
{
  strnum[fmt_ulong(strnum,lineno)] = '\0';
  strerr_die6x(111,FATAL,split," syntax error line ",strnum,": ",line.s);
}

char spbuf[1024];	/* should normally hold entire file */
substdio sssp;

struct qmail qq;

int findname(void)
/* returns 1 if a matching line was found, 0 otherwise. name will contain */
/* the correct list address in either case */
{
  char *cpat,*cp1,*cp2,*cplast;
  const char *cpname;
  unsigned long u;
  unsigned char hash,hash_hi,hash_lo;
  unsigned int pos,pos_name,pos_hi;
  int fd,match;

  /* make case insensitive hash */
  flagfound = 0;			/* default */
  cpname = "";				/* default */
  if (!stralloc_copy(&lctarget,&target)) die_nomem();
  case_lowerb(lctarget.s,lctarget.len -1);
  hash = subhashs(lctarget.s);

  /* make domain pointer */
  cpat = lctarget.s + str_chr(lctarget.s,'@');
  if (!*cpat)
    strerr_die4x(100,FATAL,MSG("ERR_ADDR_AT"),": ",target.s);
  cplast = cpat + str_len(cpat) - 1;
  if (*cplast == '.') --cplast;		/* annonying special case */
  cp1 = cpat + byte_rchr(cpat,cplast - cpat, '.');
  if (cp1 != cplast) {			/* got one '.' */
    if (!stralloc_copyb(&domain,cp1 + 1, cplast - cp1)) die_nomem();
    cp2 = cpat + byte_rchr(cpat, cp1 - cpat,'.');
    if (cp2 == cp1) cp2 = cpat;
    ++cp2;
    if (!stralloc_append(&domain,".")) die_nomem();
    if (!stralloc_catb(&domain,cp2, cp1 - cp2)) die_nomem();
  } else				/* no '.' */
    if (!stralloc_copyb(&domain,cpat + 1,cplast - cpat)) die_nomem();
  if (!stralloc_0(&domain)) die_nomem();

  if ((fd = open_read(split)) == -1)
    strerr_die4sys(111,FATAL,MSG("ERR_OPEN"),split,": ");
  substdio_fdbuf(&sssp,read,fd,spbuf,(int) sizeof(spbuf));
  lineno = 0;
  for (;;) {	/* dom:hash_lo:hash_hi:listaddress */
    if (getln(&sssp,&line,&match,'\n') == -1)
      strerr_die4sys(111,FATAL,MSG("ERR_READ"),split,": ");
     lineno++;
    if (!match)
      break;
    if (line.s[0] == '#') continue;	/* comment */
    line.s[line.len - 1] = '\0';	/* no need to allow \0 in lines */
    if (!line.s[pos = str_chr(line.s,':')])
      continue;				/* usually blank line */
    line.s[pos] = '\0';
    if (pos == 0 ||			/* no domain */
	  (case_starts(domain.s,line.s))) {	/* or matching domain */
	if (!line.s[++pos]) die_syntax();
	pos_hi = pos + str_chr(line.s + pos,':');
	if (!line.s[pos_hi]) die_syntax();
	pos_hi++;
	(void) scan_ulong(line.s + pos, &u);	/* scan_uint() not in ezmlm */
	hash_lo = (unsigned char) u;
	(void) scan_ulong(line.s + pos_hi, &u);
	hash_hi = (unsigned char) u;
	pos_name = pos_hi + str_chr(line.s + pos_hi,':');
	if (pos_hi == pos_name) hash_hi = 52L;	/* default hi = 52 */
	if (line.s[pos_name]) pos_name++;
	if (hash > hash_hi || hash < hash_lo) continue;	/* not us */
	cpname = line.s + pos_name;
	while (*cpname &&				/* isolate name */
	    (*cpname == ' ' || *cpname == '\t')) cpname++;
	pos = line.len - 2;
	while (pos && (line.s[pos] == '\n' || line.s[pos] == ' ' ||
		line.s[pos] == '\t')) line.s[pos--] = '\0';
	break;
    }
  }
  close(fd);

  if (*cpname) {
    if (!stralloc_copys(&name,cpname)) die_nomem();
    if (byte_chr(name.s,name.len,'@') == name.len) {	/* local sublist */
      if (!stralloc_append(&name,"@")) die_nomem();
      if (!stralloc_cat(&name,&outhost)) die_nomem();
    }
    if (!stralloc_0(&name)) die_nomem();
    return 1;
  } else {			/* match without name or no match =>this list */
    if (!stralloc_copy(&name,&outlocal)) die_nomem();
    if (!stralloc_append(&name,"@")) die_nomem();
    if (!stralloc_cat(&name,&outhost)) die_nomem();
    if (!stralloc_0(&name)) die_nomem();
    return 0;
  }
}

void main(int argc,char **argv)
{
  char *dir;
  char *action;
  char *dtline;
  char *nhost;
  const char *err;
  unsigned int i;
  int match;
  int opt;

  sig_pipeignore();

  while ((opt = getopt(argc,argv,"dDvV")) != opteof) {
    switch (opt) {
    case 'd': flagdo = 1; break;
    case 'D': flagdo = 0; break;
    case 'v':
    case 'V':
      strerr_die2x(0, "ezmlm-split version: ",auto_version);
    default:
      die_usage();
    }
  }

  startup(dir = argv[optind++]);
  if (!(split = argv[optind]))
    split = "split";

  if (flagdo) {
    sender = env_get("SENDER");
    if (!sender) strerr_die2x(100,FATAL,MSG("ERR_NOSENDER"));
    if (!*sender)
      strerr_die2x(100,FATAL,MSG("ERR_BOUNCE"));
    if (!sender[str_chr(sender,'@')])
      strerr_die2x(100,FATAL,MSG("ERR_ANONYMOUS"));
    if (str_equal(sender,"#@[]"))
      strerr_die2x(100,FATAL,MSG("ERR_BOUNCE"));

    action = env_get("DEFAULT");
    if (!action) strerr_die2x(100,FATAL,MSG("ERR_NODEFAULT"));
    if (!stralloc_copys(&target,sender)) die_nomem();
    if (action[0]) {
      i = str_chr(action,'-');
      if (action[i]) {
        action[i] = '\0';
        if (!stralloc_copys(&target,action + i + 1)) die_nomem();
        i = byte_rchr(target.s,target.len,'=');
        if (i < target.len)
	  target.s[i] = '@';
      }
    }
    if (!stralloc_0(&target)) die_nomem();

    if (case_diffs(action,ACTION_SUBSCRIBE) &&
      case_diffs(action,ALT_SUBSCRIBE) &&
      case_diffs(action,ACTION_UNSUBSCRIBE) &&
      case_diffs(action,ALT_UNSUBSCRIBE))
    _exit(0);			/* not for us */

    if (findname()) {
				/* new sender */
      if (!stralloc_copy(&from,&outlocal)) die_nomem();
      if (!stralloc_cats(&from,"-return-@")) die_nomem();
      if (!stralloc_cat(&from,&outhost)) die_nomem();
      if (!stralloc_0(&from)) die_nomem();
      nhost = name.s + str_rchr(name.s,'@');		/* name must have '@'*/
      *(nhost++) = '\0';
      if (!stralloc_copys(&to,name.s)) die_nomem();	/* local */
      if (!stralloc_append(&to,"-")) die_nomem();	/* - */
      if (!stralloc_cats(&to,action)) die_nomem();	/* subscribe */
      if (!stralloc_append(&to,"-")) die_nomem();	/* - */
      if (target.s[i = str_rchr(target.s,'@')])
	target.s[i] = '=';
      if (!stralloc_cats(&to,target.s)) die_nomem();	/* target */
      if (!stralloc_append(&to,"@")) die_nomem();	/* - */
      if (!stralloc_cats(&to,nhost)) die_nomem();	/* host */
      if (!stralloc_0(&to)) die_nomem();
      dtline = env_get("DTLINE");
      if (!dtline) strerr_die2x(100,FATAL,MSG("ERR_NODTLINE"));

      if (qmail_open(&qq,(stralloc *) 0) == -1)
        strerr_die2sys(111,FATAL,MSG("ERR_QMAIL_QUEUE"));
      qmail_puts(&qq,dtline);				/* delivered-to */
      if (qmail_copy(&qq,subfdin,0) != 0)
        strerr_die2sys(111,FATAL,MSG("ERR_READ_INPUT"));
      qmail_from(&qq,from.s);
      qmail_to(&qq,to.s);

      if (*(err = qmail_close(&qq)) != '\0')
        strerr_die3x(111,FATAL,MSG("ERR_TMP_QMAIL_QUEUE"),err + 1);

      strnum[fmt_ulong(strnum,qmail_qp(&qq))] = 0;
      strerr_die3x(99,INFO,"qp ",strnum);
    }
    _exit(0);
  } else {

    for (;;) {
      if (getln(subfdin,&line,&match,'\n') == -1)
	  strerr_die2sys(111,FATAL,MSG("ERR_READ_INPUT"));
      if (!match) break;
      if (line.len == 1) continue;	/* ignore blank lines */
      if (line.s[0] == '#') continue;	/* ignore comments */
      if (!stralloc_copy(&target,&line)) die_nomem();
      target.s[target.len - 1] = '\0';
      (void) findname();
      if (!stralloc_cats(&name,": ")) die_nomem();
      if (!stralloc_cats(&name,target.s)) die_nomem();
      if (!stralloc_append(&name,"\n")) die_nomem();
      if (substdio_put(subfdout,name.s,name.len) == -1)
	strerr_die2sys(111,MSG("ERR_WRITE"),"output: ");
    }
    if (substdio_flush(subfdout) == -1)
      strerr_die2sys(111,MSG("ERR_FLUSH"),"output: ");
    _exit(0);
  }
  (void)argc;
}
