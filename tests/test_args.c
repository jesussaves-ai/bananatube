/* What the command line means.
 *
 * The menu runs an app with the entry it found as argv[1]; for this app that is its own marker file,
 * /usr/share/banana/apps/bananatube/BananaTube.app. Opening YouTube from the shelf must not search for that
 * path -- it did, before arg_is_query -- while a search on the command line must still work.
 */
#include "test.h"

int main(void)
{
    /* the marker file the shelf passes: not a query, whatever it is called or wherever the card mounts */
    check(!arg_is_query("/usr/share/banana/apps/bananatube/BananaTube.app"));
    check(!arg_is_query("/data/apps/bananatube/BananaTube.app"));
    check(!arg_is_query("/run/media/mmcblk0p4/foo.app"));
    check(!arg_is_query("BananaTube.app"));
    check(!arg_is_query("./BananaTube.app"));

    /* options are not queries either */
    check(!arg_is_query("--shot"));
    check(!arg_is_query("-v"));

    /* nothing at all: no search */
    check(!arg_is_query(NULL));
    check(!arg_is_query(""));

    /* anything else is what the user asked to search for */
    check(arg_is_query("banana theme"));
    check(arg_is_query("golden sun"));
    check(arg_is_query("RG DS Plus"));
    check(arg_is_query("40% faster"));
    /* a name that merely contains "app" is still a search */
    check(arg_is_query("apples"));
    check(arg_is_query("Happy.apples"));

    test_done();
}
