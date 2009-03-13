/**
 * GPL etc.
 *
 * @author Florian Faber
 *
 * @version 0.1 (2009-01-17) [FF]
 *              - initial version
 **/

#include <stdio.h>
#include <jack/systemtest.h>
#include <jack/sanitycheck.h>

int sanitycheck() {
  int errors = 0;
  int relogin = 0;

  if (!system_user_can_rtprio()) {
	  errors++;
	  relogin++;
	  fprintf(stderr, "\nYou are not allowed to set realtime priority.\n");
		  
	  if (!system_has_rtprio_limits_conf()) {
		  errors++;
		  relogin++;
		  fprintf (stderr, "Please check your /etc/security/limits.conf for the following lines\n");
		  fprintf (stderr, "and correct/add them:\n\n");
		  fprintf(stderr, "  @audio          -       rtprio          100\n");
		  fprintf(stderr, "  @audio          -       nice            -10\n");
	  } else if (!system_has_audiogroup()) {
		  errors++;
		  relogin++;
		  fprintf(stderr, "\nYour system has no audio group. Please add it by executing (as root):\n");
		  fprintf(stderr, "  groupadd -r audio\n");
		  fprintf(stderr, "  usermod -a -G audio %s\n", system_get_username());
	  } else if (!system_user_in_audiogroup()) {
		  errors++;
		  relogin++;
		  fprintf(stderr, "\nYour system has an audio group, but you are not a member of it.\n");
		  fprintf(stderr, "Please add yourself to the audio group by executing (as root):\n");
		  fprintf(stderr, "  usermod -a -G audio %s\n", system_get_username());
	  }
  }
  if (system_has_frequencyscaling() && system_uses_frequencyscaling()) {
	  errors++;
	  fprintf(stderr, "\nYour system seems to use frequency scaling. This can have a serious impact\n");
	  fprintf(stderr, "on the audio latency. Please turn it off, e.g. by chosing the 'performance'\n");
	  fprintf(stderr, "governor.\n");
  }
  if (system_memlock_is_unlimited()) {
	  fprintf(stderr, "\nMemory locking is unlimited - this is dangerous. Please alter the line");
	  fprintf(stderr, "  @audio   -  memlock    unlimited");
	  fprintf(stderr, "in your /etc/limits.conf to");
	  fprintf(stderr, "  @audio   -  memlock    %llu\n", (system_available_physical_mem()*3)/4096);
  } else if (0==system_memlock_amount()) {
	  errors++;
	  relogin++;
	  fprintf(stderr, "\nYou are not allowed to lock memory. Please add a line\n");
	  fprintf(stderr, "  @audio   -  memlock    %llu\n", (system_available_physical_mem()*3)/4096);
	  fprintf(stderr, "in your /etc/limits.conf.\n");
  }
  
  if (0<relogin) {
	  fprintf(stderr, "\nAfter applying these changes, please re-login in order for them to take effect.\n");
  }
  
  if (0<errors) {
	  fprintf(stderr, "\nYou don't appear to have a sane system configuration. It is very likely that you\n");
	  fprintf(stderr, "encounter xruns. Please apply all the above mentioned changes and start jack again!\n");
  }
  
  return errors;
}
