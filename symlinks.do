for f in ezmlm-mktab checktag.c issub.c logmsg.c opensql.c putsubs.c searchlog.c subscribe.c tagmsg.c
do
  dependon $f
  ln sub_std/$f $f
done