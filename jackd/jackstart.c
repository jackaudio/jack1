/*  Copyright (C) 2002 Fernando Lopez-Lezcano

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Jackstart is based on code and concepts found in sucap.c, written by
    Finn Arne Gangstad <finnag@guardian.no> and givertcap.c, written by
    Tommi Ilmonen, Tommi.Ilmonen@hut.fi

*/

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#include <config.h>

#undef _POSIX_SOURCE
#include <sys/capability.h>

#include "jack/start.h"
#include "md5.h"
#include "jack_md5.h"

#define READ_BLOCKSIZE 4096

/* JACK_LOCATION must be passed on the gcc command line */
static char *jackd_bin_path = JACK_LOCATION "/jackd";

static char *jackd_md5_sum = JACKD_MD5_SUM;


static int check_capabilities (void)
{
	cap_t caps = cap_init();
	cap_flag_value_t cap;
	pid_t pid;
	int have_all_caps = 1;

	if (caps == NULL) {
		fprintf (stderr, "jackstart: could not allocate capability working storage\n");
		return 0;
	}
	pid = getpid ();
	cap_clear (caps);
	if (capgetp (pid, caps)) {
		fprintf (stderr, "jackstart: could not get capabilities for process %d\n", pid);
		return 0;
	}
	/* check that we are able to give capabilites to other processes */
	cap_get_flag(caps, CAP_SETPCAP, CAP_EFFECTIVE, &cap);
	if (cap == CAP_CLEAR) {
		have_all_caps = 0;
		goto done;
	}
	/* check that we have the capabilities we want to transfer */
	cap_get_flag(caps, CAP_SYS_NICE, CAP_EFFECTIVE, &cap);
	if (cap == CAP_CLEAR) {
		have_all_caps = 0;
		goto done;
	}
	cap_get_flag(caps, CAP_SYS_RESOURCE, CAP_EFFECTIVE, &cap);
	if (cap == CAP_CLEAR) {
		have_all_caps = 0;
		goto done;
	}
	cap_get_flag(caps, CAP_IPC_LOCK, CAP_EFFECTIVE, &cap);
	if (cap == CAP_CLEAR) {
		have_all_caps = 0;
		goto done;
	}
  done:
	cap_free (caps);
	return have_all_caps;
}


static int give_capabilities (pid_t pid)
{
	cap_t caps = cap_init();
	const unsigned caps_size = 4;
	cap_value_t cap_list[] =
		{ CAP_SETPCAP, CAP_SYS_NICE, CAP_SYS_RESOURCE, CAP_IPC_LOCK} ;

	if (caps == NULL) {
		fprintf (stderr, "jackstart: could not allocate capability working storage\n");
		return -1;
	}
	cap_clear(caps);
	if (capgetp (pid, caps)) {
		fprintf (stderr, "jackstart: could not get capabilities for process %d\n", pid);
		cap_clear(caps);
	}
	cap_set_flag(caps, CAP_EFFECTIVE, caps_size, cap_list , CAP_SET);
	cap_set_flag(caps, CAP_INHERITABLE, caps_size, cap_list , CAP_SET);
	cap_set_flag(caps, CAP_PERMITTED, caps_size, cap_list , CAP_SET);
	if (capsetp (pid, caps)) {
		fprintf (stderr, "jackstart: could not give capabilities: %s\n", strerror (errno));
		cap_free (caps);
		return -1;
	}
	cap_free (caps);
	return 0;
}

static int check_binary (const char *binpath)
{
	struct stat status;
	FILE *binstream;

	if (lstat(jackd_bin_path, &status)) {
		fprintf (stderr, "jackstart: could not stat %s: %s\n",
			 binpath, strerror(errno));
		return -1;
	}
	if (!(S_ISREG(status.st_mode))) {
		fprintf (stderr, "jackstart: %s is not a regular file\n",
			 binpath);
		return -1;
	}
	if (status.st_uid != 0) {
		fprintf (stderr, "jackstart: %s is not owned by root\n",
			 binpath);
		return -1;
	}
	if ((status.st_mode & 022) != 0) {
		fprintf (stderr,
			 "jackstart: %s mode %o writeable by non-root users\n",
			 binpath, status.st_mode & 07777);
		return -1;
	}
	if ((binstream = fopen (binpath, "r")) == NULL) {
		fprintf (stderr, "jackstart: can't open %s for reading: %s\n", 
			 binpath, strerror(errno));
		return -1;
	} else {
		/* md5sum the executable file, check man evp for more details */
		size_t sum;
		md5_t ctx;
		char buffer[READ_BLOCKSIZE + 72];
		unsigned char md_value[MD5_SIZE];
		char md_string[3];
		int i, j;

		md5_init(&ctx);
		while (1) {
			size_t n;
			sum = 0;
			do {
				n = fread (buffer + sum, 1, READ_BLOCKSIZE - sum, binstream);
				sum += n;
			} while (sum < READ_BLOCKSIZE && n != 0);
			if (n == 0 && ferror (binstream)) {
				fprintf (stderr, "jackstart: error while reading %s: %s\n", binpath, strerror(errno));
				return -1;
			}
			if (n == 0) {
				break;
			}
			md5_process(&ctx, buffer, READ_BLOCKSIZE);
		}
		if (sum > 0)
			md5_process(&ctx, buffer, sum);
		if (fclose (binstream)) {
			fprintf (stderr, "jackstart: could not close %s after reading: %s\n", binpath, strerror(errno));
		}
		md5_finish(&ctx, md_value);
		for(i = 0, j = 0; i < sizeof(md_value); i++, j+=2) {
			sprintf(md_string, "%02x", md_value[i]);
			if (md_string[0] != jackd_md5_sum[j] ||
			    md_string[1] != jackd_md5_sum[j+1]) {
				fprintf (stderr, "jackstart: md5 checksum for %s does not match\n", binpath);
				return -1;
			}
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	uid_t uid, euid;
	pid_t pid, parent_pid;
	gid_t gid;
	int pipe_fds[2];
	int err;

	parent_pid = getpid ();

	/* get real user and group ids, effective user id */
	uid = getuid ();
	gid = getgid ();
	euid = geteuid ();

	/* are we running suid root? */
	if (uid != 0) {
		if (euid != 0) {
			fprintf (stderr, "jackstart: not running suid root, can't use capabilities\n");
			fprintf (stderr, "    (currently running with uid=%d and euid=%d),\n", uid, euid);
			fprintf (stderr, "    make jackstart suid root or start jackd directly\n\n");
		}
	}
	/* see if we can get the required capabilities */
	if (check_capabilities () == 0) {
		size_t size;
		cap_t cap = cap_init();
		capgetp(0, cap);
		fprintf (stderr, "jackstart: cannot get realtime capabilities, current capabilities are:\n");
		fprintf (stderr, "           %s\n", cap_to_text(cap, &size));
		fprintf (stderr, "    probably running under a kernel with capabilities disabled,\n");
		fprintf (stderr, "    a suitable kernel would have printed something like \"=eip\"\n\n");
	}

	/* check the executable, owner, permissions, md5 checksum */
	if (check_binary(jackd_bin_path)) {
		exit(1);
	}

	/* set process group to current pid */
	if (setpgid (0, getpid())) {
		fprintf (stderr, "jackstart: failed to set process group: %s\n", 
			 strerror(errno));
		exit (1);
	}

	/* create pipe to synchronize with jackd */
	if (pipe (pipe_fds)) {
		fprintf (stderr, "jackstart: could not create pipe: %s\n",
			 strerror(errno));
		exit (1);
	}

	/* make sure the file descriptors are the right ones,
	   otherwise dup them, this is to make sure that both
	   jackstart and jackd use the same fds
	*/
	if (pipe_fds[0] != PIPE_READ_FD) {
		if (dup2 (pipe_fds[0], PIPE_READ_FD) != PIPE_READ_FD) {
			fprintf (stderr, "jackstart: could not dup pipe read file descriptor: %s\n",
				 strerror(errno));
			exit (1);
		}
	}
	if (pipe_fds[1] != PIPE_WRITE_FD) {
		if (dup2(pipe_fds[1], PIPE_WRITE_FD)!=PIPE_WRITE_FD) {
			fprintf (stderr, "jackstart: could not dup pipe write file descriptor: %s\n",
				 strerror(errno));
			exit (1);
		}
	}
	/* fork off a child to wait for jackd to start */
	fflush(NULL);
	pid = fork();
	if (pid == -1) {
		fprintf (stderr, "jackstart: fork failed\n");
		exit (1);
	}
	if (pid) {
		/* mother process: drops privileges, execs jackd */
		close(PIPE_READ_FD);

		/* get rid of any supplemental groups */
		if (!getuid () && setgroups (0, 0)) {
			fprintf (stderr, "jackstart: setgroups failed: %s\n", strerror(errno));
			exit (1);
		}

		/* set gid and uid */
		setregid(gid, gid);
		setreuid(uid, uid);
		execvp(jackd_bin_path, argv);
	
		/* we could not start jackd, clean up and exit */
		fprintf(stderr, "jackstart: unable to execute %s: %s\n", jackd_bin_path, strerror(errno));
		close (PIPE_WRITE_FD);
		wait (&err);
		exit (1);
	} else {
		/* child process: grants privileges to jackd */
		close(PIPE_WRITE_FD);

		/* wait for jackd to start */
		while (1) {
		  	int ret;
			char c;

			/* picking up pipe closure is a tricky business. 
			   this seems to work as well as anything else.
			*/

			ret = read(PIPE_READ_FD, &c, 1);
			fprintf (stderr, "back from read, ret = %d errno == %s\n", ret, strerror (errno));
			if (ret == 1) {
			  break;
			} else if (errno != EINTR) {
			  break;
			}
		}

		/* set privileges on jackd process */
		give_capabilities (parent_pid);
	}
	exit (0);
}
