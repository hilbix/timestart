$Header$

Example use:

You have a CMS with a frontpage which shall be updated each 10 minutes.
This process involves pulling some data from other remote systems.
These systems have a very unpredictable answering time, so sometimes a system
needs a second, and a minute later the same request needs 2 hours to complete.

The frontpage pulls 10 of such systems, or even more.

Your current script sequentially queries the remote systems and then compiles
the new frontpage.  If everything works as expected, the script needs a second
to complete, but if things go bad it can take days.  Not nice.

You now can think about some clever logic, splitting everything up in several tasks,
hoping that everything works afterwards and nothing gets lost and no requests pile
up when some of the remote systems decides to stay busy for some weeks.

The alternative is to use timestart and some easy locking/caching technique to
get it done.  This changes to the script are small:

(A) Create a locking for each longer running part and lock this individually.
My tool for this is called "lockrun" and "timeout".

(B) Cache the last successful request for such locked tasks in a cachefile.
Be sure to write the cachefile atomically.  My tool for this is "mvatom".
Usually I do not need to do that, as, for debugging, I keep the last
result around anyway.

(C) Move all the time consuming tasks of (B) to the front of the script.
Use the Caches where previously the time consuming tasks were.
Usually I do not need that, as my scripts already are implemented that way.

(D) Use locking to create all the output at the last step (which usually
is running quickly enough), such that it is race condition free.

(E) Invoke your script using timestart instead of cron

You still keep one linear and therefor easy to debug script instead of a
bunch of more or less parallely running script which may introduce some
synchronization problems.  You nearly change nothing compared to cron,
except that timestart randomnizes the invocation time, which ususally
distributes the load on a machine better than cron.

Timestart will start the script each 10 minutes or so.  If the script hangs in
a locked part after 10 more minutes, timestart will start a 2nd script.
Thanks to the changes the waiting part is skipped and the script hopefully
finishes with the cached information of the previous information of the locked
part.  When the blocked script continues, it will, in the last step,
pull the other changes as well and rebuild the frontpage.

However, perhaps a second part hangs then, too, so 10 minutes later the
3rd script is started and tries a 3rd time skipping those other two hanging
parts.  And so on.

Yes, the update to your frontpage is delayed a bit, but usually this is
not really a big problem!

However if something seriously breaks, "timestart" will at max run M scripts
in parallel.  Perhaps you forgot to spot some thing which may take longer.
For example a simple "df" can become unkillable if some subsystem has died.
Or some SQL table is locked for reorg, whatever.

Debugging this again is easy, just run the exact same script with debugging
turned on.  You will see where it hangs, too, as all other hanging parts
are already locked!  Easy and convenient.

And if it comes to web publishing, timestart allows to spawn a script on
demand, using SIGUSR1, so the editor can quickly "refresh" the frontpage.
If something stays locked, hitting the "refresh" button more often does
the trick.  That was easy.

However this does not protect against somebody impatiently hitting the
reload button 1000 times a second (window.setTimeout() scriblet).
But this is not a flaw of timestart, as for debugging it is more important
to be able to run a script even when the limit on the commandline is
reached.  So put that into the application logic which sends the signal.

# $Log$
# Revision 1.1  2011-09-12 15:14:18  tino
# first version
#
